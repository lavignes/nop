#include <stdio.h>
#include <string.h>

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

static U16 disImp(Emu const *emu, U8 op, U16 addr, char const *mne) {
  fprintf(stderr, " %02X      ", op);
  fprintf(stderr, "  " BLUE("%s") "              ", mne);
  return addr;
}

static U16 disImm(Emu const *emu, U8 op, U16 addr, char const *mne) {
  U8 val = emuRead(emu, addr++);
  fprintf(stderr, " %02X %02X   ", op, val);
  fprintf(stderr, "  " BLUE("%s") " " MAGENTA("#$%02X") "         ", mne, val);
  return addr;
}

static U16 disZpg(Emu const *emu, U8 op, U16 addr, char const *mne) {
  U8 zp = emuRead(emu, addr++);
  fprintf(stderr, " %02X %02X   ", op, zp);
  if (strlen(mne) == 3) {
    fprintf(stderr, "  " BLUE("%s") " $%02X           ", mne, zp);
  } else {
    fprintf(stderr, "  " BLUE("%.3s") " " MAGENTA("%c") ",$%02X         ", mne,
            mne[2], zp);
  }
  disSym(&emu->dbg, (U16)zp);
  return addr;
}

static U16 disZpx(Emu const *emu, U8 op, U16 addr, char const *mne) {
  U8 zp = emuRead(emu, addr++);
  fprintf(stderr, " %02X %02X   ", op, zp);
  fprintf(stderr, "  " BLUE("%s") " $%02X,X        ", mne, zp);
  disSym(&emu->dbg, (U16)zp);
  return addr;
}

static U16 disZpy(Emu const *emu, U8 op, U16 addr, char const *mne) {
  U8 zp = emuRead(emu, addr++);
  fprintf(stderr, " %02X %02X   ", op, zp);
  fprintf(stderr, "  " BLUE("%s") " $%02X,Y        ", mne, zp);
  disSym(&emu->dbg, (U16)zp);
  return addr;
}

static U16 disAbs(Emu const *emu, U8 op, U16 addr, char const *mne) {
  U8 lo = emuRead(emu, addr++);
  U8 hi = emuRead(emu, addr++);
  U16 ab = (((U16)hi) << 8) | lo;
  fprintf(stderr, " %02X %02X %02X", op, lo, hi);
  fprintf(stderr, "  " BLUE("%s") " $%04X        ", mne, ab);
  disSym(&emu->dbg, ab);
  return addr;
}

static U16 disAbx(Emu const *emu, U8 op, U16 addr, char const *mne) {
  U8 lo = emuRead(emu, addr++);
  U8 hi = emuRead(emu, addr++);
  U16 ab = (((U16)hi) << 8) | lo;
  fprintf(stderr, " %02X %02X %02X", op, lo, hi);
  fprintf(stderr, "  " BLUE("%s") " $%04X,X      ", mne, ab);
  disSym(&emu->dbg, ab);
  return addr;
}

static U16 disAby(Emu const *emu, U8 op, U16 addr, char const *mne) {
  U8 lo = emuRead(emu, addr++);
  U8 hi = emuRead(emu, addr++);
  U16 ab = (((U16)hi) << 8) | lo;
  fprintf(stderr, " %02X %02X %02X", op, lo, hi);
  fprintf(stderr, "  " BLUE("%s") " $%04X,Y      ", mne, ab);
  disSym(&emu->dbg, ab);
  return addr;
}

static U16 disInd(Emu const *emu, U8 op, U16 addr, char const *mne) {
  U8 lo = emuRead(emu, addr++);
  U8 hi = emuRead(emu, addr++);
  U16 ptr = (((U16)hi) << 8) | lo;
  fprintf(stderr, " %02X %02X %02X", op, lo, hi);
  fprintf(stderr, "  " BLUE("%s") " ($%04X)      ", mne, ptr);
  disSym(&emu->dbg, ptr);
  return addr;
}

static U16 disIzx(Emu const *emu, U8 op, U16 addr, char const *mne) {
  U8 zp = emuRead(emu, addr++);
  fprintf(stderr, " %02X %02X   ", op, zp);
  fprintf(stderr, "  " BLUE("%s") " ($%02X,X)      ", mne, zp);
  disSym(&emu->dbg, (U16)zp);
  return addr;
}

