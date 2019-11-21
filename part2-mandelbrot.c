/************************************************************
* Filename: part2-mandelbrot.c
* Student name: Leung Chun Yin
* Student no.: 3035437939
* Date: Nov 19, 2019
* version: 1.0
* Development platform: Course VM VB6 (Tested on workbench2)
* Compilation: gcc part2-mandelbrot.c -o 2mandel -l -lpthread SDL2 -l m
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

typedef struct taskpool
{
	int pool_size;
	TASK *pool;					// the buffer
	size_t len;					// number of items in the buffer
	pthread_mutex_t mutex;		// needed to add/remove data from the buffer
	pthread_cond_t can_produce; // signaled when items are removed
	pthread_cond_t can_consume; // signaled when items are added
} TASK_P;

MSG *create_msg(int row)
{ // Helper function to create a MSG struct and return its pointer
	MSG *msg = (MSG *)malloc(sizeof *msg);
	msg->row_index = row;
	for (int x = 0; x < IMAGE_WIDTH; x++)
	{
		// Compute a value for each point c (x, y) in the complex plane
		msg->rowdata[x] = Mandelbrot(x, msg->row_index);
	}
	return msg;
}

TASK *create_task(int *current_start_row, int tasksize)
{ // Helper function to create a TASK struct and return its pointer
	TASK *task = (TASK *)malloc(sizeof(*task));
	task->start_row = *current_start_row;
	task->num_of_rows = tasksize;
	*current_start_row += tasksize;
	return task;
}

void *producer(void *arg)
{
	TASK_P *buffer = (TASK_P *)arg;

	while (1)
	{
		pthread_mutex_lock(&buffer->mutex);

		if (buffer->len == buffer->pool_size)
		{ // full
			// wait until some task are consumed
			pthread_cond_wait(&buffer->can_produce, &buffer->mutex);
		}

		TASK t;

		// append data to the buffer
		buffer->pool[buffer->len] = t;
		++buffer->len;

		// signal the fact that new items may be consumed
		pthread_cond_signal(&buffer->can_consume);
		pthread_mutex_unlock(&buffer->mutex);
	}

	// never reached
	return NULL;
}

// consume random numbers
void *consumer(void *arg)
{
	TASK_P *buffer = (TASK_P *)arg;

	while (1)
	{

		pthread_mutex_lock(&buffer->mutex);

		while (buffer->len == 0)
		{ // empty
			// wait for new items to be appended to the buffer
			pthread_cond_wait(&buffer->can_consume, &buffer->mutex);
		}

		// grab data
		--buffer->len;

		// signal the fact that new items may be produced
		pthread_cond_signal(&buffer->can_produce);
		pthread_mutex_unlock(&buffer->mutex);
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

	// Assign arguements to variables
	int workersize = atoi(args[1]);
	int tasksize = atoi(args[2]);
	int buffersize = atoi(args[3]);

	TASK_P t_pool = {
		.pool_size = buffersize,
		.pool = (TASK *)malloc(sizeof(TASK) * buffersize),
		.len = 0,
		.mutex = PTHREAD_MUTEX_INITIALIZER,
		.can_produce = PTHREAD_COND_INITIALIZER,
		.can_consume = PTHREAD_COND_INITIALIZER};

	// Create thread for producer
	pthread_t prod;
	pthread_create(&prod, NULL, producer, (void *)&t_pool);

	// Create threads array equal to the number of consumer
	pthread_t cons[workersize];
	for (int i = 0; i < workersize; i++)
	{
		pthread_create(&cons[i], NULL, consumer, (void *)&t_pool);
	}

	// Wait for the producer thread
	pthread_join(prod, NULL);
	// Wait for all consumer threads
	for (int i = 0; i < workersize; i++)
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

	/*for (int i = 0; i < childsize; i++)
	{ // Display child counter result.
		printf(
			"Child progess %d terminated and completed %d tasks.\n",
			pid_array[0][i],
			pid_array[1][i]);
	}
	printf("All Child progesses have completed\n");
	*/
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

	if (getrusage(RUSAGE_SELF, &self_usage) == -1)
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

	printf("Draw the image\n");
	// Draw the image by using the SDL2 library
	DrawImage(pixels, IMAGE_WIDTH, IMAGE_HEIGHT, "Mandelbrot demo", 3000);
	free(pixels);

	return 0;
}
