; vim: ft=nasm
CPU 386

; This is a an ultra-minimal x86 bootloader that simply loads the kernel
; image into memory and jumps to it.

KERNEL_ADDR EQU 0x7E00
KERNEL_SIZE EQU 59     ; Size in sectors (512 bytes)

SECTION .text

GLOBAL Boot
BITS 16
Boot:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00 ; Place stack before the bootsector for now

; In early x86 processes, the A20 (address line 20) and up is not enabled
; and all memory access is limited to 1MB. To load a kernel larger than
; 1MB, we need to enable it.

WaitKeyboardController1:
    in al, 0x64
    test al, 2
    jnz WaitKeyboardController1
    mov al, 0xD1
    out 0x64, al

WaitKeyboardController2:
    in al, 0x64
    test al, 2
    jnz WaitKeyboardController2
    mov al, 0xDF
    out 0x60, al

; Now we can load the kernel image from disk.
    cld
    xor ax, ax
    mov es, ax
    mov ds, ax

    mov bx, KERNEL_ADDR
    mov ah, 0x02        ; BIOS read sector function
    mov al, KERNEL_SIZE ; Number of sectors to read
    mov ch, 0           ; Cylinder 0
    mov cl, 2           ; Sector 2 (first sector is boot sector)
    mov dh, 0           ; Head 0
    int 0x13            ; Execute

    EXTERN Kinit
    jnc Kinit

Halt:
    hlt
    jmp near Halt
