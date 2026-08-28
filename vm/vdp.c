// A little TMS9918A emulator

#include <assert.h>
#include <stdlib.h>
#include <string.h>

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
  GFX_CHARS_PER_ROW = 32,
  GFX_CHARS_ROWS = 24,

  GFX_CHAR_WIDTH = 8,
  GFX_CHAR_HEIGHT = 8,
};

enum {
  TXT_CHARS_PER_ROW = 40,
  TXT_CHARS_ROWS = 24,

  TXT_CHAR_WIDTH = 6,
  TXT_CHAR_HEIGHT = 8,
};

enum {
  SPRITE_OVERLAP = 1 << 0,
  SPRITE_PRIO = 1 << 1,

  SPRITE_MAX_PER_ROW = 4,
  SPRITE_TERMINATOR = 0xD0,
};

enum {
  STAT_SPRITE_TRUNC_INDEX_MASK = 0x1F,
  STAT_SPRITE_OVERLAP = 1 << 5,
  STAT_SPRITE_TRUNC = 1 << 6,
  STAT_INTERRUPT = 1 << 7,
};

enum { CMD_REG = 1 << 7, CMD_WRITE = 1 << 6 };

enum {
  REG0_MODE2 = 1 << 1,
};

enum {
  REG1_SPRITE_MAG = 1 << 0,
  REG1_SPRITE_16X16 = 1 << 1,

  REG1_MODE3 = 1 << 3,
  REG1_MODE1 = 1 << 4,
  REG1_INT_ENABLE = 1 << 5,
  REG1_ENABLE = 1 << 6,
  REG1_VRAM_16K = 1 << 7,
};

enum {
  REG3_MODE2_COLOR = 1 << 7,
};

enum {
  REG4_MODE2_PAT = 1 << 2,
};

#define COLOR(R, G, B, A) (((R) << 24) | ((G) << 16) | ((B) << 8) | (A))

U32 COLORS[16] = {
    COLOR(0, 0, 0, 255),       // 0 Transparent
    COLOR(0, 0, 0, 255),       // 1 Black
    COLOR(33, 200, 66, 255),   // 2 Medium Green
    COLOR(94, 220, 120, 255),  // 3 Light Green
    COLOR(84, 85, 237, 255),   // 4 Dark Blue
    COLOR(125, 118, 252, 255), // 5 Light Blue
    COLOR(212, 82, 77, 255),   // 6 Dark Red
    COLOR(66, 235, 245, 255),  // 7 Cyan
    COLOR(252, 85, 84, 255),   // 8 Medium Red
    COLOR(255, 121, 120, 255), // 9 Light Red
    COLOR(212, 193, 84, 255),  // A Dark Yellow
    COLOR(230, 206, 128, 255), // B Light Yellow
    COLOR(33, 176, 59, 255),   // C Dark Green
    COLOR(201, 91, 186, 255),  // D Magenta
    COLOR(204, 204, 204, 255), // E Gray
    COLOR(255, 255, 255, 255), // F White
};

void vdpReset(Vdp *vdp, Emu *emu, Bool random) {
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

  if (random) {
    for (UInt i = 0; i < sizeof(vdp->vram); ++i) {
      vdp->vram[i] = (U8)rand();
    }
    for (UInt i = 0; i < sizeof(vdp->reg); ++i) {
      vdp->reg[i] = (U8)rand();
    }
    vdp->addr = (U16)rand();
    vdp->buf = (U8)rand();
  }
}

