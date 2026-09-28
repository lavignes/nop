#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "asm.h"
#include "opcodes.h"

static void help(char const *name) {
  fprintf(
      stderr,
      "Usage: %s [options] <asm-file>\n"
      "\n"
      "Options:\n"
      "\n"
      "  -o, --output <path>          Write output to file (default: stdout)\n"
      "  -d, --debug <path>           Emit debug symbols to file\n"
      "  -D, --define <NAME=VALUE>    Predefine symbol\n"
      "  -h, --help                   Show this help message\n",
      name);
}

static FILE *outFile = NULL;
static char const *outFilePath = NULL;
static char const *symFilePath = NULL;

#define STACK_SIZE 32
static Lex STACK[STACK_SIZE];
static Lex *ls = STACK - 1;

const char **interned = NULL;
static UInt internedCap = 0;

static Bool emit = FALSE;
static Bool defining = FALSE;
static char const *scope = NULL;
static U16 pc = 0;
static U8 cpu = CPU_6502;
static U8 encoding = ENCODING_ASCII;

static Sym *syms = NULL;
static UInt symsLen = 0;
static UInt symsCap = 0;

static Macro *macros = NULL;
static UInt macrosLen = 0;
static UInt macrosCap = 0;

static FILE *openFile(char const *path, char const *mode);
static void closeFile(FILE *hnd);
static void pushFile(FILE *hnd, char const *path);

static U8 findCpu(char const *name);

static void pass();
static void rewindPass();

