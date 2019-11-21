/************************************************************
* Filename: part2-mandelbrot.c
* Student name: Leung Chun Yin
* Student no.: 3035437939
* Date: Nov 19, 2019
* version: 1.0
* Development platform: Course VM VB6 (Tested on workbench2)
* Compilation: gcc -pthread part2-mandelbrot.c -o 2mandel -l SDL2 -l m
*************************************************************/

// Using SDL2 and standard IO
// Define _GNU_SOURCE to stop identifier CLOCK_MONOTONIC is undefined warning
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

#include "Mandel.h"
#include "draw.h"

// Create pipe fds
int pData[2];

typedef struct task
{ // Create struct for task
	int start_row;
	int num_of_rows;
} TASK;

typedef struct message
{ // Create struct for message
	int row_index;
	pid_t child_pid;
	float rowdata[IMAGE_WIDTH];
} MSG;

typedef struct msgpool
{
	int busy;
	MSG *pool;
	size_t len;					// number of items in the buffer
	pthread_mutex_t mutex;		// needed to add/remove data from the buffer
	pthread_cond_t can_produce; // signaled when items are removed
} MSG_P;

typedef struct taskpool
{
	int task_size;
	int pool_size;
	int next_line;
	TASK *pool; // the buffer
	MSG_P *msg_pool;
	size_t len;					// number of items in the buffer
	pthread_mutex_t mutex;		// needed to add/remove data from the buffer
	pthread_cond_t can_produce; // signaled when items are removed
	pthread_cond_t can_consume; // signaled when items are added
} TASK_P;

MSG *create_msg(int row)
{ // Helper function to create a MSG struct and return its pointer
	printf("Child (%d): Creating line %d ...\n", getpid(), row);
	MSG *msg = (MSG *)malloc(sizeof *msg);
	msg->row_index = row;
	for (int x = 0; x < IMAGE_WIDTH; x++)
	{
		// Compute a value for each point c (x, y) in the complex plane
		msg->rowdata[x] = Mandelbrot(x, msg->row_index);
	}
	return msg;
}

TASK *create_task(int *start_row, int task_size)
{												// Helper function to create a TASK struct and return its pointer
	TASK *task = (TASK *)malloc(sizeof(*task)); // Collect finshed message from pipe
	task->start_row = *start_row;
	task->num_of_rows = task_size;
	*start_row += task_size;
	return task;
}

void *producer(void *arg)
{
	TASK_P *t_pool = (TASK_P *)arg;
	while (1)
	{
		//---------------------Atomic Operation---------------------//
		pthread_mutex_lock(&t_pool->mutex);

		if (t_pool->len == t_pool->pool_size)
		{ // full
			// wait until some task are consumed
			pthread_cond_wait(&t_pool->can_produce, &t_pool->mutex);
		}

		TASK *new_task;

		int task_size = (t_pool->next_line + t_pool->task_size >= IMAGE_HEIGHT) ? IMAGE_HEIGHT - t_pool->next_line : t_pool->task_size;

		// Create a new task ( next_line = -1 to hint worker all task dispatched )
		// End producer thread if all task dispatched
		if (t_pool->next_line >= IMAGE_HEIGHT)
		{
			t_pool->next_line = -1;
		}
		else
		{
			new_task = create_task(&t_pool->next_line, task_size);
			// append data to the task pool
			t_pool->pool[t_pool->len] = *new_task;
			t_pool->len++;
		}

		// signal the fact that new items may be consumed
		pthread_cond_signal(&t_pool->can_consume);
		pthread_mutex_unlock(&t_pool->mutex);
		// release queued task memory
		if (t_pool->next_line < 0)
			pthread_exit(NULL);
		//---------------------Atomic Operation---------------------//
	}

	// never reached
	return NULL;
}

