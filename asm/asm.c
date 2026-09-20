#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "asm.h"
#include "opcodes.h"

static void help(char const *name) {
  fprintf(
      stderr,
      "Usage: %s [options] <asm-file>\n\n"
      "Options:\n\n"
      "  -o, --output <path>          Write output to file (default: stdout)\n"
      "  -d, --debug <path>           Emit debug symbols to file\n"
      "  -D, ----define <NAME=VALUE>  Predefine symbol\n"
      "  -h, --help                   Show this help message\n",
      name);
}

static FILE *outFile = NULL;
static char const *outFilePath = NULL;
static char const *symFilePath = NULL;

#define STACK_SIZE 32
static Lex STACK[STACK_SIZE];
static Lex *ls;

const char **interned = NULL;
static UInt internedCap = 0;

static Bool emit = FALSE;
static Bool defining = FALSE;
static char const *scope = NULL;
static U16 pc = 0;

static Sym *syms = NULL;
static UInt symsLen = 0;
static UInt symsCap = 0;

static Macro *macros = NULL;
static UInt macrosLen = 0;
static UInt macrosCap = 0;

static FILE *openFile(char const *path, char const *mode);
static void closeFile(FILE *hnd);
static void pushFile(FILE *hnd, char const *path);

static void pass();
static void rewindPass();

int main(int argc, char const *const *argv) {
  if (argc < 2) {
    help(argv[0]);
    return EXIT_FAILURE;
  }
  outFile = stdout;
  for (int argi = 1; argi < argc; ++argi) {
    if ((strcmp(argv[argi], "-h") == 0) ||
        (strcmp(argv[argi], "--help") == 0)) {
      help(argv[0]);
      return EXIT_SUCCESS;
    }
    if ((strcmp(argv[argi], "-o") == 0) ||
        (strcmp(argv[argi], "--output") == 0)) {
      ++argi;
      if (argi == argc) {
        fprintf(stderr, "No output file specified\n");
        return EXIT_FAILURE;
      }
      outFilePath = argv[argi];
      continue;
    }
    if ((strcmp(argv[argi], "-d") == 0) ||
        (strcmp(argv[argi], "--debug") == 0)) {
      ++argi;
      if (argi == argc) {
        fprintf(stderr, "No symbol file specified\n");
        return EXIT_FAILURE;
      }
      symFilePath = argv[argi];
      continue;
    }
    FILE *hnd = openFile(argv[argi], "rb");
    pushFile(hnd, argv[argi]);
    ++argi;
    if (argi != argc) {
      fprintf(stderr, "Unexpected option: %s\n", argv[argi]);
      return EXIT_FAILURE;
    }
  }

  pass();
  rewindPass();
  emit = TRUE;
  pass();

  if (outFilePath) {
    outFile = openFile(outFilePath, "wb+");
  } else {
    outFilePath = "<stdout>";
  }

  closeFile(outFile);
  return EXIT_SUCCESS;
}

NORETURN void panic(char const *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  panicV(fmt, args);
  va_end(args);
}

NORETURN void panicV(char const *fmt, va_list args) {
  vfprintf(stderr, fmt, args);
  exit(EXIT_FAILURE);
}

NORETURN void fatal(char const *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  lexFatal(ls, fmt, args);
  va_end(args);
}

NORETURN void fatalLoc(Loc loc, char const *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  lexFatalLoc(ls, loc, fmt, args);
  va_end(args);
}

U8 peek() { return lexPeek(ls); }

void eat() { lexEat(ls); }

void expect(U8 tok) {
  U8 seen = peek();
  if (seen != tok) {
    fatal("Expected %s, but got %s\n", tokName(tok), tokName(seen));
  }
}

char const *intern(char const *str) {
  if (!interned) {
    internedCap = 16;
    interned = malloc(sizeof(char *) * internedCap);
    interned[0] = NULL;
  }
  char const **iter = interned;
  while (*iter) {
    if (strcmp(*iter, str) == 0) {
      return *iter;
    }
    ++iter;
  }
  UInt idx = iter - interned;
  if (idx == (internedCap - 1)) {
    internedCap *= 2;
    interned = realloc(interned, sizeof(char *) * internedCap);
  }
  interned[idx] = strdup(str);
  interned[idx + 1] = NULL;
  return interned[idx];
}

Lex *getLex() { return ls; }

char const *getScope() { return scope; }

U16 getPC() { return pc; }

Sym *addSym(char const *lbl, Sym sym) {
  return symCat(&syms, &symsLen, &symsCap, sym);
}

Sym *findSym(char const *lbl) { return symFind(syms, symsLen, lbl); }

