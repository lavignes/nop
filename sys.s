; vim: ft=a65

#define FLAG_MASK      $E0
#define FLAG_NONE      $00
#define FLAG_IMMEDIATE $10

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

Init:
    LDA #$6C
    STA ADRJ
    LDX #$00
    JMP _Quit

SYSVARS = *
ERRNO:   .word 0
CURRENT: .word _Quit-2
HERE:    .word HERESTART
Z:       .word 0
N:       .word 0
RDLN     .word 0
EMIT     .word 0
KEY      .word 0
INPTR    .word 0
INBUF    .dsb  40,0

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
    LDY IPL
    STA ADRL
    LDA IPH
    STA ADRH
    CLC
    ADC #$02
    BCC :+
    INC IPH
:   STA IPL
    JMP ADRJ

; ( addr -- n )
.byt "@", 1 | FLAG_NONE
.word 0
_Load:
    LDA ($00, X)
    TAY
    INPS
    LDA ($00, X)
    STY $00, X
    STA $01, X
    RTS

; ( addr -- b )
.byt "@bu", 2 | FLAG_NONE
.word _Load-2
_LoadByteUnsigned:
    LDA ($00, X)
    STA $00, X
    LDA #$00
    STA $01, X
    RTS

; ( n addr -- )
.byt "!", 1 | FLAG_NONE
.word _Load-2
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
.byt "!b", 2 | FLAG_NONE
.word _Store-2
_StoreByte:
    LDA $02, X
    STA ($00, X)
    INX
    INX
    INX
    INX
    RTS

; ( n -- R: n )
.byt ">R", 2 | FLAG_NONE
.word _Store-2
_PushR:
    LDA $01, X
    PHA
    LDA $00, X
    PHA
    INX
    INX
    RTS

; ( R: n -- n )
.byt "R>", 2 | FLAG_NONE
.word _PushR-2
_PullR:
    DEX
    DEX
    PLA
    STA $00, X
    PLA
    STA $01, X
    RTS

; ( n -- )
.byt "drop", 4 | FLAG_NONE
.word _PullR-2
_Drop:
    INX
    INX
    RTS

; ( n -- n n )
.byt "dup", 3 | FLAG_NONE
.word _Drop-2
_Dup:
    LDA $00, X
    LDY $01, X
    DEX
    DEX
    STA $00, X
    STY $01, X
    RTS

; ( n1 n2 -- n2 n1 )
.byt "swap", 4 | FLAG_NONE
.word _Dup-2
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
.byt "over", 4 | FLAG_NONE
.word _Swap-2
_Over:
    LDA $02, X
    LDY $03, X
    DEX
    DEX
    STA $00, X
    STY $01, X
    RTS

; ( n1 n2 -- n1 & n2 )
.byt "and", 3 | FLAG_NONE
.word _Over-2
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
.byt "or", 2 | FLAG_NONE
.word _And-2
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
.byt "xor", 3 | FLAG_NONE
.word _Or-2
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
.byt "+", 1 | FLAG_NONE
.word _Xor-2
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
.byt "-", 1 | FLAG_NONE
.word _Add-2
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
.byt "goto", 4 | FLAG_NONE
.word _Sub-2
_Goto:
    LDA $00, X
    STA ADRL
    LDA $01, X
    STA ADRH
    INX
    INX
    JMP (ADRL)

.byt "quit", 4 | FLAG_NONE
.word 0
_Quit:
    TXA
    LDX #$FF
    TXS
    TAX
    ;JSR _Read
    ;JSR _Eval
    JMP _Quit

.bin 0, 0, "fs/sysvm.n"

HERESTART = *
