#ifndef INTS_H
#define INTS_H

#include <stdbool.h>
#include <stdint.h>

typedef bool Bool;

typedef uint8_t U8;
typedef int8_t  I8;

#define U8_MAX UINT8_MAX
#define I8_MAX INT8_MAX
#define I8_MIN INT8_MIN

typedef uint16_t U16;
typedef int16_t  I16;

#define U16_MAX UINT16_MAX
#define I16_MAX INT16_MAX
#define I16_MIN INT16_MIN

typedef uint32_t U32;
typedef int32_t  I32;

#define U32_MAX UINT32_MAX
#define I32_MAX INT32_MAX
#define I32_MIN INT32_MIN

typedef uint64_t U64;
typedef int64_t  I64;

#define U64_MAX UINT64_MAX
#define I64_MAX INT64_MAX
#define I64_MIN INT64_MIN

typedef uintptr_t UInt;
typedef intptr_t  Int;

#define UINT_MAX UINTPTR_MAX
#define INT_MAX  INTPTR_MAX
#define INT_MIN  INTPTR_MIN

#endif // INTS_H
