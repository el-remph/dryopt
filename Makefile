.PHONY = test clean

CFLAGS = -pipe -Wall -Wextra -ggdb3 -std=c99
LDLIBS = -lm
dryopt.o: dryopt.h

test: tests/test-bin
	./tests/test.sh $<
	@echo 'Test succeeded!'

tests/test-bin: dryopt.o
tests/test-bin.o: CFLAGS += -std=c11
tests/test-bin.o: dryopt.h

clean:
	rm -fv tests/test-bin tests/test-bin.o dryopt.o
