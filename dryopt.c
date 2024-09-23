/* SPDX-FileCopyrightText:  2024 The Remph <lhr@disroot.org>
   SPDX-License-Identifier: LGPL-3.0-or-later WITH LGPL-3.0-linking-exception */

/*
C version state (inexhaustive and unordered):
C99:	lang:	__VA_ARGS__ and long long are both widely available anyway;
		restrict'd pointers, on the other hand, are not always. Some
		mixing of code and declarations
	libc:	<stdbool.h>, isfinite(3), printf(3) "%tu", vsnprintf(3)
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
#include <wchar.h>	/* wcrtomb(3) */

// global defaults
char const	*restrict prognam = NULL,
		*restrict DRYopt_help_args = NULL,
		*restrict DRYopt_help_extra = NULL;
struct dryopt_config_s dryopt_config = { .wrap = 80 };

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
static char const *__attribute__((__const__, returns_nonnull))
enum_type2str(enum dryarg_tag const tag)
{
	/* Array size specified, so in the unlikely event that some borked compiler
	   decides to make enum values non-linear, we at least get a warning */
	static char const *restrict table[FLOATING + 1] = {
		NULL, // DRYOPT_INVALID
		ENUM_MAP_ENTRY(STR),
		ENUM_MAP_ENTRY(CHAR),
		ENUM_MAP_ENTRY(SIGNED),
		ENUM_MAP_ENTRY(UNSIGNED),
		ENUM_MAP_ENTRY(FLOATING)
	};

	return tag >= sizeof table / sizeof *table ? "" : table[tag];
}

static bool __attribute__((__const__))
is_strictly_defined(enum dryarg_tag const type)
{
	switch (type) {
	// what about CHAR, requiring the whole string to be one char?
	case SIGNED: case UNSIGNED: case FLOATING: case CALLBACK:
		return true;
	default:
		return false;
	}
}

static int __attribute__((format(__printf__, 2, 3), nonnull(2)))
print_row_printf_helper(FILE * out, char const * fmt, ...)
{
	int ret;
	va_list va;
	va_start(va, fmt);
	ret = out ? vfprintf(out, fmt, va) : vsnprintf(NULL, 0, fmt, va);
	va_end(va);
	return ret;
}

static int __attribute__((nonnull(1)))
print_help_entry(struct dryopt const *restrict const opt, FILE *restrict const out)
{
	wchar_t shortopt_buf[3] = { L'-' * !!opt->shortopt, opt->shortopt, L'\0' };
//	int const unseen_bytes = wcstombs(NULL, shortopt_buf, 0) - wcslen(shortopt_buf);
	int const unseen_bytes = opt->shortopt ? wcrtomb(NULL, opt->shortopt, NULL) - 1 : 0;
	char const argsep[2] = {
		opt->takes_arg && opt->longopt
		? '='
		: opt->takes_arg == REQ_ARG || (opt->takes_arg == OPT_ARG && is_strictly_defined(opt->type))
			? ' '
			: '\0',
		'\0'
	};
	int ret = print_row_printf_helper (out,
		"  %ls%s%s%s%s%s",
		shortopt_buf,
		opt->shortopt && opt->longopt ? ", " : "",
		opt->longopt ? "--" : "",
		opt->longopt ? opt->longopt : "",
		argsep,
		opt->takes_arg == OPT_ARG ? "[" : ""
	);

	if (opt->type == ENUM_ARG) {
		size_t i = 0;
		for (; opt->enum_args[i]; i++)
			ret += print_row_printf_helper(out, "%s%s", i ? "," : "", opt->enum_args[i]);
	} else if (opt->takes_arg)
		ret += print_row_printf_helper(out, "%s%s",
			opt->type == CALLBACK ? "ARG" : enum_type2str(opt->type),
			opt->takes_arg == OPT_ARG ? "]" : "");
	// else no arg

	if (unseen_bytes > 0)
		ret -= unseen_bytes;

	return ret;
}

static int
break_space(char const *const s, unsigned offset)
/* Looks for a space at which to break the string. Starts at offset
   (ie. length until the margin line) and looks backwards; if unsuccessful,
   looks forwards instead for the rest of the string. Returns the length of
   string to print, which may be the whole thing. `unsigned' is to avoid
   unnecessary sign extension for indices, but the type returned is within
   [0, INT_MAX], so it can be passed to printf(3) */
{
	unsigned const og_offset = offset;

	{
		char const *const t = memchr(s, 0, offset);
		if (t)
			return t - s; // bounded by offset, so no risk of overflow
	}

	do
		if (isspace(s[offset]))
			return offset;
	while (--offset);

	offset = og_offset;
	while (offset++ < INT_MAX && s[offset] && !isspace(offset));
	return offset;
}

static void __attribute__((nonnull))
wrap_help_text(FILE *restrict const out, char const *restrict help_text,
		unsigned const lmargin, unsigned const rmargin, unsigned const already_printed)
{
	int break_offset;

	/* everything before the loop is just an extended first iteration
	   of the loop, accounting for factors that become nonexistent in
	   subsequent iterations (like already_printed) */

	fprintf(out, "%*s", lmargin > already_printed ? lmargin - already_printed : 0, " ");
	if (rmargin < lmargin) {
		fprintf(out, "%s\n", help_text);
		return;
	}

	break_offset = break_space(help_text, rmargin - lmargin);
	fprintf(out, "%.*s\n", break_offset, help_text);

	while (*(help_text += break_offset)) {
		help_text++;	/* if it gets past the above test,
				   !!isspace(*help_text), so skip it */
		break_offset = break_space(help_text, rmargin - lmargin);
		fprintf(out, "%*s%.*s\n", lmargin, " ", break_offset, help_text);
	}
}

