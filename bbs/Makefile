.PHONY: all clean

CFLAGS = -g -Wimplicit -Wstrict-aliasing

all: bbs

bbs: bbs.c session.c bufs.c abi.h bbs.h
	$(CC) $(CFLAGS) -o bbs bbs.c session.c bufs.c

clean:
	rm -f bbs
