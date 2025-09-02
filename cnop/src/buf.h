#ifndef BUF_H
#define BUF_H

#include "ints.h"

typedef struct {
    U8*  bytes;
    UInt len;
} Buf;

typedef struct {
    Buf  inner;
    UInt cap;
} GBuf;

#endif // BUF_H
