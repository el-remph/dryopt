/* SPDX-FileCopyrightText:  2024 The Remph <lhr@disroot.org>
   SPDX-License-Identifier: LGPL-3.0-or-later WITH LGPL-3.0-linking-exception */

/*
C version state (inexhaustive and unordered):
C99:	lang:	__VA_ARGS__ and long long are both widely available anyway;
		restrict'd pointers, on the other hand, are not always
	libc:	<stdbool.h>, <float.h>, isfinite(3), printf(3) "%tu"
GNU C:	variadic macro fallback, enum bitfields (widely available and
	definitely a WONTFIX)
*/

#include "dryopt.h"

#include <assert.h>
#include <ctype.h>	/* isspace(3) */
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <locale.h>
#include <math.h>	/* isfinite(3) */
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>	/* exit(3), mbtowc(3), strto{ull,ll,ld}(3), abort(3); planned: bsearch(3), qsort(3) */
#include <string.h>

char const *restrict prognam = NULL;
struct dryopt_config_s dryopt_config = {0};

#if 0
static int
dryopt_cmp(struct dryopt const *restrict const a, struct dryopt const *restrict const b)
{
	if (a->shortopt || b->shortopt)
		/* is it possible this might overflow? What if it does, does
		   it preserve the sign or wrap around? */
		return b->shortopt - a->shortopt;

	if (a->longopt && b->longopt)
		return strcmp(a->longopt, b->longopt);

	return 0; /* default */
}
#endif

static void __attribute__((cold, format(__printf__, 1, 2)))
err_(const char *restrict const fmt, ...)
{
	va_list va;

	dryopt_config.mistakes_were_made = 1;

	if (dryopt_config.autodie == noop)
		return;

	va_start(va, fmt);
	vfprintf(stderr, fmt, va);
	va_end(va);

	if (dryopt_config.autodie == die)
		exit(EXIT_FAILURE);
}

#if __STDC_VERSION__ < 199900l && defined __GNUC__
#  define ERR(fmt, args...) err_("%s: " fmt "\n", prognam, args)
#else
#  define ERR(fmt, ...) err_("%s: " fmt "\n", prognam, __VA_ARGS__)
#endif

#define ENUM_MAP_ENTRY(enum_val) [enum_val] = #enum_val
/* Array size specified, so in the unlikely event that some borked compiler
   decides to make enum values non-linear, we at least get a warning */
static char const *restrict enum_type2str[7] = {
	ENUM_MAP_ENTRY(STR),
	ENUM_MAP_ENTRY(CHAR),
	ENUM_MAP_ENTRY(SIGNED),
	ENUM_MAP_ENTRY(UNSIGNED),
	ENUM_MAP_ENTRY(FLOATING),
	ENUM_MAP_ENTRY(CALLBACK),
	ENUM_MAP_ENTRY(ENUM_ARG)
};

extern void __attribute__((cold, leaf))
auto_help (
	struct dryopt const opts[],
	size_t const optn,
	FILE *restrict const outfile,
	char const *restrict const program_name,
	char const *restrict const help_args,
	char const *restrict const help_extra
) {
	size_t i;

	fprintf(outfile, "Usage: %s [OPTS] %s\n",
		program_name, help_args ? help_args : "[ARGS]");

	if (help_extra)
		fprintf(outfile, "%s\n", help_extra);

	for (i = 0; i < optn; i++) {
		fputs("  ", outfile);
		if (opts[i].shortopt)
			fprintf(outfile, "-%lc", opts[i].shortopt);
		if (opts[i].longopt)
			fprintf(outfile, "%s--%s", opts[i].shortopt ? ", " : "", opts[i].longopt);

		if (opts[i].takes_arg)
			fprintf(outfile, "=%s%s%s",
				opts[i].takes_arg == OPT_ARG ? "[" : "",
				opts[i].type >= CALLBACK ? "ARG" : enum_type2str[opts[i].type],
				opts[i].takes_arg == OPT_ARG ? "]" : "");
		else if (opts[i].type == ENUM_ARG) {
			size_t j;
			fputc('=', outfile);
			for (j = 0; opts[i].enum_args[j]; j++)
				fprintf(outfile, "%s%s", j ? "," : "", opts[i].enum_args[j]);
		} // else no arg

		if (opts[i].helpstr)
			fprintf(outfile, "\t%s", opts[i].helpstr);
		fputc('\n', outfile);
	}
}

