/************************************************************
* Filename: part2-mandelbrot.c
* Student name: Leung Chun Yin
* Student no.: 3035437939
* Date: Nov 24, 2019
* version: 1.5
* Development platform: Course VM VB6 (Tested on workbench2)
* Compilation: gcc -pthread part2-mandelbrot.c -o 2mandel -l SDL2 -l m
*************************************************************/

// Using SDL2 and standard IO
// Define _GNU_SOURCE to stop identifier CLOCK_MONOTONIC is undefined warning
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

#include "Mandel.h"
#include "draw.h"

// Global variables
int task_size; // Size of a task i.e num_of_rows
int next_line; // Next starting line of a task
int pool_size; // Size of the task pool
float *pixels; // Array of storing pixels of the result image

typedef struct task
{					 // Struct of a task
	int start_row;   // The starting row of this task
	int num_of_rows; // Number of row need to work in this task
} TASK;

typedef struct taskpool
{								// Struct of a task pool
	int production_ends;		// Indicator of all tasks are dispatched
	int worker_id;				// Id of next thread id
	int *task_count;			// Array to store finished task's count of each thread
	TASK **pool;				// Task pool
	size_t len;					// Number of items in the pool
	pthread_mutex_t mutex;		// Mutex Lock for add/remove data from the pool
	pthread_cond_t can_produce; // Signaled when items are removed
	pthread_cond_t can_consume; // Signaled when items are added
} TASK_P;

void write_row(int row)
{ // Helper function to write 1 row of pixel
	int base = row * IMAGE_WIDTH;
	for (int x = 0; x < IMAGE_WIDTH; x++)
	{
		pixels[base + x] = Mandelbrot(x, row);
	}
}

TASK *create_task(int *next_line, int task_size)
{ // Helper function to create a TASK struct and return its pointer
	TASK *task = (TASK *)malloc(sizeof(*task));
	task->start_row = *next_line;
	// Check this task will over done the required Image height or not
	task_size = task_size > (IMAGE_HEIGHT - *next_line) ? (IMAGE_HEIGHT - *next_line) : task_size;
	task->num_of_rows = task_size;
	*next_line += task_size;
	return task;
}

// Boss thread to produce task
void *producer(void *arg)
{
	int can_exit = 0;
	TASK_P *t_pool = (TASK_P *)arg;
	while (1)
	{
		TASK *new_task;
		//---------------------Atomic Operation---------------------//
		// Get the pool lock
		pthread_mutex_lock(&t_pool->mutex);

		if (t_pool->len == pool_size)
		{ // Full
			// Wait until some task are consumed
			pthread_cond_wait(&t_pool->can_produce, &t_pool->mutex);
		}

		// Check wheather all task is dispatched
		if (next_line >= IMAGE_HEIGHT)
		{ // production_ends = 1 to hint worker all task dispatched
			t_pool->production_ends = 1;
			can_exit = 1;
		}
		else
		{ // Create a new task
			new_task = create_task(&next_line, task_size);
			// Append data to the task pool
			t_pool->pool[t_pool->len] = new_task;
			t_pool->len++;
		}

		// Signal that new items may be consumed
		pthread_cond_signal(&t_pool->can_consume);
		pthread_mutex_unlock(&t_pool->mutex);
		//---------------------Atomic Operation---------------------//
		// End producer loop when all task is dispatched
		if (can_exit == 1)
			break;
	}
}

