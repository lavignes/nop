#include "buf.h"
#include "fatal.h"

#include <stdlib.h>
#include <string.h>

Bool viewEqual(View lhs, View rhs) {
    if (lhs.len != rhs.len) {
        return false;
    }
    return memcmp(lhs.bytes, rhs.bytes, lhs.len) == 0;
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