static void mode1DrawActiveLine(Vdp *vdp, Emu const *emu) {
  U32 bdColor = COLORS[vdp->reg[7] & 0x0F];
  U32 txtColor = COLORS[vdp->reg[7] >> 4];
  U32 colors[2] = {bdColor, txtColor};
  for (UInt i = 0; i < COLS_TXT_BORDER_LEFT; ++i) {
    vdp->pix[vdp->line][i] = bdColor;
  }
  U16 nameBase = ((U16)(vdp->reg[2] & 0x0F)) << 10;
  U16 colorBase = ((U16)vdp->reg[3]) << 6;
  U16 patBase = ((U16)(vdp->reg[4] & 0x07)) << 11;
  U16 ny = (vdp->line - LINES_BORDER_TOP) / TXT_CHAR_HEIGHT;
  U16 cy = (vdp->line - LINES_BORDER_TOP) % TXT_CHAR_HEIGHT;
  for (U16 nx = 0; nx < TXT_CHARS_PER_ROW; ++nx) {
    U8 ch = vdp->vram[nameBase + (ny * TXT_CHARS_PER_ROW) + nx];
    U8 pat = vdp->vram[patBase + (ch * TXT_CHAR_HEIGHT) + cy];
    for (UInt i = 0; i < TXT_CHAR_WIDTH; ++i) {
      vdp->pix[vdp->line][COLS_TXT_BORDER_LEFT + (nx * TXT_CHAR_WIDTH) + i] =
          colors[(pat & (0x80 >> i)) ? 1 : 0];
    }
  }
  for (UInt i = (COLS_TXT_BORDER_LEFT + COLS_TXT_ACTIVE);
       i < (COLS_TXT_BORDER_LEFT + COLS_TXT_ACTIVE + COLS_TXT_BORDER_RIGHT);
       ++i) {
    vdp->pix[vdp->line][i] = bdColor;
  }
}

static void mode1Tick(Vdp *vdp, Emu const *emu) {
  U32 bdColor = COLORS[vdp->reg[7] & 0x0F];
  if (vdp->line < LINES_BORDER_TOP) {
    if (vdp->col <
        (COLS_TXT_BORDER_LEFT + COLS_TXT_ACTIVE + COLS_TXT_BORDER_RIGHT)) {
      vdp->pix[vdp->line][vdp->col] = bdColor;
    }
  } else if (vdp->line < (LINES_BORDER_TOP + LINES_ACTIVE)) {
    if (vdp->col ==
        (COLS_TXT_BORDER_LEFT + COLS_TXT_ACTIVE + COLS_TXT_BORDER_RIGHT)) {
      mode1DrawActiveLine(vdp, emu);
    }
  } else if (vdp->line <
             (LINES_BORDER_TOP + LINES_ACTIVE + LINES_BORDER_BOTTOM)) {
    if (vdp->col <
        (COLS_TXT_BORDER_LEFT + COLS_TXT_ACTIVE + COLS_TXT_BORDER_RIGHT)) {
      vdp->pix[vdp->line][vdp->col] = bdColor;
    }
  }
}

