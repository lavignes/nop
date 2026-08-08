#include "emu.h"

static void flag(Cpu *cpu, U8 flag, Bool cond) {
  if (cond) {
    cpu->p |= flag;
  } else {
    cpu->p &= ~flag;
  }
}

static UInt IMP(Cpu *cpu, Bus *bus) { return 1; }

static UInt ACC(Cpu *cpu, Bus *bus) {
  cpu->ea = &cpu->a;
  return 2;
}

static UInt IMM(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  cpu->eaAddr = cpu->pc++;
  return 2;
}

static UInt ZPG(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  cpu->eaAddr = bus->read(bus->data, cpu->pc++);
  return 3;
}

static UInt ZPX(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  cpu->eaAddr = bus->read(bus->data, cpu->pc++) + cpu->x;
  return 4;
}

static UInt ZPY(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  cpu->eaAddr = bus->read(bus->data, cpu->pc++) + cpu->y;
  return 4;
}

static UInt ABS(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  U8 lo = bus->read(bus->data, cpu->pc++);
  U8 hi = bus->read(bus->data, cpu->pc++);
  cpu->eaAddr = (((U16)hi) << 8) | lo;
  return 4;
}

static UInt ABX(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  U8 lo = bus->read(bus->data, cpu->pc++);
  U8 hi = bus->read(bus->data, cpu->pc++);
  U16 base = (((U16)hi) << 8) | lo;
  cpu->eaAddr = base + cpu->x;
  return ((lo + cpu->x) > 0xFF) ? 5 : 4;
}

static UInt ABY(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  U8 lo = bus->read(bus->data, cpu->pc++);
  U8 hi = bus->read(bus->data, cpu->pc++);
  U16 base = (((U16)hi) << 8) | lo;
  cpu->eaAddr = base + cpu->y;
  return ((lo + cpu->y) > 0xFF) ? 5 : 4;
}

static UInt IDX(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  U8 zpAddr = bus->read(bus->data, cpu->pc++) + cpu->x;
  U8 lo = bus->read(bus->data, zpAddr);
  U8 hi = bus->read(bus->data, (U8)(zpAddr + 1));
  cpu->eaAddr = (((U16)hi) << 8) | lo;
  return 6;
}

static UInt IDY(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  U8 zpAddr = bus->read(bus->data, cpu->pc++);
  U8 lo = bus->read(bus->data, zpAddr);
  U8 hi = bus->read(bus->data, (U8)(zpAddr + 1));
  U16 base = (((U16)hi) << 8) | lo;
  cpu->eaAddr = base + cpu->y;
  return ((lo + cpu->y) > 0xFF) ? 6 : 5;
}

static UInt JAB(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  U8 lo = bus->read(bus->data, cpu->pc++);
  U8 hi = bus->read(bus->data, cpu->pc++);
  cpu->eaAddr = (((U16)hi) << 8) | lo;
  return 3;
}

