CC      = gcc
CFLAGS  = -Wall -Wextra -pedantic -std=c11
TARGET  = lock_program

all: $(TARGET)

$(TARGET): lock.c
	$(CC) $(CFLAGS) -o $(TARGET) lock.c

clean:
	rm -f $(TARGET)
