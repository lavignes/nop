; vim: ft=a65

#include "vdp.inc"
#include "psg.inc"

ZP_LO  = $00
ZP_HI  = $01
ZP_TMP = $02

* = $E000

Reset:
    cld
    sei

    jsr VdpInit
    jsr PsgInit
    jsr Play
    bra *

Irq:
    rti

Nmi:
    rti

VdpRegs:
    .byt $00
    .byt $D0
    .byt VDP_VRAM_ADDR_NAME >> 10
    .byt $00
    .byt VDP_VRAM_ADDR_PAT >> 11
    .byt $00
    .byt $00
    .byt $F5
VdpRegsEnd:

VdpInit:
    ldx #(VdpRegsEnd - VdpRegs - 1)
@Regs:
    lda VdpRegs,X
    sta VDP_PORT1
    txa
    ora #VDP_CMD_REG
    sta VDP_PORT1
    dex
    bpl @Regs
    rts

PsgInit:
    lda #(PSG_CMD | PSG_CH0 | PSG_VOL | PSG_VOL_OFF)
    sta PSG_PORT
    lda #(PSG_CMD | PSG_CH1 | PSG_VOL | PSG_VOL_OFF)
    sta PSG_PORT
    lda #(PSG_CMD | PSG_CH2 | PSG_VOL | PSG_VOL_OFF)
    sta PSG_PORT
    lda #(PSG_CMD | PSG_CH3 | PSG_VOL | PSG_VOL_OFF)
    sta PSG_PORT
    rts

Play:
    ldx #0
@Loop:
    lda Melody,X
    sta ZP_LO
    lda Melody+1,X
    sta ZP_HI
    ora ZP_LO
    beq @Done

    lda ZP_LO
    and #$0F
    ora #(PSG_CMD | PSG_CH0 | PSG_TONE)
    sta PSG_PORT

    lda ZP_LO
    lsr
    lsr
    lsr
    lsr
    sta ZP_TMP
    lda ZP_HI
    asl
    asl
    asl
    asl
    ora ZP_TMP
    and #$3F
    sta PSG_PORT

    lda #(PSG_CMD | PSG_CH0 | PSG_VOL | PSG_VOL_MAX)
    sta PSG_PORT

    jsr Delay

    inx
    inx
    bra @Loop
@Done:
    lda #(PSG_CMD | PSG_CH0 | PSG_VOL | PSG_VOL_OFF)
    sta PSG_PORT
    jsr Delay
    bra Play

Delay:
    phx
    phy
    ldy #$C0
@Outer:
    ldx #$FF
@Inner:
    dex
    bne @Inner
    dey
    bne @Outer
    ply
    plx
    rts

Melody:
    .word NOTE_C4, NOTE_D4, NOTE_E4, NOTE_F4
    .word NOTE_G4, NOTE_A4, NOTE_B4, NOTE_C5
    .word NOTE_C5, NOTE_B4, NOTE_A4, NOTE_G4
    .word NOTE_F4, NOTE_E4, NOTE_D4, NOTE_C4
    .word 0

.dsb $FFFA - *, $00
.word Nmi
.word Reset
.word Irq
