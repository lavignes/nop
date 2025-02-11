; vim: ft=nasm
CPU 386

RSBASE   EQU 0x1000
PSBASE   EQU 0x7C00

HEAPBASE EQU 0x10000
HEAPMAX  EQU 0xA0000

FLAG_NONE         EQU 0x00
FLAG_IMMEDIATE    EQU 0x01
FLAG_INLINE       EQU 0x02
FLAG_COMPILE_ONLY EQU 0x04

STATE_INTERPRETING EQU 0
STATE_COMPILING    EQU 1

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
    PSPUSH VAR_SYSALLOC
    ret
_SYSALLOC_END:

DEFENTRY "SYSHERE", _SYSHERE, FLAG_INLINE
    PSPUSH [VAR_SYSALLOC]
    ret
_SYSHERE_END:

DEFENTRY "SYSIN", _SYSIN, FLAG_INLINE
    PSPUSH VAR_SYSIN
    ret
_SYSIN_END:

DEFENTRY "SYSSTR", _SYSSTR, FLAG_INLINE
    PSPUSH VAR_SYSSTR
    ret
_SYSSTR_END:

DEFENTRY "SYSBUF", _SYSBUF, FLAG_INLINE
    PSPUSH VAR_SYSBUF
    ret
_SYSBUF_END:

DEFENTRY "SYSINT", _SYSINT, FLAG_INLINE
    PSPUSH VAR_SYSINT
    ret
_SYSINT_END:

DEFENTRY "SYSPANIC", _SYSPANIC, FLAG_INLINE
    PSPUSH VAR_SYSPANIC
    ret
_SYSPANIC_END:

DEFENTRY "(PANIC)", _panic, FLAG_NONE
    cli
    DROP

    mov esi, [VAR_SYSSTR]
    movzx ecx, BYTE [esi]
    inc esi
    mov edi, 0xB8000
.next_char:
    movzx ebx, BYTE [esi]
    mov BYTE [edi], bl
    mov BYTE [edi+1], 0x47
    inc esi,
    add edi, 2
    dec ecx
    jnz .next_char

.loop:
    hlt
    jmp .loop
_panic_END:

DEFENTRY "(INTERRUPT)", _interrupt, FLAG_NONE
    DROP
    DROP
    ret
_interrupt_END:

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

DEFENTRY "(SYSTOK)", _ll_SYSTOK, FLAG_NONE
.next_space:
    call _from_SYSIN
    PSPOP ecx
    cmp ecx, ' '
    jbe .next_space

    mov edi, [VAR_SYSSTR]
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
    mov edi, [VAR_SYSSTR]
    sub ecx, edi
    dec ecx
    mov BYTE [edi], cl

    PSPUSH [VAR_SYSSTR]
    ret
_ll_SYSTOK_END:

DEFENTRY "SYSTOK", _SYSTOK, FLAG_NONE
    call _ll_SYSTOK
    mov esi, eax
    movzx ecx, BYTE [esi]
    inc ecx
    mov edi, [VAR_SYSBUF]
    mov eax, edi
    rep movsb
    ret
_SYSTOK_END:

DEFENTRY "SYSFIND", _SYSFIND, FLAG_NONE
    mov edi, eax
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
    mov eax, edx
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
    DROP
    ret
_write_END:

DEFENTRY "b,", _writebyte, FLAG_INLINE
    mov edx, [VAR_SYSALLOC]
    mov edi, [edx]
    mov BYTE [edi], al
    inc edi
    mov [edx], edi
    DROP
    ret
_writebyte_END:

DEFENTRY "h,", _writehalf, FLAG_INLINE
    mov edx, [VAR_SYSALLOC]
    mov edi, [edx]
    mov WORD [edi], ax
    add edi, 2
    mov [edx], edi
    DROP
    ret
_writehalf_END:

DEFENTRY "mov", _move, FLAG_INLINE
    mov ecx, eax
    mov edi, [ebp]
    mov esi, [ebp+4]
    rep movsd
    mov eax, [ebp+8]
    add ebp, 12
    ret
