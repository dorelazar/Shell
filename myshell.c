#include "linux/limits.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include "LineParser.c"
typedef struct process
{
    cmdLine *cmd;         /* the parsed command line*/
    pid_t pid;            /* the process id that is running the command*/
    int status;           /* status of the process: RUNNING/SUSPENDED/TERMINATED */
    struct process *next; /* next process in chain */
} process;
#define HISTLEN 20
#define TERMINATED -1
#define RUNNING 1
#define SUSPENDED 0
void updateProcessStatus(process *process_list, int pid, int status)
{
    // fprintf(stdout, "proc first time\n");
    process *curr_process = process_list;
    while (curr_process != NULL)
    {
        if (curr_process->pid == pid)
        {
            curr_process->status = status;
            break;
        }
        curr_process = curr_process->next;
    }
}
void updateProcessList(process **process_list)
{
    process *curr_process = *process_list;
    while (curr_process != NULL)
    {
        if (waitpid(curr_process->pid, NULL, WNOHANG) == -1)
        {
            updateProcessStatus(curr_process, curr_process->pid, TERMINATED);
        }
        curr_process = curr_process->next;
    }
}
void freeProcessList(process *process_list)
{
    process *curr_process = process_list;
    while (curr_process != NULL)
    {
        process *next_process = curr_process->next;
        FREE(curr_process->cmd->inputRedirect);
        FREE(curr_process->cmd->outputRedirect);
        int i;
        for (i = 0; i < curr_process->cmd->argCount; ++i)
            FREE(curr_process->cmd->arguments[i]);
        free(curr_process->cmd);
        free(curr_process);
        curr_process = next_process;
    }
}
void addProcess(process **process_list, cmdLine *cmd, pid_t pid)
{
    process *new_process = malloc(sizeof(process));
    new_process->cmd = cmd;
    new_process->pid = pid;
    new_process->status = RUNNING;
    new_process->next = *process_list;

    // Set the head of the process list to the new process
    *process_list = new_process;
}
void printProcessList(process **process_list)
{
    updateProcessList(process_list);
    process *curr_process = *process_list;
    process *prev_process = NULL;
    printf("INDEX\tPID\tSTATUS\t\tCOMMAND+ARGUMENTS\n");
    int index = 0;
    while (curr_process != NULL)
    {
        char *status_str;
        switch (curr_process->status)
        {
        case TERMINATED:
            status_str = "Terminated";
            break;
        case RUNNING:
            status_str = "Running";
            break;
        case SUSPENDED:
            status_str = "Suspended";
            break;
        default:
            status_str = "Unknown";
            break;
        }
        printf("%-6d\t%-6d\t%-16s\t%s ", index++, curr_process->pid, status_str, curr_process->cmd->arguments[0]);
        for (int i = 1; i < curr_process->cmd->argCount; i++)
        {
            printf("%s ", curr_process->cmd->arguments[i]);
        }
        printf("\n");
        if (curr_process->status == TERMINATED)
        {
            // remove the process from the list
            if (prev_process == NULL)
            {
                *process_list = curr_process->next; // remove the first process in the list
            }
            else
            {
                prev_process->next = curr_process->next; // remove a process from the middle of the list
            }
            process *temp = curr_process;
            curr_process = curr_process->next; // move to the next process
            FREE(temp->cmd->inputRedirect);
            FREE(temp->cmd->outputRedirect);
            for (unsigned int i = 0; i < temp->cmd->argCount; ++i)
                FREE(temp->cmd->arguments[i]);
            FREE(temp->cmd);
            free(temp); // free the memory used by the terminated process
        }
        else
        {
            prev_process = curr_process;
            curr_process = curr_process->next;
        }
    }
}
void execute(cmdLine *pCmdLine, int debug_mode, process **p_list, char *history[HISTLEN], int s, int num_cmds)
{
    if (strncmp(pCmdLine->arguments[0], "cd", 2) == 0)
    {
        if (chdir(pCmdLine->arguments[1]) == -1)
        {
            fprintf(stderr, "chdir failed\n");
        }
    }
    else if (strncmp(pCmdLine->arguments[0], "procs", 5) == 0)
    {
        printProcessList(p_list);
    }
    else if (strncmp(pCmdLine->arguments[0], "kill", 4) == 0)
    {
        if (pCmdLine->argCount > 1)
        {
            int pid_to_kill = atoi(pCmdLine->arguments[1]);
            int killed = kill(pid_to_kill, SIGINT);
            if (killed == 0)
            {
                updateProcessStatus(*p_list, pid_to_kill, TERMINATED);
                printf("killed process with pid: %d\n", pid_to_kill);
            }
        }
    }
    else if (strcmp(pCmdLine->arguments[0], "history") == 0)
    {
        printf("Command history:\n");
        for (int i = 0; i < num_cmds; i++)
        {
            int idx = (s + i) % HISTLEN;
            printf("%d: %s\n", i + 1, history[idx]);
        }
    }
    else if (strncmp(pCmdLine->arguments[0], "wake", 4) == 0)
    {
        if (pCmdLine->argCount > 1)
        {
            int pid_to_wake = atoi(pCmdLine->arguments[1]);
            int woken = kill(pid_to_wake, SIGCONT);
            if (woken == 0)
            {
                updateProcessStatus(*p_list, pid_to_wake, RUNNING);
                printf("woken process with pid: %d\n", pid_to_wake);
            }
        }
    }
    else if (strncmp(pCmdLine->arguments[0], "suspend", 7) == 0)
    {
        if (pCmdLine->argCount > 1)
        {
            int pid_to_suspend = atoi(pCmdLine->arguments[1]);
            int woken = kill(pid_to_suspend, SIGTSTP);
            if (woken == 0)
            {
                updateProcessStatus(*p_list, pid_to_suspend, SUSPENDED);
                printf("suspended process with pid: %d\n", pid_to_suspend);
            }
        }
    }
    else if (pCmdLine->next != NULL) // command has pipe
    {
        int file[2];
        if (pipe(file) < 0)
        {
            perror("pipe");
            return;
        }
        int child1 = fork();
        if (child1 == -1)
        {
            perror("fork");
            return;
        }
        else if (child1 == 0) // child process 1
        {
            if (pCmdLine->inputRedirect != NULL)
            {
                fclose(stdin);
                if (fopen(pCmdLine->inputRedirect, "r") == NULL)
                {
                    perror("Failed to open input file");
                    freeCmdLines(pCmdLine);
                    _exit(1);
                }
            }
            if (pCmdLine->outputRedirect != NULL)
            {
                fprintf(stderr, "Left child cannot have an output redirection\n");
                freeCmdLines(pCmdLine);
                _exit(1);
            }
            close(STDOUT_FILENO);
            dup(file[1]);
            close(file[1]);
            close(file[0]);
            int status = execvp(pCmdLine->arguments[0], pCmdLine->arguments);
            if (status == -1)
            {
                perror("execution failed");
                freeCmdLines(pCmdLine);
                _exit(1);
            }
        }
        else // parent process
        {
            addProcess(p_list, pCmdLine, child1);
            int child2 = fork();
            if (child2 == -1)
            {
                perror("fork bomb");
                freeProcessList(*p_list);
                _exit(1);
            }
            else if (child2 == 0) // child process 2
            {
                if (pCmdLine->next->outputRedirect != NULL)
                {
                    fclose(stdout);
                    if (fopen(pCmdLine->next->outputRedirect, "w") == NULL)
                    {
                        perror("Failed to open output file");
                        freeCmdLines(pCmdLine);
                        _exit(1);
                    }
                }
                if (pCmdLine->next->inputRedirect != NULL)
                {
                    fprintf(stderr, "Right child cannot have an input redirection\n");
                    freeCmdLines(pCmdLine);
                    _exit(1);
                }
                close(STDIN_FILENO);
                dup(file[0]);
                close(file[0]);
                close(file[1]);
                int status = execvp(pCmdLine->next->arguments[0], pCmdLine->next->arguments);
                if (status == -1)
                {
                    perror("execution failed");
                    freeCmdLines(pCmdLine);
                    _exit(1);
                }
            }
            else // parent process
            {
                addProcess(p_list, pCmdLine->next, child2);
                close(file[0]);
                close(file[1]);
                if (pCmdLine->next->blocking == 1)
                {
                    waitpid(child1, NULL, 0);
                    waitpid(child2, NULL, 0);
                    updateProcessStatus(*p_list, child1, TERMINATED);
                    updateProcessStatus(*p_list, child2, TERMINATED);
                }
            }
        }
    }
    else // no pipe
    {
        int pid = fork();
        if (pid == 0) // child
        {
            if (pCmdLine->inputRedirect != NULL)
            {
                fclose(stdin);
                if (fopen(pCmdLine->inputRedirect, "r") == NULL)
                {
                    perror("Failed to open input file\n");
                    for (int i = 0; i < num_cmds; i++)
                    {
                        int idx = (s + i) % HISTLEN;
                        free(history[idx]);
                    }
                    fclose(stdin);
                    // freeProcessList(*p_list);
                    _exit(1);
                }
            }
            if (pCmdLine->outputRedirect != NULL)
            {
                fclose(stdout);
                if (fopen(pCmdLine->outputRedirect, "w") == NULL)
                {
                    perror("Failed to open output file\n");
                    for (int i = 0; i < num_cmds; i++)
                    {
                        int idx = (s + i) % HISTLEN;
                        free(history[idx]);
                    }
                    fclose(stdout);
                    // freeProcessList(*p_list);
                    _exit(1);
                }
            }
            int status = execvp(pCmdLine->arguments[0], pCmdLine->arguments);
            if (status == -1)
            {
                perror("execution failed");
                for (int i = 0; i < num_cmds; i++)
                {
                    int idx = (s + i) % HISTLEN;
                    free(history[idx]);
                }
                freeProcessList(*p_list);
                exit(0);
            }
        }
        else if (pid > 0)
        { // parent
            addProcess(p_list, pCmdLine, pid);
            if (debug_mode == 1)
            {
                fprintf(stderr, "the PID is : %d\nthe Executing command is : %s\n", pid, pCmdLine->arguments[0]);
            }
            if (pCmdLine->blocking == 1)
            {
                waitpid(pid, NULL, 0);
                updateProcessStatus(*p_list, pid, TERMINATED);
            }
        }
    }
}
int main(int argc, char const *argv[])
{
    process *p_list = NULL;
    char buf[PATH_MAX];
    char *history[HISTLEN];
    int s = 0, e = 0;
    int num_cmds = 0;
    while (1)
    {
        printf("Current work directory:%s\n", getcwd(buf, sizeof(buf)));
        char input[2048];
        fgets(input, 2048, stdin);
        input[strcspn(input, "\n")] = '\0';
        if (strcmp(input, "quit") == 0)
        {
            break;
        }
        else
        {
            int debug_mode = 0;
            if (argc > 1 && strncmp(argv[1], "-d", 2) == 0)
            {
                debug_mode = 1;
            }
            if (strcmp(input, "!!") == 0)
            {
                if (num_cmds == 0)
                {
                    printf("No commands in history\n");
                    continue;
                }
                int last_idx = ((e - 1) + HISTLEN) % HISTLEN;
                if (last_idx < 0)
                {
                    last_idx += HISTLEN;
                }
                char *last_cmd = history[last_idx];
                printf("%s\n", last_cmd);
                if (num_cmds < HISTLEN)
                {
                    history[e] = strdup(last_cmd);
                    e = (e + 1) % HISTLEN;
                    num_cmds++;
                }
                else
                {
                    free(history[s]);
                    history[s] = strdup(last_cmd);
                    s = (s + 1) % HISTLEN;
                }
                cmdLine *cmd = parseCmdLines(last_cmd);
                execute(cmd, debug_mode, &p_list, history, s, num_cmds);
                if (strcmp(cmd->arguments[0], "cd") == 0 ||
                    strcmp(cmd->arguments[0], "wake") == 0 ||
                    strcmp(cmd->arguments[0], "suspend") == 0 ||
                    strcmp(cmd->arguments[0], "kill") == 0 ||
                    strcmp(cmd->arguments[0], "history") == 0 ||
                    strcmp(cmd->arguments[0], "procs") == 0)
                {
                    freeCmdLines(cmd);
                }
                continue;
            }

            // handle "!n" command
            if (input[0] == '!' && isdigit(input[1]))
            {
                int n = atoi(&input[1]);
                if (n < 1 || n > num_cmds)
                {
                    printf("Invalid command number\n");
                    continue;
                }
                int cmd_idx = (s + n - 1) % HISTLEN;
                if (cmd_idx < 0)
                {
                    cmd_idx += HISTLEN;
                }
                char *cmd_str = history[cmd_idx];
                printf("%s\n", cmd_str);
                if (num_cmds < HISTLEN)
                {
                    history[e] = strdup(cmd_str);
                    e = (e + 1) % HISTLEN;
                    num_cmds++;
                }
                else
                {
                    free(history[s]);
                    history[s] = strdup(cmd_str);
                    s = (s + 1) % HISTLEN;
                }
                cmdLine *cmd = parseCmdLines(cmd_str);
                execute(cmd, debug_mode, &p_list, history, s, num_cmds);
                if (strcmp(cmd->arguments[0], "cd") == 0 ||
                    strcmp(cmd->arguments[0], "wake") == 0 ||
                    strcmp(cmd->arguments[0], "suspend") == 0 ||
                    strcmp(cmd->arguments[0], "kill") == 0 ||
                    strcmp(cmd->arguments[0], "history") == 0 ||
                    strcmp(cmd->arguments[0], "procs") == 0)
                {
                    freeCmdLines(cmd);
                }
                continue;
            }
            // add command to history
            if (num_cmds < HISTLEN)
            {
                history[e] = strdup(input);
                e = (e + 1) % HISTLEN;
                num_cmds++;
            }
            else
            {
                free(history[s]);
                history[s] = strdup(input);
                s = (s + 1) % HISTLEN;
            }
            // parse and execute command
            cmdLine *cmd = parseCmdLines(input);
            execute(cmd, debug_mode, &p_list, history, s, num_cmds);
            if (strcmp(cmd->arguments[0], "cd") == 0 ||
                strcmp(cmd->arguments[0], "wake") == 0 ||
                strcmp(cmd->arguments[0], "suspend") == 0 ||
                strcmp(cmd->arguments[0], "kill") == 0 ||
                strcmp(cmd->arguments[0], "history") == 0 ||
                strcmp(cmd->arguments[0], "procs") == 0)
            {
                freeCmdLines(cmd);
            }
        }
    }
    for (int i = 0; i < num_cmds; i++)
    {
        int idx = (s + i) % HISTLEN;
        free(history[idx]);
    }
    freeProcessList(p_list);
    return 0;
}