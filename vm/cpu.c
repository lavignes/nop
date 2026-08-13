#include "vm.h"

static void flag(Cpu *cpu, U8 flag, Bool cond) {
  if (cond) {
    cpu->p |= flag;
  } else {
    cpu->p &= ~flag;
  }
}

static void IMP(Cpu *cpu, Bus *bus) {
  (void)cpu;
  (void)bus;
}

static void ACC(Cpu *cpu, Bus *bus) {
  (void)bus;
  cpu->ea = &cpu->a;
}

static void IMM(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  cpu->eaAddr = cpu->pc++;
}

static void ZPG(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  cpu->eaAddr = busRead(bus, cpu->pc++);
}

static void ZPX(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  cpu->eaAddr = busRead(bus, cpu->pc++) + cpu->x;
}

static void ZPY(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  cpu->eaAddr = busRead(bus, cpu->pc++) + cpu->y;
}

static void ABS(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  U8 lo = busRead(bus, cpu->pc++);
  U8 hi = busRead(bus, cpu->pc++);
  cpu->eaAddr = (((U16)hi) << 8) | lo;
}

static void ABX(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  U8 lo = busRead(bus, cpu->pc++);
  U8 hi = busRead(bus, cpu->pc++);
  U16 base = (((U16)hi) << 8) | lo;
  cpu->eaAddr = base + cpu->x;
}

static void ABY(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  U8 lo = busRead(bus, cpu->pc++);
  U8 hi = busRead(bus, cpu->pc++);
  U16 base = (((U16)hi) << 8) | lo;
  cpu->eaAddr = base + cpu->y;
}

static void IDX(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  U8 zpAddr = busRead(bus, cpu->pc++) + cpu->x;
  U8 lo = busRead(bus, zpAddr);
  U8 hi = busRead(bus, (U8)(zpAddr + 1));
  cpu->eaAddr = (((U16)hi) << 8) | lo;
}

static void IDY(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  U8 zpAddr = busRead(bus, cpu->pc++);
  U8 lo = busRead(bus, zpAddr);
  U8 hi = busRead(bus, (U8)(zpAddr + 1));
  U16 base = (((U16)hi) << 8) | lo;
  cpu->eaAddr = base + cpu->y;
}

static void JAB(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  U8 lo = busRead(bus, cpu->pc++);
  U8 hi = busRead(bus, cpu->pc++);
  cpu->eaAddr = (((U16)hi) << 8) | lo;
}

static void JID(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  U8 idLo = busRead(bus, cpu->pc++);
  U8 idHi = busRead(bus, cpu->pc++);
  U8 lo = busRead(bus, (((U16)idHi) << 8) | idLo);
  U8 hi = busRead(bus, (((U16)idHi) << 8) | (U8)(idLo + 1));
  cpu->eaAddr = (((U16)hi) << 8) | lo;
}

static U8 read(Cpu *cpu, Bus *bus) {
  if (cpu->ea == bus) {
    return busRead(bus, cpu->eaAddr);
  } else {
    return *(U8 *)cpu->ea;
  }
}

static void write(Cpu *cpu, Bus *bus, U8 val) {
  if (cpu->ea == bus) {
    busWrite(bus, cpu->eaAddr, val);
  } else {
    *(U8 *)cpu->ea = val;
  }
}