static void drawSpriteLine(Vdp *vdp, Emu const *emu) {
  memset(vdp->sprStat, 0, sizeof(vdp->sprStat));
  U16 attrBase = ((U16)(vdp->reg[5] & 0x7F)) << 7;
  U16 patBase = ((U16)(vdp->reg[6] & 0x07)) << 11;
  UInt lenBase = (vdp->reg[1] & REG1_SPRITE_16X16) ? 16 : 8;
  UInt scale = (vdp->reg[1] & REG1_SPRITE_MAG) ? 2 : 1;
  UInt len = lenBase * scale;
  Int ly = (Int)vdp->line - LINES_BORDER_TOP;
  UInt sprCount = 0;
  for (UInt sn = 0; sn < 32; ++sn) {
    U16 attrIdx = attrBase + (sn * 4);
    U8 sy = vdp->vram[attrIdx];
    if (sy == SPRITE_TERMINATOR) {
      break;
    }
    UInt row = ((UInt)(ly - sy - 1)) & 0xFF;
    if (row >= len) {
      continue;
    }
    if (sprCount == SPRITE_MAX_PER_ROW) {
      if ((vdp->stat & STAT_SPRITE_TRUNC) == 0) {
        vdp->stat = (vdp->stat & ~STAT_SPRITE_TRUNC_INDEX_MASK) |
                    STAT_SPRITE_TRUNC | sn;
      }
      break;
    }
    ++sprCount;
    U8 sx = vdp->vram[attrIdx + 1];
    U8 patNum = vdp->vram[attrIdx + 2];
    U8 attr = vdp->vram[attrIdx + 3];
    Int x = (Int)sx - ((attr & 0x80) ? 32 : 0);
    U8 color = attr & 0x0F;
    UInt patRow = row / scale;
    if (lenBase == 16) {
      patNum &= 0xFC;
    }
    for (UInt i = 0; i < len; ++i) {
      UInt col = i / scale;
      U8 pat =
          vdp->vram[patBase + (patNum * 8) + ((col >= 8) ? 16 : 0) + patRow];
      if ((pat & (0x80 >> (col & 0x07))) == 0) {
        continue;
      }
      Int ax = x + (Int)i;
      if ((ax < 0) || (ax >= COLS_GFX_ACTIVE)) {
        continue;
      }
      UInt dst = COLS_GFX_BORDER_LEFT + ax;
      if (vdp->sprStat[dst] & SPRITE_OVERLAP) {
        vdp->stat |= STAT_SPRITE_OVERLAP;
      }
      vdp->sprStat[dst] |= SPRITE_OVERLAP;
      if (color && !(vdp->sprStat[dst] & SPRITE_PRIO)) {
        vdp->pix[vdp->line][dst] = COLORS[color];
        vdp->sprStat[dst] |= SPRITE_PRIO;
      }
    }
  }
}

static void mode0DrawActiveLine(Vdp *vdp, Emu const *emu) {
  U32 bdColor = COLORS[vdp->reg[7] & 0x0F];
  for (UInt i = 0; i < COLS_GFX_BORDER_LEFT; ++i) {
    vdp->pix[vdp->line][i] = bdColor;
  }
  U16 nameBase = ((U16)(vdp->reg[2] & 0x0F)) << 10;
  U16 colorBase = ((U16)vdp->reg[3]) << 6;
  U16 patBase = ((U16)(vdp->reg[4] & 0x07)) << 11;
  U16 ny = (vdp->line - LINES_BORDER_TOP) / GFX_CHAR_HEIGHT;
  U16 cy = (vdp->line - LINES_BORDER_TOP) % GFX_CHAR_HEIGHT;
  for (U16 nx = 0; nx < GFX_CHARS_PER_ROW; ++nx) {
    U8 ch = vdp->vram[nameBase + (ny * GFX_CHARS_PER_ROW) + nx];
    U8 pat = vdp->vram[patBase + (ch * GFX_CHAR_HEIGHT) + cy];
    U8 color = vdp->vram[colorBase + (ch >> 3)];
    U32 colors[2] = {(color >> 4) ? COLORS[color >> 4] : bdColor,
                     (color & 0x0F) ? COLORS[color & 0x0F] : bdColor};
    for (UInt i = 0; i < GFX_CHAR_WIDTH; ++i) {
      vdp->pix[vdp->line][COLS_GFX_BORDER_LEFT + (nx * GFX_CHAR_WIDTH) + i] =
          colors[(pat & (0x80 >> i)) ? 1 : 0];
    }
  }
  for (UInt i = (COLS_GFX_BORDER_LEFT + COLS_GFX_ACTIVE);
       i < (COLS_GFX_BORDER_LEFT + COLS_GFX_ACTIVE + COLS_GFX_BORDER_RIGHT);
       ++i) {
    vdp->pix[vdp->line][i] = bdColor;
  }
  drawSpriteLine(vdp, emu);
}

