#include "linux/limits.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
int main(int argc, char const *argv[])
{
    int file[2];
    pipe(file);
    fprintf(stderr,"(parent_process>forking...)\n");
    int child1 = fork();
    fprintf(stderr,"(parent_process>created process with id: %d)\n",child1);
    if (child1 < 0)
    {
        printf("fork bomb");
        _exit(1);
    }
    else if (child1 == 0)
    { // child process
        fprintf(stderr,"(child1>redirecting stdout to the write end of the pipe…)\n");
        close(STDOUT_FILENO);
        dup(file[1]);
        close(file[1]);
        char *args[] = {"ls", "-l", 0};
        fprintf(stderr,"(child1>going to execute cmd: %s)\n", args[0]);
        execvp(args[0], args);
    }
    else
    {
        fprintf(stderr,"(parent_process>closing the write end of the pipe…)\n");
        close(file[1]);
        int child2 = fork();
        fprintf(stderr,"(parent_process>created process with id: %d)\n",child2);
        if (child2 < 0)
        {
            printf("fork bomb");
            _exit(1);
        }
        else if (child2 == 0)
        {
            fprintf(stderr,"(child2>redirecting stdin to the write end of the pipe…)\n");
            close(STDIN_FILENO);
            dup(file[0]);
            close(file[0]);
            char *args[] = {"tail", "-n", "2", 0};
            fprintf(stderr,"(child2>going to execute cmd: %s)\n", args[0]);
            execvp(args[0], args);
        }
        else
        {
            fprintf(stderr,"(parent_process>closing the read end of the pipe…)\n");
            close(file[0]);
            fprintf(stderr,"(parent_process>waiting for child processes to terminate…)\n");
            waitpid(child1, NULL, 0);
            waitpid(child2, NULL, 0);
        }
    }
    fprintf(stderr,"(parent_process>)>exiting...\n");
    return 0;
}
