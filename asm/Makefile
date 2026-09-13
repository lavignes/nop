.PHONY: all clean

CFLAGS = -g -Wimplicit -Wstrict-aliasing

all: asm

asm: asm.c lex.c expr.c abi.h asm.h opcodes.h
	$(CC) $(CFLAGS) -o asm asm.c lex.c expr.c

clean:
	rm -f asm
