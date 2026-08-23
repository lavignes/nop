#include <stdio.h>

#include "vm.h"

#define RESET "\x1b[0m"
#define RED(str) "\x1b[31m" str RESET
#define GREEN(str) "\x1b[32m" str RESET
#define YELLOW(str) "\x1b[33m" str RESET
#define BLUE(str) "\x1b[34m" str RESET
#define MAGENTA(str) "\x1b[35m" str RESET
#define CYAN(str) "\x1b[36m" str RESET
#define WHITE(str) "\x1b[37m" str RESET

static void disSym(Dbg const *dbg, U16 addr) {
  Symbol const *sym = symValFind(dbg, (UInt)addr);
  if (sym) {
    fprintf(stderr, CYAN("; %s"), sym->name);
    return;
  }
  sym = symValFind(dbg, (UInt)(addr - 1));
  if (sym) {
    fprintf(stderr, CYAN("; %s+1"), sym->name);
  }
}

static U16 disImpl(Emu const *emu, U8 op, U16 addr, char const *mne) {
  fprintf(stderr, " %02X      ", op);
  fprintf(stderr, "  " BLUE("%s") "            ", mne);
  return addr;
}

static U16 disImm(Emu const *emu, U8 op, U16 addr, char const *mne) {
  U8 val = emuRead(emu, addr++);
  fprintf(stderr, " %02X %02X   ", op, val);
  fprintf(stderr, "  " BLUE("%s") " " MAGENTA("#$%02X") "       ", mne, val);
  return addr;
}

static U16 disZp(Emu const *emu, U8 op, U16 addr, char const *mne) {
  U8 zp = emuRead(emu, addr++);
  fprintf(stderr, " %02X %02X   ", op, zp);
  fprintf(stderr, "  " BLUE("%s") " $%02X        ", mne, zp);
  disSym(&emu->dbg, (U16)zp);
  return addr;
}

static U16 disZpX(Emu const *emu, U8 op, U16 addr, char const *mne) {
  U8 zp = emuRead(emu, addr++);
  fprintf(stderr, " %02X %02X   ", op, zp);
  fprintf(stderr, "  " BLUE("%s") " $%02X,X      ", mne, zp);
  disSym(&emu->dbg, (U16)zp);
  return addr;
}

static U16 disZpY(Emu const *emu, U8 op, U16 addr, char const *mne) {
  U8 zp = emuRead(emu, addr++);
  fprintf(stderr, " %02X %02X   ", op, zp);
  fprintf(stderr, "  " BLUE("%s") " $%02X,Y      ", mne, zp);
  disSym(&emu->dbg, (U16)zp);
  return addr;
}

static U16 disAb(Emu const *emu, U8 op, U16 addr, char const *mne) {
  U8 lo = emuRead(emu, addr++);
  U8 hi = emuRead(emu, addr++);
  U16 ab = (((U16)hi) << 8) | lo;
  fprintf(stderr, " %02X %02X %02X", op, lo, hi);
  fprintf(stderr, "  " BLUE("%s") " $%04X      ", mne, ab);
  disSym(&emu->dbg, ab);
  return addr;
}

static U16 disAbX(Emu const *emu, U8 op, U16 addr, char const *mne) {
  U8 lo = emuRead(emu, addr++);
  U8 hi = emuRead(emu, addr++);
  U16 ab = (((U16)hi) << 8) | lo;
  fprintf(stderr, " %02X %02X %02X", op, lo, hi);
  fprintf(stderr, "  " BLUE("%s") " $%04X,X    ", mne, ab);
  disSym(&emu->dbg, ab);
  return addr;
}

static U16 disAbY(Emu const *emu, U8 op, U16 addr, char const *mne) {
  U8 lo = emuRead(emu, addr++);
  U8 hi = emuRead(emu, addr++);
  U16 ab = (((U16)hi) << 8) | lo;
  fprintf(stderr, " %02X %02X %02X", op, lo, hi);
  fprintf(stderr, "  " BLUE("%s") " $%04X,Y    ", mne, ab);
  disSym(&emu->dbg, ab);
  return addr;
}

static U16 disId(Emu const *emu, U8 op, U16 addr, char const *mne) {
  U8 lo = emuRead(emu, addr++);
  U8 hi = emuRead(emu, addr++);
  U16 ptr = (((U16)hi) << 8) | lo;
  fprintf(stderr, " %02X %02X %02X", op, lo, hi);
  fprintf(stderr, "  " BLUE("%s") " ($%04X)    ", mne, ptr);
  disSym(&emu->dbg, ptr);
  return addr;
}

