// Trivial example program with copious --help output

#include <errno.h>
#include <inttypes.h>	// strtoumax(3)
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "../dryopt.h"

#define OPT(c, name, help) DRYOPT(c, #name, help, REQ_ARG, &(name), 0)

int main(int argc __attribute__((unused)), char *const * argv)
{
	int exit_val = 0, width = 0, precision = -1;
	bool prefix = true;
	char fmt[sizeof "%#*.*jb\n"];

	struct dryopt opts[] = {
		OPT(L'w', width,
			"Minimum width of field, padded with spaces."
			" Signedness determines justification direction"),
		OPT(L'p', precision,
			"Minimum number of digits to appear, padded with"
			" leading zeroes if necessary. Negative values == 0"),
		DRYOPT(0, "prefix",
			"Print in printf(3) `alternate form' (typically"
			" with a `0b' prefix on nonzero output). Default: true",
			NO_ARG, &prefix, true)
	};

	DRYopt_help_args = "INTEGER...",
	DRYopt_help_extra =	"\
Print INTEGERs in binary, like GNU printf(3) \"%b\". See printf(3) for more\n\
information." "\n\nOptions:";

	if (!*(argv += DRYOPT_PARSE(argv, opts))) {
		fprintf(stderr, "%s: not enough arguments\n\n", prognam);
		auto_help(opts, sizeof opts / sizeof *opts, stderr);
		return 1;
	}

	sprintf(fmt, "%%%s*.*jb\n", prefix ? "#" : "");

	do {
		errno = 0;
		char * endptr, * warnstr = NULL;
		uintmax_t const n = strtoumax(*argv, &endptr, 0);

		if (errno) {
			warnstr = strerror(errno);
			goto hell;
		}

		if (*argv && *endptr) {
			warnstr = "trailing junk after number";
			goto hell;
		}

		if (printf(fmt, width, precision, n) < 0)
			warnstr = strerror(errno);
		else
			continue;

hell:		exit_val = 1;
		fprintf(stderr, "%s: %s: %s\n", prognam, *argv, warnstr);
	} while (*++argv);

	return exit_val;
}
