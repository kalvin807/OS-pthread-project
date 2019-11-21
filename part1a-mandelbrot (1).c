/* 2019-20 Programming Project
	part1a-mandelbrot.c 
	Lui Kin Ping (303553736)
	31/10/2019 Version 1.0
	Platform: x2go (Xfce Version 4.12)
	Compilation: gcc part1a-mandelbrot.c -o part1a-mandelbrot.c -l SDL2 -l m
*/

//Using SDL2 and standard IO
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <string.h>
#include <fcntl.h>
#include "Mandel.h"
#include "draw.h"

int main( int argc, char* args[] )
{
	//data structure to store the results of on row of pixels
	typedef struct message {
		float rowdata[IMAGE_WIDTH];
		int row_index;
	} MSG;
	
	//data structure to store the start and end times of the whole program
	struct timespec start_time, end_time;
	//get the start time
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	
	//data structure to store the start and end times of the computation
	struct timespec start_compute, end_compute;
	
	//generate mandelbrot image and store each pixel for later display
	//each pixel is represented as a value in the range of [0,1]
	
	//store the 2D image as a linear array of pixels (in row-major format)
	float * pixels;
	
	
	//allocate memory to store the pixels/
	pixels = (float *) malloc(sizeof(float) * IMAGE_WIDTH * IMAGE_HEIGHT);
	if (pixels == NULL) {
		printf("Out of memory!!\n");
		exit(1);
	}
	
	//compute the mandelbrot image
	//keep track of the execution time - we are going to parallelize this part
	//printf("Start the computation ...\n");
	clock_gettime(CLOCK_MONOTONIC, &start_compute);
    	int x, y;
	float difftime;
	int childs = atoi(args[1]);	// get process number in argument
	int frag = IMAGE_HEIGHT/childs;	// getting how many rows will be processes by one child
	pid_t pid;

	//set pipes for each child and set pipe buffer from 65536 to 1048576 in case the pipe buffer is full too fast
	int pfd[childs][2];
	for (int i = 0; i < childs; i++){
		pipe(pfd[i]);
		fcntl(pfd[i][0], F_SETPIPE_SZ, 1048576);
		fcntl(pfd[i][1], F_SETPIPE_SZ, 1048576);
	}
	
	printf("Start collecting the image lines\n");
	clock_gettime(CLOCK_MONOTONIC, &start_compute);
	for (int i = 0; i < childs; i++){			// fork processes by for loop
		if (fork() == 0){
			printf("Child(%d): Start the computation ...\n",getpid());
			if (i < childs - 1){
				for (y = i*frag; y < (i+1)*frag; y++){			// y = rows, and determine the rows child doing
					MSG* temp = (MSG *) malloc(sizeof(MSG));	
					temp->row_index = y;
					for (x=0; x<IMAGE_WIDTH; x++) {
						temp->rowdata[x] = Mandelbrot(x, y);	// calculate the result to the temp MSG
    					}
					write(pfd[i][1], temp, sizeof(MSG));		// write the result to the child's pipe
				}
			}
			else if (i == childs - 1){
				for (y = i*frag; y < IMAGE_HEIGHT; y++){			// if IMAGE_HEIGHT mod childs != 0, the remaining part will be calculated in the last child
					MSG* temp = (MSG *) malloc(sizeof(MSG));	
					temp->row_index = y;
					for (x=0; x<IMAGE_WIDTH; x++) {
						temp->rowdata[x] = Mandelbrot(x, y);	// calculate the result to the temp MSG
    					}
					write(pfd[i][1], temp, sizeof(MSG));		// write the result to the child's pipe
				}
			}
			struct rusage usage;					// get use time of child
			int ret = getrusage(RUSAGE_SELF,&usage);
			float sec = usage.ru_utime.tv_sec + usage.ru_stime.tv_sec;
			float usec = usage.ru_utime.tv_usec + usage.ru_stime.tv_usec;
			float tot = sec *  1000.0 + usec / 1000.0;
			printf("Child(%d):	...completed. Elapse time = %.3f ms\n",getpid(),tot);
			exit(1);
		}
	}
	for (int i = 0; i < childs; i++){	// read the result to pixels[]
		close(pfd[i][1]);		// close the pipe
			
		MSG * buf;
		buf = (MSG *) malloc(sizeof(MSG));
		while(read(pfd[i][0],buf,sizeof(MSG))){
			for (int k = 0; k < IMAGE_WIDTH; k++){
				pixels[(buf->row_index)*IMAGE_WIDTH+k] = buf->rowdata[k];
			}
		}
		close(pfd[i][0]);
		
	}
	
	for (int i = 0; i < childs; i++){	// wait for the child to finish
		wait(NULL);
	}
	
	printf("All Child processes have completed\n");
	/*
	clock_gettime(CLOCK_MONOTONIC, &end_compute);
	difftime = (end_compute.tv_nsec - start_compute.tv_nsec)/1000000.0 + (end_compute.tv_sec - start_compute.tv_sec)*1000.0;
	printf(" ... completed. Elapse time = %.3f ms\n", difftime);
	*/
	//Report timing

	// get time spent by all child processes in user mode
	struct rusage usage;
	int ret = getrusage(RUSAGE_CHILDREN,&usage);
	float sec = usage.ru_utime.tv_sec;
	float usec = usage.ru_utime.tv_usec;
	float tot = sec *  1000.0 + usec / 1000.0;
	printf("Total time spent by all child processes in user mode = %.3f ms\n",tot);
	
	// get time spent by all child processes in system mode
	sec = usage.ru_stime.tv_sec;
	usec = usage.ru_stime.tv_usec;
	tot = sec *  1000.0 + usec / 1000.0;
	printf("Total time spent by all child processes in system mode = %.3f ms\n",tot);

	// get time spent by parent process in user mode
	struct rusage usage2;
	int ret2 = getrusage(RUSAGE_SELF,&usage2);
	float sec2 = usage2.ru_utime.tv_sec;
	float usec2 = usage2.ru_utime.tv_usec;
	float tot2 = sec2 *  1000.0 + usec2 / 1000.0;
	printf("Total time spent by parent process in user mode = %.3f ms\n",tot2);
	
	// get time spent by parent process in system mode
	sec2 = usage2.ru_stime.tv_sec;
	usec2 = usage2.ru_stime.tv_usec;
	tot2 = sec2 *  1000.0 + usec2 / 1000.0;
	printf("Total time spent by parent process in system mode = %.3f ms\n",tot2);
	
	clock_gettime(CLOCK_MONOTONIC, &end_time);
	difftime = (end_time.tv_nsec - start_time.tv_nsec)/1000000.0 + (end_time.tv_sec - start_time.tv_sec)*1000.0;
	printf("Total elapse time measured by the parent process = %.3f ms\n", difftime);
	
	printf("Draw the image\n");
	//Draw the image by using the SDL2 library
	DrawImage(pixels, IMAGE_WIDTH, IMAGE_HEIGHT, "Mandelbrot demo", 3000);
	
	return 0;
}

