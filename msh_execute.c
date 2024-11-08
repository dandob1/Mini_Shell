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

int execute_builtin(struct msh_command *cmd) {
    if (strcmp(cmd->program, "cd") == 0) {
        if (cmd->numberArgs < 2) {
            fprintf(stderr, "cd: missing argument\n");
            return 1;
        }
        // Directly use chdir without handling '~' or other expansions
        if (chdir(cmd->args[1]) != 0) {
            perror("cd");
        }
        return 1;
    } else if (strcmp(cmd->program, "exit") == 0) {
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

    if (p->num_commands == 1) {
        struct msh_command *cmd = p->commands[0];
        if (execute_builtin(cmd)) {
            return;
        }
    }

    pid_t pid;
    pid_t lastPid;
    int input_fd = STDIN_FILENO;
    int pipefd[2];

    //execute commands
    for (size_t i = 0; i < p->num_commands; i++) {
        struct msh_command *command = p->commands[i];

        if (i < p->num_commands - 1) {
            if (pipe(pipefd) == -1) {
                perror("pipe");
                exit(1);
            }
        }

        pid = fork();

        //child process
        if (pid == -1) {
            perror("fork");
            exit(1);
        } else if (pid == 0) {

            if (input_fd != STDIN_FILENO) {
                if (dup2(input_fd, STDIN_FILENO) == -1) {
                    perror("dup2");
                    exit(1);
                }
                close(input_fd);
            }

            if (i < p->num_commands - 1) {
                if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
                    perror("dup2");
                    exit(1);
                }
                close(pipefd[0]);
                close(pipefd[1]);
            }

            execvp(command->program, command->args);
            perror("execvp");
            exit(1);
        } else {

            lastPid = pid;

            if (input_fd != STDIN_FILENO) {
                close(input_fd);
            }

            if (i < p->num_commands - 1) {
                close(pipefd[1]);
                input_fd = pipefd[0];
            }
        }
    }

    if (!p->background) {
        int status;
        if (waitpid(lastPid, &status, 0) == -1) {
            perror("waitpid");
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