static bool __attribute__((__const__))
fits_in_bits(long long unsigned n, size_t const nbits, bool const issigned)
/* similar to C23's stdc_bit_width(), but can deal with signed as well as
   unsigned */
{
	union { long long unsigned u; long long i; } arg = {n};

	// Sets all the bits that we can copy
	long long unsigned const mask = (1llu << (nbits - issigned)) - 1;

	// bitwise abs
	if (issigned && arg.i < 0)
		arg.u = ~arg.u;

	/* test if that fits (unset bits above mask and see
	   if that changes anything) */
	return (arg.u & mask) == arg.u;
}

static bool bigendian;
static bool __attribute__((__const__))
init_bigendian(void)
{
	union {unsigned char c[2]; unsigned short s;} feff = {{ 0xfe, 0xff }};
	switch (feff.s) {
	case 0xfeff:	return true;
	case 0xfffe:	return false;
	default:	abort();
	}
}

static void
copy_word(void *restrict dest, size_t const destz, void const *restrict src, size_t srcz)
{
	memcpy(dest, bigendian ? src + srcz - destz : src, destz);
}

static void
write_optarg(struct dryopt const *restrict const opt, union dryoptarg const arg)
{
	assert(opt->sizeof_arg <= sizeof arg);

	switch (opt->type) {
		// Sometimes .sizeof_arg is ignored. You were warned!
	case STR:
		*(void**)opt->argptr = arg.p;
		break;
	case CHAR:
		/* again, this is unsigned to avoid sign extension. Why don't
		   I just add a field to the union? */
		assert(arg.u <= UCHAR_MAX);
		*(unsigned char*)opt->argptr = (unsigned char)arg.u;
		break;

	case SIGNED: case UNSIGNED: case ENUM_ARG:
		/* We don't use sizeof arg here because arg.f could be 12
		   or 16 bytes, while arg.i and arg.u is typically 8 */
		if (sizeof arg.i != opt->sizeof_arg) {
			assert(sizeof arg.i > opt->sizeof_arg);
			if (!fits_in_bits(arg.u, opt->sizeof_arg * CHAR_BIT, opt->type == SIGNED)) {
				/* signed output should always make sense here,
				   since overflow of the long long sign bit is
				   tested by strtoll(3) earlier */
				ERR("%lld: %s", arg.i, strerror(ERANGE));
				return;
			}
		}
		copy_word(opt->argptr, opt->sizeof_arg, &arg, sizeof arg.u);
		break;

	case FLOATING:
		// floating point format is not as simple as integral :(
		if (sizeof arg.f == opt->sizeof_arg)
			*(double*)opt->argptr = arg.f;
		else {
			assert(opt->sizeof_arg == sizeof(float));
			// TODO: what about subnormal values?
			if (isfinite(arg.f) && (arg.f > FLT_MAX || arg.f < -FLT_MAX)) {
				ERR("%g: %s", arg.f, strerror(ERANGE));
				return;
			}
			*(float*)opt->argptr = (float)arg.f;
		}
		break;

	case CALLBACK:	return; // should already have been handled
	default:	abort();
	}
}

static bool __attribute__((pure))
str_n_isnegative(char const * str)
/* since strtoul(3) and friends don't report underflow on unsigned numbers,
   we must sadly retrace their steps in order to see if a negative number
   was inappropriately passed. Basically just /^\s*-/ */
{
	while (isspace(*str))
		str++;
	return *str == '-';
}

static char *
parse_optarg(struct dryopt const *restrict const opt, char *restrict optstr,
		union dryoptarg *restrict const parsed)
