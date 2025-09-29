#include "tok.h"
#include "utf8.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

void tokStreamInitFile(TokStream* ts, View path, FILE* hnd) {
    memset(ts, 0, sizeof(*ts));
    ts->kind          = TOK_STREAM_FILE;
    ts->pos           = (Pos){path, 1, 1};
    ts->chardev.hnd   = hnd;
    ts->chardev.cline = 1;
    ts->chardev.ccol  = 1;
}

void tokStreamInitView(TokStream* ts, View path, View view) {
    memset(ts, 0, sizeof(*ts));
    ts->kind          = TOK_STREAM_VIEW;
    ts->pos           = (Pos){path, 1, 1};
    ts->chardev.view  = view;
    ts->chardev.cline = 1;
    ts->chardev.ccol  = 1;
}

void tokStreamFini(TokStream* ts) {
    switch (ts->kind) {
    case TOK_STREAM_FILE:
        if (fclose(ts->chardev.hnd) == EOF) {
            int err = errno;
            fprintf(stderr, VIEW_FMT ":" UINT_FMT ":" UINT_FMT ": ",
                    VIEW_FMT_ARG(ts->pos.path), ts->pos.line, ts->pos.col);
            fatal("failed to close file: %s", strerror(err));
        }
        // fallthrough
    case TOK_STREAM_VIEW:
        bufFini(&ts->chardev.buf);
        break;
    default:
        UNREACHABLE();
    }
}

FORMAT(2)
NORETURN void tokStreamFatal(TokStream const* ts, char const* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    tokStreamFatalV(ts, fmt, args);
}

FORMAT(3)
NORETURN
void tokStreamFatalPos(TokStream const* ts, Pos pos, char const* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    tokStreamFatalPosV(ts, pos, fmt, args);
}

NORETURN void tokStreamFatalV(TokStream const* ts, char const* fmt,
                              va_list args) {
    switch (ts->kind) {
    case TOK_STREAM_FILE:
    case TOK_STREAM_VIEW:
        tokStreamFatalPosV(ts, ts->pos, fmt, args);
    default:
        UNREACHABLE();
    }
}

NORETURN void tokStreamFatalPosV(TokStream const* ts, Pos pos, char const* fmt,
                                 va_list args) {
    switch (ts->kind) {
    case TOK_STREAM_FILE:
    case TOK_STREAM_VIEW:
        fprintf(stderr, VIEW_FMT ":" UINT_FMT ":" UINT_FMT ": ",
                VIEW_FMT_ARG(pos.path), pos.line, pos.col);
        break;
    default:
        UNREACHABLE();
    }
    fatalV(fmt, args);
}

static NORETURN void cfatal(TokStream* ts, char const* fmt, ...) {
    fprintf(stderr, VIEW_FMT ":" UINT_FMT ":" UINT_FMT ": ",
            VIEW_FMT_ARG(ts->pos.path), ts->chardev.cline, ts->chardev.ccol);
    va_list args;
    va_start(args, fmt);
    fatalV(fmt, args);
}

