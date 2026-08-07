#include "emu.h"

#include <stdlib.h>

static void flag(Cpu *cpu, U8 flag, Bool cond) {
  if (cond) {
    cpu->p |= flag;
  } else {
    cpu->p &= ~flag;
  }
}

static void IMP(Cpu *cpu, Bus *bus) {}

static void ACC(Cpu *cpu, Bus *bus) { cpu->ea = &cpu->a; }

static void IMM(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  cpu->eaAddr = cpu->pc++;
}

static void ZPG(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  cpu->eaAddr = bus->read(bus->data, cpu->pc++);
}

static void ZPX(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  cpu->eaAddr = bus->read(bus->data, cpu->pc++) + cpu->x;
}

static void ZPY(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  cpu->eaAddr = bus->read(bus->data, cpu->pc++) + cpu->y;
}

static void ABS(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  U8 lo = bus->read(bus->data, cpu->pc++);
  U8 hi = bus->read(bus->data, cpu->pc++);
  cpu->eaAddr = (((U16)hi) << 8) | lo;
}

static void ABX(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  U8 lo = bus->read(bus->data, cpu->pc++);
  U8 hi = bus->read(bus->data, cpu->pc++);
  cpu->eaAddr = ((((U16)hi) << 8) | lo) + cpu->x;
}

static void ABY(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  U8 lo = bus->read(bus->data, cpu->pc++);
  U8 hi = bus->read(bus->data, cpu->pc++);
  cpu->eaAddr = ((((U16)hi) << 8) | lo) + cpu->y;
}

static void IDX(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  U8 zpAddr = bus->read(bus->data, cpu->pc++) + cpu->x;
  U8 lo = bus->read(bus->data, zpAddr);
  U8 hi = bus->read(bus->data, (U8)(zpAddr + 1));
  cpu->eaAddr = (((U16)hi) << 8) | lo;
}

static void IDY(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  U8 zpAddr = bus->read(bus->data, cpu->pc++);
  U8 lo = bus->read(bus->data, zpAddr);
  U8 hi = bus->read(bus->data, (U8)(zpAddr + 1));
  cpu->eaAddr = ((((U16)hi) << 8) | lo) + cpu->y;
}

static void JAB(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  U8 lo = bus->read(bus->data, cpu->pc++);
  U8 hi = bus->read(bus->data, cpu->pc++);
  cpu->eaAddr = (((U16)hi) << 8) | lo;
}

static void JID(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  U8 idLo = bus->read(bus->data, cpu->pc++);
  U8 idHi = bus->read(bus->data, cpu->pc++);
  U8 lo = bus->read(bus->data, (((U16)idHi) << 8) | idLo);
  U8 hi =
      bus->read(bus->data, (((U16)idHi) << 8) | (U8)(idLo + 1)); // wrapping bug
  cpu->eaAddr = (((U16)hi) << 8) | lo;
}

static U8 read(Cpu *cpu, Bus *bus) {
  if (cpu->ea == bus) {
    return bus->read(bus->data, cpu->eaAddr);
  } else {
    return *(U8 *)cpu->ea;
  }
}

static void write(Cpu *cpu, Bus *bus, U8 val) {
  if (cpu->ea == bus) {
    bus->write(bus->data, cpu->eaAddr, val);
  } else {
    *(U8 *)cpu->ea = val;
  }
}

static void ADC(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  if (cpu->p & CPU_FLAG_DECIMAL) {
    abort(); // TODO: implement BCD mode
  }
  U16 sum = cpu->a + val + (cpu->p & CPU_FLAG_CARRY ? 1 : 0);
  U8 res = sum & 0xFF;
  flag(cpu, CPU_FLAG_CARRY, sum > U8_MAX);
  flag(cpu, CPU_FLAG_ZERO, (sum & 0xFF) == 0);
  flag(cpu, CPU_FLAG_OVERFLOW, (~(cpu->a ^ val) & (cpu->a ^ res) & 0x80) != 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (sum & 0x80) != 0);
  cpu->a = res;
}

static void AND(Cpu *cpu, Bus *bus) {
  cpu->a &= read(cpu, bus);
  flag(cpu, CPU_FLAG_ZERO, cpu->a == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (cpu->a & 0x80) != 0);
}

static void ASL(Cpu *cpu, Bus *bus) {
  flag(cpu, CPU_FLAG_CARRY, (read(cpu, bus) & 0x80) != 0);
  U8 val = read(cpu, bus) << 1;
  write(cpu, bus, val);
  flag(cpu, CPU_FLAG_ZERO, val == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (val & 0x80) != 0);
}

