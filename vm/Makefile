.PHONY: all clean

all: vm

vm: vm.c cpu.c dis.c sym.c dbg.c abi.h vm.h
	$(CC) -g -Wimplicit -Wstrict-aliasing -o vm vm.c cpu.c dis.c sym.c dbg.c -ledit

clean:
	rm -f vm

