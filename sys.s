; vim: ft=a65

#define FLAG_MASK      %00111111
#define FLAG_NONE      %00000000
#define FLAG_IMMEDIATE %10000000

#define STATE_COMPILE   %0
#define STATE_INTERPRET %1

#define INLEN 40

; Increment parameter top indirect
#define INPS   \
    INC $00, X \
    BNE :+     \
    INC $01, X \
:              \

; Increment instruction pointer
#define INIP   \
    INC IPL    \
    BNE :+     \
    INC IPH    \
:              \

; Instruction Pointer
IPL = $02
IPH = $03

; Indirect Address Pointer
ADRJ = $04
ADRL = $05
ADRH = $06

* = $0200

    JMP _Abort

SYSVARS = *
CURRENT: .word _Abort-3
HERE:    .word HERESTART
STATE:   .byt 0
ERRNO:   .byt 0
M:       .word 0
Q:       .word 0
EMIT:    .word 0
KEY:     .word 0
RDIN:    .word SysRefill
INOFF:   .byt  INLEN
INBUF:   .dsb  INLEN,0

SYSTXT:  .word SYSTXTSTART

DoCell:
    DEX
    DEX
    PLA
    STA $00, X
    PLA
    STA $01, X
    INPS
    ; Fall through to DoNext

; Jump to (IP) and increment IP by 2
DoNext:
    LDA IPL
    STA ADRL
    LDA IPH
    STA ADRH
    CLC
    ADC #2
    BCC :+
    INC IPH
:   STA IPL
    JMP ADRJ

SysRefill:
    LDY INOFF
    BEQ :++++++
    TXA
    PHA
    ; Move all bytes backwards
    LDX #0
    STX INOFF
:   CPY #INLEN
    BEQ :+
    LDA INBUF, Y
    STA INBUF, X
    INX
    INY
    BNE :-
    ; X is (INLEN - INOFF), read that many bytes
:   LDA SYSTXT+0
    STA ADRL
    LDA SYSTXT+1
    STA ADRH
    LDY #0
:   CPX #INLEN
    BEQ :+
    LDA (ADRL), Y
    STA INBUF, X
    INX
    INY
    BNE :-
    ; SYSTXT += Y
:   CLC
    TYA
    ADC ADRL
    STA SYSTXT+0
    BCC :+
:   INC SYSTXT+1
    PLA
    TAX
:   RTS

SysWord:
    JSR SysRefill
    LDY #0
    ; Skip leading spaces
:   LDA INBUF, Y
    INY
    CMP #' '
    BEQ :-
    DEY
    ; INOFF is start of word
    STY INOFF
    LDA INBUF, Y
    INY
    CMP #'"'
    BEQ :++
    CMP #' '
    BNE :-
:   DEY
:   DEY
    RTS ; Y is end of word

; .byt  name, ...
; .word prev
; .byt  flaglen
; native code / JSR to DTC routine

SysFind:
    TXA
    PHA
    TYA
    TAX ; X is end of word
    PHA ; Save it for later words
    LDA CURRENT+0
    STA ADRL
    LDA CURRENT+1
    STA ADRH



    PLA
    TAX
    RTS

SysInterpret:
    JSR SysWord
    JSR SysFind
    RTS

; ( addr -- n )
.byt "@"
.word 0
.byt 1 | FLAG_NONE
_Load:
    LDA ($00, X)
    TAY
    INPS
    LDA ($00, X)
    STY $00, X
    STA $01, X
    RTS

; ( addr -- b )
.byt "@b"
.word _Load-3
.byt 2 | FLAG_NONE
_LoadByte:
    LDY #0
    LDA ($00, X)
    STA $00, X
    BPL :+
    DEY
:   STA $01, X
    RTS

; ( addr -- bu )
.byt "@bu"
.word _LoadByte-3
.byt 3 | FLAG_NONE
_LoadByteUnsigned:
    LDA ($00, X)
    STA $00, X
    LDA #0
    STA $01, X
    RTS

; ( n addr -- )
.byt "!"
.word _LoadByteUnsigned-3
.byt 1 | FLAG_NONE
_Store:
    LDA $02, X
    STA ($00, X)
    INPS
    LDA $03, X
    STA ($00, X)
    INX
    INX
    INX
    INX
    RTS

