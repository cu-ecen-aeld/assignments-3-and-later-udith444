# Cross compilation support
ifdef CROSS_COMPILE
CC := $(CROSS_COMPILE)gcc
else
CC := gcc
endif

CFLAGS := -Wall -Wextra

# Default target
all: writer

writer: writer.o
	$(CC) $(CFLAGS) -o writer writer.o

writer.o: writer.c
	$(CC) $(CFLAGS) -c writer.c

clean:
	rm -f writer *.o

.PHONY: all clean
