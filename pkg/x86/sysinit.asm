; vim: ft=nasm
CPU 386

SECTION .rodata

; Identity-mapped GDT
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

digits:
    DB '0123456789ABCDEF'
.end:

SECTION .text

BITS 16

GLOBAL Go32
; Setup GDT and jump to 32-bit mode
Go32:
    cli
    lgdt [gdtr]

    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp (gdt.code - gdt):SysInit

SECTION .data

ALIGN 4
syspeek_state:
    DB 0           ; Length
    TIMES 255 DB 0 ; Buffer

ALIGN 4
systok_state:
    DB 0           ; Length
    TIMES 255 DB 0 ; Buffer

ALIGN 4
syspanic:     DD 0 ; Pointer to panic handler
sysstate:     DD 0 ; Current interpreter state
sysptop:      DD 0 ; Top of parameter stack
sysrtop:      DD 0 ; Top of return stack
sysdict:      DD 0 ; Pointer to last dictionary entry
syshere:      DD 0 ; Pointer to free heap
sysin:        DD 0 ; Pointer to input
syspeek:      DD 0 ; Pointer to token being read (could be incomplete)
systok:       DD 0 ; Pointer to latest token (usually copied from syspeek)

SECTION .text

RTOP     EQU 0x00080000
PTOP     EQU 0x00007E00

MEM_BASE EQU 0x00100000

BITS 32

%define _DEFLINK 0x00000000

%macro DEF 3
%strlen _DEFNAMELEN %1
ALIGN 4
    DB %1          ; Name
ALIGN 4
    DW %2.End - %2 ; Code length
    DB %3          ; Flags
    DB _DEFNAMELEN ; Name length
    DD _DEFLINK    ; Link to next definition
%define _DEFLINK (%2 - 4)
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

FLAG_NONE         EQU 0x00000000
FLAG_HIDDEN       EQU 0x00000001
FLAG_IMMEDIATE    EQU 0x00000002
FLAG_INLINE       EQU 0x00000004

STATE_INTERPRET   EQU 0x00000000
STATE_COMPILE     EQU 0x00000001

PANIC_NOT_FOUND   EQU 0x00000001

DEF "'syspanic", _syspanic, FLAG_INLINE
    PPUSH syspanic
    ret
.End:

DEF "'sysstate", _sysstate, FLAG_INLINE
    PPUSH sysstate
    ret
.End:

DEF "'sysptop", _sysptop, FLAG_INLINE
    PPUSH sysptop
    ret
.End:

DEF "'sysrtop", _sysrtop, FLAG_INLINE
    PPUSH sysrtop
    ret
.End:

DEF "'sysdict", _sysdict, FLAG_INLINE
    PPUSH sysdict
    ret
.End:

DEF "'syshere", _syshere, FLAG_INLINE
    PPUSH syshere
    ret
.End:

DEF "'sysin", _sysin, FLAG_INLINE
    PPUSH sysin
    ret
.End:

DEF "'syspeek", _syspeek, FLAG_INLINE
    PPUSH syspeek
    ret
.End:

DEF "'systok", _systok, FLAG_INLINE
    PPUSH systok
    ret
.End:

; ( addr -- val )
DEF "@", _Load, FLAG_INLINE
    mov eax, [eax]
    ret
.End:

; ( val addr -- )
DEF "!", _Store, FLAG_INLINE
    mov edx, [ebp]
    mov [eax], edx
    add ebp, 8
    mov eax, [ebp - 4]
    ret
.End:

; ( addr -- val )
DEF "@b", _LoadByte, FLAG_INLINE
    movsx eax, BYTE [eax]
    ret
.End:

; ( val addr -- )
DEF "!b", _StoreByte, FLAG_INLINE
    mov edx, [ebp]
    mov BYTE [eax], dl
    add ebp, 8
    mov eax, [ebp - 4]
    ret
.End:

; ( addr -- val )
DEF "@bu", _LoadByteUnsigned, FLAG_INLINE
    movzx eax, BYTE [eax]
    ret
.End:

; ( addr -- val )
DEF "@h", _LoadHalf, FLAG_INLINE
    movsx eax, WORD [eax]
    ret
.End:

; ( val addr -- )
DEF "!h", _StoreHalf, FLAG_INLINE
    mov edx, [ebp]
    mov WORD [eax], dx
    add ebp, 8
    mov eax, [ebp - 4]
    ret
.End:

; ( addr -- val )
DEF "@hu", _LoadHalfUnsigned, FLAG_INLINE
    movzx eax, WORD [eax]
    ret
