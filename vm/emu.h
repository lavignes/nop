#ifndef EMU_H
#define EMU_H

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
  U8 ram[];
} Emu;

typedef struct {
  void *data;
  U8 (*read)(void *data, U16 addr);
  void (*write)(void *data, U16 addr, U8 val);
} Bus;

void cpuTick(Cpu *cpu, Bus *bus);

#endif // EMU_H