_move_END:

DEFENTRY "bmov", _bmove, FLAG_INLINE
    mov ecx, eax
    mov edi, [ebp]
    mov esi, [ebp+4]
    rep movsb
    mov eax, [ebp+8]
    add ebp, 12
    ret
_bmove_END:

DEFENTRY "hmov", _hmove, FLAG_INLINE
    mov ecx, eax
    mov edi, [ebp]
    mov esi, [ebp+4]
    rep movsw
    mov eax, [ebp+8]
    add ebp, 12
    ret
_hmove_END:

DEFENTRY "bmov,", _bmovewrite, FLAG_INLINE
    mov ecx, eax
    mov edx, [VAR_SYSALLOC]
    mov edi, [edx]
    mov esi, [ebp]
    rep movsb
    mov [edx], edi
    mov eax, [ebp+4]
    add ebp, 8
    ret
_bmovewrite_END:

DEFENTRY "align", _align, FLAG_INLINE
    add eax, 3
    and eax, 0xFFFFFFFC
    ret
_align_END:

DEFENTRY "halign", _halign, FLAG_INLINE
    inc eax
    and eax, 0xFFFFFFFE
    ret
_halign_END:

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

DEFENTRY "over", _over, FLAG_INLINE
    sub ebp, 4
    mov [ebp], eax
    mov eax, [ebp+8]
    ret
_over_END:

DEFENTRY ">R", _to_return, FLAG_NONE
    pop edx
    push eax
    DROP
    push edx
    ret
_to_return_END:

DEFENTRY "<R", _from_return, FLAG_NONE
    pop edx
    sub ebp, 4
    mov [ebp], eax
    pop eax
    push edx
    ret
_from_return_END:

DEFENTRY "[exit]", _exit, FLAG_COMPILE_ONLY | FLAG_IMMEDIATE
    mov ecx, [VAR_SYSALLOC]
    mov edi, [ecx]
    mov BYTE [edi], 0xC3   ; retn
    inc edi
    mov [ecx], edi
    ret
_exit_END:

DEFENTRY "[call]", _call, FLAG_COMPILE_ONLY | FLAG_IMMEDIATE
    call _ll_SYSTOK
    call _SYSFIND
    PSPOP edx

    ; get POINTER
    add edx, 4

    mov ecx, [VAR_SYSALLOC]
    mov edi, [ecx]
    mov BYTE [edi], 0xBA   ; mov edx,
    inc edi
    mov [edi], edx         ;          POINTER
    add edi, 4
    mov WORD [edi], 0xD2FF ; call edx
    add edi, 2
    mov [ecx], edi
    ret
_call_END:

DEFENTRY "[lit]", _lit, FLAG_COMPILE_ONLY | FLAG_IMMEDIATE
    ; compile literal
    mov ecx, [VAR_SYSALLOC]
    mov edi, [ecx]
    mov WORD [edi], 0xED83 ; sub ebp,
    add edi, 2
    mov BYTE [edi], 0x04   ;          4
    inc edi
    mov WORD [edi], 0x4589 ; mov [ebp+ ], eax
    add edi, 2
    mov BYTE [edi], 0x00   ;          0
    inc edi
    mov BYTE [edi], 0xB8   ; mov eax,
    inc edi
    mov DWORD [edi], eax   ;          LITERAL
    add edi, 4
    mov [ecx], edi
    DROP
    ret
_lit_END:

DEFENTRY "[", _start_compile, FLAG_INLINE | FLAG_IMMEDIATE
    mov DWORD [VAR_SYSSTATE], STATE_COMPILING
    ret
_start_compile_END:

DEFENTRY "]", _end_compile, FLAG_INLINE | FLAG_IMMEDIATE
    mov DWORD [VAR_SYSSTATE], STATE_INTERPRETING
    ret
_end_compile_END:

DEFENTRY "\", _line_comment, FLAG_IMMEDIATE
.loop:
    call _from_SYSIN
    PSPOP ecx
    cmp ecx, 10 ; '\n' why NASM!?
    jne .loop
    ret
_line_comment_END:

io_wait:
    pusha
    mov ecx, 0xFFFF
.delay:
    dec ecx
    jnz .delay
    popa
    ret

GLOBAL _SYSQUIT
DEFENTRY "SYSQUIT", _SYSQUIT, FLAG_NONE
    cli
    mov esp, RSBASE
    mov ebp, PSBASE

    mov DWORD [SYSIN+0], NUCLEUSSRC-1
    mov DWORD [SYSIN+4], NUCLEUSSIZE+1
    mov DWORD [SYSIN+8], 0

    mov DWORD [SYSALLOC+0], HEAPBASE
    mov DWORD [SYSALLOC+4], HEAPMAX
    mov DWORD [SYSALLOC+8], 0

    mov DWORD [VAR_SYSSTATE], STATE_INTERPRETING
    mov DWORD [VAR_SYSDICT], _LINK
    mov DWORD [VAR_SYSALLOC], SYSALLOC
    mov DWORD [VAR_SYSIN], SYSIN
    mov DWORD [VAR_SYSSTR], SYSSTR
    mov DWORD [VAR_SYSBUF], SYSBUF
    mov DWORD [VAR_SYSINT], _interrupt
    mov DWORD [VAR_SYSPANIC], _panic

    ; Intialize the IDT
    mov esi, interrupt_handler0
    mov edi, IDT
    mov ecx, 256
.fill_idt:
    mov WORD [edi], si
    mov WORD [edi+2], cs    ; code segment to load
    mov BYTE [edi+4], 0     ; unused
    mov BYTE [edi+5], 0x8E  ; type: interrupt
    mov edx, esi
    shr edx, 16
    mov WORD [edi+6], dx
    add edi, 8
    add esi, (interrupt_handler0_END - interrupt_handler0)
    dec ecx
    jnz .fill_idt

    ; Initialize the PICs and remap their IRQs
    mov eax, 0x11
    out 0x20, al
    call io_wait
    out 0xA0, al
    call io_wait

    ; IRQ 0-7 -> Interrupt 0x20 - 0x27
    mov eax, 0x20
    out 0x21, al
    call io_wait

    ; IRQ 8-15 -> Interrupt 0x28 - 0x2F
    mov eax, 0x28
    out 0xA1, al
    call io_wait

    ; Enable cascade to second PIC
    mov eax, 4
    out 0x21, al
    call io_wait
    mov eax, 2
    out 0xA1, al
    call io_wait

    ; Enable PICs x86 mode
    mov eax, 1
    out 0x21, al
    call io_wait
    out 0xA1, al
    call io_wait

    ; Enable the PICs
    xor eax, eax
    out 0x21, al
    call io_wait
    out 0xA1, al
    call io_wait

    ; Enable interrupts
    lidt [IDTR]
    sti

.interpret:
    call _ll_SYSTOK
    call _SYSFIND
    PSPOP edx
    test edx, edx
    jz .not_found

    ; get POINTER
    add edx, 4

    test DWORD [VAR_SYSSTATE], STATE_COMPILING
    jz .execute

    ; check flags
    test BYTE [edx-6], FLAG_IMMEDIATE
    jnz .execute

    test BYTE [edx-6], FLAG_INLINE
    jz .compile

    ; compile inline
    mov ebx, [VAR_SYSALLOC]
    mov edi, [ebx]
    movzx ecx, WORD [edx-8]
    dec ecx ; ignore retn byte
    mov esi, edx
    rep movsb
    mov [ebx], edi
    jmp .interpret

.compile:
    ; compile absolute call
    mov ecx, [VAR_SYSALLOC]
    mov edi, [ecx]
    mov BYTE [edi], 0xBA   ; mov edx,
    inc edi
    mov [edi], edx         ;          POINTER
    add edi, 4
    mov WORD [edi], 0xD2FF ; call edx
    add edi, 2
    mov [ecx], edi
    jmp .interpret

