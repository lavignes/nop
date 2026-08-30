; vim: ft=a65
;
; A small text-mode monitor for nop. Type a command line and press Enter:
;
;   R AAAA          - dump 128 bytes of memory starting at AAAA
;   W AAAA BB BB .. - write the given bytes to memory starting at AAAA
;   L NNNN          - load disk block NNNN into the buffer, then dump it
;   S NNNN          - store the buffer to disk block NNNN
;
; AAAA/NNNN/BB are hex, letters uppercase. The disk buffer is one 512-byte
; block at BUF. Blocks are byte-addressed as NNNN*512 (SDSC, see vm/sd.c).

* = $E000

#define VDP_CMD_REG(R) ($80 | ((R) & $07))

VDP_PORT0 = $C000
VDP_PORT1 = $C001

VIA0     = $C100
VIA_ORA  = VIA0 + $1
VIA_DDRB = VIA0 + $2
VIA_DDRA = VIA0 + $3
VIA_SR   = VIA0 + $A
VIA_ACR  = VIA0 + $B
VIA_PCR  = VIA0 + $C
VIA_IFR  = VIA0 + $D
VIA_IER  = VIA0 + $E

; SD card wiring on VIA port B (see vm/sd.c)
SD_CS   = $01 ; PA0, active low
SD_SCK  = $02 ; PA1
SD_MOSI = $04 ; PA2
SD_MISO = $80 ; PA7, input

; VDP text-mode layout
NAME_BASE     = $0000 ; name table in VRAM
PAT_BASE      = $0800 ; pattern table in VRAM
COLS          = 40
ROWS          = 24
VDP_CMD_WRITE = $40

; Zero page
PTR    = $10
PTR_HI = $11
TMP    = $12
TMP2   = $13
TMP3   = $14
NUM    = $15
NUM_HI = $16
CNT    = $17
CNTLO  = $18
CNTHI  = $19
ROW    = $1A
COL    = $1B
LINE_LEN = $1C
LINEPOS  = $1D
BLK    = $1E
BLK_HI = $1F
ARG0   = $20
ARG1   = $21
ARG2   = $22
ARG3   = $23
SD_OUT = $24 ; current port-B output level (holds CS state)
SDB    = $25
SVX    = $26 ; ScrollUp X/Y save
SVY    = $27
SPTR   = $2D ; ScrollUp staging pointer (must not alias PTR)
SPTR_HI = $2E
; Keyboard ring buffer, filled by the IRQ handler and drained by GetKey.
KHEAD  = $28 ; consumer index (GetKey)
KTAIL  = $29 ; producer index (IRQ)
KREL   = $2A ; next code is a key-up (F0 seen)
KEXT   = $2B ; next code is extended (E0 seen)
KSC    = $2C ; Translate scratch (IRQ-owned)
KSHIFT = $2F ; shift held (IRQ-owned)
DBUF   = $30 ; DumpMem row bytes ($30..$37)

; RAM buffers
LINE    = $0200 ; input line (up to 40 chars)
KBUF    = $0230 ; keyboard ring buffer (16 bytes)
BUF     = $0400 ; disk block buffer (512 bytes)
SCRATCH = $0600 ; scroll staging (up to (ROWS-1)*COLS bytes)

Reset:
    cld
    sei
    ldx #$FF
    txs
    lda #$00
    sta KHEAD
    sta KTAIL
    sta KREL
    sta KEXT
    sta KSHIFT
    jsr VdpInit
    jsr ViaInit
    jsr ClearScreen
    cli ; enable IRQs; the keyboard is interrupt-driven
    lda #<Banner
    ldy #>Banner
    jsr PrintStr
MainLoop:
    lda #'>'
    jsr PutChar
    jsr ReadLine
    jsr RunLine
    jmp MainLoop

Banner:
    .byt "NOP MONITOR", $0D
    .byt "R AAAA  W AAAA BB..", $0D
    .byt "G AAAA  L NNNN  S NNNN", $0D, $00
MsgErr: .byt "?", $0D, $00
MsgIO:  .byt "IO ERR", $0D, $00

; -------- Command dispatch --------
RunLine:
    lda LINE_LEN
    beq @done
    lda LINE
    cmp #'R'
    beq CmdRead
    cmp #'W'
    beq CmdWrite
    cmp #'L'
    beq CmdLoad
    cmp #'S'
    beq CmdStore
    cmp #'G'
    beq CmdJump
    lda #<MsgErr
    ldy #>MsgErr
    jmp PrintStr
