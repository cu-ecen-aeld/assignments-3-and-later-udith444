# Cross-compilation support
CROSS_COMPILE ?=
CC := $(CROSS_COMPILE)gcc
CFLAGS := -Wall -Werror -Wextra
TARGET := aesdsocket

# Default target
all: $(TARGET)

$(TARGET): aesdsocket.c
	$(CC) $(CFLAGS) -o $(TARGET) aesdsocket.c

clean:
	rm -f $(TARGET)

# Install target for buildroot (you'll need this later)
install: $(TARGET)
	mkdir -p $(DESTDIR)/usr/bin
	cp $(TARGET) $(DESTDIR)/usr/bin/

.PHONY: all clean install
