CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -D_GNU_SOURCE

all: myprogram

myprogram: myprogram.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f myprogram

.PHONY: all clean
