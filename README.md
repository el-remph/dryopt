# DRYopt #

Or, **I Can't Believe It's Another Getopt**. Process options DRYly, without
churning out lines of boilerplate getopt(3) code.

## Features ##

- Handles long and short options
- Minimal code to write for caller -- options are parsed in a single function
  call without loops as would be the case with getopt(3); functionality lost
  from this can be implemented through [argp]-style callbacks
- Automatic `--help` generation
- Cool [type system](#type-system)
- wchar_t options allowed (UTF-32 on sane systems, equivalent on FreeBSD and
  Solaris, UCS-2 on W*ndows); respects locale
- No heap allocation, and not too intrusive with the globals
- Single-{source,header,object}

[argp]: https://sourceware.org/glibc/manual/latest/html_node/Argp.html

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
  argument and option `-b` takes no argument (all the following examples
  set `-b`):
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

|			| GCC	| Clang	| [TCC]	| [cproc] |
| ---------------------	| ---	| -----	| -----	| ------- |
| *amd64 Linux/GNU*	|14.2.1	|18.1.8	| 0.9.27| f66a661 |
| *amd64 Linux/musl*	|14.2.1	|18.1.8	|	|         |

NB: For musl-based binaries, tests/test.sh was run with
`erange_str='Result not representable'` in its environment

[TCC]: https://bellard.org/tcc/
[cproc]: https://git.sr.ht/~mcf/cproc

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

[popt]: https://github.com/rpm-software-management/popt


## Example ##

See [tests/test-bin.c](tests/test-bin.c).

## Copyright ##

Copyright (C) 2024 The Remph <lhr@disroot.org>; free software under the
terms of the GNU Lesser General Public Licence as published by the Free
Software Foundation, version three or later, with an exception to allow
for static linking -- in essence, making this a file-scope copyleft. See
[COPYING](COPYING) for more information.
