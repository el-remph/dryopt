/* SPDX-FileCopyrightText:  2024 The Remph <lhr@disroot.org>
   SPDX-License-Identifier: LGPL-3.0-or-later WITH LGPL-3.0-linking-exception */

/*
C version state (inexhaustive and unordered):
C99:	lang:	__VA_ARGS__ and long long are both widely available anyway;
		restrict'd pointers, on the other hand, are not always. Little
		bit of designated initialisers
	libc:	<stdbool.h>, isfinite(3), vsnprintf(3)
GNU C:	variadic macro fallback, enum bitfields (widely available and
	definitely a WONTFIX), anonymous union (widely available and
	probably a WONTFIX)
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
#include <stdlib.h>	/* exit(3), strtou?ll(3), abort(3); planned: bsearch(3), qsort(3) */
#include <string.h>
#include <wchar.h>	/* mbrtowc(3), wcrtomb(3) */

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

static bool __attribute__((pure))
opt_is_boolean(struct dryopt const *const opt)
// derived from negated_boolean_longopt() below
{
	return opt->type == UNSIGNED && !opt->takes_arg
		&& ((!opt->set_arg && opt->assign_val.u == 1)
		    || opt->set_arg == DRYARG_OR);
}

static int __attribute__((nonnull(1)))
print_help_entry(struct dryopt const *restrict const opt, FILE *restrict const out)
{
	wchar_t const shortopt_buf[3] = { L'-' * !!opt->shortopt, opt->shortopt, L'\0' };
//	int const unseen_bytes = wcstombs(NULL, shortopt_buf, 0) - wcslen(shortopt_buf);
	mbstate_t ps = {0};
	int const unseen_bytes = opt->shortopt ? wcrtomb(NULL, opt->shortopt, &ps) - 1 : 0;
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
		opt->longopt ? opt_is_boolean(opt) ? "--[no-]" : "--" : "",
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
	memcpy(dest, bigendian ? (char const*)src + srcz - destz : src, destz);
}

static union dryoptarg
get_target(struct dryopt const *const opt)
{
	union dryoptarg target = {0};
	copy_word(&target, sizeof target, opt->argptr, opt->sizeof_arg);
	return target;
}

