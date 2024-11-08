#include <msh.h>
#include <msh_parse.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>

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
};

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

            //if theres a input duplicate it
            if (inputfd != STDIN_FILENO) {
                if (dup2(inputfd, STDIN_FILENO) == -1) {
                    perror("dup2");
                    exit(1);
                }
                close(inputfd);
            }

            //if its not the last command set up a pipe
            if (i < p->num_commands - 1) {
                if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
                    perror("dup2");
                    exit(1);
                }
                close(pipefd[0]);
                close(pipefd[1]);
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

    if (!p->background) {
        //wait for child processes
        for (size_t i = 0; i < num_pids; i++) {
            int status;
            if (waitpid(pids[i], &status, 0) == -1) {
                perror("waitpid");
            }
        }
    }

    return;
}


/**
 * `msh_init` is called on initialization. You can place anything
 * you'd like here, but for M2, you'll likely want to set up signal
 * handlers here.
 */
void
msh_init(void)
{
	return;
}
