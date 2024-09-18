#!/bin/sh
set -eu

exe=./$1

do_test() {
	expectation=$1
	shift
	echo >&2 "Testing: $*"
	if reality=`$exe "$@"`; then :; else # preserve $? without triggering errexit
		code=$?
		echo >&2 "$exe $*: exit $code"
		return $code
	fi
	if test "$expectation" != "$reality"; then
		printf >&2 '>>> %s:\n>>> expected:\n%s\n>>> got:\n%s\n' \
			"$exe $*" "$expectation" "$reality"
		return 1
	fi
	return 0
}

do_test '-v 32767	-b 0	-s string	-n 1	-F 0
arguments after options:	foo	bar'	\
	-nbv32767sstring foo bar

do_test '-v -32768	-b 1	-s (null)	-n 0	-F 0
arguments after options:	-bar	foo	mung'	\
	-nv-32768 --no-flag -- -bar foo mung

export LANG=C
if type errno >/dev/null 2>&1; then
	erange_str=`errno ERANGE | sed 's/^ERANGE 34 //'`
else
	erange_str='Numerical result out of range'
fi

# overflow tests
for i in 32768 -32769; do
	# The negation ensures `set -e' interprets this right -- the command
	# must fail, and shell errexit triggers if it doesn't; stderr is captured
	stderr_contents=`! $exe --value:$i 2>&1 >/dev/null`
	expected="./$1: $i: $erange_str"
	if test "$stderr_contents" != "$expected"; then
		printf >&2 '>>> %s:\n>>> expected:\n%s\n>>> got:\n%s\n' \
			"$exe --value:$i" "$expected" "$stderr_contents"
		exit 1
	fi
done

help_output="\
Usage: ./tests/test-bin [OPTS] [ARGS]
  -v, --value=SIGNED	set value
  -b, --bigvalue=[UNSIGNED]	set bigvalue
  -c, --callback=[ARG]	call callback
  -s, --strarg=[STR]	set strarg
  -n, --flag	boolean; takes no argument
  -F, --float=FLOATING	set fl (double)"
do_test "$help_output" -h
do_test "$help_output" '-?'
do_test "$help_output" --help

# Distinguishing optional arguments
for i in '-b -n' '--bigvalue --flag'; do
	do_test '-v 0	-b 0	-s (null)	-n 1	-F 0
arguments after options:'	\
		$i
done
for i in '-b 0777 -n' '--bigvalue 0777 --flag'; do
	do_test '-v 0	-b 511	-s (null)	-n 1	-F 0
arguments after options:'	\
		$i
done

do_test '-v 0	-b 0	-s (null)	-n 0	-F 0
arguments after options:	--flag'	\
	--bigvalue -- --flag
do_test '-v 0	-b 0	-s (null)	-n 0	-F 0
arguments after options:	-n'	\
	-b -- -n

# Don't segfault on a trailing option with optional argument and no argument
# given
for i in -b --bigvalue --bigvalue=; do
	do_test '-v 0	-b 0	-s (null)	-n 0	-F 0
arguments after options:'	\
		$i
done
