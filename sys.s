; vim: ft=a65

#include "vdp.inc"

* = $E000

Reset:
    cld
    sei

    jsr VdpInit
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

.dsb $FFFA - *, $00
.word Nmi
.word Reset
.word Irq
