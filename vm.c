#include <ctype.h>
#include <histedit.h>
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

static U8 A;
static U8 X;
static U8 Y;
static U8 P;
static U8 SP;
static U16 PC = 0x0200;

static bool debug = false;

static U8 MEM[65536];

void help(char const *name) {
  fprintf(stderr, "Usage: %s [options] <romfile>\n\n", name);
  fprintf(stderr, "Options:\n\n");
  fprintf(stderr, "  -h, --help       Show this help message\n");
  fprintf(stderr, "  -d, --debug      Start in debug mode\n");
}

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

static U16 bpcnt = 0;
static Breakpoint *bphead = NULL;
static Breakpoint nextpoint = {NULL, 0, 0};

void tick() {
  if (nextpoint.next && (PC == nextpoint.addr)) {
    debug = true;
    nextpoint.next = NULL;
  }
  for (Breakpoint const *bp = bphead; bp; bp = bp->next) {
    if (PC != bp->addr) {
      continue;
    }
    fprintf(stderr, "Hit breakpoint %d at $%04X\n", bp->num, bp->addr);
    debug = true;
    break;
  }
  if (debug) {
    debugger();
  }
  // TODO: check for interrupts here
  doop();
  // TODO: read/write memory-mapped IO here
}

Int parse(char const *str) {
  bool neg = str[0] == '-';
  if (neg || (str[0] == '+')) {
    ++str;
  }
  long val;
  if (str[0] == '$') {
    val = strtol(str + 1, NULL, 16);
  } else if (str[0] == '%') {
    val = strtol(str + 1, NULL, 2);
  } else if ((strcmp(str, "a") == 0) || (strcmp(str, "A") == 0)) {
    val = (unsigned long)A;
  } else if ((strcmp(str, "x") == 0) || (strcmp(str, "X") == 0)) {
    val = (unsigned long)X;
  } else if ((strcmp(str, "y") == 0 || strcmp(str, "Y") == 0)) {
    val = (unsigned long)Y;
  } else if ((strcmp(str, "pc") == 0 || strcmp(str, "PC") == 0)) {
    val = (unsigned long)PC;
  } else if ((strcmp(str, "sp") == 0 || strcmp(str, "SP") == 0)) {
    val = (unsigned long)SP;
  } else if ((strcmp(str, "p") == 0 || strcmp(str, "P") == 0)) {
    val = (unsigned long)P;
  } else if ((strcmp(str, "*pc") == 0) || (strcmp(str, "*pc") == 0)) {
    val = (unsigned long)MEM[PC];
  } else if ((strcmp(str, "*sp") == 0) || (strcmp(str, "*SP") == 0)) {
    val = (unsigned long)MEM[SP];
  } else if (isdigit(str[0])) {
    val = strtol(str, NULL, 10);
  }
  if (val == LONG_MAX) {
    return INT_MAX;
  }
  if (neg) {
    val = -val;
  }
  return (Int)val;
}

U16 disasm(U16 addr, U16 *cytot);
void regs();

typedef enum { DBG_DEBUG, DBG_BREAK, DBG_CONTINUE } DbgResult;

DbgResult dbgquit() { exit(EXIT_SUCCESS); }

DbgResult dbgcont() { return DBG_CONTINUE; }

DbgResult dbgstep() { return DBG_BREAK; }

DbgResult dbgnext() {
  U16 cytot = 0;
  U8 op = MEM[PC];
  if (op == 0x20) {
    nextpoint.addr = PC + 3;
    nextpoint.next = bphead; // to mark it active
    return DBG_CONTINUE;
  }
  return DBG_BREAK;
}

DbgResult dbgregs() {
  regs();
  return DBG_DEBUG;
}

DbgResult dbgbreak() {
  char *tok = strtok(NULL, " \t\n");
  if (!tok) {
    fprintf(stderr, "No address provided for breakpoint\n");
    return DBG_DEBUG;
  }
  Int addr = parse(tok);
  if ((addr == INT_MAX) || (addr > U16_MAX)) {
    fprintf(stderr, "Invalid address for breakpoint: %s\n", tok);
    return DBG_DEBUG;
  }
  Breakpoint *bp = malloc(sizeof(Breakpoint));
  bp->num = ++bpcnt;
  bp->addr = (U16)addr;
  bp->next = bphead;
  bphead = bp;
  fprintf(stderr, "Breakpoint %d set at $%04X\n", bp->num, bp->addr);
  return DBG_DEBUG;
}

DbgResult dbgdel() {
  char *tok = strtok(NULL, " \t\n");
  if (!tok) {
    fprintf(stderr, "No breakpoint number provided for deletion\n");
    return DBG_DEBUG;
  }
  Int num = parse(tok);
  if ((num == INT_MAX) || (num > U16_MAX)) {
    fprintf(stderr, "Invalid breakpoint number: %s\n", tok);
    return DBG_DEBUG;
  }
  Breakpoint **prev = &bphead;
  Breakpoint *bp = bphead;
  while (bp) {
    if (bp->num == (U16)num) {
      *prev = bp->next;
      free(bp);
      fprintf(stderr, "Breakpoint %d deleted\n", (U16)num);
      return DBG_DEBUG;
    }
    prev = &bp->next;
    bp = bp->next;
  }
  fprintf(stderr, "No breakpoint with number %d\n", (U16)num);
  return DBG_DEBUG;
}

DbgResult dbgexa() {
  Int start = PC;
  Int len = 16;
  char *tok = strtok(NULL, " \t\n");
  if (tok) {
    start = parse(tok);
    if ((start == INT_MAX) || (start > U16_MAX)) {
      fprintf(stderr, "Invalid address for examine: %s\n", tok);
      return DBG_DEBUG;
    }
  }
  tok = strtok(NULL, " \t\n");
  if (tok) {
    len = parse(tok);
    if ((len == INT_MAX) || (len <= 0)) {
      fprintf(stderr, "Invalid length for examine: %s\n", tok);
      return DBG_DEBUG;
    }
  }
  Int end = ((start + len - 1) > U16_MAX) ? U16_MAX : (start + len - 1);
  while (start <= end) {
    fprintf(stderr, "%04X ", (U16)start);
    for (UInt i = 0; i < 16; ++i) {
      if (i == 8)
        fprintf(stderr, " ");
      fprintf(stderr, (start + i) > end ? "   " : " %02X",
              MEM[(U16)(start + i)]);
    }
    fprintf(stderr, "  |");
    for (UInt i = 0; i < 16; ++i) {
      if ((start + i) > end) {
        fprintf(stderr, " ");
      } else {
        U8 byte = MEM[(U16)(start + i)];
        fprintf(stderr, "%c", isprint(byte) ? (char)byte : '.');
      }
    }
    fprintf(stderr, "|\n");
    start += 16;
  }
  return DBG_DEBUG;
}

DbgResult dbgdis() {
  Int start = PC;
  Int len = 1;
  char *tok = strtok(NULL, " \t\n");
  if (tok) {
    start = parse(tok);
    if ((start == INT_MAX) || (start > U16_MAX)) {
      fprintf(stderr, "Invalid address for disasm: %s\n", tok);
      return DBG_DEBUG;
    }
  }
  tok = strtok(NULL, " \t\n");
  if (tok) {
    len = parse(tok);
    if ((len == INT_MAX) || (len <= 0)) {
      fprintf(stderr, "Invalid length for disasm: %s\n", tok);
      return DBG_DEBUG;
    }
  }
  U16 addr = (U16)start, cytot = 0;
  Int count = 0;
  while (count < len && addr <= U16_MAX) {
    addr = disasm(addr, &cytot);
    count++;
  }
  return DBG_DEBUG;
}

typedef struct {
  char const *name;
  DbgResult (*fn)();
} DbgCmd;

static DbgCmd const DEBUG_CMDS[] = {
    {"quit", dbgquit},      {"continue", dbgcont}, {"step", dbgstep},
    {"next", dbgnext},      {"break", dbgbreak},   {"delete", dbgdel},
    {"registers", dbgregs}, {"x", dbgexa},         {"examine", dbgexa},
    {"disasm", dbgdis},     {NULL, NULL}};

char *dbgprompt(EditLine *el) {
  (void)el;
  return "> ";
}

unsigned char dbgcompl(EditLine *el, int ch) {
  (void)ch;
  LineInfo const *li = el_line(el);
  int len = li->cursor - li->buffer;
  if (len <= 0) {
    return CC_REFRESH;
  }
  int matchcnt = 0;
  DbgCmd const *match = NULL;
  for (DbgCmd const *cmd = DEBUG_CMDS; cmd->name; ++cmd) {
    if (strncmp(li->buffer, cmd->name, len) == 0) {
      match = cmd;
      ++matchcnt;
    }
  }
  if (matchcnt == 1) {
    el_deletestr(el, len);
    el_insertstr(el, match->name);
    return CC_REFRESH;
  }
  if (matchcnt > 1) {
    fprintf(stderr, "\n");
    bool first = true;
    for (DbgCmd const *cmd = DEBUG_CMDS; cmd->name; ++cmd) {
      if (strncmp(li->buffer, cmd->name, len) == 0) {
        fprintf(stderr, "%s%s", first ? "" : ", ", cmd->name);
        first = false;
      }
    }
    fprintf(stderr, "\n");
    return CC_REDISPLAY;
  }
  return CC_REFRESH;
}

