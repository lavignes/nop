#ifndef BBS_H
#define BBS_H

#include <netinet/in.h>

#include "abi.h"

typedef struct {
  U8 bytes[16];
} Guid;

int guidGen(Guid *guid);
UInt guidHash(Guid const *guid);

#define SESSION_TX_CAP 4096
#define SESSION_RX_CAP 4096

#define SESSIONS_MAX 256

typedef struct {
  int fd;
  Guid *guid;
  struct sockaddr_in addr;
  U8 tx[SESSION_TX_CAP];
  UInt txLen;
  U8 rx[SESSION_RX_CAP];
  UInt rxLen;
} Session;

void sessionInit();
Session *sessionAlloc(int fd, struct sockaddr_in const *addr);
void sessionFree(Session *session);

Session *sessionFindById(int fd);
Session *sessionFindByGuid(Guid const *guid);

#endif // BBS_H