static void expectEOL() {
  U8 seen = peek();
  switch (peek()) {
  case TOK_EOF:
  case '\n':
    return;
  default:
    fatal("Expected end of line, but got %s\n", tokName(seen));
  }
}

static Mnemonic const *findMnemonic(char const *name) {
  for (UInt i = 0; i < (sizeof(MNEMONICS) / sizeof(MNEMONICS[0])); ++i) {
    if (strcasecmp(MNEMONICS[i].name, name) == 0) {
      return &MNEMONICS[i];
    }
  }
  return NULL;
}

static void emitByte(U8 byte) {
  if (fputc(byte, outFile) == EOF) {
    panic("Failed to write to output file: %s\n", strerror(errno));
  }
}

static void emitWord(U16 word) {
  emitByte((U8)(word & 0xFF));
  emitByte((U8)((word >> 8) & 0xFF));
}

static void eatMnemonic(Mnemonic const *mne) {
  Loc mneLoc = lexLoc(ls);
  eat();
  U8 bzpOp = mne->opcodes[ADDR_BZP];
  U8 bzrOp = mne->opcodes[ADDR_BZR];
  if ((bzpOp != ILLEGAL) || (bzrOp != ILLEGAL)) {
    UInt bitLen;
    UInt bitCap;
    Loc bitLoc;
    Expr *bitExpr = exprEatLoc(&bitLen, &bitCap, &bitLoc);
    I32 bit;
    if (emit) {
      if (!exprSolve(bitExpr, bitLen, &bit)) {
        fatalLoc(bitLoc, "Bit number must be known\n");
      }
      if ((bit < 0) || (bit > 7)) {
        fatalLoc(bitLoc, "Bit number must be between 0 and 7\n");
      }
    }
    expect(',');
    eat();
    UInt addrLen;
    UInt addrCap;
    Loc addrLoc;
    Expr *addrExpr = exprEatLoc(&addrLen, &addrCap, &addrLoc);
    I32 addr;
    if (emit) {
      if (!exprSolve(addrExpr, addrLen, &addr)) {
        fatalLoc(addrLoc, "Zero-page address must be known\n");
      }
      if (!exprCanReprU8(addr)) {
        fatalLoc(addrLoc, "Zero-page address must be between $00 and $FF\n");
      }
    }
    expect(',');
    eat();
    UInt relLen;
    UInt relCap;
    Loc relLoc;
    Expr *relExpr = exprEatLoc(&relLen, &relCap, &relLoc);
    I32 rel;
    if (emit) {
      if (!exprSolve(relExpr, relLen, &rel)) {
        fatalLoc(relLoc, "Branch address must be known\n");
      }
      I32 relOffset = rel - (pc + 3);
      if (!exprCanReprI8(relOffset)) {
        fatalLoc(relLoc, "Branch distance too far: %d bytes\n", relOffset);
      }
      U8 baseOp = (bzpOp != ILLEGAL) ? bzpOp : bzrOp;
      U8 opcode = baseOp + (bit * 16);
      emitByte(opcode);
      emitByte((U8)addr);
      emitByte((U8)relOffset);
    }
    free(bitExpr);
    free(addrExpr);
    free(relExpr);
    pc += 3;
    return;
  }
  if (peek() == '#') {
    U8 opcode = mne->opcodes[ADDR_IMM];
    if (opcode == ILLEGAL) {
      fatalLoc(mneLoc, "Instruction does not support IMM addressing\n");
    }
    eat();
    UInt valLen;
    UInt valCap;
    Loc valLoc;
    Expr *valExpr = exprEatLoc(&valLen, &valCap, &valLoc);
    I32 val;
    if (emit) {
      if (!exprSolve(valExpr, valLen, &val)) {
        fatalLoc(valLoc, "Immediate value must be known\n");
      }
      if (!exprCanReprU8(val)) {
        fatalLoc(valLoc, "Immediate value must fit in a byte\n");
      }
      emitByte(opcode);
      emitByte((U8)val);
    }
    free(valExpr);
    pc += 2;
    return;
  }
  if (peek() == '(') {
    eat();
    Bool isZp = (peek() == '<');
    UInt addrLen;
    UInt addrCap;
    Loc addrLoc;
    Expr *addrExpr = exprEatLoc(&addrLen, &addrCap, &addrLoc);
    I32 addr;
    if (exprSolve(addrExpr, addrLen, &addr)) {
      isZp = exprCanReprU8(addr);
    } else if (emit) {
      fatalLoc(addrLoc, "Address must be known\n");
    }
    if (isZp) {
      if (peek() == ',') {
        U8 opcode = mne->opcodes[ADDR_IZX];
        if (opcode == ILLEGAL) {
          fatalLoc(mneLoc, "Instruction does not support IZX addressing\n");
        }
        eat();
        expect(TOK_X);
        eat();
        expect(')');
        eat();
        if (emit) {
          emitByte(opcode);
          emitByte((U8)addr);
        }
        free(addrExpr);
        pc += 2;
        return;
      }
      expect(')');
      eat();
      if (peek() == ',') {
        U8 opcode = mne->opcodes[ADDR_IZY];
        if (opcode == ILLEGAL) {
          fatalLoc(mneLoc, "Instruction does not support IZY addressing\n");
        }
        eat();
        expect(TOK_Y);
        eat();
        if (emit) {
          emitByte(opcode);
          emitByte((U8)addr);
        }
        free(addrExpr);
        pc += 2;
        return;
      }
      U8 opcode = mne->opcodes[ADDR_IZP];
      if (opcode == ILLEGAL) {
        fatalLoc(mneLoc, "Instruction does not support IZP addressing\n");
      }
      if (emit) {
        emitByte(opcode);
        emitByte((U8)addr);
      }
      free(addrExpr);
      pc += 2;
      return;
    }
    U8 opcode = mne->opcodes[ADDR_IAX];
    if (opcode == ILLEGAL) {
      fatalLoc(mneLoc, "Instruction does not support IAX addressing\n");
    }
    expect(',');
    eat();
    expect(TOK_X);
    eat();
    expect(')');
    eat();
    if (emit) {
      if (!exprCanReprU16(addr)) {
        fatalLoc(addrLoc, "Address must fit in word\n");
      }
      emitByte(opcode);
      emitWord((U16)addr);
    }
    free(addrExpr);
    pc += 3;
    return;
  }
  U8 relOp = mne->opcodes[ADDR_REL];
  if (relOp != ILLEGAL) {
    UInt relLen;
    UInt relCap;
    Loc relLoc;
    Expr *relExpr = exprEatLoc(&relLen, &relCap, &relLoc);
    I32 rel;
    if (emit) {
      if (!exprSolve(relExpr, relLen, &rel)) {
        fatalLoc(relLoc, "Branch address must be known\n");
      }
      I32 relOffset = rel - (pc + 2);
      if (!exprCanReprI8(relOffset)) {
        fatalLoc(relLoc, "Branch distance too far: %d bytes\n", relOffset);
      }
      emitByte(relOp);
      emitByte((U8)relOffset);
    }
    free(relExpr);
    pc += 2;
    return;
  }
  if ((peek() == TOK_EOF) || (peek() == '\n')) {
    U8 opcode = mne->opcodes[ADDR_IMP];
    if (opcode == ILLEGAL) {
      fatalLoc(mneLoc, "Instruction does not support IMP addressing\n");
    }
    if (emit) {
      emitByte(opcode);
    }
    pc += 1;
    return;
  }
  Bool isZp = (peek() == '<');
  UInt addrLen;
  UInt addrCap;
  Loc addrLoc;
  Expr *addrExpr = exprEatLoc(&addrLen, &addrCap, &addrLoc);
  I32 addr;
  if (exprSolve(addrExpr, addrLen, &addr)) {
    isZp = exprCanReprU8(addr);
  } else if (emit) {
    fatalLoc(addrLoc, "Address must be known\n");
  }
  if (isZp) {
    if (peek() == ',') {
      eat();
      if (peek() == TOK_X) {
        U8 opcode = mne->opcodes[ADDR_ZPX];
        if (opcode == ILLEGAL) {
          fatalLoc(mneLoc, "Instruction does not support ZPX addressing\n");
        }
        eat();
        if (emit) {
          emitByte(opcode);
          emitByte((U8)addr);
        }
        free(addrExpr);
        pc += 2;
        return;
      }
      expect(TOK_Y);
      eat();
      U8 opcode = mne->opcodes[ADDR_ZPY];
      if (opcode == ILLEGAL) {
        fatalLoc(mneLoc, "Instruction does not support ZPY addressing\n");
      }
      if (emit) {
        emitByte(opcode);
        emitByte((U8)addr);
      }
      free(addrExpr);
      pc += 2;
      return;
    }
    U8 opcode = mne->opcodes[ADDR_ZPG];
    if (opcode == ILLEGAL) {
      fatalLoc(mneLoc, "Instruction does not support ZPG addressing\n");
    }
    if (emit) {
      emitByte(opcode);
      emitByte((U8)addr);
    }
    free(addrExpr);
    pc += 2;
    return;
  }
  if (peek() == ',') {
    eat();
    if (peek() == TOK_X) {
      U8 opcode = mne->opcodes[ADDR_ABX];
      if (opcode == ILLEGAL) {
        fatalLoc(mneLoc, "Instruction does not support ABX addressing\n");
      }
      eat();
      if (emit) {
        if (!exprCanReprU16(addr)) {
          fatalLoc(addrLoc, "Address must fit in word\n");
        }
        emitByte(opcode);
        emitWord((U16)addr);
      }
      free(addrExpr);
      pc += 3;
      return;
    }
    expect(TOK_Y);
    eat();
    U8 opcode = mne->opcodes[ADDR_ABY];
    if (opcode == ILLEGAL) {
      fatalLoc(mneLoc, "Instruction does not support ABY addressing\n");
    }
    if (emit) {
      if (!exprCanReprU16(addr)) {
        fatalLoc(addrLoc, "Address must fit in word\n");
      }
      emitByte(opcode);
      emitWord((U16)addr);
    }
    free(addrExpr);
    pc += 3;
    return;
  }
  U8 opcode = mne->opcodes[ADDR_ABS];
  if (opcode == ILLEGAL) {
    fatalLoc(mneLoc, "Instruction does not support ABS addressing\n");
  }
  if (emit) {
    if (!exprCanReprU16(addr)) {
      fatalLoc(addrLoc, "Address must fit in word\n");
    }
    emitByte(opcode);
    emitWord((U16)addr);
  }
  free(addrExpr);
  pc += 3;
}

