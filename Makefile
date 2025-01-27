rwildcard = $(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))

NASM = nasm
AFLAGS = -Iboot
AFLAGS += -f elf32
AFLAGS += -g

OBJCOPY = objcopy

LD = gcc-14
LDFLAGS = -nostdlib -nodefaultlibs -nostartfiles -fno-builtin
LDFLAGS += -static -fno-pic -fno-pie -Tboot/link.ld
LDFLAGS += -g

ASRCS = $(call rwildcard,boot,*.s)
OBJS = $(ASRCS:.s=.o)

.PHONY: all clean run debug

all: nop.img

run: nop.img
	qemu-system-i386 -cpu 486 -machine isapc,acpi=off -vga cirrus -drive file=nop.img,format=raw

debug: nop.img
	qemu-system-i386 -s -S -cpu 486 -machine isapc,acpi=off -vga cirrus -drive file=nop.img,format=raw

nop.img: bin/boot.bin
	dd if=/dev/zero of=nop.img bs=512 count=59
	dd if=bin/boot.bin of=nop.img seek=0 bs=512 conv=notrunc

bin/boot.bin: bin/boot.elf
	$(OBJCOPY) -S -O binary $< $@

bin/boot.elf: $(OBJS) boot/link.ld
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.s
	$(NASM) $(AFLAGS) -o $@ $<

nucleus.o: nucleus.s nucleus.nop

clean:
	rm -f bin/*
	rm -f nop.img
	rm -f $(call rwildcard,boot,*.o)

