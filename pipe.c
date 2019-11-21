#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>

int main()
{
    int p[2];
    pipe(p);

    typedef struct TEST
    {
        int a;
        int b;
    } T;
    pid_t pid;
    pid = fork();
    if (pid > 0)
    {
        close(p[0]);
        T in1;
        in1.a = 1;
        in1.b = 2;
        write(p[1], &in1, sizeof(in1));
        T in2;
        in2.a = 3;
        in2.b = 4;
        write(p[1], &in2, sizeof(in2));
    }
    else
    {
        sleep(1);
        close(p[1]);
        T out;
        read(p[0], &out, sizeof(T));
        printf("a:%d b:%d \n", out.a, out.b);
        read(p[0], &out, sizeof(T));
        printf("a:%d b:%d \n", out.a, out.b);
        read(p[0], &out, sizeof(T));
        printf("a:%d b:%d \n", out.a, out.b);
        exit(0);
    }
    return 0;
}