#
# Makefile
#
# source: writer.c
# target: writer
#

# option Crosscompile
CROSS_COMPILE ?=

# compiler and flags: -Wall, -Wextra -> show warnings, -g add debug information
CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Wextra -Werror -g

# target
TARGET = aesdsocket

# source
SRC = aesdsocket.c
OBJ = aesdsocket.o

# default
all: $(TARGET)

# link object file
$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^
        #$(CC) $(CFLAGS) $(INCLUDES) $(OBJS) -o $(TARGET) $(LDFLAGS)

# compile source file 
%.o:  %.c
	$(CC) $(CFLAGS) -c $< -o $@

# clean target
clean:
	rm -f $(TARGET) *.o

# all, clean are no files
.PHONY:  all clean