@done:
    rts

; R AAAA
CmdRead:
    jsr ArgAddr
    lda NUM
    sta PTR
    lda NUM_HI
    sta PTR_HI
    lda #$80 ; 128 bytes = 8 rows of 16
    sta CNT
    jmp DumpMem

; W AAAA BB BB ..
CmdWrite:
    jsr ArgAddr
    lda NUM
    sta PTR
    lda NUM_HI
    sta PTR_HI
@loop:
    jsr SkipSpaces
    ldx LINEPOS
    cpx LINE_LEN
    bcs @done
    lda LINEPOS
    sta TMP2 ; remember position to detect a non-hex token
    jsr ParseHex
    lda LINEPOS
    cmp TMP2
    beq @done ; nothing consumed: stop
    lda NUM
    ldy #$00
    sta (PTR), Y
    inc PTR
    bne @loop
    inc PTR_HI
    jmp @loop
@done:
    lda #$0D
    jmp PutChar

; L NNNN
CmdLoad:
    jsr ArgBlk
    jsr SdReadBlock
    bcs IoErr
    lda #<BUF
    sta PTR
    lda #>BUF
    sta PTR_HI
    lda #$80
    sta CNT
    jmp DumpMem

; S NNNN
CmdStore:
    jsr ArgBlk
    jsr SdWriteBlock
    bcs IoErr
    lda #$0D
    jmp PutChar

IoErr:
    lda #<MsgIO
    ldy #>MsgIO
    jmp PrintStr

