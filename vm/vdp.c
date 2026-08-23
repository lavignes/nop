#include <assert.h>

#include "vm.h"

enum {
  LINES_NTSC = 262,

  LINES_ACTIVE = 192,
  LINES_BORDER_BOTTOM = 24,
  LINES_VBLANK = 19,
  LINES_BORDER_TOP = 27,
};

enum {
  COLS_NTSC = 342,

  COLS_GFX_ACTIVE = 256,
  COLS_GFX_BORDER_RIGHT = 15,
  COLS_GFX_HBLANK = 58,
  COLS_GFX_BORDER_LEFT = 13,

  COLS_TXT_ACTIVE = 240,
  COLS_TXT_BORDER_RIGHT = 25,
  COLS_TXT_HBLANK = 58,
  COLS_TXT_BORDER_LEFT = 19,
};

enum {
  STAT_SPRITE_TRUNC_INDEX_MASK = 0x1F,
  STAT_SPRITE_OVERLAP = 1 << 5,
  STAT_SPRITE_TRUNC = 1 << 6,
  STAT_INTERRUPT = 1 << 7,
};

enum {
  REG0_EXTVID = 1 << 0,
  REG0_MODE2 = 1 << 1,
};

enum {
  REG1_SPRITE_MAG = 1 << 0,
  REG1_SPRITE_16X16 = 1 << 1,
  REG1_MODE3 = 1 << 3,
  REG1_MODE1 = 1 << 4,
  REG1_INT_ENABLE = 1 << 5,
  REG1_BLANK = 1 << 6,
  REG1_VRAM_16K = 1 << 7,
};

#define COLOR(R, G, B, A) (((B) << 24) | ((G) << 16) | ((R) << 8) | (A))

U32 COLORS[16] = {
    COLOR(0, 0, 0, 255),       // Transparent
    COLOR(0, 0, 0, 255),       // Black
    COLOR(33, 200, 66, 255),   // Medium Green
    COLOR(94, 220, 120, 255),  // Light Green
    COLOR(84, 85, 237, 255),   // Dark Blue
    COLOR(125, 118, 252, 255), // Light Blue
    COLOR(212, 82, 77, 255),   // Dark Red
    COLOR(66, 235, 245, 255),  // Cyan
    COLOR(252, 85, 84, 255),   // Medium Red
    COLOR(255, 121, 120, 255), // Light Red
    COLOR(212, 193, 84, 255),  // Dark Yellow
    COLOR(230, 206, 128, 255), // Light Yellow
    COLOR(33, 176, 59, 255),   // Dark Green
    COLOR(201, 91, 186, 255),  // Magenta
    COLOR(204, 204, 204, 255), // Gray
    COLOR(255, 255, 255, 255), // White
};

void vdpReset(Vdp *vdp, Emu *emu) {
  assert(LINES_NTSC == (LINES_ACTIVE + LINES_BORDER_BOTTOM + LINES_VBLANK +
                        LINES_BORDER_TOP));
  assert(COLS_NTSC == (COLS_GFX_ACTIVE + COLS_GFX_BORDER_RIGHT +
                       COLS_GFX_HBLANK + COLS_GFX_BORDER_LEFT));
  assert(COLS_NTSC == (COLS_TXT_ACTIVE + COLS_TXT_BORDER_RIGHT +
                       COLS_TXT_HBLANK + COLS_TXT_BORDER_LEFT));
  assert(SCREEN_WIDTH ==
         (COLS_GFX_BORDER_LEFT + COLS_GFX_ACTIVE + COLS_GFX_BORDER_RIGHT));
  assert(SCREEN_WIDTH ==
         (COLS_TXT_BORDER_LEFT + COLS_TXT_ACTIVE + COLS_TXT_BORDER_RIGHT));
  assert(SCREEN_HEIGHT ==
         (LINES_BORDER_TOP + LINES_ACTIVE + LINES_BORDER_BOTTOM));
}

static void mode0DrawLine(Vdp *vdp, Emu *emu) {
  for (UInt i = 0; i < COLS_GFX_BORDER_LEFT; ++i) {
    vdp->pix[vdp->line][i] = COLORS[vdp->reg[7] & 0x0F];
  }
  for (UInt i = COLS_GFX_BORDER_LEFT;
       i < (COLS_GFX_BORDER_LEFT + COLS_GFX_ACTIVE); ++i) {
    vdp->pix[vdp->line][i] = COLORS[0x0F];
  }
  for (UInt i = (COLS_GFX_BORDER_LEFT + COLS_GFX_ACTIVE);
       i < (COLS_GFX_BORDER_LEFT + COLS_GFX_ACTIVE + COLS_GFX_BORDER_RIGHT);
       ++i) {
    vdp->pix[vdp->line][i] = COLORS[vdp->reg[7] & 0x0F];
  }
}

static void mode0Tick(Vdp *vdp, Emu *emu) {
  if (vdp->line < LINES_BORDER_TOP) {
    if (vdp->col <
        (COLS_GFX_BORDER_LEFT + COLS_GFX_ACTIVE + COLS_GFX_BORDER_RIGHT)) {
      vdp->pix[vdp->line][vdp->col] = COLORS[vdp->reg[7] & 0x0F];
    }
  } else if (vdp->line < (LINES_BORDER_TOP + LINES_ACTIVE)) {
    if (vdp->col ==
        (COLS_GFX_BORDER_LEFT + COLS_GFX_ACTIVE + COLS_GFX_BORDER_RIGHT)) {
      mode0DrawLine(vdp, emu);
    }
  } else if (vdp->line <
             (LINES_BORDER_TOP + LINES_ACTIVE + LINES_BORDER_BOTTOM)) {
    if (vdp->col <
        (COLS_GFX_BORDER_LEFT + COLS_GFX_ACTIVE + COLS_GFX_BORDER_RIGHT)) {
      vdp->pix[vdp->line][vdp->col] = COLORS[vdp->reg[7] & 0x0F];
    }
  }
}

static void mode1Tick(Vdp *vdp, Emu *emu) {}

Bool vdpTick(Vdp *vdp, Emu *emu) {
  U8 mode = ((vdp->reg[1] & REG1_MODE3) >> 1) | (vdp->reg[0] & REG0_MODE2) |
            ((vdp->reg[1] & REG1_MODE1) >> 4);
  switch (mode) {
  case 0:
    mode0Tick(vdp, emu);
    break;
  case 1:
    mode1Tick(vdp, emu);
    break;
  default:
    // TODO: Mode 2
    mode0Tick(vdp, emu);
    break;
  }
  ++vdp->col;
  if (vdp->col == COLS_NTSC) {
    vdp->col = 0;
    ++vdp->line;
    if (vdp->line == LINES_NTSC) {
      vdp->line = 0;
    }
  }
  if ((vdp->col == 1) && (vdp->line == (LINES_BORDER_TOP + LINES_ACTIVE))) {
    vdp->stat |= STAT_INTERRUPT;
    emu->nmi = TRUE;
  }
  return (vdp->line == (LINES_BORDER_TOP + LINES_ACTIVE + LINES_BORDER_BOTTOM));
}
