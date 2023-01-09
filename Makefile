PROGS=imapsearch

CC=gcc

CFLAGS+=-Wall -Wextra
imapsearch: CFLAGS+=-DSSLFT
imapsearch: LDFLAGS+=-lcrypto -lssl

all: ${PROGS}

clean:
	rm -f ${PROGS} *.o

