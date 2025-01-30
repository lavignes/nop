; vim: ft=nasm
CPU 386

RSBASE   EQU 0x7800
PSBASE   EQU 0x80000

HEAPBASE EQU 0x8800
HEAPMAX  EQU 0x70000

FLAG_NONE         EQU 0x00
FLAG_IMMEDIATE    EQU 0x01
FLAG_COMPILE_ONLY EQU 0x02
FLAG_INLINE       EQU 0x04

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

SECTION .text

DEFENTRY "SYSSTATE", _SYSSTATE, FLAG_INLINE
    PSPUSH VAR_SYSSTATE
    ret
_SYSSTATE_END:

DEFENTRY "SYSDICT", _SYSDICT, FLAG_INLINE
    PSPUSH VAR_SYSDICT
    ret
_SYSDICT_END:

DEFENTRY "SYSALLOC", _SYSALLOC, FLAG_INLINE
    PSPUSH VAR_SYSDICT
    ret
_SYSALLOC_END:

DEFENTRY "SYSHERE", _SYSHERE, FLAG_INLINE
    mov edx, [VAR_SYSALLOC]
    PSPUSH [edx]
    ret
_SYSHERE_END:

DEFENTRY "SYSIN", _SYSIN, FLAG_INLINE
    PSPUSH VAR_SYSIN
    ret
_SYSIN_END:

DEFENTRY "SYSBUF", _SYSBUF, FLAG_INLINE
    PSPUSH VAR_SYSBUF
    ret
_SYSBUF_END:

DEFENTRY "<SYSIN", _from_SYSIN, FLAG_NONE
    PSPUSH [VAR_SYSIN]
    ; Decrement NUCLEUSSIZE
    dec DWORD [eax+4]
    ; Increment NUCLEUSSRC
    inc DWORD [eax]
    ; Read byte
    mov eax, [eax]
    movzx eax, BYTE [eax]
    ret
_from_SYSIN_END:

DEFENTRY "SYSTOK", _SYSTOK, FLAG_NONE
.next_space:
    call _from_SYSIN
    PSPOP ecx
    cmp ecx, ' '
    jbe .next_space

    mov edi, [VAR_SYSBUF]
    inc edi
.next_char:
    mov BYTE [edi], cl
    inc edi
    call _from_SYSIN
    PSPOP ecx
    cmp ecx, ' '
    ja .next_char

    ; Store string length
    mov ecx, edi
    mov edi, [VAR_SYSBUF]
    sub ecx, edi
    dec ecx
    mov BYTE [edi], cl
    ret
_SYSTOK_END:

DEFENTRY "SYSFIND", _SYSFIND, FLAG_NONE
    mov edi, [VAR_SYSBUF]
    movzx ecx, BYTE [edi]
    inc edi
    mov edx, VAR_SYSDICT

.next_link:
    ; Follow the pointer
    mov edx, [edx]
    test edx, edx
    je .entry_in_edx

    ; Quick compare length
    cmp cl, BYTE [edx-1]
    jne .next_link

    ; Load entry name ptr into ESI (EDX-4-ECX) & 0xFFFFFFFC
    mov esi, edx
    sub esi, 4
    sub esi, ecx
    and esi, 0xFFFFFFFC

    ; Compare strings
    push edi
    push ecx
    repe cmpsb
    pop ecx
    pop edi

    jne .next_link

.entry_in_edx:
    PSPUSH edx
    ret
_SYSFIND_END:

DEFENTRY "@", _fetch, FLAG_INLINE
    mov eax, [eax]
    ret
_fetch_END:

DEFENTRY "!", _store, FLAG_INLINE
    mov ecx, [ebp]
    mov [eax], ecx
    add ebp, 8
    mov eax, [ebp-4]
    ret
_store_END:

DEFENTRY "b@", _fetchbyte, FLAG_INLINE
    movsx eax, BYTE [eax]
    ret
_fetchbyte_END:

DEFENTRY "b!", _storebyte, FLAG_INLINE
    mov ecx, [ebp]
    mov BYTE [eax], cl
    add ebp, 8
    mov eax, [ebp-4]
    ret
_storebyte_END:

DEFENTRY "ub@", _fetchubyte, FLAG_INLINE
    movzx eax, BYTE [eax]
    ret
