#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t U8;
typedef int8_t I8;

#define U8_MAX UINT8_MAX
#define I8_MAX INT8_MAX
#define I8_MIN INT8_MIN

typedef uint16_t U16;
typedef int16_t I16;

#define U16_MAX UINT16_MAX
#define I16_MAX INT16_MAX
#define I16_MIN INT16_MIN

#undef UINT_MAX
#undef INT_MAX
#undef INT_MIN

typedef uintptr_t UInt;
typedef intptr_t Int;
#define UINT_MAX UINTPTR_MAX
#define INT_MAX INTPTR_MAX
#define INT_MIN INTPTR_MIN

enum {
  FLAG_CARRY = 1 << 0,
  FLAG_ZERO = 1 << 1,
  FLAG_INTERRUPT = 1 << 2,
  FLAG_DECIMAL = 1 << 3,
  FLAG_BREAK = 1 << 4,
  FLAG_UNUSED = 1 << 5,
  FLAG_OVERFLOW = 1 << 6,
  FLAG_NEGATIVE = 1 << 7,
};

U8 A;
U8 X;
U8 Y;
U8 P;
U8 SP;
U16 PC = 0x0200;

bool debug = false;

U8 MEM[65536];

void help(char const *name) { (void)name; }

void tick();
void debugger();

int main(int argc, char *argv[]) {
  FILE *rom;
  if (argc < 2) {
    help(argv[0]);
    return EXIT_FAILURE;
  }
  for (int argi = 1; argi < argc; ++argi) {
    if ((strcmp(argv[argi], "-h") == 0) ||
        (strcmp(argv[argi], "--help") == 0)) {
      help(argv[0]);
      return EXIT_SUCCESS;
    }
    if ((strcmp(argv[argi], "-d") == 0) ||
        (strcmp(argv[argi], "--debug") == 0)) {
      debug = true;
      continue;
    }
    rom = fopen(argv[argi], "rb");
    if (!rom) {
      fprintf(stderr, "Could not open ROM file: %s\n", argv[argi]);
      return EXIT_FAILURE;
    }
    ++argi;
    if (argi != argc) {
      fprintf(stderr, "Unexpected option: %s\n", argv[argi]);
      return EXIT_FAILURE;
    }
  }

  long read = fread(MEM + PC, 1, sizeof(MEM) - PC, rom);
  if (read != (sizeof(MEM) - PC)) {
    int err = ferror(rom);
    if (err) {
      fprintf(stderr, "Error reading ROM file: %s\n", strerror(err));
      return EXIT_FAILURE;
    }
  }
  fclose(rom);

  while (true) {
    tick();
  }

  return EXIT_SUCCESS;
}

void doop();

typedef struct Breakpoint Breakpoint;

struct Breakpoint {
  Breakpoint *next;
  U16 num;
  U16 addr;
};

U16 BREAKPOINT_COUNTER = 0;
Breakpoint *BREAKPOINTS = NULL;

void tick() {
  for (Breakpoint const *bp = BREAKPOINTS; bp != NULL; bp = bp->next) {
    if (PC == bp->addr) {
      debug = true;
      break;
    }
  }
  if (debug) {
    debugger();
  }
  // TODO: check for interrupts here
  doop();
  // TODO: read/write memory-mapped IO here
}

Int parse(char const *str) {
  long val;
  if (str[0] == '$') {
    val = strtol(str + 1, NULL, 16);
  } else if (str[0] == '%') {
    val = strtol(str + 1, NULL, 2);
  } else {
    val = strtol(str, NULL, 10);
  }
  if (val == LONG_MAX) {
    return INT_MAX;
  }
  return (Int)val;
}

U16 disasm(U16 addr);
void regs();