.End:

; ( src dest n -- )
DEF "Mov", _Mov, FLAG_INLINE
    mov ecx, eax
    mov edi, [ebp]
    mov esi, [ebp + 4]
    rep movsd
    mov eax, [ebp + 8]
    add ebp, 12
    ret
.End:

; ( src dest n -- )
DEF "Movb", _MovByte, FLAG_INLINE
    mov ecx, eax
    mov edi, [ebp]
    mov esi, [ebp + 4]
    rep movsb
    mov eax, [ebp + 8]
    add ebp, 12
    ret
.End:

; ( src dest n -- )
DEF "Movh", _MovHalf, FLAG_INLINE
    mov ecx, eax
    mov edi, [ebp]
    mov esi, [ebp + 4]
    rep movsw
    mov eax, [ebp + 8]
    add ebp, 12
    ret
.End:

; ( addr n -- addr )
DEF "AlignTo", _AlignTo, FLAG_INLINE
    PPOP ecx
    test ecx, ecx
    jz .Done
    mov ebx, eax
    xor edx, edx
    div ecx
    test edx, edx
    jz .Aligned
    sub ecx, edx
    add eax, ecx
    jmp .Done
.Aligned:
    mov eax, ebx
.Done:
    ret
.End:

DEF "+", _Add, FLAG_INLINE
    add eax, [ebp]
    PNIP
    ret
.End:

DEF "-", _Sub, FLAG_INLINE
    sub [ebp], eax
    PDROP
    ret
.End:

DEF "*", _Mul, FLAG_INLINE
    imul eax, [ebp]
    PNIP
    ret
.End:

DEF "/", _Div, FLAG_INLINE
    xor edx, edx
    mov ecx, eax
    mov eax, [ebp]
    idiv ecx
    PNIP
    ret
.End:

DEF "%", _Mod, FLAG_INLINE
    xor edx, edx
    mov ecx, eax
    mov eax, [ebp]
    idiv ecx
    mov eax, edx
    PNIP
    ret
.End:

DEF "*u", _MulUnsigned, FLAG_INLINE
    xor edx, edx
    mul DWORD [ebp]
    PNIP
    ret
.End:

DEF "/u", _DivUnsigned, FLAG_INLINE
    xor edx, edx
    mov ecx, eax
    mov eax, [ebp]
    div ecx
    PNIP
    ret
.End:

DEF "%u", _ModUnsigned, FLAG_INLINE
    xor edx, edx
    mov ecx, eax
    mov eax, [ebp]
    div ecx
    mov eax, edx
    PNIP
    ret
.End:

DEF "BitAnd", _BitAnd, FLAG_INLINE
    and eax, [ebp]
    PNIP
    ret
.End:

DEF "BitOr", _BitOr, FLAG_INLINE
    or eax, [ebp]
    PNIP
    ret
.End:

DEF "BitXor", _BitXor, FLAG_INLINE
    xor eax, [ebp]
    PNIP
    ret
.End:

DEF "BitNot", _BitNot, FLAG_INLINE
    not eax
    ret
.End:

DEF "And", _And, FLAG_INLINE
    PPOP edx
    or edx, edx
    je .False
    or eax, eax
    je .False
    mov eax, 1
    jmp .Done
.False:
    xor eax, eax
.Done:
    ret
.End:

DEF "Or", _Or, FLAG_INLINE
    PPOP edx
    or edx, edx
    jne .True
    or eax, eax
    jne .True
    xor eax, eax
    jmp .Done
.True:
    mov eax, 1
.Done:
    ret
.End:

DEF "Not", _Not, FLAG_INLINE
    or eax, eax
    sete al
    movzx eax, al
    ret
.End:

DEF "=", _Equal, FLAG_INLINE
    cmp eax, [ebp]
    sete al
    movzx eax, al
    PNIP
    ret
.End:

DEF "<>", _Nequal, FLAG_INLINE
    cmp eax, [ebp]
    setne al
    movzx eax, al
    PNIP
    ret
.End:

DEF "<", _LessThan, FLAG_INLINE
    cmp eax, [ebp]
    setg al
    movzx eax, al
    PNIP
    ret
.End:

DEF ">", _GreaterThan, FLAG_INLINE
    cmp eax, [ebp]
    setl al
    movzx eax, al
    PNIP
    ret
.End:

DEF "<=", _LessEqual, FLAG_INLINE
    cmp eax, [ebp]
    setge al
    movzx eax, al
    PNIP
    ret
.End:

DEF ">=", _GreaterEqual, FLAG_INLINE
    cmp eax, [ebp]
    setle al
    movzx eax, al
    PNIP
    ret
.End:

DEF "<u", _LessThanUnsigned, FLAG_INLINE
    cmp eax, [ebp]
    seta al
    movzx eax, al
    PNIP
    ret
.End:

DEF ">u", _GreaterThanUnsigned, FLAG_INLINE
    cmp eax, [ebp]
    setb al
    movzx eax, al
    PNIP
    ret
.End:

DEF "<=u", _LessEqualUnsigned, FLAG_INLINE
    cmp eax, [ebp]
    setae al
    movzx eax, al
    PNIP
    ret
.End:

DEF ">=u", _GreaterEqualUnsigned, FLAG_INLINE
    cmp eax, [ebp]
    setbe al
    movzx eax, al
    PNIP
    ret
.End:

; ( x -- )
DEF "Drop", _Drop, FLAG_INLINE
    PDROP
    ret
.End:

; ( x y -- y )
DEF "Nip", _Nip, FLAG_INLINE
    PNIP
    ret
.End:

; ( x y -- y x )
DEF "Swap", _Swap, FLAG_INLINE
    xchg eax, [ebp]
    ret
.End:

; ( x y -- x y y )
DEF "Dup", _Dup, FLAG_INLINE
    sub ebp, 4
    mov [ebp], eax
    ret
.End:

; ( x y -- x y x )
DEF "Over", _Over, FLAG_INLINE
    sub ebp, 4
    mov [ebp], eax
    mov eax, [ebp + 8]
    ret
.End:

DEF ">>r", _PushR, FLAG_IMMEDIATE | FLAG_INLINE
    pop edx
    push eax
    PDROP
    push edx
    ret
.End:

DEF "<<r", _PullR, FLAG_IMMEDIATE | FLAG_INLINE
    pop edx
    sub ebp, 4
    mov [ebp], eax
    pop eax
    push edx
    ret
.End:

DEF "[", _StartInterpret, FLAG_IMMEDIATE | FLAG_INLINE
    mov DWORD [sysstate], STATE_INTERPRET
    ret
.End:

DEF "]", _StopInterpret, FLAG_IMMEDIATE | FLAG_INLINE
    mov DWORD [sysstate], STATE_COMPILE
    ret
.End:

DEF "\", _StartComment, FLAG_IMMEDIATE
    call _PullSysin
    PPOP edx
    cmp edx, 10
    jne _StartComment
    ret
.End:

DEF ",", _Compile, FLAG_IMMEDIATE
    mov edx, syshere
    mov edi, [edx]
    mov [edi], eax
    add edi, 4
    mov [edx], edi
    PDROP
    ret
.End:

DEF "b,", _CompileByte, FLAG_IMMEDIATE
    mov edx, syshere
    mov edi, [edx]
    mov BYTE [edi], al
    inc edi
    mov [edx], edi
    PDROP
    ret
.End:

DEF "h,", _CompileHalf, FLAG_IMMEDIATE
    mov edx, syshere
    mov edi, [edx]
    mov WORD [edi], ax
    add edi, 2
    mov [edx], edi
    PDROP
    ret
.End:

; ( src n -- )
DEF "#b,", _CompileBytes, FLAG_IMMEDIATE
    mov ecx, eax
    mov edx, syshere
    mov edi, [edx]
    mov esi, [ebp]
    rep movsb
    mov [edx], edi
    mov eax, [ebp + 4]
    add ebp, 8
    ret
.End:

DEF "Exit,", _CompileExit, FLAG_IMMEDIATE
    mov edx, syshere
    mov edi, [edx]
    mov BYTE [edi], 0xC3   ; retn
    inc edi
    mov [edx], edi
    ret
.End:

; ( val -- )
DEF "Lit,", _CompileLit, FLAG_IMMEDIATE
    mov edx, syshere
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

; ( addr -- )
DEF "Call,", _CompileCall, FLAG_IMMEDIATE
    mov edx, syshere
    mov edi, [edx]
    mov BYTE [edi], 0xBA   ; mov edx,
    inc edi
    mov [edi], eax         ;          POINTER
    add edi, 4
    mov WORD [edi], 0xD2FF ; call edx
    add edi, 2
    mov [edx], edi
    PDROP
    ret
.End:

; ( addr -- )
DEF "Goto,", _CompileGoto, FLAG_IMMEDIATE
    mov edx, syshere
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

