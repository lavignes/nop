// stdlib
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
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

#define UINT_FMT PRIuPTR
#define INT_FMT PRIiPTR
#define INT_FMTX PRIXPTR

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

static struct termios orig_termios;
static bool raw_mode = false;

static U8 MEM[65536];

static void help(char const *name) {
  fprintf(stderr, "Usage: %s [options] <romfile>\n\n", name);
  fprintf(stderr, "Options:\n\n");
  fprintf(stderr, "  -h, --help       Show this help message\n");
  fprintf(stderr, "  -d, --debug      Start in debug mode\n");
  fprintf(stderr, "  -l, --labellist  Load symbol labellist from file\n");
  fprintf(stderr, "  -r, --random     Initialize memory with random data\n");
}

static void tick();
static void debugger();
static void symload(char const *filename);
static void enable_raw_mode();
static void disable_raw_mode();

int main(int argc, char *argv[]) {
  FILE *rom;
  char const *labellist = NULL;
  bool random = false;

  atexit(disable_raw_mode);

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
      random = true;
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

  if (random) {
    srand((unsigned int)time(NULL));
    for (UInt i = 0; i < sizeof(MEM); ++i) {
      MEM[i] = (U8)rand();
    }
  }

  size_t read = fread(MEM + PC, 1, sizeof(MEM) - PC, rom);
  if (read != (sizeof(MEM) - PC)) {
    int err = ferror(rom);
    if (err) {
      fprintf(stderr, "Error reading ROM file: %s\n", strerror(err));
      return EXIT_FAILURE;
    }
  }
  fclose(rom);

  if (labellist) {
    symload(labellist);
  }

  if (!debug) {
    enable_raw_mode();
  }

  while (true) {
    tick();
  }

  return EXIT_SUCCESS;
}

static U8 io_addr = 0x00;
static U8 io_data = 0x00;
static U8 io_status = 0x00;

static U8 io_addr_read() { return io_addr; }

static U8 io_data_read() {
  switch (io_addr) {
  case 0x00: {
    if (io_status & 0x01) {
      return io_status;
    }
    struct pollfd pfd = {STDIN_FILENO, POLLIN, 0};
    if (poll(&pfd, 1, 0) > 0) {
      io_data = fgetc(stdin);
      io_status |= 0x01;
    }
    return io_status;
  }
  case 0x01:
    return io_data;
  default:
    return 0x00;
  }
}

static void io_addr_write(U8 val) { io_addr = val; }

static void io_data_write(U8 val) {
  switch (io_addr) {
  case 0x00:
    io_status &= ~0x01;
    return;
  case 0x01:
    fputc(val, stdout);
    fflush(stdout);
    return;
  default:
    return;
  }
}

static void doop();

typedef struct Breakpoint Breakpoint;

struct Breakpoint {
  Breakpoint *next;
  U16 num;
  U16 addr;
};

static U16 bpcnt = 0;
static Breakpoint *bphead = NULL;
static Breakpoint nextpoint = {NULL, 0, 0};

static void tick() {
  if (nextpoint.next && (PC == nextpoint.addr)) {
    debug = true;
    nextpoint.next = NULL;
  }
  for (Breakpoint const *bp = bphead; bp; bp = bp->next) {
    if (PC != bp->addr) {
      continue;
    }
    fprintf(stderr, "Hit breakpoint %u at $%04X\r\n", bp->num, bp->addr);
    debug = true;
    break;
  }
  if (debug) {
    debugger();
  }
  // TODO: check for interrupts here
  doop();
}

typedef struct Symbol Symbol;

struct Symbol {
  Symbol *next;
  char const *name;
  Int val;
};

static U16 symcnt = 0;
static Symbol *symhead = NULL;

static Symbol *symadd(char const *name, Int val) {
  Symbol *sym = malloc(sizeof(Symbol));
  sym->name = strdup(name);
  sym->val = val;
  sym->next = symhead;
  symhead = sym;
  ++symcnt;
  return sym;
}

static void symload(char const *filename) {
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
    symadd(name, val);
  }
  fclose(file);
}

static void enable_raw_mode() {
  if (raw_mode) {
    return;
  }
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
    return;
  }
  struct termios raw = orig_termios;
  cfmakeraw(&raw);
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
    return;
  }
  raw_mode = true;
}

static void disable_raw_mode() {
  if (!raw_mode) {
    return;
  }
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
  raw_mode = false;
}

static Symbol const *symfind(char const *name, size_t namelen) {
  for (Symbol const *sym = symhead; sym; sym = sym->next) {
    if (strlen(sym->name) != namelen) {
      continue;
    }
    if (strncmp(sym->name, name, namelen) == 0) {
      return sym;
    }
  }
  return NULL;
}

static Symbol const *symvalfind(Int val) {
  for (Symbol const *sym = symhead; sym; sym = sym->next) {
    if (val == sym->val) {
      return sym;
    }
  }
  return NULL;
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
  size_t len;
  UInt type;
  Int val;
} Tok;

static Tok tokstash = {NULL, 0, TOK_EOE, 0};

static void eat() {
  if (tokstash.start) {
    tokstash.start += tokstash.len;
    tokstash.len = 0;
  }
}