static U32 cpeek(TokStream* ts) {
    if (ts->chardev.cstashed) {
        return ts->chardev.cstash;
    }
    ts->chardev.cstashed = true;
    if (ts->chardev.clen == 0) {
        switch (ts->kind) {
        case TOK_STREAM_FILE:
            ts->chardev.clen = fread(ts->chardev.cbuf, 1, 1, ts->chardev.hnd);
            if (ts->chardev.clen != 1) {
                int err = ferror(ts->chardev.hnd);
                if (err) {
                    cfatal(ts, "failed to read from file: %s\n", strerror(err));
                }
                if (feof(ts->chardev.hnd)) {
                    ts->chardev.cstash = TOK_EOF;
                    return TOK_EOF;
                }
            }
            break;
        case TOK_STREAM_VIEW:
            if (ts->chardev.view.len == 0) {
                ts->chardev.cstash = TOK_EOF;
                return TOK_EOF;
            }
            ts->chardev.cbuf[0] = ts->chardev.view.bytes[0];
            ts->chardev.clen    = 1;
            ts->chardev.view.bytes += 1;
            ts->chardev.view.len -= 1;
            break;
        default:
            UNREACHABLE();
        }
    }
    UInt len = 0;
    while (len == 0) {
        ts->chardev.cstash =
            utf8Decode((View){ts->chardev.cbuf, ts->chardev.clen}, &len);
        ts->chardev.clen -= len;
        memmove(ts->chardev.cbuf, ts->chardev.cbuf + len, ts->chardev.clen);
        if (len == 0) {
            if (ts->chardev.clen == 4) {
                cfatal(ts, "invalid UTF-8 sequence\n");
            }
            UInt read;
            switch (ts->kind) {
            case TOK_STREAM_FILE:
                read = fread(ts->chardev.cbuf + ts->chardev.clen, 1, 1,
                             ts->chardev.hnd);
                if (read != 0) {
                    int err = ferror(ts->chardev.hnd);
                    if (err) {
                        cfatal(ts, "failed to read from file: %s\n",
                               strerror(err));
                    }
                    if (feof(ts->chardev.hnd)) {
                        cfatal(ts, "invalid UTF-8 sequence\n");
                    }
                }
                break;
            case TOK_STREAM_VIEW:
                if (ts->chardev.view.len == 0) {
                    cfatal(ts, "invalid UTF-8 sequence\n");
                }
                ts->chardev.cbuf[ts->chardev.clen] = ts->chardev.view.bytes[0];
                ts->chardev.clen += 1;
                ts->chardev.view.bytes += 1;
                ts->chardev.view.len -= 1;
                read = 1;
                break;
            default:
                UNREACHABLE();
            }
            ts->chardev.clen += read;
        }
    }
    return ts->chardev.cstash;
}

static void ceat(TokStream* ts) {
    ts->chardev.cstashed = false;
    ++ts->chardev.ccol;
    if (ts->chardev.cstash == '\n') {
        ++ts->chardev.cline;
        ts->chardev.ccol = 1;
    }
}

static void cpush(TokStream* ts, U32 c) {
    U8   tmp[4];
    UInt len = utf8Encode(VIEW(tmp), c);
    bufCat(&ts->chardev.buf, (View){tmp, len});
}

static U32 eatByteEscape(TokStream* ts) {
    U32 c = 0;
    for (UInt i = 0; i < 2; ++i) {
        U32 d = cpeek(ts);
        if (!isxdigit(d)) {
            cfatal(ts, "invalid byte escape sequence\n");
        }
        ceat(ts);
        c <<= 4;
        if (isdigit(d)) {
            c |= (d - '0');
        } else {
            c |= (tolower(d) - 'a' + 10);
        }
    }
    return c;
}

static U32 eatUnicodeEscape(TokStream* ts) {
    if (cpeek(ts) != '{') {
        cfatal(ts, "invalid unicode escape sequence\n");
    }
    ceat(ts);
    U32 c = 0;
    while (true) {
        U32 d = cpeek(ts);
        if (d == '_') {
            ceat(ts);
            continue;
        }
        if (d == '}') {
            break;
        }
        if (!isxdigit(d)) {
            cfatal(ts, "invalid unicode escape sequence\n");
        }
        ceat(ts);
        c <<= 4;
        if (isdigit(d)) {
            c |= (d - '0');
        } else {
            c |= (tolower(d) - 'a' + 10);
        }
        if (c > 0x10FFFF) {
            cfatal(ts, "unicode code point out of range\n");
        }
    }
    if (cpeek(ts) != '}') {
        cfatal(ts, "unterminated unicode escape sequence\n");
    }
    ceat(ts);
    return c;
}

