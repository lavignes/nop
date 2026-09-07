.PHONY: all clean

CFLAGS = -g -Wimplicit -Wstrict-aliasing

all: vm

vm: asm.c lex.c abi.h asm.h
	$(CC) $(CFLAGS) -o asm asm.c lex.c

clean:
	rm -f asm
