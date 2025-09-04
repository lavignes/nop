#ifndef UTF8_H
#define UTF8_H

#include "buf.h"

U32  utf8Decode(BufView buf, UInt* len);
UInt utf8Encode(BufView buf, U32 c);
UInt utf8Len(BufView buf);
void utf8Cat(Buf* buf, U32 c);

#endif // UTF8_H
