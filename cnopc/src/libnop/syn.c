#include <nop/buf.h>
#include <nop/fatal.h>
#include <nop/syn.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

void itemBufAdd(ItemBuf* buf, Item item) { BUF_ADD_IMPL() }
void itemBufFini(ItemBuf* buf) { BUF_FINI_IMPL() }

static void popStream(Syn* syn) {
    assert(syn->ts >= syn->tsstack);
    toksFini(syn->ts);
    --syn->ts;
}

static U32 peek(Syn* syn) {
    U32 tok = toksPeek(syn->ts);
    if ((tok == TOK_EOF) && (syn->ts > syn->tsstack)) {
        popStream(syn);
        return peek(syn);
    }
    return tok;
}

FORMAT(2)
NORETURN
static void fatalHere(Syn const* syn, char const* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    toksFatalPosV(syn->ts, syn->ts->pos, fmt, args);
}

static void expect(Syn* syn, U32 tok) {
    static Buf expected = {0};
    static Buf found    = {0};
    U32        peeked   = peek(syn);
    if (peeked == tok) {
        return;
    }
    tokName(tok, &expected);
    tokName(peeked, &found);
    fatalHere(syn, "expected " VIEW_FMT ", got " VIEW_FMT "\n",
              VIEW_FMT_ARG(expected.view), VIEW_FMT_ARG(found.view));
}

static void eat(Syn* syn) { toksEat(syn->ts); }

void synInit(Syn* syn, Toks ts) {
    syn->tsstack[0] = ts;
    syn->ts         = syn->tsstack;
}

void synParse(Syn* syn, Pkg* pkg) {
    (void)pkg;

    expect(syn, TOK_PKG);
    eat(syn);
    expect(syn, TOK_IDENT);
    eat(syn);
}
