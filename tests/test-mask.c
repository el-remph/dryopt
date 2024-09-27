#include <stdio.h>
#include "../dryopt.h"

enum { foo = 1, bar = 2, mung = 4, snark = 8 };
static unsigned char mask = 0;

#define OPT(c, n)	\
	{ c, #n, .assign_val.u = n, .type = UNSIGNED,	\
	  .takes_arg = NO_ARG, .set_arg = DRYARG_OR,	\
	  .sizeof_arg = sizeof mask, .argptr = &mask }

struct dryopt opts[] = { OPT(L'f', foo), OPT(L'b', bar), OPT(L'm', mung), OPT(L's', snark) };

int main(int argc __attribute__((unused)), char *const argv[])
{
	dryopt_config.plus_negates_bool = 1;
	if (argc - DRYOPT_PARSE(argv, opts)) {
		fputs("extraneous args", stderr);
		return 1;
	}
	printf("%d\n", mask);
	return 0;
}