void debugger() {
  static EditLine *el = NULL;
  static History *hist = NULL;
  static HistEvent ev;
  static char *prevline = NULL;
  static char *workline = NULL;
  if (!el) {
    el = el_init("vm", stdin, stderr, stderr);
    el_set(el, EL_PROMPT, &dbgprompt);
    el_set(el, EL_EDITOR, "emacs");
    el_set(el, EL_ADDFN, "ed-complete", "Complete command", dbgcompl);
    el_set(el, EL_BIND, "^I", "ed-complete", NULL);
    hist = history_init();
    history(hist, &ev, H_SETSIZE, 100);
    el_set(el, EL_HIST, history, hist);
  }
  regs();
  U16 cytot = 0;
  disasm(PC, &cytot);
  while (true) {
    free(workline);
    int count;
    char const *line = el_gets(el, &count);
    if (!line || (count <= 0)) {
      exit(EXIT_FAILURE);
    }
    workline = strdup(line);
    char *tok = strtok(workline, " \t\n");
    if (!tok) {
      if (prevline) {
        free(workline);
        workline = strdup(prevline);
        tok = strtok(workline, " \t\n");
      }
      if (!tok) {
        continue;
      }
    } else {
      history(hist, &ev, H_ENTER, line);
      free(prevline);
      prevline = strdup(line);
    }
    size_t toklen = strlen(tok);
    DbgCmd const *match = NULL;
    int matchcnt = 0;
    for (DbgCmd const *cmd = DEBUG_CMDS; cmd->name; ++cmd) {
      if (strncmp(tok, cmd->name, toklen) != 0) {
        continue;
      }
      match = cmd;
      ++matchcnt;
    }
    if (matchcnt == 0) {
      fprintf(stderr, "Unknown command: %s\n", tok);
      continue;
    }
    if (matchcnt > 1) {
      fprintf(stderr, "Ambiguous command: %s (", tok);
      bool first = true;
      for (DbgCmd const *cmd = DEBUG_CMDS; cmd->name; ++cmd) {
        if (strncmp(tok, cmd->name, toklen) == 0) {
          fprintf(stderr, "%s%s", first ? "" : ", ", cmd->name);
          first = false;
        }
      }
      fprintf(stderr, ")\n");
      continue;
    }
    DbgCmd const *cmd = match;
    DbgResult res = cmd->fn();
    if (res == DBG_BREAK) {
      break;
    }
    if (res == DBG_CONTINUE) {
      debug = false;
      return;
    }
  }
}

void regs() {
  fprintf(stderr, "PC:$%04X SP:$%02X A:$%02X X:$%02X Y:$%02X P:$%02X |", PC, SP,
          A, X, Y, P);
  fprintf(stderr, "%c%c%c%c%c%c%c%c|\n", (P & FLAG_NEGATIVE) ? 'N' : '.',
          (P & FLAG_OVERFLOW) ? 'V' : '.', (P & FLAG_UNUSED) ? '1' : '.',
          (P & FLAG_BREAK) ? 'B' : '.', (P & FLAG_DECIMAL) ? 'D' : '.',
          (P & FLAG_INTERRUPT) ? 'I' : '.', (P & FLAG_ZERO) ? 'Z' : '.',
          (P & FLAG_CARRY) ? 'C' : '.');
}

U16 disimpl(U8 op, U16 addr, char const *mne) {
  fprintf(stderr, " %02X      ", op);
  fprintf(stderr, "  %s            ", mne);
  return addr;
}

U16 disimm(U8 op, U16 addr, char const *mne) {
  U8 val = MEM[addr++];
  fprintf(stderr, " %02X %02X   ", op, val);
  fprintf(stderr, "  %s #$%02X       ", mne, val);
  return addr;
}

U16 diszp(U8 op, U16 addr, char const *mne) {
  U8 zp = MEM[addr++];
  fprintf(stderr, " %02X %02X   ", op, zp);
  fprintf(stderr, "  %s $%02X        ", mne, zp);
  return addr;
}

U16 diszpx(U8 op, U16 addr, char const *mne) {
  U8 zp = MEM[addr++];
  fprintf(stderr, " %02X %02X   ", op, zp);
  fprintf(stderr, "  %s $%02X,X      ", mne, zp);
  return addr;
}

U16 diszpy(U8 op, U16 addr, char const *mne) {
  U8 zp = MEM[addr++];
  fprintf(stderr, " %02X %02X   ", op, zp);
  fprintf(stderr, "  %s $%02X,Y      ", mne, zp);
  return addr;
}

U16 disab(U8 op, U16 addr, char const *mne) {
  U8 lo = MEM[addr++];
  U8 hi = MEM[addr++];
  U16 ab = (((U16)hi) << 8) | lo;
  fprintf(stderr, " %02X %02X %02X", op, lo, hi);
  fprintf(stderr, "  %s $%04X      ", mne, ab);
  return addr;
}

