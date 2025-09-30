#include <nop/syn.h>

#include <assert.h>

int main(int argc, char const* argv[]) {
    (void)argc;
    (void)argv;

    Toks ts = {0};
    toksInitView(&ts, VIEW("test.nop"), VIEW("\
        pkg Test;                             \
                                              \
        use U32;                              \
    "));

    Syn syn = {0};
    synInit(&syn, ts);

    Pkg pkg = {0};
    synParse(&syn, &pkg);

    return 0;
}
