/* Portability is not as much of a focus here as this module isn't required
   for the rest of the library to function. Portability liberties taken over
   dryopt.c:
   - language: Mixing of code and data abounds
   - libc: strdup(3) (old as the hills), getline(3) (widely available for
     some years now) */

#define _POSIX_C_SOURCE 200809l // strdup(3), getline(3)

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>	// getline(3)
#include <stdlib.h>	// free(3)
#include <string.h>	// strdup(3), strerror(3)

#include "dryopt.h"
#include "common.h"

#if __STDC_VERSION__ < 199900l && defined __GNUC__
#  define CONF_ERR(fmt, args...) err_("%s: %s: line %llu: " fmt "\n", prognam, filename, line_i, args)
#else
#  define CONF_ERR(fmt, ...) err_("%s: %s: line %llu: " fmt "\n", prognam, filename, line_i, __VA_ARGS__)
#endif

static bool __attribute__((__const__))
is_conf_comment(char const c)
{
	switch (c) {
	case '#': case ';': case 0:
		return true;
	default:
		return false;
	}
}

extern void
dryopt_config_file (
	FILE *restrict const conf, char const *restrict const filename, // used by CONF_ERR
	struct dryopt opts[], size_t const optn
) {
	long long unsigned line_i = 1;
	size_t getline_n = 0;
	ssize_t len;
	char * line = NULL;

	// This doesn't support embedded NUL bytes
	for (; (len = getline(&line, &getline_n, conf)) != -1; line_i++) {
		// In perl: chomp
		line[len - 1] = '\0';

		// In sed: /^(\s*[;#]|$)/d. Or more accurately, s/^\s*//;/^[^;#]/!d
		char *const longopt = eat_space(line);
		if (is_conf_comment(*longopt))
			continue;

		struct found_longopt l = find_longopt(longopt, opts, optn);
		if (l.opti == (size_t)-1 && l.arg == NULL) {
			CONF_ERR("unrecognised option: %s", longopt);
			continue;
		}
		if (l.opti == (size_t)-2)
			continue; // already handled

		if (!l.arg)
			/* there is, ironically, some code repetition between here and
			   parse_longopt() */
			if (opts[l.opti].takes_arg == REQ_ARG)
				CONF_ERR("missing %s argument to `%s'", enum_type2str(opts[l.opti].type), longopt);
			else if (opts[l.opti].type == CALLBACK)
				opts[l.opti].callback(opts + l.opti, NULL);
			else
				write_optarg(opts + l.opti, opts[l.opti].assign_val);
		else
			if (opts[l.opti].takes_arg == NO_ARG)
				CONF_ERR("option `%s' does not take an argument", longopt);
			else if (opts[l.opti].type == STR) {
				/* Have to strdup(3), as if we lose the pointer to the
				   beginning of the buffer, it can't be freed */
				char * newstr = strdup(l.arg);
				if (newstr)
					*(void**)opts[l.opti].argptr = newstr;
				else
					CONF_ERR("%s", strerror(errno));
			} else {
				union dryoptarg parsed;
				char * leftover = parse_optarg(opts + l.opti, l.arg, &parsed);
				if (!leftover)
					CONF_ERR("%s: %s", strerror(EINVAL), l.arg);
				leftover = eat_space(leftover);
				if (!is_conf_comment(*leftover))
					CONF_ERR("byte %lu: trailing junk after argument to option `%s': %s",
						leftover - line, longopt, leftover);
				/* if err_() doesn't exit(3), then imitate parse_longopt()'s
				   behaviour and just carry on */
				write_optarg(opts + l.opti, parsed);
			}
	}

	free(line);
}
