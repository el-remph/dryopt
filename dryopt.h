/* SPDX-FileCopyrightText:  2024 The Remph <lhr@disroot.org>
   SPDX-License-Identifier: LGPL-3.0-or-later WITH LGPL-3.0-linking-exception */

#ifndef DRYOPT_H
#define DRYOPT_H

#include <stddef.h>	/* wchar_t, size_t */
#include <stdio.h>	/* FILE* */

struct dryopt {
	/* Each of these can be 0 to indicate it should not be (eg.
	   .shortopt = L'\0' means no shortopt, .longopt = NULL means no
	   longopt) */
	wchar_t shortopt;
	char const *restrict longopt, *restrict helpstr;

	enum dryarg_tag {
		DRYOPT_INVALID = 0,	/* prevent negligent zero-initialisation */
		STR, CHAR, SIGNED,
		UNSIGNED,	/* applies to _Bool */
		FLOATING,	/* note that this only works for the (typically)
				   IEEE binary formats, not DFP (C23 _Decimal*) */
		CALLBACK,
		ENUM_ARG	/* eg. --colour={auto,always,never} */
	} type: 4;

	/* overwritten with REQ_ARG if .type is ENUM_ARGS */
	enum { NO_ARG = 0, OPT_ARG, REQ_ARG } takes_arg: 2;

	/* Similar to popt(3) POPT_ARGFLAG_(OR|AND|XOR). No equivalent to
	   POPT_{ARGFLAG_NOT,BIT_{SET,CLR}} -- do it yourself you lazy sod */
	enum { DRYARG_WRITE = 0, DRYARG_AND, DRYARG_OR, DRYARG_XOR } set_arg: 2;

	/* ignored if .type is STR, CHAR or CALLBACK. maximum sizeof
	   .assign_val (8, 010) */
	unsigned sizeof_arg: 4;

	void * argptr;	/* type pointed to depends on .type */
	/* If .type == CALLBACK, dryopt ignores this union, so the caller
	   can use it to pass arbitrary data to the callback */
	union {
		/* if no arg is given and .takes_arg != REQ_ARG, this is
		   written to .argptr */
		union dryoptarg {
			long long unsigned u;	/* this must come first as
						   it is easiest to cast to */
			long long signed i;
			double f;
			void * p;
		} assign_val;

		/* if .type == ENUM_ARG, this is a string vector in order
		   of enum value, eg. enum { ALWAYS, AUTO, NEVER } should
		   be { "always", "auto", "never", NULL }. The index of the
		   matching arg will be written to argptr */
		char const *const * enum_args;
	};
};

/* Return number of characters consumed successfully from arg. 0 means arg
   was invalid */
typedef size_t (*dryopt_callback)(struct dryopt const*, char const * arg);

#define DRYARG(ARGPTR)	\
	.type = _Generic((ARGPTR),			\
			_Bool*:	UNSIGNED,		\
			char**:	STR,			\
			char*:	CHAR,			\
			/* ho hum */			\
			signed short*:		SIGNED,	\
			signed int*:		SIGNED,	\
			signed long*:		SIGNED,	\
			signed long long*:	SIGNED,	\
			unsigned short*:	UNSIGNED,	\
			unsigned int*:		UNSIGNED,	\
			unsigned long*:		UNSIGNED,	\
			unsigned long long*:	UNSIGNED,	\
			float*:		FLOATING,	\
			double*:	FLOATING,	\
			dryopt_callback:	CALLBACK),	\
	.sizeof_arg = _Generic((ARGPTR),		\
			char**: 0,			\
			dryopt_callback: 0,		\
			default: sizeof *(ARGPTR)),	\
	.argptr = (ARGPTR)

/* BEWARE! <ARGPTR> may be evaluated multiple times!
   Also, this one depends on C11 _Generic. If you lack that, you'll have to
   fill out the struct yourself */
#define DRYOPT(SHORT, LONG, HELP, TAKES_ARG, ARGPTR, VAL) {		\
	.shortopt = (SHORT), .longopt = (LONG), .helpstr = (HELP),	\
	DRYARG(ARGPTR), .takes_arg = TAKES_ARG,	.assign_val = {VAL}	\
}


extern size_t dryopt_parse(char *const[], struct dryopt[], size_t)
	__attribute__((__access__(read_write, 2, 3), nonnull));

/* Note: this returns! */
extern void auto_help(	struct dryopt opts[], size_t optn,
			FILE *restrict outfile, char const *restrict program_name,
			char const *restrict help_args, char const *restrict help_extra)
	__attribute__((cold, leaf));

extern struct dryopt_config_s {
	/* defaults are zeroes across the board */
	enum { no_sort = 0, do_sort, already_sorted } sorting: 2; /* TODO */
	enum { die = 0, complain, noop } autodie: 2;
	unsigned no_setlocale: 1;

	/* this one is an output field: it starts at 0, and is set to 1 on
	   error. This is redundant unless autodie != die */
	unsigned mistakes_were_made: 1;

	/* number of columns to wrap auto_help() output to. Default is 80;
	   set to 0 to disable wrapping. Ten bits fills out to a 16-bit
	   word, and allows wrapping up to 1023 (0x3FF) columns, which is
	   plenty: I get 255 on a 2560x1440 screen with 12.5pt font, but
	   bigger screens and smaller fonts are available. But does anyone
	   like to read lines that long? If anything 10 bits is more than
	   enough */
	unsigned wrap: 10;
} dryopt_config;

/* WARNING! <OPTS> may be evaluated twice! */
#define DRYOPT_PARSE(ARGV, OPTS) dryopt_parse((ARGV), (OPTS), sizeof(OPTS) / sizeof(struct dryopt))

#endif /* DRYOPT_H */
