CC ?= $(CROSS_COMPILE) gcc
CFLAGS ?=-Wall  -O0
LDFLAGS ?= 
ARGS ?= -I../aesd-char-driver
all: aesdsocket

default: aesdsocket

debug: aesdsocket.c 
	$(CC) $(CFLAGS) $(ARGS) aesdsocket.c -o aesdsocket $(LDFLAGS)


aesdsocket: aesdsocket.c 
	$(CC) $(CFLAGS) $(ARGS) aesdsocket.c -o aesdsocket $(LDFLAGS)


clean: 
	rm -f *.o aesdsocket


