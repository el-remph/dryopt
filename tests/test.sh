#!/bin/sh
# Dependencies: usually just builtins (echo(1p), printf(1p), test(1p)) and
# uname(1) with -o. Under MSYS, additionally: realpath(1p), dirname(1p),
# sed(1p), unix2dos(1)

set -efu +m
exe=./$1

case `uname -s` in
MINGW*|Windows*)
	is_msw=1
	exename=$(realpath "$exe" | $(dirname "$0")/path-msys-to-windows.sed)
	;;
*)
	is_msw= exename=$exe
esac

do_test() {
	expectation=$1
	shift
	echo "+> $exe $*"
	if reality=`$exe "$@"`; then :; else # preserve $? without triggering errexit
		code=$?
		echo "$exe $*: exit $code"
		return $code
	fi

	# Not pretty, but the only portable way without -o pipefail
	if test -n $is_msw; then
		reality=`echo "$reality" | dos2unix`
	fi

	if test "$expectation" != "$reality"; then
		printf '>>> %s:\n>>> expected:\n%s\n>>> got:\n%s\n' \
			"$exe $*" "$expectation" "$reality"
		return 1
	fi
	return 0
}

fail_test() {
	expectation=$1
	shift
	echo "+> $exe $*"
	if reality=`$exe "$@" 2>&1 >/dev/null`; then
		echo "$exe $*: false success"
		return 1
	fi
	if test "$expectation" != "$reality"; then
		printf '>>> %s:\n>>> expected:\n%s\n>>> got:\n%s\n' \
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
if test -z "${erange_str-}"; then
	if type errno >/dev/null 2>&1; then
		erange_str=`errno ERANGE | sed -E 's/^ERANGE [0-9]+ //'`
#		erange_str=${erange_str#'ERANGE 34 '}
	else
		erange_str='Numerical result out of range'
	fi
fi

# overflow tests
for i in 32768 -32769; do
	fail_test "$exename: $i: $erange_str" --value:$i
done

help_output="\
Usage: $exename [OPTS] [ARGS]
  -v, --value=SIGNED             set value
  -b, --bigvalue=[UNSIGNED]      set bigvalue
  -s, --strarg=[STR]             set strarg
  -n, --[no-]flag                boolean; takes no argument
  -F, --float=FLOATING           set fl (double)
  -e, --enum=never,auto,always   pick one of a predetermined set of arguments
  -c, --callback=[ARG]           call callback
  -h, -?, --help                 Print this help and exit"
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

# Don't segfault if the argument is mandatory either; try to fail with grace
for i in -F --float --float=; do
	fail_test "$exename: missing FLOATING argument to ${i%=}" $i
done

for i in -c --callback; do
	do_test 'callback saw: yeeble
-v 0	-b 1	-s (null)	-n 0	-F 0
arguments after options:	deeble' $i yeeble deeble
	do_test 'callback saw: (null)
-v 0	-b 1	-s (null)	-n 0	-F 0
arguments after options:' $i
done

# Preserve `-' as a positional argument
do_test '-v 0	-b 1	-s (null)	-n 0	-F 0
arguments after options:	-'	\
	-
do_test '-v 0	-b 1	-s (null)	-n 1	-F 0
arguments after options:	-'	\
	-n -
do_test '-v 0	-b 0	-s (null)	-n 0	-F 0
arguments after options:	-'	\
	-b -
