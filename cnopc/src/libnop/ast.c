#include <nop/ast.h>
#include <nop/buf.h>
#include <nop/fatal.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

void itemBufAdd(ItemBuf* buf, Item item) { BUF_ADD_IMPL() }
void itemBufFini(ItemBuf* buf) { BUF_FINI_IMPL() }

static void popStream(Prs* prs) {
    assert(prs->ts >= prs->tsstack);
    toksFini(prs->ts);
    --prs->ts;
}

static U32 peek(Prs* prs) {
    U32 tok = toksPeek(prs->ts);
    if ((tok == TOK_EOF) && (prs->ts > prs->tsstack)) {
        popStream(prs);
        return peek(prs);
    }
    return tok;
}

FORMAT(2)
NORETURN
static void fatalHere(Prs const* prs, char const* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    toksFatalPosV(prs->ts, prs->ts->pos, fmt, args);
}

static void expect(Prs* prs, U32 tok) {
    static Buf expected = {0};
    static Buf found    = {0};
    U32        peeked   = peek(prs);
    if (peeked == tok) {
        return;
    }
    tokName(tok, &expected);
    tokName(peeked, &found);
    fatalHere(prs, "expected " VIEW_FMT ", got " VIEW_FMT "\n",
              VIEW_FMT_ARG(expected.view), VIEW_FMT_ARG(found.view));
}

static void eat(Prs* prs) { toksEat(prs->ts); }

static Fn eatFn(Prs* prs) {
    expect(prs, TOK_FN);
    eat(prs);
    expect(prs, TOK_IDENT);
    eat(prs);
    expect(prs, '(');
    eat(prs);
}

static Item eatItem(Prs* prs) {
    switch (peek(prs)) {
    case TOK_FN:
        return (Item){
            .kind = ITEM_FN,
            .pos  = prs->ts->pos,
            .fn   = eatFn(prs),
        };
    }
}

void prsInit(Prs* prs, Toks ts) {
    prs->tsstack[0] = ts;
    prs->ts         = prs->tsstack;
}

void prsAst(Prs* prs, Pkg* pkg) {
    expect(prs, TOK_PKG);
    eat(prs);
    expect(prs, TOK_IDENT);
    eat(prs);
    expect(prs, ';');
    eat(prs);
    while (peek(prs) != TOK_EOF) {
        itemBufAdd(&pkg->items, eatItem(prs));
    }
}
