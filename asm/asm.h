#ifndef ASM_H
#define ASM_H

#include <stdarg.h>
#include <stdio.h>

#include "abi.h"

#define FORMAT(n)
#ifdef __has_attribute
#if __has_attribute(format)
#undef FORMAT
#define FORMAT(n) __attribute__((format(printf, (n), (n + 1))))
#endif
#endif

#define NORETURN
#ifdef __has_attribute
#if __has_attribute(noreturn)
#undef NORETURN
#define NORETURN __attribute__((noreturn))
#endif
#endif

FORMAT(1) NORETURN void panic(char const *fmt, ...);
NORETURN void panicV(char const *fmt, va_list args);

#ifdef __builtin_unreachable
#define UNREACHABLE() __builtin_unreachable()
#else
#define UNREACHABLE()                                                          \
  (panic("%s:%d is not meant to be reachable\n", __FILE__, __LINE__))
#endif

#define TODO(msg)                                                              \
  (panic("%s:%d: not implemented: %s\n", __FILE__, __LINE__, (msg)))

void strCat(char **dst, UInt *cap, char const *src);

typedef struct {
  char *id;
  UInt line;
  UInt col;
} Loc;

typedef struct {
  char const *scope;
  char const *id;
} Label;

typedef struct {
  U8 tok;
  Bool unary;
} Op;

enum {
  EXPR_CONST,
  EXPR_ADDR,
  EXPR_OP,
  EXPR_LABEL,
};

typedef struct {
  U8 kind;
  union {
    I32 num;
    U16 addr;
    Op op;
    Label lbl;
  };
} Expr;

void exprCat(Expr **exprs, UInt *len, UInt *cap, Expr expr);
Expr *exprEat(UInt *len, UInt *cap);
Expr *exprEatLoc(UInt *len, UInt *cap, Loc *loc);
I32 exprEatSolvedLoc(Loc *loc);

Bool exprSolve(Expr const *exprs, UInt len, I32 *num);
U8 exprEatSolvedU8();
U16 exprEatSolvedU16();

Bool exprCanReprU8(I32 num);
Bool exprCanReprI8(I32 num);
Bool exprCanReprU16(I32 num);

enum : U8 {
  TOK_EOF = 26,

  TOK_ID = 128,
  TOK_NUM,
  TOK_STR,

  TOK_DB,
  TOK_DW,
  TOK_DS,
  TOK_INCLUDE,
  TOK_INCBIN,
  TOK_IF,
  TOK_ELSE,
  TOK_END,
  TOK_MACRO,

  TOK_DEFINED,
  TOK_STRLEN,

  TOK_ASL, // <<
  TOK_ASR, // >>
  TOK_LSR, // ~>
  TOK_LTE, // <=
  TOK_GTE, // >=
  TOK_EQ,  // ==
  TOK_NEQ, // !=
  TOK_AND, // &&
  TOK_OR,  // ||

  TOK_A,
  TOK_X,
  TOK_Y,

  TOK_ARG,
  TOK_ARGC,
  TOK_SHIFT,
};

char const *tokName(U8 tok);

typedef struct {
  U8 tok;
  Loc loc;
  union {
    char *txt;
    I32 num;
  };
} LocTok;

enum {
  MACRO_TOK,
  MACRO_ID,
  MACRO_NUM,
  MACRO_STR,
  MACRO_ARG,
  MACRO_ARGC,
  MACRO_SHIFT,
};

typedef struct {
  U8 kind;
  Loc loc;
  union {
    U8 tok;
    char *txt;
    I32 num;
  };
} MacroTok;

typedef struct {
  char *name;
  Loc loc;
  MacroTok *toks;
  UInt toksLen;
} Macro;

typedef struct {
  MacroTok *buf;
  UInt bufLen;
  UInt bufCap;
} Arg;

void argsEnqueue(Arg **args, UInt *len, UInt *cap, Arg arg);
void argsDequeue(Arg **args, UInt *len);

enum {
  LEX_FILE,
  LEX_MACRO,
  LEX_IF_ELSE,
};

typedef struct {
  U8 kind;
  Loc loc;
  union {
    struct {
      FILE *hnd;
      U8 stash;
      U8 charStash;
      UInt charLine;
      UInt charCol;
      char *txt;
      UInt txtCap;
      I32 num;
    } file;

    struct {
      char *name;
      MacroTok *toks;
      UInt toksLen;
      UInt toksIdx;
      Arg *args;
      UInt argsLen;
      UInt argsIdx;
    } macro;

    struct {
      LocTok *toks;
      UInt toksLen;
      UInt toksCap;
      UInt toksIdx;
    } ifElse;
  };
} Lex;

void lexFileInit(Lex *lex, char const *name, FILE *hnd);
void lexMacroInit(Lex *lex, char const *name, MacroTok *toks, UInt toksLen,
                  Arg *args, UInt argsLen);
void lexIfElseInit(Lex *lex, LocTok *toks, UInt toksLen);

FORMAT(2) NORETURN void lexFatal(Lex const *lex, char const *fmt, ...);
FORMAT(3)
NORETURN void lexFatalLoc(Lex const *lex, Loc loc, char const *fmt, ...);

U8 lexPeek(Lex *lex);
void lexEat(Lex *lex);
void lexRewind(Lex *lex);

char const *lexTxt(Lex const *lex);
I32 lexNum(Lex const *lex);
Loc lexLoc(Lex const *lex);
Label lexLabel(Lex const *lex);

FORMAT(1) NORETURN void fatal(char const *fmt, ...);
FORMAT(2) NORETURN void fatalLoc(Loc loc, char const *fmt, ...);

U8 peek();
void eat();
void expect(U8 tok);

U16 getPC();

#endif // ASM_H