static U16 disIdX(Emu const *emu, U8 op, U16 addr, char const *mne) {
  U8 zp = emuRead(emu, addr++);
  fprintf(stderr, " %02X %02X   ", op, zp);
  fprintf(stderr, "  " BLUE("%s") " ($%02X,X)    ", mne, zp);
  disSym(&emu->dbg, (U16)zp);
  return addr;
}

static U16 disIdY(Emu const *emu, U8 op, U16 addr, char const *mne) {
  U8 zp = emuRead(emu, addr++);
  fprintf(stderr, " %02X %02X   ", op, zp);
  fprintf(stderr, "  " BLUE("%s") " ($%02X),Y    ", mne, zp);
  disSym(&emu->dbg, (U16)zp);
  return addr;
}

static U16 disRel(Emu const *emu, U8 op, U16 addr, char const *mne) {
  I8 offset = (I8)emuRead(emu, addr++);
  U16 target = addr + offset;
  fprintf(stderr, " %02X %02X   ", op, (U8)offset);
  fprintf(stderr, "  " BLUE("%s") " $%04X      ", mne, target);
  disSym(&emu->dbg, target);
  return addr;
}

static U16 disIzp(Emu const *emu, U8 op, U16 addr, char const *mne) {
  U8 zp = emuRead(emu, addr++);
  fprintf(stderr, " %02X %02X   ", op, zp);
  fprintf(stderr, "  " BLUE("%s") " ($%02X)      ", mne, zp);
  disSym(&emu->dbg, (U16)zp);
  return addr;
}

static U16 disJix(Emu const *emu, U8 op, U16 addr, char const *mne) {
  U8 lo = emuRead(emu, addr++);
  U8 hi = emuRead(emu, addr++);
  U16 ab = (((U16)hi) << 8) | lo;
  fprintf(stderr, " %02X %02X %02X", op, lo, hi);
  fprintf(stderr, "  " BLUE("%s") " ($%04X,X)  ", mne, ab);
  disSym(&emu->dbg, ab);
  return addr;
}

static U16 disZpRel(Emu const *emu, U8 op, U16 addr, char const *mne) {
  U8 zp = emuRead(emu, addr++);
  I8 offset = (I8)emuRead(emu, addr++);
  U16 target = addr + offset;
  fprintf(stderr, " %02X %02X %02X", op, zp, (U8)offset);
  fprintf(stderr, "  " BLUE("%s") " $%02X,$%04X ", mne, zp, target);
  disSym(&emu->dbg, target);
  return addr;
}

typedef struct {
  char const *mne;
  U16 (*fn)(Emu const *, U8, U16, char const *);
} DisEntry;