void debugger() {
  static char *line = NULL;
  static size_t len = 0;
  regs();
  disasm(PC);
  while (true) {
    fprintf(stderr, "> ");
    if (getline(&line, &len, stdin) == -1) {
      exit(EXIT_FAILURE);
    }
    char *tok = strtok(line, " \t\n");
    if (!tok) {
      continue;
    }
    if ((strcmp(tok, "q") == 0) || (strcmp(tok, "quit") == 0)) {
      exit(EXIT_SUCCESS);
    }
    if ((strcmp(tok, "c") == 0) || (strcmp(tok, "continue") == 0)) {
      debug = false;
      return;
    }
    if ((strcmp(tok, "s") == 0) || (strcmp(tok, "step") == 0)) {
      break;
    }
    if ((strcmp(tok, "b") == 0) || (strcmp(tok, "break") == 0)) {
      tok = strtok(NULL, " \t\n");
      if (!tok) {
        fprintf(stderr, "No address provided for breakpoint\n");
        continue;
      }
      Int addr = parse(tok);
      if ((addr == INT_MAX) || (addr > U16_MAX)) {
        fprintf(stderr, "Invalid address for breakpoint: %s\n", tok);
        continue;
      }
      ++BREAKPOINT_COUNTER;
      Breakpoint *bp = malloc(sizeof(Breakpoint));
      bp->num = BREAKPOINT_COUNTER;
      bp->addr = (U16)addr;
      bp->next = BREAKPOINTS;
      BREAKPOINTS = bp;
      fprintf(stderr, "Breakpoint %d set at $%04X\n", bp->num, bp->addr);
      continue;
    }
    if (strcmp(tok, "del") == 0) {
      tok = strtok(NULL, " \t\n");
      if (!tok) {
        fprintf(stderr, "No breakpoint number provided for deletion\n");
        continue;
      }
      Int num = parse(tok);
      if ((num == INT_MAX) || (num > U16_MAX)) {
        fprintf(stderr, "Invalid breakpoint number: %s\n", tok);
        continue;
      }
      Breakpoint **prevptr = &BREAKPOINTS;
      Breakpoint *bp = BREAKPOINTS;
      while (bp != NULL) {
        if (bp->num == (U16)num) {
          *prevptr = bp->next;
          free(bp);
          fprintf(stderr, "Breakpoint %d deleted\n", (U16)num);
          break;
        }
        prevptr = &bp->next;
        bp = bp->next;
      }
      if (bp == NULL) {
        fprintf(stderr, "No breakpoint with number %d\n", (U16)num);
      }
      continue;
    }
    if ((strcmp(tok, "r") == 0) || (strcmp(tok, "regs") == 0)) {
      regs();
      continue;
    }
    if (strcmp(tok, "x") == 0) {
      Int start = PC;
      tok = strtok(NULL, " \t\n");
      if (tok) {
        start = parse(tok);
        if ((start == INT_MAX) || (start > U16_MAX)) {
          fprintf(stderr, "Invalid address for examine: %s\n", tok);
          continue;
        }
      }
      Int end = ((start + 15) > U16_MAX) ? U16_MAX : (start + 15);
      tok = strtok(NULL, " \t\n");
      if (tok) {
        end = parse(tok);
        if ((end == INT_MAX) || (end > U16_MAX) || (end < start)) {
          fprintf(stderr, "Invalid end address for examine: %s\n", tok);
          continue;
        }
      }
      while (start <= end) {
        fprintf(stderr, "%04X ", (U16)start);
        for (UInt i = 0; i < 16; ++i) {
          if (i == 8) {
            fprintf(stderr, " ");
          }
          if ((start + i) > end) {
            fprintf(stderr, "   ");
            continue;
          }
          fprintf(stderr, " %02X", MEM[(U16)(start + i)]);
        }
        fprintf(stderr, "  |");
        for (UInt i = 0; i < 16; ++i) {
          if ((start + i) > end) {
            fprintf(stderr, " ");
            continue;
          }
          U8 byte = MEM[(U16)(start + i)];
          if (isprint(byte)) {
            fprintf(stderr, "%c", (char)byte);
            continue;
          } else {
            fprintf(stderr, ".");
          }
        }
        fprintf(stderr, "|\n");
        start += 16;
      }
      continue;
    }
    if ((strcmp(tok, "dis") == 0) || (strcmp(tok, "disasm") == 0)) {
      Int start = PC;
      tok = strtok(NULL, " \t\n");
      if (tok) {
        start = parse(tok);
        if ((start == INT_MAX) || (start > U16_MAX)) {
          fprintf(stderr, "Invalid address for disasm: %s\n", tok);
          continue;
        }
      }
      Int end = start;
      tok = strtok(NULL, " \t\n");
      if (tok) {
        end = parse(tok);
        if ((end == INT_MAX) || (end > U16_MAX) || (end < start)) {
          fprintf(stderr, "Invalid end address for disasm: %s\n", tok);
          continue;
        }
      }
      U16 addr = (U16)start;
      while (addr <= (U16)end) {
        addr = disasm(addr);
      }
      continue;
    }
    fprintf(stderr, "Unknown command: %s\n", tok);
  }
}

void regs() {
  fprintf(stderr, "PC:$%04X S:$%02X A:$%02X X:$%02X Y:$%02X P:$%02X |", PC, SP,
          A, X, Y, P);
  fprintf(stderr, "%c%c%c%c%c%c%c%c|\n", (P & FLAG_NEGATIVE) ? 'N' : '.',
          (P & FLAG_OVERFLOW) ? 'V' : '.', (P & FLAG_UNUSED) ? '1' : '.',
          (P & FLAG_BREAK) ? 'B' : '.', (P & FLAG_DECIMAL) ? 'D' : '.',
          (P & FLAG_INTERRUPT) ? 'I' : '.', (P & FLAG_ZERO) ? 'Z' : '.',
          (P & FLAG_CARRY) ? 'C' : '.');
}

U16 disimpl(U8 op, U16 addr, char const *mne) {
  fprintf(stderr, " %02X      ", op);
  fprintf(stderr, "  %s\n", mne);
  return addr;
}

U16 disimm(U8 op, U16 addr, char const *mne) {
  U8 val = MEM[addr++];
  fprintf(stderr, " %02X %02X   ", op, val);
  fprintf(stderr, "  %s #$%02X\n", mne, val);
  return addr;
}

U16 diszp(U8 op, U16 addr, char const *mne) {
  U8 zp = MEM[addr++];
  fprintf(stderr, " %02X %02X   ", op, zp);
  fprintf(stderr, "  %s $%02X\n", mne, zp);
  return addr;
}

U16 diszpx(U8 op, U16 addr, char const *mne) {
  U8 zp = MEM[addr++];
  fprintf(stderr, " %02X %02X   ", op, zp);
  fprintf(stderr, "  %s $%02X,X\n", mne, zp);
  return addr;
}