static void
write_optarg(struct dryopt const *restrict const opt, union dryoptarg arg)
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

		// Should this come before fits_in_bits()?
		if (opt->set_arg) {
			union dryoptarg const target = get_target(opt);
			switch (opt->set_arg) {
			case DRYARG_AND:	arg.u &= target.u; break;
			case DRYARG_OR:		arg.u |= target.u; break;
			case DRYARG_XOR:	arg.u ^= target.u; break;
			default:		abort();
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
			switch (errno) {
			case 0: case EINVAL:
				break;
			default:
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
			size_t const consumed = opt->callback(opt, optstr);
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

static bool
negated_boolean_longopt(char const neg_long_opt[], struct dryopt const *const opt)
{
	if (!(opt->longopt
	   && opt->type == UNSIGNED
	   && !opt->takes_arg
	   && strcmp(neg_long_opt, opt->longopt) == 0))
		return false;

	// Regular boolean
	if (!opt->set_arg && opt->assign_val.u == 1) {
		memset(opt->argptr, 0, opt->sizeof_arg);
		return true;
	}

	// Boolean bit
	if (opt->set_arg == DRYARG_OR) {
		struct dryopt tmp = *opt;
		tmp.set_arg = DRYARG_AND,
		/* negate only the bits that will be set (XOR -1 is
		   negation), to elude, er, my own overflow testing in
		   write_optarg(). Drat */
		tmp.assign_val.u ^= -1ull >> ((sizeof tmp.assign_val.u - tmp.sizeof_arg) * CHAR_BIT);
		write_optarg(&tmp, tmp.assign_val);
		return true;
	}

	return false;
}

static struct optarg_handled {
	/* TODO: how about a system of unsigned x, y; denoting argv[x][y],
	   where we end up? eg.
		struct optarg_handled { size_t x: 1, y: SIZE_WIDTH - 1; } */
	char *restrict new_arg;
	unsigned argi: 1;
} handle_optarg (
	struct dryopt const *restrict const opt,
	char *restrict const arg, char *const rest_argv[]
) {
	struct optarg_handled ret = {0};
	union dryoptarg parsed;
	assert(opt->takes_arg != NO_ARG);

	if (arg)
		ret.new_arg = parse_optarg(opt, arg, &parsed);
	else if (opt->takes_arg == OPT_ARG) {
		// peek at next arg
		if (is_strictly_defined(opt->type)
			&& (rest_argv[ret.argi] || opt->type == CALLBACK))
		{
			ret.new_arg = parse_optarg(opt, rest_argv[ret.argi], &parsed);
			if (ret.new_arg) {
				if (!*ret.new_arg)
					ret.argi++;
				else
					ret.new_arg = NULL; // it never happened
			}
		}
	} else if ((ret.new_arg = rest_argv[ret.argi++]))
		ret.new_arg = parse_optarg(opt, ret.new_arg, &parsed);
	else
		return ret;

	if (ret.new_arg) {
		write_optarg(opt, parsed);
	} else if (opt->takes_arg == OPT_ARG)
		write_optarg(opt, opt->assign_val);
	// else nothing

	return ret;
}

#define CHECK_ARGNFOUND(optfmt, opt)				\
	do if (!oh.new_arg && opts[opti].takes_arg == REQ_ARG)	\
		ERR("missing %s argument to " optfmt, enum_type2str(opts[opti].type), opt);	\
	while (0)
#define CHECK_TRAILING_JUNK(optfmt, opt, og_arg)	\
	do if (oh.new_arg && *oh.new_arg)		\
		ERR("trailing junk after %lu bytes of argument to "optfmt": %s",	\
			(long unsigned)(oh.new_arg - (og_arg)), opt, (og_arg));	\
	while (0)

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

	for (opti = 0; opti < optn; opti++)
		if (opts[opti].longopt && strcmp(longopt, opts[opti].longopt) == 0)
			goto found;

	if (!long_arg && strncmp(longopt, "no", 2) == 0) {
		/* Could be a negated boolean long option */
		char * neg_long_opt = longopt + 2;
		if (*neg_long_opt == '-')
			neg_long_opt++;

		for (opti = 0; opti < optn; opti++)
			if (negated_boolean_longopt(neg_long_opt, opts + opti))
				return argi;
	}

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
			opts[opti].callback(opts + opti, NULL);
		else
			write_optarg(opts + opti, opts[opti].assign_val);
	else {
		struct optarg_handled const oh =
			handle_optarg(opts + opti, long_arg, argv + argi);
		CHECK_ARGNFOUND("--%s", longopt);
		argi += oh.argi;
		CHECK_TRAILING_JUNK("--%s", longopt, long_arg);
	}

	return argi;
}


static size_t
parse_shortopts(char *const argv[], struct dryopt opts[], size_t const optn)
{
	size_t argi = 0, opti;
	char * optstr = argv[argi++];
	mbstate_t ps = {0};

	if (*optstr == '-')
		optstr++;

	for (;;) {
		wchar_t wc;
		int conv_ret = mbrtowc(&wc, optstr, MB_CUR_MAX, &ps);
		if (conv_ret <= 0) {
			if (conv_ret < 0)
				ERR("%s: byte %lu of `%s'",
					strerror(errno), (long unsigned)(optstr - *argv), *argv);
			return argi;
		}
		optstr += conv_ret;

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

		// Now we go back to multibyte processing
found:		if (opts[opti].type == ENUM_ARG)
			opts[opti].takes_arg = REQ_ARG;

		if (opts[opti].takes_arg == NO_ARG)
			if (opts[opti].type == CALLBACK)
				opts[opti].callback(opts + opti, NULL);
			else
				write_optarg(opts + opti, opts[opti].assign_val);
		else {
			struct optarg_handled const oh =
				handle_optarg(opts + opti, *optstr ? optstr : NULL, argv + argi);
			CHECK_ARGNFOUND("-%lc", wc);
			argi += oh.argi;
			if (oh.argi) {
				CHECK_TRAILING_JUNK("-%lc", wc, argv[argi - 1]);
				return argi;
			}
			if (oh.new_arg)
				optstr = oh.new_arg;
		}
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

		if (argv[argi][0] != '-')
			break;

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
	}

	return argi;
}
