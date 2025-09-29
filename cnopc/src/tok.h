#ifndef TOK_H
#define TOK_H

#include <stdio.h>

#include "buf.h"
#include "fatal.h"

typedef struct {
    View path;
    UInt line;
    UInt col;
} Pos;

typedef enum {
    TOK_STREAM_FILE,
    TOK_STREAM_VIEW,
} TokStreamKind;

typedef enum {
    TOK_EOF     = 26,

    TOK_ID      = 0xF0000,
    TOK_INT     = 0xF0001,
    TOK_FLOAT   = 0xF0002,
    TOK_BOOL    = 0xF0003,
    TOK_STR     = 0xF0004,
    TOK_CHAR    = 0xF0005,
    TOK_NIL     = 0xF0006,
    TOK_NEVER   = 0xF0007,
    TOK_COMMENT = 0xF0008,

    TOK_PKG     = 0xF0100,
    TOK_FN      = 0xF0101,

    TOK_SHL     = 0xF1000, // <<
    TOK_SHR     = 0xF1001, // >>
    TOK_LTE     = 0xF1002, // <=
    TOK_GTE     = 0xF1003, // >=
    TOK_EQ      = 0xF1004, // ==
    TOK_NEQ     = 0xF1005, // !=
    TOK_AND     = 0xF1006, // &&
    TOK_OR      = 0xF1007, // ||
    TOK_ADD_EQ  = 0xF1008, // +=
    TOK_SUB_EQ  = 0xF1009, // -=
    TOK_MUL_EQ  = 0xF100A, // *=
    TOK_DIV_EQ  = 0xF100B, // /=
    TOK_MOD_EQ  = 0xF100C, // %=
    TOK_AND_EQ  = 0xF100D, // &=
    TOK_OR_EQ   = 0xF100E, // |=
    TOK_XOR_EQ  = 0xF100F, // ^=
} Tok;

typedef struct {
    TokStreamKind kind;
    Pos           pos;

    // TODO: consider adding a buffer for holding the raw
    // input which is useful for error reports and formatters.

    union {
        struct {
            union {
                FILE* hnd;
                View  view;
            };
            U32  stash;
            Bool stashed;
            U32  cstash;
            Bool cstashed;
            UInt cline;
            UInt ccol;
            U8   cbuf[4];
            UInt clen;
            Buf  buf;
            union {
                struct {
                    IntKind kind;
                    UInt    val;
                } num;

                struct {
                    FloatKind kind;
                    F64       val;
                } fnum;
            };
        } chardev;
    };
} TokStream;

void tokStreamInitFile(TokStream* ts, View path, FILE* hnd);
void tokStreamInitView(TokStream* ts, View path, View view);
void tokStreamFini(TokStream* ts);

FORMAT(2)
NORETURN void tokStreamFatal(TokStream const* ts, char const* fmt, ...);

FORMAT(3)
NORETURN
void tokStreamFatalPos(TokStream const* ts, Pos pos, char const* fmt, ...);

NORETURN void tokStreamFatalV(TokStream const* ts, char const* fmt,
                              va_list args);
NORETURN void tokStreamFatalPosV(TokStream const* ts, Pos pos, char const* fmt,
                                 va_list args);

U32  tokStreamPeek(TokStream* ts);
void tokStreamEat(TokStream* ts);

View tokStreamView(TokStream const* ts);
UInt tokStreamInt(TokStream const* ts, IntKind* kind);
F64  tokStreamFloat(TokStream const* ts, FloatKind* kind);
Bool tokStreamBool(TokStream const* ts);
U32  tokStreamChar(TokStream const* ts);
Pos  tokStreamPos(TokStream const* ts);

#endif // TOK_H