static void BCC(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  if (!(cpu->p & CPU_FLAG_CARRY)) {
    cpu->pc += (I8)val;
  }
}

static void BCS(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  if (cpu->p & CPU_FLAG_CARRY) {
    cpu->pc += (I8)val;
  }
}

static void BEQ(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  if (cpu->p & CPU_FLAG_ZERO) {
    cpu->pc += (I8)val;
  }
}

static void BIT(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  U8 res = cpu->a & val;
  flag(cpu, CPU_FLAG_ZERO, res == 0);
  flag(cpu, CPU_FLAG_OVERFLOW, (val & 0x40) != 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (val & 0x80) != 0);
}

static void BMI(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  if (cpu->p & CPU_FLAG_NEGATIVE) {
    cpu->pc += (I8)val;
  }
}

static void BNE(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  if (!(cpu->p & CPU_FLAG_ZERO)) {
    cpu->pc += (I8)val;
  }
}

static void BPL(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  if (!(cpu->p & CPU_FLAG_NEGATIVE)) {
    cpu->pc += (I8)val;
  }
}

static void BRK(Cpu *cpu, Bus *bus) {
  (void)bus;
  ++cpu->pc;
  bus->write(bus->data, 0x0100 + cpu->sp--, (U8)((cpu->pc >> 8) & 0xFF));
  bus->write(bus->data, 0x0100 + cpu->sp--, (U8)(cpu->pc & 0xFF));
  bus->write(bus->data, 0x0100 + cpu->sp--,
             cpu->p | CPU_FLAG_BREAK | CPU_FLAG_UNUSED);
  flag(cpu, CPU_FLAG_INTERRUPT, TRUE);
  U8 lo = bus->read(bus->data, 0xFFFE);
  U8 hi = bus->read(bus->data, 0xFFFF);
  cpu->pc = (((U16)hi) << 8) | lo;
}

static void BVC(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  if (!(cpu->p & CPU_FLAG_OVERFLOW)) {
    cpu->pc += (I8)val;
  }
}

static void BVS(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  if (cpu->p & CPU_FLAG_OVERFLOW) {
    cpu->pc += (I8)val;
  }
}

static void CLC(Cpu *cpu, Bus *bus) {
  (void)bus;
  flag(cpu, CPU_FLAG_CARRY, FALSE);
}

static void CLD(Cpu *cpu, Bus *bus) {
  (void)bus;
  flag(cpu, CPU_FLAG_DECIMAL, FALSE);
}

static void CLI(Cpu *cpu, Bus *bus) {
  (void)bus;
  flag(cpu, CPU_FLAG_INTERRUPT, FALSE);
}

static void CLV(Cpu *cpu, Bus *bus) {
  (void)bus;
  flag(cpu, CPU_FLAG_OVERFLOW, FALSE);
}

static void CMP(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  U8 res = cpu->a - val;
  flag(cpu, CPU_FLAG_CARRY, cpu->a >= val);
  flag(cpu, CPU_FLAG_ZERO, res == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (res & 0x80) != 0);
}

static void CPX(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  U8 res = cpu->x - val;
  flag(cpu, CPU_FLAG_CARRY, cpu->x >= val);
  flag(cpu, CPU_FLAG_ZERO, res == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (res & 0x80) != 0);
}

static void CPY(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  U8 res = cpu->y - val;
  flag(cpu, CPU_FLAG_CARRY, cpu->y >= val);
  flag(cpu, CPU_FLAG_ZERO, res == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (res & 0x80) != 0);
}

static void DEC(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus) - 1;
  write(cpu, bus, val);
  flag(cpu, CPU_FLAG_ZERO, val == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (val & 0x80) != 0);
}

static void DEX(Cpu *cpu, Bus *bus) {
  (void)bus;
  U8 val = cpu->x - 1;
  flag(cpu, CPU_FLAG_ZERO, val == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (val & 0x80) != 0);
}

static void DEY(Cpu *cpu, Bus *bus) {
  (void)bus;
  U8 val = cpu->y - 1;
  flag(cpu, CPU_FLAG_ZERO, val == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (val & 0x80) != 0);
}

static void EOR(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  cpu->a ^= val;
  flag(cpu, CPU_FLAG_ZERO, cpu->a == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (cpu->a & 0x80) != 0);
}

static void INC(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus) + 1;
  flag(cpu, CPU_FLAG_ZERO, val == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (val & 0x80) != 0);
}

