#ifndef FATAL_H
#define FATAL_H

#include <stdarg.h>

#define NORETURN
#ifdef __has_attribute
#if __has_attribute(noreturn)
#undef NORETURN
#define NORETURN __attribute__((noreturn))
#endif
#endif

#define FORMAT(n)
#ifdef __has_attribute
#if __has_attribute(format)
#undef FORMAT
#define FORMAT(n) __attribute__((format(printf, (n), (n + 1))))
#endif
#endif

#ifdef __builtin_unreachable
#define UNREACHABLE() __builtin_unreachable()
#else
#define UNREACHABLE()                                                          \
    do {                                                                       \
        fatal("%s:%d: is not meant to be reached\n", __FILE__, __LINE__);      \
    } while (0)
#endif

#define TODO(msg)                                                              \
    do {                                                                       \
        fatal("%s:%d: not implemented: %s\n", __FILE__, __LINE__, msg);        \
    } while (0)

NORETURN void fatalv(char const* fmt, va_list args);
FORMAT(1) NORETURN void fatal(char const* fmt, ...);

#endif // FATAL_H
