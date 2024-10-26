#include <sunit.h>
#include <msh_parse.h>

#include <string.h>
#include <stdlib.h>

sunit_ret_t
nocmd(void)
{
	struct msh_sequence *s;
	char *input;

	s = msh_sequence_alloc();
	SUNIT_ASSERT("sequence allocation", s != NULL);

	input = "ls |";
	SUNIT_ASSERT("MSH_ERR_PIPE_MISSING_CMD for 'ls |'", msh_sequence_parse(input, s) == MSH_ERR_PIPE_MISSING_CMD);

	msh_sequence_free(s);

	s = msh_sequence_alloc();
	SUNIT_ASSERT("sequence allocation", s != NULL);

	input = "| ls";
	SUNIT_ASSERT("MSH_ERR_PIPE_MISSING_CMD for '| ls'", msh_sequence_parse(input, s) == MSH_ERR_PIPE_MISSING_CMD);

	msh_sequence_free(s);

	return SUNIT_SUCCESS;
}

sunit_ret_t
too_many_cmd(void)
{
	struct msh_sequence *s;
	char input[100] = "";
	int i;

	s = msh_sequence_alloc();
	SUNIT_ASSERT("sequence allocation", s != NULL);

	for (i = 0; i <= MSH_MAXCMNDS + 5; i++) {
		strcat(input, "ls");
		if (i < MSH_MAXCMNDS) {
			strcat(input, " | ");
		}
	}

	SUNIT_ASSERT("MSH_ERR_TOO_MANY_CMDS", msh_sequence_parse(input, s) == MSH_ERR_TOO_MANY_CMDS);
	msh_sequence_free(s);

	return SUNIT_SUCCESS;
}

sunit_ret_t
too_many_args(void)
{
	/* add your own tests here. */
	struct msh_sequence *s = msh_sequence_alloc();
	char input[2048];
	int i;

	strcpy(input, "ls");
	for (i = 0; i <= MSH_MAXARGS; i++) {
		strcat(input, " arg");
	}

	SUNIT_ASSERT("MSH_ERR_TOO_MANY_ARGS", msh_sequence_parse(input, s) == MSH_ERR_TOO_MANY_ARGS);
	msh_sequence_free(s);

	return SUNIT_SUCCESS;
}

int
main(void)
{
	struct sunit_test tests[] = {
		SUNIT_TEST("pipeline with no command after |", nocmd),
		SUNIT_TEST("pipeline with no command before |", nocmd),
		SUNIT_TEST("too many commands", too_many_cmd),
		SUNIT_TEST("too many args", too_many_args),
		/* add your own tests here... */
		SUNIT_TEST_TERM
	};

	sunit_execute("Testing edge cases and errors", tests);

	return 0;
}
