#ifndef SYN_H
#define SYN_H

#include <nop/tok.h>

typedef struct {
    View pkg;
    View id;
} Name;

typedef struct Type Type;

typedef struct {
    Type* items;
    UInt  len;
} TypeView;

typedef struct {
    TypeView view;
    UInt     cap;
} TypeBuf;

typedef struct {
    Name  name;
    Type* type;
} Field;

typedef struct {
    Field* items;
    UInt   len;
} FieldView;

typedef struct {
    FieldView view;
    UInt      cap;
} FieldBuf;

typedef struct {
    Name  name;
    Type* type;
} Param;

typedef struct {
    Param* items;
    UInt   len;
} ParamView;

typedef struct {
    ParamView view;
    UInt      cap;
} ParamBuf;

typedef enum {
    TYPE_NIL,
    TYPE_NEVER,
    TYPE_BOOL,
    TYPE_CHAR,
    TYPE_U8,
    TYPE_I8,
    TYPE_U16,
    TYPE_I16,
    TYPE_U32,
    TYPE_I32,
    TYPE_U64,
    TYPE_I64,
    TYPE_UINT,
    TYPE_INT,
    TYPE_F32,
    TYPE_F64,
    TYPE_FLOAT,
    TYPE_INTEGER,
    TYPE_PTR,
    TYPE_MUT,
    TYPE_FN,
    TYPE_RECORD,
    TYPE_UNION,
    TYPE_ENUM,
} TypeKind;

struct Type {
    TypeKind kind;
    union {
        struct {
            Type* type;
        } ptr;

        struct {
            Type* type;
        } mut;

        struct {
            TypeView params;
            Type*    ret;
        } fn;

        struct {
            FieldView fields;
        } record;
    };
};

typedef struct Scope Scope;

struct Scope {
    Scope* parent;
};

typedef struct {
    Scope* items;
    UInt   len;
} ScopeView;

typedef struct {
    ScopeView view;
    UInt      cap;
} ScopeBuf;

typedef struct {
    Scope* scope;
} Fn;

typedef enum {
    ITEM_FN,
} ItemKind;

typedef struct {
    ItemKind kind;
    Pos      pos;
    union {
        Fn fn;
    };
} Item;

typedef struct {
    Item* items;
    UInt  len;
} ItemView;

typedef struct {
    ItemView view;
    UInt     cap;
} ItemBuf;

void itemBufAdd(ItemBuf* buf, Item item);
void itemBufFini(ItemBuf* buf);

typedef struct {
    View     name;
    ItemBuf  items;
    ScopeBuf scopes;
} Pkg;

#define STREAM_RECURSION_CAP 16

typedef struct {
    Toks  tsstack[STREAM_RECURSION_CAP];
    Toks* ts;
} Prs;

void prsInit(Prs* prs, Toks ts);
Pkg  prsPkg(Prs* prs);

#endif // SYN_H
