
NASM = nasm
AFLAGS += -f elf32
AFLAGS += -g

OBJCOPY = objcopy

LD = gcc
LDFLAGS = -nostdlib -nodefaultlibs -nostartfiles -fno-builtin
LDFLAGS += -static -fno-pic -fno-pie -Tlink.ld
LDFLAGS += -g

SRCS = boot.s kernel.s
OBJS = $(SRCS:.s=.o)

.PHONY: all clean

all: nop-x86.img

run: nop-x86.img
	qemu-system-i386 -cpu 486 -machine isapc,acpi=off -vga cirrus -drive file=nop-x86.img,format=raw

debug: nop-x86.img
	qemu-system-i386 -cpu 486 -machine isapc,acpi=off -vga cirrus -drive file=nop-x86.img,format=raw -s -S

nop-x86.img: nop-x86.bin
	dd if=/dev/zero of=nop-x86.img bs=512 count=60
	dd if=nop-x86.bin of=nop-x86.img seek=0 bs=512 conv=notrunc

nop-x86.bin: nop-x86.elf
	$(OBJCOPY) -O binary $< $@

nop-x86.elf: $(OBJS) link.ld
	$(LD) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.s
	$(NASM) $(AFLAGS) -o $@ $<

clean:
	rm -f nop-x86.elf nop-x86.bin nop-x86.img
	rm -f $(OBJS)

