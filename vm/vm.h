#ifndef VM_H
#define VM_H

#include <signal.h>

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

  IO_SIZE = 0x1000,
  IO_START_ADDR = 0xC000,
  IO_END_ADDR = IO_START_ADDR + IO_SIZE - 1,

  ROM_SIZE = 0x2000,
  ROM_START_ADDR = 0xD000,
  ROM_END_ADDR = ROM_START_ADDR + ROM_SIZE - 1,
};

typedef struct {
  U8 a;
  U8 x;
  U8 y;
  U8 sp;
  U8 p;
  U16 pc;

  void *ea;
  U16 eaAddr;
} Cpu;

typedef struct {
} Vdp;

typedef struct {
  Cpu cpu;
  Vdp vdp;
  U8 ram[RAM_SIZE];
  U8 rom[ROM_SIZE];
} Emu;

#ifdef BUS_MOCK
U8 busRead(Emu *emu, U16 addr);
void busWrite(Emu *emu, U16 addr, U8 val);
#else
static inline U8 busRead(Emu *emu, U16 addr) {
  switch (addr) {
  case RAM_START_ADDR ... RAM_END_ADDR:
    return emu->ram[addr - RAM_START_ADDR];
  case IO_START_ADDR ... IO_END_ADDR:
    return 0;
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
    break;
  case ROM_START_ADDR ... ROM_END_ADDR:
    break;
  default:
    break;
  }
}
#endif // BUS_MOCK

UInt cpuTick(Cpu *cpu, Emu *bus);

typedef struct Symbol Symbol;

struct Symbol {
  Symbol *next;
  char const *name;
  Int val;
};

Symbol const *symValFind(Int val);
Symbol const *symFind(char const *name, UInt namelen);
Symbol const *symFirst(void);
void symLoad(char const *filename);

typedef struct Breakpoint Breakpoint;
struct Breakpoint {
  Breakpoint *next;
  U16 num;
  U16 addr;
};

extern U16 bpCount;
extern Breakpoint *bpHead;
extern Breakpoint nextpoint;
extern Bool debug;
extern volatile sig_atomic_t sigintFlag;

U8 dbgRead(Emu const *emu, U16 addr);
U16 disAsm(Emu const *emu, U16 addr);
void dbgTick(Emu *emu);

void termRawModeOn(void);
void termRawModeOff(void);

#endif // VM_H
