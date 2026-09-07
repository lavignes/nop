#ifndef ASM_H
#define ASM_H

#include <stdio.h>

#include "abi.h"

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

enum {
  STREAM_FILE,
  STREAM_MACRO,
  STREAM_IFELSE,
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
      char *buf;
      UInt bufLen;
      UInt bufCap;
      I32 num;
    } file;

    struct {
      char *name;
    } macro;

    struct {
      LocTok buf;
      UInt bufLen;
      UInt bufCap;
      UInt idx;
    } ifelse;
  };
} Stream;

#endif // ASM_H