// consume random numbers
void *consumer(void *arg)
{
	// Get the task pool
	TASK_P *t_pool = (TASK_P *)arg;
	MSG_P *m_pool = t_pool->msg_pool;

	while (1)
	{
		//---------------------Atomic Operation-------------------//
		pthread_mutex_lock(&t_pool->mutex);

		while (t_pool->len == 0)
		{ // empty
			// wait for new items to be appended to the buffer
			if (t_pool->next_line < 0)
			{
				printf("Child (%d): Bye ...\n", getpid());
				pthread_mutex_unlock(&t_pool->mutex);
				pthread_exit(NULL);
			}
			pthread_cond_wait(&t_pool->can_consume, &t_pool->mutex);
		}

		// grab data
		--t_pool->len;
		TASK *task = &(t_pool->pool[t_pool->len]);

		// signal the fact that new items may be produced
		pthread_cond_signal(&t_pool->can_produce);
		pthread_mutex_unlock(&t_pool->mutex);
		//---------------------Atomic Operation---------------------//
		// end worker thread if start_row == -1 Else consume it
		if (task->start_row > 0)
		{
			// consume a task

			// Start computing
			struct timespec start_compute, end_compute;
			clock_gettime(CLOCK_MONOTONIC, &start_compute);
			printf("Child (%d): Start the computation ...\n", getpid());

			// Create msg row by row
			for (int y = 0; y < task->num_of_rows; y++)
			{
				MSG *msg = create_msg(y + task->start_row);
				if (y == task->num_of_rows - 1)
					msg->child_pid = getpid();
				else
					msg->child_pid = -1;

				//---------------------Atomic Operation---------------------//
				pthread_mutex_lock(&m_pool->mutex);
				while (&m_pool->busy == 1)
				{
					pthread_cond_wait(&m_pool->can_produce, &m_pool->mutex);
				}
				&m_pool->busy == 1;
				m_pool->pool[y + task->start_row] = *msg;
				&m_pool->busy == 0;
				pthread_cond_signal(&m_pool->can_produce);
				pthread_mutex_unlock(&m_pool->mutex);
				//---------------------Atomic Operation---------------------//
			}

			// Report time usage of this computation
			clock_gettime(CLOCK_MONOTONIC, &end_compute);
			printf(
				"Child (%d): ...completed. Elapse time = %.3f ms\n", getpid(),
				(end_compute.tv_nsec - start_compute.tv_nsec) / 1000000.0 +
					(end_compute.tv_sec - start_compute.tv_sec) * 1000.0);
		}

		// Release finished tas		--t_pool->len;k memory
		// End producer thread if all task dispatched
		if (task->start_row < 0)
		{
			close(pData[1]);
			pthread_exit(NULL);
		}
	}

	// never reached
	return NULL;
}
int main(int argc, char *args[])
{
	// Data structure to store the start and end times of the whole program
	struct timespec prog_start_time, prog_end_time;

	// Get the start time
	clock_gettime(CLOCK_MONOTONIC, &prog_start_time);

	// Create data pipe
	pipe(pData);

	// Assign arguements to variables
	int worker_size = atoi(args[1]);
	int task_size = atoi(args[2]);
	int buffer_size = atoi(args[3]);

	MSG_P m_pool = {
		.busy = 0,
		.pool = (MSG *)malloc(sizeof(MSG) * IMAGE_HEIGHT),
		.len = 0,
		.mutex = PTHREAD_MUTEX_INITIALIZER,
		.can_produce = PTHREAD_COND_INITIALIZER,
	};

	TASK_P t_pool = {
		.task_size = task_size,
		.pool_size = buffer_size,
		.next_line = 0,
		.pool = (TASK *)malloc(sizeof(TASK) * buffer_size),
		.msg_pool = &m_pool,
		.len = 0,
		.mutex = PTHREAD_MUTEX_INITIALIZER,
		.can_produce = PTHREAD_COND_INITIALIZER,
		.can_consume = PTHREAD_COND_INITIALIZER};

	// Create thread for producer
	pthread_t prod;
	pthread_create(&prod, NULL, producer, (void *)&t_pool);

	// Create threads array equal to the number of consumer
	pthread_t cons[worker_size];
	for (int i = 0; i < worker_size; i++)
	{
		pthread_create(&cons[i], NULL, consumer, (void *)&t_pool);
	}

	// Wait for the producer thread
	pthread_join(prod, NULL);
	// Wait for all consumer threads
	for (int i = 0; i < worker_size; i++)
	{
		pthread_join(cons[i], NULL);
	}

	// Generate mandelbrot image and store each pixel for later display
	// Each pixel is represented as a value in the range of [0,1]
	// Store the 2D image as a linear array of pixels (in row-major format)
	float *pixels;
	// Allocate memory to store the pixels
	pixels = (float *)malloc(sizeof(float) * IMAGE_WIDTH * IMAGE_HEIGHT);
	if (pixels == NULL)
	{
		printf("Out of memory!!\n");
		exit(1);
	}

	// Collect finshed message from pipe
	// Close unused pipe end
	close(pData[1]);

	// Counter of collection progress
	int recieved_rows = 0;

	// MSG buffer for reading data from pipe
	MSG *msg = (MSG *)malloc(sizeof(*msg));

	while (recieved_rows < IMAGE_HEIGHT)
	{ // When not all rows recieved
		msg = &(m_pool.pool[recieved_rows]);
		// Copy recieved rowdata to corrsponding section
		memcpy(&pixels[msg->row_index * IMAGE_WIDTH],
			   msg->rowdata,
			   IMAGE_WIDTH * sizeof(float));
		recieved_rows++;
	}

	/*for (int i = 0; i < childsize; i++)
	{ // Display child counter result.
		printf(
			"Child progess %d terminated and completed %d tasks.\n",
			pid_array[0][i],
			pid_array[1][i]);
	}
	printf("All Child progesses have completed\n");
	
	// Report timing
	struct rusage child_usage, self_usage;
	if (getrusage(RUSAGE_CHILDREN, &child_usage) == -1)
		printf("Get child time usage error");

	printf(
		"Total time spent by all child progesses in user mode = %.3f ms\n",
		child_usage.ru_utime.tv_usec / 1000000.0 +
			child_usage.ru_utime.tv_sec * 1000.0);
	printf(
		"Total time spent by all child progesses in system mode = %.3f ms\n",
		child_usage.ru_stime.tv_usec / 1000000.0 +
			child_usage.ru_stime.tv_sec * 1000.0);

	if (getrusage(RUSAm_poolGE_SELF, &self_usage) == -1)
		printf("Get self time usage error");

	printf(
		"Total time spent by parent progess in user mode = %.3f ms\n",
		self_usage.ru_utime.tv_usec / 1000000.0 +
			self_usage.ru_utime.tv_sec * 1000.0);
	printf(
		"Total time spent by parent progess in system mode = %.3f ms\n",
		self_usage.ru_stime.tv_usec / 1000000.0 +
			self_usage.ru_stime.tv_sec * 1000.0);

	clock_gettime(CLOCK_MONOTONIC, &prog_end_time);
	printf(
		"Total elapse time measured by parent progess = %.3f ms\n",
		(prog_end_time.tv_nsec - prog_start_time.tv_nsec) / 1000000.0 +
			(prog_end_time.tv_sec - prog_start_time.tv_sec) * 1000.0);
	*/
	printf("Draw the image\n");
	// Draw the image by using the SDL2 library
	DrawImage(pixels, IMAGE_WIDTH, IMAGE_HEIGHT, "Mandelbrot demo", 3000);
	free(pixels);

	return 0;
}
