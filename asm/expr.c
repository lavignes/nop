#include <stdlib.h>

#include "asm.h"

void exprCat(Expr **exprs, UInt *len, UInt *cap, Expr expr) {
  if (!*exprs) {
    *len = 0;
    *cap = 8;
    *exprs = malloc(sizeof(Expr) * *cap);
  }
  if (*len == *cap) {
    *cap *= 2;
    *exprs = realloc(*exprs, sizeof(Expr) * *cap);
  }
  (*exprs)[*len] = expr;
  ++*len;
}

#define OP_STACK_CAP 64
static Op opStack[OP_STACK_CAP];
static UInt opStackLen;

static void pushOp(Op op) {
  if (opStackLen == OP_STACK_CAP) {
    fatal("Operator stack overflow\n");
  }
  opStack[opStackLen] = op;
  ++opStackLen;
}

static Op popOp() {
  if (!opStackLen) {
    fatal("Operator stack underflow\n");
  }
  --opStackLen;
  return opStack[opStackLen];
}

static U8 precedence(Op op) {
  if (op.unary) {
    return 0;
  }
  switch (op.tok) {
  case '/':
  case '%':
  case '*':
    return 1;
  case '+':
  case '-':
    return 2;
  case TOK_ASL:
  case TOK_ASR:
  case TOK_LSR:
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
    UNREACHABLE();
  }
}

static void pushApply(Expr **exprs, UInt *len, UInt *cap, Op op) {
  if (op.tok == '(') {
    pushOp(op);
    return;
  }
  while (opStackLen) {
    Op top = popOp();
    if ((top.tok == '(') || (precedence(top) >= precedence(op))) {
      pushOp(top);
      break;
    }
    exprCat(exprs, len, cap, (Expr){.kind = EXPR_OP, .op = top});
  }
  pushOp(op);
}

static void pushApplyBinary(U32 tok) { pushApply((Op){tok, false}); }

static void pushApplyUnary(U32 tok) { pushApply((Op){tok, true}); }

Expr *exprEat(UInt *len, UInt *cap) {
  opStackLen = 0;
  Expr *exprs = NULL;
  Bool seenVal = FALSE;
  UInt parenDepth = 0;
  while (TRUE) {
    switch (peek()) {
    case '*':
      if (!seenVal) {
        exprCat(&exprs, len, cap, (Expr){.kind = EXPR_CONST, .num = getPC()});
        seenVal = TRUE;
        continue;
      }
      pushApplyBinary('*');
      seenVal = FALSE;
      continue;
    case '+':
    case '-':
    case '<':
    case '>':
      // sometimes unary
      if (seenVal) {
        pushApplyBinary(peek());
      } else {
        pushApplyUnary(peek());
      }
      eat();
      seenVal = FALSE;
      continue;
    case '!':
    case '~':
      // always unary
      pushApplyUnary(peek());
      eat();
      seenVal = FALSE;
      continue;
    case '&':
    case TOK_AND:
    case '|':
    case TOK_OR:
    case '/':
    case '%':
    case TOK_ASL:
    case TOK_ASR:
    case TOK_LSR:
    case TOK_LTE:
    case TOK_GTE:
    case TOK_EQ:
    case TOK_NEQ:
      // always binary
      if (!seenVal) {
        fatal("Expected a value\n");
      }
      pushApplyBinary(peek());
      eat();
      seenVal = FALSE;
      continue;
    case TOK_NUM:
      if (!seenVal) {
        fatal("Expected an operator\n");
      }
      exprCat(&exprs, len, cap, (Expr){.kind = EXPR_CONST, .num = lexNum(ls)});
      eat();
      seenVal = TRUE;
      continue;
    case '(':
      if (seenVal) {
        fatal("Expected an operator\n");
      }
      ++parenDepth;
      pushApply((Op){'(', TRUE});
      eat();
      seenVal = FALSE;
      continue;
    case ')':
      if (!seenVal) {
        fatal("Expected a value\n");
      }
      --parenDepth;
      while (TRUE) {
        if (!opStackLen) {
          fatal("Mismatched parentheses\n");
        }
        Op top = popOp();
        if (top.tok == '(') {
          break;
        }
        exprCat(&exprs, len, cap, (Expr){.kind = EXPR_OP, .op = top});
      }
      eat();
      seenVal = TRUE;
      continue;
    case TOK_ID:
      if (!seenVal) {
        fatal("Expected an operator\n");
      }
      exprCat(&exprs, len, cap, (Expr){.kind = EXPR_LABEL, .lbl = lexTxt(ls)});
      eat();
      seenVal = TRUE;
      continue;
    }
  }
}

Expr *exprEatLoc(UInt *len, UInt *cap, Loc *loc);
I32 exprEatSolvedLoc(Loc *loc);

Bool exprSolve(Expr const *exprs, UInt len, I32 *num);
U8 exprEatSolvedU8();
U16 exprEatSolvedU16();

Bool exprCanReprU8(I32 num) { return (num >= 0) && (num <= U8_MAX); }

Bool exprCanReprI8(I32 num) { return (num >= I8_MIN) && (num <= I8_MAX); }

Bool exprCanReprU16(I32 num) { return (num >= 0) && (num <= U16_MAX); }