; ( addr -- )
DEF "Branch,", _CompileBranch, FLAG_IMMEDIATE
    mov edx, syshere
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
    mov WORD [edi], 0x840F ; jz
    add edi, 2
    pop ebx
    push edi
    push ebx
    mov DWORD [edi], 0     ;    OFFSET
    add edi, 4
    mov [edx], edi
    ret
.End:

DEF "Defer,", _CompileDefer, FLAG_IMMEDIATE
    call _Peek
    PPUSH [syspeek]
    call _Find
    test eax, eax
    jz .Panic
    add eax, 4 ; get POINTER
    call _CompileCall
    jmp .Done
.Panic:
    mov eax, PANIC_NOT_FOUND
    mov edx, [syspanic]
    call edx
.Done:
    ret
.End:

DEF "<<sysin", _PullSysin, FLAG_NONE
    PPUSH sysin
    inc DWORD [eax]
    mov eax, [eax]
    movzx eax, BYTE [eax]
    ret
.End:

DEF "Peek", _Peek, FLAG_NONE
.NextSpace:
    call _PullSysin
    PPOP edx
    cmp edx, ' '
    jbe .NextSpace

    mov edi, [syspeek]
    inc edi
.NextChar:
    mov BYTE [edi], dl
    inc edi
    call _PullSysin
    PPOP edx
    ; We treat a couple characters as special cases that always terminate:
    ; - " to handle string literals
    ; - { to handle braced things
    cmp edx, '"'
    je .EarlyTerminate
    cmp edx, '{'
    je .EarlyTerminate
    cmp edx, ' '
    ja .NextChar
    jmp .Done

.EarlyTerminate:
    inc ecx ; Make sure we include the special char in the token

.Done:
    mov ecx, edi
    mov edi, [syspeek]
    sub ecx, edi
    dec ecx
    mov BYTE [edi], cl
    ret
.End:

; ( -- pstr-addr )
DEF "Tok", _Tok, FLAG_NONE
    call _Peek
    mov esi, [syspeek]
    movzx ecx, BYTE [esi]
    inc ecx
    mov edi, [systok]
    mov eax, edi
    rep movsb
    ret
.End:

; ( pstr-addr -- entry-addr )
DEF "Find", _Find, FLAG_NONE
    mov edi, eax
    movzx ecx, BYTE [edi]
    inc edi
    mov edx, sysdict

.NextLink:
    mov edx, [edx]
    test edx, edx
    jz .EntryInEdx

    ; Entry name length compare?
    cmp cl, BYTE [edx - 1]
    jne .NextLink

    ; Load entry name ptr into ESI: (EDX - 4 - ECX) & 0xFFFFFFFC
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

DEF "Panic", _Panic, FLAG_NONE
    PDROP
    ret
.End:

DEF "RunLoop", _RunLoop, FLAG_NONE
    call _Peek
    PPUSH [syspeek]
    call _Find
    test eax, eax
    jz .NotFound

    add eax, 4
    test DWORD [sysstate], STATE_COMPILE
    jz .Execute

    movzx ecx, BYTE [eax - 6]
    test ecx, FLAG_IMMEDIATE
    jnz .Execute
    test ecx, FLAG_INLINE
    jz .Compile

    ; Inline compile
    sub ebp, 4
    mov [ebp], eax
    movzx eax, WORD [eax - 8]
    dec eax ; Ignore retn byte at end
    call _CompileBytes
    jmp _RunLoop

.Compile:
    call _CompileCall
    jmp _RunLoop

.Execute:
    call eax
    jmp _RunLoop

.NotFound:
    mov edi, [syspeek]
    movzx ecx, BYTE [edi]
    inc edi
    xor eax, eax

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
    je .PanicNotDigit
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

.TryLiteral:
    test DWORD [sysstate], STATE_COMPILE
    jz _RunLoop
    call _CompileLit
    jmp _RunLoop

.PanicNotDigit:
    mov eax, PANIC_NOT_FOUND

    mov edx, [syspanic]
    call edx
    jmp _RunLoop
    ret
.End:

SysInit:
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

    mov DWORD [syspanic], _Panic
    mov DWORD [sysstate], STATE_INTERPRET
    mov DWORD [sysptop], PTOP
    mov DWORD [sysrtop], RTOP
    mov DWORD [sysdict], _DEFLINK
    mov DWORD [syshere], MEM_BASE
    mov DWORD [sysin], (syssrc - 1)
    mov DWORD [syspeek], syspeek_state
    mov DWORD [systok], systok_state

    jmp _RunLoop

SECTION .rodata

syssrc:
    incbin "kern/start.nop"
.end:

