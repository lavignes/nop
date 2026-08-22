#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vm.h"

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

static void tokEat(Dbg *dbg) {
  if (dbg->tokStash.start) {
    dbg->tokStash.start += dbg->tokStash.len;
    dbg->tokStash.len = 0;
  }
}

static Tok tokPeek(Dbg *dbg, char const *str) {
  if (!str) {
    if (dbg->tokStash.type == TOK_EOE) {
      return dbg->tokStash;
    }
    str = dbg->tokStash.start;
  } else {
    dbg->tokStash.start = str;
  }
  dbg->tokStash.type = TOK_EOE;
  while (isspace((unsigned char)*str)) {
    ++str;
  }
  if (*str == '\0') {
    return dbg->tokStash;
  }
  dbg->tokStash.start = str;
  if (*str == '\'') {
    ++str;
    switch (*str) {
    case '\0':
      dbg->tokStash.type = TOK_ERR;
      dbg->tokStash.len = 0;
      return dbg->tokStash;
    case '\\':
      ++str;
      switch (*str) {
      case 'a':
        dbg->tokStash.val = '\a';
        break;
      case 'b':
        dbg->tokStash.val = '\b';
        break;
      case 'f':
        dbg->tokStash.val = '\f';
        break;
      case 'n':
        dbg->tokStash.val = '\n';
        break;
      case 'r':
        dbg->tokStash.val = '\r';
        break;
      case 't':
        dbg->tokStash.val = '\t';
        break;
      case 'v':
        dbg->tokStash.val = '\v';
        break;
      case '\\':
        dbg->tokStash.val = '\\';
        break;
      case '\'':
        dbg->tokStash.val = '\'';
        break;
      case '"':
        dbg->tokStash.val = '"';
        break;
      case '0':
        dbg->tokStash.val = '\0';
        break;
      default:
        dbg->tokStash.type = TOK_ERR;
        dbg->tokStash.len = 0;
        return dbg->tokStash;
      }
    default:
      dbg->tokStash.val = (Int)(unsigned char)(*str);
      break;
    }
    ++str;
    if (*str != '\'') {
      dbg->tokStash.type = TOK_ERR;
      dbg->tokStash.len = 0;
      return dbg->tokStash;
    }
    ++str;
    dbg->tokStash.type = TOK_NUM;
    dbg->tokStash.len = 3;
    return dbg->tokStash;
  }
  if (isdigit((unsigned char)*str) || (*str == '$') || (*str == '%')) {
    if (*str == '%') {
      ++str;
      if ((*str != '0') && (*str != '1')) {
        // edge case, this is a modulus
        dbg->tokStash.type = '%';
        dbg->tokStash.len = 1;
        return dbg->tokStash;
      }
      while ((*str == '0') || (*str == '1')) {
        ++str;
      }
      dbg->tokStash.val = strtol(dbg->tokStash.start + 1, NULL, 2);
    } else if (*str == '$') {
      ++str;
      while (isxdigit((unsigned char)*str)) {
        ++str;
      }
      dbg->tokStash.val = strtol(dbg->tokStash.start + 1, NULL, 16);
    } else {
      while (isdigit((unsigned char)*str)) {
        ++str;
      }
      dbg->tokStash.val = strtol(dbg->tokStash.start, NULL, 10);
    }
    dbg->tokStash.len = str - dbg->tokStash.start;
    dbg->tokStash.type = TOK_NUM;
    return dbg->tokStash;
  }
  if (isalpha((unsigned char)*str) || (*str == '_')) {
    ++str;
    while (isalnum((unsigned char)*str) || (*str == '_')) {
      ++str;
    }
    dbg->tokStash.len = str - dbg->tokStash.start;
    dbg->tokStash.type = TOK_ID;
    return dbg->tokStash;
  }
  for (UInt i = 0; i < (sizeof(DIGRAPHS) / sizeof(DIGRAPHS[0])); ++i) {
    char const *dg = DIGRAPHS[i].name;
    UInt len = strlen(dg);
    if (strncmp(str, dg, len) == 0) {
      dbg->tokStash.type = DIGRAPHS[i].type;
      dbg->tokStash.len = len;
      return dbg->tokStash;
    }
  }
  dbg->tokStash.type = (unsigned char)*str;
  dbg->tokStash.len = 1;
  return dbg->tokStash;
}

enum {
  EXPR_NUM,
  EXPR_ID,
  EXPR_OP,
};

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