static Expr *constExpr(I32 num);

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
    if ((strcmp(argv[argi], "-D") == 0) ||
        (strcmp(argv[argi], "--define") == 0)) {
      ++argi;
      if (argi == argc) {
        fprintf(stderr, "No symbol definition specified\n");
        return EXIT_FAILURE;
      }
      char const *name = argv[argi];
      char const *eq = strchr(name, '=');
      if (!eq) {
        fprintf(stderr, "Invalid symbol definition: %s\n", name);
        return EXIT_FAILURE;
      }
      char const *val = eq + 1;
      long num = strtol(val, NULL, 10);
      if ((num == LONG_MIN) || (num == LONG_MAX)) {
        fprintf(stderr, "Invalid symbol value: %s\n", val);
        return EXIT_FAILURE;
      }
      Sym sym = {0};
      sym.lbl = internN(name, eq - name);
      sym.exprs = constExpr((I32)num);
      sym.exprsLen = 1;
      sym.loc.name = "<command line>";
      sym.loc.line = 1;
      sym.loc.col = 1;
      addSym(sym.lbl, sym);
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

  if (outFilePath) {
    outFile = openFile(outFilePath, "wb+");
  } else {
    outFilePath = "<stdout>";
  }

  emit = TRUE;
  pass();

  closeFile(outFile);

  if (symFilePath) {
    FILE *symFile = openFile(symFilePath, "wb+");
    for (UInt i = 0; i < symsLen; ++i) {
      Sym *sym = syms + i;
      fprintf(symFile, "al C:%04x .%s\n", sym->exprs[0].num, sym->lbl);
    }
    closeFile(symFile);
  }

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

void logs(char const *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  logsV(fmt, args);
  va_end(args);
}

void logsV(char const *fmt, va_list args) { vfprintf(stderr, fmt, args); }

NORETURN void fatal(char const *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  lexFatalV(ls, fmt, args);
  va_end(args);
}

NORETURN void fatalLoc(Loc loc, char const *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  lexFatalLocV(ls, loc, fmt, args);
  va_end(args);
}

void warn(char const *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  lexWarnV(ls, fmt, args);
  va_end(args);
}

void warnLoc(Loc loc, char const *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  lexWarnLocV(ls, loc, fmt, args);
  va_end(args);
}

static void invokeMacro(Macro *macro) {
  Loc loc = lexLoc(ls);
  eat();
  Arg *args = NULL;
  UInt argsLen = 0;
  UInt argsCap = 0;
  MacroTok *toks = NULL;
  UInt toksLen = 0;
  UInt toksCap = 0;
  UInt depth = 0;
  if (peek() == '(') {
    eat();
    ++depth;
  }
  while (TRUE) {
    switch (peek()) {
    case '\n':
    case TOK_EOF:
      if (depth == 0) {
        goto flush;
      }
      break;
    case TOK_ID:
      macroTokCat(&toks, &toksLen, &toksCap,
                  (MacroTok){.kind = MACRO_ID,
                             .loc = lexLoc(ls),
                             .txt = intern(lexLbl(ls))});
      break;
    case TOK_NUM:
      macroTokCat(
          &toks, &toksLen, &toksCap,
          (MacroTok){.kind = MACRO_NUM, .loc = lexLoc(ls), .num = lexNum(ls)});
      break;
    case TOK_STR:
      macroTokCat(&toks, &toksLen, &toksCap,
                  (MacroTok){.kind = MACRO_STR,
                             .loc = lexLoc(ls),
                             .txt = intern(lexTxt(ls))});
      break;
    default:
      if (depth > 0) {
        if (peek() == '(') {
          ++depth;
        } else if (peek() == ')') {
          --depth;
          if (depth == 0) {
            eat();
            goto flush;
          }
        }
      }
      macroTokCat(
          &toks, &toksLen, &toksCap,
          (MacroTok){.kind = MACRO_TOK, .loc = lexLoc(ls), .tok = peek()});
      break;
    }
    eat();
    if (peek() == ',') {
      eat();
      argsEnqueue(&args, &argsLen, &argsCap,
                  (Arg){.buf = toks, .bufLen = toksLen, .bufCap = toksCap});
      toks = NULL;
      toksLen = 0;
      toksCap = 0;
    }
  }
flush:
  if (toksLen > 0) {
    argsEnqueue(&args, &argsLen, &argsCap,
                (Arg){.buf = toks, .bufLen = toksLen, .bufCap = toksCap});
  }
  ++ls;
  if (ls >= (STACK + STACK_SIZE)) {
    fatal("Macro expansion stack overflow\n");
  }
  lexMacroInit(ls, loc, macro->name, macro->toks, macro->toksLen, args,
               argsLen);
}

static void invokeIf() {
  eat();
  Loc loc;
  defining = TRUE;
  Bool ignore = (exprEatSolvedLoc(&loc) == 0);
  UInt depth = 0;
  LocTok *toks = NULL;
  UInt toksLen = 0;
  UInt toksCap = 0;
  while (TRUE) {
    switch (peek()) {
    case TOK_IF:
    case TOK_MACRO:
    case TOK_REPEAT:
      ++depth;
      goto ifConsume;
    case TOK_END:
      if (depth == 0) {
        eat();
        goto flush;
      }
      --depth;
      goto ifConsume;
    case TOK_ELSE:
      if (depth == 0) {
        eat();
        ignore = !ignore;
      }
      break;
    case TOK_EOF:
      fatal("Unexpected end of file\n");
    case TOK_ID:
    case TOK_STR:
      if (!ignore) {
        locTokCat(&toks, &toksLen, &toksCap,
                  (LocTok){.tok = peek(),
                           .loc = lexLoc(ls),
                           .txt = intern(lexLbl(ls))});
      }
      break;
    case TOK_NUM:
    case TOK_ARG:
      if (!ignore) {
        locTokCat(
            &toks, &toksLen, &toksCap,
            (LocTok){.tok = peek(), .loc = lexLoc(ls), .num = lexNum(ls)});
      }
      break;
    default:
    ifConsume:
      if (!ignore) {
        locTokCat(&toks, &toksLen, &toksCap,
                  (LocTok){.tok = peek(), .loc = lexLoc(ls)});
      }
      break;
    }
    eat();
  }
flush:
  defining = FALSE;
  ++ls;
  if (ls >= (STACK + STACK_SIZE)) {
    fatal("Too many nested if/else blocks\n");
  }
  lexIfElseInit(ls, loc, toks, toksLen);
}

static Macro *findMacro(char const *lbl) {
  for (UInt i = 0; i < macrosLen; ++i) {
    if (!strcmp(macros[i].name, lbl)) {
      return &macros[i];
    }
  }
  return NULL;
}

static void popLex() {
  lexFini(ls);
  --ls;
}

U8 peek() {
  U8 tok = lexPeek(ls);
  if ((tok == TOK_EOF) && (ls > STACK)) {
    popLex();
    return peek();
  }
  if (defining) {
    return tok;
  }
  switch (tok) {
  case TOK_ID: {
    Macro *macro = findMacro(lexLbl(ls));
    if (macro) {
      invokeMacro(macro);
      return peek();
    }
    return tok;
  }
  case TOK_IF:
    invokeIf();
    return peek();
  default:
    return tok;
  }
}

void eat() { lexEat(ls); }

void expect(U8 tok) {
  U8 seen = peek();
  if (seen != tok) {
    fatal("Expected %s, but got %s\n", tokName(tok), tokName(seen));
  }
}

char const *internN(char const *str, UInt len) {
  if (!interned) {
    internedCap = 16;
    interned = malloc(sizeof(char *) * internedCap);
    interned[0] = NULL;
  }
  char const **iter = interned;
  while (*iter) {
    if (strncmp(*iter, str, len) == 0 && (*iter)[len] == '\0') {
      return *iter;
    }
    ++iter;
  }
  UInt idx = iter - interned;
  if (idx == (internedCap - 1)) {
    internedCap *= 2;
    interned = realloc(interned, sizeof(char *) * internedCap);
  }
  char *newStr = malloc(len + 1);
  strncpy(newStr, str, len);
  newStr[len] = '\0';
  interned[idx] = newStr;
  interned[idx + 1] = NULL;
  return interned[idx];
}

char const *intern(char const *str) {
  UInt len = strlen(str);
  return internN(str, len);
}

U8 getEncoding() { return encoding; }

Lex *getLex() { return ls; }

char const *getScope() { return scope; }

U16 getPC() { return pc; }

static void addPC(U16 len) { pc += len; }

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

static U8 findCpu(char const *name) {
  for (UInt i = 0; i < (sizeof(CPUS) / sizeof(CPUS[0])); ++i) {
    if (strcasecmp(CPUS[i].name, name) == 0) {
      return CPUS[i].cpu;
    }
  }
  return 0;
}

static char const *cpuName(U8 which) {
  for (UInt i = 0; i < (sizeof(CPUS) / sizeof(CPUS[0])); ++i) {
    if (CPUS[i].cpu == which) {
      return CPUS[i].name;
    }
  }
  UNREACHABLE();
  return NULL;
}

static U8 supportedCpus(Mnemonic const *mne) {
  U8 cpus = 0;
  for (UInt i = 0; i < (sizeof(mne->opcodes) / sizeof(mne->opcodes[0])); ++i) {
    cpus |= mne->opcodes[i].cpus;
  }
  return cpus;
}

static Mnemonic const *findMnemonic(char const *name) {
  for (UInt i = 0; i < (sizeof(MNEMONICS) / sizeof(MNEMONICS[0])); ++i) {
    if (strcasecmp(MNEMONICS[i].name, name) == 0) {
      return &MNEMONICS[i];
    }
  }
  return NULL;
}

static Mnemonic const *findSupportedMnemonic(char const *name) {
  Mnemonic const *mne = findMnemonic(name);
  if (mne && !(supportedCpus(mne) & cpu)) {
    return NULL;
  }
  return mne;
}

static Bool opcodeFor(Mnemonic const *mne, U8 mode, U8 *op) {
  Opcode opcode = mne->opcodes[mode];
  if (!(opcode.cpus & cpu)) {
    return FALSE;
  }
  *op = opcode.op;
  return TRUE;
}

static void emitBytes(U8 const *bytes, UInt len) {
  if (fwrite(bytes, 1, len, outFile) != len) {
    panic("Failed to write to output file: %s\n", strerror(errno));
  }
}

static void emitByte(U8 byte) { emitBytes(&byte, 1); }

static void emitWord(U16 word) {
  emitByte((U8)(word & 0xFF));
  emitByte((U8)((word >> 8) & 0xFF));
}

static void eatMnemonic(Mnemonic const *mne) {
  Loc mneLoc = lexLoc(ls);
  eat();
  U8 bzpOp;
  if (opcodeFor(mne, ADDR_BZP, &bzpOp)) {
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
    if (emit) {
      I32 addr;
      if (!exprSolve(addrExpr, addrLen, &addr)) {
        fatalLoc(addrLoc, "Zero-page address must be known\n");
      }
      if (!exprCanReprU8(addr)) {
        fatalLoc(addrLoc, "Zero-page address must be between $00 and $FF\n");
      }
      emitByte(bzpOp + (U8)(bit * 16));
      emitByte((U8)addr);
    }
    free(bitExpr);
    free(addrExpr);
    addPC(2);
    return;
  }
  U8 bzrOp;
  if (opcodeFor(mne, ADDR_BZR, &bzrOp)) {
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
        fatalLoc(addrLoc, "Zero-page address must fit in byte: $%08X\n", addr);
      }
    }
    expect(',');
    eat();
    UInt relLen;
    UInt relCap;
    Loc relLoc;
    Expr *relExpr = exprEatLoc(&relLen, &relCap, &relLoc);
    if (emit) {
      I32 rel;
      if (!exprSolve(relExpr, relLen, &rel)) {
        fatalLoc(relLoc, "Branch address must be known\n");
      }
      I32 relOffset = rel - (pc + 3);
      if (!exprCanReprI8(relOffset)) {
        fatalLoc(relLoc, "Branch distance too far: %d bytes\n", relOffset);
      }
      emitByte(bzrOp + (U8)(bit * 16));
      emitByte((U8)addr);
      emitByte((U8)relOffset);
    }
    free(bitExpr);
    free(addrExpr);
    free(relExpr);
    addPC(3);
    return;
  }
  if (peek() == '#') {
    U8 opcode;
    if (!opcodeFor(mne, ADDR_IMM, &opcode)) {
      fatalLoc(mneLoc,
               "Instruction does not support IMM addressing on CPU: \"%s\"\n",
               cpuName(cpu));
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
    addPC(2);
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
        U8 opcode;
        if (!opcodeFor(mne, ADDR_IZX, &opcode)) {
          fatalLoc(
              mneLoc,
              "Instruction does not support IZX addressing on CPU: \"%s\"\n",
              cpuName(cpu));
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
        addPC(2);
        return;
      }
      expect(')');
      eat();
      if (peek() == ',') {
        U8 opcode;
        if (!opcodeFor(mne, ADDR_IZY, &opcode)) {
          fatalLoc(
              mneLoc,
              "Instruction does not support IZY addressing on CPU: \"%s\"\n",
              cpuName(cpu));
        }
        eat();
        expect(TOK_Y);
        eat();
        if (emit) {
          emitByte(opcode);
          emitByte((U8)addr);
        }
        free(addrExpr);
        addPC(2);
        return;
      }
      U8 opcode;
      if (!opcodeFor(mne, ADDR_IZP, &opcode)) {
        fatalLoc(mneLoc,
                 "Instruction does not support IZP addressing on CPU: \"%s\"\n",
                 cpuName(cpu));
      }
      if (emit) {
        emitByte(opcode);
        emitByte((U8)addr);
      }
      free(addrExpr);
      addPC(2);
      return;
    }
    if (peek() == ')') {
      U8 opcode;
      if (!opcodeFor(mne, ADDR_IND, &opcode)) {
        fatalLoc(mneLoc,
                 "Instruction does not support IND addressing on CPU: \"%s\"\n",
                 cpuName(cpu));
      }
      eat();
      if (emit) {
        if (!exprCanReprU16(addr)) {
          fatalLoc(addrLoc, "Address must fit in word: $%08X\n", addr);
        }
        // Warn on 6502 page boundary bug for JMP indirect
        if ((cpu == CPU_6502) || (cpu == CPU_6502X)) {
          if (((addr & 0xFF) == 0xFF) && (mne == (MNEMONICS + MNE_JMP))) {
            warnLoc(addrLoc, "JMP IND address crosses page boundary: $%04X\n",
                    addr);
          }
        }
        emitByte(opcode);
        emitWord((U16)addr);
      }
      free(addrExpr);
      addPC(3);
      return;
    }
    U8 opcode;
    if (!opcodeFor(mne, ADDR_IAX, &opcode)) {
      fatalLoc(mneLoc,
               "Instruction does not support IAX addressing on CPU: \"%s\"\n",
               cpuName(cpu));
    }
    expect(',');
    eat();
    expect(TOK_X);
    eat();
    expect(')');
    eat();
    if (emit) {
      if (!exprCanReprU16(addr)) {
        fatalLoc(addrLoc, "Address must fit in word: %08X\n", addr);
      }
      emitByte(opcode);
      emitWord((U16)addr);
    }
    free(addrExpr);
    addPC(3);
    return;
  }
  U8 relOp;
  if (opcodeFor(mne, ADDR_REL, &relOp)) {
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
    addPC(2);
    return;
  }
  if ((peek() == TOK_EOF) || (peek() == '\n')) {
    U8 opcode;
    if (!opcodeFor(mne, ADDR_IMP, &opcode)) {
      fatalLoc(mneLoc,
               "Instruction does not support IMP addressing on CPU: \"%s\"\n",
               cpuName(cpu));
    }
    if (emit) {
      emitByte(opcode);
    }
    addPC(1);
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
        U8 opcode;
        if (!opcodeFor(mne, ADDR_ZPX, &opcode)) {
          fatalLoc(
              mneLoc,
              "Instruction does not support ZPX addressing on CPU: \"%s\"\n",
              cpuName(cpu));
        }
        eat();
        if (emit) {
          emitByte(opcode);
          emitByte((U8)addr);
        }
        free(addrExpr);
        addPC(2);
        return;
      }
      expect(TOK_Y);
      eat();
      U8 opcode;
      if (!opcodeFor(mne, ADDR_ZPY, &opcode)) {
        fatalLoc(mneLoc,
                 "Instruction does not support ZPY addressing on CPU: \"%s\"\n",
                 cpuName(cpu));
      }
      if (emit) {
        emitByte(opcode);
        emitByte((U8)addr);
      }
      free(addrExpr);
      addPC(2);
      return;
    }
    U8 opcode;
    if (!opcodeFor(mne, ADDR_ZPG, &opcode)) {
      fatalLoc(mneLoc,
               "Instruction does not support ZPG addressing on CPU: \"%s\"\n",
               cpuName(cpu));
    }
    if (emit) {
      emitByte(opcode);
      emitByte((U8)addr);
    }
    free(addrExpr);
    addPC(2);
    return;
  }
  if (peek() == ',') {
    eat();
    if (peek() == TOK_X) {
      U8 opcode;
      if (!opcodeFor(mne, ADDR_ABX, &opcode)) {
        fatalLoc(mneLoc,
                 "Instruction does not support ABX addressing on CPU: \"%s\"\n",
                 cpuName(cpu));
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
      addPC(3);
      return;
    }
    expect(TOK_Y);
    eat();
    U8 opcode;
    if (!opcodeFor(mne, ADDR_ABY, &opcode)) {
      fatalLoc(mneLoc,
               "Instruction does not support ABY addressing on CPU \"%s\"\n",
               cpuName(cpu));
    }
    if (emit) {
      if (!exprCanReprU16(addr)) {
        fatalLoc(addrLoc, "Address must fit in word: $%08X\n", addr);
      }
      emitByte(opcode);
      emitWord((U16)addr);
    }
    free(addrExpr);
    addPC(3);
    return;
  }
  U8 opcode;
  if (!opcodeFor(mne, ADDR_ABS, &opcode)) {
    fatalLoc(mneLoc,
             "Instruction does not support ABS addressing on CPU \"%s\"\n",
             cpuName(cpu));
  }
  if (emit) {
    if (!exprCanReprU16(addr)) {
      fatalLoc(addrLoc, "Address must fit in word: $%08X\n", addr);
    }
    emitByte(opcode);
    emitWord((U16)addr);
  }
  free(addrExpr);
  addPC(3);
}