; G AAAA : jump to the address. If the routine ends in RTS it returns to the
; monitor prompt (RunLine's return address is still on the stack).
CmdJump:
    jsr ArgAddr
    lda NUM
    sta PTR
    lda NUM_HI
    sta PTR_HI
    jmp (PTR)

; Parse the address argument after the command letter into NUM/NUM_HI.
ArgAddr:
    lda #$01
    sta LINEPOS ; skip command letter
    jsr SkipSpaces
    jmp ParseHex

; Parse the block argument into BLK/BLK_HI.
ArgBlk:
    jsr ArgAddr
    lda NUM
    sta BLK
    lda NUM_HI
    sta BLK_HI
    rts

; -------- Memory dump: PTR = start, CNT = byte count (multiple of 8) --------
; Each row: address, 8 hex bytes, then their ASCII (non-$20..$5F shown as '.').
DumpMem:
@row:
    lda PTR_HI
    jsr PrintHex8
    lda PTR
    jsr PrintHex8
    lda #':'
    jsr PutChar
    lda #' '
    jsr PutChar
    ldx #$00
@byte:
    ldy #$00
    lda (PTR), Y
    sta DBUF, X ; keep the byte for the ASCII column
    jsr PrintHex8
    lda #' '
    jsr PutChar
    inc PTR
    bne @noc
    inc PTR_HI
@noc:
    dec CNT
    inx
    cpx #$08
    bne @byte
    lda #' '
    jsr PutChar
    ldx #$00
@asc:
    lda DBUF, X
    cmp #$20
    bcc @dot
    cmp #$60
    bcc @put
@dot:
    lda #'.'
@put:
    jsr PutChar
    inx
    cpx #$08
    bne @asc
    lda #$0D
    jsr PutChar
    lda CNT
    bne @row
    rts

; -------- Line input --------
ReadLine:
    lda #$00
    sta LINE_LEN
@loop:
    jsr GetKey ; ASCII in A
    cmp #$0D
    beq @enter
    cmp #$08 ; backspace
    beq @bs
    ldx LINE_LEN
    cpx #COLS-1
    bcs @loop ; line full, ignore
    sta LINE, X
    inc LINE_LEN
    jsr PutChar
    jmp @loop
@bs:
    lda LINE_LEN
    beq @loop
    dec LINE_LEN
    jsr BackSpace
    jmp @loop
@enter:
    lda #$0D
    jmp PutChar

; -------- Hex parsing over LINE, position in LINEPOS --------
SkipSpaces:
@l:
    ldx LINEPOS
    cpx LINE_LEN
    bcs @done
    lda LINE, X
    cmp #' '
    bne @done
    inc LINEPOS
    jmp @l
@done:
    rts

; Parse up to 4 hex digits from LINEPOS into NUM/NUM_HI, stopping at non-hex.
ParseHex:
    lda #$00
    sta NUM
    sta NUM_HI
@l:
    ldx LINEPOS
    cpx LINE_LEN
    bcs @done
    lda LINE, X
    jsr HexDigit ; -> A value, carry set if valid
    bcc @done
    sta TMP
    ldy #$04
@sh:
    asl NUM
    rol NUM_HI
    dey
    bne @sh
    lda NUM
    ora TMP
    sta NUM
    inc LINEPOS
    jmp @l
@done:
    rts

; A = ascii; returns value in A with carry set if a hex digit, else carry clear
HexDigit:
    cmp #'0'
    bcc @no
    cmp #('9'+1)
    bcc @dig
    cmp #'A'
    bcc @no
    cmp #('F'+1)
    bcs @no
    sec
    sbc #('A'-10)
    sec
    rts
@dig:
    sec
    sbc #'0'
    sec
    rts
@no:
    clc
    rts

; -------- Hex output --------
; Print A as two hex digits.
PrintHex8:
    pha
    lsr
    lsr
    lsr
    lsr
    jsr @nib
    pla
    and #$0F
@nib:
    and #$0F
    cmp #$0A
    bcc @dig
    clc
    adc #('A'-10)
    jmp PutChar
@dig:
    clc
    adc #'0'
    jmp PutChar

; -------- Keyboard --------
; The IRQ handler decodes PS/2 scancodes and pushes ASCII into KBUF.
; GetKey blocks until a byte is available, then returns it in A.
GetKey:
@wait:
    lda KTAIL
    cmp KHEAD
    beq @wait ; buffer empty
    ldx KHEAD
    lda KBUF, X
    pha
    inx
    txa
    and #$0F ; 16-entry ring
    sta KHEAD
    pla
    rts

; Translate PS/2 Set-2 make code in A to ASCII (0 = ignore), honoring KSHIFT.
; IRQ-only; uses KSC as scratch so it cannot corrupt main-line state.
Translate:
    sta KSC
    ldx #$00
@l:
    lda ScanTbl, X
    beq @none ; $00 terminates the table
    cmp KSC
    beq @hit
    inx
    jmp @l
@hit:
    lda KSHIFT
    beq @unshift
    lda ShiftTbl, X
    rts
@unshift:
    lda AsciiTbl, X
    rts
@none:
    lda #$00
    rts

; Push the ASCII byte in A into KBUF (drop it if the buffer is full).
KbPush:
    ldy KTAIL
    sta KBUF, Y
    iny
    tya
    and #$0F
    cmp KHEAD
    beq @full ; would collide with head: drop the byte
    sta KTAIL
@full:
    rts

; Parallel tables: ScanTbl[i] -> AsciiTbl[i] (unshifted) / ShiftTbl[i] (shift).
ScanTbl:
    .byt $1C, $32, $21, $23, $24, $2B, $34, $33 ; A B C D E F G H
    .byt $43, $3B, $42, $4B, $3A, $31, $44, $4D ; I J K L M N O P
    .byt $15, $2D, $1B, $2C, $3C, $2A, $1D, $22 ; Q R S T U V W X
    .byt $35, $1A                               ; Y Z
    .byt $45, $16, $1E, $26, $25, $2E, $36, $3D ; 0 1 2 3 4 5 6 7
    .byt $3E, $46                               ; 8 9
    .byt $4E, $55, $54, $5B, $5D, $4C, $52      ; - = [ ] \ ; '
    .byt $41, $49, $4A                          ; , . /
    .byt $29, $5A, $66                          ; space enter backspace
    .byt $00
AsciiTbl:
    .byt 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'
    .byt 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P'
    .byt 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X'
    .byt 'Y', 'Z'
    .byt '0', '1', '2', '3', '4', '5', '6', '7'
    .byt '8', '9'
    .byt $2D, $3D, $5B, $5D, $5C, $3B, $27      ; - = [ ] \ ; '
    .byt $2C, $2E, $2F                          ; , . /
    .byt ' ', $0D, $08
ShiftTbl:
    .byt 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'
    .byt 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P'
    .byt 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X'
    .byt 'Y', 'Z'
    .byt $29, $21, $40, $23, $24, $25, $5E, $26 ; ) ! @ # $ % ^ &
    .byt $2A, $28                               ; * (
    .byt $5F, $2B, $7B, $7D, $7C, $3A, $22      ; _ + { } | : "
    .byt $3C, $3E, $3F                          ; < > ?
    .byt ' ', $0D, $08

; -------- Text output --------
; PutChar: A = ascii. Handles $0D (newline) and prints $20..$5F glyphs.
PutChar:
    cmp #$0D
    beq NewLine
    cmp #'a'
    bcc @nolow
    cmp #('z'+1)
    bcs @nolow
    sec
    sbc #$20 ; lowercase -> uppercase
@nolow:
    cmp #$20
    bcc @done ; control char
    cmp #$60
    bcs @done ; past the font
    pha
    jsr SetCurPtr
    pla
    sta VDP_PORT0
    ; advance cursor
    inc COL
    lda COL
    cmp #COLS
    bcc @done
    lda #$00
    sta COL
    jsr NextRow
@done:
    rts

NewLine:
    lda #$00
    sta COL
    jmp NextRow

NextRow:
    inc ROW
    lda ROW
    cmp #ROWS
    bcc @done
    jsr ScrollUp
    lda #(ROWS-1)
    sta ROW
@done:
    rts

BackSpace:
    lda COL
    bne @dec
    lda ROW
    beq @done ; at top-left, nothing to do
    dec ROW
    lda #(COLS-1)
    sta COL
    jmp @blank
@dec:
    dec COL
@blank:
    jsr SetCurPtr
    lda #$20
    sta VDP_PORT0
@done:
    rts

; -------- VDP cursor / addressing --------
; Compute VRAM name-table address of (ROW,COL) into TMP/TMP2 = ROW*COLS + COL.
CellAddr:
    lda ROW
    sta TMP
    lda #$00
    sta TMP2
    asl TMP
    rol TMP2
    asl TMP
    rol TMP2
    asl TMP
    rol TMP2 ; ROW*8
    lda TMP
    sta TMP3 ; save low of ROW*8 (high fits in TMP2 but ROW*8 < 192 so TMP2=0)
    asl TMP
    rol TMP2
    asl TMP
    rol TMP2 ; ROW*32
    clc
    lda TMP
    adc TMP3
    sta TMP
    lda TMP2
    adc #$00
    sta TMP2 ; ROW*40
    clc
    lda TMP
    adc COL
    sta TMP
    lda TMP2
    adc #$00
    sta TMP2
    rts

; Point the VDP write address at the current cursor cell.
SetCurPtr:
    jsr CellAddr
    lda TMP ; NAME_BASE is 0, so address == TMP/TMP2
    sta VDP_PORT1
    lda TMP2
    ora #VDP_CMD_WRITE
    sta VDP_PORT1
    rts

ClearScreen:
    lda #<NAME_BASE
    sta VDP_PORT1
    lda #>NAME_BASE
    ora #VDP_CMD_WRITE
    sta VDP_PORT1
    ldx #$04 ; 4*256 = 1024 cells (>= COLS*ROWS), harmless spill
    ldy #$00
@l:
    lda #$20
    sta VDP_PORT0
    iny
    bne @l
    dex
    bne @l
    lda #$00
    sta ROW
    sta COL
    rts

; Scroll the text area up one line; last line is blanked. Preserves X/Y so
; callers can hold loop counters across a PutChar that triggers a scroll.
ScrollUp:
    stx SVX
    sty SVY
    ; read rows 1..ROWS-1 into SCRATCH
    lda #<(NAME_BASE + COLS)
    sta VDP_PORT1
    lda #>(NAME_BASE + COLS)
    sta VDP_PORT1 ; read mode (no write bit)
    lda #<SCRATCH
    sta SPTR
    lda #>SCRATCH
    sta SPTR_HI
    lda #<((ROWS-1)*COLS)
    sta CNTLO
    lda #>((ROWS-1)*COLS)
    sta CNTHI
@rd:
    lda VDP_PORT0
    ldy #$00
    sta (SPTR), Y
    inc SPTR
    bne @r1
    inc SPTR_HI
@r1:
    dec CNTLO
    lda CNTLO
    cmp #$FF
    bne @r2
    dec CNTHI
@r2:
    lda CNTLO
    ora CNTHI
    bne @rd
    ; write SCRATCH back starting at row 0
    lda #<NAME_BASE
    sta VDP_PORT1
    lda #>NAME_BASE
    ora #VDP_CMD_WRITE
    sta VDP_PORT1
    lda #<SCRATCH
    sta SPTR
    lda #>SCRATCH
    sta SPTR_HI
    lda #<((ROWS-1)*COLS)
    sta CNTLO
    lda #>((ROWS-1)*COLS)
    sta CNTHI
@wr:
    ldy #$00
    lda (SPTR), Y
    sta VDP_PORT0
    inc SPTR
    bne @w1
    inc SPTR_HI
@w1:
    dec CNTLO
    lda CNTLO
    cmp #$FF
    bne @w2
    dec CNTHI
@w2:
    lda CNTLO
    ora CNTHI
    bne @wr
    ; blank the last row
    lda #<(NAME_BASE + (ROWS-1)*COLS)
    sta VDP_PORT1
    lda #>(NAME_BASE + (ROWS-1)*COLS)
    ora #VDP_CMD_WRITE
    sta VDP_PORT1
    ldx #COLS
@bl:
    lda #$20
    sta VDP_PORT0
    dex
    bne @bl
    ldx SVX
    ldy SVY
    rts

; PrintStr: A=lo, Y=hi of a $00-terminated string.
PrintStr:
    sta PTR
    sty PTR_HI
    ldy #$00
@l:
    lda (PTR), Y
    beq @done
    jsr PutChar
    inc PTR
    bne @l
    inc PTR_HI
    jmp @l
@done:
    rts

; -------- SD card (SPI, bit-banged over VIA port B) --------
; Exchange one byte: A out (MSB first), returns received byte in A.
; Preserves X and Y. CS level is held in SD_OUT (MOSI/SCK low there).
SdXfer:
    stx TMP3 ; save X (Y is untouched)
    sta TMP
    ldx #$08
@bit:
    asl TMP ; next out bit -> carry
    lda SD_OUT
    bcc @z
    ora #SD_MOSI
@z:
    sta SDB
    sta VIA_ORA ; drive MOSI, SCK low
    ora #SD_SCK
    sta VIA_ORA ; rising edge: card samples MOSI, presents MISO
    lda VIA_ORA
    asl ; MISO (bit 7) -> carry
    rol TMP2 ; shift into result (MSB first)
    lda SDB
    sta VIA_ORA ; SCK low
    dex
    bne @bit
    ldx TMP3
    lda TMP2
    rts

SdSelect:
    lda #$00 ; CS low, SCK low, MOSI low
    sta SD_OUT
    sta VIA_ORA
    rts

SdDeselect:
    lda #SD_CS
    sta SD_OUT
    sta VIA_ORA
    lda #$FF ; one trailing byte to release the bus
    jmp SdXfer

; Wait for a non-$FF byte (timeout ~256), return it in A.
SdWaitResp:
    ldy #$00
@l:
    lda #$FF
    jsr SdXfer
    cmp #$FF
    bne @done
    dey
    bne @l
@done:
    rts

; Send a command frame. A = index (0..63), arg in ARG0..ARG3. Returns R1 in A.
SdCmd:
    ora #$40 ; command start bits
    jsr SdXfer
    lda ARG0
    jsr SdXfer
    lda ARG1
    jsr SdXfer
    lda ARG2
    jsr SdXfer
    lda ARG3
    jsr SdXfer
    lda #$95 ; valid CRC for CMD0; ignored otherwise
    jsr SdXfer
    jmp SdWaitResp

; Put the card into SPI mode. Carry set on error.
SdInit:
    lda #SD_CS ; 80 clocks with CS high
    sta SD_OUT
    sta VIA_ORA
    ldx #$0A
@clk:
    lda #$FF
    jsr SdXfer
    dex
    bne @clk
    jsr SdSelect
    lda #$00
    sta ARG0
    sta ARG1
    sta ARG2
    sta ARG3
    lda #$00 ; CMD0 GO_IDLE_STATE
    jsr SdCmd
    cmp #$01 ; expect idle
    bne @err
@acmd:
    lda #55 ; CMD55 APP_CMD
    jsr SdCmd
    lda #41 ; ACMD41 SD_SEND_OP_COND
    jsr SdCmd
    cmp #$00 ; ready?
    bne @acmd
    jsr SdDeselect
    clc
    rts
@err:
    jsr SdDeselect
    sec
    rts

; Compute byte address = BLK * 512 into ARG0..ARG3 (big-endian).
BlkToArg:
    lda BLK
    sta TMP  ; low
    lda BLK_HI
    sta TMP2 ; mid
    lda #$00
    sta TMP3 ; high
    ldx #$09 ; << 9
@sh:
    asl TMP
    rol TMP2
    rol TMP3
    dex
    bne @sh
    lda #$00
    sta ARG0
    lda TMP3
    sta ARG1
    lda TMP2
    sta ARG2
    lda TMP
    sta ARG3
    rts

; Read block BLK into BUF. Carry set on error.
SdReadBlock:
    jsr SdInit
    bcs @err
    jsr BlkToArg
    jsr SdSelect
    lda #17 ; CMD17 READ_SINGLE_BLOCK
    jsr SdCmd
    cmp #$00
    bne @err2
    ldy #$00 ; wait for the data start token
@wtok:
    lda #$FF
    jsr SdXfer
    cmp #$FE
    beq @data
    dey
    bne @wtok
    jmp @err2
@data:
    lda #<BUF
    sta PTR
    lda #>BUF
    sta PTR_HI
    ldx #$02 ; 2 pages = 512 bytes
    ldy #$00
@rd:
    lda #$FF
    jsr SdXfer
    sta (PTR), Y
    iny
    bne @rd
    inc PTR_HI
    dex
    bne @rd
    lda #$FF ; eat 2 CRC bytes
    jsr SdXfer
    lda #$FF
    jsr SdXfer
    jsr SdDeselect
    clc
    rts
@err2:
    jsr SdDeselect
@err:
    sec
    rts

; Write BUF to block BLK. Carry set on error.
SdWriteBlock:
    jsr SdInit
    bcs @err
    jsr BlkToArg
    jsr SdSelect
    lda #24 ; CMD24 WRITE_BLOCK
    jsr SdCmd
    cmp #$00
    bne @err2
    lda #$FE ; data start token
    jsr SdXfer
    lda #<BUF
    sta PTR
    lda #>BUF
    sta PTR_HI
    ldx #$02
    ldy #$00
@wr:
    lda (PTR), Y
    jsr SdXfer
    iny
    bne @wr
    inc PTR_HI
    dex
    bne @wr
    lda #$FF ; dummy CRC
    jsr SdXfer
    lda #$FF
    jsr SdXfer
    lda #$FF ; data response
    jsr SdXfer
    and #$1F
    cmp #$05 ; accepted?
    bne @err2
    ldy #$00 ; wait out busy (card holds MISO 0)
@busy:
    lda #$FF
    jsr SdXfer
    cmp #$00
    bne @done
    dey
    bne @busy
@done:
    jsr SdDeselect
    clc
    rts
@err2:
    jsr SdDeselect
@err:
    sec
    rts

; -------- VDP init: text mode 40x24 --------
VdpInit:
    ldx #$00
@l:
    lda VdpRegs, X
    sta VDP_PORT1
    txa
    ora #VDP_CMD_REG(0)
    sta VDP_PORT1
    inx
    cpx #$08
    bne @l
    ; load the font into the pattern table at PAT_BASE + $20*8
    lda #<(PAT_BASE + $20*8)
    sta VDP_PORT1
    lda #>(PAT_BASE + $20*8)
    ora #VDP_CMD_WRITE
    sta VDP_PORT1
    lda #<Font
    sta PTR
    lda #>Font
    sta PTR_HI
    ldx #$02 ; 512 bytes (64 glyphs * 8)
    ldy #$00
@fl:
    lda (PTR), Y
    sta VDP_PORT0
    iny
    bne @fl
    inc PTR_HI
    dex
    bne @fl
    rts

VdpRegs:
    .byt $00               ; R0: text mode
    .byt $D0               ; R1: enable + 16K + mode 1 (text)
    .byt (NAME_BASE >> 10) ; R2: name table base
    .byt $00               ; R3: unused in text
    .byt (PAT_BASE >> 11)  ; R4: pattern table base
    .byt $00               ; R5: unused
    .byt $00               ; R6: unused
    .byt $F5               ; R7: white on blue

; -------- VIA init --------
ViaInit:
    lda #(SD_CS | SD_SCK | SD_MOSI) ; port A: SPI outputs, PA7 (MISO) input
    sta VIA_DDRA
    lda #$00 ; port B: CB1/CB2 keyboard input
    sta VIA_DDRB
    lda #SD_CS ; deselect card
    sta VIA_ORA
    sta SD_OUT
    lda #%00001100 ; ACR: CB1 input, shift register in external clock mode
    sta VIA_ACR
    lda #$00 ; CB1 negative edge
    sta VIA_PCR
    lda #$7F
    sta VIA_IFR
    lda #$84 ; enable SR interrupt (bit 2)
    sta VIA_IER
    rts

; -------- Font: 6x8 glyphs, ASCII $20..$5F --------
Font:
    .byt $00, $00, $00, $00, $00, $00, $00, $00   ; $20 ' '
    .byt $20, $20, $20, $20, $20, $00, $20, $00   ; $21 '!'
    .byt $50, $50, $50, $00, $00, $00, $00, $00   ; $22 '"'
    .byt $50, $50, $F8, $50, $F8, $50, $50, $00   ; $23 '#'
    .byt $20, $78, $A0, $70, $28, $78, $20, $00   ; $24 '$'
    .byt $C0, $C8, $10, $20, $48, $88, $00, $00   ; $25 '%'
    .byt $60, $90, $A0, $40, $A8, $90, $68, $00   ; $26 '&'
    .byt $20, $20, $20, $00, $00, $00, $00, $00   ; $27 '''
    .byt $10, $20, $40, $40, $40, $20, $10, $00   ; $28 '('
    .byt $40, $20, $10, $10, $10, $20, $40, $00   ; $29 ')'
    .byt $00, $20, $A8, $70, $A8, $20, $00, $00   ; $2A '*'
    .byt $00, $20, $20, $F8, $20, $20, $00, $00   ; $2B '+'
    .byt $00, $00, $00, $00, $00, $20, $20, $40   ; $2C ','
    .byt $00, $00, $00, $F8, $00, $00, $00, $00   ; $2D '-'
    .byt $00, $00, $00, $00, $00, $20, $20, $00   ; $2E '.'
    .byt $08, $08, $10, $20, $40, $80, $80, $00   ; $2F '/'
    .byt $70, $88, $98, $A8, $C8, $88, $70, $00   ; $30 '0'
    .byt $20, $60, $20, $20, $20, $20, $70, $00   ; $31 '1'
    .byt $70, $88, $08, $30, $40, $80, $F8, $00   ; $32 '2'
    .byt $F8, $08, $10, $30, $08, $88, $70, $00   ; $33 '3'
    .byt $18, $28, $48, $88, $F8, $08, $08, $00   ; $34 '4'
    .byt $F8, $80, $F0, $08, $08, $88, $70, $00   ; $35 '5'
    .byt $30, $40, $80, $F0, $88, $88, $70, $00   ; $36 '6'
    .byt $F8, $08, $10, $20, $40, $40, $40, $00   ; $37 '7'
    .byt $70, $88, $88, $70, $88, $88, $70, $00   ; $38 '8'
    .byt $70, $88, $88, $78, $08, $10, $60, $00   ; $39 '9'
    .byt $00, $20, $20, $00, $00, $20, $20, $00   ; $3A ':'
    .byt $00, $20, $20, $00, $20, $20, $40, $00   ; $3B ';'
    .byt $10, $20, $40, $80, $40, $20, $10, $00   ; $3C '<'
    .byt $00, $00, $F8, $00, $F8, $00, $00, $00   ; $3D '='
    .byt $40, $20, $10, $08, $10, $20, $40, $00   ; $3E '>'
    .byt $70, $88, $08, $10, $20, $00, $20, $00   ; $3F '?'
    .byt $70, $88, $B8, $A8, $B8, $80, $78, $00   ; $40 '@'
    .byt $70, $88, $88, $F8, $88, $88, $88, $00   ; $41 'A'
    .byt $F0, $88, $88, $F0, $88, $88, $F0, $00   ; $42 'B'
    .byt $78, $88, $80, $80, $80, $88, $78, $00   ; $43 'C'
    .byt $E0, $90, $88, $88, $88, $90, $E0, $00   ; $44 'D'
    .byt $F8, $80, $80, $F0, $80, $80, $F8, $00   ; $45 'E'
    .byt $F8, $80, $80, $F0, $80, $80, $80, $00   ; $46 'F'
    .byt $78, $88, $80, $B8, $88, $88, $78, $00   ; $47 'G'
    .byt $88, $88, $88, $F8, $88, $88, $88, $00   ; $48 'H'
    .byt $70, $20, $20, $20, $20, $20, $70, $00   ; $49 'I'
    .byt $18, $08, $08, $08, $88, $88, $70, $00   ; $4A 'J'
    .byt $88, $90, $A0, $C0, $A0, $90, $88, $00   ; $4B 'K'
    .byt $80, $80, $80, $80, $80, $80, $F8, $00   ; $4C 'L'
    .byt $88, $D8, $A8, $A8, $88, $88, $88, $00   ; $4D 'M'
    .byt $88, $C8, $A8, $A8, $98, $88, $88, $00   ; $4E 'N'
    .byt $70, $88, $88, $88, $88, $88, $70, $00   ; $4F 'O'
    .byt $F0, $88, $88, $F0, $80, $80, $80, $00   ; $50 'P'
    .byt $70, $88, $88, $88, $A8, $90, $68, $00   ; $51 'Q'
    .byt $F0, $88, $88, $F0, $A0, $90, $88, $00   ; $52 'R'
    .byt $78, $80, $80, $70, $08, $08, $F0, $00   ; $53 'S'
    .byt $F8, $20, $20, $20, $20, $20, $20, $00   ; $54 'T'
    .byt $88, $88, $88, $88, $88, $88, $70, $00   ; $55 'U'
    .byt $88, $88, $88, $88, $88, $50, $20, $00   ; $56 'V'
    .byt $88, $88, $88, $A8, $A8, $D8, $88, $00   ; $57 'W'
    .byt $88, $88, $50, $20, $50, $88, $88, $00   ; $58 'X'
    .byt $88, $88, $50, $20, $20, $20, $20, $00   ; $59 'Y'
    .byt $F8, $08, $10, $20, $40, $80, $F8, $00   ; $5A 'Z'
    .byt $70, $40, $40, $40, $40, $40, $70, $00   ; $5B '['
    .byt $80, $80, $40, $20, $10, $08, $08, $00   ; $5C '\'
    .byt $70, $10, $10, $10, $10, $10, $70, $00   ; $5D ']'
    .byt $20, $50, $88, $00, $00, $00, $00, $00   ; $5E '^'
    .byt $00, $00, $00, $00, $00, $00, $00, $F8   ; $5F '_'

; -------- IRQ: keyboard scancode ready on VIA port A (CA1) --------
Irq:
    pha
    txa
    pha
    tya
    pha
    lda VIA_SR ; scancode; reading clears the SR flag
    cmp #$F0 ; break prefix
    beq @setrel
    cmp #$E0 ; extended prefix
    beq @setext
    cmp #$12 ; left shift
    beq @shift
    cmp #$59 ; right shift
    beq @shift
    ldx KREL
    bne @wasrel ; this code is a key-up: ignore
    ldx KEXT
    bne @wasext ; this code is extended: ignore
    jsr Translate
    cmp #$00
    beq @out
    jsr KbPush
    jmp @out
@shift:
    ldx KREL
    bne @shiftup ; shift released
    lda #$01
    sta KSHIFT
    jmp @out
@shiftup:
    lda #$00
    sta KSHIFT
    sta KREL ; consume the release
    jmp @out
@setrel:
    lda #$01
    sta KREL
    jmp @out
@setext:
    lda #$01
    sta KEXT
    jmp @out
@wasrel:
    lda #$00 ; clear both: handles E0 F0 xx (extended key-up)
    sta KREL
    sta KEXT
    jmp @out
@wasext:
    lda #$00
    sta KEXT
@out:
    pla
    tay
    pla
    tax
    pla
    rti

.dsb $FFFA - *, $00
Vectors:
    .word Reset ; NMI (unused)
    .word Reset ; RESET
    .word Irq   ; IRQ/BRK
