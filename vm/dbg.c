#include <ctype.h>
#include <histedit.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vm.h"

U8 dbgRead(U16 addr) {
  if (addr >= RAM_START_ADDR && addr <= RAM_END_ADDR) {
    return bus.ram[addr - RAM_START_ADDR];
  }
  if (addr >= ROM_START_ADDR && addr <= ROM_END_ADDR) {
    return bus.rom[addr - ROM_START_ADDR];
  }
  return 0;
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
          intStack[intCount++] = bus.cpu.a;
          break;
        case 'x':
          intStack[intCount++] = bus.cpu.x;
          break;
        case 'y':
          intStack[intCount++] = bus.cpu.y;
          break;
        default:
          break;
        }
      }
      if (expr->tok.len == 2) {
        if ((tolower((unsigned char)expr->tok.start[0]) == 'p') &&
            (tolower((unsigned char)expr->tok.start[1]) == 'c')) {
          intStack[intCount++] = bus.cpu.pc;
          break;
        }
        if ((tolower((unsigned char)expr->tok.start[0]) == 's') &&
            (tolower((unsigned char)expr->tok.start[1]) == 'p')) {
          intStack[intCount++] = 0x0100 | ((U16)bus.cpu.sp);
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
          intStack[intCount++] = bus.ram[(U16)rhs];
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

static void dbgRegs() {
  fprintf(stderr, "bus.cpu.pc:%04X bus.cpu.sp:01%02X bus.cpu.a:%02X bus.cpu.x:%02X bus.cpu.y:%02X bus.cpu.p:%02X |", bus.cpu.pc, bus.cpu.sp, bus.cpu.a,
          bus.cpu.x, bus.cpu.y, bus.cpu.p);
  fprintf(stderr, "%c%c%c%c%c%c%c%c|\n", (bus.cpu.p & CPU_FLAG_NEGATIVE) ? 'N' : '.',
          (bus.cpu.p & CPU_FLAG_OVERFLOW) ? 'V' : '.', (bus.cpu.p & CPU_FLAG_UNUSED) ? '1' : '.',
          (bus.cpu.p & CPU_FLAG_BREAK) ? 'B' : '.', (bus.cpu.p & CPU_FLAG_DECIMAL) ? 'D' : '.',
          (bus.cpu.p & CPU_FLAG_INTERRUPT) ? 'I' : '.', (bus.cpu.p & CPU_FLAG_ZERO) ? 'Z' : '.',
          (bus.cpu.p & CPU_FLAG_CARRY) ? 'C' : '.');
}

typedef enum { DBG_DEBUG, DBG_BREAK, DBG_CONTINUE, DBG_CLEAR } DbgResult;

static DbgResult dbgQuit() { exit(EXIT_SUCCESS); }

static DbgResult dbgCont() { return DBG_CONTINUE; }

static DbgResult dbgStep() { return DBG_BREAK; }

static DbgResult dbgClear() { return DBG_CLEAR; }

static DbgResult dbgNext() {
  U8 op = bus.ram[bus.cpu.pc];
  if (op == 0x20) { // JSR
    nextpoint.addr = bus.cpu.pc + 3;
    nextpoint.next = bpHead; // to mark it active
    return DBG_CONTINUE;
  }
  return DBG_BREAK;
}

static DbgResult dbgRegsCmd() {
  dbgRegs();
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
  Int start = bus.cpu.pc;
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
              bus.ram[(U16)(start + i)]);
    }
    fprintf(stderr, "  |");
    for (UInt i = 0; i < 16; ++i) {
      if ((start + i) > end) {
        fprintf(stderr, " ");
      } else {
        U8 byte = bus.ram[(U16)(start + i)];
        fprintf(stderr, "%c", isprint(byte) ? (char)byte : '.');
      }
    }
    fprintf(stderr, "|\n");
    start += 16;
  }
  return DBG_DEBUG;
}

static DbgResult dbgDis() {
  Int start = bus.cpu.pc;
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
    {(char const *[]){"registers", NULL}, dbgRegsCmd, HELP_REGS},
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
  for (Symbol const *sym = symFirst(); sym; sym = sym->next) {
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
  for (Symbol const *sym = symFirst(); sym; sym = sym->next) {
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

void dbgTick() {
  extern void termRawModeOff();
  extern void termRawModeOn();
  extern Bool debug;
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
  dbgRegs();
  disAsm(bus.cpu.pc);
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