static UInt JID(Cpu *cpu, Bus *bus) {
  cpu->ea = bus;
  U8 idLo = bus->read(bus->data, cpu->pc++);
  U8 idHi = bus->read(bus->data, cpu->pc++);
  U8 lo = bus->read(bus->data, (((U16)idHi) << 8) | idLo);
  U8 hi =
      bus->read(bus->data, (((U16)idHi) << 8) | (U8)(idLo + 1)); // wrapping bug
  cpu->eaAddr = (((U16)hi) << 8) | lo;
  return 5;
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

static UInt ADC(Cpu *cpu, Bus *bus) {
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
  return 1;
}

static UInt AND(Cpu *cpu, Bus *bus) {
  cpu->a &= read(cpu, bus);
  flag(cpu, CPU_FLAG_ZERO, cpu->a == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (cpu->a & 0x80) != 0);
  return 1;
}

static UInt ASL(Cpu *cpu, Bus *bus) {
  flag(cpu, CPU_FLAG_CARRY, (read(cpu, bus) & 0x80) != 0);
  U8 val = read(cpu, bus) << 1;
  write(cpu, bus, val);
  flag(cpu, CPU_FLAG_ZERO, val == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (val & 0x80) != 0);
  return 1;
}

static UInt BCC(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  UInt cycles = 0;
  if (!(cpu->p & CPU_FLAG_CARRY)) {
    U16 newPc = cpu->pc + (I8)val;
    cycles = 1;
    if ((cpu->pc ^ newPc) & 0xFF00) {
      cycles = 2;
    }
    cpu->pc = newPc;
  }
  return cycles;
}

static UInt BCS(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  UInt cycles = 0;
  if (cpu->p & CPU_FLAG_CARRY) {
    U16 newPc = cpu->pc + (I8)val;
    cycles = 1;
    if ((cpu->pc ^ newPc) & 0xFF00) {
      cycles = 2;
    }
    cpu->pc = newPc;
  }
  return cycles;
}

static UInt BEQ(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  UInt cycles = 0;
  if (cpu->p & CPU_FLAG_ZERO) {
    U16 newPc = cpu->pc + (I8)val;
    cycles = 1;
    if ((cpu->pc ^ newPc) & 0xFF00) {
      cycles = 2;
    }
    cpu->pc = newPc;
  }
  return cycles;
}

static UInt BIT(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  U8 res = cpu->a & val;
  flag(cpu, CPU_FLAG_ZERO, res == 0);
  flag(cpu, CPU_FLAG_OVERFLOW, (val & 0x40) != 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (val & 0x80) != 0);
  return 1;
}

static UInt BMI(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  UInt cycles = 0;
  if (cpu->p & CPU_FLAG_NEGATIVE) {
    U16 newPc = cpu->pc + (I8)val;
    cycles = 1;
    if ((cpu->pc ^ newPc) & 0xFF00) {
      cycles = 2;
    }
    cpu->pc = newPc;
  }
  return cycles;
}

static UInt BNE(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  UInt cycles = 0;
  if (!(cpu->p & CPU_FLAG_ZERO)) {
    U16 newPc = cpu->pc + (I8)val;
    cycles = 1;
    if ((cpu->pc ^ newPc) & 0xFF00) {
      cycles = 2;
    }
    cpu->pc = newPc;
  }
  return cycles;
}

static UInt BPL(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  UInt cycles = 0;
  if (!(cpu->p & CPU_FLAG_NEGATIVE)) {
    U16 newPc = cpu->pc + (I8)val;
    cycles = 1;
    if ((cpu->pc ^ newPc) & 0xFF00) {
      cycles = 2;
    }
    cpu->pc = newPc;
  }
  return cycles;
}

static UInt BRK(Cpu *cpu, Bus *bus) {
  ++cpu->pc;
  bus->write(bus->data, 0x0100 + cpu->sp--, (U8)((cpu->pc >> 8) & 0xFF));
  bus->write(bus->data, 0x0100 + cpu->sp--, (U8)(cpu->pc & 0xFF));
  bus->write(bus->data, 0x0100 + cpu->sp--,
             cpu->p | CPU_FLAG_BREAK | CPU_FLAG_UNUSED);
  flag(cpu, CPU_FLAG_INTERRUPT, TRUE);
  U8 lo = bus->read(bus->data, 0xFFFE);
  U8 hi = bus->read(bus->data, 0xFFFF);
  cpu->pc = (((U16)hi) << 8) | lo;
  return 6;
}

static UInt BVC(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  UInt cycles = 0;
  if (!(cpu->p & CPU_FLAG_OVERFLOW)) {
    U16 newPc = cpu->pc + (I8)val;
    cycles = 1;
    if ((cpu->pc ^ newPc) & 0xFF00) {
      cycles = 2;
    }
    cpu->pc = newPc;
  }
  return cycles;
}

static UInt BVS(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  UInt cycles = 0;
  if (cpu->p & CPU_FLAG_OVERFLOW) {
    U16 newPc = cpu->pc + (I8)val;
    cycles = 1;
    if ((cpu->pc ^ newPc) & 0xFF00) {
      cycles = 2;
    }
    cpu->pc = newPc;
  }
  return cycles;
}

static UInt CLC(Cpu *cpu, Bus *bus) {
  (void)bus;
  flag(cpu, CPU_FLAG_CARRY, FALSE);
  return 1;
}

static UInt CLD(Cpu *cpu, Bus *bus) {
  (void)bus;
  flag(cpu, CPU_FLAG_DECIMAL, FALSE);
  return 1;
}

static UInt CLI(Cpu *cpu, Bus *bus) {
  (void)bus;
  flag(cpu, CPU_FLAG_INTERRUPT, FALSE);
  return 1;
}

static UInt CLV(Cpu *cpu, Bus *bus) {
  (void)bus;
  flag(cpu, CPU_FLAG_OVERFLOW, FALSE);
  return 1;
}

static UInt CMP(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  U8 res = cpu->a - val;
  flag(cpu, CPU_FLAG_CARRY, cpu->a >= val);
  flag(cpu, CPU_FLAG_ZERO, res == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (res & 0x80) != 0);
  return 1;
}

static UInt CPX(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  U8 res = cpu->x - val;
  flag(cpu, CPU_FLAG_CARRY, cpu->x >= val);
  flag(cpu, CPU_FLAG_ZERO, res == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (res & 0x80) != 0);
  return 1;
}

static UInt CPY(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  U8 res = cpu->y - val;
  flag(cpu, CPU_FLAG_CARRY, cpu->y >= val);
  flag(cpu, CPU_FLAG_ZERO, res == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (res & 0x80) != 0);
  return 1;
}

static UInt DEC(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus) - 1;
  write(cpu, bus, val);
  flag(cpu, CPU_FLAG_ZERO, val == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (val & 0x80) != 0);
  return 1;
}

