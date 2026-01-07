; vim: ft=a65

#define FLAG_MASK      %00111111
#define FLAG_NONE      %00000000
#define FLAG_IMMEDIATE %10000000

#define STATE_COMPILE   %0
#define STATE_INTERPRET %1

#define INLEN 40

; Increment parameter top indirect
#define INPS   \
    inc $00, x \
    bne :+     \
    inc $01, x \
:              \

; Increment instruction pointer
#define INIP   \
    inc IPL    \
    bne :+     \
    inc IPH    \
:              \

; Instruction Pointer
IP  = $02
IPL = $02
IPH = $03

; Indirect Address Pointer
ADRJ = $04
ADR  = $05
ADRL = $05
ADRH = $06

* = $0200

    JMP _Abort

SYSVARS = *
CURRENT: .word _Abort-3  ; Current dictionary pointer
HERE:    .word HERESTART ; Heap pointer
STATE:   .byt 0          ; Interpreter state
ERRNO:   .byt 0          ; General-purpose error register
M:       .word 0         ; Native scratch register
Q:       .word 0         ; Language scratch register
EMIT:    .word 0         ; 'emit' routine
KEY:     .word 0         ; 'key?' routine
RDIN:    .word SysRefill ; 'refill' routine
INOFF:   .byt  INLEN     ; Offset into INBUF
INBUF:   .dsb  INLEN,0   ; Input buffer

SYSTXT:  .word SYSTXTSTART

DoCell:
    dex
    dex
    pla
    sta $00, x
    pla
    sta $01, x
    INPS
    ; Fall through to DoNext

; Jump to (IP) and increment IP by 2
DoNext:
    lda IPH
    sta ADRH
    lda IPL
    sta ADRL
    clc
    adc #2
    bcc :+
    inc IPH
:   sta IPL
    jmp ADRJ

SysRefill:
    ldy INOFF
    beq @Return
    txa
    pha
    ldx #0
    stx INOFF
:   cpy #INLEN      ; Move all bytes backwards
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
:   cpx #INLEN
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

; Returns token with range [INOFF,Y) in INBUF
SysToken:
    jsr SysRefill
    ldy #0
:   lda INBUF, y    ; Skip leading non-printable
    iny
    cmp #'!'
    bcc :-
    dey
    sty INOFF       ; INOFF is token start
:   lda INBUF, y
    iny
    cmp #'"'
    beq :++
    cmp #' '
    bcs :-
:   dey
:   rts             ; Y is token end

; .byt  name, ...
; .word prev
; .byt  flaglen
; native code / JSR to DTC routine

; Assuming token with range [INOFF,Y) in INBUF
; On exit:
; * ADR points to the found token, or NULL
; * INOFF and Y are unchanged
SysFind:
    lda #0
    sta ADRL
    sta ADRH
    txa
    pha
    tya
    pha
    ; Calculate token length: Y - INOFF
    ; example: hello
    ;          ^    ^ :: Y=5, INOFF=0 -> length=5
    sec
    sbc INOFF
    beq @Return     ; Zero length token
    sta M           ; Store length in M
    lda CURRENT+0   ; Load initial ADR
    sta ADRL
    lda CURRENT+1
    sta ADRH
@CheckLen:
    lda ADRL        ; Save ADR
    pha
    lda ADRH
    pha
    ldy #2
    lda (ADR), y    ; Get flaglen byte at offset +2
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
    sta ADRH
    pla
    sta ADRL
    bne @Return
@NextLink:
    pla             ; Restore ADR
    sta ADRH
    pla
    sta ADRL
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
    tay
    pla
    tax
    rts

SysInterpret:
    jsr SysToken
    jsr SysFind
    rts

; ( addr -- n )
.byt "@"
.word 0
.byt 1 | FLAG_NONE
_Load:
    lda ($00, x)
    tay
    INPS
    lda ($00, x)
    sty $00, x
    sta $01, x
    jmp DoNext

; ( addr -- b )
.byt "@b"
.word _Load-3
.byt 2 | FLAG_NONE
_LoadByte:
    ldy #0
    lda ($00, x)
    sta $00, x
    bpl :+
    dey
:   sta $01, x
    jmp DoNext

; ( addr -- bu )
.byt "@bu"
.word _LoadByte-3
.byt 3 | FLAG_NONE
_LoadByteUnsigned:
    lda ($00, x)
    sta $00, x
    lda #0
    sta $01, x
    jmp DoNext

