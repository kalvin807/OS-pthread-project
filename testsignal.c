#define _POSIX_SOURCE
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>

int sigint_set = 0;
int sigusr_set = 0;

void handle_sigint(int sig)
{
    sigint_set = 1;
}

void handle_sigusr1(int sig)
{
    sigusr_set = 1;
}

int main()
{
    pid_t pid_array[5];
    pid_t pid;
    signal(SIGUSR1, handle_sigusr1);
    signal(SIGINT, handle_sigint);
    for (int i = 0; i < 5; i++)
    {
        pid = fork();
        if (pid < 0)
        {
            printf("cannot fork!\n");
        }
        else if (pid == 0)
        { //child
            break;
        }
        else
        {
            pid_array[i] = pid;
        }
    }

    if (pid == 0)
    {
        printf("Hello from child [%d]\n", getpid());
        while (1)
        {
            while (sigusr_set == 0)
                sleep(1);
            if (sigint_set == 1)
                exit(0);
            printf("The child is [%d] now work. \n", getpid());
            sigusr_set = 0;
        }
        printf("The child is [%d] now die. \n", getpid());
        exit(0);
    }
    else
    {
        sleep(2);
        printf("This is the parentProcess id = %d \n", getpid());
        for (int i = 0; i < 5; i++)
        {
            printf("Order loop\n");
            sleep(1);
            int j = kill(pid_array[i], SIGUSR1);
        }
        sleep(5);
        for (int i = 0; i < 5; i++)
        {
            printf("Kill loop\n");
            sleep(1);
            int j = kill(pid_array[i], SIGUSR1);
            j = kill(pid_array[i], SIGINT);
        }
    }
    return 0;
}