.execute:
    call edx
    jmp .interpret

.not_found:
    mov edi, [VAR_SYSSTR]
    movzx ecx, BYTE [edi]
    inc edi
    PSPUSH 0

    ; assume base 10
    mov ebx, 10
.parse_digit:
    test ecx, ecx
    jz .try_literal
    movzx edx, BYTE [edi]

    ; test for base prefix
    cmp edx, '$'
    jne .try_base2
    mov ebx, 16
    jmp .next_char
.try_base2:
    cmp edx, '%'
    jne .try_underscore
    mov ebx, 2
    jmp .next_char
    ; ignore underscores in numbers
.try_underscore:
    cmp edx, '_'
    je .next_char

.test_digits:
    mov esi, DIGITS
.next_digit:
    cmp esi, DIGITSEND
    je .not_digit
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

.not_digit:
    DROP
    PSPUSH 0
    mov edx, [VAR_SYSPANIC]
    call edx
    jmp .interpret

.try_literal:
    test DWORD [VAR_SYSSTATE], STATE_COMPILING
    jz .interpret

    call _lit
    jmp .interpret
_SYSQUIT_END:

; Do not place more entries below because we want _LINK
; to always be _SYSQUIT

%macro INTERRUPT_HANDLER 1
interrupt_handler%1:
    PSPUSH 0
    nop        ; the length of these needs to be identical
    PSPUSH %1
    call interrupt_common
    iret
interrupt_handler%1_END:
%endmacro

%macro INTERRUPT_HANDLER_WITH_CODE 1
interrupt_handler%1:
    PSPUSH 0
    pop eax    ; note that we insert a nop above
    PSPUSH %1
    call interrupt_common
    iret
interrupt_handler%1_END:
%endmacro

interrupt_common:
    push ebx
    push ecx
    push edx
    push esi
    push edi
    mov edx, [VAR_SYSINT]
    call edx
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    ret

; Interrupts 0 - 7
%assign i 0
%rep 8
INTERRUPT_HANDLER i
%assign i i+1
%endrep
INTERRUPT_HANDLER_WITH_CODE 8
INTERRUPT_HANDLER_WITH_CODE 9
INTERRUPT_HANDLER_WITH_CODE 10
INTERRUPT_HANDLER_WITH_CODE 11
INTERRUPT_HANDLER_WITH_CODE 12
INTERRUPT_HANDLER_WITH_CODE 13
INTERRUPT_HANDLER_WITH_CODE 14
INTERRUPT_HANDLER           15
INTERRUPT_HANDLER           16
INTERRUPT_HANDLER_WITH_CODE 17
; Interrupts 18 - 255
%assign i 18
%rep 238
INTERRUPT_HANDLER i
%assign i i+1
%endrep

%if (interrupt_handler0_END - interrupt_handler0) != \
    (interrupt_handler8_END - interrupt_handler8)
%error "All interrupt handlers need to be the exact same size!"
%endif

SECTION .data

ALIGN 4
SYSSTR:
    DB 0
    TIMES 255 DB 0

ALIGN 4
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
IDT:
    TIMES 256 DQ 0
IDT_END:

ALIGN 4
VAR_SYSSTATE:   DD 0
VAR_SYSDICT:    DD 0
VAR_SYSALLOC:   DD 0
VAR_SYSIN:      DD 0
VAR_SYSSTR:     DD 0
VAR_SYSBUF:     DD 0
VAR_SYSINT:     DD 0
VAR_SYSPANIC:   DD 0

SECTION .rodata

ALIGN 4
IDTR:
    DW (IDT_END - IDT - 1)
    DD IDT

ALIGN 4
DIGITS:
    DB "0123456789ABCDEF"
DIGITSEND:

ALIGN 4
NUCLEUSSRC:
    INCBIN "nucleus.nop"
NUCLEUSSIZE EQU $ - NUCLEUSSRC