U16 disabx(U8 op, U16 addr, char const *mne) {
  U8 lo = MEM[addr++];
  U8 hi = MEM[addr++];
  U16 ab = (((U16)hi) << 8) | lo;
  fprintf(stderr, " %02X %02X %02X", op, lo, hi);
  fprintf(stderr, "  %s $%04X,X    ", mne, ab);
  return addr;
}

U16 disaby(U8 op, U16 addr, char const *mne) {
  U8 lo = MEM[addr++];
  U8 hi = MEM[addr++];
  U16 ab = (((U16)hi) << 8) | lo;
  fprintf(stderr, " %02X %02X %02X", op, lo, hi);
  fprintf(stderr, "  %s $%04X,Y    ", mne, ab);
  return addr;
}

U16 disid(U8 op, U16 addr, char const *mne) {
  U8 lo = MEM[addr++];
  U8 hi = MEM[addr++];
  U16 ptr = (((U16)hi) << 8) | lo;
  fprintf(stderr, " %02X %02X %02X", op, lo, hi);
  fprintf(stderr, "  %s ($%04X)    ", mne, ptr);
  return addr;
}

U16 disidx(U8 op, U16 addr, char const *mne) {
  U8 zp = MEM[addr++];
  fprintf(stderr, " %02X %02X   ", op, zp);
  fprintf(stderr, "  %s ($%02X,X)    ", mne, zp);
  return addr;
}

U16 disidy(U8 op, U16 addr, char const *mne) {
  U8 zp = MEM[addr++];
  fprintf(stderr, " %02X %02X   ", op, zp);
  fprintf(stderr, "  %s ($%02X),Y    ", mne, zp);
  return addr;
}

U16 disrel(U8 op, U16 addr, char const *mne) {
  I8 offset = (I8)MEM[addr++];
  U16 target = addr + offset;
  fprintf(stderr, " %02X %02X   ", op, (U8)offset);
  fprintf(stderr, "  %s $%04X      ", mne, target);
  return addr;
}

typedef U16 (*DisFn)(U8, U16, char const *);

typedef struct {
  char const *mne;
  DisFn fn;
  U16 cycles;
} DisEntry;

