#ifndef NUM_H
#define NUM_H

#include <float.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

typedef bool Bool;

typedef uint8_t U8;
typedef int8_t  I8;

#define U8_MAX UINT8_MAX
#define I8_MAX INT8_MAX
#define I8_MIN INT8_MIN
#define U8_FMT "%" PRIu8
#define I8_FMT "%" PRId8

typedef uint16_t U16;
typedef int16_t  I16;

#define U16_MAX UINT16_MAX
#define I16_MAX INT16_MAX
#define I16_MIN INT16_MIN
#define U16_FMT "%" PRIu16
#define I16_FMT "%" PRIi16

typedef uint32_t U32;
typedef int32_t  I32;

#define U32_MAX UINT32_MAX
#define I32_MAX INT32_MAX
#define I32_MIN INT32_MIN
#define U32_FMT "%" PRIu32
#define I32_FMT "%" PRIi32

typedef uint64_t U64;
typedef int64_t  I64;

#define U64_MAX UINT64_MAX
#define I64_MAX INT64_MAX
#define I64_MIN INT64_MIN
#define U64_FMT "%" PRIu64
#define I64_FMT "%" PRIi64

typedef uintptr_t UInt;
typedef intptr_t  Int;

#define UINT_MAX UINTPTR_MAX
#define INT_MAX  INTPTR_MAX
#define INT_MIN  INTPTR_MIN
#define UINT_FMT "%" PRIuPTR
#define INT_FMT  "%" PRIiPTR

typedef float F32;

#define F32_MAX     FLT_MAX
#define F32_MIN     FLT_MIN
#define F32_EPSILON FLT_EPSILON
#define F32_FMT     "%f"

typedef double F64;

#define F64_MAX     DBL_MAX
#define F64_MIN     DBL_MIN
#define F64_EPSILON DBL_EPSILON
#define F64_FMT     "%lf"

typedef enum {
    INT_I8,
    INT_I16,
    INT_I32,
    INT_I64,
    INT_INT,

    INT_U8,
    INT_U16,
    INT_U32,
    INT_U64,
    INT_UINT,

    INT_INTEGER
} IntKind;

typedef enum { FLOAT_F32, FLOAT_F64, FLOAT_FLOAT } FloatKind;

#endif // NUM_H
