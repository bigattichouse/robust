# Self-contained: C99 + libm only. No dependency on the parent project.
CC     ?= cc
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -pedantic

check_table1: check_table1.c
	$(CC) $(CFLAGS) -o $@ $< -lm

run: check_table1
	./check_table1

clean:
	rm -f check_table1

.PHONY: run clean
