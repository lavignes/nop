#ifndef BUF_H
#define BUF_H

#include "ints.h"

typedef struct {
    U8*  bytes;
    UInt len;
} BufView;

static BufView const BUF_VIEW_NULL = {0};

#define BUF_VIEW(cstr) ((BufView){(U8*)(cstr), sizeof(cstr) - 1})

typedef struct {
    BufView view;
    UInt    cap;
} Buf;

void bufCat(Buf* buf, BufView view);
void bufFini(Buf* buf);

#endif // BUF_H