static U16 disIzy(Emu const *emu, U8 op, U16 addr, char const *mne) {
  U8 zp = emuRead(emu, addr++);
  fprintf(stderr, " %02X %02X   ", op, zp);
  fprintf(stderr, "  " BLUE("%s") " ($%02X),Y      ", mne, zp);
  disSym(&emu->dbg, (U16)zp);
  return addr;
}

static U16 disRel(Emu const *emu, U8 op, U16 addr, char const *mne) {
  I8 offset = (I8)emuRead(emu, addr++);
  U16 target = addr + offset;
  fprintf(stderr, " %02X %02X   ", op, (U8)offset);
  fprintf(stderr, "  " BLUE("%s") " $%04X        ", mne, target);
  disSym(&emu->dbg, target);
  return addr;
}

static U16 disIzp(Emu const *emu, U8 op, U16 addr, char const *mne) {
  U8 zp = emuRead(emu, addr++);
  fprintf(stderr, " %02X %02X   ", op, zp);
  fprintf(stderr, "  " BLUE("%s") " ($%02X)        ", mne, zp);
  disSym(&emu->dbg, (U16)zp);
  return addr;
}

static U16 disIax(Emu const *emu, U8 op, U16 addr, char const *mne) {
  U8 lo = emuRead(emu, addr++);
  U8 hi = emuRead(emu, addr++);
  U16 ab = (((U16)hi) << 8) | lo;
  fprintf(stderr, " %02X %02X %02X", op, lo, hi);
  fprintf(stderr, "  " BLUE("%s") " ($%04X,X)    ", mne, ab);
  disSym(&emu->dbg, ab);
  return addr;
}

static U16 disBzp(Emu const *emu, U8 op, U16 addr, char const *mne) {
  U8 zp = emuRead(emu, addr++);
  fprintf(stderr, " %02X %02X   ", op, zp);
  fprintf(stderr, "  " BLUE("%.3s") " " MAGENTA("%c") ",$%02X         ", mne,
          mne[3], zp);
  disSym(&emu->dbg, (U16)zp);
  return addr;
}

static U16 disBzr(Emu const *emu, U8 op, U16 addr, char const *mne) {
  U8 zp = emuRead(emu, addr++);
  I8 offset = (I8)emuRead(emu, addr++);
  U16 target = addr + offset;
  fprintf(stderr, " %02X %02X %02X", op, zp, (U8)offset);
  fprintf(stderr, "  " BLUE("%.3s") " " MAGENTA("%c") ",$%02X,$%04X ", mne,
          mne[3], zp, target);
  disSym(&emu->dbg, target);
  return addr;
}

typedef struct {
  char const *mne;
  U16 (*fn)(Emu const *, U8, U16, char const *);
} DisEntry;

static DisEntry const DIS_TBL[256] = {
    [0x00] = {"BRK", disImm},  [0x01] = {"ORA", disIzx},
    [0x05] = {"ORA", disZpg},  [0x06] = {"ASL", disZpg},
    [0x08] = {"PHP", disImp},  [0x09] = {"ORA", disImm},
    [0x0A] = {"ASL", disImp},  [0x0D] = {"ORA", disAbs},
    [0x0E] = {"ASL", disAbs},  [0x10] = {"BPL", disRel},
    [0x11] = {"ORA", disIzy},  [0x15] = {"ORA", disZpx},
    [0x16] = {"ASL", disZpx},  [0x18] = {"CLC", disImp},
    [0x19] = {"ORA", disAby},  [0x1D] = {"ORA", disAbx},
    [0x1E] = {"ASL", disAbx},  [0x20] = {"JSR", disAbs},
    [0x21] = {"AND", disIzx},  [0x24] = {"BIT", disZpg},
    [0x25] = {"AND", disZpg},  [0x26] = {"ROL", disZpg},
    [0x28] = {"PLP", disImp},  [0x29] = {"AND", disImm},
    [0x2A] = {"ROL", disImp},  [0x2C] = {"BIT", disAbs},
    [0x2D] = {"AND", disAbs},  [0x2E] = {"ROL", disAbs},
    [0x30] = {"BMI", disRel},  [0x31] = {"AND", disIzy},
    [0x35] = {"AND", disZpx},  [0x36] = {"ROL", disZpx},
    [0x38] = {"SEC", disImp},  [0x39] = {"AND", disAby},
    [0x3D] = {"AND", disAbx},  [0x3E] = {"ROL", disAbx},
    [0x40] = {"RTI", disImp},  [0x41] = {"EOR", disIzx},
    [0x45] = {"EOR", disZpg},  [0x46] = {"LSR", disZpg},
    [0x48] = {"PHA", disImp},  [0x49] = {"EOR", disImm},
    [0x4A] = {"LSR", disImp},  [0x4C] = {"JMP", disAbs},
    [0x4D] = {"EOR", disAbs},  [0x4E] = {"LSR", disAbs},
    [0x50] = {"BVC", disRel},  [0x51] = {"EOR", disIzy},
    [0x55] = {"EOR", disZpx},  [0x56] = {"LSR", disZpx},
    [0x58] = {"CLI", disImp},  [0x59] = {"EOR", disAby},
    [0x5D] = {"EOR", disAbx},  [0x5E] = {"LSR", disAbx},
    [0x60] = {"RTS", disImp},  [0x61] = {"ADC", disIzx},
    [0x65] = {"ADC", disZpg},  [0x66] = {"ROR", disZpg},
    [0x68] = {"PLA", disImp},  [0x69] = {"ADC", disImm},
    [0x6A] = {"ROR", disImp},  [0x6C] = {"JMP", disInd},
    [0x6D] = {"ADC", disAbs},  [0x6E] = {"ROR", disAbs},
    [0x70] = {"BVS", disRel},  [0x71] = {"ADC", disIzy},
    [0x75] = {"ADC", disZpx},  [0x76] = {"ROR", disZpx},
    [0x78] = {"SEI", disImp},  [0x79] = {"ADC", disAby},
    [0x7D] = {"ADC", disAbx},  [0x7E] = {"ROR", disAbx},
    [0x81] = {"STA", disIzx},  [0x84] = {"STY", disZpg},
    [0x85] = {"STA", disZpg},  [0x86] = {"STX", disZpg},
    [0x88] = {"DEY", disImp},  [0x8A] = {"TXA", disImp},
    [0x8C] = {"STY", disAbs},  [0x8D] = {"STA", disAbs},
    [0x8E] = {"STX", disAbs},  [0x90] = {"BCC", disRel},
    [0x91] = {"STA", disIzy},  [0x94] = {"STY", disZpx},
    [0x95] = {"STA", disZpx},  [0x96] = {"STX", disZpy},
    [0x98] = {"TYA", disImp},  [0x99] = {"STA", disAby},
    [0x9A] = {"TXS", disImp},  [0x9D] = {"STA", disAbx},
    [0xA0] = {"LDY", disImm},  [0xA1] = {"LDA", disIzx},
    [0xA2] = {"LDX", disImm},  [0xA4] = {"LDY", disZpg},
    [0xA5] = {"LDA", disZpg},  [0xA6] = {"LDX", disZpg},
    [0xA8] = {"TAY", disImp},  [0xA9] = {"LDA", disImm},
    [0xAA] = {"TAX", disImp},  [0xAC] = {"LDY", disAbs},
    [0xAD] = {"LDA", disAbs},  [0xAE] = {"LDX", disAbs},
    [0xB0] = {"BCS", disRel},  [0xB1] = {"LDA", disIzy},
    [0xB4] = {"LDY", disZpx},  [0xB5] = {"LDA", disZpx},
    [0xB6] = {"LDX", disZpy},  [0xB8] = {"CLV", disImp},
    [0xB9] = {"LDA", disAby},  [0xBA] = {"TSX", disImp},
    [0xBC] = {"LDY", disAbx},  [0xBD] = {"LDA", disAbx},
    [0xBE] = {"LDX", disAby},  [0xC0] = {"CPY", disImm},
    [0xC1] = {"CMP", disIzx},  [0xC4] = {"CPY", disZpg},
    [0xC5] = {"CMP", disZpg},  [0xC6] = {"DEC", disZpg},
    [0xC8] = {"INY", disImp},  [0xC9] = {"CMP", disImm},
    [0xCA] = {"DEX", disImp},  [0xCC] = {"CPY", disAbs},
    [0xCD] = {"CMP", disAbs},  [0xCE] = {"DEC", disAbs},
    [0xD0] = {"BNE", disRel},  [0xD1] = {"CMP", disIzy},
    [0xD5] = {"CMP", disZpx},  [0xD6] = {"DEC", disZpx},
    [0xD8] = {"CLD", disImp},  [0xD9] = {"CMP", disAby},
    [0xDD] = {"CMP", disAbx},  [0xDE] = {"DEC", disAbx},
    [0xE0] = {"CPX", disImm},  [0xE1] = {"SBC", disIzx},
    [0xE4] = {"CPX", disZpg},  [0xE5] = {"SBC", disZpg},
    [0xE6] = {"INC", disZpg},  [0xE8] = {"INX", disImp},
    [0xE9] = {"SBC", disImm},  [0xEA] = {"NOP", disImp},
    [0xEC] = {"CPX", disAbs},  [0xED] = {"SBC", disAbs},
    [0xEE] = {"INC", disAbs},  [0xF0] = {"BEQ", disRel},
    [0xF1] = {"SBC", disIzy},  [0xF5] = {"SBC", disZpx},
    [0xF6] = {"INC", disZpx},  [0xF8] = {"SED", disImp},
    [0xF9] = {"SBC", disAby},  [0xFD] = {"SBC", disAbx},
    [0xFE] = {"INC", disAbx},
#ifndef CPU_NMOS
    [0x04] = {"TSB", disZpg},  [0x07] = {"RMB0", disBzp},
    [0x0C] = {"TSB", disAbs},  [0x0F] = {"BBR0", disBzr},
    [0x12] = {"ORA", disIzp},  [0x14] = {"TRB", disZpg},
    [0x17] = {"RMB1", disBzp}, [0x1A] = {"INC", disImp},
    [0x1C] = {"TRB", disAbs},  [0x1F] = {"BBR1", disBzr},
    [0x27] = {"RMB2", disBzp}, [0x2F] = {"BBR2", disBzr},
    [0x32] = {"AND", disIzp},  [0x34] = {"BIT", disZpx},
    [0x37] = {"RMB3", disBzp}, [0x3A] = {"DEC", disImp},
    [0x3C] = {"BIT", disAbx},  [0x3F] = {"BBR3", disBzr},
    [0x47] = {"RMB4", disBzp}, [0x4F] = {"BBR4", disBzr},
    [0x52] = {"EOR", disIzp},  [0x57] = {"RMB5", disBzp},
    [0x5A] = {"PHY", disImp},  [0x5F] = {"BBR5", disBzr},
    [0x64] = {"STZ", disZpg},  [0x67] = {"RMB6", disBzp},
    [0x6F] = {"BBR6", disBzr}, [0x72] = {"ADC", disIzp},
    [0x74] = {"STZ", disZpx},  [0x77] = {"RMB7", disBzp},
    [0x7A] = {"PLY", disImp},  [0x7C] = {"JMP", disIax},
    [0x7F] = {"BBR7", disBzr}, [0x80] = {"BRA", disRel},
    [0x87] = {"SMB0", disBzp}, [0x89] = {"BIT", disImm},
    [0x8F] = {"BBS0", disBzr}, [0x92] = {"STA", disIzp},
    [0x97] = {"SMB1", disBzp}, [0x9C] = {"STZ", disAbs},
    [0x9E] = {"STZ", disAbx},  [0x9F] = {"BBS1", disBzr},
    [0xA7] = {"SMB2", disBzp}, [0xAF] = {"BBS2", disBzr},
    [0xB2] = {"LDA", disIzp},  [0xB7] = {"SMB3", disBzp},
    [0xBF] = {"BBS3", disBzr}, [0xC7] = {"SMB4", disBzp},
    [0xCB] = {"WAI", disImp},  [0xCF] = {"BBS4", disBzr},
    [0xD2] = {"CMP", disIzp},  [0xD7] = {"SMB5", disBzp},
    [0xDA] = {"PHX", disImp},  [0xDB] = {"STP", disImp},
    [0xDF] = {"BBS5", disBzr}, [0xE7] = {"SMB6", disBzp},
    [0xEF] = {"BBS6", disBzr}, [0xF2] = {"SBC", disIzp},
    [0xF7] = {"SMB7", disBzp}, [0xFA] = {"PLX", disImp},
    [0xFF] = {"BBS7", disBzr},
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
    addr = disImp(emu, op, addr, "ILL");
  }
  fprintf(stderr, "\n");
  return addr;
}
