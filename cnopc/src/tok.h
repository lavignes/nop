#ifndef TOK_H
#define TOK_H

#include <stdio.h>

#include "buf.h"
#include "fatal.h"

typedef struct {
    View path;
    UInt line;
    UInt col;
} FilePos;

typedef enum {
    TOK_STREAM_FILE,
} TokStreamKind;

typedef enum {
    TOK_EOF      = 26,

    TOK_ID       = 0xF0000,
    TOK_NUM      = 0xF0001,
    TOK_STR      = 0xF0002,

    TOK_IF       = 0xF0100,
    TOK_ELSE     = 0xF0101,
    TOK_FN       = 0xF0102,
    TOK_LET      = 0xF0103,
    TOK_RETURN   = 0xF0104,
    TOK_WHILE    = 0xF0105,
    TOK_FOR      = 0xF0106,
    TOK_IN       = 0xF0107,
    TOK_BREAK    = 0xF0108,
    TOK_CONTINUE = 0xF0109,
    TOK_STRUCT   = 0xF010A,
    TOK_ENUM     = 0xF010B,
    TOK_UNION    = 0xF010C,

} Tok;

typedef struct {
    TokStreamKind kind;
    FilePos       pos;

    union {
        struct {
            FILE* hnd;

            U32  stash;
            Bool stashed;

            U32  cstash;
            Bool cstashed;
            UInt cline;
            UInt ccol;
            U8   cbuf[4];
            UInt clen;

            Buf buf;
            Num num;

        } file;
    };

} TokStream;

void tokStreamInitFile(TokStream* ts, View path, FILE* hnd);
void tokStreamFini(TokStream* ts);

FORMAT(2)
NORETURN void tokStreamFatal(TokStream* ts, char const* fmt, ...);

FORMAT(3)
NORETURN
void tokStreamFatalPos(TokStream* ts, FilePos pos, char const* fmt, ...);

NORETURN void tokStreamFatalV(TokStream* ts, char const* fmt, va_list args);
NORETURN void tokStreamFatalPosV(TokStream* ts, FilePos pos, char const* fmt,
                                 va_list args);

U32 tokStreamPeek(TokStream* ts);
U32 tokStreamEat(TokStream* ts);

View    tokStreamView(TokStream* ts);
Num     tokStreamNum(TokStream* ts);
FilePos tokStreamPos(TokStream* ts);

#endif // TOK_H