static UInt DEX(Cpu *cpu, Bus *bus) {
  (void)bus;
  U8 val = cpu->x - 1;
  flag(cpu, CPU_FLAG_ZERO, val == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (val & 0x80) != 0);
  return 1;
}

static UInt DEY(Cpu *cpu, Bus *bus) {
  (void)bus;
  U8 val = cpu->y - 1;
  flag(cpu, CPU_FLAG_ZERO, val == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (val & 0x80) != 0);
  return 1;
}

static UInt EOR(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  cpu->a ^= val;
  flag(cpu, CPU_FLAG_ZERO, cpu->a == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (cpu->a & 0x80) != 0);
  return 1;
}

static UInt INC(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus) + 1;
  flag(cpu, CPU_FLAG_ZERO, val == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (val & 0x80) != 0);
  return 1;
}

static UInt INX(Cpu *cpu, Bus *bus) {
  ++cpu->x;
  flag(cpu, CPU_FLAG_ZERO, cpu->x == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (cpu->x & 0x80) != 0);
  return 1;
}

static UInt INY(Cpu *cpu, Bus *bus) {
  ++cpu->y;
  flag(cpu, CPU_FLAG_ZERO, cpu->y == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (cpu->y & 0x80) != 0);
  return 1;
}

static UInt JMP(Cpu *cpu, Bus *bus) {
  (void)bus;
  cpu->pc = cpu->eaAddr;
  return 0;
}

static UInt JSR(Cpu *cpu, Bus *bus) {
  U16 ret = cpu->pc - 1;
  bus->write(bus->data, 0x0100 + cpu->sp--, (U8)((ret >> 8) & 0xFF));
  bus->write(bus->data, 0x0100 + cpu->sp--, (U8)(ret & 0xFF));
  cpu->pc = cpu->eaAddr;
  return 5;
}

static UInt LDA(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  flag(cpu, CPU_FLAG_ZERO, val == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (val & 0x80) != 0);
  cpu->a = val;
  return 1;
}

static UInt LDX(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  flag(cpu, CPU_FLAG_ZERO, val == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (val & 0x80) != 0);
  cpu->x = val;
  return 1;
}

static UInt LDY(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  flag(cpu, CPU_FLAG_ZERO, val == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (val & 0x80) != 0);
  cpu->y = val;
  return 1;
}

static UInt LSR(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  flag(cpu, CPU_FLAG_CARRY, (val & 0x01) != 0);
  val >>= 1;
  write(cpu, bus, val);
  flag(cpu, CPU_FLAG_ZERO, val == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, FALSE);
  return 1;
}

static UInt NOP(Cpu *cpu, Bus *bus) {
  (void)cpu;
  (void)bus;
  return 1;
}

