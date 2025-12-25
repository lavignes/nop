NASM = nasm
AFLAGS += -f elf32 -I../../fs/
AFLAGS += -g

OBJCOPY = objcopy

LD = gcc
LDFLAGS = -nostdlib -nodefaultlibs -nostartfiles -fno-builtin
LDFLAGS += -static -fno-pic -fno-pie -Tboot.ld
LDFLAGS += -g

SRCS = boot.asm sysinit.asm
OBJS = $(SRCS:.asm=.o)
DEPS = $(SRCS:.asm=.d)

.PHONY: all clean run debug

all: boot.img

run: boot.img
	qemu-system-i386 -cpu 486 -machine isapc,acpi=off -vga cirrus -drive file=boot.img,format=raw

debug: boot.img
	qemu-system-i386 -cpu 486 -machine isapc,acpi=off -vga cirrus -drive file=boot.img,format=raw -s -S

boot.img: boot.bin
	dd if=/dev/zero of=boot.img bs=512 count=60
	dd if=boot.bin of=boot.img seek=0 bs=512 conv=notrunc

boot.bin: boot.elf
	$(OBJCOPY) -O binary $< $@

boot.elf: $(OBJS) $(DEPS) boot.ld
	$(LD) $(OBJS) -o $@ $(LDFLAGS)

%.o %.d: %.asm
	$(NASM) $(AFLAGS) -MD -MF $(addsuffix .d,$(basename $<)) $< -o $(addsuffix .o,$(basename $<))

clean:
	rm -f boot.elf boot.bin boot.img
	rm -f *.o *.d

ifneq ($(MAKECMDGOALS),clean)
include $(DEPS)
endif
