#include <nop/tok.h>
#include <nop/utf8.h>

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static struct {
    View name;
    U32  tok;
} const NAMES[] = {
    {VIEW("end of file"), TOK_EOF},
    {VIEW("identifier"), TOK_IDENT},
    {VIEW("integer literal"), TOK_INT},
    {VIEW("float literal"), TOK_FLOAT},
    {VIEW("boolean literal"), TOK_BOOL},
    {VIEW("string literal"), TOK_STR},
    {VIEW("character literal"), TOK_CHAR},
    {VIEW("nil literal"), TOK_NIL},
    {VIEW("never literal"), TOK_NEVER},
    {VIEW("comment"), TOK_COMMENT},

    {VIEW("`pkg`"), TOK_PKG},
    {VIEW("`fn`"), TOK_FN},

    {VIEW("`<<`"), TOK_SHL},
    {VIEW("`>>`"), TOK_SHR},
    {VIEW("`<=`"), TOK_LTE},
    {VIEW("`>=`"), TOK_GTE},
    {VIEW("`==`"), TOK_EQ},
    {VIEW("`!=`"), TOK_NEQ},
    {VIEW("`&&`"), TOK_AND},
    {VIEW("`||`"), TOK_OR},
    {VIEW("`+=`"), TOK_ADD_EQ},
    {VIEW("`-=`"), TOK_SUB_EQ},
    {VIEW("`*=`"), TOK_MUL_EQ},
    {VIEW("`/=`"), TOK_DIV_EQ},
    {VIEW("`%=`"), TOK_MOD_EQ},
    {VIEW("`&=`"), TOK_AND_EQ},
    {VIEW("`|=`"), TOK_OR_EQ},
    {VIEW("`^=`"), TOK_XOR_EQ},

    {VIEW("`->`"), TOK_ARROW},
};

void tokName(U32 tok, Buf* buf) {
    bufClear(buf);
    for (UInt i = 0; i < (sizeof(NAMES) / sizeof(NAMES[0])); ++i) {
        if (NAMES[i].tok != tok) {
            continue;
        }
        bufCat(buf, NAMES[i].name);
        return;
    }
    bufCat(buf, VIEW("`"));
    utf8Cat(buf, tok);
    bufCat(buf, VIEW("`"));
}

void toksInitFile(Toks* ts, View path, FILE* hnd) {
    memset(ts, 0, sizeof(*ts));
    ts->kind          = TOKS_FILE;
    ts->pos           = (Pos){path, 1, 1};
    ts->bstream.hnd   = hnd;
    ts->bstream.cline = 1;
    ts->bstream.ccol  = 1;
}

void toksInitView(Toks* ts, View path, View view) {
    memset(ts, 0, sizeof(*ts));
    ts->kind          = TOKS_VIEW;
    ts->pos           = (Pos){path, 1, 1};
    ts->bstream.view  = view;
    ts->bstream.cline = 1;
    ts->bstream.ccol  = 1;
}

void toksFini(Toks* ts) {
    switch (ts->kind) {
    case TOKS_FILE:
        if (fclose(ts->bstream.hnd) == EOF) {
            int err = errno;
            fprintf(stderr, VIEW_FMT ":" UINT_FMT ":" UINT_FMT ": ",
                    VIEW_FMT_ARG(ts->pos.path), ts->pos.line, ts->pos.col);
            fatal("failed to close file: %s", strerror(err));
        }
        // fallthrough
    case TOKS_VIEW:
        bufFini(&ts->bstream.buf);
        break;
    default:
        UNREACHABLE();
    }
}

FORMAT(2)
NORETURN void toksFatal(Toks const* ts, char const* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    toksFatalV(ts, fmt, args);
}

FORMAT(3)
NORETURN
void toksFatalPos(Toks const* ts, Pos pos, char const* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    toksFatalPosV(ts, pos, fmt, args);
}

NORETURN void toksFatalV(Toks const* ts, char const* fmt, va_list args) {
    switch (ts->kind) {
    case TOKS_FILE:
    case TOKS_VIEW:
        toksFatalPosV(ts, ts->pos, fmt, args);
    default:
        UNREACHABLE();
    }
}

NORETURN void toksFatalPosV(Toks const* ts, Pos pos, char const* fmt,
                            va_list args) {
    switch (ts->kind) {
    case TOKS_FILE:
    case TOKS_VIEW:
        fprintf(stderr, VIEW_FMT ":" UINT_FMT ":" UINT_FMT ": ",
                VIEW_FMT_ARG(pos.path), pos.line, pos.col);
        break;
    default:
        UNREACHABLE();
    }
    fatalV(fmt, args);
}

