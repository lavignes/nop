#include <nop/buf.h>
#include <nop/fatal.h>

#include <stdlib.h>
#include <string.h>

Int viewCmp(View lhs, View rhs) {
    UInt len;
    if (lhs.len < rhs.len) {
        len = lhs.len;
    } else {
        len = rhs.len;
    }
    Int cmp = memcmp(lhs.bytes, rhs.bytes, len);
    if (cmp != 0) {
        return cmp;
    }
    if (lhs.len == rhs.len) {
        return 0;
    }
    if (lhs.len < rhs.len) {
        return -1;
    }
    return 1;
}

void bufFini(Buf* buf) {
    if (!buf->view.bytes) {
        return;
    }
    free(buf->view.bytes);
    memset(buf, 0, sizeof(*buf));
}

void bufCat(Buf* buf, View view) {
    if (!buf->view.bytes) {
        buf->view.bytes = malloc(view.len);
        if (!buf->view.bytes) {
            fatal("out of memory");
        }
        buf->view.len = 0;
        buf->cap      = view.len;
    }
    if ((buf->view.len + view.len) > buf->cap) {
        buf->view.bytes = realloc(buf->view.bytes, buf->cap + view.len);
        if (!buf->view.bytes) {
            fatal("out of memory");
        }
        buf->cap += view.len;
    }
    memcpy(buf->view.bytes + buf->view.len, view.bytes, view.len);
    buf->view.len += view.len;
}

void bufClear(Buf* buf) { buf->view.len = 0; }
