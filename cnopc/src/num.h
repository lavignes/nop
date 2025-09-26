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
    NUM_U8,
    NUM_I8,
    NUM_U16,
    NUM_I16,
    NUM_U32,
    NUM_I32,
    NUM_I64,
    NUM_U64,
    NUM_INT,
    NUM_UINT,
    NUM_F32,
    NUM_F64,
} NumKind;

typedef struct {
    NumKind kind;

    union {
        Bool b;
        U8   u8;
        I8   i8;
        U16  u16;
        I16  i16;
        U32  u32;
        I32  i32;
        U64  u64;
        I64  i64;
        UInt u;
        Int  i;
        F32  f32;
        F64  f64;
    };

} Num;

#endif // NUM_H