static U32 eatString(TokStream* ts) {
    ceat(ts);
    while (true) {
        U32 c = cpeek(ts);
        switch (c) {
        case TOK_EOF:
            cfatal(ts, "unterminated string literal\n");
        case '"':
            ceat(ts);
            goto stringdone;
        case '\\':
            ceat(ts);
            switch (cpeek(ts)) {
            case 'n':
                ceat(ts);
                cpush(ts, '\n');
                break;
            case 'r':
                ceat(ts);
                cpush(ts, '\r');
                break;
            case 't':
                ceat(ts);
                cpush(ts, '\t');
                break;
            case '\\':
                ceat(ts);
                cpush(ts, '\\');
                break;
            case '"':
                ceat(ts);
                cpush(ts, '"');
                break;
            case '0':
                ceat(ts);
                cpush(ts, '\0');
                break;
            case 'x':
                ceat(ts);
                cpush(ts, eatByteEscape(ts));
                break;
            case 'u':
                ceat(ts);
                cpush(ts, eatUnicodeEscape(ts));
                break;
            default:
                cfatal(ts, "invalid escape sequence\n");
            }
            break;
        default:
            ceat(ts);
            cpush(ts, c);
            break;
        }
    }
stringdone:
    ts->chardev.stashed = true;
    ts->chardev.stash   = TOK_STR;
    return TOK_STR;
}

static U32 eatCharacter(TokStream* ts) {
    ceat(ts);
    U32 c = cpeek(ts);
    switch (c) {
    case TOK_EOF:
        cfatal(ts, "unterminated character literal\n");
    case '\\':
        ceat(ts);
        switch (cpeek(ts)) {
        case 'n':
            ceat(ts);
            ts->chardev.num.val = '\n';
            break;
        case 'r':
            ceat(ts);
            ts->chardev.num.val = '\r';
            break;
        case 't':
            ceat(ts);
            ts->chardev.num.val = '\t';
            break;
        case '\\':
            ceat(ts);
            ts->chardev.num.val = '\\';
            break;
        case '\'':
            ceat(ts);
            ts->chardev.num.val = '\'';
            break;
        case '0':
            ceat(ts);
            ts->chardev.num.val = '\0';
            break;
        case 'x':
            ceat(ts);
            ts->chardev.num.val = eatByteEscape(ts);
            break;
        case 'u':
            ceat(ts);
            ts->chardev.num.val = eatUnicodeEscape(ts);
            break;
        default:
            cfatal(ts, "invalid escape sequence\n");
        }
        break;
    default:
        ceat(ts);
        ts->chardev.num.val = c;
        break;
    }
    if (cpeek(ts) != '\'') {
        cfatal(ts, "unterminated character literal\n");
    }
    ceat(ts);
    ts->chardev.stashed = true;
    ts->chardev.stash   = TOK_CHAR;
    return TOK_CHAR;
}

static FloatKind eatFloatSuffix(TokStream* ts) {
    ceat(ts);
    if (cpeek(ts) == '3') {
        ceat(ts);
        if (cpeek(ts) == '2') {
            ceat(ts);
            return FLOAT_F32;
        } else {
            cfatal(ts, "invalid float suffix\n");
        }
    } else if (cpeek(ts) == '6') {
        ceat(ts);
        if (cpeek(ts) == '4') {
            ceat(ts);
            return FLOAT_F64;
        } else {
            cfatal(ts, "invalid float suffix\n");
        }
    }
    return FLOAT_FLOAT;
}

