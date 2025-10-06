#include <nop/tok.h>

#include <assert.h>
#include <math.h>

Bool f32Eq(F64 lhs, F64 rhs) { return fabs(lhs - rhs) < 0.0001; }

int main() {
    Toks ts = {0};
    toksInitView(&ts, VIEW("test.nop"),
                 VIEW("123.456f32 123.456F64 123.456        \n"
                      "123F 123E10 123.0e-10 123.0E+1f32    \n"));

    FloatKind fk;

    assert(toksPeek(&ts) == TOK_FLOAT);
    assert(f32Eq(toksFloat(&ts, &fk), 123.456));
    assert(fk == FLOAT_F32);
    assert(toksPos(&ts).line == 1);
    assert(toksPos(&ts).col == 1);
    toksEat(&ts);

    assert(toksPeek(&ts) == TOK_FLOAT);
    assert(toksFloat(&ts, &fk) == 123.456);
    assert(fk == FLOAT_F64);
    assert(toksPos(&ts).line == 1);
    assert(toksPos(&ts).col == 12);
    toksEat(&ts);

    assert(toksPeek(&ts) == TOK_FLOAT);
    assert(toksFloat(&ts, &fk) == 123.456);
    assert(fk == FLOAT_FLOAT);
    assert(toksPos(&ts).line == 1);
    assert(toksPos(&ts).col == 23);
    toksEat(&ts);

    assert(toksPeek(&ts) == TOK_FLOAT);
    assert(toksFloat(&ts, &fk) == 123.0);
    assert(fk == FLOAT_FLOAT);
    assert(toksPos(&ts).line == 2);
    assert(toksPos(&ts).col == 1);
    toksEat(&ts);

    assert(toksPeek(&ts) == TOK_FLOAT);
    assert(toksFloat(&ts, &fk) == 123e10);
    assert(fk == FLOAT_FLOAT);
    assert(toksPos(&ts).line == 2);
    assert(toksPos(&ts).col == 6);
    toksEat(&ts);

    assert(toksPeek(&ts) == TOK_FLOAT);
    assert(toksFloat(&ts, &fk) == 123.0e-10);
    assert(fk == FLOAT_FLOAT);
    assert(toksPos(&ts).line == 2);
    assert(toksPos(&ts).col == 13);
    toksEat(&ts);

    assert(toksPeek(&ts) == TOK_FLOAT);
    assert(toksFloat(&ts, &fk) == 123.0e+1);
    assert(fk == FLOAT_F32);
    assert(toksPos(&ts).line == 2);
    assert(toksPos(&ts).col == 23);
    toksEat(&ts);

    return 0;
}