static UInt ORA(Cpu *cpu, Bus *bus) {
  cpu->a |= read(cpu, bus);
  flag(cpu, CPU_FLAG_ZERO, cpu->a == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (cpu->a & 0x80) != 0);
  return 1;
}

static UInt PHA(Cpu *cpu, Bus *bus) {
  bus->write(bus->data, 0x0100 + cpu->sp--, cpu->a);
  return 2;
}

static UInt PHP(Cpu *cpu, Bus *bus) {
  bus->write(bus->data, 0x0100 + cpu->sp--,
             cpu->p | CPU_FLAG_BREAK | CPU_FLAG_UNUSED);
  return 2;
}

static UInt PLA(Cpu *cpu, Bus *bus) {
  cpu->a = bus->read(bus->data, 0x0100 + ++cpu->sp);
  flag(cpu, CPU_FLAG_ZERO, cpu->a == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (cpu->a & 0x80) != 0);
  return 3;
}

static UInt PLP(Cpu *cpu, Bus *bus) {
  cpu->p = bus->read(bus->data, 0x0100 + ++cpu->sp) &
           ~(CPU_FLAG_BREAK | CPU_FLAG_UNUSED);
  return 3;
}

static UInt ROL(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  Bool cy = (cpu->p & CPU_FLAG_CARRY) != 0;
  flag(cpu, CPU_FLAG_CARRY, (val & 0x80) != 0);
  val = (val << 1) | (cy ? 1 : 0);
  write(cpu, bus, val);
  flag(cpu, CPU_FLAG_ZERO, val == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (val & 0x80) != 0);
  return 1;
}

static UInt ROR(Cpu *cpu, Bus *bus) {
  U8 val = read(cpu, bus);
  Bool cy = (cpu->p & CPU_FLAG_CARRY) != 0;
  flag(cpu, CPU_FLAG_CARRY, (val & 0x01) != 0);
  val = (val >> 1) | (cy ? 0x80 : 0);
  write(cpu, bus, val);
  flag(cpu, CPU_FLAG_ZERO, val == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (val & 0x80) != 0);
  return 1;
}

static UInt RTI(Cpu *cpu, Bus *bus) {
  cpu->p = bus->read(bus->data, 0x0100 + ++cpu->sp) &
           ~(CPU_FLAG_BREAK | CPU_FLAG_UNUSED);
  U8 lo = bus->read(bus->data, 0x0100 + ++cpu->sp);
  U8 hi = bus->read(bus->data, 0x0100 + ++cpu->sp);
  cpu->pc = (((U16)hi) << 8) | lo;
  return 5;
}

static UInt RTS(Cpu *cpu, Bus *bus) {
  U8 lo = bus->read(bus->data, 0x0100 + ++cpu->sp);
  U8 hi = bus->read(bus->data, 0x0100 + ++cpu->sp);
  cpu->pc = ((((U16)hi) << 8) | lo) + 1;
  return 5;
}

static UInt SBC(Cpu *cpu, Bus *bus) {
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
  return 1;
}

static UInt SEC(Cpu *cpu, Bus *bus) {
  (void)bus;
  flag(cpu, CPU_FLAG_CARRY, TRUE);
  return 1;
}

static UInt SED(Cpu *cpu, Bus *bus) {
  (void)bus;
  flag(cpu, CPU_FLAG_DECIMAL, TRUE);
  return 1;
}

static UInt SEI(Cpu *cpu, Bus *bus) {
  (void)bus;
  flag(cpu, CPU_FLAG_INTERRUPT, TRUE);
  return 1;
}

static UInt STA(Cpu *cpu, Bus *bus) {
  write(cpu, bus, cpu->a);
  return 1;
}

static UInt STX(Cpu *cpu, Bus *bus) {
  write(cpu, bus, cpu->x);
  return 1;
}

static UInt STY(Cpu *cpu, Bus *bus) {
  write(cpu, bus, cpu->y);
  return 1;
}

static UInt TAX(Cpu *cpu, Bus *bus) {
  (void)bus;
  cpu->x = cpu->a;
  flag(cpu, CPU_FLAG_ZERO, cpu->x == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (cpu->x & 0x80) != 0);
  return 1;
}

