#include "tok.h"

#include <assert.h>

int main(int argc, char const* argv[]) {
    (void)argc;
    (void)argv;

    TokStream ts = {0};
    tokStreamInitView(&ts, VIEW("test.nop"), VIEW("8u"));

    assert(tokStreamPeek(&ts) == TOK_INT);
    tokStreamEat(&ts);
    assert(tokStreamPeek(&ts) == TOK_EOF);

    return 0;
}
