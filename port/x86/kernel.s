; vim: ft=nasm
CPU 386

RSTOP    EQU 0x00007C00
PSTOP    EQU 0x00080000

HEAPBASE EQU 0x00100000

FLAG_NONE         EQU 0x00
FLAG_IMMEDIATE    EQU 0x01
FLAG_INLINE       EQU 0x02
FLAG_COMPILE_ONLY EQU 0x04

STATE_INTERPRET  EQU 0x00
STATE_COMPILE    EQU 0x01

SECTION .rodata

; This is an identity-mapped GDT
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

    jmp (GDT_CODE - GDT):_kabort

%define _LINK 0x00000000

%macro DEFENTRY 3
ALIGN 4
    DB %1
ALIGN 4
    DW %2_END - %2
    DB %3
%strlen _LEN %1
    DB _LEN
    DD _LINK
%define _LINK (%2 - 4)
%2:
%endmacro

%macro NIP 0
    add ebp, 4
%endmacro

%macro DROP 0
    mov eax, [ebp]
    NIP
%endmacro

%macro PSPUSH 1
    sub ebp, 4
    mov [ebp], eax
    mov eax, %1
%endmacro

%macro PSPOP 1
    mov %1, eax
    DROP
%endmacro

BITS 32

DEFENTRY "ktok", _ktok, FLAG_NONE
.next_space:
    call _kread
    PSPOP edx
    cmp edx, ' '
    jbe .next_space

    mov edi, [VAR_KSTR]
    inc edi
.next_char:
    mov BYTE [edi], dl
    inc edi
    call _kread
    PSPOP edx
    cmp edx, ' '
    ja .next_char

    mov ecx, edi
    mov edi, [VAR_KSTR]
    sub ecx, edi
    dec ecx
    mov BYTE [edi], cl

    PSPUSH [VAR_KSTR]
    ret
_ktok_END:

DEFENTRY "kbuf", _kbuf, FLAG_NONE
    call _ktok
    mov esi, eax
    movzx ecx, BYTE [esi]
    inc ecx
    mov edi, [VAR_KBUF]
    mov eax, edi
    rep movsb
    ret
_kbuf_END:

; Real kernel entry/reset point
DEFENTRY "kabort", _kabort, FLAG_NONE
    cli
    mov ax, (GDT_DATA - GDT)
    mov ds, ax
    mov es, ax
    mov ss, ax

    xor ax, ax
    mov fs, ax
    mov gs, ax

    mov esp, RSTOP
    mov ebp, PSTOP

    call _ktok
    call _kfind
 
_kabort_END:

SECTION .data

