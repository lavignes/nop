// stdlib
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// POSIX
#include <poll.h>
#include <termios.h>
#include <unistd.h>

// libedit
#include <histedit.h>

static volatile sig_atomic_t sigintFlag = 0;
static Bool debug = FALSE;

static struct termios termiosOrig;
static Bool termRawMode = FALSE;

static void sigintHandler(int sig) {
  (void)sig;
  if (debug) {
    exit(EXIT_SUCCESS);
  }
  sigintFlag = 1;
}

static void help(char const *name) {
  fprintf(stderr, "Usage: %s [options] <romfile>\n\n", name);
  fprintf(stderr, "Options:\n\n");
  fprintf(stderr, "  -h, --help              Show this help message\n");
  fprintf(stderr, "  -d, --debug             Start in debug mode\n");
  fprintf(stderr,
          "  -l, --labellist <file>  Load symbol labellist from file\n");
  fprintf(stderr,
          "  -r, --random            Initialize memory with random data\n");
}

static void emuTick();
static void dbgTick();
static void symLoad(char const *filename);
static void termRawModeOn();
static void termRawModeOff();

int main(int argc, char const *const *argv) {
  FILE *rom;
  char const *labellist = NULL;
  Bool random = FALSE;

  atexit(termRawModeOff);
  signal(SIGINT, sigintHandler);

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
      debug = TRUE;
      continue;
    }
    if ((strcmp(argv[argi], "-l") == 0) ||
        (strcmp(argv[argi], "--labellist") == 0)) {
      ++argi;
      if (argi == argc) {
        fprintf(stderr, "No labellist file specified\n");
        return EXIT_FAILURE;
      }
      labellist = argv[argi];
      continue;
    }
    if ((strcmp(argv[argi], "-r") == 0) ||
        (strcmp(argv[argi], "--random") == 0)) {
      random = TRUE;
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

  if (!rom) {
    fprintf(stderr, "No ROM file specified\n");
    return EXIT_FAILURE;
  }

  if (random) {
    srand((unsigned int)time(NULL));
    for (UInt i = 0; i < sizeof(MEM); ++i) {
      MEM[i] = (U8)rand();
    }
  }

  UInt read = fread(MEM + PC, 1, sizeof(MEM) - PC, rom);
  if (read != (sizeof(MEM) - PC)) {
    int err = ferror(rom);
    if (err) {
      fprintf(stderr, "Error reading ROM file: %s\n", strerror(err));
      return EXIT_FAILURE;
    }
  }
  fclose(rom);

  if (labellist) {
    symLoad(labellist);
  }

  if (!debug) {
    termRawModeOn();
  }

  while (TRUE) {
    emuTick();
  }

  return EXIT_SUCCESS;
}

static U8 ioAddr = 0x00;
static U8 ioData = 0x00;
static U8 ioStatus = 0x00;

static U8 ioAddrRead() { return ioAddr; }

static U8 ioDataRead() {
  switch (ioAddr) {
  case 0x00: {
    if (ioStatus & 0x01) {
      return ioStatus;
    }
    struct pollfd pfd = {STDIN_FILENO, POLLIN, 0};
    if (poll(&pfd, 1, 0) > 0) {
      ioData = (U8)fgetc(stdin);
      ioStatus |= 0x01;
    }
    return ioStatus;
  }
  case 0x01:
    return ioData;
  default:
    return 0x00;
  }
}

static void ioAddrWrite(U8 val) { ioAddr = val; }

static void ioDataWrite(U8 val) {
  switch (ioAddr) {
  case 0x00:
    ioStatus &= ~0x01;
    return;
  case 0x01:
    fputc(val, stdout);
    fflush(stdout);
    return;
  default:
    return;
  }
}

static void cpuTick();

typedef struct Breakpoint Breakpoint;

struct Breakpoint {
  Breakpoint *next;
  U16 num;
  U16 addr;
};

static U16 bpCount = 0;
static Breakpoint *bpHead = NULL;
static Breakpoint nextpoint = {NULL, 0, 0};

static void emuTick() {
  if (sigintFlag) {
    sigintFlag = 0;
    debug = TRUE;
  }
  if (nextpoint.next && (PC == nextpoint.addr)) {
    debug = TRUE;
    nextpoint.next = NULL;
  }
  for (Breakpoint const *bp = bpHead; bp; bp = bp->next) {
    if (PC != bp->addr) {
      continue;
    }
    fprintf(stderr, "Hit breakpoint %u at $%04X\r\n", bp->num, bp->addr);
    debug = TRUE;
    break;
  }
  if (debug) {
    dbgTick();
  }
  // TODO: check for interrupts here
  cpuTick();
}

typedef struct Symbol Symbol;

struct Symbol {
  Symbol *next;
  char const *name;
  Int val;
};

static U16 symCount = 0;
static Symbol *symHead = NULL;

static Symbol *symAdd(char const *name, Int val) {
  Symbol *sym = malloc(sizeof(Symbol));
  sym->name = strdup(name);
  sym->val = val;
  sym->next = symHead;
  symHead = sym;
  ++symCount;
  return sym;
}

static Symbol const *symFind(char const *name, UInt namelen) {
  for (Symbol const *sym = symHead; sym; sym = sym->next) {
    if (strlen(sym->name) != namelen) {
      continue;
    }
    if (strncmp(sym->name, name, namelen) == 0) {
      return sym;
    }
  }
  return NULL;
}

static Symbol const *symValFind(Int val) {
  for (Symbol const *sym = symHead; sym; sym = sym->next) {
    if (val == sym->val) {
      return sym;
    }
  }
  return NULL;
}

static void symLoad(char const *filename) {
  FILE *file = fopen(filename, "r");
  if (!file) {
    fprintf(stderr, "Could not open labellist file: %s\n", filename);
    return;
  }
  char line[256];
  for (UInt lineno = 0; fgets(line, sizeof(line), file); ++lineno) {
    char name[64];
    Int val;
    Int block;
    if (sscanf(line, "%63[^,], 0x%" INT_FMTX ", %" INT_FMT, name, &val,
               &block) != 3) {
      fprintf(stderr, "Invalid labellist line %" UINT_FMT ": %s", lineno, line);
      continue;
    }
    // filter out invalid names
    if (!isalpha(name[0]) && (name[0] != '_')) {
      continue;
    }
    // filter out local labels (block != 0)
    if (block != 0) {
      continue;
    }
    symAdd(name, val);
  }
  fclose(file);
}

