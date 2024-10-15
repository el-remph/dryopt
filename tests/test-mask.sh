#!/bin/sh
set -efmu
exe=./$1 sepstr=/////

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

test_range() {
	start=$1 end=$2
	for i in `seq $start $end`; do
		set -- foo bar mung snark
		for j in 1 2 4 8; do
			if test 0 -ne $(( i & j )); then
				set -- $@ --$1
			fi
			shift
		done

		do_test $i $@

		if test -n "${BASH_VERSION+}"; then
			set -- "${@/#--/--no-}"
		else
			# Negate options, by splitting the stack into two separate
			# stacks with $sepstr, then popping off the top one and
			# pushing to the bottom one until $sepstr
			set -- "$@" "$sepstr"
			while test "$sepstr" != "$1"; do
				set -- "$@" "--no-${1#--}"
				shift
			done
			shift # lose $sepstr
		fi

		# I can explain -- 15 is 0b1111, which is (_BitInt(4))-1, so XORing
		# it with $i flips the mask, just like setting every option and then
		# unsetting those corresponding to $i
		do_test $((i ^ 15)) --foo --bar --mung --snark $@
	done
}

nproc=`nproc`
# 16 is the number of tests to do, testing 0..15. I don't think starting
# more than 4 jobs will be worth it
interval=$(( 16 / (nproc >= 4 ? 4 : 2) ))
pids=
for i in `seq 0 $interval 15`; do
	test_range $i $(( i + interval - 1 )) &
	pids="$pids $!"
done

exit_val=0
for pid in $pids; do
	# get return values
	wait $pid || exit_val=$?
done
exit $exit_val