static Expr *constExpr(I32 num) {
  Expr *exprs = malloc(sizeof(Expr));
  exprs[0] = (Expr){.kind = EXPR_CONST, .num = num};
  return exprs;
}

static Bool lblIsGlobal(char const *lbl) {
  UInt len = strlen(lbl);
  return memchr(lbl, '.', len) == NULL;
}

static void eatDirective() {
  Loc dirLoc = lexLoc(ls);
  switch (peek()) {
  case TOK_DB:
    eat();
    return;
  case TOK_DW:
    return;
  }
}

static void pass() {
  while (peek() != TOK_EOF) {
    switch (peek()) {
    case '\n':
      eat();
      continue;
    case '*':
      eat();
      expect('=');
      eat();
      pc = exprEatSolvedU16();
      expectEOL();
      eat();
      continue;
    case TOK_ID: {
      Mnemonic const *mne = findMnemonic(lexTxt(ls));
      if (mne) {
        eatMnemonic(mne);
        expectEOL();
        eat();
        continue;
      }
      Loc loc = lexLoc(ls);
      char const *lbl = lexLabel(ls);
      eat();
      Sym *sym = findSym(lbl);
      if (!sym) {
        sym = addSym(
            lbl, (Sym){.lbl = lbl, .exprs = NULL, .exprsLen = 0, .loc = loc});
      } else if (!emit) {
        fatalLoc(loc,
                 "Label %s is already defined\n\t%s:%" UINT_FMT ":%" UINT_FMT
                 ": First defined here\n",
                 lbl, sym->loc.name, sym->loc.line, sym->loc.col);
      }
      switch (peek()) {
      case ':':
        eat();
        break;
      case '=':
        eat();
        if (emit) {
          I32 val = exprEatSolvedLoc(&loc);
          sym->exprs = constExpr(val);
          sym->exprsLen = 1;
        } else {
          UInt valLen;
          UInt valCap;
          Expr *valExpr = exprEat(&valLen, &valCap);
          I32 val;
          if (exprSolve(valExpr, valLen, &val)) {
            sym->exprs = constExpr(val);
            sym->exprsLen = 1;
          } else {
            --symsLen;
          }
          free(valExpr);
        }
        expectEOL();
        eat();
        continue;
      default:
        if (!lblIsGlobal(lbl)) {
          fatal("Expected `:` or `=`\n");
        }
        fatalLoc(loc, "Unrecognized instruction or directive\n");
      }
      if (lblIsGlobal(lbl)) {
        scope = lbl;
      }
      sym->exprs = constExpr(pc);
      sym->exprsLen = 1;
      continue;
    }
    default:
      eatDirective();
    }
  }
}

static void rewindPass() {
  lexRewind(ls);
  macros = NULL;
  pc = 0;
  defining = FALSE;
  scope = NULL;
}

static FILE *openFile(char const *path, char const *mode) {
  FILE *hnd = fopen(path, mode);
  if (!hnd) {
    panic("Could not open file: %s\n", path);
  }
  return hnd;
}

static void closeFile(FILE *hnd) {
  if (fclose(hnd) == EOF) {
    panic("Failed to close file: %s\n", strerror(errno));
  }
}

static void pushFile(FILE *hnd, char const *path) {
  ++ls;
  if (ls == (STACK + STACK_SIZE)) {
    panic("Too many nested files\n");
  }
  lexFileInit(ls, path, hnd);
}