static DisEntry const DISASM_TABLE[256] = {
    [0x00] = {"BRK", disimm, 7},  [0x01] = {"ORA", disidx, 6},
    [0x05] = {"ORA", diszp, 3},   [0x06] = {"ASL", diszp, 5},
    [0x08] = {"PHP", disimpl, 3}, [0x09] = {"ORA", disimm, 2},
    [0x0A] = {"ASL", disimpl, 2}, [0x0D] = {"ORA", disab, 4},
    [0x0E] = {"ASL", disab, 6},   [0x10] = {"BPL", disrel, 2},
    [0x11] = {"ORA", disidy, 5},  [0x15] = {"ORA", diszpx, 4},
    [0x16] = {"ASL", diszpx, 6},  [0x18] = {"CLC", disimpl, 2},
    [0x19] = {"ORA", disaby, 4},  [0x1D] = {"ORA", disabx, 4},
    [0x1E] = {"ASL", disabx, 7},  [0x20] = {"JSR", disab, 6},
    [0x21] = {"AND", disidx, 6},  [0x24] = {"BIT", diszp, 3},
    [0x25] = {"AND", diszp, 3},   [0x26] = {"ROL", diszp, 5},
    [0x28] = {"PLP", disimpl, 4}, [0x29] = {"AND", disimm, 2},
    [0x2A] = {"ROL", disimpl, 2}, [0x2C] = {"BIT", disab, 4},
    [0x2D] = {"AND", disab, 4},   [0x2E] = {"ROL", disab, 6},
    [0x30] = {"BMI", disrel, 2},  [0x31] = {"AND", disidy, 5},
    [0x35] = {"AND", diszpx, 4},  [0x36] = {"ROL", diszpx, 6},
    [0x38] = {"SEC", disimpl, 2}, [0x39] = {"AND", disaby, 4},
    [0x3D] = {"AND", disabx, 4},  [0x3E] = {"ROL", disabx, 7},
    [0x40] = {"RTI", disimpl, 6}, [0x41] = {"EOR", disidx, 6},
    [0x45] = {"EOR", diszp, 3},   [0x46] = {"LSR", diszp, 5},
    [0x48] = {"PHA", disimpl, 3}, [0x49] = {"EOR", disimm, 2},
    [0x4A] = {"LSR", disimpl, 2}, [0x4C] = {"JMP", disab, 3},
    [0x4D] = {"EOR", disab, 4},   [0x4E] = {"LSR", disab, 6},
    [0x50] = {"BVC", disrel, 2},  [0x51] = {"EOR", disidy, 5},
    [0x55] = {"EOR", diszpx, 4},  [0x56] = {"LSR", diszpx, 6},
    [0x58] = {"CLI", disimpl, 2}, [0x59] = {"EOR", disaby, 4},
    [0x5D] = {"EOR", disabx, 4},  [0x5E] = {"LSR", disabx, 7},
    [0x60] = {"RTS", disimpl, 6}, [0x61] = {"ADC", disidx, 6},
    [0x65] = {"ADC", diszp, 3},   [0x66] = {"ROR", diszp, 5},
    [0x68] = {"PLA", disimpl, 4}, [0x69] = {"ADC", disimm, 2},
    [0x6A] = {"ROR", disimpl, 2}, [0x6C] = {"JMP", disid, 5},
    [0x6D] = {"ADC", disab, 4},   [0x6E] = {"ROR", disab, 6},
    [0x70] = {"BVS", disrel, 2},  [0x71] = {"ADC", disidy, 5},
    [0x75] = {"ADC", diszpx, 4},  [0x76] = {"ROR", diszpx, 6},
    [0x78] = {"SEI", disimpl, 2}, [0x79] = {"ADC", disaby, 4},
    [0x7D] = {"ADC", disabx, 4},  [0x7E] = {"ROR", disabx, 7},
    [0x81] = {"STA", disidx, 6},  [0x84] = {"STY", diszp, 3},
    [0x85] = {"STA", diszp, 3},   [0x86] = {"STX", diszp, 3},
    [0x88] = {"DEY", disimpl, 2}, [0x8A] = {"TXA", disimpl, 2},
    [0x8C] = {"STY", disab, 4},   [0x8D] = {"STA", disab, 4},
    [0x8E] = {"STX", disab, 4},   [0x90] = {"BCC", disrel, 2},
    [0x91] = {"STA", disidy, 6},  [0x94] = {"STY", diszpx, 4},
    [0x95] = {"STA", diszpx, 4},  [0x96] = {"STX", diszpy, 4},
    [0x98] = {"TYA", disimpl, 2}, [0x99] = {"STA", disaby, 5},
    [0x9A] = {"TXS", disimpl, 2}, [0x9D] = {"STA", disabx, 5},
    [0xA0] = {"LDY", disimm, 2},  [0xA1] = {"LDA", disidx, 6},
    [0xA2] = {"LDX", disimm, 2},  [0xA4] = {"LDY", diszp, 3},
    [0xA5] = {"LDA", diszp, 3},   [0xA6] = {"LDX", diszp, 3},
    [0xA8] = {"TAY", disimpl, 2}, [0xA9] = {"LDA", disimm, 2},
    [0xAA] = {"TAX", disimpl, 2}, [0xAC] = {"LDY", disab, 4},
    [0xAD] = {"LDA", disab, 4},   [0xAE] = {"LDX", disab, 4},
    [0xB0] = {"BCS", disrel, 2},  [0xB1] = {"LDA", disidy, 5},
    [0xB4] = {"LDY", diszpx, 4},  [0xB5] = {"LDA", diszpx, 4},
    [0xB6] = {"LDX", diszpy, 4},  [0xB8] = {"CLV", disimpl, 2},
    [0xB9] = {"LDA", disaby, 4},  [0xBA] = {"TSX", disimpl, 2},
    [0xBC] = {"LDY", disabx, 4},  [0xBD] = {"LDA", disabx, 4},
    [0xBE] = {"LDX", disaby, 4},  [0xC0] = {"CPY", disimm, 2},
    [0xC1] = {"CMP", disidx, 6},  [0xC4] = {"CPY", diszp, 3},
    [0xC5] = {"CMP", diszp, 3},   [0xC6] = {"DEC", diszp, 5},
    [0xC8] = {"INY", disimpl, 2}, [0xC9] = {"CMP", disimm, 2},
    [0xCA] = {"DEX", disimpl, 2}, [0xCC] = {"CPY", disab, 4},
    [0xCD] = {"CMP", disab, 4},   [0xCE] = {"DEC", disab, 6},
    [0xD0] = {"BNE", disrel, 2},  [0xD1] = {"CMP", disidy, 5},
    [0xD5] = {"CMP", diszpx, 4},  [0xD6] = {"DEC", diszpx, 6},
    [0xD8] = {"CLD", disimpl, 2}, [0xD9] = {"CMP", disaby, 4},
    [0xDD] = {"CMP", disabx, 4},  [0xDE] = {"DEC", disabx, 7},
    [0xE0] = {"CPX", disimm, 2},  [0xE1] = {"SBC", disidx, 6},
    [0xE4] = {"CPX", diszp, 3},   [0xE5] = {"SBC", diszp, 3},
    [0xE6] = {"INC", diszp, 5},   [0xE8] = {"INX", disimpl, 2},
    [0xE9] = {"SBC", disimm, 2},  [0xEA] = {"NOP", disimpl, 2},
    [0xEC] = {"CPX", disab, 4},   [0xED] = {"SBC", disab, 4},
    [0xEE] = {"INC", disab, 6},   [0xF0] = {"BEQ", disrel, 2},
    [0xF1] = {"SBC", disidy, 5},  [0xF5] = {"SBC", diszpx, 4},
    [0xF6] = {"INC", diszpx, 6},  [0xF8] = {"SED", disimpl, 2},
    [0xF9] = {"SBC", disaby, 4},  [0xFD] = {"SBC", disabx, 4},
    [0xFE] = {"INC", disabx, 7},
};