static UInt TAY(Cpu *cpu, Bus *bus) {
  (void)bus;
  cpu->y = cpu->a;
  flag(cpu, CPU_FLAG_ZERO, cpu->y == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (cpu->y & 0x80) != 0);
  return 1;
}

static UInt TSX(Cpu *cpu, Bus *bus) {
  (void)bus;
  cpu->x = cpu->sp;
  flag(cpu, CPU_FLAG_ZERO, cpu->x == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (cpu->x & 0x80) != 0);
  return 1;
}

static UInt TXA(Cpu *cpu, Bus *bus) {
  (void)bus;
  cpu->a = cpu->x;
  flag(cpu, CPU_FLAG_ZERO, cpu->a == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (cpu->a & 0x80) != 0);
  return 1;
}

static UInt TXS(Cpu *cpu, Bus *bus) {
  (void)bus;
  cpu->sp = cpu->x;
  return 1;
}

static UInt TYA(Cpu *cpu, Bus *bus) {
  (void)bus;
  cpu->a = cpu->y;
  flag(cpu, CPU_FLAG_ZERO, cpu->a == 0);
  flag(cpu, CPU_FLAG_NEGATIVE, (cpu->a & 0x80) != 0);
  return 1;
}

typedef struct {
  UInt (*exec)(Cpu *cpu, Bus *bus);
  UInt (*addr)(Cpu *cpu, Bus *bus);
} OpEntry;

