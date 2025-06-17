OUTPUT_ARCH(i386)
OUTPUT_FORMAT(elf32-i386)

SECTIONS {
    . = 0x7C00;
    .boot : {
        boot.o(.text)
    }

    . = 0x7DFE;
    .mbrsig : {
        SHORT(0xAA55);
    }

    . = 0x7E00;
    .text : {
        kernel.o(.text)
        kernel.o(.rodata)
        *(.text)
    }

    .rodata : {
        *(.rodata)
    }

    .stab : {
        *(.stab)
    }

    .stabstr : {
        *(.stabstr)
    }

    .data : {
        *(.data)
    }

    .bss : {
        *(.bss)
    }

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
        *(.note.gnu.build-id)
    }
}