static NORETURN void cfatal(Toks* ts, char const* fmt, ...) {
    fprintf(stderr, VIEW_FMT ":" UINT_FMT ":" UINT_FMT ": ",
            VIEW_FMT_ARG(ts->pos.path), ts->bstream.cline, ts->bstream.ccol);
    va_list args;
    va_start(args, fmt);
    fatalV(fmt, args);
}

static U32 cpeek(Toks* ts) {
    if (ts->bstream.cstashed) {
        return ts->bstream.cstash;
    }
    ts->bstream.cstashed = true;
    if (ts->bstream.clen == 0) {
        switch (ts->kind) {
        case TOKS_FILE:
            ts->bstream.clen = fread(ts->bstream.cbuf, 1, 1, ts->bstream.hnd);
            if (ts->bstream.clen != 1) {
                int err = ferror(ts->bstream.hnd);
                if (err) {
                    cfatal(ts, "failed to read from file: %s\n", strerror(err));
                }
                if (feof(ts->bstream.hnd)) {
                    ts->bstream.cstash = TOK_EOF;
                    return TOK_EOF;
                }
            }
            break;
        case TOKS_VIEW:
            if (ts->bstream.view.len == 0) {
                ts->bstream.cstash = TOK_EOF;
                return TOK_EOF;
            }
            ts->bstream.cbuf[0] = ts->bstream.view.bytes[0];
            ts->bstream.clen    = 1;
            ts->bstream.view.bytes += 1;
            ts->bstream.view.len -= 1;
            break;
        default:
            UNREACHABLE();
        }
    }
    UInt len = 0;
    while (len == 0) {
        ts->bstream.cstash =
            utf8Decode((View){ts->bstream.cbuf, ts->bstream.clen}, &len);
        ts->bstream.clen -= len;
        memmove(ts->bstream.cbuf, ts->bstream.cbuf + len, ts->bstream.clen);
        if (len == 0) {
            if (ts->bstream.clen == 4) {
                cfatal(ts, "invalid UTF-8 sequence\n");
            }
            UInt read;
            switch (ts->kind) {
            case TOKS_FILE:
                read = fread(ts->bstream.cbuf + ts->bstream.clen, 1, 1,
                             ts->bstream.hnd);
                if (read != 0) {
                    int err = ferror(ts->bstream.hnd);
                    if (err) {
                        cfatal(ts, "failed to read from file: %s\n",
                               strerror(err));
                    }
                    if (feof(ts->bstream.hnd)) {
                        cfatal(ts, "invalid UTF-8 sequence\n");
                    }
                }
                break;
            case TOKS_VIEW:
                if (ts->bstream.view.len == 0) {
                    cfatal(ts, "invalid UTF-8 sequence\n");
                }
                ts->bstream.cbuf[ts->bstream.clen] = ts->bstream.view.bytes[0];
                ts->bstream.clen += 1;
                ts->bstream.view.bytes += 1;
                ts->bstream.view.len -= 1;
                read = 1;
                break;
            default:
                UNREACHABLE();
            }
            ts->bstream.clen += read;
        }
    }
    return ts->bstream.cstash;
}

static void ceat(Toks* ts) {
    ts->bstream.cstashed = false;
    ++ts->bstream.ccol;
    if (ts->bstream.cstash == '\n') {
        ++ts->bstream.cline;
        ts->bstream.ccol = 1;
    }
}

static void cpush(Toks* ts, U32 c) {
    U8   tmp[4];
    UInt len = utf8Encode(VIEW(tmp), c);
    bufCat(&ts->bstream.buf, (View){tmp, len});
}