static void termRawModeOn() {
  if (termRawMode) {
    return;
  }
  if (tcgetattr(STDIN_FILENO, &termiosOrig) == -1) {
    return;
  }
  struct termios raw = termiosOrig;
  cfmakeraw(&raw);
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
    return;
  }
  termRawMode = TRUE;
}

static void termRawModeOff() {
  if (!termRawMode) {
    return;
  }
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &termiosOrig);
  termRawMode = FALSE;
}

enum {
  TOK_EOE = 0x04,
  TOK_ERR = 0x256,
  TOK_NUM,
  TOK_ID,
  TOK_SRA, // >>
  TOK_SRL, // ~>
  TOK_SLL, // <<
  TOK_AND, // &&
  TOK_OR,  // ||
  TOK_LTE, // <=
  TOK_GTE, // >=
  TOK_EQ,  // ==
  TOK_NEQ, // !=
};

static struct {
  char const *name;
  UInt type;
} const DIGRAPHS[] = {
    {"<<", TOK_SLL}, {"~>", TOK_SRL}, {">>", TOK_SRA},
    {"&&", TOK_AND}, {"||", TOK_OR},  {"<=", TOK_LTE},
    {">=", TOK_GTE}, {"==", TOK_EQ},  {"!=", TOK_NEQ},
};

typedef struct {
  char const *start;
  UInt len;
  UInt type;
  Int val;
} Tok;

static Tok tokStash = {NULL, 0, TOK_EOE, 0};

static void tokEat() {
  if (tokStash.start) {
    tokStash.start += tokStash.len;
    tokStash.len = 0;
  }
}

static Tok tokPeek(char const *str) {
  if (!str) {
    if (tokStash.type == TOK_EOE) {
      return tokStash;
    }
    str = tokStash.start;
  } else {
    tokStash.start = str;
  }
  tokStash.type = TOK_EOE;
  while (isspace((unsigned char)*str)) {
    ++str;
  }
  if (*str == '\0') {
    return tokStash;
  }
  tokStash.start = str;
  if (*str == '\'') {
    ++str;
    switch (*str) {
    case '\0':
      tokStash.type = TOK_ERR;
      tokStash.len = 0;
      return tokStash;
    case '\\':
      ++str;
      switch (*str) {
      case 'a':
        tokStash.val = '\a';
        break;
      case 'b':
        tokStash.val = '\b';
        break;
      case 'f':
        tokStash.val = '\f';
        break;
      case 'n':
        tokStash.val = '\n';
        break;
      case 'r':
        tokStash.val = '\r';
        break;
      case 't':
        tokStash.val = '\t';
        break;
      case 'v':
        tokStash.val = '\v';
        break;
      case '\\':
        tokStash.val = '\\';
        break;
      case '\'':
        tokStash.val = '\'';
        break;
      case '"':
        tokStash.val = '"';
        break;
      case '0':
        tokStash.val = '\0';
        break;
      default:
        tokStash.type = TOK_ERR;
        tokStash.len = 0;
        return tokStash;
      }
    default:
      tokStash.val = (Int)(unsigned char)(*str);
      break;
    }
    ++str;
    if (*str != '\'') {
      tokStash.type = TOK_ERR;
      tokStash.len = 0;
      return tokStash;
    }
    ++str;
    tokStash.type = TOK_NUM;
    tokStash.len = 3;
    return tokStash;
  }
  if (isdigit((unsigned char)*str) || (*str == '$') || (*str == '%')) {
    if (*str == '%') {
      ++str;
      if ((*str != '0') && (*str != '1')) {
        // edge case, this is a modulus
        tokStash.type = '%';
        tokStash.len = 1;
        return tokStash;
      }
      while ((*str == '0') || (*str == '1')) {
        ++str;
      }
      tokStash.val = strtol(tokStash.start + 1, NULL, 2);
    } else if (*str == '$') {
      ++str;
      while (isxdigit((unsigned char)*str)) {
        ++str;
      }
      tokStash.val = strtol(tokStash.start + 1, NULL, 16);
    } else {
      while (isdigit((unsigned char)*str)) {
        ++str;
      }
      tokStash.val = strtol(tokStash.start, NULL, 10);
    }
    tokStash.len = str - tokStash.start;
    tokStash.type = TOK_NUM;
    return tokStash;
  }
  if (isalpha((unsigned char)*str) || (*str == '_')) {
    ++str;
    while (isalnum((unsigned char)*str) || (*str == '_')) {
      ++str;
    }
    tokStash.len = str - tokStash.start;
    tokStash.type = TOK_ID;
    return tokStash;
  }
  for (UInt i = 0; i < (sizeof(DIGRAPHS) / sizeof(DIGRAPHS[0])); ++i) {
    char const *dg = DIGRAPHS[i].name;
    UInt len = strlen(dg);
    if (strncmp(str, dg, len) == 0) {
      tokStash.type = DIGRAPHS[i].type;
      tokStash.len = len;
      return tokStash;
    }
  }
  tokStash.type = (unsigned char)*str;
  tokStash.len = 1;
  return tokStash;
}

enum {
  EXPR_NUM,
  EXPR_ID,
  EXPR_OP,
};

typedef struct {
  UInt kind;
  Tok tok;
  Bool unary;
} Expr;

static U8 tokPrec(Tok tok, Bool unary) {
  if (unary) {
    return 0;
  }
  switch (tok.type) {
  case '/':
  case '%':
  case '*':
    return 1;
  case '+':
  case '-':
    return 2;
  case TOK_SRA:
  case TOK_SRL:
  case TOK_SLL:
    return 3;
  case '<':
  case '>':
  case TOK_LTE:
  case TOK_GTE:
    return 4;
  case TOK_EQ:
  case TOK_NEQ:
    return 5;
  case '&':
    return 6;
  case '^':
    return 7;
  case '|':
    return 8;
  case TOK_AND:
    return 9;
  case TOK_OR:
    return 10;
  default:
    abort();
  }
}