static U32 eatNumber(TokStream* ts) {
    bufClear(&ts->chardev.buf);
    Bool      flt   = false;
    FloatKind fkind = FLOAT_FLOAT;
    IntKind   ikind = INT_INTEGER;
    int       base  = 10;
    if (cpeek(ts) == '0') {
        ceat(ts);
        switch (cpeek(ts)) {
        case 'b':
            ceat(ts);
            base = 2;
            break;
        case 'o':
            ceat(ts);
            base = 8;
            break;
        case 'x':
            ceat(ts);
            base = 16;
            break;
        default:
            cpush(ts, '0');
            break;
        }
    }
    while (true) {
        U32 c = cpeek(ts);
        if (c == '_') {
            ceat(ts);
            continue;
        }
        if (isdigit(c)) {
            ceat(ts);
            cpush(ts, c);
            continue;
        }
        if (isxdigit(c) && (base == 16)) {
            ceat(ts);
            cpush(ts, c);
            continue;
        }
        if (cpeek(ts) == '.') {
            flt = true;
            ceat(ts);
            cpush(ts, '.');
            continue;
        }
        if (tolower(c) == 'e') {
            flt = true;
            ceat(ts);
            cpush(ts, 'e');
            if ((cpeek(ts) == '+') || (cpeek(ts) == '-')) {
                cpush(ts, cpeek(ts));
                ceat(ts);
            }
            continue;
        }
        break;
    }
    if (cpeek(ts) == 'f') {
        flt   = true;
        fkind = eatFloatSuffix(ts);
    } else if (cpeek(ts) == 'u') {
        ceat(ts);
        switch (cpeek(ts)) {
        case '8':
            ceat(ts);
            ikind = INT_U8;
            break;
        case '1':
            ceat(ts);
            if (cpeek(ts) == '6') {
                ceat(ts);
                ikind = INT_U16;
            } else {
                cfatal(ts, "invalid integer suffix\n");
            }
            break;
        case '3':
            ceat(ts);
            if (cpeek(ts) == '2') {
                ceat(ts);
                ikind = INT_U32;
            } else {
                cfatal(ts, "invalid integer suffix\n");
            }
            break;
        case '6':
            ceat(ts);
            if (cpeek(ts) == '4') {
                ceat(ts);
                ikind = INT_U64;
            } else {
                cfatal(ts, "invalid integer suffix\n");
            }
            break;
        default:
            ikind = INT_UINT;
            break;
        }
    } else if (cpeek(ts) == 'i') {
        ceat(ts);
        switch (cpeek(ts)) {
        case '8':
            ceat(ts);
            ikind = INT_I8;
            break;
        case '1':
            ceat(ts);
            if (cpeek(ts) == '6') {
                ceat(ts);
                ikind = INT_I16;
            } else {
                cfatal(ts, "invalid integer suffix\n");
            }
            break;
        case '3':
            ceat(ts);
            if (cpeek(ts) == '2') {
                ceat(ts);
                ikind = INT_I32;
            } else {
                cfatal(ts, "invalid integer suffix\n");
            }
            break;
        case '6':
            ceat(ts);
            if (cpeek(ts) == '4') {
                ceat(ts);
                ikind = INT_I64;
            } else {
                cfatal(ts, "invalid integer suffix\n");
            }
            break;
        default:
            ikind = INT_INT;
            break;
        }
    }
    cpush(ts, 0);
    char* end;
    if (flt) {
        if (base != 10) {
            tokStreamFatal(ts, "floating point literals must be base 10\n");
        }
        errno = 0;
        if (fkind == FLOAT_F32) {
            ts->chardev.fnum.val =
                strtof((char const*)ts->chardev.buf.view.bytes, &end);
        } else {
            ts->chardev.fnum.val =
                strtod((char const*)ts->chardev.buf.view.bytes, &end);
        }
        if (errno == ERANGE) {
            tokStreamFatal(ts, "floating point literal out of range\n");
        }
        if (end != (((char*)ts->chardev.buf.view.bytes) +
                    ts->chardev.buf.view.len - 1)) {
            tokStreamFatal(ts, "invalid floating point literal\n");
        }
        ts->chardev.fnum.kind = fkind;
        ts->chardev.stashed   = true;
        ts->chardev.stash     = TOK_FLOAT;
        return TOK_FLOAT;
    }
    errno = 0;
    ts->chardev.num.val =
        strtoul((char const*)ts->chardev.buf.view.bytes, &end, base);
    if (errno == ERANGE) {
        tokStreamFatal(ts, "integer literal out of range\n");
    }
    if (end !=
        (((char*)ts->chardev.buf.view.bytes) + ts->chardev.buf.view.len - 1)) {
        tokStreamFatal(ts, "invalid integer literal\n");
    }
    ts->chardev.num.kind = ikind;
    ts->chardev.stashed  = true;
    ts->chardev.stash    = TOK_INT;
    return TOK_INT;
}