U16 diszpy(U8 op, U16 addr, char const *mne) {
  U8 zp = MEM[addr++];
  fprintf(stderr, " %02X %02X   ", op, zp);
  fprintf(stderr, "  %s $%02X,Y\n", mne, zp);
  return addr;
}

U16 disab(U8 op, U16 addr, char const *mne) {
  U8 lo = MEM[addr++];
  U8 hi = MEM[addr++];
  fprintf(stderr, " %02X %02X %02X", op, lo, hi);
  U16 ab = (((U16)hi) << 8) | lo;
  fprintf(stderr, "  %s $%04X\n", mne, ab);
  return addr;
}

U16 disabx(U8 op, U16 addr, char const *mne) {
  U8 lo = MEM[addr++];
  U8 hi = MEM[addr++];
  fprintf(stderr, " %02X %02X %02X", op, lo, hi);
  U16 ab = (((U16)hi) << 8) | lo;
  fprintf(stderr, "  %s $%04X,X\n", mne, ab);
  return addr;
}

U16 disaby(U8 op, U16 addr, char const *mne) {
  U8 lo = MEM[addr++];
  U8 hi = MEM[addr++];
  fprintf(stderr, " %02X %02X %02X", op, lo, hi);
  U16 ab = (((U16)hi) << 8) | lo;
  fprintf(stderr, "  %s $%04X,Y\n", mne, ab);
  return addr;
}

U16 disid(U8 op, U16 addr, char const *mne) {
  U8 lo = MEM[addr++];
  U8 hi = MEM[addr++];
  fprintf(stderr, " %02X %02X %02X", op, lo, hi);
  U16 ptr = (((U16)hi) << 8) | lo;
  fprintf(stderr, "  %s ($%04X)\n", mne, ptr);
  return addr;
}

U16 disidx(U8 op, U16 addr, char const *mne) {
  U8 zp = MEM[addr++];
  fprintf(stderr, " %02X %02X   ", op, zp);
  fprintf(stderr, "  %s ($%02X,X)\n", mne, zp);
  return addr;
}

U16 disidy(U8 op, U16 addr, char const *mne) {
  U8 zp = MEM[addr++];
  fprintf(stderr, " %02X %02X   ", op, zp);
  fprintf(stderr, "  %s ($%02X),Y\n", mne, zp);
  return addr;
}

U16 disrel(U8 op, U16 addr, char const *mne) {
  I8 offset = (I8)MEM[addr++];
  U16 target = addr + offset;
  fprintf(stderr, " %02X %02X   ", op, (U8)offset);
  fprintf(stderr, "  %s $%04X\n", mne, target);
  return addr;
}

