#include <msh_parse.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

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
    char *input;
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
    //function to free data
    msh_free_data_fn_t fn;
    //redirect output
    char *stdout_file;
    //redirect input
    char *stderr_file;
};

void
msh_pipeline_free(struct msh_pipeline *p)
{
    //null check
	if(p != NULL) {
        //loop through and clear everything including the commands
		for (size_t i = 0; i < p->num_commands; i++) {
			free(p->commands[i]->program);
			for (int j = 0; j < p->commands[i]->numberArgs; j++) {
                    free(p->commands[i]->args[j]);
            }
            free(p->commands[i]->stdout_file);
            free(p->commands[i]->stderr_file);

            if(p->commands[i]->data != NULL && p->commands[i]->fn != NULL) {
                p->commands[i]->fn(p->commands[i]->data);
            }
			free(p->commands[i]);
		}

        free(p->input);
		free(p);
	}
}

void
msh_sequence_free(struct msh_sequence *s)
{
    //null check
	if(s != NULL) {
        //loop and clear each embedded pipeline
		for (size_t i = 0; i < s->num_pipelines; i++) {
			if (s->pipelines[i] != NULL) {
				msh_pipeline_free(s->pipelines[i]);
			}
		}
		free(s);
	}
}

struct msh_sequence *
msh_sequence_alloc(void)
{
    //allocate space
	struct msh_sequence *allocated = malloc(sizeof(struct msh_sequence));
    //null check
	if (allocated == NULL) {
        free(allocated);
		return NULL;
	}
    //initialize
	allocated->num_pipelines = 0;
	memset(allocated->pipelines, 0, sizeof(allocated->pipelines));
	return allocated;
}

/**
 * `msh_pipeline_input` returns the string used as input for the
 * pipeline. Most useful when printing out the "jobs" builtin command
 * output.
 *
 * - `@p` - The borrowed pipeline for which we retrieve the input
 * - `@return` - the borrowed input used to create the pipeline
 */
char *
msh_pipeline_input(struct msh_pipeline *p)
{
    if (p == NULL) {
        return NULL;
    }

	return p->input;
}

