#!/bin/sh
set -eux

# The formatting of these may look odd, but it's to make the shell xtrace
# output more legible
test "
`./$1 -nbv32767sstring foo bar`" = "
-v 32767	-b 0	-s string	-n 1	-F 0
arguments after options:	foo	bar"
test "
`./$1 -nv-32768 --no-flag -- -bar foo mung`" = "
-v -32768	-b 1	-s (null)	-n 0	-F 0
arguments after options:	-bar	foo	mung"

export LANG=C
if type errno >/dev/null 2>&1; then
	erange_str=`errno ERANGE | sed 's/^ERANGE 34 //'`
else
	erange_str='Numerical result out of range'
fi

for toobig in 32768 -32769; do
	# The negation ensures `set -e' interprets this right -- the command
	# must fail, and shell errexit triggers if it doesn't; stderr is captured
	stderr_contents=`! ./$1 --value:$toobig 2>&1 >/dev/null`
	test "$stderr_contents" = "./$1: $toobig: $erange_str"
done

help_output="\
Usage: ./tests/test-bin [OPTS] [ARGS]
  -v, --value=SIGNED	set value
  -b, --bigvalue=[UNSIGNED]	set bigvalue
  -c, --callback=[ARG]	call callback
  -s, --strarg=[STR]	set strarg
  -n, --flag	boolean; takes no argument
  -F, --float=FLOATING	set fl (double)
  -e, --enum=never,auto,always"

test "`./$1 -h`" = "$help_output"
test "`./$1 -\?`" = "$help_output"
test "`./$1 --help`" = "$help_output"

test "
`./$1 -b -n`" = "
-v 0	-b 0	-s (null)	-n 1	-F 0
arguments after options:"
test "
`./$1 -b 0777 -n`" = "
-v 0	-b 511	-s (null)	-n 1	-F 0
arguments after options:"
test "
`./$1 --bigvalue --flag`" = "
-v 0	-b 0	-s (null)	-n 1	-F 0
arguments after options:"
test "
`./$1 --bigvalue 0777 --flag`" = "
-v 0	-b 511	-s (null)	-n 1	-F 0
arguments after options:"

# Don't segfault on a trailing option with optional argument and no argument
# given
./$1 -b >/dev/null
./$1 --bigvalue >/dev/null
./$1 --bigvalue= >/dev/null
