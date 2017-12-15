CC    := gcc
CFLAGS:= -Os -Wall

default: driver
	./driver

driver: lib
	$(CC) $(CFLAGS) -L$(PWD) -o $@ $@.c lib.o -lm

lib: library.c
	$(CC) $(CFLAGS) -o $@.o -c $^

clean:
	rm -f lib.o driver