static Tok peek(char const *str) {
  if (!str) {
    if (tokstash.type == TOK_EOE) {
      return tokstash;
    }
    str = tokstash.start;
  } else {
    tokstash.start = str;
  }
  tokstash.type = TOK_EOE;
  while (isspace((unsigned char)*str)) {
    ++str;
  }
  if (*str == '\0') {
    return tokstash;
  }
  tokstash.start = str;
  if (*str == '\'') {
    ++str;
    switch (*str) {
    case '\0':
      tokstash.type = TOK_ERR;
      tokstash.len = 0;
      return tokstash;
    case '\\':
      ++str;
      switch (*str) {
      case 'n':
        tokstash.val = '\n';
        break;
      case 't':
        tokstash.val = '\t';
        break;
      case '\'':
        tokstash.val = '\'';
        break;
      case '\\':
        tokstash.val = '\\';
        break;
      case '0':
        tokstash.val = '\0';
        break;
      default:
        tokstash.type = TOK_ERR;
        tokstash.len = 0;
        return tokstash;
      }
    default:
      tokstash.val = (Int)(unsigned char)(*str);
      break;
    }
    ++str;
    if (*str != '\'') {
      tokstash.type = TOK_ERR;
      tokstash.len = 0;
      return tokstash;
    }
    ++str;
    tokstash.type = TOK_NUM;
    tokstash.len = 3;
    return tokstash;
  }
  if (isdigit((unsigned char)*str) || (*str == '$') || (*str == '%')) {
    if (*str == '%') {
      ++str;
      if ((*str != '0') && (*str != '1')) {
        // edge case, this is a modulus
        tokstash.type = '%';
        tokstash.len = 1;
        return tokstash;
      }
      while ((*str == '0') || (*str == '1')) {
        ++str;
      }
      tokstash.val = strtol(tokstash.start + 1, NULL, 2);
    } else if (*str == '$') {
      ++str;
      while (isxdigit((unsigned char)*str)) {
        ++str;
      }
      tokstash.val = strtol(tokstash.start + 1, NULL, 16);
    } else {
      while (isdigit((unsigned char)*str)) {
        ++str;
      }
      tokstash.val = strtol(tokstash.start, NULL, 10);
    }
    tokstash.len = str - tokstash.start;
    tokstash.type = TOK_NUM;
    return tokstash;
  }
  if (isalpha((unsigned char)*str) || (*str == '_')) {
    ++str;
    while (isalnum((unsigned char)*str) || (*str == '_')) {
      ++str;
    }
    tokstash.len = str - tokstash.start;
    tokstash.type = TOK_ID;
    return tokstash;
  }
  for (UInt i = 0; i < (sizeof(DIGRAPHS) / sizeof(DIGRAPHS[0])); ++i) {
    char const *dg = DIGRAPHS[i].name;
    size_t dgl = strlen(dg);
    if (strncmp(str, dg, dgl) == 0) {
      tokstash.type = DIGRAPHS[i].type;
      tokstash.len = dgl;
      return tokstash;
    }
  }
  tokstash.type = (unsigned char)*str;
  tokstash.len = 1;
  return tokstash;
}

enum {
  EXPR_NUM,
  EXPR_ID,
  EXPR_OP,
};

typedef struct {
  UInt kind;
  Tok tok;
  bool unary;
} Expr;

