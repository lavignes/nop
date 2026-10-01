#ifndef BBS_H
#define BBS_H

#include <netinet/in.h>

#include "abi.h"

typedef struct {
  U8 bytes[16];
} Guid;

int guidGen(Guid *guid);
UInt guidHash(Guid const *guid);

#define RING_BUF_CAP 4096

typedef struct {
  U8 buf[RING_BUF_CAP];
  UInt head;
  UInt tail;
} RingBuf;

void ringBufInit(RingBuf *rb);
UInt ringBufLen(RingBuf const *rb);
UInt ringBufAvail(RingBuf const *rb);
UInt ringBufRead(RingBuf *rb, U8 *data, UInt len);
UInt ringBufWrite(RingBuf *rb, U8 const *data, UInt len);
UInt ringBufPeek(RingBuf const *rb, U8 *data, UInt len);
UInt ringBufSkip(RingBuf *rb, UInt len);

#define SESSION_MAX 256

typedef struct {
  int fd;
  Guid *guid;
  struct sockaddr_in addr;
  RingBuf tx;
  RingBuf rx;
} Session;

void sessionInit();
Session *sessionAlloc(int fd, struct sockaddr_in const *addr);
void sessionFree(Session *session);

Session *sessionFindByFd(int fd);
Session *sessionFindByGuid(Guid const *guid);

#endif // BBS_H
