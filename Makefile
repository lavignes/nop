XA = xa

.PHONY: all clean

all: asm vm sys.bin

asm: asm.c
	$(CC) -g -Wimplicit -Wstrict-aliasing -o asm asm.c

vm: vm.c
	$(CC) -g -Wimplicit -Wstrict-aliasing -o vm vm.c -ledit

sys.bin sys.lst sys.lbl: asm sys.s
	$(XA) -C -XMASM -XCA65 -P sys.lst -l sys.lbl -o sys.bin sys.s

clean:
	rm -f vm asm
	rm -f sys.bin sys.lst sys.lbl