static U8 prec(Tok tok, bool unary) {
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

static Expr ostack[64];
static Expr estack[64];
static Int istack[64];
static UInt olen;
static UInt elen;
static UInt ilen;

static Int solve() {
  for (UInt i = 0; i < elen; ++i) {
    Expr const *ex = estack + i;
    switch (ex->kind) {
    case EXPR_NUM:
      istack[ilen++] = ex->tok.val;
      break;
    case EXPR_ID: {
      if (ex->tok.len == 1) {
        switch (tolower((unsigned char)ex->tok.start[0])) {
        case 'a':
          istack[ilen++] = A;
          break;
        case 'x':
          istack[ilen++] = X;
          break;
        case 'y':
          istack[ilen++] = Y;
          break;
        default:
          break;
        }
      }
      if (ex->tok.len == 2) {
        if ((tolower((unsigned char)ex->tok.start[0]) == 'p') &&
            (tolower((unsigned char)ex->tok.start[1]) == 'c')) {
          istack[ilen++] = PC;
          break;
        }
        if ((tolower((unsigned char)ex->tok.start[0]) == 's') &&
            (tolower((unsigned char)ex->tok.start[1]) == 'p')) {
          istack[ilen++] = 0x0100 | ((U16)SP);
          break;
        }
      }
      Symbol const *sym = symfind(ex->tok.start, ex->tok.len);
      if (!sym) {
        return INT_MAX;
      }
      istack[ilen++] = sym->val;
      break;
    }
    case EXPR_OP: {
      Int rhs = istack[--ilen];
      if (ex->unary) {
        switch (ex->tok.type) {
        case '+':
          istack[ilen++] = rhs;
          break;
        case '-':
          istack[ilen++] = -rhs;
          break;
        case '!':
          istack[ilen++] = !rhs;
          break;
        case '~':
          istack[ilen++] = ~rhs;
          break;
        case '<':
          istack[ilen++] = rhs & 0xFF;
          break;
        case '>':
          istack[ilen++] = (((UInt)rhs) >> 8) & 0xFF;
          break;
        case '*':
          if ((rhs < 0) || (rhs > U16_MAX)) {
            return INT_MAX;
          }
          istack[ilen++] = MEM[(U16)rhs];
          break;
        default:
          abort();
        }
        continue;
      }
      Int lhs = istack[--ilen];
      switch (ex->tok.type) {
      case '+':
        istack[ilen++] = lhs + rhs;
        break;
      case '-':
        istack[ilen++] = lhs - rhs;
        break;
      case '*':
        istack[ilen++] = lhs * rhs;
        break;
      case '/':
        if (rhs == 0) {
          return INT_MAX;
        }
        istack[ilen++] = lhs / rhs;
        break;
      case '%':
        if (rhs == 0) {
          return INT_MAX;
        }
        istack[ilen++] = lhs % rhs;
        break;
      case '<':
        istack[ilen++] = lhs < rhs;
        break;
      case '>':
        istack[ilen++] = lhs > rhs;
        break;
      case '&':
        istack[ilen++] = lhs & rhs;
        break;
      case '|':
        istack[ilen++] = lhs | rhs;
        break;
      case '^':
        istack[ilen++] = lhs ^ rhs;
        break;
      case TOK_SRA:
        istack[ilen++] = lhs >> rhs;
        break;
      case TOK_SRL:
        istack[ilen++] = ((UInt)lhs) >> rhs;
        break;
      case TOK_SLL:
        istack[ilen++] = lhs << rhs;
        break;
      case TOK_AND:
        istack[ilen++] = lhs && rhs;
        break;
      case TOK_OR:
        istack[ilen++] = lhs || rhs;
        break;
      case TOK_LTE:
        istack[ilen++] = lhs <= rhs;
        break;
      case TOK_GTE:
        istack[ilen++] = lhs >= rhs;
        break;
      case TOK_EQ:
        istack[ilen++] = lhs == rhs;
        break;
      case TOK_NEQ:
        istack[ilen++] = lhs != rhs;
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
  if (ilen != 1) {
    return INT_MAX;
  }
  return istack[0];
}

static void pushop(Tok tok, bool unary) {
  if (tok.type == '(') {
    ostack[olen++] = (Expr){EXPR_OP, tok, true};
    return;
  }
  while (olen > 0) {
    Expr top = ostack[--olen];
    if ((top.tok.type == '(') ||
        (prec(top.tok, top.unary) >= prec(tok, unary))) {
      ostack[olen++] = top;
      break;
    }
    estack[elen++] = top;
  }
  ostack[olen++] = (Expr){EXPR_OP, tok, unary};
}

static Int expr() {
  olen = 0;
  elen = 0;
  ilen = 0;
  bool expectop = false;
  UInt parendepth = 0;
  while (true) {
    Tok tok = peek(NULL);
    switch (tok.type) {
    case '+':
    case '-':
    case '<':
    case '>':
    case '*':
      // sometimes unary
      pushop(tok, !expectop);
      eat();
      expectop = false;
      continue;
    case '!':
    case '~':
      // always unary
      pushop(tok, true);
      eat();
      expectop = false;
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
      if (!expectop) {
        return INT_MAX;
      }
      pushop(tok, false);
      eat();
      expectop = false;
      continue;
    case TOK_NUM:
      if (expectop) {
        return INT_MAX;
      }
      estack[elen++] = (Expr){EXPR_NUM, tok, false};
      eat();
      expectop = true;
      continue;
    case TOK_ID:
      if (expectop) {
        return INT_MAX;
      }
      estack[elen++] = (Expr){EXPR_ID, tok, false};
      eat();
      expectop = true;
      continue;
    case '(':
      if (expectop) {
        return INT_MAX;
      }
      pushop(tok, true);
      eat();
      ++parendepth;
      expectop = false;
      continue;
    case ')':
      if (!expectop) {
        return INT_MAX;
      }
      --parendepth;
      while (true) {
        if (olen == 0) {
          return INT_MAX;
        }
        Expr top = ostack[--olen];
        if (top.tok.type == '(') {
          break;
        }
        estack[elen++] = top;
      }
      eat();
      continue;
    default:
      if (!expectop) {
        return INT_MAX;
      }
      if (parendepth > 0) {
        return INT_MAX;
      }
      while (olen > 0) {
        estack[elen++] = ostack[--olen];
      }
      return solve();
    }
  }
}

static U16 disasm(U16 addr);
static void regs();

typedef enum { DBG_DEBUG, DBG_BREAK, DBG_CONTINUE, DBG_CLEAR } DbgResult;

static DbgResult dbgquit() { exit(EXIT_SUCCESS); }

static DbgResult dbgcont() { return DBG_CONTINUE; }

static DbgResult dbgstep() { return DBG_BREAK; }

static DbgResult dbgclear() { return DBG_CLEAR; }

static DbgResult dbgnext() {
  U8 op = MEM[PC];
  if (op == 0x20) { // JSR
    nextpoint.addr = PC + 3;
    nextpoint.next = bphead; // to mark it active
    return DBG_CONTINUE;
  }
  return DBG_BREAK;
}

static DbgResult dbgregs() {
  regs();
  return DBG_DEBUG;
}

static DbgResult dbgbreak() {
  Tok tok = peek(NULL);
  if (tok.type == TOK_EOE) {
    fprintf(stderr, "No address provided for breakpoint\n");
    return DBG_DEBUG;
  }
  Int addr = expr();
  if ((addr == INT_MAX) || (addr > U16_MAX)) {
    Tok end = peek(NULL);
    UInt len = end.start + end.len - tok.start;
    fprintf(stderr, "Invalid address for breakpoint: \"%.*s\"\n", (int)len,
            tok.start);
    return DBG_DEBUG;
  }
  Breakpoint *bp = malloc(sizeof(Breakpoint));
  bp->num = ++bpcnt;
  bp->addr = (U16)addr;
  bp->next = bphead;
  bphead = bp;
  fprintf(stderr, "Breakpoint %u set at $%04X\n", bp->num, bp->addr);
  return DBG_DEBUG;
}

static DbgResult dbgdel() {
  Tok tok = peek(NULL);
  if (tok.type == TOK_EOE) {
    fprintf(stderr, "No breakpoint number provided for deletion\n");
    return DBG_DEBUG;
  }
  Int num = expr();
  if ((num == INT_MAX) || (num > U16_MAX)) {
    Tok end = peek(NULL);
    UInt len = end.start + end.len - tok.start;
    fprintf(stderr, "Invalid breakpoint number: \"%.*s\"\n", (int)len,
            tok.start);
    return DBG_DEBUG;
  }
  Breakpoint **prev = &bphead;
  Breakpoint *bp = bphead;
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

static DbgResult dbgexa() {
  Int start = PC;
  Int len = 16;
  Tok tok = peek(NULL);
  if (tok.type != TOK_EOE) {
    start = expr();
    if ((start == INT_MAX) || (start > U16_MAX)) {
      Tok end = peek(NULL);
      UInt len = end.start + end.len - tok.start;
      fprintf(stderr, "Invalid address for examine: \"%.*s\"\n", (int)len,
              tok.start);
      return DBG_DEBUG;
    }
  }
  tok = peek(NULL);
  if (tok.type != TOK_EOE) {
    if (tok.type != ',') {
      fprintf(stderr, "Expected ',' after address\n");
      return DBG_DEBUG;
    }
    tok = peek(NULL);
    if (tok.type == TOK_EOE) {
      fprintf(stderr, "No length provided for disasm\n");
      return DBG_DEBUG;
    }
    eat();
    len = expr();
    if ((len == INT_MAX) || (len <= 0)) {
      Tok end = peek(NULL);
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

static DbgResult dbgdis() {
  Int start = PC;
  Int len = 1;
  Tok tok = peek(NULL);
  if (tok.type != TOK_EOE) {
    start = expr();
    if ((start == INT_MAX) || (start > U16_MAX)) {
      Tok end = peek(NULL);
      UInt len = end.start + end.len - tok.start;
      fprintf(stderr, "Invalid address for disasm: \"%.*s\"\n", (int)len,
              tok.start);
      return DBG_DEBUG;
    }
  }
  tok = peek(NULL);
  if (tok.type != TOK_EOE) {
    if (tok.type != ',') {
      fprintf(stderr, "Expected ',' after address\n");
      return DBG_DEBUG;
    }
    eat();
    tok = peek(NULL);
    if (tok.type == TOK_EOE) {
      fprintf(stderr, "No length provided for disasm\n");
      return DBG_DEBUG;
    }
    len = expr();
    if ((len == INT_MAX) || (len <= 0)) {
      Tok end = peek(NULL);
      UInt len = end.start + end.len - tok.start;
      fprintf(stderr, "Invalid length for disasm: \"%.*s\"\n", (int)len,
              tok.start);
      return DBG_DEBUG;
    }
  }
  U16 addr = (U16)start;
  Int count = 0;
  while ((count < len) && (addr <= U16_MAX)) {
    addr = disasm(addr);
    ++count;
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
    {"disasm", dbgdis},     {"clear", dbgclear},   {NULL, NULL}};

static char *dbgprompt(EditLine *el) {
  (void)el;
  return "> ";
}

static void printmatches(char const *prefix, size_t len) {
  bool first = true;
  for (DbgCmd const *cmd = DEBUG_CMDS; cmd->name; ++cmd) {
    if (strncmp(prefix, cmd->name, len) == 0) {
      fprintf(stderr, "%s%s", first ? "" : ", ", cmd->name);
      first = false;
    }
  }
  for (Symbol const *sym = symhead; sym; sym = sym->next) {
    if (strncmp(prefix, sym->name, len) == 0) {
      fprintf(stderr, "%s%s", first ? "" : ", ", sym->name);
      first = false;
    }
  }
}

static unsigned char dbgcompl(EditLine *el, int ch) {
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
  for (DbgCmd const *cmd = DEBUG_CMDS; cmd->name; ++cmd) {
    if (strncmp(str, cmd->name, len) == 0) {
      match = cmd->name;
      ++matchcnt;
    }
  }
  for (Symbol const *sym = symhead; sym; sym = sym->next) {
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
    printmatches(str, (size_t)len);
    fprintf(stderr, " ?\n");
    return CC_REDISPLAY;
  }
  return CC_REFRESH;
}

static void debugger() {
  static EditLine *el = NULL;
  static History *hist = NULL;
  static HistEvent ev;
  static char *prevline = NULL;
  static char *workline = NULL;
  disable_raw_mode();
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
  disasm(PC);
  while (true) {
    free(workline);
    int count;
    char const *line = el_gets(el, &count);
    if (!line || (count <= 0)) {
      exit(EXIT_FAILURE);
    }
    workline = strdup(line);
    Tok tok = peek(workline);
    if (tok.type == TOK_EOE) {
      if (prevline) {
        free(workline);
        workline = strdup(prevline);
        tok = peek(workline);
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
    UInt matchcnt = 0;
    for (DbgCmd const *cmd = DEBUG_CMDS; cmd->name; ++cmd) {
      if (strncmp(tok.start, cmd->name, tok.len) != 0) {
        continue;
      }
      match = cmd;
      ++matchcnt;
    }
    if (matchcnt == 0) {
      fprintf(stderr, "Unknown command: %.*s\n", (int)tok.len, tok.start);
      continue;
    }
    if (matchcnt > 1) {
      fprintf(stderr, "Ambiguous command: %.*s (", (int)tok.len, tok.start);
      printmatches(tok.start, tok.len);
      fprintf(stderr, ")\n");
      continue;
    }
    eat();
    DbgResult res = match->fn();
    if (res == DBG_BREAK) {
      break;
    }
    if (res == DBG_CONTINUE) {
      debug = false;
      enable_raw_mode();
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

static void dissym(U16 addr) {
  Symbol const *sym = symvalfind((UInt)addr);
  if (sym) {
    fprintf(stderr, CYAN("; %s"), sym->name);
    return;
  }
  sym = symvalfind((UInt)(addr - 1));
  if (sym) {
    fprintf(stderr, CYAN("; %s+1"), sym->name);
  }
}

static U16 disimpl(U8 op, U16 addr, char const *mne) {
  fprintf(stderr, " %02X      ", op);
  fprintf(stderr, "  " BLUE("%s") "            ", mne);
  return addr;
}

static U16 disimm(U8 op, U16 addr, char const *mne) {
  U8 val = MEM[addr++];
  fprintf(stderr, " %02X %02X   ", op, val);
  fprintf(stderr, "  " BLUE("%s") " " MAGENTA("#$%02X") "       ", mne, val);
  return addr;
}

static U16 diszp(U8 op, U16 addr, char const *mne) {
  U8 zp = MEM[addr++];
  fprintf(stderr, " %02X %02X   ", op, zp);
  fprintf(stderr, "  " BLUE("%s") " $%02X        ", mne, zp);
  dissym((U16)zp);
  return addr;
}

static U16 diszpx(U8 op, U16 addr, char const *mne) {
  U8 zp = MEM[addr++];
  fprintf(stderr, " %02X %02X   ", op, zp);
  fprintf(stderr, "  " BLUE("%s") " $%02X,X      ", mne, zp);
  dissym((U16)zp);
  return addr;
}

static U16 diszpy(U8 op, U16 addr, char const *mne) {
  U8 zp = MEM[addr++];
  fprintf(stderr, " %02X %02X   ", op, zp);
  fprintf(stderr, "  " BLUE("%s") " $%02X,Y      ", mne, zp);
  dissym((U16)zp);
  return addr;
}

static U16 disab(U8 op, U16 addr, char const *mne) {
  U8 lo = MEM[addr++];
  U8 hi = MEM[addr++];
  U16 ab = (((U16)hi) << 8) | lo;
  fprintf(stderr, " %02X %02X %02X", op, lo, hi);
  fprintf(stderr, "  " BLUE("%s") " $%04X      ", mne, ab);
  dissym(ab);
  return addr;
}

static U16 disabx(U8 op, U16 addr, char const *mne) {
  U8 lo = MEM[addr++];
  U8 hi = MEM[addr++];
  U16 ab = (((U16)hi) << 8) | lo;
  fprintf(stderr, " %02X %02X %02X", op, lo, hi);
  fprintf(stderr, "  " BLUE("%s") " $%04X,X    ", mne, ab);
  dissym(ab);
  return addr;
}

static U16 disaby(U8 op, U16 addr, char const *mne) {
  U8 lo = MEM[addr++];
  U8 hi = MEM[addr++];
  U16 ab = (((U16)hi) << 8) | lo;
  fprintf(stderr, " %02X %02X %02X", op, lo, hi);
  fprintf(stderr, "  " BLUE("%s") " $%04X,Y    ", mne, ab);
  dissym(ab);
  return addr;
}

static U16 disid(U8 op, U16 addr, char const *mne) {
  U8 lo = MEM[addr++];
  U8 hi = MEM[addr++];
  U16 ptr = (((U16)hi) << 8) | lo;
  fprintf(stderr, " %02X %02X %02X", op, lo, hi);
  fprintf(stderr, "  " BLUE("%s") " ($%04X)    ", mne, ptr);
  dissym(ptr);
  return addr;
}

static U16 disidx(U8 op, U16 addr, char const *mne) {
  U8 zp = MEM[addr++];
  fprintf(stderr, " %02X %02X   ", op, zp);
  fprintf(stderr, "  " BLUE("%s") " ($%02X,X)    ", mne, zp);
  dissym((U16)zp);
  return addr;
}

static U16 disidy(U8 op, U16 addr, char const *mne) {
  U8 zp = MEM[addr++];
  fprintf(stderr, " %02X %02X   ", op, zp);
  fprintf(stderr, "  " BLUE("%s") " ($%02X),Y    ", mne, zp);
  dissym((U16)zp);
  return addr;
}

static U16 disrel(U8 op, U16 addr, char const *mne) {
  I8 offset = (I8)MEM[addr++];
  U16 target = addr + offset;
  fprintf(stderr, " %02X %02X   ", op, (U8)offset);
  fprintf(stderr, "  " BLUE("%s") " $%04X      ", mne, target);
  dissym(target);
  return addr;
}

typedef U16 (*DisFn)(U8, U16, char const *);

typedef struct {
  char const *mne;
  DisFn fn;
} DisEntry;

static DisEntry const DISASM_TABLE[256] = {
    [0x00] = {"BRK", disimm},  [0x01] = {"ORA", disidx},
    [0x05] = {"ORA", diszp},   [0x06] = {"ASL", diszp},
    [0x08] = {"PHP", disimpl}, [0x09] = {"ORA", disimm},
    [0x0A] = {"ASL", disimpl}, [0x0D] = {"ORA", disab},
    [0x0E] = {"ASL", disab},   [0x10] = {"BPL", disrel},
    [0x11] = {"ORA", disidy},  [0x15] = {"ORA", diszpx},
    [0x16] = {"ASL", diszpx},  [0x18] = {"CLC", disimpl},
    [0x19] = {"ORA", disaby},  [0x1D] = {"ORA", disabx},
    [0x1E] = {"ASL", disabx},  [0x20] = {"JSR", disab},
    [0x21] = {"AND", disidx},  [0x24] = {"BIT", diszp},
    [0x25] = {"AND", diszp},   [0x26] = {"ROL", diszp},
    [0x28] = {"PLP", disimpl}, [0x29] = {"AND", disimm},
    [0x2A] = {"ROL", disimpl}, [0x2C] = {"BIT", disab},
    [0x2D] = {"AND", disab},   [0x2E] = {"ROL", disab},
    [0x30] = {"BMI", disrel},  [0x31] = {"AND", disidy},
    [0x35] = {"AND", diszpx},  [0x36] = {"ROL", diszpx},
    [0x38] = {"SEC", disimpl}, [0x39] = {"AND", disaby},
    [0x3D] = {"AND", disabx},  [0x3E] = {"ROL", disabx},
    [0x40] = {"RTI", disimpl}, [0x41] = {"EOR", disidx},
    [0x45] = {"EOR", diszp},   [0x46] = {"LSR", diszp},
    [0x48] = {"PHA", disimpl}, [0x49] = {"EOR", disimm},
    [0x4A] = {"LSR", disimpl}, [0x4C] = {"JMP", disab},
    [0x4D] = {"EOR", disab},   [0x4E] = {"LSR", disab},
    [0x50] = {"BVC", disrel},  [0x51] = {"EOR", disidy},
    [0x55] = {"EOR", diszpx},  [0x56] = {"LSR", diszpx},
    [0x58] = {"CLI", disimpl}, [0x59] = {"EOR", disaby},
    [0x5D] = {"EOR", disabx},  [0x5E] = {"LSR", disabx},
    [0x60] = {"RTS", disimpl}, [0x61] = {"ADC", disidx},
    [0x65] = {"ADC", diszp},   [0x66] = {"ROR", diszp},
    [0x68] = {"PLA", disimpl}, [0x69] = {"ADC", disimm},
    [0x6A] = {"ROR", disimpl}, [0x6C] = {"JMP", disid},
    [0x6D] = {"ADC", disab},   [0x6E] = {"ROR", disab},
    [0x70] = {"BVS", disrel},  [0x71] = {"ADC", disidy},
    [0x75] = {"ADC", diszpx},  [0x76] = {"ROR", diszpx},
    [0x78] = {"SEI", disimpl}, [0x79] = {"ADC", disaby},
    [0x7D] = {"ADC", disabx},  [0x7E] = {"ROR", disabx},
    [0x81] = {"STA", disidx},  [0x84] = {"STY", diszp},
    [0x85] = {"STA", diszp},   [0x86] = {"STX", diszp},
    [0x88] = {"DEY", disimpl}, [0x8A] = {"TXA", disimpl},
    [0x8C] = {"STY", disab},   [0x8D] = {"STA", disab},
    [0x8E] = {"STX", disab},   [0x90] = {"BCC", disrel},
    [0x91] = {"STA", disidy},  [0x94] = {"STY", diszpx},
    [0x95] = {"STA", diszpx},  [0x96] = {"STX", diszpy},
    [0x98] = {"TYA", disimpl}, [0x99] = {"STA", disaby},
    [0x9A] = {"TXS", disimpl}, [0x9D] = {"STA", disabx},
    [0xA0] = {"LDY", disimm},  [0xA1] = {"LDA", disidx},
    [0xA2] = {"LDX", disimm},  [0xA4] = {"LDY", diszp},
    [0xA5] = {"LDA", diszp},   [0xA6] = {"LDX", diszp},
    [0xA8] = {"TAY", disimpl}, [0xA9] = {"LDA", disimm},
    [0xAA] = {"TAX", disimpl}, [0xAC] = {"LDY", disab},
    [0xAD] = {"LDA", disab},   [0xAE] = {"LDX", disab},
    [0xB0] = {"BCS", disrel},  [0xB1] = {"LDA", disidy},
    [0xB4] = {"LDY", diszpx},  [0xB5] = {"LDA", diszpx},
    [0xB6] = {"LDX", diszpy},  [0xB8] = {"CLV", disimpl},
    [0xB9] = {"LDA", disaby},  [0xBA] = {"TSX", disimpl},
    [0xBC] = {"LDY", disabx},  [0xBD] = {"LDA", disabx},
    [0xBE] = {"LDX", disaby},  [0xC0] = {"CPY", disimm},
    [0xC1] = {"CMP", disidx},  [0xC4] = {"CPY", diszp},
    [0xC5] = {"CMP", diszp},   [0xC6] = {"DEC", diszp},
    [0xC8] = {"INY", disimpl}, [0xC9] = {"CMP", disimm},
    [0xCA] = {"DEX", disimpl}, [0xCC] = {"CPY", disab},
    [0xCD] = {"CMP", disab},   [0xCE] = {"DEC", disab},
    [0xD0] = {"BNE", disrel},  [0xD1] = {"CMP", disidy},
    [0xD5] = {"CMP", diszpx},  [0xD6] = {"DEC", diszpx},
    [0xD8] = {"CLD", disimpl}, [0xD9] = {"CMP", disaby},
    [0xDD] = {"CMP", disabx},  [0xDE] = {"DEC", disabx},
    [0xE0] = {"CPX", disimm},  [0xE1] = {"SBC", disidx},
    [0xE4] = {"CPX", diszp},   [0xE5] = {"SBC", diszp},
    [0xE6] = {"INC", diszp},   [0xE8] = {"INX", disimpl},
    [0xE9] = {"SBC", disimm},  [0xEA] = {"NOP", disimpl},
    [0xEC] = {"CPX", disab},   [0xED] = {"SBC", disab},
    [0xEE] = {"INC", disab},   [0xF0] = {"BEQ", disrel},
    [0xF1] = {"SBC", disidy},  [0xF5] = {"SBC", diszpx},
    [0xF6] = {"INC", diszpx},  [0xF8] = {"SED", disimpl},
    [0xF9] = {"SBC", disaby},  [0xFD] = {"SBC", disabx},
    [0xFE] = {"INC", disabx},
};

static U16 disasm(U16 addr) {
  Symbol const *sym = symvalfind((UInt)addr);
  if (sym) {
    fprintf(stderr, YELLOW("%s:") "\n", sym->name);
  }
  fprintf(stderr, "%04X ", addr);
  U8 op = MEM[addr++];
  DisEntry const *entry = &DISASM_TABLE[op];
  if (entry->fn) {
    addr = entry->fn(op, addr, entry->mne);
  } else {
    addr = disimpl(op, addr, "ILL");
  }
  fprintf(stderr, "\n");
  return addr;
}

static void flag(U8 flag, bool condition) {
  if (condition) {
    P |= flag;
  } else {
    P &= ~flag;
  }
}

static U8 *impl() { return NULL; }

static U8 *acc() { return &A; }

static U8 *imm() { return &MEM[PC++]; }

static U8 *zp() { return &MEM[MEM[PC++]]; }

static U8 *zpx() {
  U8 addr = MEM[PC++] + X;
  return &MEM[addr];
}

static U8 *zpy() {
  U8 addr = MEM[PC++] + Y;
  return &MEM[addr];
}

static U8 *ab() {
  U8 lo = MEM[PC++];
  U8 hi = MEM[PC++];
  U16 addr = (((U16)hi) << 8) | lo;
  return &MEM[addr];
}

static U8 *abx() {
  U8 lo = MEM[PC++];
  U8 hi = MEM[PC++];
  U16 addr = ((((U16)hi) << 8) | lo) + X;
  return &MEM[addr];
}

static U8 *aby() {
  U8 lo = MEM[PC++];
  U8 hi = MEM[PC++];
  U16 addr = ((((U16)hi) << 8) | lo) + Y;
  return &MEM[addr];
}

static U8 *idx() {
  U8 zpaddr = MEM[PC++] + X;
  U8 lo = MEM[zpaddr];
  U8 hi = MEM[(U8)(zpaddr + 1)];
  U16 addr = (((U16)hi) << 8) | lo;
  return &MEM[addr];
}

static U8 *idy() {
  U8 zpaddr = MEM[PC++];
  U8 lo = MEM[zpaddr];
  U8 hi = MEM[(U8)(zpaddr + 1)];
  U16 addr = ((((U16)hi) << 8) | lo) + Y;
  return &MEM[addr];
}

static U8 *jab() {
  U16 addr = PC;
  PC += 2;
  return &MEM[addr];
}

static U8 *jid() {
  static U16 addr; // HACK for wrapping bug
  U8 idlo = MEM[PC++];
  U8 idhi = MEM[PC++];
  U8 lo = MEM[(((U16)idhi) << 8) | idlo];
  U8 hi = MEM[(((U16)idhi) << 8) | (U8)(idlo + 1)]; // wrapping bug
  addr = (((U16)hi) << 8) | lo;
  return (U8 *)&addr;
}

static void adc(U8 *val) {
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

static void and(U8 *val) {
  A &= *val;
  flag(FLAG_ZERO, A == 0);
  flag(FLAG_NEGATIVE, (A & 0x80) != 0);
}

static void asl(U8 *val) {
  flag(FLAG_CARRY, (*val & 0x80) != 0);
  *val <<= 1;
  flag(FLAG_ZERO, *val == 0);
  flag(FLAG_NEGATIVE, (*val & 0x80) != 0);
}

static void bcc(U8 *val) {
  if (!(P & FLAG_CARRY)) {
    PC += (I8)*val;
  }
}

static void bcs(U8 *val) {
  if (P & FLAG_CARRY) {
    PC += (I8)*val;
  }
}

static void beq(U8 *val) {
  if (P & FLAG_ZERO) {
    PC += (I8)*val;
  }
}

static void bit(U8 *val) {
  U8 res = A & *val;
  flag(FLAG_ZERO, res == 0);
  flag(FLAG_OVERFLOW, (*val & 0x40) != 0);
  flag(FLAG_NEGATIVE, (*val & 0x80) != 0);
}

static void bmi(U8 *val) {
  if (P & FLAG_NEGATIVE) {
    PC += (I8)*val;
  }
}

static void bne(U8 *val) {
  if (!(P & FLAG_ZERO)) {
    PC += (I8)*val;
  }
}

static void bpl(U8 *val) {
  if (!(P & FLAG_NEGATIVE)) {
    PC += (I8)*val;
  }
}

static void brk_(U8 *val) {
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

static void bvc(U8 *val) {
  if (!(P & FLAG_OVERFLOW)) {
    PC += (I8)*val;
  }
}

static void bvs(U8 *val) {
  if (P & FLAG_OVERFLOW) {
    PC += (I8)*val;
  }
}

static void clc(U8 *val) {
  (void)val;
  flag(FLAG_CARRY, false);
}

static void cld(U8 *val) {
  (void)val;
  flag(FLAG_DECIMAL, false);
}

static void cli(U8 *val) {
  (void)val;
  flag(FLAG_INTERRUPT, false);
}

static void clv(U8 *val) {
  (void)val;
  flag(FLAG_OVERFLOW, false);
}

static void cmp(U8 *val) {
  U8 res = A - *val;
  flag(FLAG_CARRY, A >= *val);
  flag(FLAG_ZERO, res == 0);
  flag(FLAG_NEGATIVE, (res & 0x80) != 0);
}

static void cpx(U8 *val) {
  U8 res = X - *val;
  flag(FLAG_CARRY, X >= *val);
  flag(FLAG_ZERO, res == 0);
  flag(FLAG_NEGATIVE, (res & 0x80) != 0);
}

static void cpy(U8 *val) {
  U8 res = Y - *val;
  flag(FLAG_CARRY, Y >= *val);
  flag(FLAG_ZERO, res == 0);
  flag(FLAG_NEGATIVE, (res & 0x80) != 0);
}

static void dec(U8 *val) {
  --(*val);
  flag(FLAG_ZERO, *val == 0);
  flag(FLAG_NEGATIVE, (*val & 0x80) != 0);
}

static void dex(U8 *val) {
  (void)val;
  --X;
  flag(FLAG_ZERO, X == 0);
  flag(FLAG_NEGATIVE, (X & 0x80) != 0);
}

static void dey(U8 *val) {
  (void)val;
  --Y;
  flag(FLAG_ZERO, Y == 0);
  flag(FLAG_NEGATIVE, (Y & 0x80) != 0);
}

static void eor(U8 *val) {
  A ^= *val;
  flag(FLAG_ZERO, A == 0);
  flag(FLAG_NEGATIVE, (A & 0x80) != 0);
}

static void inc(U8 *val) {
  ++(*val);
  flag(FLAG_ZERO, *val == 0);
  flag(FLAG_NEGATIVE, (*val & 0x80) != 0);
}

static void inx(U8 *val) {
  (void)val;
  ++X;
  flag(FLAG_ZERO, X == 0);
  flag(FLAG_NEGATIVE, (X & 0x80) != 0);
}

static void iny(U8 *val) {
  (void)val;
  ++Y;
  flag(FLAG_ZERO, Y == 0);
  flag(FLAG_NEGATIVE, (Y & 0x80) != 0);
}

static void jmp(U8 *val) { PC = *(U16 *)val; }

static void jsr(U8 *val) {
  U16 ret = PC - 1;
  MEM[0x0100 + SP--] = (U8)((ret >> 8) & 0xFF);
  MEM[0x0100 + SP--] = (U8)(ret & 0xFF);
  PC = *(U16 *)val;
}

static void lda(U8 *val) {
  if (val == (U8 *)&MEM[0]) {
    A = io_addr_read();
  } else if (val == (U8 *)&MEM[1]) {
    A = io_data_read();
  } else {
    A = *val;
  }
  flag(FLAG_ZERO, A == 0);
  flag(FLAG_NEGATIVE, (A & 0x80) != 0);
}

static void ldx(U8 *val) {
  if (val == (U8 *)&MEM[0]) {
    X = io_addr_read();
  } else if (val == (U8 *)&MEM[1]) {
    X = io_data_read();
  } else {
    X = *val;
  }
  flag(FLAG_ZERO, X == 0);
  flag(FLAG_NEGATIVE, (X & 0x80) != 0);
}

static void ldy(U8 *val) {
  if (val == (U8 *)&MEM[0]) {
    Y = io_addr_read();
  } else if (val == (U8 *)&MEM[1]) {
    Y = io_data_read();
  } else {
    Y = *val;
  }
  flag(FLAG_ZERO, Y == 0);
  flag(FLAG_NEGATIVE, (Y & 0x80) != 0);
}

static void lsr(U8 *val) {
  flag(FLAG_CARRY, (*val & 0x01) != 0);
  *val >>= 1;
  flag(FLAG_ZERO, *val == 0);
  flag(FLAG_NEGATIVE, false);
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
  bool cy = (P & FLAG_CARRY) != 0;
  flag(FLAG_CARRY, (*val & 0x80) != 0);
  *val = (*val << 1) | (cy ? 1 : 0);
  flag(FLAG_ZERO, *val == 0);
  flag(FLAG_NEGATIVE, (*val & 0x80) != 0);
}

static void ror(U8 *val) {
  bool cy = (P & FLAG_CARRY) != 0;
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

static void sec(U8 *val) {
  (void)val;
  flag(FLAG_CARRY, true);
}

static void sed(U8 *val) {
  (void)val;
  flag(FLAG_DECIMAL, true);
}

static void sei(U8 *val) {
  (void)val;
  flag(FLAG_INTERRUPT, true);
}

static void sta(U8 *val) {
  if (val == (U8 *)&MEM[0]) {
    io_addr_write(A);
    return;
  }
  if (val == (U8 *)&MEM[1]) {
    io_data_write(A);
    return;
  }
  *val = A;
}

static void stx(U8 *val) {
  if (val == (U8 *)&MEM[0]) {
    io_addr_write(X);
    return;
  }
  if (val == (U8 *)&MEM[1]) {
    io_data_write(X);
    return;
  }
  *val = X;
}

static void sty(U8 *val) {
  if (val == (U8 *)&MEM[0]) {
    io_addr_write(Y);
    return;
  }
  if (val == (U8 *)&MEM[1]) {
    io_data_write(Y);
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

typedef void (*ExecFn)(U8 *);
typedef U8 *(*AddrFn)();

typedef struct {
  ExecFn exec;
  AddrFn addr;
} OpEntry;

static OpEntry const OP_TABLE[256] = {
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

static void doop() {
  U8 op = MEM[PC++];
  OpEntry const *entry = &OP_TABLE[op];
  if (entry->exec) {
    entry->exec(entry->addr());
  } else {
    --PC;
    debug = true;
  }
}
