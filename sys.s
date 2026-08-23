; vim: ft=a65

* = $E000

Reset:
    cld
    cli
    jsr VdpInitMin
@Halt:
    jmp @Halt

Nmi:
    pha
    txa
    pha
    lda VDP_PORT1
    ldx #$00
@Move:
    inc SPRITE_SHADOW + 1, X
    txa
    clc
    adc #$04
    tax
    cpx #(SPRITE_COUNT * 4)
    bne @Move
    jsr VdpWriteSprites
    pla
    tax
    pla
    rti

Irq:
    rti

VDP_TRANSPARENT  = $0
VDP_BLACK        = $1
VDP_MEDIUM_GREEN = $2
VDP_LIGHT_GREEN  = $3
VDP_DARK_BLUE    = $4
VDP_LIGHT_BLUE   = $5
VDP_DARK_RED     = $6
VDP_CYAN         = $7
VDP_MEDIUM_RED   = $8
VDP_LIGHT_RED    = $9
VDP_DARK_YELLOW  = $A
VDP_LIGHT_YELLOW = $B
VDP_DARK_GREEN   = $C
VDP_MAGENTA      = $D
VDP_GRAY         = $E
VDP_WHITE        = $F

#define VDP_COLOR(F, B) (((F) << 4) | (B))
#define VDP_CMD_REG(R) ($80 | ((R) & $07))
VDP_CMD_WRITE = $40

VDP_PORT0 = $C000
VDP_PORT1 = $C001

SPRITE_SHADOW = $0200
SPRITE_COUNT  = 32

VdpInitRegs:
    .byt $00
    .byt $E1
    .byt $05
    .byt $80
    .byt $01
    .byt $20
    .byt $00
    .byt VDP_COLOR(VDP_TRANSPARENT, VDP_BLACK)

VdpStar:
    .byt $00, $44, $6C, $38, $7C, $FE, $10, $10

VdpBall:
    .byt $18, $3C, $7E, $FF, $FF, $7E, $3C, $18

VdpSprites:
    .byt $30, $40, $00, VDP_WHITE
    .byt $50, $60, $00, VDP_CYAN
    .byt $70, $80, $00, VDP_MEDIUM_RED
    .byt $34, $44, $00, VDP_LIGHT_YELLOW
    .byt $D0
VdpSpritesEnd:

VdpInitMin:
    lda $00           ; random RAM byte (randomized at startup with -r)
    and #$1B          ; keep mode (M1/M3) + sprite size/mag bits
    ora #$E0          ; force display on + interrupts + 16K
    sta VDP_PORT1
    lda #VDP_CMD_REG(1)
    sta VDP_PORT1
    lda #$20
    sta VDP_PORT1
    lda #VDP_CMD_REG(5)
    sta VDP_PORT1
    rts

VdpInit:
    lda VDP_PORT1
    ldx #$07
@Loop:
    lda VdpInitRegs, X
    sta VDP_PORT1
    txa
    ora #VDP_CMD_REG(0)
    sta VDP_PORT1
    dex
    bpl @Loop

    lda #$00
    sta VDP_PORT1
    lda #(VDP_CMD_WRITE | $08)
    sta VDP_PORT1
    ldx #$07
@Loop2:
    lda VdpStar, X
    sta VDP_PORT0
    dex
    bpl @Loop2

    lda #$00
    sta VDP_PORT1
    lda #(VDP_CMD_WRITE | $14)
    sta VDP_PORT1
    lda #$00
    sta VDP_PORT0

    lda #$00
    sta VDP_PORT1
    lda #(VDP_CMD_WRITE | $00)
    sta VDP_PORT1
    ldx #$07
@Loop3:
    lda VdpBall, X
    sta VDP_PORT0
    dex
    bpl @Loop3

    ldx #$00
@Loop4:
    lda VdpSprites, X
    sta SPRITE_SHADOW, X
    inx
    cpx #(VdpSpritesEnd - VdpSprites)
    bne @Loop4
    jsr VdpWriteSprites

    rts

VdpWriteSprites:
    lda #$00
    sta VDP_PORT1
    lda #(VDP_CMD_WRITE | $10)
    sta VDP_PORT1
    ldx #$00
@Loop:
    lda SPRITE_SHADOW, X
    sta VDP_PORT0
    inx
    cpx #(SPRITE_COUNT * 4)
    bne @Loop
    rts

.dsb $FFFA - *, $00
Vectors:
    .word Nmi
    .word Reset
    .word Irq
