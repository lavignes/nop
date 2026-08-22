.PHONY: all clean

SDL3_CFLAGS = $(shell pkg-config --cflags sdl3)
SDL3_LIBS = $(shell pkg-config --libs sdl3)
LIBEDIT_CFLAGS = $(shell pkg-config --cflags libedit)
LIBEDIT_LIBS = $(shell pkg-config --libs libedit)

all: vm

vm: vm.c cpu.c dis.c sym.c dbg.c vdp.c abi.h vm.h
	$(CC) -g -Wimplicit -Wstrict-aliasing -o vm vm.c cpu.c dis.c sym.c dbg.c vdp.c \
		$(SDL3_CFLAGS) $(SDL3_LIBS) $(LIBEDIT_CFLAGS) $(LIBEDIT_LIBS)

clean:
	rm -f vm

