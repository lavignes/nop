#include <string.h>

#include <sys/random.h>

#include "bbs.h"

int guidGen(Guid *guid) {
  if (getrandom(guid->bytes, sizeof(guid->bytes), 0) < 0) {
    return -1;
  }
  // Mark as UUIDv4
  guid->bytes[6] = (guid->bytes[6] & 0x0F) | 0x40;
  guid->bytes[8] = (guid->bytes[8] & 0x3F) | 0x80;
  return 0;
}

UInt guidHash(Guid const *guid) {
  UInt hash = 5381;
  for (UInt i = 0; i < sizeof(guid->bytes); ++i) {
    hash = ((hash << 5) + hash) + guid->bytes[i];
  }
  return hash;
}

typedef struct {
  Guid guid;
  Session *session;
} SessionGuid;

SessionGuid sessionGuids[SESSION_MAX];
Session sessions[SESSION_MAX];
UInt sessionsLen;

void sessionInit() {
  for (UInt i = 0; i < SESSION_MAX; ++i) {
    sessions[i].fd = -1;
    sessionGuids[i].session = NULL;
  }
  sessionsLen = 0;
}

Session *sessionAlloc(int fd, struct sockaddr_in const *addr) {
  if (sessionsLen >= SESSION_MAX) {
    return NULL;
  }

  Session *session = NULL;
  UInt hash = ((UInt)fd) % SESSION_MAX;
  for (UInt i = 0; i < SESSION_MAX; ++i) {
    UInt idx = (hash + i) % SESSION_MAX;
    session = sessions + idx;
    if (session->fd < 0) {
      session->fd = fd;
      memcpy(&session->addr, addr, sizeof(struct sockaddr_in));
      ringBufInit(&session->tx);
      ringBufInit(&session->rx);
    }
  }

  Guid guid;
  if (guidGen(&guid) < 0) {
    return NULL;
  }
  hash = guidHash(&guid) % SESSION_MAX;
  for (UInt i = 0; i < SESSION_MAX; ++i) {
    UInt idx = (hash + i) % SESSION_MAX;
    SessionGuid *sg = sessionGuids + idx;
    if (sg->session == NULL) {
      sg->session = session;
      memcpy(&sg->guid, &guid, sizeof(Guid));
      session->guid = &sg->guid;
      break;
    }
  }

  ++sessionsLen;
  return session;
}

void sessionFree(Session *session) {
  UInt hash = guidHash(session->guid) % SESSION_MAX;
  for (UInt i = 0; i < SESSION_MAX; ++i) {
    UInt idx = (hash + i) % SESSION_MAX;
    SessionGuid *sg = sessionGuids + idx;
    if (sg->session == session) {
      sg->session = NULL;
      break;
    }
  }
  session->fd = -1;

  --sessionsLen;
}

Session *sessionFindByFd(int fd) {
  UInt hash = ((UInt)fd) % SESSION_MAX;
  for (UInt i = 0; i < SESSION_MAX; ++i) {
    UInt idx = (hash + i) % SESSION_MAX;
    Session *session = sessions + idx;
    if (sessions->fd == fd) {
      return session;
    }
  }
  return NULL;
}

Session *sessionFindByGuid(Guid const *guid) {
  UInt hash = guidHash(guid) % SESSION_MAX;
  for (UInt i = 0; i < SESSION_MAX; ++i) {
    UInt idx = (hash + i) % SESSION_MAX;
    SessionGuid *sg = sessionGuids + idx;
    if (memcmp(&sg->guid, guid, sizeof(Guid)) == 0) {
      return sg->session;
    }
  }
  return NULL;
}