_fetchubyte_END:

DEFENTRY "h@", _fetchhalf, FLAG_INLINE
    movsx eax, WORD [eax]
    ret
_fetchhalf_END:

DEFENTRY "h!", _storehalf, FLAG_INLINE
    mov ecx, [ebp]
    mov WORD [eax], cx
    add ebp, 8
    mov eax, [ebp-4]
    ret
_storehalf_END:

DEFENTRY "uh@", _fetchuhalf, FLAG_INLINE
    movzx eax, WORD [eax]
    ret
_fetchuhalf_END:

DEFENTRY ",", _write, FLAG_INLINE
    mov edx, [VAR_SYSALLOC]
    mov edi, [edx]
    mov [edi], eax
    add edi, 4
    mov [edx], edi
    ret
_write_END:

DEFENTRY "b,", _writebyte, FLAG_INLINE
    mov edx, [VAR_SYSALLOC]
    mov edi, [edx]
    mov BYTE [edi], al
    inc edi
    mov [edx], edi
    ret
_writebyte_END:

DEFENTRY "h,", _writehalf, FLAG_INLINE
    mov edx, [VAR_SYSALLOC]
    mov edi, [edx]
    mov WORD [edi], ax
    add edi, 2
    mov [edx], edi
    ret
_writehalf_END:

DEFENTRY "+", _add, FLAG_INLINE
    add eax, [ebp]
    NIP
    ret
_add_END:

DEFENTRY "-", _sub, FLAG_INLINE
    sub [ebp], eax
    DROP
    ret
_sub_END:

DEFENTRY "*", _mul, FLAG_INLINE
    imul eax, [ebp]
    NIP
    ret
_mul_END:

DEFENTRY "/", _div, FLAG_INLINE
    xor edx, edx
    mov ecx, eax
    mov eax, [ebp]
    idiv ecx
    NIP
    ret
_div_END:

DEFENTRY "%", _mod, FLAG_INLINE
    xor edx, edx
    mov ecx, eax
    mov eax, [ebp]
    idiv ecx
    mov eax, edx
    NIP
    ret
_mod_END:

DEFENTRY "u*", _umul, FLAG_INLINE
    xor edx, edx
    mul DWORD [ebp]
    NIP
    ret
_umul_END:

DEFENTRY "u/", _udiv, FLAG_INLINE
    xor edx, edx
    mov ecx, eax
    mov eax, [ebp]
    div ecx
    NIP
    ret
_udiv_END:

DEFENTRY "u%", _umod, FLAG_INLINE
    xor edx, edx
    mov ecx, eax
    mov eax, [ebp]
    div ecx
    mov eax, edx
    NIP
    ret
_umod_END:

DEFENTRY "and", _and, FLAG_INLINE
    and eax, [ebp]
    NIP
    ret
_and_END:

DEFENTRY "or", _or, FLAG_INLINE
    or eax, [ebp]
    NIP
    ret
_or_END:

DEFENTRY "xor", _xor, FLAG_INLINE
    xor eax, [ebp]
    NIP
    ret
_xor_END:

DEFENTRY "invert", _invert, FLAG_INLINE
    not eax
    ret
_invert_END:

DEFENTRY "=", _equ, FLAG_INLINE
    cmp eax, [ebp]
    sete al
    movzx eax, al
    NIP
    ret
_equ_END:

DEFENTRY "<>", _nequ, FLAG_INLINE
    cmp eax, [ebp]
    setne al
    movzx eax, al
    NIP
    ret
_nequ_END:

DEFENTRY "<", _lt, FLAG_INLINE
    cmp eax, [ebp]
    setg al
    movzx eax, al
    NIP
    ret
_lt_END:

DEFENTRY ">", _gt, FLAG_INLINE
    cmp eax, [ebp]
    setl al
    movzx eax, al
    NIP
    ret
_gt_END:

DEFENTRY "<=", _lte, FLAG_INLINE
    cmp eax, [ebp]
    setge al
    movzx eax, al
    NIP
    ret
_lte_END:

DEFENTRY ">=", _gte, FLAG_INLINE
    cmp eax, [ebp]
    setle al
    movzx eax, al
    NIP
    ret
_gte_END:

DEFENTRY "u<", _ult, FLAG_INLINE
    cmp eax, [ebp]
    seta al
    movzx eax, al
    NIP
    ret
_ult_END:

DEFENTRY "u>", _ugt, FLAG_INLINE
    cmp eax, [ebp]
    setb al
    movzx eax, al
    NIP
    ret
_ugt_END:

DEFENTRY "u<=", _ulte, FLAG_INLINE
    cmp eax, [ebp]
    setae al
    movzx eax, al
    NIP
    ret
_ulte_END:

DEFENTRY "u>=", _ugte, FLAG_INLINE
    cmp eax, [ebp]
    setbe al
    movzx eax, al
    NIP
    ret
_ugte_END:

DEFENTRY "drop", _drop, FLAG_INLINE
    DROP
    ret
_drop_END:

DEFENTRY "nip", _nip, FLAG_INLINE
    NIP
    ret
_nip_END:

DEFENTRY "swap", _swap, FLAG_INLINE
    xchg eax, [ebp]
    ret
_swap_END:

DEFENTRY "dup", _dup, FLAG_INLINE
    sub ebp, 4
    mov [ebp], eax
    ret
_dup_END:

DEFENTRY ">R", _to_return, FLAG_INLINE
    push eax
    DROP
    ret
_to_return_END:

DEFENTRY "<R", _from_return, FLAG_INLINE
    sub ebp, 4
    mov [ebp], eax
    pop eax
    ret
_from_return_END:

GLOBAL _SYSQUIT
DEFENTRY "SYSQUIT", _SYSQUIT, FLAG_NONE
    cli
    mov esp, RSBASE
    mov ebp, PSBASE-4

    mov DWORD [SYSIN+0], NUCLEUSSRC-1
    mov DWORD [SYSIN+4], NUCLEUSSIZE+1

    mov DWORD [SYSALLOC+0], HEAPBASE
    mov DWORD [SYSALLOC+4], HEAPMAX

    mov DWORD [VAR_SYSSTATE], 0
    mov DWORD [VAR_SYSDICT], _LINK
    mov DWORD [VAR_SYSALLOC], SYSALLOC
    mov DWORD [VAR_SYSIN], SYSIN
    mov DWORD [VAR_SYSBUF], SYSBUF

    ; TODO: setup basic hardware stuff like the IDT
    ; and enable interrupts

.interpret:
    call _SYSTOK
    call _SYSFIND
    PSPOP edx
    test edx, edx
    jz .not_found

    add edx, 4
    call edx
    jmp .interpret

.not_found:
    mov edi, [VAR_SYSBUF]
    movzx ecx, BYTE [edi]
    inc edi
    xor eax, eax

.parse_digit:
    test ecx, ecx
    jz .interpret

    ; assume base 10
    mov ebx, 10
    movzx edx, BYTE [edi]

    ; test for base prefix
    cmp edx, '$'
    jne .try_base2
    mov ebx, 16
    jmp .next_char
.try_base2:
    cmp edx, '%'
    jne .test_digits
    mov ebx, 2
    jmp .next_char

.test_digits:
    mov esi, DIGITS
.next_digit:
    cmp esi, DIGITSEND
    je .next_char
    cmp dl, BYTE [esi]
    je .accum_digit
    inc esi
    jmp .next_digit

.accum_digit:
    sub esi, DIGITS
    xor edx, edx
    mul ebx
    add eax, esi

.next_char:
    dec ecx
    inc edi
    jmp .parse_digit
_SYSQUIT_END:

; Do not place more entries below because we want _LINK
; to always be _SYSQUIT

SECTION .data

SYSBUF:
    DB 0
    TIMES 255 DB 0

ALIGN 4
SYSIN:
    DD 0
    DD 0
    DD 0

ALIGN 4
SYSALLOC:
    DD 0
    DD 0
    DD 0

ALIGN 4
VAR_SYSSTATE:   DD 0
VAR_SYSDICT:    DD 0
VAR_SYSALLOC:   DD 0
VAR_SYSIN:      DD 0
VAR_SYSBUF:     DD 0

SECTION .rodata

DIGITS:
    DB "0123456789ABCDEF"
DIGITSEND:

NUCLEUSSRC:
    INCBIN "nucleus.nop"
NUCLEUSSIZE EQU $ - NUCLEUSSRC

