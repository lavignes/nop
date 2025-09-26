#include "tok.h"

#include <errno.h>
#include <string.h>

void tokStreamInitFile(TokStream* ts, View path, FILE* hnd) {
    memset(ts, 0, sizeof(*ts));
    ts->kind       = TOK_STREAM_FILE;
    ts->pos        = (FilePos){path, 1, 1};
    ts->file.hnd   = hnd;
    ts->file.cline = 1;
    ts->file.ccol  = 1;
}

void tokStreamFini(TokStream* ts) {
    switch (ts->kind) {
    case TOK_STREAM_FILE:
        if (fclose(ts->file.hnd) == EOF) {
            int err = errno;
            fprintf(stderr, VIEW_FMT ":%zu:%zu", VIEW_FMT_ARG(ts->pos.path),
                    ts->pos.line, ts->pos.col);
            fatal("failed to close file: %s", strerror(err));
        }
        bufFini(&ts->file.buf);
        break;
    default:
        UNREACHABLE();
    }
}

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