; ( b addr -- )
.byt "!b"
.word _Store-3
.byt 2 | FLAG_NONE
_StoreByte:
    LDA $02, X
    STA ($00, X)
    INX
    INX
    INX
    INX
    RTS

; P: ( n -- ) R: ( -- n )
.byt ">R"
.word _StoreByte-3
.byt 2 | FLAG_NONE
_PushR:
    LDA $01, X
    PHA
    LDA $00, X
    PHA
    INX
    INX
    RTS

; R: ( n -- ) P: ( -- n )
.byt "R>"
.word _PushR-3
.byt 2 | FLAG_NONE
_PullR:
    DEX
    DEX
    PLA
    STA $00, X
    PLA
    STA $01, X
    RTS

; ( n -- )
.byt "drop"
.word _PullR-3
.byt 4 | FLAG_NONE
_Drop:
    INX
    INX
    RTS

; ( n -- n n )
.byt "dup"
.word _Drop-3
.byt 3 | FLAG_NONE
_Dup:
    LDA $00, X
    LDY $01, X
    DEX
    DEX
    STA $00, X
    STY $01, X
    RTS

; ( n1 n2 -- n2 n1 )
.byt "swap"
.word _Dup-3
.byt 4 | FLAG_NONE
_Swap:
    LDA $00, X
    TAY
    LDA $02, X
    STA $00, X
    STY $02, X
    LDA $01, X
    TAY
    LDA $03, X
    STA $01, X
    STY $03, X
    RTS

; ( n1 n2 -- n1 n2 n1 )
; TODO: >R dup R> swap
.byt "over"
.word _Swap-3
.byt 4 | FLAG_NONE
_Over:
    LDA $02, X
    LDY $03, X
    DEX
    DEX
    STA $00, X
    STY $01, X
    RTS

; ( n1 n2 -- n1 & n2 )
.byt "and"
.word _Over-3
.byt 3 | FLAG_NONE
_And:
    LDA $02, X
    AND $00, X
    STA $02, X
    LDA $03, X
    AND $01, X
    STA $03, X
    INX
    INX
    RTS

; ( n1 n2 -- n1 | n2 )
.byt "or"
.word _And-3
.byt 2 | FLAG_NONE
_Or:
    LDA $02, X
    ORA $00, X
    STA $02, X
    LDA $03, X
    ORA $01, X
    STA $03, X
    INX
    INX
    RTS

; ( n1 n2 -- n1 ^ n2 )
.byt "xor"
.word _Or-3
.byt 3 | FLAG_NONE
_Xor:
    LDA $02, X
    EOR $00, X
    STA $02, X
    LDA $03, X
    EOR $01, X
    STA $03, X
    INX
    INX
    RTS

; ( n1 n2 -- n1 + n2 )
.byt "+"
.word _Xor-3
.byt 1 | FLAG_NONE
_Add:
    CLC
    LDA $02, X
    ADC $00, X
    STA $02, X
    LDA $03, X
    ADC $01, X
    STA $03, X
    INX
    INX
    RTS

; ( n1 n2 -- n1 - n2 )
.byt "-"
.word _Add-3
.byt 1 | FLAG_NONE
_Sub:
    SEC
    LDA $02, X
    SBC $00, X
    STA $02, X
    LDA $03, X
    SBC $01, X
    STA $03, X
    INX
    INX
    RTS

; ( addr -- )
.byt "goto"
.word _Sub-3
.byt 4 | FLAG_NONE
_Goto:
    LDA $00, X
    STA ADRL
    LDA $01, X
    STA ADRH
    INX
    INX
    JMP (ADRL)

.byt "quit"
.word _Goto-3
.byt 4 | FLAG_NONE
_Quit:
    TXA
    LDX #$FF
    TXS
    TAX
    LDA #STATE_INTERPRET
    STA STATE
:   JSR SysInterpret
    JMP :-

.byt "abort"
.word _Quit-3
.byt 5 | FLAG_NONE
_Abort:
    CLD
    ; TODO: reset SYSVARS
    LDA #$6C
    STA ADRJ
    LDX #$00
    JMP _Quit

SYSTXTSTART:
.bin 0, 0, "fs/sysvm.n"

HERESTART = *
