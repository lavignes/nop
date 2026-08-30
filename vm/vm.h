#ifndef VM_H
#define VM_H

#include <histedit.h>
#include <stdio.h>

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
  RAMLO_SIZE = 0x8000,
  RAMLO_START_ADDR = 0x0000,
  RAMLO_END_ADDR = RAMLO_START_ADDR + RAMLO_SIZE - 1,

  RAMHI_SIZE = 0x4000,
  RAMHI_START_ADDR = 0x8000,
  RAMHI_END_ADDR = RAMHI_START_ADDR + RAMHI_SIZE - 1,

  IO_SIZE = 0x1000,
  IO_START_ADDR = 0xC000,
  IO_END_ADDR = IO_START_ADDR + IO_SIZE - 1,

  IO_DEV_SIZE = 0x100,

  VDP_START_ADDR = 0xC000,
  VDP_END_ADDR = VDP_START_ADDR + IO_DEV_SIZE - 1,

  VIA_START_ADDR = 0xC100,
  VIA_END_ADDR = VIA_START_ADDR + IO_DEV_SIZE - 1,

  PSG_START_ADDR = 0xC200,
  PSG_END_ADDR = PSG_START_ADDR + IO_DEV_SIZE - 1,

  BNK_START_ADDR = 0xC500,
  BNK_END_ADDR = BNK_START_ADDR + IO_DEV_SIZE - 1,

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
  PSG_HZ = 10738635 / 3,
  SAMPLE_RATE = 44100,
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

typedef struct {
  U8 ora;
  U8 orb;
  U8 ddra;
  U8 ddrb;
  U8 ira;
  U8 irb;
  U8 paIn;
  U8 pbIn;
  U16 t1c;
  U8 t1ll;
  U8 t1lh;
  U16 t2c;
  U8 t2ll;
  U8 sr;
  U8 acr;
  U8 pcr;
  U8 ifr;
  U8 ier;
  Bool t1Active;
  Bool t2Active;

  Bool ca2Out;
  Bool cb2Out;
  Bool ca1In;
  Bool ca2In;
  Bool cb2In;
  Bool cb1In;
  Bool cb1Out;
  U8 ca2Pulse;
  U8 cb2Pulse;

  U8 srCount;
  Bool srRun;
  U16 srDiv;
} Via;

typedef struct {
  U8 buf[16];
  U8 head;
  U8 tail;
  U8 lastTail;
  U8 pulseCycle;
  Bool pulsed;
  U16 clkDiv;
} Ps2;

typedef struct {
  U16 tonePeriod[3];
  U16 toneCounter[3];
  U8 toneOut[3];
  U8 vol[4];
  U8 noiseCtrl;
  U16 noiseCounter;
  U16 noiseShift;
  U8 noiseFlip;
  U8 latchCh;
  Bool latchVol;
  U32 clkRem;
} Psg;

typedef struct {
  FILE *file;
  Bool lastSck;
  Bool miso;
  U8 bitCnt;
  U8 inBits;
  U8 outByte;
  U8 state;
  U8 afterSend;
  U8 cmd[6];
  U8 cmdCnt;
  Bool appCmd;
  Bool idle;
  U8 resp[520];
  UInt respLen;
  UInt respPos;
  U8 data[512];
  UInt dataCnt;
  U32 writeAddr;
} Sd;

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
  Via via;
  Psg psg;
  Ps2 ps2;
  Sd sd;
  U8 ramlo[RAMLO_SIZE];
  U8 ramhi[4][RAMHI_SIZE];
  U8 rom[ROM_SIZE];
  U8 bnk;
} Emu;

U8 emuRead(Emu const *emu, U16 addr);

void cpuReset(Cpu *cpu, Emu *emu);
UInt cpuTick(Cpu *cpu, Emu *emu);

void vdpReset(Vdp *vdp, Emu *emu, Bool random);
Bool vdpTick(Vdp *vdp, Emu *emu);
U8 vdpRead(Vdp *vdp, U16 addr);
void vdpWrite(Vdp *vdp, U16 addr, U8 val);

typedef enum {
  VIA_PORT_A,
  VIA_PORT_B,
} ViaPort;

void viaReset(Via *via);
U8 viaRead(Via *via, U16 reg);
void viaWrite(Via *via, U16 reg, U8 val);
Bool viaTick(Via *via, UInt cycles);

void viaSetPort(Via *via, ViaPort port, U8 val);
U8 viaGetPort(Via const *via, ViaPort port);

void viaSetC1(Via *via, ViaPort port, Bool level);
Bool viaGetC1(Via const *via, ViaPort port);
Bool viaC1Irq(Via const *via, ViaPort port);

void viaSetC2(Via *via, ViaPort port, Bool level);
Bool viaGetC2(Via const *via, ViaPort port);
Bool viaC2Irq(Via const *via, ViaPort port);

void psgReset(Psg *psg);
void psgWrite(Psg *psg, U8 val);
I16 psgSample(Psg *psg);

void ps2Reset(Ps2 *ps2);
void ps2Key(Ps2 *ps2, UInt scancode, Bool down);
Bool ps2Pending(Ps2 const *ps2);
void ps2Tick(Ps2 *ps2, Via *via, ViaPort port);

void sdReset(Sd *sd);
void sdTick(Sd *sd, Via *via, ViaPort port);

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
  case RAMLO_START_ADDR ... RAMLO_END_ADDR:
    return emu->ramlo[addr - RAMLO_START_ADDR];
  case RAMHI_START_ADDR ... RAMHI_END_ADDR:
    return emu->ramhi[emu->bnk & 0x3][addr - RAMHI_START_ADDR];
  case VDP_START_ADDR ... VDP_END_ADDR:
    return vdpRead(&emu->vdp, addr - VDP_START_ADDR);
  case VIA_START_ADDR ... VIA_END_ADDR:
    return viaRead(&emu->via, addr - VIA_START_ADDR);
  case PSG_START_ADDR ... PSG_END_ADDR:
    return 0;
  case ROM_START_ADDR ... ROM_END_ADDR:
    return emu->rom[addr - ROM_START_ADDR];
  default:
    return 0;
  }
}

static inline void busWrite(Emu *emu, U16 addr, U8 val) {
  switch (addr) {
  case RAMLO_START_ADDR ... RAMLO_END_ADDR:
    emu->ramlo[addr - RAMLO_START_ADDR] = val;
    break;
  case RAMHI_START_ADDR ... RAMHI_END_ADDR:
    emu->ramhi[emu->bnk & 0x3][addr - RAMHI_START_ADDR] = val;
    break;
  case VDP_START_ADDR ... VDP_END_ADDR:
    vdpWrite(&emu->vdp, addr - VDP_START_ADDR, val);
    break;
  case VIA_START_ADDR ... VIA_END_ADDR:
    viaWrite(&emu->via, addr - VIA_START_ADDR, val);
    break;
  case PSG_START_ADDR ... PSG_END_ADDR:
    psgWrite(&emu->psg, val);
    break;
  case ROM_START_ADDR ... ROM_END_ADDR:
    break;
  default:
    break;
  }
}
#endif // BUS_MOCK

#endif // VM_H