static U32 eatByteEscape(Toks* ts) {
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

static U32 eatUnicodeEscape(Toks* ts) {
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

static U32 eatString(Toks* ts) {
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
    ts->bstream.stashed = true;
    ts->bstream.stash   = TOK_STR;
    return TOK_STR;
}

static U32 eatCharacter(Toks* ts) {
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
            ts->bstream.num.val = '\n';
            break;
        case 'r':
            ceat(ts);
            ts->bstream.num.val = '\r';
            break;
        case 't':
            ceat(ts);
            ts->bstream.num.val = '\t';
            break;
        case '\\':
            ceat(ts);
            ts->bstream.num.val = '\\';
            break;
        case '\'':
            ceat(ts);
            ts->bstream.num.val = '\'';
            break;
        case '0':
            ceat(ts);
            ts->bstream.num.val = '\0';
            break;
        case 'x':
            ceat(ts);
            ts->bstream.num.val = eatByteEscape(ts);
            break;
        case 'u':
            ceat(ts);
            ts->bstream.num.val = eatUnicodeEscape(ts);
            break;
        default:
            cfatal(ts, "invalid escape sequence\n");
        }
        break;
    default:
        ceat(ts);
        ts->bstream.num.val = c;
        break;
    }
    if (cpeek(ts) != '\'') {
        cfatal(ts, "unterminated character literal\n");
    }
    ceat(ts);
    ts->bstream.stashed = true;
    ts->bstream.stash   = TOK_CHAR;
    return TOK_CHAR;
}