static void ADC(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  U8 carry = cpu->p & CPU_FLAG_CARRY ? 1 : 0;
  if (cpu->p & CPU_FLAG_DECIMAL) {
    U8 al = cpu->a & 0x0F;
    U8 ah = cpu->a >> 4;
    U8 vl = val & 0x0F;
    U8 vh = val >> 4;
    U8 lo = al + vl + carry;
    if (lo > 9) {
      lo += 6;
    }
    U8 cy = lo > 0x0F ? 1 : 0;
    U8 hi = ah + vh + cy;
    if (hi > 9) {
      hi += 6;
    }
    U8 res = ((hi & 0x0F) << 4) | (lo & 0x0F);
    flag(cpu, CPU_FLAG_CARRY, hi > 0x0F);
    flag(cpu, CPU_FLAG_ZERO, res == 0);
    flag(cpu, CPU_FLAG_NEGATIVE, (res & 0x80) != 0);
    flag(cpu, CPU_FLAG_OVERFLOW, FALSE);
    cpu->a = res;
  } else {
    U16 sum = cpu->a + val + carry;
    U8 res = sum & 0xFF;
    flag(cpu, CPU_FLAG_CARRY, sum > U8_MAX);
    flag(cpu, CPU_FLAG_ZERO, (sum & 0xFF) == 0);
    flag(cpu, CPU_FLAG_OVERFLOW,
         (~(cpu->a ^ val) & (cpu->a ^ res) & 0x80) != 0);
    flag(cpu, CPU_FLAG_NEGATIVE, (sum & 0x80) != 0);
    cpu->a = res;
  }
}

static void AND(Cpu *cpu, Bus *bus) {
  cpu->a &= read(cpu, bus);
  flag(cpu, CPU_FLAG_ZERO, cpu->a == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (cpu->a & 0x80) != 0);
}

static void ASL(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  flag(cpu, CPU_FLAG_CARRY, (val & 0x80) != 0);
  val <<= 1;
  write(cpu, bus, val);
  flag(cpu, CPU_FLAG_ZERO, val == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (val & 0x80) != 0);
}

static void BCC(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  if (!(cpu->p & CPU_FLAG_CARRY)) {
    U16 newPc = cpu->pc + (I8)val;
    cpu->pc = newPc;
  }
}

static void BCS(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  if (cpu->p & CPU_FLAG_CARRY) {
    U16 newPc = cpu->pc + (I8)val;
    cpu->pc = newPc;
  }
}

static void BEQ(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  if (cpu->p & CPU_FLAG_ZERO) {
    U16 newPc = cpu->pc + (I8)val;
    cpu->pc = newPc;
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
    U16 newPc = cpu->pc + (I8)val;
    cpu->pc = newPc;
  }
}

static void BNE(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  if (!(cpu->p & CPU_FLAG_ZERO)) {
    U16 newPc = cpu->pc + (I8)val;
    cpu->pc = newPc;
  }
}

static void BPL(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  if (!(cpu->p & CPU_FLAG_NEGATIVE)) {
    U16 newPc = cpu->pc + (I8)val;
    cpu->pc = newPc;
  }
}

static void BRK(Cpu *cpu, Bus *bus) {
  ++cpu->pc;
  busWrite(bus, 0x0100 + cpu->sp--, (U8)((cpu->pc >> 8) & 0xFF));
  busWrite(bus, 0x0100 + cpu->sp--, (U8)(cpu->pc & 0xFF));
  busWrite(bus, 0x0100 + cpu->sp--, cpu->p | CPU_FLAG_BREAK | CPU_FLAG_UNUSED);
  flag(cpu, CPU_FLAG_INTERRUPT, TRUE);
  U8 lo = busRead(bus, 0xFFFE);
  U8 hi = busRead(bus, 0xFFFF);
  cpu->pc = (((U16)hi) << 8) | lo;
}

static void BVC(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  if (!(cpu->p & CPU_FLAG_OVERFLOW)) {
    U16 newPc = cpu->pc + (I8)val;
    cpu->pc = newPc;
  }
}

