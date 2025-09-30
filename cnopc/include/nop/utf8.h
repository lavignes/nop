#ifndef UTF8_H
#define UTF8_H

#include <nop/buf.h>

U32  utf8Decode(View view, UInt* len);
UInt utf8Encode(View view, U32 c);
UInt utf8Len(View view);
void utf8Cat(Buf* buf, U32 c);

#endif // UTF8_H
