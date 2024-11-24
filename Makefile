.PHONY = test clean example

CFLAGS = -pipe -Wall -Wextra -ggdb3 -std=c99
LDLIBS = -lm
ARFLAGS = rcs

(%): % ;
%.a:
	${AR} ${ARFLAGS} $@ $?

OBJS = dryopt.o config-parse.o
dryopt.a: ${OBJS}
${OBJS}: dryopt.h common.h

TESTBINS = tests/test-bin tests/test-mask
EXMPBINS = examples/as-bin
TESTOBJS = ${TESTBINS:=.o}
EXMPOBJS = ${EXMPBINS:=.o}

test: ${TESTBINS}
	./tests/test.sh tests/test-bin
	./tests/test-mask.sh tests/test-mask
	@echo 'Test succeeded!'

example: ${EXMPBINS}

${TESTBINS} ${EXMPBINS}: dryopt.a
${TESTOBJS} ${EXMPOBJS}: dryopt.h
tests/test-bin.o examples/as-bin.o: CFLAGS += -std=c11

clean:
	rm -fv dryopt.a ${OBJS} ${TESTBINS} ${TESTOBJS} ${EXMPBINS} ${EXMPOBJS}
