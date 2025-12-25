; vim: ft=nasm
CPU 386

SECTION .text

BITS 16

GLOBAL X86Boot
X86Boot:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, X86Boot ; Place stack below bootloader

; Enable A20 line
WaitKBCtrl1:
    in al, 0x64
    test al, 0x02
    jnz WaitKBCtrl1
    mov al, 0xD1
    out 0x64, al
WaitKBCtrl2:
    in al, 0x64
    test al, 0x02
    jnz WaitKBCtrl2
    mov al, 0xDF
    out 0x60, al

    cld
    xor ax, ax
    mov ah, 0x02       ; BIOS read sectors function
    mov al, 20         ; Number of sectors to read
    mov bx, 0x7E00
    mov ch, 0          ; Cylinder 0
    mov cl, 2          ; Sector 2 (first sector after bootloader)
    mov dh, 0          ; Head 0
    int 0x13

    EXTERN Go32
    jnc Go32

Halt:
    hlt
    jmp Halt

