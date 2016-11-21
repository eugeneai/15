.PHONY: all clean gdb test

PRG=15-solve
COMPLEXITY=100

gdb: all
		gdb --args ./$(PRG) $(COMPLEXITY) 20 1

test: all
		./$(PRG) $(COMPLEXITY) 20 1

$(PRG):	15.c
		gcc -o $(PRG) 15.c -g3 -pthread -fPIC

all: $(PRG)

clean:
		rm -rf *.o
		rm -f ./$(PRG)