static DisEntry const DIS_TBL[256] = {
    [0x00] = {"BRK", disImm},    [0x01] = {"ORA", disIdX},
    [0x05] = {"ORA", disZp},     [0x06] = {"ASL", disZp},
    [0x08] = {"PHP", disImpl},   [0x09] = {"ORA", disImm},
    [0x0A] = {"ASL", disImpl},   [0x0D] = {"ORA", disAb},
    [0x0E] = {"ASL", disAb},     [0x10] = {"BPL", disRel},
    [0x11] = {"ORA", disIdY},    [0x15] = {"ORA", disZpX},
    [0x16] = {"ASL", disZpX},    [0x18] = {"CLC", disImpl},
    [0x19] = {"ORA", disAbY},    [0x1D] = {"ORA", disAbX},
    [0x1E] = {"ASL", disAbX},    [0x20] = {"JSR", disAb},
    [0x21] = {"AND", disIdX},    [0x24] = {"BIT", disZp},
    [0x25] = {"AND", disZp},     [0x26] = {"ROL", disZp},
    [0x28] = {"PLP", disImpl},   [0x29] = {"AND", disImm},
    [0x2A] = {"ROL", disImpl},   [0x2C] = {"BIT", disAb},
    [0x2D] = {"AND", disAb},     [0x2E] = {"ROL", disAb},
    [0x30] = {"BMI", disRel},    [0x31] = {"AND", disIdY},
    [0x35] = {"AND", disZpX},    [0x36] = {"ROL", disZpX},
    [0x38] = {"SEC", disImpl},   [0x39] = {"AND", disAbY},
    [0x3D] = {"AND", disAbX},    [0x3E] = {"ROL", disAbX},
    [0x40] = {"RTI", disImpl},   [0x41] = {"EOR", disIdX},
    [0x45] = {"EOR", disZp},     [0x46] = {"LSR", disZp},
    [0x48] = {"PHA", disImpl},   [0x49] = {"EOR", disImm},
    [0x4A] = {"LSR", disImpl},   [0x4C] = {"JMP", disAb},
    [0x4D] = {"EOR", disAb},     [0x4E] = {"LSR", disAb},
    [0x50] = {"BVC", disRel},    [0x51] = {"EOR", disIdY},
    [0x55] = {"EOR", disZpX},    [0x56] = {"LSR", disZpX},
    [0x58] = {"CLI", disImpl},   [0x59] = {"EOR", disAbY},
    [0x5D] = {"EOR", disAbX},    [0x5E] = {"LSR", disAbX},
    [0x60] = {"RTS", disImpl},   [0x61] = {"ADC", disIdX},
    [0x65] = {"ADC", disZp},     [0x66] = {"ROR", disZp},
    [0x68] = {"PLA", disImpl},   [0x69] = {"ADC", disImm},
    [0x6A] = {"ROR", disImpl},   [0x6C] = {"JMP", disId},
    [0x6D] = {"ADC", disAb},     [0x6E] = {"ROR", disAb},
    [0x70] = {"BVS", disRel},    [0x71] = {"ADC", disIdY},
    [0x75] = {"ADC", disZpX},    [0x76] = {"ROR", disZpX},
    [0x78] = {"SEI", disImpl},   [0x79] = {"ADC", disAbY},
    [0x7D] = {"ADC", disAbX},    [0x7E] = {"ROR", disAbX},
    [0x81] = {"STA", disIdX},    [0x84] = {"STY", disZp},
    [0x85] = {"STA", disZp},     [0x86] = {"STX", disZp},
    [0x88] = {"DEY", disImpl},   [0x8A] = {"TXA", disImpl},
    [0x8C] = {"STY", disAb},     [0x8D] = {"STA", disAb},
    [0x8E] = {"STX", disAb},     [0x90] = {"BCC", disRel},
    [0x91] = {"STA", disIdY},    [0x94] = {"STY", disZpX},
    [0x95] = {"STA", disZpX},    [0x96] = {"STX", disZpY},
    [0x98] = {"TYA", disImpl},   [0x99] = {"STA", disAbY},
    [0x9A] = {"TXS", disImpl},   [0x9D] = {"STA", disAbX},
    [0xA0] = {"LDY", disImm},    [0xA1] = {"LDA", disIdX},
    [0xA2] = {"LDX", disImm},    [0xA4] = {"LDY", disZp},
    [0xA5] = {"LDA", disZp},     [0xA6] = {"LDX", disZp},
    [0xA8] = {"TAY", disImpl},   [0xA9] = {"LDA", disImm},
    [0xAA] = {"TAX", disImpl},   [0xAC] = {"LDY", disAb},
    [0xAD] = {"LDA", disAb},     [0xAE] = {"LDX", disAb},
    [0xB0] = {"BCS", disRel},    [0xB1] = {"LDA", disIdY},
    [0xB4] = {"LDY", disZpX},    [0xB5] = {"LDA", disZpX},
    [0xB6] = {"LDX", disZpY},    [0xB8] = {"CLV", disImpl},
    [0xB9] = {"LDA", disAbY},    [0xBA] = {"TSX", disImpl},
    [0xBC] = {"LDY", disAbX},    [0xBD] = {"LDA", disAbX},
    [0xBE] = {"LDX", disAbY},    [0xC0] = {"CPY", disImm},
    [0xC1] = {"CMP", disIdX},    [0xC4] = {"CPY", disZp},
    [0xC5] = {"CMP", disZp},     [0xC6] = {"DEC", disZp},
    [0xC8] = {"INY", disImpl},   [0xC9] = {"CMP", disImm},
    [0xCA] = {"DEX", disImpl},   [0xCC] = {"CPY", disAb},
    [0xCD] = {"CMP", disAb},     [0xCE] = {"DEC", disAb},
    [0xD0] = {"BNE", disRel},    [0xD1] = {"CMP", disIdY},
    [0xD5] = {"CMP", disZpX},    [0xD6] = {"DEC", disZpX},
    [0xD8] = {"CLD", disImpl},   [0xD9] = {"CMP", disAbY},
    [0xDD] = {"CMP", disAbX},    [0xDE] = {"DEC", disAbX},
    [0xE0] = {"CPX", disImm},    [0xE1] = {"SBC", disIdX},
    [0xE4] = {"CPX", disZp},     [0xE5] = {"SBC", disZp},
    [0xE6] = {"INC", disZp},     [0xE8] = {"INX", disImpl},
    [0xE9] = {"SBC", disImm},    [0xEA] = {"NOP", disImpl},
    [0xEC] = {"CPX", disAb},     [0xED] = {"SBC", disAb},
    [0xEE] = {"INC", disAb},     [0xF0] = {"BEQ", disRel},
    [0xF1] = {"SBC", disIdY},    [0xF5] = {"SBC", disZpX},
    [0xF6] = {"INC", disZpX},    [0xF8] = {"SED", disImpl},
    [0xF9] = {"SBC", disAbY},    [0xFD] = {"SBC", disAbX},
    [0xFE] = {"INC", disAbX},
#ifndef CPU_NMOS
    [0x04] = {"TSB", disZp},     [0x07] = {"RMB0", disZp},
    [0x0C] = {"TSB", disAb},     [0x0F] = {"BBR0", disZpRel},
    [0x12] = {"ORA", disIzp},    [0x14] = {"TRB", disZp},
    [0x17] = {"RMB1", disZp},    [0x1A] = {"INC", disImpl},
    [0x1C] = {"TRB", disAb},     [0x1F] = {"BBR1", disZpRel},
    [0x27] = {"RMB2", disZp},    [0x2F] = {"BBR2", disZpRel},
    [0x32] = {"AND", disIzp},    [0x34] = {"BIT", disZpX},
    [0x37] = {"RMB3", disZp},    [0x3A] = {"DEC", disImpl},
    [0x3C] = {"BIT", disAbX},    [0x3F] = {"BBR3", disZpRel},
    [0x47] = {"RMB4", disZp},    [0x4F] = {"BBR4", disZpRel},
    [0x52] = {"EOR", disIzp},    [0x57] = {"RMB5", disZp},
    [0x5A] = {"PHY", disImpl},   [0x5F] = {"BBR5", disZpRel},
    [0x64] = {"STZ", disZp},     [0x67] = {"RMB6", disZp},
    [0x6F] = {"BBR6", disZpRel}, [0x72] = {"ADC", disIzp},
    [0x74] = {"STZ", disZpX},    [0x77] = {"RMB7", disZp},
    [0x7A] = {"PLY", disImpl},   [0x7C] = {"JMP", disJix},
    [0x7F] = {"BBR7", disZpRel}, [0x80] = {"BRA", disRel},
    [0x87] = {"SMB0", disZp},    [0x89] = {"BIT", disImm},
    [0x8F] = {"BBS0", disZpRel}, [0x92] = {"STA", disIzp},
    [0x97] = {"SMB1", disZp},    [0x9C] = {"STZ", disAb},
    [0x9E] = {"STZ", disAbX},    [0x9F] = {"BBS1", disZpRel},
    [0xA7] = {"SMB2", disZp},    [0xAF] = {"BBS2", disZpRel},
    [0xB2] = {"LDA", disIzp},    [0xB7] = {"SMB3", disZp},
    [0xBF] = {"BBS3", disZpRel}, [0xC7] = {"SMB4", disZp},
    [0xCB] = {"WAI", disImpl},   [0xCF] = {"BBS4", disZpRel},
    [0xD2] = {"CMP", disIzp},    [0xD7] = {"SMB5", disZp},
    [0xDA] = {"PHX", disImpl},   [0xDB] = {"STP", disImpl},
    [0xDF] = {"BBS5", disZpRel}, [0xE7] = {"SMB6", disZp},
    [0xEF] = {"BBS6", disZpRel}, [0xF2] = {"SBC", disIzp},
    [0xF7] = {"SMB7", disZp},    [0xFA] = {"PLX", disImpl},
    [0xFF] = {"BBS7", disZpRel},
#endif // CPU_NMOS
};

U16 disAsm(Emu const *emu, U16 addr) {
  Symbol const *sym = symValFind(&emu->dbg, (UInt)addr);
  if (sym) {
    fprintf(stderr,
            "\033[33m"
            "%s:" RESET "\n",
            sym->name);
  }
  fprintf(stderr, "%04X ", addr);
  U8 op = emuRead(emu, addr++);
  DisEntry const *entry = &DIS_TBL[op];
  if (entry->fn) {
    addr = entry->fn(emu, op, addr, entry->mne);
  } else {
    addr = disImpl(emu, op, addr, "ILL");
  }
  fprintf(stderr, "\n");
  return addr;
}