static int cmnd_parse(char *str, struct msh_command **command) {
    //string to follow through the command
	struct msh_command *tempCommand = malloc(sizeof(struct msh_command));
    //null check
    if (tempCommand == NULL) {
        return MSH_ERR_NOMEM;
    }
    //initialize
    memset(tempCommand, 0, sizeof(struct msh_command));
    tempCommand->final = 0;
    tempCommand->stdout_file = NULL;
    tempCommand->stderr_file = NULL;
    tempCommand->data = NULL;
    tempCommand->fn = NULL;

    //counter/helper vairbales
    char *token;
    char *saveptr;
    size_t count = 0;

    //get first piece and check its not null
    token = strtok_r(str, " ", &saveptr);
    if (token == NULL) {
        free(tempCommand);
        return MSH_ERR_NO_EXEC_PROG;
    }

    //set the first piece we just got as the program and null check
    tempCommand->program = strdup(token);
    if (tempCommand->program == NULL) {
        free(tempCommand);
        return MSH_ERR_NOMEM;
    }

    //edge test case to make sure first is the name and null check after strdup
    tempCommand->args[count] = strdup(tempCommand->program);
    if (tempCommand->args[count] == NULL) {
        free(tempCommand->program);
        for (size_t j = 0; j < count; j++) {
            free(tempCommand->args[j]);
        }
        free(tempCommand);
        return MSH_ERR_NOMEM;
    }
    count++;

    //keep parsing all of the pieces
    while ((token = strtok_r(NULL, " ", &saveptr)) != NULL) {
        if ((strcmp(token, "1>") == 0) || (strcmp(token, "1>>") == 0) ||
            (strcmp(token, "2>") == 0) || (strcmp(token, "2>>") == 0) || 
            (strcmp(token, ">") == 0) || (strcmp(token, ">>") == 0) ||
            (strcmp(token, "<") == 0)) {

            //next token must be the filename
            char *filename = strtok_r(NULL, " ", &saveptr);
            if (filename == NULL) {
                //error qand free everything since there was no filename
                for (size_t j = 0; j < count; j++) {
                    free(tempCommand->args[j]);
                }
                free(tempCommand->program);
                free(tempCommand);
                return MSH_ERR_NO_REDIR_FILE;
            }

            if (strcmp(token, "<") == 0) {
                //input redirection
                //use data to store stdin filename
                char *infile = strdup(filename);
                if (!infile) {
                    for (size_t j = 0; j < count; j++) {
                        free(tempCommand->args[j]);
                    }
                    free(tempCommand->program);
                    free(tempCommand);
                    return MSH_ERR_NOMEM;
                }
                // put data into command->data
                msh_command_putdata(tempCommand, infile, free);
                continue;
            } 
            int fd;
            int append = 0;

            if (strcmp(token, ">") == 0) {
                fd = 1;
                append = 0;
            } else if (strcmp(token, ">>") == 0) {
                fd = 1;
                append = 1;
            } else if (strcmp(token, "1>") == 0) {
                fd = 1;
                append = 0;
            } else if (strcmp(token, "1>>") == 0) {
                fd = 1;
                append = 1;
            } else if (strcmp(token, "2>") == 0) {
                fd = 2;
                append = 0;
            } else if (strcmp(token, "2>>") == 0) {
                fd = 2;
                append = 1;
            } else {
                for (size_t j = 0; j < count; j++) {
                    free(tempCommand->args[j]);
                }
                free(tempCommand->program);
                free(tempCommand);
                return MSH_ERR_SEQ_REDIR_OR_BACKGROUND_MISSING_CMD;
            }

        
            //stdout handing
            char *filename_dup;
            if (append) {
                filename_dup = malloc(strlen(filename) + 3);
                if (!filename_dup) {
                    for (size_t j = 0; j < count; j++) {
                        free(tempCommand->args[j]);
                    }
                    free(tempCommand->program);
                    free(tempCommand);
                    return MSH_ERR_NOMEM;
                }
                strcpy(filename_dup, ">>");
                strcat(filename_dup, filename);
            } else {
                filename_dup = strdup(filename);
                if (!filename_dup) {
                    for (size_t j = 0; j < count; j++) {
                        free(tempCommand->args[j]);
                    }
                    free(tempCommand->program);
                    free(tempCommand);
                    return MSH_ERR_NOMEM;
                }
            }

            if (fd == 1) {
                //you are already handling one file so error for multiple redirections
                if (tempCommand->stdout_file != NULL) {
                    free(filename_dup);
                    for (size_t j = 0; j < count; j++) {
                        free(tempCommand->args[j]);
                    }
                    free(tempCommand->program);
                    free(tempCommand->stdout_file);
                    free(tempCommand);
                    return MSH_ERR_MULT_REDIRECTIONS;
                }
                tempCommand->stdout_file = filename_dup;
            } else {
                //you already redirected once so now theres an eror
                if (tempCommand->stderr_file != NULL) {
                    free(filename_dup);
                    for (size_t j = 0; j < count; j++) {
                        free(tempCommand->args[j]);
                    }
                    free(tempCommand->program);
                    free(tempCommand->stderr_file);
                    free(tempCommand);
                    return MSH_ERR_MULT_REDIRECTIONS;
                }
                tempCommand->stderr_file = filename_dup;
                //normal argument
            } 
        } else {
            //have more args than we are allowed and free everything
            if (count >= MSH_MAXARGS) {
                for (size_t j = 0; j < count; j++) {
                    free(tempCommand->args[j]);
                }
                free(tempCommand->program);
                free(tempCommand);
                return MSH_ERR_TOO_MANY_ARGS;
            }
            //store the piece and make sure it allocated
            tempCommand->args[count] = strdup(token);
            if (tempCommand->args[count] == NULL) {
                for (size_t j = 0; j < count; j++) {
                    free(tempCommand->args[j]);
                }
                free(tempCommand->program);
                free(tempCommand);
                return MSH_ERR_NOMEM;
            }
            count++;
        }
    }

    //store the count
    tempCommand->args[count] = NULL;
    tempCommand->numberArgs = (int)count;

    *command = tempCommand;
    return 0;

}

