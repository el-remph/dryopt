#!/bin/sh
set -efu +m
exe=./$1

do_test() {
	expectation=$1
	shift
	command="$exe $*"
	>&2 echo "+> $command"
	reality=`$command`
	if test $expectation -ne "$reality"; then
		printf >&2 '>>> %s:\n>>> expected %d, got `%s'\''\n' \
			"$command" "$expectation" "$reality"
		return 1
	fi
}

i=-1
while test $((++i)) -lt 16; do
	args=
	set -- foo bar mung snark
	for j in 1 2 4 8; do
		if test 0 -ne $(( i & j )); then
			set -- $@ --$1
		fi
		shift
	done

	do_test $i $@

	# I can explain -- 15 is 0b1111, which is (_BitInt(4))-1, so XORing
	# it with $i flips the mask, just like setting every option and then
	# unsetting those corresponding to $i
	do_test $((i ^ 15)) --foo --bar --mung --snark ${@/#--/--no-}	# FIXME: that last substitution wants bash
done
