#!/bin/sh
# Dependencies (all POSIX): usually just builtins (echo, printf, test) and
# uname. Under MSYS, additionally: basename, sed

set -efu +m
exe=./$1
exename=`basename "$exe"`

case `uname -s` in
MINGW*|MSYS*|Windows*)
	is_msw=1
	;;
*)
	is_msw=
	;;
esac

set_reality_or_die() {
	if reality=`$exe "$@"`; then :; else # preserve $? without triggering errexit
		code=$?
		echo "$exe $*: exit $code"
		return $code
	fi

	# Not pretty, but the only portable way without -o pipefail
	if test -n $is_msw; then
		reality=`echo "$reality" | sed 's/\r$//'` # dos2unix(1) not always available
	fi
}


do_test() {
	expectation=$1
	shift
	echo "+> $exe $*"
	set_reality_or_die "$@"
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
	case $reality in
	*[[:punct:]]$exename*": $expectation") ;;
	*)
		printf '>>> %s:\n>>> expected:\n%s\n>>> got:\n%s\n' \
			"$exe $*" "*/$exename*: $expectation" "$reality"
		return 1
	esac
	return 0
}

test_help() {
	set_reality_or_die "$@"
	case $reality in
	"\
Usage: "*[[:punct:]]$exename*' [OPTS] [ARGS]
  -v, --value=SIGNED             set value
  -b, --bigvalue=[UNSIGNED]      set bigvalue
  -s, --strarg=[STR]             set strarg
  -n, --[no-]flag                boolean; takes no argument
  -F, --float=FLOATING           set fl (double)
  -e, --enum=never,auto,always   pick one of a predetermined set of arguments
  -c, --callback=[ARG]           call callback
  -h, -?, --help                 Print this help and exit')
		;;
	*)
		printf '>>> %s:\n>>> bad help message:\n%s\n' \
			"$exe $*" "$reality"
		return 1
	esac
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
	fail_test "$i: $erange_str" --value:$i
done

test_help -h
test_help '-?'
test_help --help

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
	fail_test "missing FLOATING argument to ${i%=}" $i
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
