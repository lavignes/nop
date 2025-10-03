#include <nop/fatal.h>

#include <stdio.h>
#include <stdlib.h>

NORETURN void fatalV(char const* fmt, va_list args) {
    vfprintf(stderr, fmt, args);
    exit(EXIT_FAILURE);
}

FORMAT(1) NORETURN void fatal(char const* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fatalV(fmt, args);
}