msh_err_t
msh_sequence_parse(char *str, struct msh_sequence *seq)
{
    //base cases
	if (str == NULL || seq == NULL) {
        return MSH_ERR_NOMEM;
    }
    //get a helper string and make sure it allocated
    char *tempString = strdup(str);
    if (tempString == NULL) {
        return MSH_ERR_NOMEM;
    }

    //splitting at the ;
    char *savePipeline;
    char *token = strtok_r(tempString, ";", &savePipeline);
    seq->num_pipelines = 0;

    //parsing the pipeline
    while (token != NULL) {
        //edge cases to make sure its not missing a command (test case 4)
        size_t len = strlen(token);

        //trim leading whitespace
        while (len > 0 && isspace((unsigned char)*token)) {
            token++;
            len = strlen(token);
        }

        //trim trailing whitespace
        while (len > 0 && isspace((unsigned char)token[len - 1])) {
            token[len - 1] = '\0';
            len--;
        }
        
        //base case, theres no command
        if (len == 0) {
            free(tempString);
            return MSH_ERR_PIPE_MISSING_CMD;
        //theres 2 | following one another, error
        } else if (token[0] == '|' || token[len - 1] == '|') {
            free(tempString);
            return MSH_ERR_PIPE_MISSING_CMD;
        }
        for (size_t i = 0; i < len; i ++) {
            if (token[i] == '|' && token[i+1] == '|') {
                free(tempString);
                return MSH_ERR_PIPE_MISSING_CMD;
            }
        }

        //allocate a new pipeline and null check/startup
        struct msh_pipeline *pipeline = malloc(sizeof(struct msh_pipeline));
        if (pipeline == NULL) {
            free(tempString);
            return MSH_ERR_NOMEM;
        }
        //initialize the pipeline
        memset(pipeline->commands, 0, sizeof(pipeline->commands));
        pipeline->num_commands = 0;
        pipeline->background = 0;
        pipeline->input = strdup(token);
        if (pipeline->input == NULL) {
            free(pipeline);
            free(tempString);
            return MSH_ERR_NOMEM;
        }

        //check for & at the end of the pipeline
        if (len > 0 && token[len - 1] == '&') {
            // Set the pipeline to run in the background
            pipeline->background = 1;

            //remove '&' and any spaces from the pipeline input
            token[len - 1] = '\0';
            len--;
            while (len > 0 && isspace((unsigned char)token[len - 1])) {
                token[len - 1] = '\0';
                len--;
            }
            
        }

        //split the pipeline at the |
        char *saveptr_command;
        char *command_str = strtok_r(token, "|", &saveptr_command);
        while (command_str != NULL) {
            //get rid of the whitespace
            while (isspace((unsigned char)*command_str)) {
                command_str++;
            }
            //make sure its ont empy and free fi it is
            if (*command_str == '\0') {
                // Free the current pipeline
                msh_pipeline_free(pipeline);
                free(tempString);
                return MSH_ERR_PIPE_MISSING_CMD;
            }
            //make sure we dont have too many commands and free if we do
            if (pipeline->num_commands >= MSH_MAXCMNDS) {
                msh_pipeline_free(pipeline);
                free(tempString);
                return MSH_ERR_TOO_MANY_CMDS;
            }

            //parse the command and call the helper method
            struct msh_command *cmd = NULL;
            int parsed = cmnd_parse(command_str, &cmd);
            if (parsed != 0) {
                msh_pipeline_free(pipeline);
                free(tempString);
                return parsed;
            }

            //put the command in the pipeline and incrememnt the commands
            pipeline->commands[pipeline->num_commands] = cmd;
            pipeline->num_commands++;
            //go to the next command
            command_str = strtok_r(NULL, "|", &saveptr_command);
        }

        //set the final variable to true
        if (pipeline->num_commands > 0) {
            pipeline->commands[pipeline->num_commands - 1]->final = 1;
        }

        //check if we have too many pipelines
        if (seq->num_pipelines >= MSH_MAXCMNDS) {
            msh_pipeline_free(pipeline);
            free(tempString);
            return MSH_ERR_TOO_MANY_CMDS;
        }
        //add the new pipeline to the sequence and increment
        seq->pipelines[seq->num_pipelines] = pipeline;
        seq->num_pipelines++;
        // Move to the next pipeline
        token = strtok_r(NULL, ";", &savePipeline);
    }
    //free what we allocated
    free(tempString);
    return 0;

}

	
//make a seperate method

//free ptr to that str
	//optional: make method substring counting
//allocatr to the pipeline array
//use str_tok to parse into pipeline
	//allocate one pipeline //increment count //put pipeline string
	//use str_tok to parse into command 
		//incrememt count //put command string
		//keep track if this command is the last command
		//use str_tok to parse into args
			//store in args array
/*free(free_ptr); //free the strdup from the top

}
	(void)str;
	(void)seq;

	return 0;*/


/**
 * `msh_sequence_pipeline` dequeues the first pipeline in the sequence.
 *
 * - `@s` - the sequence we're querying
 * - `@return` - return a pointer to the zero-indexed `nth` command in
 *     the pipeline, or `NULL` if `nth` >= the number of commands in
 *     the pipeline. The caller of this function is passed the
 *     ownership for the pipeline, thus must free the pipeline.
 */
