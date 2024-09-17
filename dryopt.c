/* SPDX-FileCopyrightText:  2024 The Remph <lhr@disroot.org>
   SPDX-License-Identifier: LGPL-3.0-or-later WITH LGPL-3.0-linking-exception */

/*
C version state (inexhaustive and unordered):
C99:	lang:	__VA_ARGS__ and long long are both widely available anyway;
		restrict'd pointers, on the other hand, are not always
	libc:	strtold(3), <stdbool.h>, <float.h>, isfinite(3), printf(3) "%tu"
GNU C:	variadic macro fallback, enum bitfields (widely available and
	definitely a WONTFIX)
*/

#include "dryopt.h"

#include <assert.h>
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
static char const *restrict enumarg2str[7] = {
	ENUM_MAP_ENTRY(BOOLEAN),
	ENUM_MAP_ENTRY(STR),
	ENUM_MAP_ENTRY(CHAR),
	ENUM_MAP_ENTRY(SIGNED),
	ENUM_MAP_ENTRY(UNSIGNED),
	ENUM_MAP_ENTRY(FLOATING),
	ENUM_MAP_ENTRY(CALLBACK)
};

extern void __attribute__((cold))
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
		fprintf(outfile, "\n%s", help_extra);

	for (i = 0; i < optn; i++) {
		fputs("  ", outfile);
		if (opts[i].shortopt)
			fprintf(outfile, "-%lc", opts[i].shortopt);
		if (opts[i].longopt)
			fprintf(outfile, "%s--%s", opts[i].shortopt ? ", " : "", opts[i].longopt);
		if (opts[i].arg) {
			fputc('=', outfile);
			if (opts[i].optional)
				fputc('[', outfile);
			fputs(opts[i].arg == CALLBACK ? "ARG" : enumarg2str[opts[i].arg], outfile);
			if (opts[i].optional)
				fputc(']', outfile);
		}
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

// This represents a parsed arg, to be written to dryopt.argptr
union parsed_optarg {
	long long signed i;
	long long unsigned u;
	long double f; // f for float
	void * p;
};

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
write_optarg(struct dryopt const *restrict const opt, union parsed_optarg arg)
{
	switch (opt->arg) {
		// Sometimes .sizeof_arg is ignored. You were warned!
	case BOOLEAN:
		assert(arg.i == !!arg.i);
		copy_word(opt->argptr, opt->sizeof_arg, &arg, sizeof arg.i);
		break;
	case STR:
		*(void**)opt->argptr = arg.p;
		break;
	case CHAR:
		/* again, this is unsigned to avoid sign extension. Why don't
		   I just add a field to the union? */
		assert(arg.u <= UCHAR_MAX);
		*(unsigned char*)opt->argptr = (unsigned char)arg.u;
		break;

	case SIGNED: case UNSIGNED:
		/* We don't use sizeof arg here because arg.f could be 12
		   or 16 bytes, while arg.i and arg.u is typically 8 */
		if (sizeof arg.i != opt->sizeof_arg) {
			assert(sizeof arg.i > opt->sizeof_arg);
			if (!fits_in_bits(arg.u, opt->sizeof_arg * CHAR_BIT, opt->arg == SIGNED)) {
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
			*(long double*)opt->argptr = arg.f;
		else {
			long double const
				max = opt->sizeof_arg == sizeof(float) ? FLT_MAX : DBL_MAX;
			// TODO: what about subnormal values?
			if (isfinite(arg.f) && (arg.f > max || arg.f < -max)) {
				ERR("%Lg: %s", arg.f, strerror(ERANGE));
				return;
			}
			switch (opt->sizeof_arg) {
			case sizeof(float):
				*(float*)opt->argptr = (float)arg.f;
				break;
			case sizeof(double):
				*(double*)opt->argptr = (double)arg.f;
				break;
			default:
				abort();
			}
		}
		break;

	case CALLBACK:	return; // should already have been handled
	default:	abort();
	}
}

static char *
parse_optarg(struct dryopt const *restrict const opt, char *restrict optstr)
/* returns optstr after the argument was parsed, or NULL if no argument was
   parsed */
{
	union parsed_optarg parsed;
	bool arg_found = false;

	switch (opt->arg) {
	case BOOLEAN:
		/* boolean short options take no argument. TODO: what
		   about unsetting? */
		parsed.i = 1;
		arg_found = true;
		break;
	case STR:
		parsed.p = optstr,
		optstr += strlen(optstr),	// STR consumes the whole rest of the string
		arg_found = true;
		break;
	case CHAR:
		if (*optstr)
			// cast is to avoid sign extension
			parsed.u = (unsigned char)*optstr++,
			arg_found = true;
		break;
	case SIGNED: case UNSIGNED: case FLOATING:
		{
			char * endptr = NULL;
			errno = 0;
			switch (opt->arg) {
			case SIGNED:
				parsed.i = strtoll(optstr, &endptr, 0);
				break;
			case UNSIGNED:
				parsed.u = strtoull(optstr, &endptr, 0);
				break;
			case FLOATING:
				parsed.f = strtold(optstr, &endptr);
				break;
			default:
				abort();
			}
			if (errno) {
				ERR("%s: %s", optstr, strerror(errno));
				return optstr;
			}
			arg_found = *optstr && optstr != endptr;
			optstr = endptr;
			break;
		}
	case CALLBACK:
		{
			size_t const consumed = ((dryopt_callback)opt->argptr)(opt, optstr);
			arg_found = !!consumed, optstr += consumed;
			break;
		}
	default:
		abort();
	}

	if (arg_found)
		write_optarg(opt, parsed);
	else if (opt->optional)
		memset(opt->argptr, 0, opt->sizeof_arg);
	else
		return NULL;
	return optstr;
}

// Returns n of arguments consumed from argv
static size_t
parse_longopt(char *const argv[], struct dryopt const opts[], size_t const optn)
{
/*	if (dryopt_config.sorting) { //}
		bsearch(longopt, opts, optn, sizeof *opts, */

	size_t opti, argi = 0;
	char	*restrict longopt = argv[argi++],
		* long_arg = NULL;

	if (*longopt == '-')
		if (*++longopt == '-')
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
			if (opts[opti].longopt && opts[opti].arg == BOOLEAN
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
/*	if (strcmp(longopt, "version") == 0)
		auto_version(); */
	ERR("unrecognised long option: %s", longopt);
	return argi;

	// inaccessible except by goto label:
found:	if (opts[opti].arg == BOOLEAN) {
		if (long_arg) {
			// TODO: parse yes|no|true|false|[10] as an argument
			ERR("option --%s does not take an argument", longopt);
			return argi;
		}
		parse_optarg(opts + opti, NULL);
	} else {
		char * og_long_arg;

		if (!long_arg && !opts[opti].optional && !(long_arg = argv[argi++])) {
arg_not_found:		ERR("missing argument for --%s", longopt);
			return argi;
		}

		og_long_arg = long_arg,
		long_arg = parse_optarg(opts + opti, long_arg);
		if (!long_arg)
			goto arg_not_found;
		if (*long_arg)
			ERR("trailing junk after %tu bytes of argument to --%s: %s",
				og_long_arg - long_arg, longopt, og_long_arg);
	}

	return argi;
}


static size_t
parse_shortopts(char *const argv[], struct dryopt const opts[], size_t const optn)
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
		if (wc == L'h' || wc == L'?') {
			auto_help(opts, optn, stdout, prognam, NULL, NULL);
			exit(EXIT_SUCCESS);
		}
		ERR("unrecognised option: %lc", wc);
		continue;

		// Now we go back to multibyte processing
found:		if (opts[opti].arg != BOOLEAN) {
			switch (*optstr) {
			case '\0':
				if (!opts[opti].optional && !(optstr = argv[argi++]))
					goto arg_not_found;
				break;
			case '=': case ':':
				optstr++;
			}
		}

		if (!(optstr = parse_optarg(opts + opti, optstr))) {
arg_not_found:		ERR("missing %s arg to -%lc",
				opts[opti].arg == CALLBACK ? "" : enumarg2str[opts[opti].arg],
				wc);
			return argi;
		}

		if (argi > 1 && optstr && *optstr) {
			ERR("trailing junk after %tu bytes of argument to -%lc: %s",
				optstr - argv[argi - 1], wc, argv[argi - 1]);
			return argi;
		}
	}
}

extern size_t
dryopt_parse(char *const argv[], struct dryopt const opts[], size_t const optn)
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
