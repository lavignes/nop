; vim: ft=nasm
CPU 386

RTOP     EQU 0x00007C00
PTOP     EQU 0x00080000

HEAPBASE EQU 0x00100000
HEAPMAX  EQU 0x00400000

FLAG_NONE        EQU 0x00
FLAG_HIDDEN      EQU 0x01
FLAG_IMMEDIATE   EQU 0x02
FLAG_INLINE      EQU 0x03

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

    jmp (gdt.code - gdt):_kAbort

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

DEFENTRY "'kstate", _kstate, FLAG_INLINE
    PPUSH kstate
    ret
.End:

DEFENTRY "'kptop", _kptop, FLAG_INLINE
    PPUSH kptop
    ret
.End:

DEFENTRY "'krtop", _krtop, FLAG_INLINE
    PPUSH krtop
    ret
.End:

DEFENTRY "'kdict", _kdict, FLAG_INLINE
    PPUSH kdict
    ret
.End:

DEFENTRY "'kalloc", _kalloc, FLAG_INLINE
    PPUSH kalloc
    ret
.End:

DEFENTRY "'kin", _kin, FLAG_INLINE
    PPUSH kin
    ret
.End:

DEFENTRY "'ktmp", _ktmp, FLAG_INLINE
    PPUSH ktmp
    ret
.End:

DEFENTRY "'kbuf", _kbuf, FLAG_INLINE
    PPUSH kbuf
    ret
.End:

DEFENTRY "'kpanic", _kpanic, FLAG_INLINE
    PPUSH kpanic
    ret
.End:

DEFENTRY "@", _Load, FLAG_INLINE
    mov eax, [eax]
    ret
.End:

DEFENTRY "!", _Store, FLAG_INLINE
    mov edx, [ebp]
    mov [eax], edx
    add ebp, 8
    mov eax, [ebp-4]
    ret
.End:

DEFENTRY "b@", _bLoad, FLAG_INLINE
    movsx eax, BYTE [eax]
    ret
.End:

DEFENTRY "b!", _bStore, FLAG_INLINE
    mov edx, [ebp]
    mov BYTE [eax], dl
    add ebp, 8
    mov eax, [ebp-4]
    ret
.End:

DEFENTRY "ub@", _ubLoad, FLAG_INLINE
    movzx eax, BYTE [eax]
    ret
.End:

DEFENTRY "h@", _hLoad, FLAG_INLINE
    movsx eax, WORD [eax]
    ret
.End:

DEFENTRY "h!", _hStore, FLAG_INLINE
    mov edx, [ebp]
    mov WORD [eax], dx
    add ebp, 8
    mov eax, [ebp-4]
    ret
.End:

DEFENTRY "uh@", _uhLoad, FLAG_INLINE
    movzx eax, WORD [eax]
    ret
.End:

DEFENTRY ",", _Compile, FLAG_INLINE
    mov edx, [kalloc]
    mov edi, [edx]
    mov [edi], eax
    add edi, 4
    mov [edx], edi
    PDROP
    ret
.End:

DEFENTRY "b,", _bCompile, FLAG_INLINE
    mov edx, [kalloc]
    mov edi, [edx]
    mov BYTE [edi], al
    inc edi
    mov [edx], edi
    PDROP
    ret
.End:

DEFENTRY "h,", _hCompile, FLAG_INLINE
    mov edx, [kalloc]
    mov edi, [edx]
    mov WORD [edi], ax
    add edi, 2
    mov [edx], edi
    PDROP
    ret
.End:

DEFENTRY "Mov", _Mov, FLAG_INLINE
    mov ecx, eax
    mov edi, [ebp]
    mov esi, [ebp+4]
    rep movsd
    mov eax, [ebp+8]
    add ebp, 12
    ret
.End:

DEFENTRY "bMov", _bMov, FLAG_INLINE
    mov ecx, eax
    mov edi, [ebp]
    mov esi, [ebp+4]
    rep movsb
    mov eax, [ebp+8]
    add ebp, 12
    ret
.End:

DEFENTRY "hMov", _hMov, FLAG_INLINE
    mov ecx, eax
    mov edi, [ebp]
    mov esi, [ebp+4]
    rep movsw
    mov eax, [ebp+8]
    add ebp, 12
    ret
.End:

DEFENTRY "bMov,", _bMovCompile, FLAG_INLINE
    mov ecx, eax
    mov edx, [kalloc]
    mov edi, [edx]
    mov esi, [ebp]
    rep movsb
    mov [edx], edi
    mov eax, [ebp+4]
    add ebp, 8
    ret
.End:

DEFENTRY "Align", _Align, FLAG_INLINE
    PPOP ebx
    mov ecx, eax
    xor edx, edx
    div ebx
    test edx, edx
    je .Aligned
    mov eax, 8
    sub eax, edx
    add eax, ecx
    ret
.Aligned:
    mov eax, ecx
    ret
.End:

DEFENTRY "+", _Add, FLAG_INLINE
    add eax, [ebp]
    PNIP
    ret
.End:

DEFENTRY "-", _Sub, FLAG_INLINE
    sub [ebp], eax
    PDROP
    ret
.End:

DEFENTRY "*", _Mul, FLAG_INLINE
    imul eax, [ebp]
    PNIP
    ret
.End:

DEFENTRY "/", _Div, FLAG_INLINE
    xor edx, edx
    mov ecx, eax
    mov eax, [ebp]
    idiv ecx
    PNIP
    ret
.End:

DEFENTRY "%", _Mod, FLAG_INLINE
    xor edx, edx
    mov ecx, eax
    mov eax, [ebp]
    idiv ecx
    mov eax, edx
    PNIP
    ret
.End:

DEFENTRY "u*", _uMul, FLAG_INLINE
    xor edx, edx
    mul DWORD [ebp]
    PNIP
    ret
.End:

DEFENTRY "u/", _uDiv, FLAG_INLINE
    xor edx, edx
    mov ecx, eax
    mov eax, [ebp]
    div ecx
    PNIP
    ret
.End:

DEFENTRY "u%", _uMod, FLAG_INLINE
    xor edx, edx
    mov ecx, eax
    mov eax, [ebp]
    div ecx
    mov eax, edx
    PNIP
    ret
.End:

DEFENTRY "%And", _BitAnd, FLAG_INLINE
    and eax, [ebp]
    PNIP
    ret
.End:

DEFENTRY "%Or", _BitOr, FLAG_INLINE
    or eax, [ebp]
    PNIP
    ret
.End:

DEFENTRY "%Xor", _BitXor, FLAG_INLINE
    xor eax, [ebp]
    PNIP
    ret
.End:

DEFENTRY "%Not", _BitNot, FLAG_INLINE
    not eax
    ret
.End:

DEFENTRY "And", _And, FLAG_INLINE
    PPOP edx
    or edx, edx
    je .False
    or eax, eax
    je .False
    mov eax, 1
    ret
.False:
    xor eax, eax
    ret
.End:

DEFENTRY "Or", _Or, FLAG_INLINE
    PPOP edx
    or edx, edx
    jne .True
    or eax, eax
    jne .True
    xor eax, eax
    ret
.True:
    mov eax, 1
    ret
.End:

DEFENTRY "Not", _Not, FLAG_INLINE
    or eax, eax
    sete al
    movzx eax, al
    ret
.End:

DEFENTRY "=", _Equal, FLAG_INLINE
    cmp eax, [ebp]
    sete al
    movzx eax, al
    PNIP
    ret
.End:

DEFENTRY "<>", _Nequal, FLAG_INLINE
    cmp eax, [ebp]
    setne al
    movzx eax, al
    PNIP
    ret
.End:

DEFENTRY "<", _LessThan, FLAG_INLINE
    cmp eax, [ebp]
    setg al
    movzx eax, al
    PNIP
    ret
.End:

DEFENTRY ">", _GreaterThan, FLAG_INLINE
    cmp eax, [ebp]
    setl al
    movzx eax, al
    PNIP
    ret
.End:

DEFENTRY "<=", _LessEqual, FLAG_INLINE
    cmp eax, [ebp]
    setge al
    movzx eax, al
    PNIP
    ret
.End:

DEFENTRY ">=", _GreaterEqual, FLAG_INLINE
    cmp eax, [ebp]
    setle al
    movzx eax, al
    PNIP
    ret
.End:

DEFENTRY "u<", _uLessThan, FLAG_INLINE
    cmp eax, [ebp]
    seta al
    movzx eax, al
    PNIP
    ret
.End:

DEFENTRY "u>", _uGreaterThan, FLAG_INLINE
    cmp eax, [ebp]
    setb al
    movzx eax, al
    PNIP
    ret
.End:

