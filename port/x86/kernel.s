; vim: ft=nasm
CPU 386

RTOP    EQU 0x00007C00
PTOP    EQU 0x00080000

HEAPBASE EQU 0x00100000

FLAG_NONE         EQU 0x00
FLAG_IMMEDIATE    EQU 0x01
FLAG_INLINE       EQU 0x02
FLAG_COMPILE_ONLY EQU 0x04

STATE_INTERPRET  EQU 0x00
STATE_COMPILE    EQU 0x01

SECTION .text

; Minimal code to jump to protected mode
GLOBAL Kinit
BITS 16
Kinit:
    cli
    lgdt [gdtr]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp (gdt_code - gdt):_Kabort

%define _LINK 0x00000000

%macro DEFENTRY 3
ALIGN 4
    DB %1
ALIGN 4
    DW %2.End - %2
    DB %3
%strlen _LEN %1
    DB _LEN
    DD _LINK
%define _LINK (%2 - 4)
%2:
%endmacro

%macro PNIP 0
    add ebp, 4
%endmacro

%macro PDROP 0
    mov eax, [ebp]
    PNIP
%endmacro

%macro PPUSH 1
    sub ebp, 4
    mov [ebp], eax
    mov eax, %1
%endmacro

%macro PPOP 1
    mov %1, eax
    PDROP
%endmacro

BITS 32

DEFENTRY "Knext", _Knext, FLAG_NONE
    PPUSH [kin]
    dec DWORD [eax+4]
    inc DWORD [eax]
    mov eax, [eax]
    movzx eax, BYTE [eax]
    ret
.End:

DEFENTRY "Keat", _Keat, FLAG_NONE
.NextSpace:
    call _Knext
    PPOP edx
    cmp edx, ' '
    jbe .NextSpace

    mov edi, [ktmp]
    inc edi
.NextChar:
    mov BYTE [edi], dl
    inc edi
    call _Knext
    PPOP edx
    ; This is a special case to make strings nicer.
    ; When we encounter a " (double quote, that always terminates a word)
    cmp edx, '"'
    je .StringTerminate
    cmp edx, ' '
    ja .NextChar
    jmp .Done

.StringTerminate:
    inc ecx ; Make sure we include the " in the word

.Done:
    mov ecx, edi
    mov edi, [ktmp]
    sub ecx, edi
    dec ecx
    mov BYTE [edi], cl
    ret
.End:

DEFENTRY "Ktok", _Ktok, FLAG_NONE
    call _Knext
    mov esi, [ktmp]
    movzx ecx, BYTE [esi]
    inc ecx
    mov edi, [kbuf]
    mov eax, edi
    rep movsb
    ret
.End:

DEFENTRY "Kfind", _Kfind, FLAG_NONE
    mov edi, eax
    movzx ecx, BYTE [edi]
    inc edi
    mov edx, kdict

.NextLink:
    mov edx, [edx]
    test edx, edx
    je .EntryInEdx

    ; Entry name length compare?
    cmp cl, BYTE [edx-1]
    jne .NextLink

    ; Load entry name ptr into ESI (EDX-4-ECX) & 0xFFFFFFFC
    mov esi, edx
    sub esi, 4
    sub esi, ecx
    and esi, 0xFFFFFFFC

    ; Copare name
    push edi
    push ecx
    repe cmpsb
    pop ecx
    pop edi
    jne .NextLink

.EntryInEdx
    mov eax, edx
    ret
.End:

DEFENTRY ":Lit", _Lit, FLAG_COMPILE_ONLY | FLAG_IMMEDIATE
.End:

DEFENTRY "Krun", _Krun, FLAG_NONE
    call _Ktok
    call _Kfind
    PPOP edx
    test edx, edx
    jz .NotFound

    add edx, 4
    test DWORD [kstate], STATE_COMPILE
    jz .Execute

    movzx ecx, BYTE [edx-6]
    test ecx, FLAG_IMMEDIATE
    jnz .Execute
    test ecx, FLAG_INLINE
    jz .Compile

    ; Inline compile
    mov ebx, [kalloc]
    mov edi, [ebx]
    movzx ecx, WORD [edx-8]
    dec ecx ; Ignore retn byte at end
    mov esi, edx
    rep movsb
    mov [ebx], edi
    jmp _Krun