static const struct {
    U8  text[2];
    U32 tok;
} DIGRAPHS[] = {
    {"<<", TOK_SHL},    {">>", TOK_SHR},    {"<=", TOK_LTE},
    {">=", TOK_GTE},    {"==", TOK_EQ},     {"!=", TOK_NEQ},
    {"&&", TOK_AND},    {"||", TOK_OR},     {"+=", TOK_ADD_EQ},
    {"-=", TOK_SUB_EQ}, {"*=", TOK_MUL_EQ}, {"/=", TOK_DIV_EQ},
    {"%=", TOK_MOD_EQ}, {"&=", TOK_AND_EQ}, {"|=", TOK_OR_EQ},
    {"^=", TOK_XOR_EQ},
};

// TODO: I dont think types should be keywords. They should be identifiers that
// refer to types.
// It will make parsing easier.
/*
static const struct {
    View name;
    U32  tok;
} TYPES[] = {
    {VIEW("Nil"), TOK_TYPE_NIL},   {VIEW("U8"), TOK_TYPE_U8},
    {VIEW("U16"), TOK_TYPE_U16},   {VIEW("U32"), TOK_TYPE_U32},
    {VIEW("U64"), TOK_TYPE_U64},   {VIEW("UInt"), TOK_TYPE_UINT},
    {VIEW("I8"), TOK_TYPE_I8},     {VIEW("I16"), TOK_TYPE_I16},
    {VIEW("I32"), TOK_TYPE_I32},   {VIEW("I64"), TOK_TYPE_I64},
    {VIEW("Int"), TOK_TYPE_INT},   {VIEW("F32"), TOK_TYPE_F32},
    {VIEW("F64"), TOK_TYPE_F64},   {VIEW("Bool"), TOK_TYPE_BOOL},
    {VIEW("Char"), TOK_TYPE_CHAR}, {VIEW("Never"), TOK_TYPE_NEVER},
};
*/

static const struct {
    View name;
    U32  tok;
} CONSTANTS[] = {
    {VIEW("TRUE"), TOK_BOOL},
    {VIEW("FALSE"), TOK_BOOL},
    {VIEW("NIL"), TOK_NIL},
    {VIEW("NEVER"), TOK_NEVER},
};

static const struct {
    View name;
    U32  tok;
} KEYWORDS[] = {
    {VIEW("pkg"), TOK_PKG},
    {VIEW("fn"), TOK_FN},
};

