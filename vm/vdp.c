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

void vdpReset(Vdp *vdp, Emu *emu) {
  assert(LINES_NTSC == (LINES_ACTIVE + LINES_BORDER_BOTTOM + LINES_VBLANK +
                        LINES_BORDER_TOP));
  assert(COLS_NTSC == (COLS_GFX_ACTIVE + COLS_GFX_BORDER_RIGHT +
                       COLS_GFX_HBLANK + COLS_GFX_BORDER_LEFT));
  assert(COLS_NTSC == (COLS_TXT_ACTIVE + COLS_TXT_BORDER_RIGHT +
                       COLS_TXT_HBLANK + COLS_TXT_BORDER_LEFT));
}

void vdpTick(Vdp *vdp, Emu *emu, UInt cycles) {}