static void INX(Cpu *cpu, Bus *bus) {
  ++cpu->x;
  flag(cpu, CPU_FLAG_ZERO, cpu->x == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (cpu->x & 0x80) != 0);
}

static void INY(Cpu *cpu, Bus *bus) {
  ++cpu->y;
  flag(cpu, CPU_FLAG_ZERO, cpu->y == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (cpu->y & 0x80) != 0);
}

static void JMP(Cpu *cpu, Bus *bus) { cpu->pc = cpu->eaAddr; }

static void JSR(Cpu *cpu, Bus *bus) {
  U16 ret = cpu->pc - 1;
  bus->write(bus->data, 0x0100 + cpu->sp--, (U8)((ret >> 8) & 0xFF));
  bus->write(bus->data, 0x0100 + cpu->sp--, (U8)(ret & 0xFF));
  cpu->pc = cpu->eaAddr;
}

static void LDA(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  flag(cpu, CPU_FLAG_ZERO, val == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (val & 0x80) != 0);
  cpu->a = val;
}

static void LDX(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  flag(cpu, CPU_FLAG_ZERO, val == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (val & 0x80) != 0);
  cpu->x = val;
}

static void LDY(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  flag(cpu, CPU_FLAG_ZERO, val == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (val & 0x80) != 0);
  cpu->y = val;
}

static void LSR(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  flag(cpu, CPU_FLAG_CARRY, (val & 0x01) != 0);
  val >>= 1;
  write(cpu, bus, val);
  flag(cpu, CPU_FLAG_ZERO, val == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, FALSE);
}

static void nop(U8 *val) { (void)val; }

static void ora(U8 *val) {
  A |= *val;
  flag(FLAG_ZERO, A == 0);
  flag(FLAG_NEGATIVE, (A & 0x80) != 0);
}

static void pha(U8 *val) {
  (void)val;
  MEM[0x0100 + SP--] = A;
}

static void php(U8 *val) {
  (void)val;
  MEM[0x0100 + SP--] = P | FLAG_BREAK | FLAG_UNUSED;
}

static void pla(U8 *val) {
  (void)val;
  A = MEM[0x0100 + ++SP];
  flag(FLAG_ZERO, A == 0);
  flag(FLAG_NEGATIVE, (A & 0x80) != 0);
}

static void plp(U8 *val) {
  (void)val;
  P = MEM[0x0100 + ++SP] & ~(FLAG_BREAK | FLAG_UNUSED);
}

static void rol(U8 *val) {
  Bool cy = (P & FLAG_CARRY) != 0;
  flag(FLAG_CARRY, (*val & 0x80) != 0);
  *val = (*val << 1) | (cy ? 1 : 0);
  flag(FLAG_ZERO, *val == 0);
  flag(FLAG_NEGATIVE, (*val & 0x80) != 0);
}

static void ror(U8 *val) {
  Bool cy = (P & FLAG_CARRY) != 0;
  flag(FLAG_CARRY, (*val & 0x01) != 0);
  *val = (*val >> 1) | (cy ? 0x80 : 0);
  flag(FLAG_ZERO, *val == 0);
  flag(FLAG_NEGATIVE, (*val & 0x80) != 0);
}

static void rti(U8 *val) {
  (void)val;
  P = MEM[0x0100 + ++SP] & ~(FLAG_BREAK | FLAG_UNUSED);
  U8 lo = MEM[0x0100 + ++SP];
  U8 hi = MEM[0x0100 + ++SP];
  PC = (((U16)hi) << 8) | lo;
}

static void rts(U8 *val) {
  (void)val;
  U8 lo = MEM[0x0100 + ++SP];
  U8 hi = MEM[0x0100 + ++SP];
  PC = ((((U16)hi) << 8) | lo) + 1;
}

static void sbc(U8 *val) {
  if (P & FLAG_DECIMAL) {
    abort(); // TODO: implement BCD mode
  }
  U16 diff = A - *val - (P & FLAG_CARRY ? 0 : 1);
  U8 res = diff & 0xFF;
  flag(FLAG_CARRY, diff <= U8_MAX);
  flag(FLAG_ZERO, (diff & 0xFF) == 0);
  flag(FLAG_OVERFLOW, ((A ^ *val) & (A ^ res) & 0x80) != 0);
  flag(FLAG_NEGATIVE, (diff & 0x80) != 0);
  A = res;
}

static void sec(U8 *val) {
  (void)val;
  flag(FLAG_CARRY, TRUE);
}