U16 disasm(U16 addr, U16 *cytot) {
  fprintf(stderr, "%04X ", addr);
  U8 op = MEM[addr++];
  DisEntry const *entry = &DISASM_TABLE[op];
  U16 cycles;
  if (entry->fn) {
    addr = entry->fn(op, addr, entry->mne);
    cycles = entry->cycles;
  } else {
    addr = disimpl(op, addr, "ILL");
    cycles = 2;
  }
  *cytot += cycles;
  fprintf(stderr, "/  %4d  +%d\n", *cytot, cycles);
  return addr;
}

void flag(U8 flag, bool condition) {
  if (condition) {
    P |= flag;
  } else {
    P &= ~flag;
  }
}

U8 *impl() { return NULL; }

U8 *acc() { return &A; }

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
  U8 lo = MEM[PC++];
  U8 hi = MEM[PC++];
  U16 addr = (((U16)hi) << 8) | lo;
  return &MEM[addr];
}

U8 *abx() {
  U8 lo = MEM[PC++];
  U8 hi = MEM[PC++];
  U16 addr = ((((U16)hi) << 8) | lo) + X;
  return &MEM[addr];
}

U8 *aby() {
  U8 lo = MEM[PC++];
  U8 hi = MEM[PC++];
  U16 addr = ((((U16)hi) << 8) | lo) + Y;
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

U8 *jab() {
  U16 addr = PC;
  PC += 2;
  return &MEM[addr];
}

U8 *jid() {
  U8 idlo = MEM[PC++];
  U8 idhi = MEM[PC++];
  U16 ptr = (((U16)idhi) << 8) | idlo;
  U8 lo = MEM[ptr];
  U8 hi = MEM[(U8)(ptr + 1)]; // Page wrapping
  U16 addr = (((U16)hi) << 8) | lo;
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

void bcc(U8 *val) {
  if (!(P & FLAG_CARRY)) {
    PC += (I8)*val;
  }
}

void bcs(U8 *val) {
  if (P & FLAG_CARRY) {
    PC += (I8)*val;
  }
}

void beq(U8 *val) {
  if (P & FLAG_ZERO) {
    PC += (I8)*val;
  }
}

void bit(U8 *val) {
  U8 res = A & *val;
  flag(FLAG_ZERO, res == 0);
  flag(FLAG_OVERFLOW, (*val & 0x40) != 0);
  flag(FLAG_NEGATIVE, (*val & 0x80) != 0);
}

void bmi(U8 *val) {
  if (P & FLAG_NEGATIVE) {
    PC += (I8)*val;
  }
}

void bne(U8 *val) {
  if (!(P & FLAG_ZERO)) {
    PC += (I8)*val;
  }
}

void bpl(U8 *val) {
  if (!(P & FLAG_NEGATIVE)) {
    PC += (I8)*val;
  }
}

void brk_(U8 *val) {
  (void)val;
  debug = true;
  ++PC;
  MEM[0x0100 + SP--] = (U8)((PC >> 8) & 0xFF);
  MEM[0x0100 + SP--] = (U8)(PC & 0xFF);
  MEM[0x0100 + SP--] = P | FLAG_BREAK | FLAG_UNUSED;
  flag(FLAG_INTERRUPT, true);
  U8 lo = MEM[0xFFFE];
  U8 hi = MEM[0xFFFF];
  PC = (((U16)hi) << 8) | lo;
}

void bvc(U8 *val) {
  if (!(P & FLAG_OVERFLOW)) {
    PC += (I8)*val;
  }
}

void bvs(U8 *val) {
  if (P & FLAG_OVERFLOW) {
    PC += (I8)*val;
  }
}

void clc(U8 *val) {
  (void)val;
  flag(FLAG_CARRY, false);
}

void cld(U8 *val) {
  (void)val;
  flag(FLAG_DECIMAL, false);
}

void cli(U8 *val) {
  (void)val;
  flag(FLAG_INTERRUPT, false);
}

void clv(U8 *val) {
  (void)val;
  flag(FLAG_OVERFLOW, false);
}

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

void dex(U8 *val) {
  (void)val;
  --X;
  flag(FLAG_ZERO, X == 0);
  flag(FLAG_NEGATIVE, (X & 0x80) != 0);
}

void dey(U8 *val) {
  (void)val;
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

void inx(U8 *val) {
  (void)val;
  ++X;
  flag(FLAG_ZERO, X == 0);
  flag(FLAG_NEGATIVE, (X & 0x80) != 0);
}

void iny(U8 *val) {
  (void)val;
  ++Y;
  flag(FLAG_ZERO, Y == 0);
  flag(FLAG_NEGATIVE, (Y & 0x80) != 0);
}

void jmp(U8 *val) { PC = *(U16 *)val; }

void jsr(U8 *val) {
  U16 ret = PC - 1;
  MEM[0x0100 + SP--] = (U8)((ret >> 8) & 0xFF);
  MEM[0x0100 + SP--] = (U8)(ret & 0xFF);
  PC = *(U16 *)val;
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

void nop(U8 *val) { (void)val; }

void ora(U8 *val) {
  A |= *val;
  flag(FLAG_ZERO, A == 0);
  flag(FLAG_NEGATIVE, (A & 0x80) != 0);
}

void pha(U8 *val) {
  (void)val;
  MEM[0x0100 + SP--] = A;
}

void php(U8 *val) {
  (void)val;
  MEM[0x0100 + SP--] = P | FLAG_BREAK | FLAG_UNUSED;
}

void pla(U8 *val) {
  (void)val;
  A = MEM[0x0100 + ++SP];
  flag(FLAG_ZERO, A == 0);
  flag(FLAG_NEGATIVE, (A & 0x80) != 0);
}

void plp(U8 *val) {
  (void)val;
  P = MEM[0x0100 + ++SP] & ~(FLAG_BREAK | FLAG_UNUSED);
}

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

void rti(U8 *val) {
  (void)val;
  P = MEM[0x0100 + ++SP] & ~(FLAG_BREAK | FLAG_UNUSED);
  U8 lo = MEM[0x0100 + ++SP];
  U8 hi = MEM[0x0100 + ++SP];
  PC = (((U16)hi) << 8) | lo;
}

void rts(U8 *val) {
  (void)val;
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

void sec(U8 *val) {
  (void)val;
  flag(FLAG_CARRY, true);
}

void sed(U8 *val) {
  (void)val;
  flag(FLAG_DECIMAL, true);
}

void sei(U8 *val) {
  (void)val;
  flag(FLAG_INTERRUPT, true);
}

void sta(U8 *addr) { *addr = A; }

void stx(U8 *addr) { *addr = X; }

void sty(U8 *addr) { *addr = Y; }

void tax(U8 *val) {
  (void)val;
  X = A;
  flag(FLAG_ZERO, X == 0);
  flag(FLAG_NEGATIVE, (X & 0x80) != 0);
}

void tay(U8 *val) {
  (void)val;
  Y = A;
  flag(FLAG_ZERO, Y == 0);
  flag(FLAG_NEGATIVE, (Y & 0x80) != 0);
}

void tsx(U8 *val) {
  (void)val;
  X = SP;
  flag(FLAG_ZERO, X == 0);
  flag(FLAG_NEGATIVE, (X & 0x80) != 0);
}

void txa(U8 *val) {
  (void)val;
  A = X;
  flag(FLAG_ZERO, A == 0);
  flag(FLAG_NEGATIVE, (A & 0x80) != 0);
}

void txs(U8 *val) {
  (void)val;
  SP = X;
}

void tya(U8 *val) {
  (void)val;
  A = Y;
  flag(FLAG_ZERO, A == 0);
  flag(FLAG_NEGATIVE, (A & 0x80) != 0);
}

typedef void (*ExecFn)(U8 *);
typedef U8 *(*AddrFn)();

typedef struct {
  ExecFn exec;
  AddrFn addr;
} OpEntry;

OpEntry const OP_TABLE[256] = {
    [0x00] = {brk_, imm}, [0x01] = {ora, idx},  [0x05] = {ora, zp},
    [0x06] = {asl, zp},   [0x08] = {php, impl}, [0x09] = {ora, imm},
    [0x0A] = {asl, acc},  [0x0D] = {ora, ab},   [0x0E] = {asl, ab},
    [0x10] = {bpl, imm},  [0x11] = {ora, idy},  [0x15] = {ora, zpx},
    [0x16] = {asl, zpx},  [0x18] = {clc, impl}, [0x19] = {ora, aby},
    [0x1D] = {ora, abx},  [0x1E] = {asl, abx},  [0x20] = {jsr, jab},
    [0x21] = {and, idx},  [0x24] = {bit, zp},   [0x25] = {and, zp},
    [0x26] = {rol, zp},   [0x28] = {plp, impl}, [0x29] = {and, imm},
    [0x2A] = {rol, acc},  [0x2C] = {bit, ab},   [0x2D] = {and, ab},
    [0x2E] = {rol, ab},   [0x30] = {bmi, imm},  [0x31] = {and, idy},
    [0x35] = {and, zpx},  [0x36] = {rol, zpx},  [0x38] = {sec, impl},
    [0x39] = {and, aby},  [0x3D] = {and, abx},  [0x3E] = {rol, abx},
    [0x40] = {rti, impl}, [0x41] = {eor, idx},  [0x45] = {eor, zp},
    [0x46] = {lsr, zp},   [0x48] = {pha, impl}, [0x49] = {eor, imm},
    [0x4A] = {lsr, acc},  [0x4C] = {jmp, jab},  [0x4D] = {eor, ab},
    [0x4E] = {lsr, ab},   [0x50] = {bvc, imm},  [0x51] = {eor, idy},
    [0x55] = {eor, zpx},  [0x56] = {lsr, zpx},  [0x58] = {cli, impl},
    [0x59] = {eor, aby},  [0x5D] = {eor, abx},  [0x5E] = {lsr, abx},
    [0x60] = {rts, impl}, [0x61] = {adc, idx},  [0x65] = {adc, zp},
    [0x66] = {ror, zp},   [0x68] = {pla, impl}, [0x69] = {adc, imm},
    [0x6A] = {ror, acc},  [0x6C] = {jmp, jid},  [0x6D] = {adc, ab},
    [0x6E] = {ror, ab},   [0x70] = {bvs, imm},  [0x71] = {adc, idy},
    [0x75] = {adc, zpx},  [0x76] = {ror, zpx},  [0x78] = {sei, impl},
    [0x79] = {adc, aby},  [0x7D] = {adc, abx},  [0x7E] = {ror, abx},
    [0x81] = {sta, idx},  [0x84] = {sty, zp},   [0x85] = {sta, zp},
    [0x86] = {stx, zp},   [0x88] = {dey, impl}, [0x8A] = {txa, impl},
    [0x8C] = {sty, ab},   [0x8D] = {sta, ab},   [0x8E] = {stx, ab},
    [0x90] = {bcc, imm},  [0x91] = {sta, idy},  [0x94] = {sty, zpx},
    [0x95] = {sta, zpx},  [0x96] = {stx, zpy},  [0x98] = {tya, impl},
    [0x99] = {sta, aby},  [0x9A] = {txs, impl}, [0x9D] = {sta, abx},
    [0xA0] = {ldy, imm},  [0xA1] = {lda, idx},  [0xA2] = {ldx, imm},
    [0xA4] = {ldy, zp},   [0xA5] = {lda, zp},   [0xA6] = {ldx, zp},
    [0xA8] = {tay, impl}, [0xA9] = {lda, imm},  [0xAA] = {tax, impl},
    [0xAC] = {ldy, ab},   [0xAD] = {lda, ab},   [0xAE] = {ldx, ab},
    [0xB0] = {bcs, imm},  [0xB1] = {lda, idy},  [0xB4] = {ldy, zpx},
    [0xB5] = {lda, zpx},  [0xB6] = {ldx, zpy},  [0xB8] = {clv, impl},
    [0xB9] = {lda, aby},  [0xBA] = {tsx, impl}, [0xBC] = {ldy, abx},
    [0xBD] = {lda, abx},  [0xBE] = {ldx, aby},  [0xC0] = {cpy, imm},
    [0xC1] = {cmp, idx},  [0xC4] = {cpy, zp},   [0xC5] = {cmp, zp},
    [0xC6] = {dec, zp},   [0xC8] = {iny, impl}, [0xC9] = {cmp, imm},
    [0xCA] = {dex, impl}, [0xCC] = {cpy, ab},   [0xCD] = {cmp, ab},
    [0xCE] = {dec, ab},   [0xD0] = {bne, imm},  [0xD1] = {cmp, idy},
    [0xD5] = {cmp, zpx},  [0xD6] = {dec, zpx},  [0xD8] = {cld, impl},
    [0xD9] = {cmp, aby},  [0xDD] = {cmp, abx},  [0xDE] = {dec, abx},
    [0xE0] = {cpx, imm},  [0xE1] = {sbc, idx},  [0xE4] = {cpx, zp},
    [0xE5] = {sbc, zp},   [0xE6] = {inc, zp},   [0xE8] = {inx, impl},
    [0xE9] = {sbc, imm},  [0xEA] = {nop, impl}, [0xEC] = {cpx, ab},
    [0xED] = {sbc, ab},   [0xEE] = {inc, ab},   [0xF0] = {beq, imm},
    [0xF1] = {sbc, idy},  [0xF5] = {sbc, zpx},  [0xF6] = {inc, zpx},
    [0xF8] = {sed, impl}, [0xF9] = {sbc, aby},  [0xFD] = {sbc, abx},
    [0xFE] = {inc, abx},
};

void doop() {
  U8 op = MEM[PC++];
  OpEntry const *entry = &OP_TABLE[op];
  if (entry->exec) {
    entry->exec(entry->addr());
  } else {
    --PC;
    fprintf(stderr, "Illegal instruction: $%02X\n", op);
    debug = true;
  }
}
