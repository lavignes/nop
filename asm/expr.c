#include <stdlib.h>
#include <string.h>

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

static void pushApplyBinary(Expr **exprs, UInt *len, UInt *cap, U32 tok) {
  pushApply(exprs, len, cap, (Op){tok, false});
}

static void pushApplyUnary(Expr **exprs, UInt *len, UInt *cap, U32 tok) {
  pushApply(exprs, len, cap, (Op){tok, true});
}

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
        eat();
        seenVal = TRUE;
        continue;
      }
      pushApplyBinary(&exprs, len, cap, '*');
      eat();
      seenVal = FALSE;
      continue;
    case '+':
    case '-':
    case '<':
    case '>':
      // sometimes unary
      if (seenVal) {
        pushApplyBinary(&exprs, len, cap, peek());
      } else {
        pushApplyUnary(&exprs, len, cap, peek());
      }
      eat();
      seenVal = FALSE;
      continue;
    case '!':
    case '~':
      // always unary
      pushApplyUnary(&exprs, len, cap, peek());
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
      pushApplyBinary(&exprs, len, cap, peek());
      eat();
      seenVal = FALSE;
      continue;
    case TOK_NUM:
      if (seenVal) {
        fatal("Expected an operator\n");
      }
      exprCat(&exprs, len, cap,
              (Expr){.kind = EXPR_CONST, .num = lexNum(getLex())});
      eat();
      seenVal = TRUE;
      continue;
    case '(':
      if (seenVal) {
        fatal("Expected an operator\n");
      }
      ++parenDepth;
      pushApply(&exprs, len, cap, (Op){'(', TRUE});
      eat();
      seenVal = FALSE;
      continue;
    case ')':
      if (!seenVal) {
        fatal("Expected a value\n");
      }
      if (parenDepth == 0) {
        goto complete;
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
      if (seenVal) {
        fatal("Expected an operator\n");
      }
      exprCat(&exprs, len, cap,
              (Expr){.kind = EXPR_LABEL, .lbl = lexLabel(getLex())});
      eat();
      seenVal = TRUE;
      continue;
    case TOK_DEFINED: {
      if (seenVal) {
        fatal("Expected an operator\n");
      }
      eat();
      expect(TOK_ID);
      char const *lbl = lexLabel(getLex());
      exprCat(&exprs, len, cap,
              (Expr){.kind = EXPR_CONST, .num = findSym(lbl) != NULL});
      eat();
      seenVal = TRUE;
      continue;
    }
    case TOK_STRLEN:
      if (seenVal) {
        fatal("Expected an operator\n");
      }
      eat();
      expect(TOK_STR);
      exprCat(&exprs, len, cap,
              (Expr){.kind = EXPR_CONST, .num = strlen(lexTxt(getLex()))});
      eat();
      seenVal = TRUE;
      continue;
    default:
      if (!seenVal) {
        fatal("Expected a value\n");
      }
      if (parenDepth) {
        fatal("Mismatched parentheses\n");
      }
      goto complete;
    }
  }
complete:
  while (opStackLen) {
    Op top = popOp();
    exprCat(&exprs, len, cap, (Expr){.kind = EXPR_OP, .op = top});
  }
  return exprs;
}

Expr *exprEatLoc(UInt *len, UInt *cap, Loc *loc) {
  peek();
  *loc = lexLoc(getLex());
  return exprEat(len, cap);
}

I32 exprEatSolvedLoc(Loc *loc) {
  UInt len;
  UInt cap;
  Expr *exprs = exprEatLoc(&len, &cap, loc);
  I32 num;
  if (!exprSolve(exprs, len, &num)) {
    fatalLoc(*loc, "Expression must be constant\n");
  }
  free(exprs);
  return num;
}

static void numPush(I32 **stack, UInt *len, UInt *cap, I32 num) {
  if (!*stack) {
    *len = 0;
    *cap = 16;
    *stack = malloc(sizeof(I32) * *cap);
  }
  if (*len == *cap) {
    *cap *= 2;
    *stack = realloc(*stack, sizeof(I32) * *cap);
  }
  (*stack)[*len] = num;
  ++*len;
}

static I32 numPop(I32 **stack, UInt *len) {
  if (!*len) {
    panic("Number stack underflow\n");
  }
  --*len;
  return (*stack)[*len];
}

