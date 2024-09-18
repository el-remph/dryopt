#include "../dryopt.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

size_t callback(struct dryopt const * opt __attribute__((unused)), char const * arg) {
	printf("callback saw: %s\n", arg);
	return strlen(arg);
}

// initialised to defaults
short value = 0;
uintmax_t bigvalue = 1;
char * strarg = NULL;
bool flag = false;
double fl = 0.0;
enum { NEVER, AUTO, ALWAYS } e = ALWAYS;
char const *const enum_args[] = { "never", "auto", "always", NULL };
// don't forget to ^ NULL-terminate! pillock

static struct dryopt opts[] = {
	DRYOPT(L'v', "value",	"set value", &value, REQ_ARG, 0),
	DRYOPT(L'b', "bigvalue",	"set bigvalue", &bigvalue, OPT_ARG, 0),
	DRYOPT(L'c', "callback",	"call callback", callback, OPT_ARG, 0),
	DRYOPT(L's', "strarg",	"set strarg", &strarg, OPT_ARG, 0),
	DRYOPT(L'n', "flag",	"boolean; takes no argument", &flag, NO_ARG, 1),
	DRYOPT(L'F', "float",	"set fl (double)", &fl, REQ_ARG, 0),
	// DRYOPT can't be used to init an ENUM_ARG
	{ L'e', "enum", .type = ENUM_ARG, .sizeof_arg = sizeof e,
		.argptr = &e, .enum_args = enum_args }
};

int main(int argc __attribute__((unused)), char *const argv[]) {
	size_t i = DRYOPT_PARSE(argv, opts);
	printf("-v %hd	-b %ju	-s %s	-n %d	-F %g\narguments after options:",
		value, bigvalue, strarg, flag, fl);
	while (argv[i])
		printf("\t%s", argv[i++]);
	putchar('\n');
	return 0;
}
