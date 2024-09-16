/* SPDX-FileCopyrightText:  2024 The Remph <lhr@disroot.org>
   SPDX-License-Identifier: LGPL-3.0-or-later WITH LGPL-3.0-linking-exception */

#ifndef DRYOPT_H
#define DRYOPT_H

#include <stddef.h>	/* wchar_t, size_t */
#include <stdio.h>	/* FILE* */

/* Any field can be 0 to indicate it should not be (eg. .shortopt = L'\0'
   means no shortopt, .longopt = NULL means no longopt) */
struct dryopt {
	wchar_t shortopt;
	char const *restrict longopt, *restrict helpstr;
	void * argptr;	/* type pointed to depends on .arg */
	enum {
		BOOLEAN = 0, STR, CHAR, SIGNED, UNSIGNED,
		FLOATING,	/* note that this only works for the (typically)
				   IEEE binary formats, not DFP (C23 _Decimal*) */
		CALLBACK
	} arg: 3;
	unsigned int
		optional: 1,	/* ignored if .arg == BOOLEAN */
		sizeof_arg: 5;	/* ignored if .arg is STR, CHAR or CALLBACK.
				   unfortunately must be 5 bits, because
				   of the chance of a 16-byte long double */
};

/* Return number of characters consumed successfully from arg. 0 means arg
   was invalid */
typedef size_t (*dryopt_callback)(struct dryopt const*, char const * arg);

/* BEWARE! <ARGPTR> may be evaluated multiple times!
   Also, this one depends on C11 _Generic. If you lack that, you'll have to
   fill out the struct yourself */
#define DRYOPT(SHORT, LONG, HELP, ARGPTR, OPTIONAL) {	\
	.shortopt = (SHORT), .longopt = (LONG), .helpstr = (HELP),	\
	.arg = _Generic((ARGPTR),			\
			_Bool*:	BOOLEAN,		\
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
			long double*:	FLOATING,	\
			dryopt_callback:	CALLBACK),	\
	.optional = OPTIONAL,				\
	.sizeof_arg = _Generic((ARGPTR),		\
			char**: 0,			\
			dryopt_callback: 0,		\
			default: sizeof *(ARGPTR)),	\
	.argptr = (ARGPTR) }


extern size_t dryopt_parse (char *const[], struct dryopt const[], size_t)
	__attribute__((__access__(read_only, 2, 3), nonnull));

/* Note: this returns! */
extern void auto_help(	struct dryopt const opts[], size_t optn,
			FILE *restrict outfile, char const *restrict program_name,
			char const *restrict help_args, char const *restrict help_extra);

extern struct dryopt_config_s {
	/* defaults are zeroes across the board */
	enum { no_sort = 0, do_sort, already_sorted } sorting: 2; /* TODO */
	enum { die = 0, complain, noop } autodie: 2;
	unsigned no_setlocale: 1;

	/* this one is an output field: it starts at 0, and is set to 1 on
	   error. This is redundant unless autodie != die */
	unsigned mistakes_were_made: 1;
} dryopt_config;

/* WARNING! <OPTS> may be evaluated twice! */
#define DRYOPT_PARSE(ARGV, OPTS) dryopt_parse((ARGV), (OPTS), sizeof(OPTS) / sizeof(struct dryopt))

#endif /* DRYOPT_H */