Bool exprSolve(Expr const *exprs, UInt len, I32 *num) {
  I32 *stack = NULL;
  UInt stackLen = 0;
  UInt stackCap = 0;
  for (UInt i = 0; i < len; ++i) {
    Expr const *expr = exprs + i;
    switch (expr->kind) {
    case EXPR_CONST:
      numPush(&stack, &stackLen, &stackCap, expr->num);
      break;
    case EXPR_LABEL: {
      Sym *sym = findSym(expr->lbl);
      if (!sym) {
        goto fail;
      }
      I32 num;
      // yuck
      if (!exprSolve(sym->exprs, sym->exprsLen, &num)) {
        goto fail;
      }
      numPush(&stack, &stackLen, &stackCap, num);
      break;
    }
    case EXPR_OP: {
      I32 rhs = numPop(&stack, &stackLen);
      if (expr->op.unary) {
        switch (expr->op.tok) {
        case '+':
          numPush(&stack, &stackLen, &stackCap, rhs);
          break;
        case '-':
          numPush(&stack, &stackLen, &stackCap, -rhs);
          break;
        case '~':
          numPush(&stack, &stackLen, &stackCap, ~rhs);
          break;
        case '!':
          numPush(&stack, &stackLen, &stackCap, !rhs);
          break;
        case '<':
          numPush(&stack, &stackLen, &stackCap, rhs & 0xFF);
          break;
        case '>':
          numPush(&stack, &stackLen, &stackCap, (rhs >> 8) & 0xFF);
          break;
        default:
          UNREACHABLE();
        }
      } else {
        I32 lhs = numPop(&stack, &stackLen);
        switch (expr->op.tok) {
        case '+':
          numPush(&stack, &stackLen, &stackCap, lhs + rhs);
          break;
        case '-':
          numPush(&stack, &stackLen, &stackCap, lhs - rhs);
          break;
        case '*':
          numPush(&stack, &stackLen, &stackCap, lhs * rhs);
          break;
        case '/':
          numPush(&stack, &stackLen, &stackCap, lhs / rhs);
          break;
        case '%':
          numPush(&stack, &stackLen, &stackCap, lhs % rhs);
          break;
        case TOK_ASL:
          numPush(&stack, &stackLen, &stackCap, lhs << rhs);
          break;
        case TOK_ASR:
          numPush(&stack, &stackLen, &stackCap, lhs >> rhs);
          break;
        case TOK_LSR:
          numPush(&stack, &stackLen, &stackCap, ((U32)lhs) >> ((U32)rhs));
          break;
        case '<':
          numPush(&stack, &stackLen, &stackCap, lhs < rhs);
          break;
        case TOK_LTE:
          numPush(&stack, &stackLen, &stackCap, lhs <= rhs);
          break;
        case '>':
          numPush(&stack, &stackLen, &stackCap, lhs > rhs);
          break;
        case TOK_GTE:
          numPush(&stack, &stackLen, &stackCap, lhs >= rhs);
          break;
        case TOK_EQ:
          numPush(&stack, &stackLen, &stackCap, lhs == rhs);
          break;
        case TOK_NEQ:
          numPush(&stack, &stackLen, &stackCap, lhs != rhs);
          break;
        case '&':
          numPush(&stack, &stackLen, &stackCap, lhs & rhs);
          break;
        case '|':
          numPush(&stack, &stackLen, &stackCap, lhs | rhs);
          break;
        case '^':
          numPush(&stack, &stackLen, &stackCap, lhs ^ rhs);
          break;
        case TOK_AND:
          numPush(&stack, &stackLen, &stackCap, lhs && rhs);
          break;
        case TOK_OR:
          numPush(&stack, &stackLen, &stackCap, lhs || rhs);
          break;
        default:
          UNREACHABLE();
        }
      }
      break;
    }
    default:
      UNREACHABLE();
    }
  }
  *num = numPop(&stack, &stackLen);
  if (stackLen != 0) {
    panic("Number stack not empty after evaluation\n");
  }
  free(stack);
  return TRUE;
fail:
  free(stack);
  return FALSE;
}

U8 exprEatSolvedU8() {
  Loc loc;
  I32 num = exprEatSolvedLoc(&loc);
  if (!exprCanReprU8(num)) {
    fatalLoc(loc, "Expression must fit in a byte: $%08X\n", num);
  }
  return (U8)num;
}

U16 exprEatSolvedU16() {
  Loc loc;
  I32 num = exprEatSolvedLoc(&loc);
  if (!exprCanReprU16(num)) {
    fatalLoc(loc, "Expression must fit in a word: $%08X\n", num);
  }
  return (U16)num;
}

Bool exprCanReprU8(I32 num) { return (num >= 0) && (num <= U8_MAX); }

Bool exprCanReprI8(I32 num) { return (num >= I8_MIN) && (num <= I8_MAX); }

Bool exprCanReprU16(I32 num) { return (num >= 0) && (num <= U16_MAX); }

Sym *symCat(Sym **syms, UInt *len, UInt *cap, Sym sym) {
  if (!*syms) {
    *len = 0;
    *cap = 16;
    *syms = malloc(sizeof(Sym) * *cap);
  }
  if (*len == *cap) {
    *cap *= 2;
    *syms = realloc(*syms, sizeof(Sym) * *cap);
  }
  (*syms)[*len] = sym;
  ++*len;
  return &(*syms)[*len - 1];
}

Sym *symFind(Sym *syms, UInt len, char const *lbl) {
  for (UInt i = 0; i < len; ++i) {
    if (!strcmp(syms[i].lbl, lbl)) {
      return &syms[i];
    }
  }
  return NULL;
}
