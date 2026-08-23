#ifndef VM_H
#define VM_H

#include <histedit.h>

#include "abi.h"

enum {
  CPU_FLAG_CARRY = 1 << 0,
  CPU_FLAG_ZERO = 1 << 1,
  CPU_FLAG_INTERRUPT = 1 << 2,
  CPU_FLAG_DECIMAL = 1 << 3,
  CPU_FLAG_BREAK = 1 << 4,
  CPU_FLAG_UNUSED = 1 << 5,
  CPU_FLAG_OVERFLOW = 1 << 6,
  CPU_FLAG_NEGATIVE = 1 << 7,
};

enum {
  RAM_SIZE = 0xC000,
  RAM_START_ADDR = 0x0000,
  RAM_END_ADDR = RAM_START_ADDR + RAM_SIZE - 1,

  IO_SIZE = 0x2000,
  IO_START_ADDR = 0xC000,
  IO_END_ADDR = IO_START_ADDR + IO_SIZE - 1,

  ROM_SIZE = 0x2000,
  ROM_START_ADDR = 0xE000,
  ROM_END_ADDR = ROM_START_ADDR + ROM_SIZE - 1,

  VRAM_SIZE = 0x4000,
};

enum {
  SCREEN_WIDTH = 256 + 13 + 15,
  SCREEN_HEIGHT = 192 + 27 + 24,
};

enum {
  CPU_HZ = 1000000,
  VDP_HZ = 10738635 / 2,
};

typedef struct {
  U8 a;
  U8 x;
  U8 y;
  U8 sp;
  U8 p;
  U16 pc;

  Bool nmi;
  Bool irq;
  Bool wai;
  Bool stp;

  void *ea;
  U16 eaAddr;
} Cpu;

typedef struct {
  U8 cmd[2];
  Bool cmdIdx;
  U16 addr;
  U8 buf;
  U16 line;
  U16 col;
  U32 pix[SCREEN_HEIGHT][SCREEN_WIDTH];
  U8 sprStat[SCREEN_WIDTH];

  U8 stat;
  U8 reg[8];
  U8 vram[VRAM_SIZE];
} Vdp;

typedef struct Breakpoint Breakpoint;
struct Breakpoint {
  Breakpoint *next;
  U16 num;
  U16 addr;
};

typedef struct {
  char const *start;
  UInt len;
  UInt type;
  Int val;
} Tok;

typedef struct {
  UInt kind;
  Tok tok;
  Bool unary;
} Expr;

typedef struct Symbol Symbol;

struct Symbol {
  Symbol *next;
  char const *name;
  Int val;
};

typedef struct {
  Bool debug;
  U16 bpCount;
  Breakpoint *bpHead;
  Breakpoint nextpoint;

  U16 symCount;
  Symbol *symHead;

  Tok tokStash;

  Expr opStack[64];
  Expr exprStack[64];
  Int intStack[64];
  UInt opCount;
  UInt exprCount;
  UInt intCount;

  EditLine *el;
  History *hist;
  HistEvent ev;
  char *prevline;
  char *workline;
} Dbg;

typedef struct {
  Bool nmi;
  Dbg dbg;
  Cpu cpu;
  Vdp vdp;
  U8 ram[RAM_SIZE];
  U8 rom[ROM_SIZE];
} Emu;

U8 emuRead(Emu const *emu, U16 addr);

void cpuReset(Cpu *cpu, Emu *emu);
UInt cpuTick(Cpu *cpu, Emu *emu);

void vdpReset(Vdp *vdp, Emu *emu, Bool random);
Bool vdpTick(Vdp *vdp, Emu *emu);
U8 vdpRead(Vdp *vdp, U16 addr);
void vdpWrite(Vdp *vdp, U16 addr, U8 val);

Symbol const *symValFind(Dbg const *dbg, Int val);
Symbol const *symFind(Dbg const *dbg, char const *name, UInt namelen);
void symLoad(Dbg *dbg, char const *filename);

void dbgTick(Dbg *dbg, Emu *emu);
U16 disAsm(Emu const *emu, U16 addr);

void termRawModeOn();
void termRawModeOff();

#ifdef BUS_MOCK
U8 busRead(Emu *emu, U16 addr);
void busWrite(Emu *emu, U16 addr, U8 val);
#else
static inline U8 busRead(Emu *emu, U16 addr) {
  switch (addr) {
  case RAM_START_ADDR ... RAM_END_ADDR:
    return emu->ram[addr - RAM_START_ADDR];
  case IO_START_ADDR ... IO_END_ADDR:
    return vdpRead(&emu->vdp, addr - IO_START_ADDR);
  case ROM_START_ADDR ... ROM_END_ADDR:
    return emu->rom[addr - ROM_START_ADDR];
  default:
    return 0;
  }
}

static inline void busWrite(Emu *emu, U16 addr, U8 val) {
  switch (addr) {
  case RAM_START_ADDR ... RAM_END_ADDR:
    emu->ram[addr - RAM_START_ADDR] = val;
    break;
  case IO_START_ADDR ... IO_END_ADDR:
    vdpWrite(&emu->vdp, addr - IO_START_ADDR, val);
    break;
  case ROM_START_ADDR ... ROM_END_ADDR:
    break;
  default:
    break;
  }
}
#endif // BUS_MOCK

#endif // VM_H
