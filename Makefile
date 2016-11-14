.PHONY: all clean

test: all
		./15 20 20 1

15:	15.c
		gcc -o 15 15.c -g3 -pthread -fPIC

all:15

clean:
		rm -rf *.o
		rm -f ./15