static void sed(U8 *val) {
  (void)val;
  flag(FLAG_DECIMAL, TRUE);
}

static void sei(U8 *val) {
  (void)val;
  flag(FLAG_INTERRUPT, TRUE);
}

static void sta(U8 *val) {
  if (val == (U8 *)&MEM[0]) {
    ioAddrWrite(A);
    return;
  }
  if (val == (U8 *)&MEM[1]) {
    ioDataWrite(A);
    return;
  }
  *val = A;
}

static void stx(U8 *val) {
  if (val == (U8 *)&MEM[0]) {
    ioAddrWrite(X);
    return;
  }
  if (val == (U8 *)&MEM[1]) {
    ioDataWrite(X);
    return;
  }
  *val = X;
}

static void sty(U8 *val) {
  if (val == (U8 *)&MEM[0]) {
    ioAddrWrite(Y);
    return;
  }
  if (val == (U8 *)&MEM[1]) {
    ioDataWrite(Y);
    return;
  }
  *val = Y;
}

static void tax(U8 *val) {
  (void)val;
  X = A;
  flag(FLAG_ZERO, X == 0);
  flag(FLAG_NEGATIVE, (X & 0x80) != 0);
}

static void tay(U8 *val) {
  (void)val;
  Y = A;
  flag(FLAG_ZERO, Y == 0);
  flag(FLAG_NEGATIVE, (Y & 0x80) != 0);
}

static void tsx(U8 *val) {
  (void)val;
  X = SP;
  flag(FLAG_ZERO, X == 0);
  flag(FLAG_NEGATIVE, (X & 0x80) != 0);
}

static void txa(U8 *val) {
  (void)val;
  A = X;
  flag(FLAG_ZERO, A == 0);
  flag(FLAG_NEGATIVE, (A & 0x80) != 0);
}

static void txs(U8 *val) {
  (void)val;
  SP = X;
}

static void tya(U8 *val) {
  (void)val;
  A = Y;
  flag(FLAG_ZERO, A == 0);
  flag(FLAG_NEGATIVE, (A & 0x80) != 0);
}

typedef struct {
  void (*exec)(Cpu *cpu, Bus *bus);
  void (*addr)(Cpu *cpu, Bus *bus);
} OpEntry;

