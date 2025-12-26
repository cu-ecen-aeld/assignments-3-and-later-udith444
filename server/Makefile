CC = $(CROSS_COMPILE)gcc
CFLAGS = -DUSE_AESD_CHAR_DEVICE=1 -I./include -I../aesd-char-driver

all: aesdsocket
default: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) -o aesdsocket aesdsocket.c 

clean:
	rm -f aesdsocket