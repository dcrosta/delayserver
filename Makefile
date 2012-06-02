
CC = gcc
CFLAGS = -Wall -Werror -pedantic -O2 -std=c99
LFLAGS = -pthread

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

echodelay: echo-thread.o
	$(CC) -o $@ $< $(LFLAGS)

.PHONY: clean

clean:
	rm -f *.o echodelay
