#ifndef BUF_H
#define BUF_H

#include <nop/num.h>

typedef struct {
    U8*  bytes;
    UInt len;
} View;

static View const VIEW_NULL = {0};

#define VIEW(cstr)         ((View){(U8*)(cstr), sizeof(cstr) - 1})

#define VIEW_FMT           "%*s"
#define VIEW_FMT_ARG(view) ((int)((view).len)), (view).bytes

Int viewCmp(View lhs, View rhs);

typedef struct {
    View view;
    UInt cap;
} Buf;

void bufFini(Buf* buf);
void bufCat(Buf* buf, View view);
void bufClear(Buf* buf);

#define BUF_ADD_IMPL()                                                         \
    if (!buf->view.items) {                                                    \
        buf->view.items = malloc(sizeof(*buf->view.items) * 16);               \
        if (!buf->view.items) {                                                \
            fatal("out of memory\n");                                          \
        }                                                                      \
        buf->view.len = 0;                                                     \
        buf->cap      = 16;                                                    \
    }                                                                          \
    if ((buf->cap - buf->view.len) == 0) {                                     \
        buf->view.items =                                                      \
            realloc(buf->view.items, sizeof(*buf->view.items) * buf->cap * 2); \
        if (!buf->view.items) {                                                \
            fatal("out of memory\n");                                          \
        }                                                                      \
        buf->cap *= 2;                                                         \
    }                                                                          \
    buf->view.items[buf->view.len] = item;                                     \
    ++buf->view.len;

#define BUF_FINI_IMPL()                                                        \
    if (!buf->view.items) {                                                    \
        return;                                                                \
    }                                                                          \
    free(buf->view.items);                                                     \
    memset(buf, 0, sizeof(*buf));

#endif // BUF_H