static Expr opStack[64];
static Expr exprStack[64];
static Int intStack[64];
static UInt opCount;
static UInt exprCount;
static UInt intCount;

static Int solve() {
  for (UInt i = 0; i < exprCount; ++i) {
    Expr const *expr = exprStack + i;
    switch (expr->kind) {
    case EXPR_NUM:
      intStack[intCount++] = expr->tok.val;
      break;
    case EXPR_ID: {
      if (expr->tok.len == 1) {
        switch (tolower((unsigned char)expr->tok.start[0])) {
        case 'a':
          intStack[intCount++] = A;
          break;
        case 'x':
          intStack[intCount++] = X;
          break;
        case 'y':
          intStack[intCount++] = Y;
          break;
        default:
          break;
        }
      }
      if (expr->tok.len == 2) {
        if ((tolower((unsigned char)expr->tok.start[0]) == 'p') &&
            (tolower((unsigned char)expr->tok.start[1]) == 'c')) {
          intStack[intCount++] = PC;
          break;
        }
        if ((tolower((unsigned char)expr->tok.start[0]) == 's') &&
            (tolower((unsigned char)expr->tok.start[1]) == 'p')) {
          intStack[intCount++] = 0x0100 | ((U16)SP);
          break;
        }
      }
      Symbol const *sym = symFind(expr->tok.start, expr->tok.len);
      if (!sym) {
        return INT_MAX;
      }
      intStack[intCount++] = sym->val;
      break;
    }
    case EXPR_OP: {
      Int rhs = intStack[--intCount];
      if (expr->unary) {
        switch (expr->tok.type) {
        case '+':
          intStack[intCount++] = rhs;
          break;
        case '-':
          intStack[intCount++] = -rhs;
          break;
        case '!':
          intStack[intCount++] = !rhs;
          break;
        case '~':
          intStack[intCount++] = ~rhs;
          break;
        case '<':
          intStack[intCount++] = rhs & 0xFF;
          break;
        case '>':
          intStack[intCount++] = (((UInt)rhs) >> 8) & 0xFF;
          break;
        case '*':
          if ((rhs < 0) || (rhs > U16_MAX)) {
            return INT_MAX;
          }
          intStack[intCount++] = MEM[(U16)rhs];
          break;
        default:
          abort();
        }
        continue;
      }
      Int lhs = intStack[--intCount];
      switch (expr->tok.type) {
      case '+':
        intStack[intCount++] = lhs + rhs;
        break;
      case '-':
        intStack[intCount++] = lhs - rhs;
        break;
      case '*':
        intStack[intCount++] = lhs * rhs;
        break;
      case '/':
        if (rhs == 0) {
          return INT_MAX;
        }
        intStack[intCount++] = lhs / rhs;
        break;
      case '%':
        if (rhs == 0) {
          return INT_MAX;
        }
        intStack[intCount++] = lhs % rhs;
        break;
      case '<':
        intStack[intCount++] = lhs < rhs;
        break;
      case '>':
        intStack[intCount++] = lhs > rhs;
        break;
      case '&':
        intStack[intCount++] = lhs & rhs;
        break;
      case '|':
        intStack[intCount++] = lhs | rhs;
        break;
      case '^':
        intStack[intCount++] = lhs ^ rhs;
        break;
      case TOK_SRA:
        intStack[intCount++] = lhs >> rhs;
        break;
      case TOK_SRL:
        intStack[intCount++] = ((UInt)lhs) >> rhs;
        break;
      case TOK_SLL:
        intStack[intCount++] = lhs << rhs;
        break;
      case TOK_AND:
        intStack[intCount++] = lhs && rhs;
        break;
      case TOK_OR:
        intStack[intCount++] = lhs || rhs;
        break;
      case TOK_LTE:
        intStack[intCount++] = lhs <= rhs;
        break;
      case TOK_GTE:
        intStack[intCount++] = lhs >= rhs;
        break;
      case TOK_EQ:
        intStack[intCount++] = lhs == rhs;
        break;
      case TOK_NEQ:
        intStack[intCount++] = lhs != rhs;
        break;
      default:
        abort();
      }
      break;
    }
    default:
      abort();
    }
  }
  if (intCount != 1) {
    return INT_MAX;
  }
  return intStack[0];
}

static void pushOp(Tok tok, Bool unary) {
  if (tok.type == '(') {
    opStack[opCount++] = (Expr){EXPR_OP, tok, TRUE};
    return;
  }
  while (opCount > 0) {
    Expr top = opStack[--opCount];
    if ((top.tok.type == '(') ||
        (tokPrec(top.tok, top.unary) >= tokPrec(tok, unary))) {
      opStack[opCount++] = top;
      break;
    }
    exprStack[exprCount++] = top;
  }
  opStack[opCount++] = (Expr){EXPR_OP, tok, unary};
}

