.PHONY: all

test: all
		./15 20 20 1

15:	15.c
		gcc -o 15 15.c

all:15
