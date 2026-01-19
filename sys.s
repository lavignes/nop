; vim: ft=a65

#define FLAG_MASK      %00111111
#define FLAG_NONE      %00000000
#define FLAG_IMMEDIATE %10000000
#define FLAG_HIDDEN    %01000000

#define STATE_INTERPRET %0
#define STATE_COMPILE   %1

#define BUFCAP 64

#define ERR_TIMEOUT 1

; Increment parameter top indirect
#define INPS   \
    inc $01, x \
    bne :+     \
    inc $02, x \
:              \

; Instruction Pointer
IP  = $02
IPL = $02
IPH = $03

; Native Scratch Register
M = $04

; Indirect Address Pointer
ADRJ = $06
ADR  = $07
ADRL = $07
ADRH = $08

* = $0200

    jmp _Abort

CURRENT: .word _Cmd-3           ; Current dictionary pointer
HERE:    .word HERESTART        ; Heap pointer
STATE:   .byt STATE_INTERPRET   ; Interpreter state
ERR:     .byt 0                 ; Error/status register
DEV:     .byt 0                 ; 'dev current device
READ:    .word Read             ; 'rd syscall
WRITE:   .word Write            ; 'wr syscall
SEEK:    .word Seek             ; 'sk syscall

BUFOFF:  .byt 0
BUFEND:  .byt 0
BUF:     .dsb BUFCAP, 0         ; Scratch/text buffer area