static OpEntry const OP_TBL[256] = {
    [0X00] = {BRK, IMM}, [0X01] = {ORA, IDX}, [0X05] = {ORA, ZPG},
    [0X06] = {ASL, ZPG}, [0X08] = {PHP, IMP}, [0X09] = {ORA, IMM},
    [0X0A] = {ASL, ACC}, [0X0D] = {ORA, ABS}, [0X0E] = {ASL, ABS},
    [0X10] = {BPL, IMM}, [0X11] = {ORA, IDY}, [0X15] = {ORA, ZPX},
    [0X16] = {ASL, ZPX}, [0X18] = {CLC, IMP}, [0X19] = {ORA, ABY},
    [0X1D] = {ORA, ABX}, [0X1E] = {ASL, ABX}, [0X20] = {JSR, JAB},
    [0X21] = {AND, IDX}, [0X24] = {BIT, ZPG}, [0X25] = {AND, ZPG},
    [0X26] = {ROL, ZPG}, [0X28] = {PLP, IMP}, [0X29] = {AND, IMM},
    [0X2A] = {ROL, ACC}, [0X2C] = {BIT, ABS}, [0X2D] = {AND, ABS},
    [0X2E] = {ROL, ABS}, [0X30] = {BMI, IMM}, [0X31] = {AND, IDY},
    [0X35] = {AND, ZPX}, [0X36] = {ROL, ZPX}, [0X38] = {SEC, IMP},
    [0X39] = {AND, ABY}, [0X3D] = {AND, ABX}, [0X3E] = {ROL, ABX},
    [0X40] = {RTI, IMP}, [0X41] = {EOR, IDX}, [0X45] = {EOR, ZPG},
    [0X46] = {LSR, ZPG}, [0X48] = {PHA, IMP}, [0X49] = {EOR, IMM},
    [0X4A] = {LSR, ACC}, [0X4C] = {JMP, JAB}, [0X4D] = {EOR, ABS},
    [0X4E] = {LSR, ABS}, [0X50] = {BVC, IMM}, [0X51] = {EOR, IDY},
    [0X55] = {EOR, ZPX}, [0X56] = {LSR, ZPX}, [0X58] = {CLI, IMP},
    [0X59] = {EOR, ABY}, [0X5D] = {EOR, ABX}, [0X5E] = {LSR, ABX},
    [0X60] = {RTS, IMP}, [0X61] = {ADC, IDX}, [0X65] = {ADC, ZPG},
    [0X66] = {ROR, ZPG}, [0X68] = {PLA, IMP}, [0X69] = {ADC, IMM},
    [0X6A] = {ROR, ACC}, [0X6C] = {JMP, JID}, [0X6D] = {ADC, ABS},
    [0X6E] = {ROR, ABS}, [0X70] = {BVS, IMM}, [0X71] = {ADC, IDY},
    [0X75] = {ADC, ZPX}, [0X76] = {ROR, ZPX}, [0X78] = {SEI, IMP},
    [0X79] = {ADC, ABY}, [0X7D] = {ADC, ABX}, [0X7E] = {ROR, ABX},
    [0X81] = {STA, IDX}, [0X84] = {STY, ZPG}, [0X85] = {STA, ZPG},
    [0X86] = {STX, ZPG}, [0X88] = {DEY, IMP}, [0X8A] = {TXA, IMP},
    [0X8C] = {STY, ABS}, [0X8D] = {STA, ABS}, [0X8E] = {STX, ABS},
    [0X90] = {BCC, IMM}, [0X91] = {STA, IDY}, [0X94] = {STY, ZPX},
    [0X95] = {STA, ZPX}, [0X96] = {STX, ZPY}, [0X98] = {TYA, IMP},
    [0X99] = {STA, ABY}, [0X9A] = {TXS, IMP}, [0X9D] = {STA, ABX},
    [0XA0] = {LDY, IMM}, [0XA1] = {LDA, IDX}, [0XA2] = {LDX, IMM},
    [0XA4] = {LDY, ZPG}, [0XA5] = {LDA, ZPG}, [0XA6] = {LDX, ZPG},
    [0XA8] = {TAY, IMP}, [0XA9] = {LDA, IMM}, [0XAA] = {TAX, IMP},
    [0XAC] = {LDY, ABS}, [0XAD] = {LDA, ABS}, [0XAE] = {LDX, ABS},
    [0XB0] = {BCS, IMM}, [0XB1] = {LDA, IDY}, [0XB4] = {LDY, ZPX},
    [0XB5] = {LDA, ZPX}, [0XB6] = {LDX, ZPY}, [0XB8] = {CLV, IMP},
    [0XB9] = {LDA, ABY}, [0XBA] = {TSX, IMP}, [0XBC] = {LDY, ABX},
    [0XBD] = {LDA, ABX}, [0XBE] = {LDX, ABY}, [0XC0] = {CPY, IMM},
    [0XC1] = {CMP, IDX}, [0XC4] = {CPY, ZPG}, [0XC5] = {CMP, ZPG},
    [0XC6] = {DEC, ZPG}, [0XC8] = {INY, IMP}, [0XC9] = {CMP, IMM},
    [0XCA] = {DEX, IMP}, [0XCC] = {CPY, ABS}, [0XCD] = {CMP, ABS},
    [0XCE] = {DEC, ABS}, [0XD0] = {BNE, IMM}, [0XD1] = {CMP, IDY},
    [0XD5] = {CMP, ZPX}, [0XD6] = {DEC, ZPX}, [0XD8] = {CLD, IMP},
    [0XD9] = {CMP, ABY}, [0XDD] = {CMP, ABX}, [0XDE] = {DEC, ABX},
    [0XE0] = {CPX, IMM}, [0XE1] = {SBC, IDX}, [0XE4] = {CPX, ZPG},
    [0XE5] = {SBC, ZPG}, [0XE6] = {INC, ZPG}, [0XE8] = {INX, IMP},
    [0XE9] = {SBC, IMM}, [0XEA] = {NOP, IMP}, [0XEC] = {CPX, ABS},
    [0XED] = {SBC, ABS}, [0XEE] = {INC, ABS}, [0XF0] = {BEQ, IMM},
    [0XF1] = {SBC, IDY}, [0XF5] = {SBC, ZPX}, [0XF6] = {INC, ZPX},
    [0XF8] = {SED, IMP}, [0XF9] = {SBC, ABY}, [0XFD] = {SBC, ABX},
    [0XFE] = {INC, ABX},
};

UInt cpuTick(Cpu *cpu, Bus *bus) {
  U8 op = bus->read(bus->data, cpu->pc++);
  OpEntry const *entry = &OP_TBL[op];
  if (entry->exec) {
    return entry->addr(cpu, bus) + entry->exec(cpu, bus);
  }
  return IMP(cpu, bus) + NOP(cpu, bus);
}
