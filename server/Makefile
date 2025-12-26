.PHONY = rebuild clean

ifneq ($(findstring -lpthread,$(LDFLAGS)), -lpthread)
    LDFLAGS += -lpthread
endif

all:safefile.o list.o
	$(CC) $(CFLAGS) $(LDFLAGS) -DUSE_AESD_CHAR_DEVICE -I../aesd-char-driver/ aesdsocket.c safefile.o list.o -o aesdsocket

rebuild:clean all

safe_file.o:safefile.c safefile.h
	$(CC) $(CFLAGS) -c safefile.c -o safefile.o

list.o:list.h list.c
	$(CC) $(CFLAGS) -c list.c -o list.o

install:all
	install -m 766 aesdsocket $(PREFIX)/bin/

clean:
	$(RM) *.o
	$(RM) aesdsocket