static FloatKind eatFloatSuffix(Toks* ts) {
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

static U32 eatNumber(Toks* ts) {
    bufClear(&ts->bstream.buf);
    Bool      flt   = false;
    FloatKind fkind = FLOAT_FLOAT;
    IntKind   ikind = INT_INTEGER;
    int       base  = 10;
    if (cpeek(ts) == '0') {
        ceat(ts);
        switch (cpeek(ts)) {
        case 'b':
        case 'B':
            ceat(ts);
            base = 2;
            break;
        case 'o':
        case 'O':
            ceat(ts);
            base = 8;
            break;
        case 'x':
        case 'X':
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
        if (c == '.') {
            flt = true;
            ceat(ts);
            cpush(ts, '.');
            continue;
        }
        if ((c == 'e') || (c == 'E')) {
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
    if ((cpeek(ts) == 'f') || (cpeek(ts) == 'F')) {
        flt   = true;
        fkind = eatFloatSuffix(ts);
    } else if ((cpeek(ts) == 'u') || (cpeek(ts) == 'U')) {
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
    } else if ((cpeek(ts) == 'i' || (cpeek(ts) == 'I'))) {
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
            toksFatal(ts, "floating point literals must be base 10\n");
        }
        errno = 0;
        if (fkind == FLOAT_F32) {
            ts->bstream.fnum.val =
                strtof((char const*)ts->bstream.buf.view.bytes, &end);
        } else {
            ts->bstream.fnum.val =
                strtod((char const*)ts->bstream.buf.view.bytes, &end);
        }
        if (errno == ERANGE) {
            toksFatal(ts, "floating point literal out of range\n");
        }
        if (end != (((char*)ts->bstream.buf.view.bytes) +
                    ts->bstream.buf.view.len - 1)) {
            toksFatal(ts, "invalid floating point literal\n");
        }
        ts->bstream.fnum.kind = fkind;
        ts->bstream.stashed   = true;
        ts->bstream.stash     = TOK_FLOAT;
        return TOK_FLOAT;
    }
    errno = 0;
    ts->bstream.num.val =
        strtoul((char const*)ts->bstream.buf.view.bytes, &end, base);
    if (errno == ERANGE) {
        toksFatal(ts, "integer literal out of range\n");
    }
    if (end !=
        (((char*)ts->bstream.buf.view.bytes) + ts->bstream.buf.view.len - 1)) {
        toksFatal(ts, "invalid integer literal\n");
    }
    ts->bstream.num.base = base;
    ts->bstream.num.kind = ikind;
    ts->bstream.stashed  = true;
    ts->bstream.stash    = TOK_INT;
    return TOK_INT;
}

static struct {
    U8  text[2];
    U32 tok;
} const DIGRAPHS[] = {
    {"<<", TOK_SHL},    {">>", TOK_SHR},    {"<=", TOK_LTE},
    {">=", TOK_GTE},    {"==", TOK_EQ},     {"!=", TOK_NEQ},
    {"&&", TOK_AND},    {"||", TOK_OR},     {"+=", TOK_ADD_EQ},
    {"-=", TOK_SUB_EQ}, {"*=", TOK_MUL_EQ}, {"/=", TOK_DIV_EQ},
    {"%=", TOK_MOD_EQ}, {"&=", TOK_AND_EQ}, {"|=", TOK_OR_EQ},
    {"^=", TOK_XOR_EQ}, {"->", TOK_ARROW},
};

static struct {
    View name;
    U32  tok;
} const CONSTANTS[] = {
    {VIEW("TRUE"), TOK_BOOL},
    {VIEW("FALSE"), TOK_BOOL},
    {VIEW("NIL"), TOK_NIL},
    {VIEW("NEVER"), TOK_NEVER},
};

static struct {
    View name;
    U32  tok;
} const KEYWORDS[] = {
    {VIEW("pkg"), TOK_PKG},
    {VIEW("fn"), TOK_FN},
};

U32 peekFile(Toks* ts) {
    if (ts->bstream.stashed) {
        return ts->bstream.stash;
    }
    while (true) {
        U32 c = cpeek(ts);
        if ((c == TOK_EOF) || !isspace(c)) {
            break;
        }
        ceat(ts);
    }
    ts->pos.line = ts->bstream.cline;
    ts->pos.col  = ts->bstream.ccol;
    if (cpeek(ts) == TOK_EOF) {
        ceat(ts);
        ts->bstream.stashed = true;
        ts->bstream.stash   = TOK_EOF;
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
    if (ts->bstream.buf.view.len == 0) {
        ceat(ts);
        U32 nc = cpeek(ts);
        for (UInt i = 0; i < (sizeof(DIGRAPHS) / sizeof(DIGRAPHS[0])); ++i) {
            if ((DIGRAPHS[i].text[0] != c) || (DIGRAPHS[i].text[1] != nc)) {
                continue;
            }
            ceat(ts);
            ts->bstream.stashed = true;
            ts->bstream.stash   = DIGRAPHS[i].tok;
            return DIGRAPHS[i].tok;
        }
    }
    if (ts->bstream.buf.view.len != 0) {
        for (UInt i = 0; i < (sizeof(CONSTANTS) / sizeof(CONSTANTS[0])); ++i) {
            if (viewCmp(ts->bstream.buf.view, CONSTANTS[i].name) != 0) {
                continue;
            }
            if (CONSTANTS[i].tok == TOK_BOOL) {
                if (viewCmp(ts->bstream.buf.view, VIEW("TRUE")) == 0) {
                    ts->bstream.num.val = 1;
                } else {
                    ts->bstream.num.val = 0;
                }
                ts->bstream.num.kind = INT_U8;
            }
            ts->bstream.stashed = true;
            ts->bstream.stash   = CONSTANTS[i].tok;
            return CONSTANTS[i].tok;
        }
    }
    if (ts->bstream.buf.view.len != 0) {
        for (UInt i = 0; i < (sizeof(KEYWORDS) / sizeof(KEYWORDS[0])); ++i) {
            if (viewCmp(ts->bstream.buf.view, KEYWORDS[i].name) != 0) {
                continue;
            }
            ts->bstream.stashed = true;
            ts->bstream.stash   = KEYWORDS[i].tok;
            return KEYWORDS[i].tok;
        }
        ts->bstream.stashed = true;
        ts->bstream.stash   = TOK_IDENT;
        return TOK_IDENT;
    }
    ts->bstream.stashed = true;
    ts->bstream.stash   = c;
    return c;
}

U32 toksPeek(Toks* ts) {
    switch (ts->kind) {
    case TOKS_FILE:
    case TOKS_VIEW:
        return peekFile(ts);
    default:
        UNREACHABLE();
    }
}

void toksEat(Toks* ts) {
    switch (ts->kind) {
    case TOKS_FILE:
    case TOKS_VIEW:
        ts->bstream.stashed = false;
        bufClear(&ts->bstream.buf);
        return;
    default:
        UNREACHABLE();
    }
}

View toksView(Toks const* ts) {
    switch (ts->kind) {
    case TOKS_FILE:
    case TOKS_VIEW:
        return ts->bstream.buf.view;
    default:
        UNREACHABLE();
    }
}

UInt toksInt(Toks const* ts, IntKind* kind, UInt* base) {
    switch (ts->kind) {
    case TOKS_FILE:
    case TOKS_VIEW:
        if (kind) {
            *kind = ts->bstream.num.kind;
        }
        if (base) {
            *base = ts->bstream.num.base;
        }
        return ts->bstream.num.val;
    default:
        UNREACHABLE();
    }
}

F64 toksFloat(Toks const* ts, FloatKind* kind) {
    switch (ts->kind) {
    case TOKS_FILE:
    case TOKS_VIEW:
        if (kind) {
            *kind = ts->bstream.fnum.kind;
        }
        return ts->bstream.fnum.val;
    default:
        UNREACHABLE();
    }
}

Bool toksBool(Toks const* ts) { return toksInt(ts, NULL, NULL) != 0; }

U32 toksChar(Toks const* ts) { return toksInt(ts, NULL, NULL); }

Pos toksPos(Toks const* ts) { return ts->pos; }
