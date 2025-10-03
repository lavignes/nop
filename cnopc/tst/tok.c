#include <nop/tok.h>

#include <assert.h>

int main() {
    Toks ts = {0};
    toksInitView(&ts, VIEW("test.nop"), VIEW("\
        pkg Test;                             \
                                              \
        use U32;                              \
    "));
    assert(toksPeek(&ts) == TOK_PKG);

    return 0;
}
