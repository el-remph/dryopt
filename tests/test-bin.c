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

static struct dryopt const opts[] = {
	DRYOPT(L'v', "value",	"set value", &value, 0),
	DRYOPT(L'b', "bigvalue",	"set bigvalue", &bigvalue, 1),
	DRYOPT(L'c', "callback",	"call callback", callback, 1),
	DRYOPT(L's', "strarg",	"set strarg", &strarg, 1),
	DRYOPT(L'n', "flag",	"boolean; takes no argument", &flag, 0),
	DRYOPT(L'F', "float",	"set fl (double)", &fl, 0)
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