// Worker thread to consume task
void *consumer(void *arg)
{
	// Get the task pool
	TASK_P *t_pool = (TASK_P *)arg;
	int task_count = 0;
	int worker_id = -1;
	while (1)
	{
		//---------------------Atomic Operation---------------------//
		pthread_mutex_lock(&t_pool->mutex);

		while (t_pool->len == 0)
		{ // Wait when t_pool is empty
			// Check the production is ended or not
			if (t_pool->production_ends == 1)
			{ // Exit thread and write the final task count
				t_pool->task_count[worker_id] = task_count;
				pthread_mutex_unlock(&t_pool->mutex);
				pthread_exit(NULL);
			}
			pthread_cond_wait(&t_pool->can_consume, &t_pool->mutex);
		}

		// Set the worker id if it is not set yet(i.e id is -1)
		if (worker_id < 0)
		{
			worker_id = t_pool->worker_id;
			t_pool->worker_id++;
		}

		// Consume a task
		--t_pool->len;
		TASK *task = t_pool->pool[t_pool->len];

		// Signal that new items may be produced
		pthread_cond_signal(&t_pool->can_produce);
		pthread_mutex_unlock(&t_pool->mutex);
		//---------------------Atomic Operation---------------------//
		// Start computing once it get the task
		struct timespec start_compute, end_compute;
		clock_gettime(CLOCK_MONOTONIC, &start_compute);
		printf("Child (%d): Start the computation ...\n", worker_id);

		// Write line row by row
		for (int y = 0; y < task->num_of_rows; y++)
		{
			write_row(y + task->start_row);
		}
		task_count++;
		// Report time usage of this computation
		clock_gettime(CLOCK_MONOTONIC, &end_compute);
		printf(
			"Child (%d): ...completed. Elapse time = %.3f ms\n", worker_id,
			(end_compute.tv_nsec - start_compute.tv_nsec) / 1000000.0 +
				(end_compute.tv_sec - start_compute.tv_sec) * 1000.0);
	}
}

// Main entry of this program
int main(int argc, char *args[])
{
	// Data structure to store the start and end times of the whole program
	struct timespec prog_start_time, prog_end_time;

	// Get the start time
	clock_gettime(CLOCK_MONOTONIC, &prog_start_time);

	// Assign arguements' value to global variables and initialize values
	int worker_size = atoi(args[1]);
	task_size = atoi(args[2]);
	pool_size = atoi(args[3]);
	next_line = 0;

	// Generate mandelbrot image and store each pixel for later display
	// Each pixel is represented as a value in the range of [0,1]
	// Store the 2D image as a linear array of pixels (in row-major format)
	// Allocate memory to store the pixels
	pixels = (float *)malloc(sizeof(float) * IMAGE_WIDTH * IMAGE_HEIGHT);
	if (pixels == NULL)
	{
		printf("Out of memory!!\n");
		exit(1);
	}
	// Create a task pool structure and initialize its values
	TASK_P t_pool = {
		.production_ends = 0, // Default is false
		.worker_id = 0,		  // First worker is id 0
		.task_count = malloc(sizeof(int) * worker_size),
		.pool = malloc(sizeof(TASK) * pool_size),
		.len = 0,
		.mutex = PTHREAD_MUTEX_INITIALIZER,
		.can_produce = PTHREAD_COND_INITIALIZER,
		.can_consume = PTHREAD_COND_INITIALIZER};

	// Create threads array equal to the number of worker
	pthread_t cons[worker_size];
	for (int i = 0; i < worker_size; i++)
	{
		pthread_create(&cons[i], NULL, consumer, (void *)&t_pool);
	}

	// Start the producer in current thread
	producer(&t_pool);

	// Wait for all consumer threads
	for (int i = 0; i < worker_size; i++)
	{
		pthread_join(cons[i], NULL);
	}

	for (int i = 0; i < worker_size; i++)
	{ // Display thread task counter result.
		printf(
			"Worker thread %d has terminated and completed %d tasks.\n", i, t_pool.task_count[i]);
	}

	printf("All worker threads have terminated\n");

	// Report timing
	struct rusage usage;
	if (getrusage(RUSAGE_SELF, &usage) == -1)
		printf("Get self time usage error");

	printf(
		"Total time spent by the process and its thread in user mode = %.3f ms\n",
		usage.ru_utime.tv_usec / 1000000.0 +
			usage.ru_utime.tv_sec * 1000.0);
	printf(
		"Total time spent by the process and its thread in system mode = %.3f ms\n",
		usage.ru_stime.tv_usec / 1000000.0 +
			usage.ru_stime.tv_sec * 1000.0);

	clock_gettime(CLOCK_MONOTONIC, &prog_end_time);

	printf(
		"Total elapse time measured by the process = %.3f ms\n",
		(prog_end_time.tv_nsec - prog_start_time.tv_nsec) / 1000000.0 +
			(prog_end_time.tv_sec - prog_start_time.tv_sec) * 1000.0);

	// Draw the image by using the SDL2 library
	printf("Draw the image\n");
	DrawImage(pixels, IMAGE_WIDTH, IMAGE_HEIGHT, "Mandelbrot demo", 3000);
	free(pixels);

	return 0;
}