/* returns optstr after the argument was parsed, or NULL if no argument was
   parsed */
{
	bool arg_found = false;

	switch (opt->type) {
	case STR:
		parsed->p = optstr,
		optstr += strlen(optstr),	// STR consumes the whole rest of the string
		arg_found = true;
		break;
	case CHAR:
		if (*optstr)
			// cast is to avoid sign extension
			parsed->u = (unsigned char)*optstr++,
			arg_found = true;
		break;
	case SIGNED: case UNSIGNED: case FLOATING:
		{
			char * endptr = NULL;
			errno = 0;
			switch (opt->type) {
			case SIGNED:
				parsed->i = strtoll(optstr, &endptr, 0);
				break;
			case UNSIGNED:
				parsed->u = strtoull(optstr, &endptr, 0);
				break;
			case FLOATING:
				parsed->f = strtod(optstr, &endptr);
				break;
			default:
				abort();
			}
			if (errno) {
				ERR("%s: %s", optstr, strerror(errno));
				return optstr;
			}
			arg_found = *optstr && optstr != endptr,
			optstr = endptr;
			if (arg_found && opt->type == UNSIGNED && str_n_isnegative(optstr)) {
				ERR("%s: %s", optstr, strerror(ERANGE));
				return optstr;
			}
			break;
		}
	case CALLBACK:
		{
			size_t const consumed = ((dryopt_callback)opt->argptr)(opt, optstr);
			arg_found = !!consumed, optstr += consumed;
			break;
		}
	case ENUM_ARG:
		{
			size_t i;
			for (i = 0; opt->enum_args[i]; i++)
				if (strcmp(optstr, opt->enum_args[i]) == 0) {
					parsed->u = i,
					optstr += strlen(optstr),
					arg_found = true;
					break;
				}
			break;
		}
	default:
		abort();
	}

	return arg_found ? optstr : NULL;
}

static bool __attribute__((__const__))
is_strictly_defined(enum dryarg_tag const type)
{
	switch (type) {
	// what about CALLBACK? or CHAR, requiring the whole string to be one char?
	case SIGNED: case UNSIGNED: case FLOATING:
		return true;
	default:
		return false;
	}
}

#define ARGNFOUND(optfmt, opt)	\
		ERR("missing %s argument to " optfmt,	\
			opts[opti].type >= CALLBACK ? "" : enum_type2str[opts[opti].type],	\
			opt)

// Returns n of arguments consumed from argv
static size_t
parse_longopt(char *const argv[], struct dryopt opts[], size_t const optn)
{
/*	if (dryopt_config.sorting) { //}
		bsearch(longopt, opts, optn, sizeof *opts, */

	size_t opti, argi = 0;
	char	*restrict longopt = argv[argi++],
		* long_arg = NULL;

	if (*longopt == '-' && *++longopt == '-')
		longopt++;

	{
		// find argument
		char *const equals = strpbrk(longopt, "=:");
		if (equals)
			*equals = '\0',
			long_arg = equals + 1;
	}

	if (!long_arg && strncmp(longopt, "no", 2) == 0) {
		/* Could be a negated boolean long option: prioritise
		   checking for that first */
		char * neg_long_opt = longopt + 2;
		if (*neg_long_opt == '-')
			neg_long_opt++;

		for (opti = 0; opti < optn; opti++) {
			if (opts[opti].longopt && opts[opti].type == UNSIGNED
				&& strcmp(neg_long_opt, opts[opti].longopt) == 0)
			{
				memset(opts[opti].argptr, 0, opts[opti].sizeof_arg);
				return argi;
			}
		}
	}

	for (opti = 0; opti < optn; opti++)
		if (opts[opti].longopt && strcmp(longopt, opts[opti].longopt) == 0)
			goto found;

	// fallen through from above loop: not found
	if (strcmp(longopt, "help") == 0) {
		auto_help(opts, optn, stdout, prognam, NULL, NULL);
		exit(EXIT_SUCCESS);
	}
	ERR("unrecognised long option: %s", longopt);
	return argi;

	// inaccessible except by goto label:
found:	if (opts[opti].type == ENUM_ARG)
		opts[opti].takes_arg = REQ_ARG;

	if (opts[opti].takes_arg == NO_ARG)
		if (long_arg)
			// TODO: parse yes|no|true|false|[10] as an argument
			ERR("option --%s does not take an argument", longopt);
		else
			write_optarg(opts + opti, opts[opti].assign_val);
	else {
		char * og_long_arg = long_arg;
		union dryoptarg parsed;

		/* TODO: redundancy between this and the equivalent code in
		   parse_shortopts(); move out and merge into a function */
		if (long_arg)
thru:			long_arg = parse_optarg(opts + opti, long_arg, &parsed);
		else {
			if (opts[opti].takes_arg == OPT_ARG) {
				// peek at next arg (argi was already incremented)
				if (is_strictly_defined(opts[opti].type) && argv[argi]) {
					long_arg = parse_optarg(opts + opti, argv[argi], &parsed);
					if (long_arg && !*long_arg)
						og_long_arg = argv[argi++];
					else
						long_arg = og_long_arg; // is this necessary?
				}
			} else if ((long_arg = argv[argi++]))
				goto thru;
			else
				goto arg_not_found;
		}

		if (long_arg) {
			write_optarg(opts + opti, parsed);
			if (*long_arg)
				ERR("trailing junk after %tu bytes of argument to --%s: %s",
					og_long_arg - long_arg, longopt, og_long_arg);
					// TODO: shouldn't this return?
		} else if (opts[opti].takes_arg == OPT_ARG)
			memset(opts[opti].argptr, 0, opts[opti].sizeof_arg);
		else {
arg_not_found:		ARGNFOUND("--%s", longopt);
			return argi;
		}
	}

	return argi;
}


