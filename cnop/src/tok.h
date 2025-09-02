#ifndef TOK_H
#define TOK_H

#include <stdio.h>

#include "buf.h"

typedef struct {
    Buf  path;
    UInt line;
    UInt col;
} FilePos;

typedef enum {
    TOK_STREAM_FILE,
    TOK_STREAM_MACRO,
} TokStreamKind;

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

            union {
                I64  inum;
                U64  unum;
                GBuf buf;
            };

        } file;

        struct {
        } macro;
    };

} TokStream;

#endif // TOK_H