static Int solve(Emu *emu) {
  for (UInt i = 0; i < emu->dbg.exprCount; ++i) {
    Expr const *expr = emu->dbg.exprStack + i;
    switch (expr->kind) {
    case EXPR_NUM:
      emu->dbg.intStack[emu->dbg.intCount++] = expr->tok.val;
      break;
    case EXPR_ID: {
      if (expr->tok.len == 1) {
        switch (tolower((unsigned char)expr->tok.start[0])) {
        case 'a':
          emu->dbg.intStack[emu->dbg.intCount++] = emu->cpu.a;
          break;
        case 'x':
          emu->dbg.intStack[emu->dbg.intCount++] = emu->cpu.x;
          break;
        case 'y':
          emu->dbg.intStack[emu->dbg.intCount++] = emu->cpu.y;
          break;
        default:
          break;
        }
      }
      if (expr->tok.len == 2) {
        if ((tolower((unsigned char)expr->tok.start[0]) == 'p') &&
            (tolower((unsigned char)expr->tok.start[1]) == 'c')) {
          emu->dbg.intStack[emu->dbg.intCount++] = emu->cpu.pc;
          break;
        }
        if ((tolower((unsigned char)expr->tok.start[0]) == 's') &&
            (tolower((unsigned char)expr->tok.start[1]) == 'p')) {
          emu->dbg.intStack[emu->dbg.intCount++] = 0x0100 | ((U16)emu->cpu.sp);
          break;
        }
      }
      Symbol const *sym = symFind(&emu->dbg, expr->tok.start, expr->tok.len);
      if (!sym) {
        return INT_MAX;
      }
      emu->dbg.intStack[emu->dbg.intCount++] = sym->val;
      break;
    }
    case EXPR_OP: {
      Int rhs = emu->dbg.intStack[--emu->dbg.intCount];
      if (expr->unary) {
        switch (expr->tok.type) {
        case '+':
          emu->dbg.intStack[emu->dbg.intCount++] = rhs;
          break;
        case '-':
          emu->dbg.intStack[emu->dbg.intCount++] = -rhs;
          break;
        case '!':
          emu->dbg.intStack[emu->dbg.intCount++] = !rhs;
          break;
        case '~':
          emu->dbg.intStack[emu->dbg.intCount++] = ~rhs;
          break;
        case '<':
          emu->dbg.intStack[emu->dbg.intCount++] = rhs & 0xFF;
          break;
        case '>':
          emu->dbg.intStack[emu->dbg.intCount++] = (((UInt)rhs) >> 8) & 0xFF;
          break;
        case '*':
          if ((rhs < 0) || (rhs > U16_MAX)) {
            return INT_MAX;
          }
          emu->dbg.intStack[emu->dbg.intCount++] = emuRead(emu, (U16)rhs);
          break;
        default:
          abort();
        }
        continue;
      }
      Int lhs = emu->dbg.intStack[--emu->dbg.intCount];
      switch (expr->tok.type) {
      case '+':
        emu->dbg.intStack[emu->dbg.intCount++] = lhs + rhs;
        break;
      case '-':
        emu->dbg.intStack[emu->dbg.intCount++] = lhs - rhs;
        break;
      case '*':
        emu->dbg.intStack[emu->dbg.intCount++] = lhs * rhs;
        break;
      case '/':
        if (rhs == 0) {
          return INT_MAX;
        }
        emu->dbg.intStack[emu->dbg.intCount++] = lhs / rhs;
        break;
      case '%':
        if (rhs == 0) {
          return INT_MAX;
        }
        emu->dbg.intStack[emu->dbg.intCount++] = lhs % rhs;
        break;
      case '<':
        emu->dbg.intStack[emu->dbg.intCount++] = lhs < rhs;
        break;
      case '>':
        emu->dbg.intStack[emu->dbg.intCount++] = lhs > rhs;
        break;
      case '&':
        emu->dbg.intStack[emu->dbg.intCount++] = lhs & rhs;
        break;
      case '|':
        emu->dbg.intStack[emu->dbg.intCount++] = lhs | rhs;
        break;
      case '^':
        emu->dbg.intStack[emu->dbg.intCount++] = lhs ^ rhs;
        break;
      case TOK_SRA:
        emu->dbg.intStack[emu->dbg.intCount++] = lhs >> rhs;
        break;
      case TOK_SRL:
        emu->dbg.intStack[emu->dbg.intCount++] = ((UInt)lhs) >> rhs;
        break;
      case TOK_SLL:
        emu->dbg.intStack[emu->dbg.intCount++] = lhs << rhs;
        break;
      case TOK_AND:
        emu->dbg.intStack[emu->dbg.intCount++] = lhs && rhs;
        break;
      case TOK_OR:
        emu->dbg.intStack[emu->dbg.intCount++] = lhs || rhs;
        break;
      case TOK_LTE:
        emu->dbg.intStack[emu->dbg.intCount++] = lhs <= rhs;
        break;
      case TOK_GTE:
        emu->dbg.intStack[emu->dbg.intCount++] = lhs >= rhs;
        break;
      case TOK_EQ:
        emu->dbg.intStack[emu->dbg.intCount++] = lhs == rhs;
        break;
      case TOK_NEQ:
        emu->dbg.intStack[emu->dbg.intCount++] = lhs != rhs;
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
  if (emu->dbg.intCount != 1) {
    return INT_MAX;
  }
  return emu->dbg.intStack[0];
}

static void pushOp(Dbg *dbg, Tok tok, Bool unary) {
  if (tok.type == '(') {
    dbg->opStack[dbg->opCount++] = (Expr){EXPR_OP, tok, TRUE};
    return;
  }
  while (dbg->opCount > 0) {
    Expr top = dbg->opStack[--dbg->opCount];
    if ((top.tok.type == '(') ||
        (tokPrec(top.tok, top.unary) >= tokPrec(tok, unary))) {
      dbg->opStack[dbg->opCount++] = top;
      break;
    }
    dbg->exprStack[dbg->exprCount++] = top;
  }
  dbg->opStack[dbg->opCount++] = (Expr){EXPR_OP, tok, unary};
}

static Int parseExpr(Emu *emu) {
  emu->dbg.opCount = 0;
  emu->dbg.exprCount = 0;
  emu->dbg.intCount = 0;
  Bool expectOp = FALSE;
  UInt parenDepth = 0;
  while (TRUE) {
    Tok tok = tokPeek(&emu->dbg, NULL);
    switch (tok.type) {
    case '+':
    case '-':
    case '<':
    case '>':
    case '*':
      // sometimes unary
      pushOp(&emu->dbg, tok, !expectOp);
      tokEat(&emu->dbg);
      expectOp = FALSE;
      continue;
    case '!':
    case '~':
      // always unary
      pushOp(&emu->dbg, tok, TRUE);
      tokEat(&emu->dbg);
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
      pushOp(&emu->dbg, tok, FALSE);
      tokEat(&emu->dbg);
      expectOp = FALSE;
      continue;
    case TOK_NUM:
      if (expectOp) {
        return INT_MAX;
      }
      emu->dbg.exprStack[emu->dbg.exprCount++] = (Expr){EXPR_NUM, tok, FALSE};
      tokEat(&emu->dbg);
      expectOp = TRUE;
      continue;
    case TOK_ID:
      if (expectOp) {
        return INT_MAX;
      }
      emu->dbg.exprStack[emu->dbg.exprCount++] = (Expr){EXPR_ID, tok, FALSE};
      tokEat(&emu->dbg);
      expectOp = TRUE;
      continue;
    case '(':
      if (expectOp) {
        return INT_MAX;
      }
      pushOp(&emu->dbg, tok, TRUE);
      tokEat(&emu->dbg);
      ++parenDepth;
      expectOp = FALSE;
      continue;
    case ')':
      if (!expectOp) {
        return INT_MAX;
      }
      --parenDepth;
      while (TRUE) {
        if (emu->dbg.opCount == 0) {
          return INT_MAX;
        }
        Expr top = emu->dbg.opStack[--emu->dbg.opCount];
        if (top.tok.type == '(') {
          break;
        }
        emu->dbg.exprStack[emu->dbg.exprCount++] = top;
      }
      tokEat(&emu->dbg);
      continue;
    default:
      if (!expectOp) {
        return INT_MAX;
      }
      if (parenDepth > 0) {
        return INT_MAX;
      }
      while (emu->dbg.opCount > 0) {
        emu->dbg.exprStack[emu->dbg.exprCount++] =
            emu->dbg.opStack[--emu->dbg.opCount];
      }
      return solve(emu);
    }
  }
}

static void dbgRegs(Emu *emu) {
  fprintf(stderr, "PC:%04X SP:01%02X A:%02X X:%02X Y:%02X P:%02X |",
          emu->cpu.pc, emu->cpu.sp, emu->cpu.a, emu->cpu.x, emu->cpu.y,
          emu->cpu.p);
  fprintf(stderr, "%c%c%c%c%c%c%c%c|\n",
          (emu->cpu.p & CPU_FLAG_NEGATIVE) ? 'N' : '.',
          (emu->cpu.p & CPU_FLAG_OVERFLOW) ? 'V' : '.',
          (emu->cpu.p & CPU_FLAG_UNUSED) ? '1' : '.',
          (emu->cpu.p & CPU_FLAG_BREAK) ? 'B' : '.',
          (emu->cpu.p & CPU_FLAG_DECIMAL) ? 'D' : '.',
          (emu->cpu.p & CPU_FLAG_INTERRUPT) ? 'I' : '.',
          (emu->cpu.p & CPU_FLAG_ZERO) ? 'Z' : '.',
          (emu->cpu.p & CPU_FLAG_CARRY) ? 'C' : '.');
}

typedef enum { DBG_DEBUG, DBG_BREAK, DBG_CONTINUE, DBG_CLEAR } DbgResult;

static DbgResult dbgQuit(Emu *emu) { exit(EXIT_SUCCESS); }

static DbgResult dbgCont(Emu *emu) { return DBG_CONTINUE; }

static DbgResult dbgStep(Emu *emu) { return DBG_BREAK; }

static DbgResult dbgClear(Emu *emu) { return DBG_CLEAR; }

static DbgResult dbgNext(Emu *emu) {
  U8 op = emuRead(emu, emu->cpu.pc);
  if (op == 0x20) { // JSR
    emu->dbg.nextpoint.addr = emu->cpu.pc + 3;
    emu->dbg.nextpoint.next = emu->dbg.bpHead; // to mark it active
    return DBG_CONTINUE;
  }
  return DBG_BREAK;
}

static DbgResult dbgRegsCmd(Emu *emu) {
  dbgRegs(emu);
  return DBG_DEBUG;
}

static DbgResult dbgBreak(Emu *emu) {
  Tok tok = tokPeek(&emu->dbg, NULL);
  if (tok.type == TOK_EOE) {
    fprintf(stderr, "No address provided for breakpoint\n");
    return DBG_DEBUG;
  }
  Int addr = parseExpr(emu);
  if ((addr == INT_MAX) || (addr > U16_MAX)) {
    Tok end = tokPeek(&emu->dbg, NULL);
    UInt len = end.start + end.len - tok.start;
    fprintf(stderr, "Invalid address for breakpoint: \"%.*s\"\n", (int)len,
            tok.start);
    return DBG_DEBUG;
  }
  Breakpoint *bp = malloc(sizeof(Breakpoint));
  bp->num = ++emu->dbg.bpCount;
  bp->addr = (U16)addr;
  bp->next = emu->dbg.bpHead;
  emu->dbg.bpHead = bp;
  fprintf(stderr, "Breakpoint %u set at $%04X\n", bp->num, bp->addr);
  return DBG_DEBUG;
}

static DbgResult dbgDel(Emu *emu) {
  Tok tok = tokPeek(&emu->dbg, NULL);
  if (tok.type == TOK_EOE) {
    fprintf(stderr, "No breakpoint number provided for deletion\n");
    return DBG_DEBUG;
  }
  Int num = parseExpr(emu);
  if ((num == INT_MAX) || (num > U16_MAX)) {
    Tok end = tokPeek(&emu->dbg, NULL);
    UInt len = end.start + end.len - tok.start;
    fprintf(stderr, "Invalid breakpoint number: \"%.*s\"\n", (int)len,
            tok.start);
    return DBG_DEBUG;
  }
  Breakpoint **prev = &emu->dbg.bpHead;
  Breakpoint *bp = emu->dbg.bpHead;
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

static DbgResult dbgExa(Emu *emu) {
  Int start = emu->cpu.pc;
  Int len = 16;
  Tok tok = tokPeek(&emu->dbg, NULL);
  if (tok.type != TOK_EOE) {
    start = parseExpr(emu);
    if ((start == INT_MAX) || (start > U16_MAX)) {
      Tok end = tokPeek(&emu->dbg, NULL);
      UInt len = end.start + end.len - tok.start;
      fprintf(stderr, "Invalid address for examine: \"%.*s\"\n", (int)len,
              tok.start);
      return DBG_DEBUG;
    }
  }
  tok = tokPeek(&emu->dbg, NULL);
  if (tok.type != TOK_EOE) {
    if (tok.type != ',') {
      fprintf(stderr, "Expected ',' after address\n");
      return DBG_DEBUG;
    }
    tok = tokPeek(&emu->dbg, NULL);
    if (tok.type == TOK_EOE) {
      fprintf(stderr, "No length provided for disasm\n");
      return DBG_DEBUG;
    }
    tokEat(&emu->dbg);
    len = parseExpr(emu);
    if ((len == INT_MAX) || (len <= 0)) {
      Tok end = tokPeek(&emu->dbg, NULL);
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
              emuRead(emu, (U16)(start + i)));
    }
    fprintf(stderr, "  |");
    for (UInt i = 0; i < 16; ++i) {
      if ((start + i) > end) {
        fprintf(stderr, " ");
      } else {
        U8 byte = emuRead(emu, (U16)(start + i));
        fprintf(stderr, "%c", isprint(byte) ? (char)byte : '.');
      }
    }
    fprintf(stderr, "|\n");
    start += 16;
  }
  return DBG_DEBUG;
}

static DbgResult dbgDis(Emu *emu) {
  Int start = emu->cpu.pc;
  Int len = 1;
  Tok tok = tokPeek(&emu->dbg, NULL);
  if (tok.type != TOK_EOE) {
    start = parseExpr(emu);
    if ((start == INT_MAX) || (start > U16_MAX)) {
      Tok end = tokPeek(&emu->dbg, NULL);
      UInt len = end.start + end.len - tok.start;
      fprintf(stderr, "Invalid address for disasm: \"%.*s\"\n", (int)len,
              tok.start);
      return DBG_DEBUG;
    }
  }
  tok = tokPeek(&emu->dbg, NULL);
  if (tok.type != TOK_EOE) {
    if (tok.type != ',') {
      fprintf(stderr, "Expected ',' after address\n");
      return DBG_DEBUG;
    }
    tokEat(&emu->dbg);
    tok = tokPeek(&emu->dbg, NULL);
    if (tok.type == TOK_EOE) {
      fprintf(stderr, "No length provided for disasm\n");
      return DBG_DEBUG;
    }
    len = parseExpr(emu);
    if ((len == INT_MAX) || (len <= 0)) {
      Tok end = tokPeek(&emu->dbg, NULL);
      UInt len = end.start + end.len - tok.start;
      fprintf(stderr, "Invalid length for disasm: \"%.*s\"\n", (int)len,
              tok.start);
      return DBG_DEBUG;
    }
  }
  U16 addr = (U16)start;
  Int count = 0;
  while ((count < len) && (addr <= U16_MAX)) {
    addr = disAsm(emu, addr);
    ++count;
  }
  return DBG_DEBUG;
}

static DbgResult dbgHelp(Emu *emu);

typedef struct {
  char const *breif;
  char const *usage;
  char const *body;
} DbgHelp;

typedef struct {
  char const **names;
  DbgResult (*fn)(Emu *);
  DbgHelp help;
} DbgCmd;

static DbgHelp const HELP_QUIT = {
    "Quit the debugger", "quit",
    "Exit the debugger and terminate the program."};
static DbgHelp const HELP_CONT = {"Continue execution", "continue",
                                  "Resume execution of the program "
                                  "until the next breakpoint or termination."};
static DbgHelp const HELP_STEP = {
    "Step into the next instruction", "step",
    "Execute the next instruction and return to the debugger."};
static DbgHelp const HELP_NEXT = {
    "Step over the next instruction", "next",
    "Execute the next instruction, stepping over function "
    "calls, and return to the debugger."};
static DbgHelp const HELP_BREAK = {
    "Set a breakpoint", "break <address>",
    "Set a breakpoint at the specified address. The "
    "program will pause execution when it reaches this address."};
static DbgHelp const HELP_DELETE = {"Delete a breakpoint",
                                    "delete <breakpoint_number>",
                                    "Remove the "
                                    "specified breakpoint from the debugger."};
static DbgHelp const HELP_REGS = {
    "Display CPU registers", "registers",
    "Show the current values of the CPU registers."};
static DbgHelp const HELP_EXA = {
    "Examine memory", "examine [<address>[, <length>]]",
    "Display the "
    "contents of memory starting from the specified address (default: PC). "
    "Optionally, specify the number of bytes to display."};
static DbgHelp const HELP_DIS = {
    "Disassemble code", "disasm [<address>[, <length>]]",
    "Disassemble the code starting from "
    "the specified address (default: PC). Optionally, specify the number of "
    "instructions to "
    "disassemble."};
static DbgHelp const HELP_CLEAR = {"Clear screen", "clear",
                                   "Clear the debugger's output screen."};
static DbgHelp const HELP_HELP = {
    "Display help information", "help [<command>]",
    "Show a list of available commands or detailed "
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

static void dbgMatches(Dbg const *dbg, char const *prefix, UInt len) {
  Bool first = TRUE;
  for (DbgCmd const *cmd = DBG_TBL; cmd->names; ++cmd) {
    for (char const **name = cmd->names; *name; ++name) {
      if (strncmp(prefix, *name, len) == 0) {
        fprintf(stderr, "%s%s", first ? "" : ", ", *name);
        first = FALSE;
      }
    }
  }
  for (Symbol const *sym = dbg->symHead; sym; sym = sym->next) {
    if (strncmp(prefix, sym->name, len) == 0) {
      fprintf(stderr, "%s%s", first ? "" : ", ", sym->name);
      first = FALSE;
    }
  }
}

static unsigned char dbgCompl(Dbg const *dbg, EditLine *el, int ch) {
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
  for (Symbol const *sym = dbg->symHead; sym; sym = sym->next) {
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
    dbgMatches(dbg, str, len);
    fprintf(stderr, " ?\n");
    return CC_REDISPLAY;
  }
  return CC_REFRESH;
}

static DbgResult dbgHelp(Emu *emu) {
  Tok tok = tokPeek(&emu->dbg, NULL);
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
        fprintf(stderr, "Usage: %s\n\n", cmd->help.usage);
        fprintf(stderr, "%s\n", cmd->help.body);
        return DBG_DEBUG;
      }
    }
  }
  fprintf(stderr, "Unknown command: %.*s\n", (int)tok.len, tok.start);
  return DBG_DEBUG;
}

void dbgTick(Dbg *dbg, Emu *emu) {
  if (dbg->nextpoint.next && (emu->cpu.pc == dbg->nextpoint.addr)) {
    dbg->debug = TRUE;
    dbg->nextpoint.next = NULL;
  }
  for (Breakpoint const *bp = dbg->bpHead; bp; bp = bp->next) {
    if (emu->cpu.pc != bp->addr) {
      continue;
    }
    fprintf(stderr, "Hit breakpoint %u at $%04X\r\n", bp->num, bp->addr);
    dbg->debug = TRUE;
    break;
  }
  if (!dbg->debug) {
    return;
  }

  termRawModeOff();
  if (!dbg->el) {
    dbg->el = el_init("vm", stdin, stderr, stderr);
    el_set(dbg->el, EL_PROMPT, &dbgPrompt);
    el_set(dbg->el, EL_EDITOR, "emacs");
    el_set(dbg->el, EL_ADDFN, "ed-complete", "Complete command", dbgCompl);
    el_set(dbg->el, EL_BIND, "^I", "ed-complete", NULL);
    dbg->hist = history_init();
    history(dbg->hist, &dbg->ev, H_SETSIZE, 100);
    el_set(dbg->el, EL_HIST, history, dbg->hist);
  }
  dbgRegs(emu);
  disAsm(emu, emu->cpu.pc);
  while (TRUE) {
    free(dbg->workline);
    int count;
    char const *line = el_gets(dbg->el, &count);
    if (!line || (count <= 0)) {
      exit(EXIT_FAILURE);
    }
    dbg->workline = strdup(line);
    Tok tok = tokPeek(dbg, dbg->workline);
    if (tok.type == TOK_EOE) {
      if (dbg->prevline) {
        free(dbg->workline);
        dbg->workline = strdup(dbg->prevline);
        tok = tokPeek(dbg, dbg->workline);
      }
      if (tok.type == TOK_EOE) {
        continue;
      }
    } else {
      // save history if not the same as prev command
      if (!dbg->prevline || (strcmp(dbg->prevline, line) != 0)) {
        history(dbg->hist, &dbg->ev, H_ENTER, line);
      }
      free(dbg->prevline);
      dbg->prevline = strdup(line);
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
      dbgMatches(dbg, tok.start, tok.len);
      fprintf(stderr, ")\n");
      continue;
    }
    tokEat(dbg);
    DbgResult res = match->fn(emu);
    if (res == DBG_BREAK) {
      break;
    }
    if (res == DBG_CONTINUE) {
      dbg->debug = FALSE;
      termRawModeOn();
      return;
    }
    if (res == DBG_CLEAR) {
      el_push(dbg->el, "\x0C");
    }
  }
}
