; vim: ft=nasm
CPU 386

SECTION .text

GLOBAL boot1
BITS 16
boot1:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7800

; Ensure A20 line is unstuck
free_a20_stage1:
    in al, 0x64
    test al, 0x02
    jnz free_a20_stage1
    mov al, 0xD1
    out 0x64, al

free_a20_stage2:
    in al, 0x64
    test al, 0x02
    jnz free_a20_stage2
    mov al, 0xDF
    out 0x60, al

; Progress
    mov ah, 0x0E
    mov al, ':'
    int 0x10

; Read rest of bootloader
    cld
    xor ax, ax
    mov es, ax
    mov ds, ax

    mov bx, 0x500 ; load kernel into conventional memory at $500
    mov ah, 0x02  ; read sectors
    mov al, 58    ; 58 sectors (to fit in lower conventioal memory)
    mov ch, 0     ; cly 0
    mov cl, 2     ; sector 2 (1-indexed)
    mov dh, 0     ; head 0
    int 0x13

    EXTERN boot2
    jnc NEAR boot2

    mov ah, 0x0E
    mov al, '('
    int 0x10

halt:
    hlt
    jmp near halt

