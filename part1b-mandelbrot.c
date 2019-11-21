/************************************************************
* Filename: part1b-mandelbrot.c
* Student name: Leung Chun Yin
* Student no.: 3035437939
* Date: Nov 1, 2019
* version: 1.4
* Development platform: Course VM VB6 (Tested on workbench2)
* Compilation: gcc part1b-mandelbrot.c -o 1bmandel -l SDL2 -l m
*************************************************************/


// Using SDL2 and standard IO
// Define _GNU_SOURCE to stop identifier CLOCK_MONOTONIC is undefined warning
#define _GNU_SOURCE
#include <signal.h>
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
int pTask[2];

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

void sigint_handler(int sig)
{ // SIGINT handler
	printf("progess %d is interrupted by ^C. Bye Bye.\n", getpid());
	// Close all pipe ends
	close(pData[1]);
	close(pTask[0]);
	// Terminate the progess
	exit(0);
}

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

void sigusr1_handler(int sig)
{ // SIGUSR1 handler
	// Read a task
	TASK *task = (TASK *)malloc(sizeof(TASK));
	if (read(pTask[0], task, sizeof(TASK)) == -1)
		printf("Child: Encountered read error\n");

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
	free(task);
}

TASK *create_task(int *current_start_row, int tasksize)
{ // Helper function to create a TASK struct and return its pointer
	TASK *task = (TASK *)malloc(sizeof(*task));
	task->start_row = *current_start_row;
	task->num_of_rows = tasksize;
	*current_start_row += tasksize;
	return task;
}

int main(int argc, char *args[])
{
	// Data structure to store the start and end times of the whole program
	struct timespec prog_start_time, prog_end_time;

	// Get the start time
	clock_gettime(CLOCK_MONOTONIC, &prog_start_time);

	// Assign arguements to variables
	int childsize = atoi(args[1]);
	int tasksize = atoi(args[2]);

	// Record the row index for next task
	int current_start_row = 0;

	// Create two pipes for data and task
	pipe(pData);
	pipe(pTask);

	// Install signal handlers
	struct sigaction sa1;
	sigaction(SIGINT, NULL, &sa1);
	sa1.sa_handler = sigint_handler;
	sigaction(SIGINT, &sa1, NULL);
	struct sigaction sa2;
	sigaction(SIGUSR1, NULL, &sa2);
	sa2.sa_handler = sigusr1_handler;
	sigaction(SIGUSR1, &sa2, NULL);

	// pid for fork()
	pid_t pid;

	// pid_array for saving childs' pid and their task counter
	pid_t pid_array[2][childsize];

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
			printf("Child (%d): Start up. Wait for task!\n", getpid());
			// Close unused pipe end.
			close(pTask[1]);
			close(pData[0]);
			// Infinite loop to keep listening signal
			while (1)
				pause();
		}
		else
		{ // Parent will save child's pid and initialize the counter
			pid_array[0][i] = pid;
			pid_array[1][i] = 0;
		}
	}

	// Parent arrives here
	// Sleep 1s to wait all child ready to work
	sleep(1);
	printf("Start collecting the image lines.\n");

	// Close unused pipe end.
	close(pTask[0]);
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

	for (int i = 0; i < childsize; i++)
	{ // Dispatch first job
		TASK *task = create_task(&current_start_row, tasksize);
		// If write success, send SIGUSR1 signal
		if (write(pTask[1], task, sizeof(TASK)) > 0)
			kill(pid_array[0][i], SIGUSR1);
		free(task);
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

			if (msg->child_pid != -1)
			{ // Check if the msg is the data of a last row

				for (int i = 0; i < childsize; i++)
				{ // Add 1 to its child's counter
					if (pid_array[0][i] == msg->child_pid)
						pid_array[1][i]++;
				}

				if (current_start_row < IMAGE_HEIGHT)
				{ // If still have unassigned task
					// If the remaining task is smaller than a normal size(When IMAGE_HEIGHT is not fully divisible by childsize)
					int size;
					if (current_start_row + tasksize > IMAGE_HEIGHT)
						size = IMAGE_HEIGHT - current_start_row;
					else
						size = tasksize;

					// Create the task
					TASK *task = create_task(&current_start_row, size);

					// Write task into task pipe
					if (write(pTask[1], task, sizeof(TASK)) > 0)
						kill(msg->child_pid, SIGUSR1);
					free(task);
				}
			}
		}
	}

	// END COMPUTATION
	free(msg);

	// Close all pipe ends
	close(pTask[1]);
	close(pData[0]);

	for (int i = 0; i < childsize; i++)
	{ // Send SIGINT to all child
		kill(pid_array[0][i], SIGINT);
	}
	for (int i = 0; i < childsize; i++)
	{ // Wait all child terminate itself
		wait(NULL);
	}
	for (int i = 0; i < childsize; i++)
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
