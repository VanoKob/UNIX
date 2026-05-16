CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c11
TARGET = myinit

all: $(TARGET)

$(TARGET): myinit.c
	$(CC) $(CFLAGS) -o $(TARGET) myinit.c

clean:
	rm -f $(TARGET)