static size_t
parse_shortopts(char *const argv[], struct dryopt opts[], size_t const optn)
{
	size_t argi = 0, opti;
	char * optstr = argv[argi++];
	if (*optstr == '-')
		optstr++;

	for (;;) {
		wchar_t wc;
		int conv_ret = mbtowc(&wc, optstr, MB_CUR_MAX);
		if (conv_ret <= 0) {
			if (conv_ret < 0)
				ERR("%s: byte %tu of `%s'",
					strerror(errno), optstr - *argv, *argv);
			return argi;
		}
		optstr += conv_ret;

		for (opti = 0; opti < optn; opti++)
			if (opts[opti].shortopt == wc)
				goto found;

		// fallen through at end of loop: not found
		switch (wc) {
		case L'h': case L'?':
			auto_help(opts, optn, stdout, prognam, NULL, NULL);
			exit(EXIT_SUCCESS);
		default:
			ERR("unrecognised option: %lc", wc);
			continue;
		}

found:		union dryoptarg parsed;
		char * new_optstr = NULL;

		// Now we go back to multibyte processing
		if (opts[opti].type == ENUM_ARG)
			opts[opti].takes_arg = REQ_ARG;

		if (opts[opti].takes_arg == NO_ARG) {
			write_optarg(opts + opti, opts[opti].assign_val);
			continue;
		}

		/* An earlier version of this used gotos to skip extraneous
		   tests (like setting new_optstr to NULL, then immediately
		   zero-testing it). Those branches were probably less
		   performant than referring to the same register or even
		   flags twice; a compiler could even optimise some cases
		   out. Moreover, this version is less unreadable
		        ^ That was premature. This is not pretty. TODO: function */
		if (!*optstr) {
			if (opts[opti].takes_arg == OPT_ARG) {
				// peek at next arg (argi was already incremented)
				if (is_strictly_defined(opts[opti].type) && argv[argi]) {
					new_optstr = parse_optarg(opts + opti, argv[argi], &parsed);
					/* if the whole of the next arg was
					   successfully consumed, make the peekahead
					   increment official and permanent, but don't
					   set optstr or this will loop onto next arg */
					if (new_optstr) {
						if (!*new_optstr)
							argi++;
						else
							new_optstr = NULL; // it never happened
					}
				}
			} else {
				optstr = argv[argi++]; // leave new_optstr as NULL
				goto thru;
			}
		} else {
thru:			new_optstr = parse_optarg(opts + opti, optstr, &parsed);
			/* argi wasn't advanced, new_optstr is just part of
			   optstr, so advance optstr */
			if (new_optstr)
				optstr = new_optstr;
		}

		if (new_optstr) {
			write_optarg(opts + opti, parsed);
			if (argi > 1 && optstr && *optstr) {
				ERR("trailing junk after %td bytes of argument to -%lc: %s",
					optstr - argv[argi - 1], wc, argv[argi - 1]);
				return argi;
			}
		} else if (opts[opti].takes_arg == OPT_ARG)
			memset(opts[opti].argptr, 0, opts[opti].sizeof_arg);
		else {
			ARGNFOUND("-%lc", wc);
			return argi;
		}
	}
}

extern size_t
dryopt_parse(char *const argv[], struct dryopt opts[], size_t const optn)
{
	size_t argi = 1;
	prognam = argv[0];
	bigendian = init_bigendian();

#if 0
	if (dryopt_config.sorting == do_sort)
		qsort(opts, optn, sizeof *opts, dryopt_cmp)
#endif

	if (!dryopt_config.no_setlocale)
		setlocale(LC_ALL, "");

	while (argv[argi]) {
		bool islong = false;

		if (argv[argi][0] != '-')
			break;

		if (argv[argi][1] == '-') {
			if (argv[argi][2] == '\0') {
				/* `--' */
				argi++;
				break;
			}
			islong = true;
		}

		argi += (islong ? parse_longopt : parse_shortopts)(argv + argi, opts, optn);
	}

	return argi;
}