/*
SysRefill:
    ldy INOFF
    beq @Return
    txa
    pha
    ldx #0
    stx INOFF
    stx INEND
:   cpy #INSZ      ; Move all bytes backwards
    beq :+
    lda INBUF, y
    sta INBUF, x
    inx
    iny
    bne :-
:   lda SYSTXT+0    ; Read X bytes from SYSTXT
    sta ADRL
    lda SYSTXT+1
    sta ADRH
    ldy #0
:   cpx #INSZ
    beq :+
    lda (ADR), y
    sta INBUF, x
    inx
    iny
    bne :-
:   clc             ; Add Y to SYSTXT
    tya
    adc ADRL
    sta SYSTXT+0
    bcc :+
    inc SYSTXT+1
:   pla
    tax
@Return:
    rts

; Tokenizes and updates INOFF, INEND
SysToken:
    jsr SysRefill
    ldy #0
:   lda INBUF, y    ; Skip leading non-printable
    iny
    cmp #'!'
    bcc :-
    dey
    sty INOFF
:   lda INBUF, y
    iny
    cmp #'"'
    beq :++
    cmp #'!'
    bcs :-
:   dey
:   sty INEND
    rts

; .byt  name, ...
; .word prev
; .byt  flaglen
; native code / JSR to DTC routine

; Assuming token with range [INOFF, INEND) in INBUF
; On exit:
; * ADR points to the found token, or NULL
SysFind:
    lda #0
    sta ADRL
    sta ADRH
    txa
    pha
    lda INEND
    sec
    sbc INOFF
    beq @Return     ; Zero length token
    sta M           ; Store length in M
    lda CURRENT+0   ; Load initial ADR
    sta ADRL
    lda CURRENT+1
    sta ADRH
@CheckLen:
    lda ADRH        ; Save ADR
    pha
    lda ADRL
    pha
    ldy #2
    lda (ADR), y    ; Get flaglen byte at offset +2
    tay
    and #FLAG_HIDDEN
    bne @NextLink
    tya
    and #FLAG_MASK
    cmp M
    bne @NextLink
    lda ADRL
    sec             ; ADR -= token len
    sbc M
    bcs :+
    dec ADRH
:   sta ADRL
    ldy #0
    ldx INOFF
:   lda (ADR), y    ; Compare bytes
    cmp INBUF, x
    bne @NextLink
    inx
    iny
    cpy M
    bne :-
    pla             ; Full match! Restore ADR before exit
    sta ADRL
    pla
    sta ADRH
    jmp @Return
@NextLink:
    pla             ; Restore ADR
    sta ADRL
    pla
    sta ADRH
    ldy #0
    lda (ADR), y
    tax
    iny
    lda (ADR), y
    stx ADRL
    sta ADRH
    ora ADRL
    bne @CheckLen   ; Continue if ADR not null
@Return:
    pla
    tax
    rts

; Multiply ADR by 2
MulTwo:
    asl ADRL
    rol ADRH
    rts

; Multiply ADR by 8
MulEight:
    jsr MulTwo
    jsr MulTwo
    jmp MulTwo

SysInterpret:
    jsr SysToken
    jsr SysFind
    txa
    pha
    lda ADRL
    ora ADRH
    beq @TryParse
    ldy #2
    lda (ADR), y
    and #FLAG_IMMEDIATE
    bne @Execute
    lda STATE
    cmp #STATE_COMPILE
    beq @Compile
@Execute:
    lda INEND
    sta INOFF
    pla
    tax
    lda ADRL
    clc
    adc #3
    bcc :+
    inc ADRH
:   sta ADRL
    jmp (ADR)
@Compile:
    ldy ADRH
    lda ADRL
    clc
    adc #3
    bcc :+
    iny
:   tax
@CompileYX:
    lda HERE+0
    sta ADRL
    lda HERE+1
    sta ADRH
    tya
    ldy #0
    sta (ADR), y
    iny
    txa
    sta (ADR), y
    lda HERE+0
    clc
    adc #2
    sta HERE+0
    bcc :+
    inc HERE+1
:   jmp @Return
@TryParse:
    ldy INOFF
    lda #0
    sta ADRL
    sta ADRH
    lda INBUF, y
    iny
    cmp #'$'
    beq @Hex
    cmp #'%'
    beq @Bin
    dey
@Dec:
    tya
    pha
    lda INBUF, y
    pha
    jsr MulTwo
    ldx ADRL
    ldy ADRH
    jsr MulTwo
    jsr MulTwo
    txa             ; ADR += YX
    clc
    adc ADRL
    sta ADRL
    tya
    adc ADRH
    sta ADRH
    pla
    sec             ; Get ASCII digit
    sbc #'0'
    clc
    adc ADRL        ; ADR += digit
    sta ADRL
    bcc :+
    inc ADRH
:   pla
    tay
    iny
    cpy INEND
    bne @Dec
    beq @Lit
@Hex:
    lda INBUF, y
    jsr MulEight
    jsr MulTwo
    sec             ; Get ASCII hex digit
    sbc #'0'
    cmp #10
    bcc :+
    sbc #7
:   clc
    adc ADRL        ; ADR += digit
    sta ADRL
    bcc :+
    inc ADRH
:   iny
    cpy INEND
    bne @Hex
    beq @Lit
@Bin:
    lda INBUF, y
    jsr MulTwo
    sec             ; Get ASCII binary digit
    sbc #'0'
    clc
    adc ADRL        ; ADR += digit
    sta ADRL
    bcc :+
    inc ADRH
:   iny
    cpy INEND
    bne @Bin
@Lit:
    lda STATE
    cmp #STATE_COMPILE
    beq @CompileLit
    pla
    tax
    dex
    dex
    lda ADRL
    sta $01, x
    lda ADRH
    sta $02, x
    txa
    pha
    jmp @Return
@CompileLit:
    ldx ADRL
    ldy ADRH
    jmp @CompileYX
@Return:
    lda INEND
    sta INOFF
    pla
    tax
    rts

*/

; ----- Inner Interpreter -----

DoCol:
    pla
    sta ADRL
    pla
    sta ADRH
    lda IPH
    pha
    lda IPL
    pha
    inc ADRL
    bne :+
    inc ADRH
:   lda ADRL
    ldy ADRH
    jmp :+

DoCell:
    dex
    dex
    pla
    sta $01, x
    pla
    sta $02, x
    INPS

Next:
    lda IPL
    sta ADRL
    ldy IPH
    sty ADRH
:   clc
    adc #2
    bcc :+
    iny
:   sta IPL
    sty IPH
    jmp ADRJ

; ----- Drivers -----

; ( buf len -- n )
Read:
    lda $00, x      ; M = len
    sta M+0
    lda $01, x
    sta M+1
    inx
    inx
    lda $00, x      ; ADR = buf
    sta ADRL
    lda $01, x
    sta ADRH
    txa
    pha
    ldy #0
    sty ERR
@ChkLen:
    lda M+0
    eor M+1
    beq @Return
    lda M+0
    bne :+
    dec M+1
:   dec M+0
    lda #0
    tax             ; Timeout counter
    sta $00         ; Select status byte
@Poll:
    dex
    beq @Timeout
    lda $01
    beq @Poll