U16 disasm(U16 addr) {
  fprintf(stderr, "%04X ", addr);
  U8 op = MEM[addr++];
  switch (op) {
  case 0x00:
    return disimm(op, addr, "BRK");
  case 0x01:
    return disidx(op, addr, "ORA");
  case 0x05:
    return diszp(op, addr, "ORA");
  case 0x06:
    return diszp(op, addr, "ASL");
  case 0x08:
    return disimpl(op, addr, "PHP");
  case 0x09:
    return disimm(op, addr, "ORA");
  case 0x0A:
    return disimpl(op, addr, "ASL");
  case 0x0D:
    return disab(op, addr, "ORA");
  case 0x0E:
    return disab(op, addr, "ASL");
  case 0x10:
    return disrel(op, addr, "BPL");
  case 0x11:
    return disidy(op, addr, "ORA");
  case 0x15:
    return diszpx(op, addr, "ORA");
  case 0x16:
    return diszpx(op, addr, "ASL");
  case 0x18:
    return disimpl(op, addr, "CLC");
  case 0x19:
    return disaby(op, addr, "ORA");
  case 0x1D:
    return disabx(op, addr, "ORA");
  case 0x1E:
    return disabx(op, addr, "ASL");
  case 0x20:
    return disab(op, addr, "JSR");
  case 0x21:
    return disidx(op, addr, "AND");
  case 0x24:
    return diszp(op, addr, "BIT");
  case 0x25:
    return diszp(op, addr, "AND");
  case 0x26:
    return diszp(op, addr, "ROL");
  case 0x28:
    return disimpl(op, addr, "PLP");
  case 0x29:
    return disimm(op, addr, "AND");
  case 0x2A:
    return disimpl(op, addr, "ROL");
  case 0x2C:
    return disab(op, addr, "BIT");
  case 0x2D:
    return disab(op, addr, "AND");
  case 0x2E:
    return disab(op, addr, "ROL");
  case 0x30:
    return disrel(op, addr, "BMI");
  case 0x31:
    return disidy(op, addr, "AND");
  case 0x35:
    return diszpx(op, addr, "AND");
  case 0x36:
    return diszpx(op, addr, "ROL");
  case 0x38:
    return disimpl(op, addr, "SEC");
  case 0x39:
    return disaby(op, addr, "AND");
  case 0x3D:
    return disabx(op, addr, "AND");
  case 0x3E:
    return disabx(op, addr, "ROL");
  case 0x40:
    return disimpl(op, addr, "RTI");
  case 0x41:
    return disidx(op, addr, "EOR");
  case 0x45:
    return diszp(op, addr, "EOR");
  case 0x46:
    return diszp(op, addr, "LSR");
  case 0x48:
    return disimpl(op, addr, "PHA");
  case 0x49:
    return disimm(op, addr, "EOR");
  case 0x4A:
    return disimpl(op, addr, "LSR");
  case 0x4C:
    return disab(op, addr, "JMP");
  case 0x4D:
    return disab(op, addr, "EOR");
  case 0x4E:
    return disab(op, addr, "LSR");
  case 0x50:
    return disrel(op, addr, "BVC");
  case 0x51:
    return disidy(op, addr, "EOR");
  case 0x55:
    return diszpx(op, addr, "EOR");
  case 0x56:
    return diszpx(op, addr, "LSR");
  case 0x58:
    return disimpl(op, addr, "CLI");
  case 0x59:
    return disaby(op, addr, "EOR");
  case 0x5D:
    return disabx(op, addr, "EOR");
  case 0x5E:
    return disabx(op, addr, "LSR");
  case 0x60:
    return disimpl(op, addr, "RTS");
  case 0x61:
    return disidx(op, addr, "ADC");
  case 0x65:
    return diszp(op, addr, "ADC");
  case 0x66:
    return diszp(op, addr, "ROR");
  case 0x68:
    return disimpl(op, addr, "PLA");
  case 0x69:
    return disimm(op, addr, "ADC");
  case 0x6A:
    return disimpl(op, addr, "ROR");
  case 0x6C:
    return disid(op, addr, "JMP");
  case 0x6D:
    return disab(op, addr, "ADC");
  case 0x6E:
    return disab(op, addr, "ROR");
  case 0x70:
    return disrel(op, addr, "BVS");
  case 0x71:
    return disidy(op, addr, "ADC");
  case 0x75:
    return diszpx(op, addr, "ADC");
  case 0x76:
    return diszpx(op, addr, "ROR");
  case 0x78:
    return disimpl(op, addr, "SEI");
  case 0x79:
    return disaby(op, addr, "ADC");
  case 0x7D:
    return disabx(op, addr, "ADC");
  case 0x7E:
    return disabx(op, addr, "ROR");
  case 0x81:
    return disidx(op, addr, "STA");
  case 0x84:
    return diszp(op, addr, "STY");
  case 0x85:
    return diszp(op, addr, "STA");
  case 0x86:
    return diszp(op, addr, "STX");
  case 0x88:
    return disimpl(op, addr, "DEY");
  case 0x8A:
    return disimpl(op, addr, "TXA");
  case 0x8C:
    return disab(op, addr, "STY");
  case 0x8D:
    return disab(op, addr, "STA");
  case 0x8E:
    return disab(op, addr, "STX");
  case 0x90:
    return disrel(op, addr, "BCC");
  case 0x91:
    return disidy(op, addr, "STA");
  case 0x94:
    return diszpx(op, addr, "STY");
  case 0x95:
    return diszpx(op, addr, "STA");
  case 0x96:
    return diszpy(op, addr, "STX");
  case 0x98:
    return disimpl(op, addr, "TYA");
  case 0x99:
    return disaby(op, addr, "STA");
  case 0x9A:
    return disimpl(op, addr, "TXS");
  case 0x9D:
    return disabx(op, addr, "STA");
  case 0xA0:
    return disimm(op, addr, "LDY");
  case 0xA1:
    return disidx(op, addr, "LDA");
  case 0xA2:
    return disimm(op, addr, "LDX");
  case 0xA4:
    return diszp(op, addr, "LDY");
  case 0xA5:
    return diszp(op, addr, "LDA");
  case 0xA6:
    return diszp(op, addr, "LDX");
  case 0xA8:
    return disimpl(op, addr, "TAY");
  case 0xA9:
    return disimm(op, addr, "LDA");
  case 0xAA:
    return disimpl(op, addr, "TAX");
  case 0xAC:
    return disab(op, addr, "LDY");
  case 0xAD:
    return disab(op, addr, "LDA");
  case 0xAE:
    return disab(op, addr, "LDX");
  case 0xB0:
    return disrel(op, addr, "BCS");
  case 0xB1:
    return disidy(op, addr, "LDA");
  case 0xB4:
    return diszpx(op, addr, "LDY");
  case 0xB5:
    return diszpx(op, addr, "LDA");
  case 0xB6:
    return diszpy(op, addr, "LDX");
  case 0xB8:
    return disimpl(op, addr, "CLV");
  case 0xB9:
    return disaby(op, addr, "LDA");
  case 0xBA:
    return disimpl(op, addr, "TSX");
  case 0xBC:
    return disabx(op, addr, "LDY");
  case 0xBD:
    return disabx(op, addr, "LDA");
  case 0xBE:
    return disaby(op, addr, "LDX");
  case 0xC0:
    return disimm(op, addr, "CPY");
  case 0xC1:
    return disidx(op, addr, "CMP");
  case 0xC4:
    return diszp(op, addr, "CPY");
  case 0xC5:
    return diszp(op, addr, "CMP");
  case 0xC6:
    return diszp(op, addr, "DEC");
  case 0xC8:
    return disimpl(op, addr, "INY");
  case 0xC9:
    return disimm(op, addr, "CMP");
  case 0xCA:
    return disimpl(op, addr, "DEX");
  case 0xCC:
    return disab(op, addr, "CPY");
  case 0xCD:
    return disab(op, addr, "CMP");
  case 0xCE:
    return disab(op, addr, "DEC");
  case 0xD0:
    return disrel(op, addr, "BNE");
  case 0xD1:
    return disidy(op, addr, "CMP");
  case 0xD5:
    return diszpx(op, addr, "CMP");
  case 0xD6:
    return diszpx(op, addr, "DEC");
  case 0xD8:
    return disimpl(op, addr, "CLD");
  case 0xD9:
    return disaby(op, addr, "CMP");
  case 0xDD:
    return disabx(op, addr, "CMP");
  case 0xDE:
    return disabx(op, addr, "DEC");
  case 0xE0:
    return disimm(op, addr, "CPX");
  case 0xE1:
    return disidx(op, addr, "SBC");
  case 0xE4:
    return diszp(op, addr, "CPX");
  case 0xE5:
    return diszp(op, addr, "SBC");
  case 0xE6:
    return diszp(op, addr, "INC");
  case 0xE8:
    return disimpl(op, addr, "INX");
  case 0xE9:
    return disimm(op, addr, "SBC");
  case 0xEA:
    return disimpl(op, addr, "NOP");
  case 0xEC:
    return disab(op, addr, "CPX");
  case 0xED:
    return disab(op, addr, "SBC");
  case 0xEE:
    return disab(op, addr, "INC");
  case 0xF0:
    return disrel(op, addr, "BEQ");
  case 0xF1:
    return disidy(op, addr, "SBC");
  case 0xF5:
    return diszpx(op, addr, "SBC");
  case 0xF6:
    return diszpx(op, addr, "INC");
  case 0xF8:
    return disimpl(op, addr, "SED");
  case 0xF9:
    return disaby(op, addr, "SBC");
  case 0xFD:
    return disabx(op, addr, "SBC");
  case 0xFE:
    return disabx(op, addr, "INC");
  default:
    return disimpl(op, addr, "JAM");
  }
}

void flag(U8 flag, bool condition) {
  if (condition) {
    P |= flag;
  } else {
    P &= ~flag;
  }
}

U8 *imm() { return &MEM[PC++]; }

U8 *zp() { return &MEM[MEM[PC++]]; }

U8 *zpx() {
  U8 addr = MEM[PC++] + X;
  return &MEM[addr];
}

U8 *zpy() {
  U8 addr = MEM[PC++] + Y;
  return &MEM[addr];
}

U8 *ab() {
  U16 addr = PC;
  PC += 2;
  return &MEM[addr];
}

U8 *abx() {
  U16 addr = PC;
  PC += 2;
  return &MEM[addr + X];
}

U8 *aby() {
  U16 addr = PC;
  PC += 2;
  return &MEM[addr + Y];
}

U8 *id() {
  U8 idlo = MEM[PC++];
  U8 idhi = MEM[PC++];
  U16 ptr = (((U16)idhi) << 8) | idlo;
  U8 lo = MEM[ptr];
  U8 hi = MEM[(U8)(ptr + 1)]; // Page wrapping
  U16 addr = (((U16)hi) << 8) | lo;
  return &MEM[addr];
}

U8 *idx() {
  U8 zpaddr = MEM[PC++] + X;
  U8 lo = MEM[zpaddr];
  U8 hi = MEM[(U8)(zpaddr + 1)];
  U16 addr = (((U16)hi) << 8) | lo;
  return &MEM[addr];
}

U8 *idy() {
  U8 zpaddr = MEM[PC++];
  U8 lo = MEM[zpaddr];
  U8 hi = MEM[(U8)(zpaddr + 1)];
  U16 addr = ((((U16)hi) << 8) | lo) + Y;
  return &MEM[addr];
}

void adc(U8 *val) {
  if (P & FLAG_DECIMAL) {
    debug = true;
  }
  U16 sum = A + *val + (P & FLAG_CARRY ? 1 : 0);
  U8 res = sum & 0xFF;
  flag(FLAG_CARRY, sum > U8_MAX);
  flag(FLAG_ZERO, (sum & 0xFF) == 0);
  flag(FLAG_OVERFLOW, (~(A ^ *val) & (A ^ res) & 0x80) != 0);
  flag(FLAG_NEGATIVE, (sum & 0x80) != 0);
  A = res;
}

void and(U8 *val) {
  A &= *val;
  flag(FLAG_ZERO, A == 0);
  flag(FLAG_NEGATIVE, (A & 0x80) != 0);
}

void asl(U8 *val) {
  flag(FLAG_CARRY, (*val & 0x80) != 0);
  *val <<= 1;
  flag(FLAG_ZERO, *val == 0);
  flag(FLAG_NEGATIVE, (*val & 0x80) != 0);
}

void bcc(I8 offset) {
  if (!(P & FLAG_CARRY)) {
    PC += offset;
  }
}

void bcs(I8 offset) {
  if (P & FLAG_CARRY) {
    PC += offset;
  }
}

void beq(I8 offset) {
  if (P & FLAG_ZERO) {
    PC += offset;
  }
}

void bit(U8 *val) {
  U8 res = A & *val;
  flag(FLAG_ZERO, res == 0);
  flag(FLAG_OVERFLOW, (*val & 0x40) != 0);
  flag(FLAG_NEGATIVE, (*val & 0x80) != 0);
}

void bmi(I8 offset) {
  if (P & FLAG_NEGATIVE) {
    PC += offset;
  }
}

void bne(I8 offset) {
  if (!(P & FLAG_ZERO)) {
    PC += offset;
  }
}

void bpl(I8 offset) {
  if (!(P & FLAG_NEGATIVE)) {
    PC += offset;
  }
}

void _brk() {
  debug = true;
  PC++;
  MEM[0x0100 + SP--] = (U8)((PC >> 8) & 0xFF);
  MEM[0x0100 + SP--] = (U8)(PC & 0xFF);
  MEM[0x0100 + SP--] = P | FLAG_BREAK | FLAG_UNUSED;
  flag(FLAG_INTERRUPT, true);
  U8 lo = MEM[0xFFFE];
  U8 hi = MEM[0xFFFF];
  PC = (((U16)hi) << 8) | lo;
}

void bvc(I8 offset) {
  if (!(P & FLAG_OVERFLOW)) {
    PC += offset;
  }
}

void bvs(I8 offset) {
  if (P & FLAG_OVERFLOW) {
    PC += offset;
  }
}

void clc() { flag(FLAG_CARRY, false); }

void cld() { flag(FLAG_DECIMAL, false); }

void cli() { flag(FLAG_INTERRUPT, false); }

void clv() { flag(FLAG_OVERFLOW, false); }

void cmp(U8 *val) {
  U8 res = A - *val;
  flag(FLAG_CARRY, A >= *val);
  flag(FLAG_ZERO, res == 0);
  flag(FLAG_NEGATIVE, (res & 0x80) != 0);
}

void cpx(U8 *val) {
  U8 res = X - *val;
  flag(FLAG_CARRY, X >= *val);
  flag(FLAG_ZERO, res == 0);
  flag(FLAG_NEGATIVE, (res & 0x80) != 0);
}

void cpy(U8 *val) {
  U8 res = Y - *val;
  flag(FLAG_CARRY, Y >= *val);
  flag(FLAG_ZERO, res == 0);
  flag(FLAG_NEGATIVE, (res & 0x80) != 0);
}

void dec(U8 *val) {
  --(*val);
  flag(FLAG_ZERO, *val == 0);
  flag(FLAG_NEGATIVE, (*val & 0x80) != 0);
}

void dex() {
  --X;
  flag(FLAG_ZERO, X == 0);
  flag(FLAG_NEGATIVE, (X & 0x80) != 0);
}

void dey() {
  --Y;
  flag(FLAG_ZERO, Y == 0);
  flag(FLAG_NEGATIVE, (Y & 0x80) != 0);
}

void eor(U8 *val) {
  A ^= *val;
  flag(FLAG_ZERO, A == 0);
  flag(FLAG_NEGATIVE, (A & 0x80) != 0);
}

void inc(U8 *val) {
  ++(*val);
  flag(FLAG_ZERO, *val == 0);
  flag(FLAG_NEGATIVE, (*val & 0x80) != 0);
}

void inx() {
  ++X;
  flag(FLAG_ZERO, X == 0);
  flag(FLAG_NEGATIVE, (X & 0x80) != 0);
}

void iny() {
  ++Y;
  flag(FLAG_ZERO, Y == 0);
  flag(FLAG_NEGATIVE, (Y & 0x80) != 0);
}

void jmp(U16 addr) { PC = addr; }

void jsr(U16 addr) {
  U16 ret = PC - 1;
  MEM[0x0100 + SP--] = (U8)((ret >> 8) & 0xFF);
  MEM[0x0100 + SP--] = (U8)(ret & 0xFF);
  PC = addr;
}

void lda(U8 *val) {
  A = *val;
  flag(FLAG_ZERO, A == 0);
  flag(FLAG_NEGATIVE, (A & 0x80) != 0);
}

void ldx(U8 *val) {
  X = *val;
  flag(FLAG_ZERO, X == 0);
  flag(FLAG_NEGATIVE, (X & 0x80) != 0);
}

void ldy(U8 *val) {
  Y = *val;
  flag(FLAG_ZERO, Y == 0);
  flag(FLAG_NEGATIVE, (Y & 0x80) != 0);
}

void lsr(U8 *val) {
  flag(FLAG_CARRY, (*val & 0x01) != 0);
  *val >>= 1;
  flag(FLAG_ZERO, *val == 0);
  flag(FLAG_NEGATIVE, false);
}

void nop() {}

void ora(U8 *val) {
  A |= *val;
  flag(FLAG_ZERO, A == 0);
  flag(FLAG_NEGATIVE, (A & 0x80) != 0);
}

void pha() { MEM[0x0100 + SP--] = A; }

void php() { MEM[0x0100 + SP--] = P | FLAG_BREAK | FLAG_UNUSED; }

void pla() {
  A = MEM[0x0100 + ++SP];
  flag(FLAG_ZERO, A == 0);
  flag(FLAG_NEGATIVE, (A & 0x80) != 0);
}

void plp() { P = MEM[0x0100 + ++SP] & ~(FLAG_BREAK | FLAG_UNUSED); }

void rol(U8 *val) {
  bool cy = (P & FLAG_CARRY) != 0;
  flag(FLAG_CARRY, (*val & 0x80) != 0);
  *val = (*val << 1) | (cy ? 1 : 0);
  flag(FLAG_ZERO, *val == 0);
  flag(FLAG_NEGATIVE, (*val & 0x80) != 0);
}