.Compile:
    mov ebx, [kalloc]
    mov edi, [ebx]
    mov BYTE [edi], 0xBA    ; mov edx, DWORD
    inc edi
    mov [edi], edx          ;          pointer
    add edi, 4
    mov WORD [edi], 0xD2FF  ; call edx
    add edi, 2
    mov [ecx], edi
    jmp _Krun

.Execute:
    call edx
    jmp _Krun

.NotFound:
    mov edi, [ktmp]
    movzx ecx, BYTE [edi]
    inc edi
    PPUSH 0

    mov ebx, 10
.ParseDigit:
    test ecx, ecx
    jz .TryLiteral
    movzx edx, BYTE [edi]

    cmp edx, '$'
    jne .TryBase2
    mov ebx, 16
    jmp .NextChar
.TryBase2:
    cmp edx, '%'
    jne .TryUnderscore
    mov ebx, 2
    jmp .NextChar
.TryUnderscore:
    cmp edx, '_'
    je .NextChar

    mov esi, digits
.NextDigit:
    sub esi, digits.end
    je .NotDigit
    cmp dl, BYTE [esi]
    je .AccumDigit
    inc esi
    jmp .NextDigit
.AccumDigit:
    sub esi, digits
    xor edx, edx
    mul ebx
    add eax, esi

.NextChar:
    dec ecx
    inc edi
    jmp .ParseDigit

.NotDigit:
    PDROP
    PPUSH 0
    mov edx, [kpanic]
    call edx
    jmp _Krun

.TryLiteral:
    test DWORD [kstate], STATE_COMPILE
    jz _Krun
    call _Lit

    jmp _Krun
.End:

; Real kernel entry/reset point
DEFENTRY "Kabort", _Kabort, FLAG_NONE
    cli
    mov ax, (gdt_data - gdt)
    mov ds, ax
    mov es, ax
    mov ss, ax

    xor ax, ax
    mov fs, ax
    mov gs, ax

    mov esp, RTOP
    mov ebp, PTOP

    jmp _Krun
.End:

; Place no more entries after this point
; _Kabort must always be the value of _LINK

SECTION .data

; ( This is a comment )
; :Fn Testing ( a -> b )
;    [ :Inline :Immediate ]
;    0= If
;       2+ <<IN Iso:gar Drop
;    Then
; ;

ALIGN 4
ktmp_state:
    DB 0
    TIMES 255 DB 0

ALIGN 4
kbuf_state:
    DB 0
    TIMES 255 DB 0

ALIGN 4
kin_state:
    DD 0
    DD 0
    DD 0

ALIGN 4
kalloc_state:
    DD 0
    DD 0
    DD 0

ALIGN 4
kstate:     DD 0
kptop:      DD 0
krtop:      DD 0
kdict:      DD 0
kalloc:     DD 0
kin:        DD 0
kpanic:     DD 0
ktmp:       DD 0
kbuf:       DD 0

SECTION .rodata

digits:
    DB '0123456789ABCDEF'
.end:

; This is an identity-mapped GDT
ALIGN 4
gdt:
    DW 0, 0
    DB 0, 0, 0, 0
gdt_code:
    DW 0xFFFF     ; segment limit: bits 0-15
    DW 0x0000     ; segment base: bits 0-15
    DB 0x00       ; segment base: bits 16-23
    DB 0b10011010 ; access byte
    DB 0b11001111 ; segment length: bits 16-19, flags (4 bits)
    DB 0x00       ; segment base: bits 24-31
gdt_data:
    DW 0xFFFF
    DW 0x0000
    DB 0x00
    DB 0b10010010
    DB 0b11001111
    DB 0x00
gdtr:
    DW (gdtr - gdt - 1)
    DD gdt