:   lda #1          ; Select data byte
    sta $00
    lda $01
    sta (ADR), y
    inc ADRL
    bne :+
    inc ADRH
:   jmp @ChkLen
@Timeout:
    lda #ERR_TIMEOUT
    sta ERR
@Return:
    pla
    tax
    lda ADRL        ; Compute bytes read
    sec
    sbc $00, x
    sta $00, x
    lda ADRH
    sbc $01, x
    sta $01, x
    jmp Next

; ( buf len -- n )
Write:
    lda $00, x      ; M = len
    sta M+0
    lda $01, x
    sta M+1
    inx
    inx
    lda $00, x      ; ADR = buf
    sta ADRL
    lda $01, x
    sta ADRH
    ldy #0
    sty ERR
    iny             ; Select data byte
    sty $00
@ChkLen:
    lda M+0
    eor M+1
    beq @Return
    lda M+0
    bne :+
    dec M+1
:   dec M+0
    lda (ADR), y
    sta $01
    inc ADRL
    bne :+
    inc ADRH
    jmp @ChkLen
@Return:
    lda ADRL        ; Compute bytes written
    sec
    sbc $00, x
    sta $00, x
    lda ADRH
    sbc $01, x
    sta $01, x
    jmp Next

; ( pos -- pos )
Seek:
    jmp Next

; ----- Primitive Words -----

; ( addr -- n )
.byt "@"
.word 0
.byt 1 | FLAG_NONE
_Load:
    lda ($01, x)
    tay
    INPS
    lda ($02, x)
    sty $01, x
    sta $02, x
    jmp Next

; ( addr -- b )
.byt "@b"
.word _Load-3
.byt 2 | FLAG_NONE
_LoadByte:
    ldy #0
    lda ($01, x)
    sta $01, x
    bpl :+
    dey
:   sty $02, x
    jmp Next

; ( addr -- bu )
.byt "@bu"
.word _LoadByte-3
.byt 3 | FLAG_NONE
_LoadByteUnsigned:
    lda ($01, x)
    sta $01, x
    lda #0
    sta $02, x
    jmp Next

; ( n addr -- )
.byt "!"
.word _LoadByteUnsigned-3
.byt 1 | FLAG_NONE
_Store:
    lda $03, x
    sta ($01, x)
    INPS
    lda $04, x
    sta ($01, x)
    inx
    inx
    inx
    inx
    jmp Next

; ( b addr -- )
.byt "!b"
.word _Store-3
.byt 2 | FLAG_NONE
_StoreByte:
    lda $03, x
    sta ($01, x)
    inx
    inx
    inx
    inx
    jmp Next

; P: ( n -- ) R: ( -- n )
.byt ">R"
.word _StoreByte-3
.byt 2 | FLAG_NONE
_PushR:
    lda $02, x
    pha
    lda $01, x
    pha
    inx
    inx
    jmp Next

; R: ( n -- ) P: ( -- n )
.byt "R>"
.word _PushR-3
.byt 2 | FLAG_NONE
_PullR:
    dex
    dex
    pla
    sta $01, x
    pla
    sta $02, x
    jmp Next

; ( n -- )
.byt "drop"
.word _PullR-3
.byt 4 | FLAG_NONE
_Drop:
    inx
    inx
    jmp Next

; ( n -- n n )
.byt "dup"
.word _Drop-3
.byt 3 | FLAG_NONE
_Dup:
    lda $01, x
    ldy $02, x
    dex
    dex
    sta $01, x
    sty $02, x
    jmp Next

; ( n1 n2 -- n2 n1 )
.byt "swap"
.word _Dup-3
.byt 4 | FLAG_NONE
_Swap:
    lda $01, x
    tay
    lda $03, x
    sta $01, x
    sty $03, x
    lda $02, x
    tay
    lda $04, x
    sta $02, x
    sty $04, x
    jmp Next

; ( n1 n2 -- n1 n2 n1 )
; TODO: >R dup R> swap
.byt "over"
.word _Swap-3
.byt 4 | FLAG_NONE
_Over:
    lda $03, x
    ldy $04, x
    dex
    dex
    sta $01, x
    sty $02, x
    jmp Next