static void mode0Tick(Vdp *vdp, Emu const *emu) {
  U32 bdColor = COLORS[vdp->reg[7] & 0x0F];
  if (vdp->line < LINES_BORDER_TOP) {
    if (vdp->col <
        (COLS_GFX_BORDER_LEFT + COLS_GFX_ACTIVE + COLS_GFX_BORDER_RIGHT)) {
      vdp->pix[vdp->line][vdp->col] = bdColor;
    }
  } else if (vdp->line < (LINES_BORDER_TOP + LINES_ACTIVE)) {
    if (vdp->col ==
        (COLS_GFX_BORDER_LEFT + COLS_GFX_ACTIVE + COLS_GFX_BORDER_RIGHT)) {
      mode0DrawActiveLine(vdp, emu);
    }
  } else if (vdp->line <
             (LINES_BORDER_TOP + LINES_ACTIVE + LINES_BORDER_BOTTOM)) {
    if (vdp->col <
        (COLS_GFX_BORDER_LEFT + COLS_GFX_ACTIVE + COLS_GFX_BORDER_RIGHT)) {
      vdp->pix[vdp->line][vdp->col] = bdColor;
    }
  }
}

static void mode3DrawActiveLine(Vdp *vdp, Emu const *emu) {
  U32 bdColor = COLORS[vdp->reg[7] & 0x0F];
  for (UInt i = 0; i < COLS_GFX_BORDER_LEFT; ++i) {
    vdp->pix[vdp->line][i] = bdColor;
  }
  U16 nameBase = ((U16)(vdp->reg[2] & 0x0F)) << 10;
  U16 colorBase = (vdp->reg[3] & REG3_MODE2_COLOR) ? 0x2000 : 0x0000;
  U16 patBase = (vdp->reg[4] & REG4_MODE2_PAT) ? 0x2000 : 0x0000;
  U16 ny = (vdp->line - LINES_BORDER_TOP) / GFX_CHAR_HEIGHT;
  U16 cy = (vdp->line - LINES_BORDER_TOP) % GFX_CHAR_HEIGHT;
  U16 chBase = ((ny / 8) & vdp->reg[4]) << 8;
  for (U16 nx = 0; nx < GFX_CHARS_PER_ROW; ++nx) {
    U16 ch = chBase + vdp->vram[nameBase + (ny * GFX_CHARS_PER_ROW) + nx];
    U8 pat = vdp->vram[patBase + (ch * GFX_CHAR_HEIGHT) + cy];
    U8 color = vdp->vram[colorBase + (ch * GFX_CHAR_HEIGHT) + cy];
    U32 colors[2] = {(color >> 4) ? COLORS[color >> 4] : bdColor,
                     (color & 0x0F) ? COLORS[color & 0x0F] : bdColor};
    for (UInt i = 0; i < GFX_CHAR_WIDTH; ++i) {
      vdp->pix[vdp->line][COLS_GFX_BORDER_LEFT + (nx * GFX_CHAR_WIDTH) + i] =
          colors[(pat & (0x80 >> i)) ? 1 : 0];
    }
  }
  for (UInt i = (COLS_GFX_BORDER_LEFT + COLS_GFX_ACTIVE);
       i < (COLS_GFX_BORDER_LEFT + COLS_GFX_ACTIVE + COLS_GFX_BORDER_RIGHT);
       ++i) {
    vdp->pix[vdp->line][i] = bdColor;
  }
  drawSpriteLine(vdp, emu);
}

