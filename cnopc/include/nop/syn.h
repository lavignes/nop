#ifndef SYN_H
#define SYN_H

#include <nop/tok.h>

typedef struct {
} SynNode;

typedef struct {
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

#define STREAM_RECURSION_CAP 16

typedef struct {
    ItemBuf items;
} Pkg;

typedef struct {
    Toks  tsstack[STREAM_RECURSION_CAP];
    Toks* ts;
} Syn;

void synInit(Syn* syn, Toks ts);
void synParse(Syn* syn, Pkg* pkg);

#endif // SYN_H
