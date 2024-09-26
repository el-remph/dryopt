#include <stdio.h>
#include "../dryopt.h"

enum { foo = 1, bar = 2, mung = 4, snark = 8 };
static unsigned char mask = 0;

#define OPT(n)	\
	{ .longopt = #n, .assign_val.u = n, .type = UNSIGNED,	\
	  .takes_arg = NO_ARG, .set_arg = DRYARG_OR,		\
	  .sizeof_arg = sizeof mask, .argptr = &mask }

struct dryopt opts[] = { OPT(foo), OPT(bar), OPT(mung), OPT(snark) };

int main(int argc __attribute__((unused)), char *const argv[])
{
	DRYOPT_PARSE(argv, opts);
	printf("%d\n", mask);
	return 0;
}
