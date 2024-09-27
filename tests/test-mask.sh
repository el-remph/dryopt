#!/bin/sh
set -efmu
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

test_range() {
	start=$1 end=$2
	for i in `seq $start $end`; do
		shorts=
		set -- foo bar mung snark
		for j in 1 2 4 8; do
			if test 0 -ne $(( i & j )); then
				set -- $@ --$1
				shorts="$shorts`expr substr $1 1 1`"
			fi
			shift
		done

		do_test $i $@
		do_test $i ${shorts:+-}$shorts

		# I can explain -- 15 is 0b1111, which is (_BitInt(4))-1, so XORing
		# it with $i flips the mask, just like setting every option and then
		# unsetting those corresponding to $i
		: $(( i ^= 15 ))
		do_test $i --foo --bar --mung --snark ${@/#--/--no-}	# FIXME: that last substitution wants bash
		do_test $i -fbms ${shorts:++}$shorts
	done
}

nproc=`nproc`
# 16 is the number of tests to do, testing 0..15. test_range() calls $exe
# 4 times, so maybe 4+ jobs might be alright. nproc is rounded down to even
# because an odd number could lead to some tests being skipped
interval=$(( 16 / (nproc - nproc % 2) ))
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