static Int parseExpr() {
  opCount = 0;
  exprCount = 0;
  intCount = 0;
  Bool expectOp = FALSE;
  UInt parenDepth = 0;
  while (TRUE) {
    Tok tok = tokPeek(NULL);
    switch (tok.type) {
    case '+':
    case '-':
    case '<':
    case '>':
    case '*':
      // sometimes unary
      pushOp(tok, !expectOp);
      tokEat();
      expectOp = FALSE;
      continue;
    case '!':
    case '~':
      // always unary
      pushOp(tok, TRUE);
      tokEat();
      expectOp = FALSE;
      continue;
    case '&':
    case '|':
    case '^':
    case '/':
    case '%':
    case TOK_SRA:
    case TOK_SRL:
    case TOK_SLL:
    case TOK_AND:
    case TOK_OR:
    case TOK_LTE:
    case TOK_GTE:
    case TOK_EQ:
    case TOK_NEQ:
      if (!expectOp) {
        return INT_MAX;
      }
      pushOp(tok, FALSE);
      tokEat();
      expectOp = FALSE;
      continue;
    case TOK_NUM:
      if (expectOp) {
        return INT_MAX;
      }
      exprStack[exprCount++] = (Expr){EXPR_NUM, tok, FALSE};
      tokEat();
      expectOp = TRUE;
      continue;
    case TOK_ID:
      if (expectOp) {
        return INT_MAX;
      }
      exprStack[exprCount++] = (Expr){EXPR_ID, tok, FALSE};
      tokEat();
      expectOp = TRUE;
      continue;
    case '(':
      if (expectOp) {
        return INT_MAX;
      }
      pushOp(tok, TRUE);
      tokEat();
      ++parenDepth;
      expectOp = FALSE;
      continue;
    case ')':
      if (!expectOp) {
        return INT_MAX;
      }
      --parenDepth;
      while (TRUE) {
        if (opCount == 0) {
          return INT_MAX;
        }
        Expr top = opStack[--opCount];
        if (top.tok.type == '(') {
          break;
        }
        exprStack[exprCount++] = top;
      }
      tokEat();
      continue;
    default:
      if (!expectOp) {
        return INT_MAX;
      }
      if (parenDepth > 0) {
        return INT_MAX;
      }
      while (opCount > 0) {
        exprStack[exprCount++] = opStack[--opCount];
      }
      return solve();
    }
  }
}

static U16 disAsm(U16 addr);
static void regs();

typedef enum { DBG_DEBUG, DBG_BREAK, DBG_CONTINUE, DBG_CLEAR } DbgResult;

static DbgResult dbgQuit() { exit(EXIT_SUCCESS); }

static DbgResult dbgCont() { return DBG_CONTINUE; }

static DbgResult dbgStep() { return DBG_BREAK; }

static DbgResult dbgClear() { return DBG_CLEAR; }

static DbgResult dbgNext() {
  U8 op = MEM[PC];
  if (op == 0x20) { // JSR
    nextpoint.addr = PC + 3;
    nextpoint.next = bpHead; // to mark it active
    return DBG_CONTINUE;
  }
  return DBG_BREAK;
}

static DbgResult dbgRegs() {
  regs();
  return DBG_DEBUG;
}

static DbgResult dbgBreak() {
  Tok tok = tokPeek(NULL);
  if (tok.type == TOK_EOE) {
    fprintf(stderr, "No address provided for breakpoint\n");
    return DBG_DEBUG;
  }
  Int addr = parseExpr();
  if ((addr == INT_MAX) || (addr > U16_MAX)) {
    Tok end = tokPeek(NULL);
    UInt len = end.start + end.len - tok.start;
    fprintf(stderr, "Invalid address for breakpoint: \"%.*s\"\n", (int)len,
            tok.start);
    return DBG_DEBUG;
  }
  Breakpoint *bp = malloc(sizeof(Breakpoint));
  bp->num = ++bpCount;
  bp->addr = (U16)addr;
  bp->next = bpHead;
  bpHead = bp;
  fprintf(stderr, "Breakpoint %u set at $%04X\n", bp->num, bp->addr);
  return DBG_DEBUG;
}

static DbgResult dbgDel() {
  Tok tok = tokPeek(NULL);
  if (tok.type == TOK_EOE) {
    fprintf(stderr, "No breakpoint number provided for deletion\n");
    return DBG_DEBUG;
  }
  Int num = parseExpr();
  if ((num == INT_MAX) || (num > U16_MAX)) {
    Tok end = tokPeek(NULL);
    UInt len = end.start + end.len - tok.start;
    fprintf(stderr, "Invalid breakpoint number: \"%.*s\"\n", (int)len,
            tok.start);
    return DBG_DEBUG;
  }
  Breakpoint **prev = &bpHead;
  Breakpoint *bp = bpHead;
  while (bp) {
    if (bp->num == (U16)num) {
      *prev = bp->next;
      free(bp);
      fprintf(stderr, "Breakpoint %u deleted\n", (U16)num);
      return DBG_DEBUG;
    }
    prev = &bp->next;
    bp = bp->next;
  }
  fprintf(stderr, "No breakpoint with number %u\n", (U16)num);
  return DBG_DEBUG;
}