DEFENTRY "u<=", _uLessEqual, FLAG_INLINE
    cmp eax, [ebp]
    setae al
    movzx eax, al
    PNIP
    ret
.End:

DEFENTRY ">=", _uGreaterEqual, FLAG_INLINE
    cmp eax, [ebp]
    setbe al
    movzx eax, al
    PNIP
    ret
.End:

DEFENTRY "Drop", _Drop, FLAG_INLINE
    PDROP
    ret
.End:

DEFENTRY "Nip", _Nip, FLAG_INLINE
    PNIP
    ret
.End:

DEFENTRY "Swap", _Swap, FLAG_INLINE
    xchg eax, [ebp]
    ret
.End:

DEFENTRY "Dup", _Dup, FLAG_INLINE
    sub ebp, 4
    mov [ebp], eax
    ret
.End:

DEFENTRY "Over", _Over, FLAG_INLINE
    sub ebp, 4
    mov [ebp], eax
    mov eax, [ebp+8]
    ret
.End:

DEFENTRY ">>R", _PushR, FLAG_INLINE
    pop edx
    push eax
    PDROP
    push edx
    ret
.End:

DEFENTRY "<<R", _PullR, FLAG_INLINE
    pop edx
    sub ebp, 4
    mov [ebp], eax
    pop eax
    push edx
    ret
.End:

DEFENTRY "Exit", _Exit, FLAG_IMMEDIATE
    mov edx, [kalloc]
    mov edi, [edx]
    mov BYTE [edi], 0xC3   ; retn
    inc edi
    mov [edx], edi
    ret
.End:

DEFENTRY "[", _StartCompile, FLAG_IMMEDIATE | FLAG_INLINE
    mov DWORD [kstate], STATE_COMPILE
    ret
.End:

DEFENTRY "]", _StopCompile, FLAG_IMMEDIATE | FLAG_INLINE
    mov DWORD [kstate], STATE_INTERPRET
    ret
.End:

DEFENTRY "(", _StartComment, FLAG_IMMEDIATE
.Loop:
    call _kNext
    PPOP edx
    cmp edx, 41
    jne .Loop
    ret
.End:

DEFENTRY "Lit,", _LitCompile, FLAG_IMMEDIATE
    mov edx, [kalloc]
    mov edi, [edx]
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
    mov [edi], eax         ;          LITERAL
    add edi, 4
    mov [edx], edi
    PDROP
    ret
.End:

DEFENTRY "Call,", _CallCompile, FLAG_IMMEDIATE
    call _kEat
    PPUSH [ktmp]
    call _kFind
    PPOP edx

    ; TODO: should check for :Inline and inline the call
    ; get POINTER
    add edx, 4

    mov ecx, [kalloc]
    mov edi, [ecx]
    mov BYTE [edi], 0xBA   ; mov edx,
    inc edi
    mov [edi], edx         ;          POINTER
    add edi, 4
    mov WORD [edi], 0xD2FF ; call edx
    add edi, 2
    mov [ecx], edi
    ret
.End:

DEFENTRY "Goto,", _GotoCompile, FLAG_IMMEDIATE
    mov edx, [kalloc]
    mov edi, [edx]
    mov BYTE [edi], 0xBA    ; mov edx,
    inc edi
    pop ebx
    push edi
    push ebx
    mov DWORD [edi], 0      ;          POINTER
    add edi, 4
    mov WORD [edi], 0xE2FF  ; jmp edx
    add edi, 2
    mov [edx], edi
    ret
.End:

DEFENTRY "Branch,", _BranchCompile, FLAG_IMMEDIATE
    mov edx, [kalloc]
    mov edi, [edx]
    mov WORD [edi], 0xD089 ; mov edx, eax
    add edi, 2
    mov WORD [edi], 0x458B ; mov eax, [ebp+ ]
    add edi, 2
    mov BYTE [edi], 0x00   ;               0
    inc edi
    mov WORD [edi], 0xC583 ; add ebp,
    add edi, 2
    mov BYTE [edi], 0x04   ;          4
    inc edi
    mov WORD [edi], 0xD285 ; test edx, edx
    add edi, 2
    mov WORD [edi], 0x850F ; jnz
    add edi, 2
    pop ebx
    push edi
    push ebx
    mov DWORD [edi], 0     ;    OFFSET
    add edi, 4
    mov [edx], edi
    ret
.End:

DEFENTRY "kNext", _kNext, FLAG_NONE
    PPUSH [kin]
    dec DWORD [eax+4]
    inc DWORD [eax]
    mov eax, [eax]
    movzx eax, BYTE [eax]
    ret
.End:

DEFENTRY "kEat", _kEat, FLAG_NONE
.NextSpace:
    call _kNext
    PPOP edx
    cmp edx, ' '
    jbe .NextSpace

    mov edi, [ktmp]
    inc edi
.NextChar:
    mov BYTE [edi], dl
    inc edi
    call _kNext
    PPOP edx
    ; This is a special case to make strings nicer.
    ; When we encounter a " (double quote, that always terminates a token)
    cmp edx, '"'
    je .StringTerminate
    cmp edx, ' '
    ja .NextChar
    jmp .Done

.StringTerminate:
    inc ecx ; Make sure we include the " in the token

.Done:
    mov ecx, edi
    mov edi, [ktmp]
    sub ecx, edi
    dec ecx
    mov BYTE [edi], cl
    ret
.End:

DEFENTRY "kTok", _kTok, FLAG_NONE
    call _kEat
    mov esi, [ktmp]
    movzx ecx, BYTE [esi]
    inc ecx
    mov edi, [kbuf]
    mov eax, edi
    rep movsb
    ret
.End:

DEFENTRY "kFind", _kFind, FLAG_NONE
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

.EntryInEdx:
    mov eax, edx
    ret
.End:

DEFENTRY "kPanic", _kPanic, FLAG_NONE
    ret
.End:

DEFENTRY "kRun", _kRun, FLAG_NONE
    call _kEat
    PPUSH [ktmp]
    call _kFind
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
    jmp _kRun

.Compile:
    mov ebx, [kalloc]
    mov edi, [ebx]
    mov BYTE [edi], 0xBA    ; mov edx,
    inc edi
    mov [edi], edx          ;          POINTER
    add edi, 4
    mov WORD [edi], 0xD2FF  ; call edx
    add edi, 2
    mov [ebx], edi
    jmp _kRun

.Execute:
    call edx
    jmp _kRun

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
    cmp esi, digits.end
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
    jmp _kRun

.TryLiteral:
    test DWORD [kstate], STATE_COMPILE
    jz _kRun
    call _LitCompile

    jmp _kRun
.End:

; Real kernel entry/reset point
DEFENTRY "kAbort", _kAbort, FLAG_NONE
    cli
    mov ax, (gdt.data - gdt)
    mov ds, ax
    mov es, ax
    mov ss, ax

    xor ax, ax
    mov fs, ax
    mov gs, ax

    mov esp, RTOP
    mov ebp, PTOP

    mov DWORD [kin_state+0], ksrc-1
    mov DWORD [kin_state+4], ksrc.size+1
    mov DWORD [kin_state+8], 0

    mov DWORD [kalloc_state+0], HEAPBASE
    mov DWORD [kalloc_state+4], HEAPMAX
    mov DWORD [kalloc_state+8], 0

    mov DWORD [kstate], STATE_INTERPRET
    mov DWORD [kdict], _LINK
    mov DWORD [kalloc], kalloc_state
    mov DWORD [kin], kin_state
    mov DWORD [ktmp], ktmp_state
    mov DWORD [kbuf], kbuf_state
    mov DWORD [kpanic], _kPanic

    jmp _kRun
.End:

; Place no more entries after this point
; _Kabort must always be the value of _LINK

SECTION .data

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
ktmp:       DD 0
kbuf:       DD 0
kpanic:     DD 0

SECTION .rodata

digits:
    DB '0123456789ABCDEF'
.end:

; This is an identity-mapped GDT
ALIGN 4
gdt:
    DW 0, 0
    DB 0, 0, 0, 0
.code:
    DW 0xFFFF     ; segment limit: bits 0-15
    DW 0x0000     ; segment base: bits 0-15
    DB 0x00       ; segment base: bits 16-23
    DB 0b10011010 ; access byte
    DB 0b11001111 ; segment length: bits 16-19, flags (4 bits)
    DB 0x00       ; segment base: bits 24-31
.data:
    DW 0xFFFF
    DW 0x0000
    DB 0x00
    DB 0b10010010
    DB 0b11001111
    DB 0x00
gdtr:
    DW (gdtr - gdt - 1)
    DD gdt

ALIGN 4
ksrc:
    incbin "kernel.nop"
.size: EQU $ - ksrc
