#ifndef ABI_H
#define ABI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef bool Bool;
#define TRUE true
#define FALSE false

typedef uint8_t U8;
typedef int8_t I8;
#define U8_MAX UINT8_MAX
#define I8_MAX INT8_MAX
#define I8_MIN INT8_MIN

typedef uint16_t U16;
typedef int16_t I16;
#define U16_MAX UINT16_MAX
#define I16_MAX INT16_MAX
#define I16_MIN INT16_MIN

#undef UINT_MAX
#undef INT_MAX
#undef INT_MIN

typedef uintptr_t UInt;
typedef intptr_t Int;
#define UINT_MAX UINTPTR_MAX
#define INT_MAX INTPTR_MAX
#define INT_MIN INTPTR_MIN

#define UINT_FMT PRIuPTR
#define INT_FMT PRIiPTR
#define INT_FMTX PRIXPTR

#endif // ABI_H
