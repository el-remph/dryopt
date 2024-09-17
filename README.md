DRYopt
======

Or, **I Can't Believe It's Another Getopt**. Process options DRYly, without
churning out lines of boilerplate getopt(3) code.


Features
--------

- Handles long and short options
- Minimal code to write for caller -- options are parsed in a single function
  call without loops as would be the case with getopt(3); functionality lost
  from this can be implemented through [argp]-style callbacks
- Automatic `--help` generation
- Type checking and resolution, overflow checking -- no extra code for the
  caller to write for casting, nor string conversion (which is often masses
  of unchecked atoi(3) anyway)
- Like with [Perl's Getopt::Long], option argument types mean that arguments
  can be clearly distinguished from option characters, and inserted into a
  bundled string. For example, `-w77 -g75 -p#` can become `-w77g75p#`. Even
  for optional arguments, given that `-a` takes an optional FLOATING argument,
  `-a12.008e2b` sets `-a` to 1200.8, while `-ab` sets `-a` to 0 (both set
  `-b`).
- wchar_t options allowed (UTF-32 on sane systems, UCS-2 on W*ndows);
  respects locale
- Not too intrusive with the globals
- No heap allocation
- Single-{source,header,object}

[Perl's Getopt::Long]: https://metacpan.org/dist/Getopt-Long
[argp]: https://sourceware.org/glibc/manual/latest/html_node/Argp.html


Requirements (minimal)
----------------------

- Some basic C99 features, widely implemented before C99 (eg. old GCC, even
  MSVC); see top of [dryopt.c](dryopt.c) for details
- C11 for the optional but very handy `DRYOPT()` macro for the caller
- No UNIVACs, no PDP-11s, no Harvard microcontrollers


Missing features (may or may not be implemented later)
------------------------------------------------------

- GNU getopt(3)-style argv permution (probably WONTFIX)
- Guaranteed UTF-32 -- could do that with a #define, a typedef for
  wchar_t/char32_t, and some ifdefery for mbtowc/mbrtoc32, only works
  at C11 -- but there is no c32type.h! So GNU libunistring? Who's even
  using characters outside the BMP as flags?
- Aliases -- probably a TODO
- Some way to negate boolean short opts -- definitely a TODO
- User-defined limits, not just the bounds of the type
- Enums! For mutually exclusive options that all set the same variable to
  different values, like gzip's `-[zdlt]`
  - What about the same, for arguments instead of options? Like
    `--colour={auto,always,never}`
- Complex systems like sections and trees of options -- use [argp] or
  [popt]\(3) instead
- getopt_long_only(3)-style single dash parsing

[popt]: https://github.com/rpm-software-management/popt


Example
-------
See [tests/test-bin.c](tests/test-bin.c).


Copyright
---------

Copyright (C) 2024 The Remph <lhr@disroot.org>; free software under the
terms of the GNU Lesser General Public Licence as published by the Free
Software Foundation, version three or later, with an exception to allow
for static linking -- in essence, making this a file-scope copyleft. See
[COPYING](COPYING) for more information.
