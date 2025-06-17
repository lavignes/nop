; vim: ft=nasm
CPU 386

SECTION .rodata

; This is a identity-mapped GDT
ALIGN 4
GDT:
    DW 0, 0
    DB 0, 0, 0, 0
GDT_CODE:
    DW 0xFFFF     ; segment limit: bits 0-15
    DW 0x0000     ; segment base: bits 0-15
    DB 0x00       ; segment base: bits 16-23
    DB 0b10011010 ; access byte
    DB 0b11001111 ; segment length: bits 16-19, flags (4 bits)
    DB 0x00       ; segment base: bits 24-31
GDT_DATA:
    DW 0xFFFF
    DW 0x0000
    DB 0x00
    DB 0b10010010
    DB 0b11001111
    DB 0x00
GDT_END:

GDTR:
    DW (GDT_END - GDT - 1)
    DD GDT

SECTION .text

; Minimal code to jump to protected mode
GLOBAL kinit
BITS 16
kinit:
    cli
    lgdt [GDTR]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp (GDT_CODE - GDT):kabort

; Real kernel entry/reset point
BITS 32
kabort:
    mov ax, (GDT_DATA - GDT)
    mov ds, ax
    mov es, ax
    mov ss, ax

    xor ax, ax
    mov fs, ax
    mov gs, ax