extern void __attribute__((cold, leaf))
auto_help (
	struct dryopt opts[],
	size_t const optn,
	FILE *restrict const outfile
) {
	static char const help_entry[] = "  -h, -?, --help";
	int len = sizeof help_entry - 1;
	size_t i;

	// first pass: find longest entry string (`  -o, --option=[ARG]')
	for (i = 0; i < optn; i++) {
		int l;
		/* also cheekily fix ENUM_ARG entries */
		if (opts[i].type == ENUM_ARG)
			opts[i].takes_arg = REQ_ARG;
		if (len < (l = print_help_entry(opts + i, NULL)))
			len = l;
	}

	fprintf(outfile, "Usage: %s [OPTS] %s\n",
		prognam, DRYopt_help_args ? DRYopt_help_args : "[ARGS]");

	if (DRYopt_help_extra)
		fprintf(outfile, "%s\n", DRYopt_help_extra);

	// second pass: actually print
	for (i = 0; i < optn; i++) {
		int const printed = print_help_entry(opts + i, outfile);
		if (printed < 0) {
			perror(prognam);
			continue;
		}

		if (opts[i].helpstr)
			wrap_help_text(outfile, opts[i].helpstr, len + 3, dryopt_config.wrap, printed);
		else
			fputc('\n', outfile);
	}

	fputs(help_entry, outfile);
	wrap_help_text(outfile, "Print this help and exit", len + 3,
			dryopt_config.wrap, sizeof help_entry - 1);
}

static bool __attribute__((__const__))
fits_in_bits(long long unsigned n, size_t const nbits, bool const issigned)
/* similar to C23 `stdc_bit_width(n) <= nbits', but can deal with signed as
   well as unsigned */
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
/* If calling this without first calling parse_optarg() (such as if
   opt->takes_arg == NO_ARG), *BEWARE* opt->type == CALLBACK */
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

#define ARGNFOUND(optfmt, opt)	\
		ERR("missing %s argument to " optfmt, enum_type2str(opts[opti].type), opt)

static bool __attribute__((pure))
opt_is_boolean(struct dryopt const *const opt)
{
	return opt->type == UNSIGNED && opt->takes_arg == NO_ARG
		&& opt->assign_val.u == 1;
}

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
			if (opts[opti].longopt && opt_is_boolean(opts + opti)
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
		auto_help(opts, optn, stdout);
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
		else if (opts[opti].type == CALLBACK)
			((dryopt_callback)opts[opti].argptr)(opts + opti, NULL);
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
				if (is_strictly_defined(opts[opti].type) && (argv[argi] || opts[opti].type == CALLBACK)) {
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
		{
			int conv_ret = mbtowc(&wc, optstr, MB_CUR_MAX);
			if (conv_ret <= 0) {
				if (conv_ret < 0)
					ERR("%s: byte %tu of `%s'",
						strerror(errno), optstr - *argv, *argv);
				return argi;
			}
			optstr += conv_ret;
		}

		for (opti = 0; opti < optn; opti++)
			if (opts[opti].shortopt == wc)
				goto found;

		// fallen through at end of loop: not found
		switch (wc) {
		case L'h': case L'?':
			auto_help(opts, optn, stdout);
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
			if (opts[opti].type == CALLBACK)
				((dryopt_callback)opts[opti].argptr)(opts + opti, NULL);
			else
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
		if (!*optstr)
			if (opts[opti].takes_arg == OPT_ARG) {
				// peek at next arg (argi was already incremented)
				if (is_strictly_defined(opts[opti].type) && (argv[argi] || opts[opti].type == CALLBACK)) {
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
			} else if ((optstr = argv[argi++]))
				goto thru;
			else
				goto arg_not_found;
		else {
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
arg_not_found:		ARGNFOUND("-%lc", wc);
			return argi;
		}
	}
}

static void // will only ever consume one arg
parse_negated_shortopt(char const *restrict optstr, struct dryopt opts[], size_t const optn)
{
	char const *const og_optstr = optstr;
	assert(*optstr == '+');
	optstr++;

	for (;;) {
		size_t opti;
		wchar_t wc;
		{
			int conv_ret = mbtowc(&wc, optstr, MB_CUR_MAX);
			if (conv_ret <= 0) {
				if (conv_ret < 0)
					ERR("%s: byte %tu of `%s'",
						strerror(errno), optstr - og_optstr, og_optstr);
				return;
			}
			optstr += conv_ret;
		}

		for (opti = 0; opti < optn; opti++)
			if (opts[opti].shortopt == wc)
				goto found;

		// fallen through at end of loop: not found
		ERR("unrecognised option: %lc", wc);
		continue;

found:		if (opt_is_boolean(opts + opti))
			memset(opts[opti].argptr, 0, opts[opti].sizeof_arg);
		else
			ERR("can't unset a non-boolean option: %lc", wc);
	}
}

extern size_t
dryopt_parse(char *const argv[], struct dryopt opts[], size_t const optn)
{
	size_t argi = 1;
	if (!prognam)
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

		switch (argv[argi][0]) {
		case '-':
			switch (argv[argi][1]) {
			case '-':
				if (argv[argi][2] == '\0')
					return ++argi;	// `--'
				// else
				islong = true;
				break;
			case 0:
				return argi;	// `-', as in stdin
			}

			argi += (islong ? parse_longopt : parse_shortopts)(argv + argi, opts, optn);
			break;
		case '+':
			if (dryopt_config.plus_negates_bool && argv[argi][1]) {
				parse_negated_shortopt(argv[argi++], opts, optn);
				break;
			} else
				__attribute__((fallthrough));
		default:
			return argi;
		}
	}

	return argi;
}
