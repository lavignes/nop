#ifndef BUF_H
#define BUF_H

#include "num.h"

typedef struct {
    U8*  bytes;
    UInt len;
} View;

static View const VIEW_NULL = {0};

#define VIEW(cstr)         ((View){(U8*)(cstr), sizeof(cstr) - 1})

#define VIEW_FMT           "%*s"
#define VIEW_FMT_ARG(view) ((int)((view).len)), (view).bytes

typedef struct {
    View view;
    UInt cap;
} Buf;

void bufCat(Buf* buf, View view);
void bufFini(Buf* buf);

#endif // BUF_H