void ror(U8 *val) {
  bool cy = (P & FLAG_CARRY) != 0;
  flag(FLAG_CARRY, (*val & 0x01) != 0);
  *val = (*val >> 1) | (cy ? 0x80 : 0);
  flag(FLAG_ZERO, *val == 0);
  flag(FLAG_NEGATIVE, (*val & 0x80) != 0);
}

void rti() {
  P = MEM[0x0100 + ++SP] & ~(FLAG_BREAK | FLAG_UNUSED);
  U8 lo = MEM[0x0100 + ++SP];
  U8 hi = MEM[0x0100 + ++SP];
  PC = (((U16)hi) << 8) | lo;
}

void rts() {
  U8 lo = MEM[0x0100 + ++SP];
  U8 hi = MEM[0x0100 + ++SP];
  PC = ((((U16)hi) << 8) | lo) + 1;
}

void sbc(U8 *val) {
  if (P & FLAG_DECIMAL) {
    debug = true;
  }
  U16 diff = A - *val - (P & FLAG_CARRY ? 0 : 1);
  U8 res = diff & 0xFF;
  flag(FLAG_CARRY, diff <= U8_MAX);
  flag(FLAG_ZERO, (diff & 0xFF) == 0);
  flag(FLAG_OVERFLOW, ((A ^ *val) & (A ^ res) & 0x80) != 0);
  flag(FLAG_NEGATIVE, (diff & 0x80) != 0);
  A = res;
}

void sec() { flag(FLAG_CARRY, true); }

void sed() { flag(FLAG_DECIMAL, true); }

void sei() { flag(FLAG_INTERRUPT, true); }

void sta(U8 *addr) { *addr = A; }

void stx(U8 *addr) { *addr = X; }

void sty(U8 *addr) { *addr = Y; }

void tax() {
  X = A;
  flag(FLAG_ZERO, X == 0);
  flag(FLAG_NEGATIVE, (X & 0x80) != 0);
}

void tay() {
  Y = A;
  flag(FLAG_ZERO, Y == 0);
  flag(FLAG_NEGATIVE, (Y & 0x80) != 0);
}

void tsx() {
  X = SP;
  flag(FLAG_ZERO, X == 0);
  flag(FLAG_NEGATIVE, (X & 0x80) != 0);
}

void txa() {
  A = X;
  flag(FLAG_ZERO, A == 0);
  flag(FLAG_NEGATIVE, (A & 0x80) != 0);
}

void txs() { SP = X; }

void tya() {
  A = Y;
  flag(FLAG_ZERO, A == 0);
  flag(FLAG_NEGATIVE, (A & 0x80) != 0);
}

