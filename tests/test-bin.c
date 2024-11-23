// TODO: non-ASCII option characters

#include "../dryopt.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>	// exit(3), EXIT_FAILURE
#include <string.h>

size_t callback(struct dryopt const * opt __attribute__((unused)), char const * arg) {
	printf("callback saw: %s\n", arg);
	return arg ? strlen(arg) : 0;
}

static size_t process_conf_file(struct dryopt const*, char const*);

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
	DRYOPT(L's', "strarg",	"set strarg", OPT_ARG, &strarg, 0),
	DRYOPT(L'n', "flag",	"boolean; takes no argument", NO_ARG, &flag, 1),
	DRYOPT(L'F', "float",	"set fl (double)", REQ_ARG, &fl, 0),
	// DRYOPT can't be used to init an ENUM_ARG
	{ L'e', "enum", "pick one of a predetermined set of arguments",
		ENUM_ARG, 0, 0, sizeof e, .argptr = &e, .enum_args = enum_args },
	// It can init a CALLBACK, but not within the strictest of ISO C
	{ L'c', "callback", "call callback", CALLBACK, OPT_ARG, .callback = callback },
	{ L'f', "conf-file", "set option values from ARG. Repeated uses of this option are cumulative", CALLBACK, REQ_ARG, .callback = process_conf_file }
};

size_t process_conf_file(struct dryopt const * opt __attribute__((unused)), char const * arg) {
	FILE *const conf = fopen(arg, "r");
	if (!conf) {
		perror(arg);
		exit(EXIT_FAILURE);
	}
	dryopt_config_file(conf, arg, opts, sizeof opts / sizeof *opts);
	if (fclose(conf))
		perror(arg);
	return strlen(arg);
}

int main(int argc __attribute__((unused)), char *const argv[]) {
	size_t i = DRYOPT_PARSE(argv, opts);

	printf("-v %"PRId16"	-b %"PRIuMAX"	-s %s	-n %d	-F %g\n"
		"arguments after options:",
		value, bigvalue, strarg, flag, fl);
	while (argv[i])
		printf("\t%s", argv[i++]);
	putchar('\n');

	// Should strarg be freed? Is it from the heap or the stack?
	if (strarg < argv[0] || strarg > argv[argc - 1])
		free(strarg);

	return 0;
}