static void mode3Tick(Vdp *vdp, Emu const *emu) {
  U32 bdColor = COLORS[vdp->reg[7] & 0x0F];
  if (vdp->line < LINES_BORDER_TOP) {
    if (vdp->col <
        (COLS_GFX_BORDER_LEFT + COLS_GFX_ACTIVE + COLS_GFX_BORDER_RIGHT)) {
      vdp->pix[vdp->line][vdp->col] = bdColor;
    }
  } else if (vdp->line < (LINES_BORDER_TOP + LINES_ACTIVE)) {
    if (vdp->col ==
        (COLS_GFX_BORDER_LEFT + COLS_GFX_ACTIVE + COLS_GFX_BORDER_RIGHT)) {
      mode3DrawActiveLine(vdp, emu);
    }
  } else if (vdp->line <
             (LINES_BORDER_TOP + LINES_ACTIVE + LINES_BORDER_BOTTOM)) {
    if (vdp->col <
        (COLS_GFX_BORDER_LEFT + COLS_GFX_ACTIVE + COLS_GFX_BORDER_RIGHT)) {
      vdp->pix[vdp->line][vdp->col] = bdColor;
    }
  }
}

static void blankTick(Vdp *vdp) {
  if (vdp->line < (LINES_BORDER_TOP + LINES_ACTIVE + LINES_BORDER_BOTTOM)) {
    if (vdp->col <
        (COLS_GFX_BORDER_LEFT + COLS_GFX_ACTIVE + COLS_GFX_BORDER_RIGHT)) {
      vdp->pix[vdp->line][vdp->col] = COLORS[vdp->reg[7] & 0x0F];
    }
  }
}

Bool vdpTick(Vdp *vdp, Emu *emu) {
  if ((vdp->col == 0) && (vdp->line == LINES_BORDER_TOP)) {
    vdp->stat &= ~(STAT_SPRITE_TRUNC_INDEX_MASK | STAT_SPRITE_OVERLAP |
                   STAT_SPRITE_TRUNC);
  }
  if (vdp->reg[1] & REG1_ENABLE) {
    if (vdp->reg[1] & REG1_MODE3) {
      mode3Tick(vdp, emu);
    } else if (vdp->reg[1] & REG1_MODE1) {
      mode1Tick(vdp, emu);
    } else {
      mode0Tick(vdp, emu);
    }
  } else {
    blankTick(vdp);
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
    if (((vdp->stat & STAT_INTERRUPT) == 0) &&
        (vdp->reg[1] & REG1_INT_ENABLE)) {
      emu->nmi = TRUE;
    }
    vdp->stat |= STAT_INTERRUPT;
  }
  return (vdp->col == 0) &&
         (vdp->line == (LINES_BORDER_TOP + LINES_ACTIVE + LINES_BORDER_BOTTOM));
}

U8 vdpRead(Vdp *vdp, U16 addr) {
  if (addr & 0x1) {
    vdp->cmdIdx = FALSE;
    U8 stat = vdp->stat;
    vdp->stat &= ~(STAT_INTERRUPT | STAT_SPRITE_OVERLAP);
    return stat;
  }
  U8 val = vdp->buf;
  vdp->buf = vdp->vram[vdp->addr];
  ++vdp->addr;
  if (vdp->addr == sizeof(vdp->vram)) {
    vdp->addr = 0;
  }
  return val;
}

void vdpWrite(Vdp *vdp, U16 addr, U8 val) {
  if (addr & 0x1) {
    vdp->cmd[vdp->cmdIdx] = val;
    if (vdp->cmdIdx) {
      vdp->cmdIdx = FALSE;
      if (vdp->cmd[1] & CMD_REG) {
        vdp->reg[vdp->cmd[1] & 0x07] = vdp->cmd[0];
        return;
      }
      vdp->addr = (((U16)(vdp->cmd[1] & 0x3F)) << 8) | vdp->cmd[0];
      if ((vdp->cmd[1] & CMD_WRITE) == 0) {
        vdp->buf = vdp->vram[vdp->addr];
        ++vdp->addr;
        if (vdp->addr == sizeof(vdp->vram)) {
          vdp->addr = 0;
        }
      }
      return;
    }
    vdp->cmdIdx = TRUE;
    return;
  }
  vdp->buf = val;
  vdp->vram[vdp->addr] = val;
  ++vdp->addr;
  if (vdp->addr == sizeof(vdp->vram)) {
    vdp->addr = 0;
  }
}
