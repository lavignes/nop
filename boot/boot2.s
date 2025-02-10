; vim: ft=nasm
CPU 386

SECTION .text

GLOBAL boot2
BITS 16
boot2:
; Progress
    mov ah, 0x0E
    mov al, 'k'
    int 0x10

; Setup identity paging and jump to protected mode
    cli
    lgdt [GDTR]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp (GDT_CODE - GDT):protected_mode

BITS 32
protected_mode:
    mov ax, (GDT_DATA - GDT)
    mov ds, ax
    mov ss, ax
    mov es, ax

    xor ax, ax
    mov fs, ax
    mov gs, ax

    EXTERN _SYSQUIT
    jmp _SYSQUIT

SECTION .rodata

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

