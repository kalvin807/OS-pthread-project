/************************************************************
* Filename: part1a-mandelbrot.c
* Student name: Leung Chun Yin
* Student no.: 3035437939
* Date: Nov 1, 2019
* version: 1.2
* Development platform: Course VM VB6 (Tested on workbench2)
* Compilation: gcc part1a-mandelbrot.c -o 1amandel -l SDL2 -l m
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
#include <unistd.h>

#include "Mandel.h"
#include "draw.h"

// Create pipe fds
int pData[2];

typedef struct message
{ // Create struct for message
	int row_index;
	float rowdata[IMAGE_WIDTH];
} MSG;

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

int main(int argc, char *args[])
{
	// Data structure to store the start and end times of the whole program
	struct timespec prog_start_time, prog_end_time;

	// Get the start time
	clock_gettime(CLOCK_MONOTONIC, &prog_start_time);

	// Assign arguements to variables
	int childsize = atoi(args[1]);

	// Record the row index for next task
	int current_start_row = 0;
	int numofrow;
	// Create two pipes for data
	pipe(pData);

	// pid for fork()
	pid_t pid;

	// Task counter for dividing workload
	pid_t task[childsize];

	for (int i = 0; i < childsize; i++)
	{ // Initialize the task counter
		task[i] = 0;
		task[i] = 0;
	}

	for (int i = 0; i < IMAGE_HEIGHT; i++)
	{ // Evenly distribute the task counter
		task[i % childsize]++;
	}

	// Create N workers
	for (int i = 0; i < childsize; i++)
	{
		pid = fork();

		if (pid < 0)
		{ // If fork() failed
			printf("fork error!\n");
			exit(1);
		}
		else if (pid == 0)
		{ // Child will arrive here.
			sleep(1);
			// Close unused pipe end.
			close(pData[0]);
			numofrow = task[i];
			break;
		}
		else
		{ // Go to next starting row
			current_start_row += task[i];
		}
	}

	if (pid == 0)
	{
		// Start computing
		struct timespec start_compute, end_compute;
		clock_gettime(CLOCK_MONOTONIC, &start_compute);
		printf("Child (%d): Start the computation ...\n", getpid());
		// Create msg row by row
		for (int y = 0; y < numofrow; y++)
		{
			MSG *msg = create_msg(y + current_start_row);
			// Write this row message to data pipe
			if (write(pData[1], msg, sizeof(MSG)) == -1)
				printf("Child: Encountered write error\n");
			free(msg);
		}
		// Report time usage of this computation
		clock_gettime(CLOCK_MONOTONIC, &end_compute);
		printf(
			"Child (%d): ...completed. Elapse time = %.3f ms\n", getpid(),
			(end_compute.tv_nsec - start_compute.tv_nsec) / 1000000.0 +
				(end_compute.tv_sec - start_compute.tv_sec) * 1000.0);
		close(pData[1]);
		// Terminate the progess
		exit(0);
	}

	// Parent arrives here
	printf("Start collecting the image lines.\n");

	// Close unused pipe end.
	close(pData[1]);

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

	// Counter of overall progress
	int recieved_rows = 0;

	// MSG buffer for reading data from pipe
	MSG *msg = (MSG *)malloc(sizeof(*msg));

	while (recieved_rows < IMAGE_HEIGHT)
	{ // When not all rows recieved

		if (read(pData[0], msg, sizeof(MSG)) > 0)
		{ // Read the data pipe
			recieved_rows++;

			// Copy recieved rowdata to corrsponding section
			memcpy(&pixels[msg->row_index * IMAGE_WIDTH],
				   msg->rowdata,
				   IMAGE_WIDTH * sizeof(float));
		}
	}
	while (wait(NULL) > 0)
	{
		// Wait all child terminate itself
	}
	// END COMPUTATION
	free(msg);
	// Close all pipe ends
	close(pData[0]);
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