U32 peekFile(TokStream* ts) {
    if (ts->chardev.stashed) {
        return ts->chardev.stash;
    }
    while (true) {
        U32 c = cpeek(ts);
        if ((c == TOK_EOF) || !isspace(c) || (c == '\n')) {
            break;
        }
        ceat(ts);
    }
    ts->pos.line = ts->chardev.cline;
    ts->pos.col  = ts->chardev.ccol;
    if (cpeek(ts) == TOK_EOF) {
        ceat(ts);
        ts->chardev.stashed = true;
        ts->chardev.stash   = TOK_EOF;
        return TOK_EOF;
    }
    if (cpeek(ts) == '"') {
        return eatString(ts);
    }
    if (cpeek(ts) == '\'') {
        return eatCharacter(ts);
    }
    if (isdigit(cpeek(ts))) {
        return eatNumber(ts);
    }
    while (true) {
        U32 c = cpeek(ts);
        if (!isalnum(c) && (c != '_')) {
            break;
        }
        ceat(ts);
        cpush(ts, c);
    }
    U32 c = cpeek(ts);
    if (ts->chardev.buf.view.len == 0) {
        ceat(ts);
        U32 nc = cpeek(ts);
        for (UInt i = 0; i < (sizeof(DIGRAPHS) / sizeof(DIGRAPHS[0])); ++i) {
            if ((DIGRAPHS[i].text[0] != c) || (DIGRAPHS[i].text[1] != nc)) {
                continue;
            }
            ceat(ts);
            ts->chardev.stashed = true;
            ts->chardev.stash   = DIGRAPHS[i].tok;
            return DIGRAPHS[i].tok;
        }
    }
    if (ts->chardev.buf.view.len != 0) {
        for (UInt i = 0; i < (sizeof(CONSTANTS) / sizeof(CONSTANTS[0])); ++i) {
            if (viewCmp(ts->chardev.buf.view, CONSTANTS[i].name) != 0) {
                continue;
            }
            if (CONSTANTS[i].tok == TOK_BOOL) {
                if (viewCmp(ts->chardev.buf.view, VIEW("TRUE")) == 0) {
                    ts->chardev.num.val = 1;
                } else {
                    ts->chardev.num.val = 0;
                }
                ts->chardev.num.kind = INT_U8;
            }
            ts->chardev.stashed = true;
            ts->chardev.stash   = CONSTANTS[i].tok;
            return CONSTANTS[i].tok;
        }
    }
    if (ts->chardev.buf.view.len != 0) {
        for (UInt i = 0; i < (sizeof(KEYWORDS) / sizeof(KEYWORDS[0])); ++i) {
            if (viewCmp(ts->chardev.buf.view, KEYWORDS[i].name) != 0) {
                continue;
            }
            ts->chardev.stashed = true;
            ts->chardev.stash   = KEYWORDS[i].tok;
            return KEYWORDS[i].tok;
        }
        ts->chardev.stashed = true;
        ts->chardev.stash   = TOK_ID;
        return TOK_ID;
    }
    ts->chardev.stashed = true;
    ts->chardev.stash   = c;
    return c;
}

U32 tokStreamPeek(TokStream* ts) {
    switch (ts->kind) {
    case TOK_STREAM_FILE:
    case TOK_STREAM_VIEW:
        return peekFile(ts);
    default:
        UNREACHABLE();
    }
}

void tokStreamEat(TokStream* ts) {
    switch (ts->kind) {
    case TOK_STREAM_FILE:
    case TOK_STREAM_VIEW:
        ts->chardev.stashed = false;
        bufClear(&ts->chardev.buf);
        return;
    default:
        UNREACHABLE();
    }
}

View tokStreamView(TokStream const* ts) {
    switch (ts->kind) {
    case TOK_STREAM_FILE:
    case TOK_STREAM_VIEW:
        return ts->chardev.buf.view;
    default:
        UNREACHABLE();
    }
}

UInt tokStreamInt(TokStream const* ts, IntKind* kind) {
    switch (ts->kind) {
    case TOK_STREAM_FILE:
    case TOK_STREAM_VIEW:
        if (kind) {
            *kind = ts->chardev.num.kind;
        }
        return ts->chardev.num.val;
    default:
        UNREACHABLE();
    }
}

F64 tokStreamFloat(TokStream const* ts, FloatKind* kind) {
    switch (ts->kind) {
    case TOK_STREAM_FILE:
    case TOK_STREAM_VIEW:
        if (kind) {
            *kind = ts->chardev.fnum.kind;
        }
        return ts->chardev.fnum.val;
    default:
        UNREACHABLE();
    }
}

Bool tokStreamBool(TokStream const* ts) { return tokStreamInt(ts, NULL) != 0; }

U32 tokStreamChar(TokStream const* ts) { return tokStreamInt(ts, NULL); }

Pos tokStreamPos(TokStream const* ts) { return ts->pos; }