; ( n addr -- )
.byt "!"
.word _LoadByteUnsigned-3
.byt 1 | FLAG_NONE
_Store:
    lda $02, x
    sta ($00, x)
    INPS
    lda $03, x
    sta ($00, x)
    inx
    inx
    inx
    inx
    jmp DoNext

; ( b addr -- )
.byt "!b"
.word _Store-3
.byt 2 | FLAG_NONE
_StoreByte:
    lda $02, x
    sta ($00, x)
    inx
    inx
    inx
    inx
    jmp DoNext

; P: ( n -- ) R: ( -- n )
.byt ">R"
.word _StoreByte-3
.byt 2 | FLAG_NONE
_PushR:
    lda $01, x
    pha
    lda $00, x
    pha
    inx
    inx
    jmp DoNext

; R: ( n -- ) P: ( -- n )
.byt "R>"
.word _PushR-3
.byt 2 | FLAG_NONE
_PullR:
    dex
    dex
    pla
    sta $00, x
    pla
    sta $01, x
    jmp DoNext

; ( n -- )
.byt "drop"
.word _PullR-3
.byt 4 | FLAG_NONE
_Drop:
    inx
    inx
    jmp DoNext

; ( n -- n n )
.byt "dup"
.word _Drop-3
.byt 3 | FLAG_NONE
_Dup:
    lda $00, x
    ldy $01, x
    dex
    dex
    sta $00, x
    sty $01, x
    jmp DoNext

; ( n1 n2 -- n2 n1 )
.byt "swap"
.word _Dup-3
.byt 4 | FLAG_NONE
_Swap:
    lda $00, x
    tay
    lda $02, x
    sta $00, x
    sty $02, x
    lda $01, x
    tay
    lda $03, x
    sta $01, x
    sty $03, x
    jmp DoNext

; ( n1 n2 -- n1 n2 n1 )
; TODO: >R dup R> swap
.byt "over"
.word _Swap-3
.byt 4 | FLAG_NONE
_Over:
    lda $02, x
    ldy $03, x
    dex
    dex
    sta $00, x
    sty $01, x
    jmp DoNext

; ( n1 n2 -- n1 & n2 )
.byt "and"
.word _Over-3
.byt 3 | FLAG_NONE
_And:
    lda $02, x
    and $00, x
    sta $02, x
    lda $03, x
    and $01, x
    sta $03, x
    inx
    inx
    jmp DoNext

; ( n1 n2 -- n1 | n2 )
.byt "or"
.word _And-3
.byt 2 | FLAG_NONE
_Or:
    lda $02, x
    ora $00, x
    sta $02, x
    lda $03, x
    ora $01, x
    sta $03, x
    inx
    inx
    jmp DoNext

; ( n1 n2 -- n1 ^ n2 )
.byt "xor"
.word _Or-3
.byt 3 | FLAG_NONE
_Xor:
    lda $02, x
    eor $00, x
    sta $02, x
    lda $03, x
    eor $01, x
    sta $03, x
    inx
    inx
    jmp DoNext

; ( n1 n2 -- n1 + n2 )
.byt "+"
.word _Xor-3
.byt 1 | FLAG_NONE
_Add:
    clc
    lda $02, x
    adc $00, x
    sta $02, x
    lda $03, x
    adc $01, x
    sta $03, x
    inx
    inx
    jmp DoNext

; ( n1 n2 -- n1 - n2 )
.byt "-"
.word _Add-3
.byt 1 | FLAG_NONE
_Sub:
    sec
    lda $02, x
    sbc $00, x
    sta $02, x
    lda $03, x
    sbc $01, x
    sta $03, x
    inx
    inx
    jmp DoNext

; ( addr -- )
.byt "goto"
.word _Sub-3
.byt 4 | FLAG_NONE
_Goto:
    lda $00, x
    sta ADRL
    lda $01, x
    sta ADRH
    inx
    inx
    jmp (ADR)

.byt "quit"
.word _Goto-3
.byt 4 | FLAG_NONE
_Quit:
    txa
    ldx #$FF
    txs
    tax
    lda #STATE_INTERPRET
    sta STATE
:   jsr SysInterpret
    jmp :-

.byt "abort"
.word _Quit-3
.byt 5 | FLAG_NONE
_Abort:
    sei
    cld
    ; TODO: reset SYSVARS
    lda #$6C
    sta ADRJ
    ldx #$00
    jmp _Quit

SYSTXTSTART:
.bin 0, 0, "fs/sysvm.n"

HERESTART = *
