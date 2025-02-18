//found on stack overflow to get rid of errors
#define _XOPEN_SOURCE 700

#include <msh.h>
#include <msh_parse.h>

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>

/**
 * A sequence of pipelines. Pipelines are separated by ";"s, enabling
 * a sequence to define a sequence of pipelines that execute one after
 * the other. A pipeline can run in the background, which enables us
 * to move on an execute the next pipeline.
 */
struct msh_sequence {
	struct msh_pipeline *pipelines[MSH_MAXCMNDS];
    size_t num_pipelines;
};

/**
 * A pipeline is a sequence of commands, separated by "|"s. The output
 * of a preceding command (before the "|") gets passed to the input of
 * the next (after the "|").
 */
struct msh_pipeline {
	struct msh_command *commands[MSH_MAXCMNDS];
    size_t num_commands;
    int background;
};

/**
 * Each command corresponds to either a program (in the `PATH`
 * environment variable, see `echo $PATH`), or a builtin command like
 * `cd`. Commands are passed arguments.
 */
struct msh_command {
	char *program;
    char *args[MSH_MAXARGS];
    int numberArgs;
    int final;
    void *data;
    msh_free_data_fn_t fn;
    char *stdout_file;
    char *stderr_file;
};

struct jobs {
    pid_t pid;
    char command[256];
    int waiting;
    int working;
};

struct jobs jobs[50];
volatile pid_t waiting = 0;

//track the pids running
pid_t foreground_pids[20];
size_t foreground_num_pids = 0;

// background pids
pid_t background_pids[20];
size_t num_background_pids = 0;

/**
 * `msh_execute` is called with the parsed pipeline for the shell to
 * execute. If the pipeline doesn't run in the background, this will
 * only return after the pipeline completes.
 */
//execute built-in commands
int execute_builtin(struct msh_command *command) {
    //check if the command is cd
    if (strcmp(command->program, "cd") == 0) {
        //check that there sat least one argument for cd
        if (command->numberArgs < 2) {
            fprintf(stderr, "cd: missing argument\n");
            return 1;
        }

        char *path = command->args[1];
        //handle the ~
        if (path[0] == '~') {
            const char *home = getenv("HOME");
            if (home == NULL) {
                fprintf(stderr, "cd: HOME not set\n");
                return 1;
            }
            //allocate enough space for the expanded path
            size_t homeLenth = strlen(home);
            size_t pathLength = strlen(path);
            char *expandedPath = malloc(homeLenth + pathLength);
            if (expandedPath == NULL) {
                perror("malloc");
                return 1;
            }
            strcpy(expandedPath, home);
            strcat(expandedPath, path + 1); //skip the ~

            //change directory
            if (chdir(expandedPath) != 0) {
                perror("cd");
                free(expandedPath);
                return 1;
            }

            free(expandedPath);
            return 1;
        }

        //directly use chdir without handling other expansions
        if (chdir(path) != 0) {
            perror("cd");
        }
        return 1;
        //check if it wants to exit
    } else if (strcmp(command->program, "exit") == 0) {
        exit(0);
        //bring process to foreground(it doesnt work)
    } else if (strcmp(command->program, "fg") == 0) {
        if (foreground_num_pids == 0) {
            fprintf(stderr, "fg: no current job\n");
            return 1;
        }
        pid_t pid = background_pids[num_background_pids - 1];
        if (kill(pid, SIGCONT) == -1) {
            perror("fg: SIGCONT");
        }

        foreground_pids[foreground_num_pids++] = pid;
        num_background_pids--;

        int status;
        if (waitpid(pid, &status, 0) == -1) {
            perror("fg");
        }

        return 1;

        //suspend a process to the background (it doesnt work)
    } else if (strcmp(command->program, "bg") == 0) {
        if (foreground_num_pids == 0) {
            fprintf(stderr, "bg: no suspended job\n");
            return 1;
        }
        pid_t pid = foreground_pids[foreground_num_pids - 1];
        if (kill(pid, SIGCONT) == -1) {
            perror("bg: SIGCONT");
        } else {
            printf("[%d] resumed in background\n", pid);
            if (num_background_pids < 20) {
                background_pids[num_background_pids++] = pid;
            }
        }
        foreground_num_pids--;

        return 1;
    }
    return 0;
}



void
msh_execute(struct msh_pipeline *p)
{
	//base case
	if (p == NULL || p->num_commands == 0) {
		return;
	}

    //if theres only one command
    if (p->num_commands == 1) {
        struct msh_command *cmd = p->commands[0];
        if (execute_builtin(cmd)) {
            return;
        }
    }

    //store pids of child process
    pid_t pids[MSH_MAXCMNDS];
    //count number of childeren
    size_t num_pids = 0;
    //initial input
    int inputfd = STDIN_FILENO;
    int pipefd[2];

    //execute commands
    for (size_t i = 0; i < p->num_commands; i++) {
        struct msh_command *command = p->commands[i];

        //create a pipe if its not the last command
        if (i < p->num_commands - 1) {
            if (pipe(pipefd) == -1) {
                perror("pipe");
                exit(1);
            }
        }

        pid_t pid = fork();

        //fork error
        if (pid == -1) {
            perror("fork");
            exit(1);
            //child process
        } else if (pid == 0) {

            //handle input redirection and use c->data to store stdin filename
            if (command->data != NULL) {
                char *stdin_file = (char *)command->data;
                int fd = open(stdin_file, O_RDONLY);
                if (fd == -1) {
                    perror("open stdin_file");
                    exit(1);
                }
                if (dup2(fd, STDIN_FILENO) == -1) {
                    perror("dup2 stdin");
                    close(fd);
                    exit(1);
                }
                close(fd);
            } else if (inputfd != STDIN_FILENO) {
                //if theres a input duplicate it
                if (inputfd != STDIN_FILENO) {
                    if (dup2(inputfd, STDIN_FILENO) == -1) {
                        perror("dup2");
                        exit(1);
                    }
                    close(inputfd);
                }
            }

            //handle stderr redirection
            if (command->stderr_file != NULL) {
                int stderr_append = 0;
                char *stderr_filename = command->stderr_file;
                if (strncmp(stderr_filename, ">>", 2) == 0) {
                    stderr_append = 1;
                    stderr_filename += 2; //skip >>
                }

                int flags;
                if (stderr_append) {
                    flags = O_WRONLY | O_CREAT | O_APPEND;
                } else {
                    flags = O_WRONLY | O_CREAT | O_TRUNC;
                }
                int fd = open(stderr_filename, flags, 0666);
                if (fd == -1) {
                    perror("open stderr_file");
                    exit(1);
                }
                if (dup2(fd, STDERR_FILENO) == -1) {
                    perror("dup2 stderr");
                    close(fd);
                    exit(1);
                }
                close(fd);
            }

            //if its not the last command set up a pipe
            if (i < p->num_commands - 1) {
                if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
                    perror("dup2");
                    exit(1);
                }
                close(pipefd[0]);
                close(pipefd[1]);
            } else {
                //last command in the pipeline
                //if stdout_file specified redirect there else it goes to terminal
                if (command->stdout_file != NULL) {
                    int stdout_append = 0;
                    char *stdout_filename = command->stdout_file;
                    if (strncmp(stdout_filename, ">>", 2) == 0) {
                        stdout_append = 1;
                        stdout_filename += 2;
                    }

                    int flags;
                    if (stdout_append) {
                        flags = O_WRONLY | O_CREAT| O_APPEND;
                    } else {
                        flags = O_WRONLY | O_CREAT | O_TRUNC;
                    }
                    int fd = open(stdout_filename, flags, 0666);
                    if (fd == -1) {
                        perror("open stdout_file");
                        exit(1);
                    }
                    if (dup2(fd, STDOUT_FILENO) == -1) {
                        perror("dup2 stdout file");
                        close(fd);
                        exit(1);
                    }
                    close(fd);
                }
            }
            
            //execute command and print if theres an error
            execvp(command->program, command->args);
            perror("execvp");
            exit(1);
        } else {
            //add child pid
            pids[num_pids++] = pid;

            //close the input if its not the standard input
            if (inputfd != STDIN_FILENO) {
                close(inputfd);
            }

            //if its not the last command set up the input for the next command
            if (i < p->num_commands - 1) {
                close(pipefd[1]);
                inputfd = pipefd[0];
            }
        }
    }

    //loop to copy the pids
    for (size_t i = 0; i < num_pids && i < MSH_MAXCMNDS; i++) {
        foreground_pids[i] = pids[i];
    }
    foreground_num_pids = num_pids;

    //wait for child processes
    if (!p->background) {
        for (size_t i = 0; i < num_pids; i++) {
            int status;
            if (waitpid(pids[i], &status, 0) == -1) {
                perror("waitpid");
            }
        }
        foreground_num_pids = 0;
    } else {
        //check if bg works
        for (size_t i = 0; i < num_pids; i++) {
            if (num_background_pids < 20) {
                background_pids[num_background_pids++] = pids[i];
            }
        }
        printf("[%ld] %d\n", num_pids, pids[num_pids - 1]);
    }

    return;
}


/**
 * `msh_init` is called on initialization. You can place anything
 * you'd like here, but for M2, you'll likely want to set up signal
 * handlers here.
 */

//works for cntrl-c
void sigint_handler()
{
    //end foreground processes
    for (size_t i = 0; i < foreground_num_pids; i++) {
        kill(foreground_pids[i], SIGTERM);
    }
}

//for cntrl-z
void sigtstp_handler()
{
    //suspend processes
    for (size_t i = 0; i < foreground_num_pids; i++) {
        kill(foreground_pids[i], SIGTSTP);
        if (num_background_pids < 20) {
            background_pids[num_background_pids++] = foreground_pids[i];
        }
    }
    foreground_num_pids = 0;
}

void
msh_init(void)
{
    //handler for SIGINT
    struct sigaction saint;
    saint.sa_handler = sigint_handler;
    sigemptyset(&saint.sa_mask);
    saint.sa_flags = 0;
    //print error
    if (sigaction(SIGINT, &saint, NULL) == -1) {
        perror("sigaction SIGINT");
        exit(1);
    }

    //handler for SIGTSTP
    struct sigaction satstp;
    satstp.sa_handler = sigtstp_handler;
    sigemptyset(&satstp.sa_mask);
    satstp.sa_flags = 0;

    //pritn error
    if (sigaction(SIGTSTP, &satstp, NULL) == -1) {
        perror("sigaction SIGTSTP");
        exit(1);
    }

    return;

}