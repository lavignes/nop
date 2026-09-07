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

FORMAT(1) NORETURN void fatal(char const *fmt, ...);
NORETURN void fatalV(char const *fmt, va_list args);

#ifdef __builtin_unreachable
#define UNREACHABLE() __builtin_unreachable()
#else
#define UNREACHABLE()                                                          \
  (fatal("%s:%d is not meant to be reachable\n", __FILE__, __LINE__))
#endif

#define TODO(msg)                                                              \
  (fatal("%s:%d: not implemented: %s\n", __FILE__, __LINE__, (msg)))

void strPush(char **dst, UInt *cap, char c);
void strCat(char **dst, UInt *cap, char const *src);

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

typedef struct {
  char *id;
  UInt line;
  UInt col;
} Loc;

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
  MacroTok *buf;
  UInt bufLen;
  UInt bufCap;
} MacroArg;

void argsEnqueue(MacroArg **args, UInt *len, UInt *cap, MacroArg arg);
void argsDequeue(MacroArg **args, UInt *len);

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
      UInt toksCap;
      UInt toksIdx;
      MacroArg *args;
      UInt argsLen;
      UInt argsCap;
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

void lexFileInit(Lex *lex, char *name, FILE *hnd);
void lexMacroInit(Lex *lex, char *name, MacroTok *toks, UInt toksLen,
                  MacroArg *args, UInt argsLen);
void lexIfElseInit(Lex *lex, LocTok *toks, UInt toksLen);

U8 lexPeek(Lex *lex);
void lexEat(Lex *lex);
void lexRewind(Lex *lex);

char const *lexTxt(Lex const *lex);
I32 lexNum(Lex const *lex);
Loc lexLoc(Lex const *lex);

#endif // ASM_H
