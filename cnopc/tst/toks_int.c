#include <nop/tok.h>

#include <assert.h>

int main() {
    Toks ts = {0};
    toksInitView(&ts, VIEW("test.nop"),
                 VIEW("123u8 456U16 789u32 101112u64 131415u \n"
                      "123i8 456i16 789i32 101112I64 131415i \n"
                      "161718                                \n"
                      "0o176 0XFFF 0B101101                  \n"));

    IntKind ik;
    UInt    base;

    assert(toksPeek(&ts) == TOK_INT);
    assert(toksInt(&ts, &ik, &base) == 123);
    assert(ik == INT_U8);
    assert(base == 10);
    assert(toksPos(&ts).line == 1);
    assert(toksPos(&ts).col == 1);
    toksEat(&ts);

    assert(toksPeek(&ts) == TOK_INT);
    assert(toksInt(&ts, &ik, &base) == 456);
    assert(ik == INT_U16);
    assert(base == 10);
    assert(toksPos(&ts).line == 1);
    assert(toksPos(&ts).col == 7);
    toksEat(&ts);

    assert(toksPeek(&ts) == TOK_INT);
    assert(toksInt(&ts, &ik, &base) == 789);
    assert(ik == INT_U32);
    assert(base == 10);
    assert(toksPos(&ts).line == 1);
    assert(toksPos(&ts).col == 14);
    toksEat(&ts);

    assert(toksPeek(&ts) == TOK_INT);
    assert(toksInt(&ts, &ik, &base) == 101112);
    assert(ik == INT_U64);
    assert(base == 10);
    assert(toksPos(&ts).line == 1);
    assert(toksPos(&ts).col == 21);
    toksEat(&ts);

    assert(toksPeek(&ts) == TOK_INT);
    assert(toksInt(&ts, &ik, &base) == 131415);
    assert(ik == INT_UINT);
    assert(base == 10);
    assert(toksPos(&ts).line == 1);
    assert(toksPos(&ts).col == 31);
    toksEat(&ts);

    assert(toksPeek(&ts) == TOK_INT);
    assert(toksInt(&ts, &ik, &base) == 123);
    assert(ik == INT_I8);
    assert(base == 10);
    assert(toksPos(&ts).line == 2);
    assert(toksPos(&ts).col == 1);
    toksEat(&ts);

    assert(toksPeek(&ts) == TOK_INT);
    assert(toksInt(&ts, &ik, &base) == 456);
    assert(ik == INT_I16);
    assert(base == 10);
    assert(toksPos(&ts).line == 2);
    assert(toksPos(&ts).col == 7);
    toksEat(&ts);

    assert(toksPeek(&ts) == TOK_INT);
    assert(toksInt(&ts, &ik, &base) == 789);
    assert(ik == INT_I32);
    assert(base == 10);
    assert(toksPos(&ts).line == 2);
    assert(toksPos(&ts).col == 14);
    toksEat(&ts);

    assert(toksPeek(&ts) == TOK_INT);
    assert(toksInt(&ts, &ik, &base) == 101112);
    assert(ik == INT_I64);
    assert(base == 10);
    assert(toksPos(&ts).line == 2);
    assert(toksPos(&ts).col == 21);
    toksEat(&ts);

    assert(toksPeek(&ts) == TOK_INT);
    assert(toksInt(&ts, &ik, &base) == 131415);
    assert(ik == INT_INT);
    assert(base == 10);
    assert(toksPos(&ts).line == 2);
    assert(toksPos(&ts).col == 31);
    toksEat(&ts);

    assert(toksPeek(&ts) == TOK_INT);
    assert(toksInt(&ts, &ik, &base) == 161718);
    assert(ik == INT_INTEGER);
    assert(base == 10);
    assert(toksPos(&ts).line == 3);
    assert(toksPos(&ts).col == 1);
    toksEat(&ts);

    assert(toksPeek(&ts) == TOK_INT);
    assert(toksInt(&ts, &ik, &base) == 0176);
    assert(ik == INT_INTEGER);
    assert(base == 8);
    assert(toksPos(&ts).line == 4);
    assert(toksPos(&ts).col == 1);
    toksEat(&ts);

    assert(toksPeek(&ts) == TOK_INT);
    assert(toksInt(&ts, &ik, &base) == 0xFFF);
    assert(ik == INT_INTEGER);
    assert(base == 16);
    assert(toksPos(&ts).line == 4);
    assert(toksPos(&ts).col == 7);
    toksEat(&ts);

    assert(toksPeek(&ts) == TOK_INT);
    assert(toksInt(&ts, &ik, &base) == 0b101101);
    assert(ik == INT_INTEGER);
    assert(base == 2);
    assert(toksPos(&ts).line == 4);
    assert(toksPos(&ts).col == 13);
    toksEat(&ts);

    return 0;
}
