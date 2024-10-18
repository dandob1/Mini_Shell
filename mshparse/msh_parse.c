#include <msh_parse.h>

void
msh_pipeline_free(struct msh_pipeline *p)
{
	(void)p;
}

void
msh_sequence_free(struct msh_sequence *s)
{
	(void)s;
}

struct msh_sequence *
msh_sequence_alloc(void)
{
	return NULL;
}

char *
msh_pipeline_input(struct msh_pipeline *p)
{
	(void)p;

	return NULL;
}

msh_err_t
msh_sequence_parse(char *str, struct msh_sequence *seq)
{
	(void)str;
	(void)seq;

	return 0;
}

struct msh_pipeline *
msh_sequence_pipeline(struct msh_sequence *s)
{
	(void)s;

	return NULL;
}

struct msh_command *
msh_pipeline_command(struct msh_pipeline *p, size_t nth)
{
	(void)p;
	(void)nth;

	return NULL;
}

int
msh_pipeline_background(struct msh_pipeline *p)
{
	(void)p;

	return 0;
}

int
msh_command_final(struct msh_command *c)
{
	(void)c;

	return 0;
}

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
	(void)c;

	return NULL;
}

char **
msh_command_args(struct msh_command *c)
{
	(void)c;

	return NULL;
}

void
msh_command_putdata(struct msh_command *c, void *data, msh_free_data_fn_t fn)
{
	(void)c;
	(void)data;
	(void)fn;
}

void *
msh_command_getdata(struct msh_command *c)
{
	(void)c;

	return NULL;
}