void doop() {
  U8 op = MEM[PC++];
  switch (op) {
  case 0x00:
    _brk();
    break;
  case 0x01:
    adc(idx());
    break;
  case 0x05:
    ora(zp());
    break;
  case 0x06:
    asl(zp());
    break;
  case 0x08:
    php();
    break;
  case 0x09:
    ora(imm());
    break;
  case 0x0A:
    asl(&A);
    break;
  case 0x0D:
    ora(ab());
    break;
  case 0x0E:
    asl(ab());
    break;
  case 0x10:
    bpl((I8)*imm());
    break;
  case 0x11:
    ora(idy());
    break;
  case 0x15:
    ora(zpx());
    break;
  case 0x16:
    asl(zpx());
    break;
  case 0x18:
    clc();
    break;
  case 0x19:
    ora(aby());
    break;
  case 0x1D:
    ora(abx());
    break;
  case 0x1E:
    asl(abx());
    break;
  case 0x20:
    jsr(*(U16 *)ab());
    break;
  case 0x21:
    and(idx());
    break;
  case 0x24:
    bit(zp());
    break;
  case 0x25:
    and(zp());
    break;
  case 0x26:
    rol(zp());
    break;
  case 0x28:
    plp();
    break;
  case 0x29:
    and(imm());
    break;
  case 0x2A:
    rol(&A);
    break;
  case 0x2C:
    bit(ab());
    break;
  case 0x2D:
    and(ab());
    break;
  case 0x2E:
    rol(ab());
    break;
  case 0x30:
    bmi((I8)*imm());
    break;
  case 0x31:
    and(idy());
    break;
  case 0x35:
    and(zpx());
    break;
  case 0x36:
    rol(zpx());
    break;
  case 0x38:
    sec();
    break;
  case 0x39:
    and(aby());
    break;
  case 0x3D:
    and(abx());
    break;
  case 0x3E:
    rol(abx());
    break;
  case 0x40:
    rti();
    break;
  case 0x41:
    eor(idx());
    break;
  case 0x45:
    eor(zp());
    break;
  case 0x46:
    lsr(zp());
    break;
  case 0x48:
    pha();
    break;
  case 0x49:
    eor(imm());
    break;
  case 0x4A:
    lsr(&A);
    break;
  case 0x4C:
    jmp(*(U16 *)ab());
    break;
  case 0x4D:
    eor(ab());
    break;
  case 0x4E:
    lsr(ab());
    break;
  case 0x50:
    bvc((I8)*imm());
    break;
  case 0x51:
    eor(idy());
    break;
  case 0x55:
    eor(zpx());
    break;
  case 0x56:
    lsr(zpx());
    break;
  case 0x58:
    cli();
    break;
  case 0x59:
    eor(aby());
    break;
  case 0x5D:
    eor(abx());
    break;
  case 0x5E:
    lsr(abx());
    break;
  case 0x60:
    rts();
    break;
  case 0x61:
    adc(idx());
    break;
  case 0x65:
    adc(zp());
    break;
  case 0x66:
    ror(zp());
    break;
  case 0x68:
    pla();
    break;
  case 0x69:
    adc(imm());
    break;
  case 0x6A:
    ror(&A);
    break;
  case 0x6C:
    jmp(*(U16 *)id());
    break;
  case 0x6D:
    adc(ab());
    break;
  case 0x6E:
    ror(ab());
    break;
  case 0x70:
    bvs((I8)*imm());
    break;
  case 0x71:
    adc(idy());
    break;
  case 0x75:
    adc(zpx());
    break;
  case 0x76:
    ror(zpx());
    break;
  case 0x78:
    sei();
    break;
  case 0x79:
    adc(aby());
    break;
  case 0x7D:
    adc(abx());
    break;
  case 0x7E:
    ror(abx());
    break;
  case 0x81:
    sta(idx());
    break;
  case 0x84:
    sty(zp());
    break;
  case 0x85:
    sta(zp());
    break;
  case 0x86:
    stx(zp());
    break;
  case 0x88:
    dey();
    break;
  case 0x8A:
    txa();
    break;
  case 0x8C:
    sty(ab());
    break;
  case 0x8D:
    sta(ab());
    break;
  case 0x8E:
    stx(ab());
    break;
  case 0x90:
    bcc((I8)*imm());
    break;
  case 0x91:
    sta(idy());
    break;
  case 0x94:
    sty(zpx());
    break;
  case 0x95:
    sta(zpx());
    break;
  case 0x96:
    stx(zpy());
    break;
  case 0x98:
    tya();
    break;
  case 0x99:
    sta(aby());
    break;
  case 0x9A:
    txs();
    break;
  case 0x9D:
    sta(abx());
    break;
  case 0xA0:
    ldy(imm());
    break;
  case 0xA1:
    lda(idx());
    break;
  case 0xA2:
    ldx(imm());
    break;
  case 0xA4:
    ldy(zp());
    break;
  case 0xA5:
    lda(zp());
    break;
  case 0xA6:
    ldx(zp());
    break;
  case 0xA8:
    tay();
    break;
  case 0xA9:
    lda(imm());
    break;
  case 0xAA:
    tax();
    break;
  case 0xAC:
    ldy(ab());
    break;
  case 0xAD:
    lda(ab());
    break;
  case 0xAE:
    ldx(ab());
    break;
  case 0xB0:
    bcs((I8)*imm());
    break;
  case 0xB1:
    lda(idy());
    break;
  case 0xB4:
    ldy(zpx());
    break;
  case 0xB5:
    lda(zpx());
    break;
  case 0xB6:
    ldx(zpy());
    break;
  case 0xB8:
    clv();
    break;
  case 0xB9:
    lda(aby());
    break;
  case 0xBA:
    tsx();
    break;
  case 0xBC:
    ldy(abx());
    break;
  case 0xBD:
    lda(abx());
    break;
  case 0xBE:
    ldx(aby());
    break;
  case 0xC0:
    cpy(imm());
    break;
  case 0xC1:
    cmp(idx());
    break;
  case 0xC4:
    cpy(zp());
    break;
  case 0xC5:
    cmp(zp());
    break;
  case 0xC6:
    dec(zp());
    break;
  case 0xC8:
    iny();
    break;
  case 0xC9:
    cmp(imm());
    break;
  case 0xCA:
    dex();
    break;
  case 0xCC:
    cpy(ab());
    break;
  case 0xCD:
    cmp(ab());
    break;
  case 0xCE:
    dec(ab());
    break;
  case 0xD0:
    bne((I8)*imm());
    break;
  case 0xD1:
    cmp(idy());
    break;
  case 0xD5:
    cmp(zpx());
    break;
  case 0xD6:
    dec(zpx());
    break;
  case 0xD8:
    cld();
    break;
  case 0xD9:
    cmp(aby());
    break;
  case 0xDD:
    cmp(abx());
    break;
  case 0xDE:
    dec(abx());
    break;
  case 0xE0:
    cpx(imm());
    break;
  case 0xE1:
    sbc(idx());
    break;
  case 0xE4:
    cpx(zp());
    break;
  case 0xE5:
    sbc(zp());
    break;
  case 0xE6:
    inc(zp());
    break;
  case 0xE8:
    inx();
    break;
  case 0xE9:
    sbc(imm());
    break;
  case 0xEA:
    nop();
    break;
  case 0xEC:
    cpx(ab());
    break;
  case 0xED:
    sbc(ab());
    break;
  case 0xEE:
    inc(ab());
    break;
  case 0xF0:
    beq((I8)*imm());
    break;
  case 0xF1:
    sbc(idy());
    break;
  case 0xF5:
    sbc(zpx());
    break;
  case 0xF6:
    inc(zpx());
    break;
  case 0xF8:
    sed();
    break;
  case 0xF9:
    sbc(aby());
    break;
  case 0xFD:
    sbc(abx());
    break;
  case 0xFE:
    inc(abx());
    break;
  default:
    _brk();
  }
}
