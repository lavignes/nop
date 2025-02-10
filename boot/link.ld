OUTPUT_ARCH(i386)
OUTPUT_FORMAT(elf32-i386)

SECTIONS {
    . = 0x7C00;
    .boot1 : {
        boot/boot1.o(.text)
    }

    . = 0x7DFE;
    .mbrsig : {
        SHORT(0xAA55);
    }

    . = 0x7E00;
    .text : {
        boot/boot2.o(.text)
        boot/boot2.o(.rodata)
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

