#include <nop/syn.h>

#include <assert.h>

int main() {
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