static OpEntry const OP_TBL[256] = {
    [0x00] = {BRK, IMM}, [0x01] = {ora, IDX}, [0x05] = {ora, ZPG},
    [0x06] = {ASL, ZPG}, [0x08] = {php, IMP}, [0x09] = {ora, IMM},
    [0x0A] = {ASL, ACC}, [0x0D] = {ora, ABS}, [0x0E] = {ASL, ABS},
    [0x10] = {BPL, IMM}, [0x11] = {ora, IDY}, [0x15] = {ora, ZPX},
    [0x16] = {ASL, ZPX}, [0x18] = {CLC, IMP}, [0x19] = {ora, ABY},
    [0x1D] = {ora, ABX}, [0x1E] = {ASL, ABX}, [0x20] = {JSR, JAB},
    [0x21] = {AND, IDX}, [0x24] = {BIT, ZPG}, [0x25] = {AND, ZPG},
    [0x26] = {rol, ZPG}, [0x28] = {plp, IMP}, [0x29] = {AND, IMM},
    [0x2A] = {rol, ACC}, [0x2C] = {BIT, ABS}, [0x2D] = {AND, ABS},
    [0x2E] = {rol, ABS}, [0x30] = {BMI, IMM}, [0x31] = {AND, IDY},
    [0x35] = {AND, ZPX}, [0x36] = {rol, ZPX}, [0x38] = {sec, IMP},
    [0x39] = {AND, ABY}, [0x3D] = {AND, ABX}, [0x3E] = {rol, ABX},
    [0x40] = {rti, IMP}, [0x41] = {EOR, IDX}, [0x45] = {EOR, ZPG},
    [0x46] = {LSR, ZPG}, [0x48] = {pha, IMP}, [0x49] = {EOR, IMM},
    [0x4A] = {LSR, ACC}, [0x4C] = {JMP, JAB}, [0x4D] = {EOR, ABS},
    [0x4E] = {LSR, ABS}, [0x50] = {BVC, IMM}, [0x51] = {EOR, IDY},
    [0x55] = {EOR, ZPX}, [0x56] = {LSR, ZPX}, [0x58] = {CLI, IMP},
    [0x59] = {EOR, ABY}, [0x5D] = {EOR, ABX}, [0x5E] = {LSR, ABX},
    [0x60] = {rts, IMP}, [0x61] = {ADC, IDX}, [0x65] = {ADC, ZPG},
    [0x66] = {ror, ZPG}, [0x68] = {pla, IMP}, [0x69] = {ADC, IMM},
    [0x6A] = {ror, ACC}, [0x6C] = {JMP, JID}, [0x6D] = {ADC, ABS},
    [0x6E] = {ror, ABS}, [0x70] = {BVS, IMM}, [0x71] = {ADC, IDY},
    [0x75] = {ADC, ZPX}, [0x76] = {ror, ZPX}, [0x78] = {sei, IMP},
    [0x79] = {ADC, ABY}, [0x7D] = {ADC, ABX}, [0x7E] = {ror, ABX},
    [0x81] = {sta, IDX}, [0x84] = {sty, ZPG}, [0x85] = {sta, ZPG},
    [0x86] = {stx, ZPG}, [0x88] = {DEY, IMP}, [0x8A] = {txa, IMP},
    [0x8C] = {sty, ABS}, [0x8D] = {sta, ABS}, [0x8E] = {stx, ABS},
    [0x90] = {BCC, IMM}, [0x91] = {sta, IDY}, [0x94] = {sty, ZPX},
    [0x95] = {sta, ZPX}, [0x96] = {stx, ZPY}, [0x98] = {tya, IMP},
    [0x99] = {sta, ABY}, [0x9A] = {txs, IMP}, [0x9D] = {sta, ABX},
    [0xA0] = {LDY, IMM}, [0xA1] = {LDA, IDX}, [0xA2] = {LDX, IMM},
    [0xA4] = {LDY, ZPG}, [0xA5] = {LDA, ZPG}, [0xA6] = {LDX, ZPG},
    [0xA8] = {tay, IMP}, [0xA9] = {LDA, IMM}, [0xAA] = {tax, IMP},
    [0xAC] = {LDY, ABS}, [0xAD] = {LDA, ABS}, [0xAE] = {LDX, ABS},
    [0xB0] = {BCS, IMM}, [0xB1] = {LDA, IDY}, [0xB4] = {LDY, ZPX},
    [0xB5] = {LDA, ZPX}, [0xB6] = {LDX, ZPY}, [0xB8] = {CLV, IMP},
    [0xB9] = {LDA, ABY}, [0xBA] = {tsx, IMP}, [0xBC] = {LDY, ABX},
    [0xBD] = {LDA, ABX}, [0xBE] = {LDX, ABY}, [0xC0] = {CPY, IMM},
    [0xC1] = {CMP, IDX}, [0xC4] = {CPY, ZPG}, [0xC5] = {CMP, ZPG},
    [0xC6] = {DEC, ZPG}, [0xC8] = {INY, IMP}, [0xC9] = {CMP, IMM},
    [0xCA] = {DEX, IMP}, [0xCC] = {CPY, ABS}, [0xCD] = {CMP, ABS},
    [0xCE] = {DEC, ABS}, [0xD0] = {BNE, IMM}, [0xD1] = {CMP, IDY},
    [0xD5] = {CMP, ZPX}, [0xD6] = {DEC, ZPX}, [0xD8] = {CLD, IMP},
    [0xD9] = {CMP, ABY}, [0xDD] = {CMP, ABX}, [0xDE] = {DEC, ABX},
    [0xE0] = {CPX, IMM}, [0xE1] = {sbc, IDX}, [0xE4] = {CPX, ZPG},
    [0xE5] = {sbc, ZPG}, [0xE6] = {INC, ZPG}, [0xE8] = {INX, IMP},
    [0xE9] = {sbc, IMM}, [0xEA] = {nop, IMP}, [0xEC] = {CPX, ABS},
    [0xED] = {sbc, ABS}, [0xEE] = {INC, ABS}, [0xF0] = {BEQ, IMM},
    [0xF1] = {sbc, IDY}, [0xF5] = {sbc, ZPX}, [0xF6] = {INC, ZPX},
    [0xF8] = {sed, IMP}, [0xF9] = {sbc, ABY}, [0xFD] = {sbc, ABX},
    [0xFE] = {INC, ABX},
};

void cpuTick(Cpu *cpu, Bus *bus) {
  U8 op = bus->read(bus->data, cpu->pc++);
  OpEntry const *entry = &OP_TBL[op];
  if (entry->exec) {
    entry->addr(cpu, bus);
    entry->exec(cpu, bus);
  }
}