static void BVS(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  if (cpu->p & CPU_FLAG_OVERFLOW) {
    U16 newPc = cpu->pc + (I8)val;
    cpu->pc = newPc;
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
  --cpu->x;
  flag(cpu, CPU_FLAG_ZERO, cpu->x == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (cpu->x & 0x80) != 0);
}

static void DEY(Cpu *cpu, Bus *bus) {
  (void)bus;
  --cpu->y;
  flag(cpu, CPU_FLAG_ZERO, cpu->y == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (cpu->y & 0x80) != 0);
}

static void EOR(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  cpu->a ^= val;
  flag(cpu, CPU_FLAG_ZERO, cpu->a == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (cpu->a & 0x80) != 0);
}

static void INC(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus) + 1;
  write(cpu, bus, val);
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

static void JMP(Cpu *cpu, Bus *bus) {
  (void)bus;
  cpu->pc = cpu->eaAddr;
}

static void JSR(Cpu *cpu, Bus *bus) {
  U16 ret = cpu->pc - 1;
  busWrite(bus, 0x0100 + cpu->sp--, (U8)((ret >> 8) & 0xFF));
  busWrite(bus, 0x0100 + cpu->sp--, (U8)(ret & 0xFF));
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

static void NOP(Cpu *cpu, Bus *bus) {
  (void)cpu;
  (void)bus;
}

static void ORA(Cpu *cpu, Bus *bus) {
  cpu->a |= read(cpu, bus);
  flag(cpu, CPU_FLAG_ZERO, cpu->a == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (cpu->a & 0x80) != 0);
}

static void PHA(Cpu *cpu, Bus *bus) {
  busWrite(bus, 0x0100 + cpu->sp--, cpu->a);
}

static void PHP(Cpu *cpu, Bus *bus) {
  busWrite(bus, 0x0100 + cpu->sp--, cpu->p | CPU_FLAG_BREAK | CPU_FLAG_UNUSED);
}

static void PLA(Cpu *cpu, Bus *bus) {
  cpu->a = busRead(bus, 0x0100 + ++cpu->sp);
  flag(cpu, CPU_FLAG_ZERO, cpu->a == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (cpu->a & 0x80) != 0);
}

static void PLP(Cpu *cpu, Bus *bus) {
  cpu->p =
      busRead(bus, 0x0100 + ++cpu->sp) & ~(CPU_FLAG_BREAK | CPU_FLAG_UNUSED);
}

static void ROL(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  Bool cy = (cpu->p & CPU_FLAG_CARRY) != 0;
  flag(cpu, CPU_FLAG_CARRY, (val & 0x80) != 0);
  val = (val << 1) | (cy ? 1 : 0);
  write(cpu, bus, val);
  flag(cpu, CPU_FLAG_ZERO, val == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (val & 0x80) != 0);
}

static void ROR(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  Bool cy = (cpu->p & CPU_FLAG_CARRY) != 0;
  flag(cpu, CPU_FLAG_CARRY, (val & 0x01) != 0);
  val = (val >> 1) | (cy ? 0x80 : 0);
  write(cpu, bus, val);
  flag(cpu, CPU_FLAG_ZERO, val == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (val & 0x80) != 0);
}

static void RTI(Cpu *cpu, Bus *bus) {
  cpu->p =
      busRead(bus, 0x0100 + ++cpu->sp) & ~(CPU_FLAG_BREAK | CPU_FLAG_UNUSED);
  U8 lo = busRead(bus, 0x0100 + ++cpu->sp);
  U8 hi = busRead(bus, 0x0100 + ++cpu->sp);
  cpu->pc = (((U16)hi) << 8) | lo;
}

static void RTS(Cpu *cpu, Bus *bus) {
  U8 lo = busRead(bus, 0x0100 + ++cpu->sp);
  U8 hi = busRead(bus, 0x0100 + ++cpu->sp);
  cpu->pc = ((((U16)hi) << 8) | lo) + 1;
}

static void SBC(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  U8 carry = cpu->p & CPU_FLAG_CARRY ? 0 : 1;
  if (cpu->p & CPU_FLAG_DECIMAL) {
    U8 al = cpu->a & 0x0F;
    U8 ah = cpu->a >> 4;
    U8 vl = val & 0x0F;
    U8 vh = val >> 4;
    I8 lo = al - vl - carry;
    if (lo < 0) {
      lo = lo + 10;
    }
    U8 cy = lo < 0 ? 1 : 0;
    I8 hi = ah - vh - cy;
    if (hi < 0) {
      hi = hi + 10;
    }
    U8 res = ((hi & 0x0F) << 4) | (lo & 0x0F);
    flag(cpu, CPU_FLAG_CARRY, hi >= 0);
    flag(cpu, CPU_FLAG_ZERO, res == 0);
    flag(cpu, CPU_FLAG_NEGATIVE, (res & 0x80) != 0);
    flag(cpu, CPU_FLAG_OVERFLOW, FALSE);
    cpu->a = res;
  } else {
    U16 diff = cpu->a - val - carry;
    U8 res = diff & 0xFF;
    flag(cpu, CPU_FLAG_CARRY, diff <= U8_MAX);
    flag(cpu, CPU_FLAG_ZERO, (diff & 0xFF) == 0);
    flag(cpu, CPU_FLAG_OVERFLOW, ((cpu->a ^ val) & (cpu->a ^ res) & 0x80) != 0);
    flag(cpu, CPU_FLAG_NEGATIVE, (diff & 0x80) != 0);
    cpu->a = res;
  }
}

static void SEC(Cpu *cpu, Bus *bus) {
  (void)bus;
  flag(cpu, CPU_FLAG_CARRY, TRUE);
}

static void SED(Cpu *cpu, Bus *bus) {
  (void)bus;
  flag(cpu, CPU_FLAG_DECIMAL, TRUE);
}

static void SEI(Cpu *cpu, Bus *bus) {
  (void)bus;
  flag(cpu, CPU_FLAG_INTERRUPT, TRUE);
}

static void STA(Cpu *cpu, Bus *bus) { write(cpu, bus, cpu->a); }

static void STX(Cpu *cpu, Bus *bus) { write(cpu, bus, cpu->x); }

static void STY(Cpu *cpu, Bus *bus) { write(cpu, bus, cpu->y); }

static void TAX(Cpu *cpu, Bus *bus) {
  (void)bus;
  cpu->x = cpu->a;
  flag(cpu, CPU_FLAG_ZERO, cpu->x == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (cpu->x & 0x80) != 0);
}

static void TAY(Cpu *cpu, Bus *bus) {
  (void)bus;
  cpu->y = cpu->a;
  flag(cpu, CPU_FLAG_ZERO, cpu->y == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (cpu->y & 0x80) != 0);
}

static void TSX(Cpu *cpu, Bus *bus) {
  (void)bus;
  cpu->x = cpu->sp;
  flag(cpu, CPU_FLAG_ZERO, cpu->x == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (cpu->x & 0x80) != 0);
}

static void TXA(Cpu *cpu, Bus *bus) {
  (void)bus;
  cpu->a = cpu->x;
  flag(cpu, CPU_FLAG_ZERO, cpu->a == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (cpu->a & 0x80) != 0);
}

static void TXS(Cpu *cpu, Bus *bus) {
  (void)bus;
  cpu->sp = cpu->x;
}

static void TYA(Cpu *cpu, Bus *bus) {
  (void)bus;
  cpu->a = cpu->y;
  flag(cpu, CPU_FLAG_ZERO, cpu->a == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (cpu->a & 0x80) != 0);
}

typedef struct {
  void (*exec)(Cpu *cpu, Bus *bus);
  void (*addr)(Cpu *cpu, Bus *bus);
  UInt cycles;
} OpEntry;

static OpEntry const OP_TBL[256] = {
    [0X00] = {BRK, IMM, 7}, [0X01] = {ORA, IDX, 6}, [0X05] = {ORA, ZPG, 3},
    [0X06] = {ASL, ZPG, 5}, [0X08] = {PHP, IMP, 3}, [0X09] = {ORA, IMM, 2},
    [0X0A] = {ASL, ACC, 2}, [0X0D] = {ORA, ABS, 4}, [0X0E] = {ASL, ABS, 6},
    [0X10] = {BPL, IMM, 2}, [0X11] = {ORA, IDY, 5}, [0X15] = {ORA, ZPX, 4},
    [0X16] = {ASL, ZPX, 6}, [0X18] = {CLC, IMP, 2}, [0X19] = {ORA, ABY, 4},
    [0X1D] = {ORA, ABX, 4}, [0X1E] = {ASL, ABX, 7}, [0X20] = {JSR, JAB, 6},
    [0X21] = {AND, IDX, 6}, [0X24] = {BIT, ZPG, 3}, [0X25] = {AND, ZPG, 3},
    [0X26] = {ROL, ZPG, 5}, [0X28] = {PLP, IMP, 4}, [0X29] = {AND, IMM, 2},
    [0X2A] = {ROL, ACC, 2}, [0X2C] = {BIT, ABS, 4}, [0X2D] = {AND, ABS, 4},
    [0X2E] = {ROL, ABS, 6}, [0X30] = {BMI, IMM, 2}, [0X31] = {AND, IDY, 5},
    [0X35] = {AND, ZPX, 4}, [0X36] = {ROL, ZPX, 6}, [0X38] = {SEC, IMP, 2},
    [0X39] = {AND, ABY, 4}, [0X3D] = {AND, ABX, 4}, [0X3E] = {ROL, ABX, 7},
    [0X40] = {RTI, IMP, 6}, [0X41] = {EOR, IDX, 6}, [0X45] = {EOR, ZPG, 3},
    [0X46] = {LSR, ZPG, 5}, [0X48] = {PHA, IMP, 3}, [0X49] = {EOR, IMM, 2},
    [0X4A] = {LSR, ACC, 2}, [0X4C] = {JMP, JAB, 3}, [0X4D] = {EOR, ABS, 4},
    [0X4E] = {LSR, ABS, 6}, [0X50] = {BVC, IMM, 2}, [0X51] = {EOR, IDY, 5},
    [0X55] = {EOR, ZPX, 4}, [0X56] = {LSR, ZPX, 6}, [0X58] = {CLI, IMP, 2},
    [0X59] = {EOR, ABY, 4}, [0X5D] = {EOR, ABX, 4}, [0X5E] = {LSR, ABX, 7},
    [0X60] = {RTS, IMP, 6}, [0X61] = {ADC, IDX, 6}, [0X65] = {ADC, ZPG, 3},
    [0X66] = {ROR, ZPG, 5}, [0X68] = {PLA, IMP, 4}, [0X69] = {ADC, IMM, 2},
    [0X6A] = {ROR, ACC, 2}, [0X6C] = {JMP, JID, 5}, [0X6D] = {ADC, ABS, 4},
    [0X6E] = {ROR, ABS, 6}, [0X70] = {BVS, IMM, 2}, [0X71] = {ADC, IDY, 5},
    [0X75] = {ADC, ZPX, 4}, [0X76] = {ROR, ZPX, 6}, [0X78] = {SEI, IMP, 2},
    [0X79] = {ADC, ABY, 4}, [0X7D] = {ADC, ABX, 4}, [0X7E] = {ROR, ABX, 7},
    [0X81] = {STA, IDX, 6}, [0X84] = {STY, ZPG, 3}, [0X85] = {STA, ZPG, 3},
    [0X86] = {STX, ZPG, 3}, [0X88] = {DEY, IMP, 2}, [0X8A] = {TXA, IMP, 2},
    [0X8C] = {STY, ABS, 4}, [0X8D] = {STA, ABS, 4}, [0X8E] = {STX, ABS, 4},
    [0X90] = {BCC, IMM, 2}, [0X91] = {STA, IDY, 6}, [0X94] = {STY, ZPX, 4},
    [0X95] = {STA, ZPX, 4}, [0X96] = {STX, ZPY, 4}, [0X98] = {TYA, IMP, 2},
    [0X99] = {STA, ABY, 5}, [0X9A] = {TXS, IMP, 2}, [0X9D] = {STA, ABX, 5},
    [0XA0] = {LDY, IMM, 2}, [0XA1] = {LDA, IDX, 6}, [0XA2] = {LDX, IMM, 2},
    [0XA4] = {LDY, ZPG, 3}, [0XA5] = {LDA, ZPG, 3}, [0XA6] = {LDX, ZPG, 3},
    [0XA8] = {TAY, IMP, 2}, [0XA9] = {LDA, IMM, 2}, [0XAA] = {TAX, IMP, 2},
    [0XAC] = {LDY, ABS, 4}, [0XAD] = {LDA, ABS, 4}, [0XAE] = {LDX, ABS, 4},
    [0XB0] = {BCS, IMM, 2}, [0XB1] = {LDA, IDY, 5}, [0XB4] = {LDY, ZPX, 4},
    [0XB5] = {LDA, ZPX, 4}, [0XB6] = {LDX, ZPY, 4}, [0XB8] = {CLV, IMP, 2},
    [0XB9] = {LDA, ABY, 4}, [0XBA] = {TSX, IMP, 2}, [0XBC] = {LDY, ABX, 4},
    [0XBD] = {LDA, ABX, 4}, [0XBE] = {LDX, ABY, 4}, [0XC0] = {CPY, IMM, 2},
    [0XC1] = {CMP, IDX, 6}, [0XC4] = {CPY, ZPG, 3}, [0XC5] = {CMP, ZPG, 3},
    [0XC6] = {DEC, ZPG, 5}, [0XC8] = {INY, IMP, 2}, [0XC9] = {CMP, IMM, 2},
    [0XCA] = {DEX, IMP, 2}, [0XCC] = {CPY, ABS, 4}, [0XCD] = {CMP, ABS, 4},
    [0XCE] = {DEC, ABS, 6}, [0XD0] = {BNE, IMM, 2}, [0XD1] = {CMP, IDY, 5},
    [0XD5] = {CMP, ZPX, 4}, [0XD6] = {DEC, ZPX, 6}, [0XD8] = {CLD, IMP, 2},
    [0XD9] = {CMP, ABY, 4}, [0XDD] = {CMP, ABX, 4}, [0XDE] = {DEC, ABX, 7},
    [0XE0] = {CPX, IMM, 2}, [0XE1] = {SBC, IDX, 6}, [0XE4] = {CPX, ZPG, 3},
    [0XE5] = {SBC, ZPG, 3}, [0XE6] = {INC, ZPG, 5}, [0XE8] = {INX, IMP, 2},
    [0XE9] = {SBC, IMM, 2}, [0XEA] = {NOP, IMP, 2}, [0XEC] = {CPX, ABS, 4},
    [0XED] = {SBC, ABS, 4}, [0XEE] = {INC, ABS, 6}, [0XF0] = {BEQ, IMM, 2},
    [0XF1] = {SBC, IDY, 5}, [0XF5] = {SBC, ZPX, 4}, [0XF6] = {INC, ZPX, 6},
    [0XF8] = {SED, IMP, 2}, [0XF9] = {SBC, ABY, 4}, [0XFD] = {SBC, ABX, 4},
    [0XFE] = {INC, ABX, 7},
};

UInt cpuTick(Cpu *cpu, Bus *bus) {
  U8 op = busRead(bus, cpu->pc++);
  OpEntry const *entry = &OP_TBL[op];
  if (entry->exec) {
    entry->addr(cpu, bus);
    entry->exec(cpu, bus);
    return entry->cycles;
  }
  IMP(cpu, bus);
  NOP(cpu, bus);
  return 1;
}
