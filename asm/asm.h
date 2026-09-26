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

FORMAT(1) void warn(char const *fmt, ...);
void warnV(char const *fmt, va_list args);

#ifdef __builtin_unreachable
#define UNREACHABLE() __builtin_unreachable()
#else
#define UNREACHABLE()                                                          \
  (panic("%s:%d is not meant to be reachable\n", __FILE__, __LINE__))
#endif

#define TODO(fmt, ...)                                                         \
  (panic("%s:%d: TODO: " fmt, __FILE__, __LINE__, ##__VA_ARGS__))

void strCat(char **dst, UInt *cap, char const *src);

typedef struct {
  char const *name;
  UInt line;
  UInt col;
} Loc;

typedef struct {
  U8 tok;
  Bool unary;
} Op;

enum {
  EXPR_CONST,
  EXPR_LABEL,
  EXPR_OP,
};

typedef struct {
  U8 kind;
  union {
    I32 num;
    U16 addr;
    Op op;
    char const *lbl;
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

typedef struct {
  char const *lbl;
  Expr *exprs;
  UInt exprsLen;
  Loc loc;
} Sym;

Sym *symCat(Sym **syms, UInt *len, UInt *cap, Sym sym);
Sym *symFind(Sym *syms, UInt len, char const *lbl);

enum : U8 {
  TOK_EOF = 26,

  TOK_ID = 128,
  TOK_NUM,
  TOK_STR,

  TOK_CPU,
  TOK_DB,
  TOK_DW,
  TOK_DS,
  TOK_INCLUDE,
  TOK_INCBIN,
  TOK_IF,
  TOK_ELSE,
  TOK_END,
  TOK_MACRO,
  TOK_REPEAT,
  TOK_STRFMT,
  TOK_IDFMT,

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
  REPEAT_TOK,
  REPEAT_ID,
  REPEAT_NUM,
  REPEAT_STR,
  REPEAT_IDX,
};

typedef struct {
  U8 kind;
  Loc loc;
  union {
    U8 tok;
    char *txt;
    I32 num;
  };
} RepeatTok;

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
  LEX_REPEAT,
  LEX_FMT,
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
      RepeatTok *toks;
      UInt toksLen;
      UInt toksIdx;
      UInt idx;
      UInt cnt;
    } repeat;

    struct {
      U8 tok;
      char const *fmt;
    } fmt;

    struct {
      LocTok *toks;
      UInt toksLen;
      UInt toksCap;
      UInt toksIdx;
    } ifElse;
  };
} Lex;

void lexFileInit(Lex *lex, char const *name, FILE *hnd);
void lexMacroInit(Lex *lex, Loc loc, char const *name, MacroTok *toks,
                  UInt toksLen, Arg *args, UInt argsLen);
void lexRepeatInit(Lex *lex, Loc loc, RepeatTok *toks, UInt toksLen, UInt cnt);
void lexFmtInit(Lex *lex, Loc loc, U8 tok, char const *fmt);
void lexIfElseInit(Lex *lex, Loc loc, LocTok *toks, UInt toksLen);

FORMAT(2) NORETURN void lexFatal(Lex const *lex, char const *fmt, ...);
NORETURN void lexFatalV(Lex const *lex, char const *fmt, va_list args);
FORMAT(3)
NORETURN void lexFatalLoc(Lex const *lex, Loc loc, char const *fmt, ...);
NORETURN void lexFatalLocV(Lex const *lex, Loc loc, char const *fmt,
                           va_list args);

FORMAT(2) void lexWarn(Lex const *lex, char const *fmt, ...);
void lexWarnV(Lex const *lex, char const *fmt, va_list args);
FORMAT(3)
void lexWarnLoc(Lex const *lex, Loc loc, char const *fmt, ...);
void lexWarnLocV(Lex const *lex, Loc loc, char const *fmt, va_list args);

U8 lexPeek(Lex *lex);
void lexEat(Lex *lex);
void lexRewind(Lex *lex);

char const *lexTxt(Lex const *lex);
I32 lexNum(Lex const *lex);
Loc lexLoc(Lex const *lex);
char const *lexLabel(Lex const *lex);

FORMAT(1) NORETURN void fatal(char const *fmt, ...);
FORMAT(2) NORETURN void fatalLoc(Loc loc, char const *fmt, ...);

U8 peek();
void eat();
void expect(U8 tok);

char const *intern(char const *str);

Lex *getLex();
char const *getScope();
U16 getPC();

Sym *addSym(char const *lbl, Sym sym);
Sym *findSym(char const *lbl);

#endif // ASM_H