struct msh_pipeline *
msh_sequence_pipeline(struct msh_sequence *s)
{
    //null check
	if (s != NULL && s->num_pipelines > 0) {
        //store first pipeling
        struct msh_pipeline *p = s->pipelines[0];
        //move all the other pipelines to the first place
        for (size_t i = 1; i < s->num_pipelines; i++) {
            s->pipelines[i - 1] = s->pipelines[i];
        }
        //set last position to null and decrease the num of pipelines
        s->pipelines[s->num_pipelines - 1] = NULL;
        s->num_pipelines--;
        return p;
	} else {
        return NULL;
	}
}

struct msh_command *
msh_pipeline_command(struct msh_pipeline *p, size_t nth)
{
    //return nth command
	if (nth < p->num_commands) {
        return p->commands[nth];
	} else {
        return NULL;
	}
}

int
msh_pipeline_background(struct msh_pipeline *p)
{
    //return background
    if (p != NULL) {
        return p->background;
	} else {
        return 0;
	}
}

int
msh_command_final(struct msh_command *c)
{
    //return final
	if (c != NULL) {
        return c->final;
    } else {
        return 0;
    }
}

/**
 * `msh_command_file_outputs` returns the files to which the standard
 * output and the standard error should be written, or `NULL` if
 * neither is specified.
 *
 * - `@c` - Command being queried.
 * - `@stdout` - return value to hold the file name to which to send
 *     the standard output of the command, or `NULL` if it should be
 *     passed down the pipeline.
 * - `@stderr` - same as for `stdout`, but for stadard error.
 */
void
msh_command_file_outputs(struct msh_command *c, char **stdout_file, char **stderr_file)
{
    //check if its null
    if (c == NULL) {
        if (stdout_file != NULL) {
            *stdout_file = NULL;
        }
        if (stderr_file != NULL) {
            *stderr_file = NULL;
        }
        return;
    }

    //its not null so redirect to a new file
    if (stdout_file != NULL) {
        *stdout_file = c->stdout_file;
    }
    if (stderr_file != NULL) {
        *stderr_file = c->stderr_file;
    }
}


char *
msh_command_program(struct msh_command *c)
{
    //return program
	if (c != NULL) {
        return c->program;
	} else {
        return NULL;
	}
}

char **
msh_command_args(struct msh_command *c)
{
    //return args
	if (c != NULL) {
		return c->args;
	} else {
		return NULL;
	}
}

/***
 * `msg_command_putdata` and `msh_command_getdata` are functions that
 * enable the shell to store some data for the command, and to
 * retrieve that data later.
 *
 * For example, if the shell wants to track the `pid` of
 * each command along with other data, it could:
 *
 * ```
 * struct proc_data {
 *     pid_t pid;
 *     // ...
 * };
 * // ...
 * struct proc_data *p = malloc(sizeof(struct proc_data));
 * *p = { .pid = child_pid, };
 * msh_command_putdata(c, p, free);
 * // later, when we want to find the process pid
 * if (msh_command_getdata(c)->pid == child_pid) {
 *     // ...
 * }
 * ```
 */

/**
 * `msh_command_putdata` stores `data` with the command. If a
 * previous, different `data` value was passed in, it is freed.
 *
 * - `@c` - The command with which to store the data.
 * - `@data` - client's data that can be stored with a command.
 *     Ownership is passed to the data-structure. Thus, if the
 *     sequence is freed, then the client's data is passed to the
 *     `freefn`.
 * - `@freefn` - the function to be used to free `data`. It is called
 *     to free the `data` if 1. a new `data` value is passed in for a
 *     command, or 2. if the sequence is freed.
 */
void
msh_command_putdata(struct msh_command *c, void *data, msh_free_data_fn_t fn)
{
    //null check and exit if there is no command
	if (c == NULL) {
        return;
    //data exists and has a free funciton you free the current data
    } else if (c->data != NULL && c->fn != NULL) {
        c->fn(c->data);
    }

    //assignt he data and the function
    c->data = data;
    c->fn = fn;
}

/**
 * `msh_command_getdata` returns the previously `put` value, or `NULL`
 * if no value was previously `put`.
 *
 * - `@c` - the command with the associated data.
 * - `@return` - the `data` associated with the command. These data is
 *     borrowed by the client.
 */
void *
msh_command_getdata(struct msh_command *c)
{
    //null check and return the data
	if (c == NULL) {
        return NULL;
    }

	return c->data;
}