static DbgResult dbgExa() {
  Int start = PC;
  Int len = 16;
  Tok tok = tokPeek(NULL);
  if (tok.type != TOK_EOE) {
    start = parseExpr();
    if ((start == INT_MAX) || (start > U16_MAX)) {
      Tok end = tokPeek(NULL);
      UInt len = end.start + end.len - tok.start;
      fprintf(stderr, "Invalid address for examine: \"%.*s\"\n", (int)len,
              tok.start);
      return DBG_DEBUG;
    }
  }
  tok = tokPeek(NULL);
  if (tok.type != TOK_EOE) {
    if (tok.type != ',') {
      fprintf(stderr, "Expected ',' after address\n");
      return DBG_DEBUG;
    }
    tok = tokPeek(NULL);
    if (tok.type == TOK_EOE) {
      fprintf(stderr, "No length provided for disasm\n");
      return DBG_DEBUG;
    }
    tokEat();
    len = parseExpr();
    if ((len == INT_MAX) || (len <= 0)) {
      Tok end = tokPeek(NULL);
      UInt len = end.start + end.len - tok.start;
      fprintf(stderr, "Invalid length for examine: \"%.*s\"\n", (int)len,
              tok.start);
      return DBG_DEBUG;
    }
  }
  Int end = ((start + len - 1) > U16_MAX) ? U16_MAX : (start + len - 1);
  while (start <= end) {
    fprintf(stderr, "%04X ", (U16)start);
    for (UInt i = 0; i < 16; ++i) {
      if (i == 8) {
        fprintf(stderr, " ");
      }
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

static DbgResult dbgDis() {
  Int start = PC;
  Int len = 1;
  Tok tok = tokPeek(NULL);
  if (tok.type != TOK_EOE) {
    start = parseExpr();
    if ((start == INT_MAX) || (start > U16_MAX)) {
      Tok end = tokPeek(NULL);
      UInt len = end.start + end.len - tok.start;
      fprintf(stderr, "Invalid address for disasm: \"%.*s\"\n", (int)len,
              tok.start);
      return DBG_DEBUG;
    }
  }
  tok = tokPeek(NULL);
  if (tok.type != TOK_EOE) {
    if (tok.type != ',') {
      fprintf(stderr, "Expected ',' after address\n");
      return DBG_DEBUG;
    }
    tokEat();
    tok = tokPeek(NULL);
    if (tok.type == TOK_EOE) {
      fprintf(stderr, "No length provided for disasm\n");
      return DBG_DEBUG;
    }
    len = parseExpr();
    if ((len == INT_MAX) || (len <= 0)) {
      Tok end = tokPeek(NULL);
      UInt len = end.start + end.len - tok.start;
      fprintf(stderr, "Invalid length for disasm: \"%.*s\"\n", (int)len,
              tok.start);
      return DBG_DEBUG;
    }
  }
  U16 addr = (U16)start;
  Int count = 0;
  while ((count < len) && (addr <= U16_MAX)) {
    addr = disAsm(addr);
    ++count;
  }
  return DBG_DEBUG;
}

static DbgResult dbgHelp();

typedef struct {
  char const *breif;
  char const *body;
} DbgHelp;

typedef struct {
  char const **names;
  DbgResult (*fn)();
  DbgHelp help;
} DbgCmd;

static DbgHelp const HELP_QUIT = {
    "Quit the debugger",
    "Usage: quit\n\nExit the debugger and terminate the program."};
static DbgHelp const HELP_CONT = {
    "Continue execution", "Usage: continue\n\nResume execution of the program "
                          "until the next breakpoint or termination."};
static DbgHelp const HELP_STEP = {
    "Step into the next instruction",
    "Usage: step\n\nExecute the next instruction and return to the debugger."};
static DbgHelp const HELP_NEXT = {
    "Step over the next instruction",
    "Usage: next\n\nExecute the next instruction, stepping over function "
    "calls, and return to the debugger."};
static DbgHelp const HELP_BREAK = {
    "Set a breakpoint",
    "Usage: break <address>\n\nSet a breakpoint at the specified address. The "
    "program will pause execution when it reaches this address."};
static DbgHelp const HELP_DELETE = {
    "Delete a breakpoint", "Usage: delete <breakpoint_number>\n\nRemove the "
                           "specified breakpoint from the debugger."};
static DbgHelp const HELP_REGS = {
    "Display CPU registers",
    "Usage: registers\n\nShow the current values of the CPU registers."};
static DbgHelp const HELP_EXA = {
    "Examine memory", "Usage: examine <address>[, <length>]\n\nDisplay the "
                      "contents of memory starting from the specified address. "
                      "Optionally, specify the number of bytes to display."};
static DbgHelp const HELP_DIS = {
    "Disassemble code",
    "Usage: disasm <address>[, <length>]\n\nDisassemble the code starting from "
    "the specified address. Optionally, specify the number of instructions to "
    "disassemble."};
static DbgHelp const HELP_CLEAR = {
    "Clear screen", "Usage: clear\n\nClear the debugger's output screen."};
static DbgHelp const HELP_HELP = {
    "Display help information",
    "Usage: help [command]\n\nShow a list of available commands or detailed "
    "help for a specific command."};

static DbgCmd const DBG_TBL[] = {
    {(char const *[]){"quit", NULL}, dbgQuit, HELP_QUIT},
    {(char const *[]){"continue", NULL}, dbgCont, HELP_CONT},
    {(char const *[]){"step", NULL}, dbgStep, HELP_STEP},
    {(char const *[]){"next", NULL}, dbgNext, HELP_NEXT},
    {(char const *[]){"break", NULL}, dbgBreak, HELP_BREAK},
    {(char const *[]){"delete", NULL}, dbgDel, HELP_DELETE},
    {(char const *[]){"registers", NULL}, dbgRegs, HELP_REGS},
    {(char const *[]){"examine", "x", NULL}, dbgExa, HELP_EXA},
    {(char const *[]){"disasm", "das", "list", NULL}, dbgDis, HELP_DIS},
    {(char const *[]){"clear", NULL}, dbgClear, HELP_CLEAR},
    {(char const *[]){"help", "?", NULL}, dbgHelp, HELP_HELP},
    {NULL, NULL, NULL}};

static char *dbgPrompt(EditLine *el) {
  (void)el;
  return "> ";
}

static void dbgMatches(char const *prefix, UInt len) {
  Bool first = TRUE;
  for (DbgCmd const *cmd = DBG_TBL; cmd->names; ++cmd) {
    for (char const **name = cmd->names; *name; ++name) {
      if (strncmp(prefix, *name, len) == 0) {
        fprintf(stderr, "%s%s", first ? "" : ", ", *name);
        first = FALSE;
      }
    }
  }
  for (Symbol const *sym = symHead; sym; sym = sym->next) {
    if (strncmp(prefix, sym->name, len) == 0) {
      fprintf(stderr, "%s%s", first ? "" : ", ", sym->name);
      first = FALSE;
    }
  }
}

static unsigned char dbgCompl(EditLine *el, int ch) {
  (void)ch;
  LineInfo const *li = el_line(el);
  char const *str = li->cursor;
  while ((str > li->buffer) && !isspace(*(str - 1))) {
    --str;
  }
  while (!isalpha(*str) && (*str != '_') && (str < li->cursor)) {
    ++str;
  }
  ptrdiff_t len = li->cursor - str;
  if (len <= 0) {
    return CC_REFRESH;
  }
  UInt matchcnt = 0;
  char const *match = NULL;
  for (DbgCmd const *cmd = DBG_TBL; cmd->names; ++cmd) {
    for (char const **name = cmd->names; *name; ++name) {
      if (strncmp(str, *name, len) == 0) {
        match = *name;
        ++matchcnt;
      }
    }
  }
  for (Symbol const *sym = symHead; sym; sym = sym->next) {
    if (strncmp(str, sym->name, len) == 0) {
      match = sym->name;
      ++matchcnt;
    }
  }
  if (matchcnt == 1) {
    el_deletestr(el, len);
    el_insertstr(el, match);
    return CC_REFRESH;
  }
  if (matchcnt > 1) {
    fprintf(stderr, "\n");
    dbgMatches(str, len);
    fprintf(stderr, " ?\n");
    return CC_REDISPLAY;
  }
  return CC_REFRESH;
}

static DbgResult dbgHelp() {
  Tok tok = tokPeek(NULL);
  if (tok.type == TOK_EOE) {
    fprintf(stderr, "Available commands:\n");
    int maxWidth = 0;
    for (DbgCmd const *cmd = DBG_TBL; cmd->names; ++cmd) {
      int width = 2;
      for (char const **name = cmd->names; *name; ++name) {
        width += (name == cmd->names ? 0 : 2) + (int)strlen(*name);
      }
      if (width > maxWidth) {
        maxWidth = width;
      }
    }
    for (DbgCmd const *cmd = DBG_TBL; cmd->names; ++cmd) {
      int width = 2;
      fprintf(stderr, "  ");
      for (char const **name = cmd->names; *name; ++name) {
        fprintf(stderr, "%s%s", name == cmd->names ? "" : ", ", *name);
        width += (name == cmd->names ? 0 : 2) + (int)strlen(*name);
      }
      fprintf(stderr, "%*s  %s\n", maxWidth - width, "", cmd->help.breif);
    }
    return DBG_DEBUG;
  }
  for (DbgCmd const *cmd = DBG_TBL; cmd->names; ++cmd) {
    for (char const **name = cmd->names; *name; ++name) {
      if (strncmp(tok.start, *name, tok.len) == 0) {
        fprintf(stderr, "%s  %s\n", *name, cmd->help.breif);
        fprintf(stderr, "%s\n", cmd->help.body);
        return DBG_DEBUG;
      }
    }
  }
  fprintf(stderr, "Unknown command: %.*s\n", (int)tok.len, tok.start);
  return DBG_DEBUG;
}

static void dbgTick() {
  static EditLine *el = NULL;
  static History *hist = NULL;
  static HistEvent ev;
  static char *prevline = NULL;
  static char *workline = NULL;
  termRawModeOff();
  if (!el) {
    el = el_init("vm", stdin, stderr, stderr);
    el_set(el, EL_PROMPT, &dbgPrompt);
    el_set(el, EL_EDITOR, "emacs");
    el_set(el, EL_ADDFN, "ed-complete", "Complete command", dbgCompl);
    el_set(el, EL_BIND, "^I", "ed-complete", NULL);
    hist = history_init();
    history(hist, &ev, H_SETSIZE, 100);
    el_set(el, EL_HIST, history, hist);
  }
  regs();
  disAsm(PC);
  while (TRUE) {
    free(workline);
    int count;
    char const *line = el_gets(el, &count);
    if (!line || (count <= 0)) {
      exit(EXIT_FAILURE);
    }
    workline = strdup(line);
    Tok tok = tokPeek(workline);
    if (tok.type == TOK_EOE) {
      if (prevline) {
        free(workline);
        workline = strdup(prevline);
        tok = tokPeek(workline);
      }
      if (tok.type == TOK_EOE) {
        continue;
      }
    } else {
      // save history if not the same as prev command
      if (!prevline || (strcmp(prevline, line) != 0)) {
        history(hist, &ev, H_ENTER, line);
      }
      free(prevline);
      prevline = strdup(line);
    }
    DbgCmd const *match = NULL;
    UInt matchCount = 0;
    for (DbgCmd const *cmd = DBG_TBL; cmd->names; ++cmd) {
      for (char const **name = cmd->names; *name; ++name) {
        if (strncmp(tok.start, *name, tok.len) != 0) {
          continue;
        }
        match = cmd;
        ++matchCount;
      }
    }
    if (matchCount == 0) {
      fprintf(stderr, "Unknown command: %.*s\n", (int)tok.len, tok.start);
      continue;
    }
    if (matchCount > 1) {
      fprintf(stderr, "Ambiguous command: %.*s (", (int)tok.len, tok.start);
      dbgMatches(tok.start, tok.len);
      fprintf(stderr, ")\n");
      continue;
    }
    tokEat();
    DbgResult res = match->fn();
    if (res == DBG_BREAK) {
      break;
    }
    if (res == DBG_CONTINUE) {
      debug = FALSE;
      termRawModeOn();
      return;
    }
    if (res == DBG_CLEAR) {
      el_push(el, "\x0C");
    }
  }
}

#define RESET "\x1b[0m"
#define RED(str) "\x1b[31m" str RESET
#define GREEN(str) "\x1b[32m" str RESET
#define YELLOW(str) "\x1b[33m" str RESET
#define BLUE(str) "\x1b[34m" str RESET
#define MAGENTA(str) "\x1b[35m" str RESET
#define CYAN(str) "\x1b[36m" str RESET
#define WHITE(str) "\x1b[37m" str RESET

static void regs() {
  fprintf(stderr, "PC:%04X SP:01%02X A:%02X X:%02X Y:%02X P:%02X |", PC, SP, A,
          X, Y, P);
  fprintf(stderr, "%c%c%c%c%c%c%c%c|\n", (P & FLAG_NEGATIVE) ? 'N' : '.',
          (P & FLAG_OVERFLOW) ? 'V' : '.', (P & FLAG_UNUSED) ? '1' : '.',
          (P & FLAG_BREAK) ? 'B' : '.', (P & FLAG_DECIMAL) ? 'D' : '.',
          (P & FLAG_INTERRUPT) ? 'I' : '.', (P & FLAG_ZERO) ? 'Z' : '.',
          (P & FLAG_CARRY) ? 'C' : '.');
}

static void disSym(U16 addr) {
  Symbol const *sym = symValFind((UInt)addr);
  if (sym) {
    fprintf(stderr, CYAN("; %s"), sym->name);
    return;
  }
  sym = symValFind((UInt)(addr - 1));
  if (sym) {
    fprintf(stderr, CYAN("; %s+1"), sym->name);
  }
}

static U16 disImpl(U8 op, U16 addr, char const *mne) {
  fprintf(stderr, " %02X      ", op);
  fprintf(stderr, "  " BLUE("%s") "            ", mne);
  return addr;
}

static U16 disImm(U8 op, U16 addr, char const *mne) {
  U8 val = MEM[addr++];
  fprintf(stderr, " %02X %02X   ", op, val);
  fprintf(stderr, "  " BLUE("%s") " " MAGENTA("#$%02X") "       ", mne, val);
  return addr;
}

static U16 disZp(U8 op, U16 addr, char const *mne) {
  U8 zp = MEM[addr++];
  fprintf(stderr, " %02X %02X   ", op, zp);
  fprintf(stderr, "  " BLUE("%s") " $%02X        ", mne, zp);
  disSym((U16)zp);
  return addr;
}

static U16 disZpX(U8 op, U16 addr, char const *mne) {
  U8 zp = MEM[addr++];
  fprintf(stderr, " %02X %02X   ", op, zp);
  fprintf(stderr, "  " BLUE("%s") " $%02X,X      ", mne, zp);
  disSym((U16)zp);
  return addr;
}

static U16 disZpY(U8 op, U16 addr, char const *mne) {
  U8 zp = MEM[addr++];
  fprintf(stderr, " %02X %02X   ", op, zp);
  fprintf(stderr, "  " BLUE("%s") " $%02X,Y      ", mne, zp);
  disSym((U16)zp);
  return addr;
}

static U16 disAb(U8 op, U16 addr, char const *mne) {
  U8 lo = MEM[addr++];
  U8 hi = MEM[addr++];
  U16 ab = (((U16)hi) << 8) | lo;
  fprintf(stderr, " %02X %02X %02X", op, lo, hi);
  fprintf(stderr, "  " BLUE("%s") " $%04X      ", mne, ab);
  disSym(ab);
  return addr;
}

static U16 disAbX(U8 op, U16 addr, char const *mne) {
  U8 lo = MEM[addr++];
  U8 hi = MEM[addr++];
  U16 ab = (((U16)hi) << 8) | lo;
  fprintf(stderr, " %02X %02X %02X", op, lo, hi);
  fprintf(stderr, "  " BLUE("%s") " $%04X,X    ", mne, ab);
  disSym(ab);
  return addr;
}

static U16 disAbY(U8 op, U16 addr, char const *mne) {
  U8 lo = MEM[addr++];
  U8 hi = MEM[addr++];
  U16 ab = (((U16)hi) << 8) | lo;
  fprintf(stderr, " %02X %02X %02X", op, lo, hi);
  fprintf(stderr, "  " BLUE("%s") " $%04X,Y    ", mne, ab);
  disSym(ab);
  return addr;
}

static U16 disId(U8 op, U16 addr, char const *mne) {
  U8 lo = MEM[addr++];
  U8 hi = MEM[addr++];
  U16 ptr = (((U16)hi) << 8) | lo;
  fprintf(stderr, " %02X %02X %02X", op, lo, hi);
  fprintf(stderr, "  " BLUE("%s") " ($%04X)    ", mne, ptr);
  disSym(ptr);
  return addr;
}

static U16 disIdX(U8 op, U16 addr, char const *mne) {
  U8 zp = MEM[addr++];
  fprintf(stderr, " %02X %02X   ", op, zp);
  fprintf(stderr, "  " BLUE("%s") " ($%02X,X)    ", mne, zp);
  disSym((U16)zp);
  return addr;
}

static U16 disIdY(U8 op, U16 addr, char const *mne) {
  U8 zp = MEM[addr++];
  fprintf(stderr, " %02X %02X   ", op, zp);
  fprintf(stderr, "  " BLUE("%s") " ($%02X),Y    ", mne, zp);
  disSym((U16)zp);
  return addr;
}

static U16 disRel(U8 op, U16 addr, char const *mne) {
  I8 offset = (I8)MEM[addr++];
  U16 target = addr + offset;
  fprintf(stderr, " %02X %02X   ", op, (U8)offset);
  fprintf(stderr, "  " BLUE("%s") " $%04X      ", mne, target);
  disSym(target);
  return addr;
}

typedef U16 (*DisFn)(U8, U16, char const *);

typedef struct {
  char const *mne;
  DisFn fn;
} DisEntry;

static DisEntry const DIS_TBL[256] = {
    [0x00] = {"BRK", disImm},  [0x01] = {"ORA", disIdX},
    [0x05] = {"ORA", disZp},   [0x06] = {"ASL", disZp},
    [0x08] = {"PHP", disImpl}, [0x09] = {"ORA", disImm},
    [0x0A] = {"ASL", disImpl}, [0x0D] = {"ORA", disAb},
    [0x0E] = {"ASL", disAb},   [0x10] = {"BPL", disRel},
    [0x11] = {"ORA", disIdY},  [0x15] = {"ORA", disZpX},
    [0x16] = {"ASL", disZpX},  [0x18] = {"CLC", disImpl},
    [0x19] = {"ORA", disAbY},  [0x1D] = {"ORA", disAbX},
    [0x1E] = {"ASL", disAbX},  [0x20] = {"JSR", disAb},
    [0x21] = {"AND", disIdX},  [0x24] = {"BIT", disZp},
    [0x25] = {"AND", disZp},   [0x26] = {"ROL", disZp},
    [0x28] = {"PLP", disImpl}, [0x29] = {"AND", disImm},
    [0x2A] = {"ROL", disImpl}, [0x2C] = {"BIT", disAb},
    [0x2D] = {"AND", disAb},   [0x2E] = {"ROL", disAb},
    [0x30] = {"BMI", disRel},  [0x31] = {"AND", disIdY},
    [0x35] = {"AND", disZpX},  [0x36] = {"ROL", disZpX},
    [0x38] = {"SEC", disImpl}, [0x39] = {"AND", disAbY},
    [0x3D] = {"AND", disAbX},  [0x3E] = {"ROL", disAbX},
    [0x40] = {"RTI", disImpl}, [0x41] = {"EOR", disIdX},
    [0x45] = {"EOR", disZp},   [0x46] = {"LSR", disZp},
    [0x48] = {"PHA", disImpl}, [0x49] = {"EOR", disImm},
    [0x4A] = {"LSR", disImpl}, [0x4C] = {"JMP", disAb},
    [0x4D] = {"EOR", disAb},   [0x4E] = {"LSR", disAb},
    [0x50] = {"BVC", disRel},  [0x51] = {"EOR", disIdY},
    [0x55] = {"EOR", disZpX},  [0x56] = {"LSR", disZpX},
    [0x58] = {"CLI", disImpl}, [0x59] = {"EOR", disAbY},
    [0x5D] = {"EOR", disAbX},  [0x5E] = {"LSR", disAbX},
    [0x60] = {"RTS", disImpl}, [0x61] = {"ADC", disIdX},
    [0x65] = {"ADC", disZp},   [0x66] = {"ROR", disZp},
    [0x68] = {"PLA", disImpl}, [0x69] = {"ADC", disImm},
    [0x6A] = {"ROR", disImpl}, [0x6C] = {"JMP", disId},
    [0x6D] = {"ADC", disAb},   [0x6E] = {"ROR", disAb},
    [0x70] = {"BVS", disRel},  [0x71] = {"ADC", disIdY},
    [0x75] = {"ADC", disZpX},  [0x76] = {"ROR", disZpX},
    [0x78] = {"SEI", disImpl}, [0x79] = {"ADC", disAbY},
    [0x7D] = {"ADC", disAbX},  [0x7E] = {"ROR", disAbX},
    [0x81] = {"STA", disIdX},  [0x84] = {"STY", disZp},
    [0x85] = {"STA", disZp},   [0x86] = {"STX", disZp},
    [0x88] = {"DEY", disImpl}, [0x8A] = {"TXA", disImpl},
    [0x8C] = {"STY", disAb},   [0x8D] = {"STA", disAb},
    [0x8E] = {"STX", disAb},   [0x90] = {"BCC", disRel},
    [0x91] = {"STA", disIdY},  [0x94] = {"STY", disZpX},
    [0x95] = {"STA", disZpX},  [0x96] = {"STX", disZpY},
    [0x98] = {"TYA", disImpl}, [0x99] = {"STA", disAbY},
    [0x9A] = {"TXS", disImpl}, [0x9D] = {"STA", disAbX},
    [0xA0] = {"LDY", disImm},  [0xA1] = {"LDA", disIdX},
    [0xA2] = {"LDX", disImm},  [0xA4] = {"LDY", disZp},
    [0xA5] = {"LDA", disZp},   [0xA6] = {"LDX", disZp},
    [0xA8] = {"TAY", disImpl}, [0xA9] = {"LDA", disImm},
    [0xAA] = {"TAX", disImpl}, [0xAC] = {"LDY", disAb},
    [0xAD] = {"LDA", disAb},   [0xAE] = {"LDX", disAb},
    [0xB0] = {"BCS", disRel},  [0xB1] = {"LDA", disIdY},
    [0xB4] = {"LDY", disZpX},  [0xB5] = {"LDA", disZpX},
    [0xB6] = {"LDX", disZpY},  [0xB8] = {"CLV", disImpl},
    [0xB9] = {"LDA", disAbY},  [0xBA] = {"TSX", disImpl},
    [0xBC] = {"LDY", disAbX},  [0xBD] = {"LDA", disAbX},
    [0xBE] = {"LDX", disAbY},  [0xC0] = {"CPY", disImm},
    [0xC1] = {"CMP", disIdX},  [0xC4] = {"CPY", disZp},
    [0xC5] = {"CMP", disZp},   [0xC6] = {"DEC", disZp},
    [0xC8] = {"INY", disImpl}, [0xC9] = {"CMP", disImm},
    [0xCA] = {"DEX", disImpl}, [0xCC] = {"CPY", disAb},
    [0xCD] = {"CMP", disAb},   [0xCE] = {"DEC", disAb},
    [0xD0] = {"BNE", disRel},  [0xD1] = {"CMP", disIdY},
    [0xD5] = {"CMP", disZpX},  [0xD6] = {"DEC", disZpX},
    [0xD8] = {"CLD", disImpl}, [0xD9] = {"CMP", disAbY},
    [0xDD] = {"CMP", disAbX},  [0xDE] = {"DEC", disAbX},
    [0xE0] = {"CPX", disImm},  [0xE1] = {"SBC", disIdX},
    [0xE4] = {"CPX", disZp},   [0xE5] = {"SBC", disZp},
    [0xE6] = {"INC", disZp},   [0xE8] = {"INX", disImpl},
    [0xE9] = {"SBC", disImm},  [0xEA] = {"NOP", disImpl},
    [0xEC] = {"CPX", disAb},   [0xED] = {"SBC", disAb},
    [0xEE] = {"INC", disAb},   [0xF0] = {"BEQ", disRel},
    [0xF1] = {"SBC", disIdY},  [0xF5] = {"SBC", disZpX},
    [0xF6] = {"INC", disZpX},  [0xF8] = {"SED", disImpl},
    [0xF9] = {"SBC", disAbY},  [0xFD] = {"SBC", disAbX},
    [0xFE] = {"INC", disAbX},
};

static U16 disAsm(U16 addr) {
  Symbol const *sym = symValFind((UInt)addr);
  if (sym) {
    fprintf(stderr, YELLOW("%s:") "\n", sym->name);
  }
  fprintf(stderr, "%04X ", addr);
  U8 op = MEM[addr++];
  DisEntry const *entry = &DIS_TBL[op];
  if (entry->fn) {
    addr = entry->fn(op, addr, entry->mne);
  } else {
    addr = disImpl(op, addr, "ILL");
  }
  fprintf(stderr, "\n");
  return addr;
}
