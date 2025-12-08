#
# Makefile
#
# source: writer.c
# target: writer
#

# option Crosscompile
CROSS_COMPILE ?=

# compiler and flags: -Wall, -Wextra -> show warnings, -g add debug information
CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -Wextra -Werror -g
LDFLAGS ?= -lpthread -lrt

INCLUDES ?=

# target
TARGET ?= aesdsocket

# source
SRC = aesdsocket.c
OBJ = aesdsocket.o

# default
all: $(TARGET)

# link object file
# $(CC) $(CFLAGS) $(INCLUDES) $(OBJS) -o $(TARGET) $(LDFLAGS)
$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(INCLUDES) $(LDFLAGS)

# compile source file 
%.o:  %.c
	$(CC) $(CFLAGS) -c $< -o $@

# clean target
clean:
	rm -f $(TARGET) *.o

# all, clean are no files
.PHONY:  all clean


