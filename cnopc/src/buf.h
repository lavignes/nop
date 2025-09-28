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

Bool viewEqual(View lhs, View rhs);

typedef struct {
    View view;
    UInt cap;
} Buf;

void bufFini(Buf* buf);
void bufCat(Buf* buf, View view);
void bufClear(Buf* buf);

#endif // BUF_H
