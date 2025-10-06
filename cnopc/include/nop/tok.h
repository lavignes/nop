#ifndef TOK_H
#define TOK_H

#include <stdio.h>

#include <nop/buf.h>
#include <nop/fatal.h>

typedef struct {
    View path;
    UInt line;
    UInt col;
} Pos;

typedef enum {
    TOKS_FILE,
    TOKS_VIEW,
} ToksKind;

enum {
    TOK_EOF     = 26,

    TOK_IDENT   = 0xF0000,
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

    TOK_ARROW   = 0xF1010, // ->
};

void tokName(U32 tok, Buf* buf);

typedef struct {
    ToksKind kind;
    Pos      pos;

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
                    UInt    base;
                } num;

                struct {
                    FloatKind kind;
                    F64       val;
                } fnum;
            };
        } bstream;
    };
} Toks;

void toksInitFile(Toks* ts, View path, FILE* hnd);
void toksInitView(Toks* ts, View path, View view);
void toksFini(Toks* ts);

FORMAT(2)
NORETURN void toksFatal(Toks const* ts, char const* fmt, ...);

FORMAT(3)
NORETURN
void toksFatalPos(Toks const* ts, Pos pos, char const* fmt, ...);

NORETURN void toksFatalV(Toks const* ts, char const* fmt, va_list args);
NORETURN void toksFatalPosV(Toks const* ts, Pos pos, char const* fmt,
                            va_list args);

U32  toksPeek(Toks* ts);
void toksEat(Toks* ts);

View toksView(Toks const* ts);
UInt toksInt(Toks const* ts, IntKind* kind, UInt* base);
F64  toksFloat(Toks const* ts, FloatKind* kind);
Bool toksBool(Toks const* ts);
U32  toksChar(Toks const* ts);
Pos  toksPos(Toks const* ts);

#endif // TOK_H
