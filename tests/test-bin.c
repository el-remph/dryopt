// TODO: non-ASCII option characters, wrapped help text lines

#include "../dryopt.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

size_t callback(struct dryopt const * opt __attribute__((unused)), char const * arg) {
	printf("callback saw: %s\n", arg);
	return arg ? strlen(arg) : 0;
}

// initialised to defaults
int16_t value = 0;
uintmax_t bigvalue = 1;
char * strarg = NULL;
bool flag = false;
double fl = 0.0;
enum { NEVER, AUTO, ALWAYS } e = ALWAYS;
char const *const enum_args[] = { "never", "auto", "always", NULL };
// don't forget to ^ NULL-terminate! pillock

static struct dryopt opts[] = {
	DRYOPT(L'v', "value",	"set value", REQ_ARG, &value, 0),
	DRYOPT(L'b', "bigvalue",	"set bigvalue", OPT_ARG, &bigvalue, 0),
	DRYOPT(L'c', "callback",	"call callback", OPT_ARG, callback, 0),
	DRYOPT(L's', "strarg",	"set strarg", OPT_ARG, &strarg, 0),
	DRYOPT(L'n', "flag",	"boolean; takes no argument", NO_ARG, &flag, 1),
	DRYOPT(L'F', "float",	"set fl (double)", REQ_ARG, &fl, 0),
	// DRYOPT can't be used to init an ENUM_ARG
	{ L'e', "enum", "pick one of a predetermined set of arguments",
		ENUM_ARG, 0, sizeof e, &e, .enum_args = enum_args },
	{ 0, "inval", "crash the program", DRYOPT_INVALID, 0, -1, 0, 0 }
};

int main(int argc __attribute__((unused)), char *const argv[])
{
	size_t i = DRYOPT_PARSE(argv, opts);

	switch (i) {
	case (size_t)-1:
		fprintf(stderr, "%s: Panic!\n", *argv);
		return -1;
	case (size_t)-2:
		fprintf(stderr, "%s: DRYopt error: %s\n", *argv, strerror(errno));
		return 1;
	}

	printf("-v %"PRId16"	-b %"PRIuMAX"	-s %s	-n %d	-F %g\n"
		"arguments after options:",
		value, bigvalue, strarg, flag, fl);
	while (argv[i])
		printf("\t%s", argv[i++]);
	putchar('\n');
	return 0;
}
