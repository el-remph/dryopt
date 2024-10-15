# DRYopt #

Or, **I Can't Believe It's Another Getopt**. Process options DRYly, without
churning out lines of boilerplate getopt(3) code.

## Features ##

- Handles long and short options
- Minimal code to write for caller -- options are parsed in a single function
  call without loops as would be the case with getopt(3); functionality lost
  from this can be implemented through [argp]-style callbacks
- [Automatic `--help` generation](#automatic---help-generation)
- Cool [type system](#type-system)
- wchar_t options allowed (UTF-32 on sane systems, equivalent on FreeBSD and
  Solaris, UCS-2 on W*ndows); respects locale
- No heap allocation, and not too intrusive with the globals
- Single-{source,header,object}

### Automatic `--help` generation ###

When using getopt(3), if you change any part of the CLI, you have to change:

- The argument parsing code
- The `--help` output
- The manpage

And since only the first of those *has* to be changed, it's easy for
documentation to go out of sync. [GNU help2man] keeps the manpage in sync
with the `--help` output and saves the programmer from having to write
that too, and [recommends] use of [argp] or [popt]\(3) to specify option
help with the options themselves. However, they both implement a great
deal more functionality and flexibility than getopt(3), and more than most
callers will need, thus requiring more work from the caller when writing
the option-parsing, and from the program at runtime in the case of popt(3)
which requires heap allocation. The aim of DRYopt is to handle simpler
cases than argp and popt(3) are aimed at, with a simpler interface and more
minimal code, without having the programmer repeat themselves in maintaining
documentation.

[GNU help2man]: https://www.gnu.org/s/help2man
[recommends]: https://www.gnu.org/software/help2man/#g_t_002d_002dhelp-Recommendations
[argp]: https://sourceware.org/glibc/manual/latest/html_node/Argp.html
[popt]: https://github.com/rpm-software-management/popt

### Type System ###

- DRYopt features (very lightweight and basic) type checking and resolution,
  including numeric overflow checking, so there is no extra code for the caller
  to write for casting, nor string conversion (which is often masses of
  unchecked atoi(3) anyway).

- As with [Perl's Getopt::Long], option argument types mean that arguments
  can be clearly distinguished from option characters, and inserted into a
  bundled string. For example,

      -w77 -g75 -p#

  can become

      -w77g75p#

- Likewise, for numeric types, the presence of an optional argument is
  determined by whether the argument fits the prescribed type, rather than
  the getopt(3) method (which is still used for non-numeric types) where
  `-o ARG` syntax is no longer available and options must be set as
  `-oARG`. For instance, given option `-a` takes an optional FLOATING
  argument and option `-b` takes no argument (*all* of the following examples
  set `-b`):
  - `-a2b` sets `-a` to 2.0
  - `-a12.008e2b` sets `-a` to 1200.8
  - `-ab` sets `-a` to 0
  - `-a -2 -b` sets `-a` to -2.0
  - `-a -b` sets `-a` to 0

- Enums, for arguments that all set the same option to different values,
  like `--colour={auto,always,never}`

[Perl's Getopt::Long]: https://metacpan.org/dist/Getopt-Long


## Requirements (minimal) ##

- ISO C95
- Some basic C99 features, widely implemented before C99 (eg. old GCC, even
  MSVC); see top of [dryopt.c](dryopt.c) for details
- C11 for the optional but very handy `DRYOPT()` macro for the caller
- No UNIVACs, no PDP-11s

### Tested on: ###

|				| GCC	| Clang	| [TCC]	| [cproc] |
| ---------------------------	| ---	| -----	| -----	| ------- |
| *amd64 Linux/GNU*		|14.2.1	|18.1.8	| 0.9.27| f66a661 |
| *amd64 Linux/musl[^musl]*	|14.2.1	|18.1.8	|	|         |
| *aarch64 Linux/GNU*		|13.2.0	|17.0.6	|	|         |
| *aarch64 Linux/musl[^musl]*	|13.2.0	|17.0.6	|	|         |
| *amd64 Windows[^msw]*		|14.2.0 |	|	|         |

[TCC]: https://bellard.org/tcc/
[cproc]: https://git.sr.ht/~mcf/cproc
[^musl]:	For musl-based binaries, tests/test.sh was run with
		`erange_str='Result not representable'` in its environment
[^msw]:	For M$ Windows, GCC was the mingw64 version, and tests/test.sh
	was run using MSYS2 bash and utils, sometimes with
	`erange_str='Result too large'` in its environment, depending on the
        exact toolchain used

## Missing features (may or may not be implemented later) ##

- GNU getopt(3)-style argv permution (probably WONTFIX)
- Guaranteed UTF-32 -- could do that with a #define, a typedef for
  wchar_t/char32_t, and some ifdefery for mbtowc/mbrtoc32, only works
  at C11 -- but there is no c32type.h! So GNU libunistring? Who's even
  using characters outside the BMP as flags?
- User-defined limits, not just the bounds of the type
- Complex systems like sections and trees of options -- use [argp] or
  [popt]\(3) instead
- getopt_long_only(3)-style single dash parsing


## Example ##

See [tests/](tests/) and [examples/](examples/).

## Copyright ##

Copyright (C) 2024 The Remph <lhr@disroot.org>; free software under the
terms of the GNU Lesser General Public Licence as published by the Free
Software Foundation, version three or later, with an exception to allow
for static linking -- in essence, making this a file-scope copyleft. See
[COPYING](COPYING) for more information.