static Expr *constExpr(I32 num) {
  Expr *exprs = malloc(sizeof(Expr));
  exprs[0] = (Expr){.kind = EXPR_CONST, .num = num};
  return exprs;
}

static Bool lblIsGlobal(char const *lbl) { return strchr(lbl, '.') == NULL; }

static char const *eatPath() {
  expect(TOK_STR);
  char const *path = lexTxt(ls);
  FILE *hnd = openFile(path, "rb");
  closeFile(hnd);
  eat();
  return path;
}

static void addMacro(char const *lbl, Macro macro) {
  if (!macros) {
    macrosLen = 0;
    macrosCap = 16;
    macros = malloc(sizeof(Macro) * macrosCap);
  }
  if (macrosLen == macrosCap) {
    macrosCap *= 2;
    macros = realloc(macros, sizeof(Macro) * macrosCap);
  }
  macros[macrosLen] = macro;
  ++macrosLen;
}

static void eatDirective() {
  Loc dirLoc = lexLoc(ls);
  switch (peek()) {
  case TOK_CPU: {
    eat();
    expect(TOK_STR);
    char const *txt = lexTxt(ls);
    cpu = findCpu(txt);
    if (!cpu) {
      fatal("Unknown CPU: \"%s\"\n", txt);
    }
    eat();
    expectEOL();
    eat();
    return;
  }
  case TOK_ENCODING: {
    eat();
    expect(TOK_STR);
    char const *txt = lexTxt(ls);
    if (strcasecmp(txt, "ascii") == 0) {
      encoding = ENCODING_ASCII;
    } else if (strcasecmp(txt, "petscii") == 0) {
      encoding = ENCODING_PETSCII;
    } else {
      fatal("Unknown encoding: \"%s\"\n", txt);
    }
    eat();
    expectEOL();
    eat();
    return;
  }
  case TOK_DB:
    eat();
    while (TRUE) {
      switch (peek()) {
      case TOK_STR: {
        char const *txt = lexTxt(ls);
        UInt len = strlen(txt);
        if (emit) {
          emitBytes((U8 *)txt, len);
        }
        addPC(len);
        eat();
        break;
      }
      default: {
        UInt len;
        UInt cap;
        Loc loc;
        Expr *expr = exprEatLoc(&len, &cap, &loc);
        if (emit) {
          I32 num;
          if (!exprSolve(expr, len, &num)) {
            fatalLoc(loc, "Byte must be known\n");
          }
          if (!exprCanReprU8(num)) {
            fatalLoc(loc, "Expression must fit in byte: $%08X\n", num);
          }
          emitByte((U8)num);
        }
        addPC(1);
        break;
      }
      }
      if (peek() != ',') {
        break;
      }
      eat();
    }
    expectEOL();
    eat();
    return;
  case TOK_DW:
    eat();
    while (TRUE) {
      UInt len;
      UInt cap;
      Loc loc;
      Expr *expr = exprEatLoc(&len, &cap, &loc);
      if (emit) {
        I32 num;
        if (!exprSolve(expr, len, &num)) {
          fatalLoc(loc, "Word must be known\n");
        }
        if (!exprCanReprU16(num)) {
          fatalLoc(loc, "Expression must fit in word: $%08X\n", num);
        }
        emitWord((U16)num);
      }
      addPC(2);
      if (peek() != ',') {
        break;
      }
      eat();
    }
    expectEOL();
    eat();
    return;
  case TOK_DS:
    eat();
    U16 len = exprEatSolvedU16();
    if (emit) {
      for (UInt i = 0; i < len; ++i) {
        emitByte(0x00);
      }
    }
    addPC(len);
    expectEOL();
    eat();
    return;
  case TOK_INCLUDE: {
    eat();
    char const *path = eatPath();
    expectEOL();
    eat();
    pushFile(openFile(path, "rb"), path);
    return;
  }
  case TOK_INCBIN: {
    eat();
    char const *path = eatPath();
    expectEOL();
    eat();
    FILE *hnd = openFile(path, "rb");
    if (fseek(hnd, 0, SEEK_END) != 0) {
      fatalLoc(dirLoc, "Failed to seek to end of file: %s\n", strerror(errno));
    }
    long size = ftell(hnd);
    if (size < 0) {
      fatalLoc(dirLoc, "Failed to get file size: %s\n", strerror(errno));
    }
    if (fseek(hnd, 0, SEEK_SET) != 0) {
      fatalLoc(dirLoc, "Failed to seek to start of file: %s\n",
               strerror(errno));
    }
    if (emit) {
      U8 *buf = malloc(size);
      if (fread(buf, 1, size, hnd) != (size_t)size) {
        fatalLoc(dirLoc, "Failed to read file: %s\n", strerror(errno));
      }
      emitBytes(buf, size);
      free(buf);
    }
    addPC((U16)size);
    closeFile(hnd);
    return;
  }
  case TOK_MACRO: {
    Loc loc = lexLoc(ls);
    eat();
    expect(TOK_ID);
    char const *lbl = lexLbl(ls);
    if (!lblIsGlobal(lbl)) {
      fatalLoc(loc, "Macro name must be global\n");
    }
    Macro *macro = findMacro(lbl);
    if (macro) {
      fatalLoc(loc,
               "Macro %s is already defined\n\t%s:%" UINT_FMT ":%" UINT_FMT
               ": First defined here\n",
               lbl, macro->loc.name, macro->loc.line, macro->loc.col);
    }
    eat();
    MacroTok *toks = NULL;
    UInt toksLen = 0;
    UInt toksCap = 0;
    UInt depth = 0;
    defining = TRUE;
    while (TRUE) {
      switch (peek()) {
      case TOK_IF:
      case TOK_MACRO:
      case TOK_REPEAT:
        ++depth;
        goto macroConsume;
      case TOK_END:
        if (depth == 0) {
          eat();
          goto macroDone;
        }
        --depth;
        goto macroConsume;
      case TOK_EOF:
        fatal("Unexpected end of file\n");
      case TOK_ID:
        macroTokCat(&toks, &toksLen, &toksCap,
                    (MacroTok){.kind = MACRO_ID,
                               .loc = lexLoc(ls),
                               .txt = intern(lexTxt(ls))});
        break;
      case TOK_NUM:
        macroTokCat(&toks, &toksLen, &toksCap,
                    (MacroTok){.kind = MACRO_NUM,
                               .loc = lexLoc(ls),
                               .num = lexNum(ls)});
        break;
      case TOK_STR:
        macroTokCat(&toks, &toksLen, &toksCap,
                    (MacroTok){.kind = MACRO_STR,
                               .loc = lexLoc(ls),
                               .txt = intern(lexTxt(ls))});
        break;
      case TOK_ARG:
        macroTokCat(&toks, &toksLen, &toksCap,
                    (MacroTok){.kind = MACRO_ARG,
                               .loc = lexLoc(ls),
                               .num = lexNum(ls)});
        break;
      case TOK_ARGC:
        macroTokCat(&toks, &toksLen, &toksCap,
                    (MacroTok){.kind = MACRO_ARGC, .loc = lexLoc(ls)});
        break;
      case TOK_SHIFT:
        macroTokCat(&toks, &toksLen, &toksCap,
                    (MacroTok){.kind = MACRO_SHIFT, .loc = lexLoc(ls)});
        break;
      default:
      macroConsume:
        macroTokCat(
            &toks, &toksLen, &toksCap,
            (MacroTok){.kind = MACRO_TOK, .loc = lexLoc(ls), .tok = peek()});
        break;
      }
      eat();
    }
  macroDone:
    defining = FALSE;
    addMacro(
        lbl,
        (Macro){.name = lbl, .toks = toks, .toksLen = toksLen, .loc = loc});
    return;
  }
  case TOK_REPEAT: {
    Loc loc = lexLoc(ls);
    eat();
    UInt count = exprEatSolvedU16();
    char const *idx = NULL;
    if (peek() == ',') {
      eat();
      expect(TOK_ID);
      idx = lexLbl(ls);
      if (!lblIsGlobal(idx)) {
        fatalLoc(loc, "Repeat label must be global\n");
      }
      eat();
    }
    RepeatTok *toks = NULL;
    UInt toksLen = 0;
    UInt toksCap = 0;
    UInt depth = 0;
    defining = TRUE;
    while (TRUE) {
      switch (peek()) {
      case TOK_IF:
      case TOK_MACRO:
      case TOK_REPEAT:
        ++depth;
        goto repeatConsume;
      case TOK_END:
        if (depth == 0) {
          eat();
          goto repeatDone;
        }
        --depth;
        goto repeatConsume;
      case TOK_EOF:
        fatal("Unexpected end of file\n");
      case TOK_ID:
        if (idx && (strcmp(lexLbl(ls), idx) == 0)) {
          repeatTokCat(&toks, &toksLen, &toksCap,
                       (RepeatTok){.kind = REPEAT_IDX, .loc = lexLoc(ls)});
        } else {
          repeatTokCat(&toks, &toksLen, &toksCap,
                       (RepeatTok){.kind = REPEAT_ID,
                                   .loc = lexLoc(ls),
                                   .txt = intern(lexTxt(ls))});
        }
        break;
      case TOK_NUM:
        repeatTokCat(&toks, &toksLen, &toksCap,
                     (RepeatTok){.kind = REPEAT_NUM,
                                 .loc = lexLoc(ls),
                                 .num = lexNum(ls)});
        break;
      case TOK_STR:
        repeatTokCat(&toks, &toksLen, &toksCap,
                     (RepeatTok){.kind = REPEAT_STR,
                                 .loc = lexLoc(ls),
                                 .txt = intern(lexTxt(ls))});
        break;
      default:
      repeatConsume:
        repeatTokCat(
            &toks, &toksLen, &toksCap,
            (RepeatTok){.kind = REPEAT_TOK, .loc = lexLoc(ls), .tok = peek()});
        break;
      }
      eat();
    }
  repeatDone:
    defining = FALSE;
    ++ls;
    if (ls == (STACK + STACK_SIZE)) {
      fatal("Too many nested blocks\n");
    }
    lexRepeatInit(ls, loc, toks, toksLen, count);
    return;
  }
  default:
    fatal("Unrecognized token: %s\n", tokName(peek()));
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
      Mnemonic const *mne = findSupportedMnemonic(lexTxt(ls));
      if (mne) {
        eatMnemonic(mne);
        expectEOL();
        eat();
        continue;
      }
      Loc loc = lexLoc(ls);
      char const *lbl = lexLbl(ls);
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
      case '\n':
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
          fatal("Expected `:`, `=`, or end of line\n");
        }
        Mnemonic const *unsupportedMne = findMnemonic(lbl);
        if (unsupportedMne) {
          fatalLoc(loc, "Instruction %s is not available on CPU: \"%s\"\n",
                   unsupportedMne->name, cpuName(cpu));
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
  macrosLen = 0;
  pc = 0;
  cpu = CPU_6502;
  encoding = ENCODING_ASCII;
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
