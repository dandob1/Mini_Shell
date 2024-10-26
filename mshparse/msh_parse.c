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
    //if it needs to run in background or no
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
    //shows this is the last command (helpful in parse)
    int final;
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
			free(p->commands[i]);
		}
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

//dont need to do this
char *
msh_pipeline_input(struct msh_pipeline *p)
{
	(void)p;

	return NULL;
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

    //counter/helper vairbales
    char *token;
    char *saveptr;
    size_t count = 1;

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
    tempCommand->args[0] = strdup(tempCommand->program);
    if (tempCommand->args[0] == NULL) {
        free(tempCommand->program);
        free(tempCommand->args);
        return MSH_ERR_NOMEM;
    }

    //keep parsing all of the pieces
    while ((token = strtok_r(NULL, " ", &saveptr)) != NULL) {
        //have more args than we are allowed and free everything (out of bounds)
        if (count >= MSH_MAXARGS) {
            for(size_t j = 0; j < count; j++) {
                free(tempCommand->args[j]);
            }
            free(tempCommand->program);
            free(tempCommand);
            return MSH_ERR_TOO_MANY_ARGS;
        }
        //store the piece and make sure it allocated
        tempCommand->args[count] = strdup(token);
        if (tempCommand->args[count] == NULL) {
            for (size_t j = 0; j < count - 1; j++) {
                free(tempCommand->args[j]);
            }
            free(tempCommand->program);
            free(tempCommand);
            return MSH_ERR_NOMEM;
        }
        //increment count
        count++;
    }
    //store the count
    tempCommand->args[count] = NULL;
    tempCommand->numberArgs = count;

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

    //edge cases to make sure its not missing a command (test case 4)
    size_t len = strlen(token);
    if (len == 0) {
        free(tempString);
        return MSH_ERR_PIPE_MISSING_CMD;
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

    //parsing the pipeline
    while (token != NULL) {
        //allocate a new pipeline and null check/startup
        struct msh_pipeline *pipeline = malloc(sizeof(struct msh_pipeline));
        if (pipeline == NULL) {
            free(tempString);
            return MSH_ERR_NOMEM;
        }
        memset(pipeline->commands, 0, sizeof(pipeline->commands));
        pipeline->num_commands = 0;
        pipeline->background = 0;

        // Split the pipeline at the |
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

//dont need to do this
void
msh_command_file_outputs(struct msh_command *c, char **stdout, char **stderr)
{
	(void)c;
	(void)stdout;
	(void)stderr;
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

//dont need to do this
void
msh_command_putdata(struct msh_command *c, void *data, msh_free_data_fn_t fn)
{
	(void)c;
	(void)data;
	(void)fn;
}

//dont need to do this
void *
msh_command_getdata(struct msh_command *c)
{
	(void)c;

	return NULL;
}
