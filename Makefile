CFLAGS = -Wall -Wextra -pedantic

all: windowmanager

windowmanager: main.o
	gcc -o windowmanager main.o -lm -lxcb -lxcb-keysyms

main.o: main.c
	gcc $(CFLAGS) -c main.c

clean:
	rm -f windowmanager *.o
