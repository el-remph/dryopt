.PHONY = test clean

CFLAGS = -pipe -Wall -Wextra -ggdb3 -std=c99
LDLIBS = -lm
dryopt.o: dryopt.h

TESTBINS = tests/test-bin tests/test-mask
TESTOBJS = ${TESTBINS:=.o}

test: ${TESTBINS}
	./tests/test.sh tests/test-bin
	./tests/test-mask.sh tests/test-mask
	@echo 'Test succeeded!'

${TESTBINS}: dryopt.o
${TESTOBJS}: dryopt.h
tests/test-bin.o: CFLAGS += -std=c11

clean:
	rm -fv ${TESTBINS} ${TESTOBJS} dryopt.o