; ( n1 n2 -- n1 & n2 )
.byt "and"
.word _Over-3
.byt 3 | FLAG_NONE
_And:
    lda $03, x
    and $01, x
    sta $03, x
    lda $04, x
    and $02, x
    sta $04, x
    inx
    inx
    jmp Next

; ( n1 n2 -- n1 | n2 )
.byt "or"
.word _And-3
.byt 2 | FLAG_NONE
_Or:
    lda $03, x
    ora $01, x
    sta $03, x
    lda $04, x
    ora $02, x
    sta $04, x
    inx
    inx
    jmp Next

; ( n1 n2 -- n1 ^ n2 )
.byt "xor"
.word _Or-3
.byt 3 | FLAG_NONE
_Xor:
    lda $03, x
    eor $01, x
    sta $03, x
    lda $04, x
    eor $02, x
    sta $04, x
    inx
    inx
    jmp Next

; ( n1 n2 -- n1 + n2 )
.byt "+"
.word _Xor-3
.byt 1 | FLAG_NONE
_Add:
    clc
    lda $03, x
    adc $01, x
    sta $03, x
    lda $04, x
    adc $02, x
    sta $04, x
    inx
    inx
    jmp Next

; ( n1 n2 -- n1 - n2 )
.byt "-"
.word _Add-3
.byt 1 | FLAG_NONE
_Sub:
    sec
    lda $03, x
    sbc $01, x
    sta $03, x
    lda $04, x
    sbc $02, x
    sta $04, x
    inx
    inx
    jmp Next

; ( n1 n2 -- n1 < n2 )
.byt "<"
.word _Sub-3
.byt 1 | FLAG_NONE
_LessThan:
    ldy #0
    sec
    lda $03, x
    sbc $01, x
    lda $04, x
    sbc $02, x
    sty $04, x
    bcs :+
    iny
:   sty $03, x
    inx
    inx
    jmp Next

; ( n1 n2 -- n1 = n2 )
.byt "="
.word _LessThan-3
.byt 1 | FLAG_NONE
_Equal:
    ldy #0
    sec
    lda $03, x
    sbc $01, x
    lda $04, x
    sbc $02, x
    sty $04, x
    beq :+
    iny
:   sty $03, x
    inx
    inx
    jmp Next

; ( addr flag - )
.byt "0br?"
.word _Equal-3
.byt 4 | FLAG_NONE
_0Branch:
    lda $01, x
    ora $02, x
    bne :+
    lda $03, x
    sta IPL
    lda $04, x
    sta IPH
:   inx
    inx
    inx
    inx
    jmp Next

; ( -- n )
.byt "lit"
.word _0Branch-3
.byt 3 | FLAG_NONE
_Lit:
    dex
    dex
    lda IPL
    sta $01, x
    lda IPH
    sta $02, x
    clc
    lda #2
    adc IPL
    bcc :+
    inc IPH
:   sta IPL
    jmp Next

.byt "exit"
.word _Lit-3
.byt 4 | FLAG_NONE
_Exit:
    pla
    sta IPL
    pla
    sta IPH
    jmp Next

; ( addr -- )
.byt "exec"
.word _Exit-3
.byt 4 | FLAG_NONE
_Exec:
    lda $01, x
    sta ADRL
    lda $02, x
    sta ADRH
    inx
    inx
    jmp ADR

.byt "abort"
.word _Exec-3
.byt 5 | FLAG_NONE
_Abort:
    sei
    cld
    ; TODO: reset SYSVARS
    lda #$6C
    sta ADRJ
    ldx #$FF
    jmp _Shell

.byt "shell"
.word _Abort-3
.byt 5 | FLAG_NONE
_Shell:
    txa
    ldx #$FF
    txs
    tax
    lda #STATE_INTERPRET
    sta STATE
    lda #0
    sta DEV
    sta ERR
    sta BUFOFF
    sta BUFEND
:   jsr DoCol
    .word _Cmd
    .word _PullR
    .word _Drop
    .word :-

; ----- Outer Interpreter -----

; ( buf len base -- n )
.byt "parse"
.word _Shell-3
.byt 5 | FLAG_NONE
_Parse:
    jmp Next

.byt "rdln"
.word _Parse-3
.byt 4 | FLAG_NONE
_RdLn:
    jmp Next

.byt "cmd"
.word _RdLn-3
.byt 4 | FLAG_NONE
_Cmd:
    jsr DoCol
    .word _Exit

HERESTART = *
