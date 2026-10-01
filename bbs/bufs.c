#include "bbs.h"

void ringBufInit(RingBuf *rb) {
  rb->head = 0;
  rb->tail = 0;
}

UInt ringBufLen(RingBuf const *rb) {
  if (rb->head >= rb->tail) {
    return rb->head - rb->tail;
  } else {
    return RING_BUF_CAP - (rb->tail - rb->head);
  }
}

UInt ringBufAvail(RingBuf const *rb) { return RING_BUF_CAP - ringBufLen(rb); }

UInt ringBufRead(RingBuf *rb, U8 *data, UInt len) {
  UInt avail = ringBufLen(rb);
  if (len > avail) {
    len = avail;
  }
  for (UInt i = 0; i < len; ++i) {
    data[i] = rb->buf[rb->tail];
    rb->tail = (rb->tail + 1) % RING_BUF_CAP;
  }
  return len;
}

UInt ringBufWrite(RingBuf *rb, U8 const *data, UInt len) {
  UInt avail = ringBufAvail(rb);
  if (len > avail) {
    len = avail;
  }
  for (UInt i = 0; i < len; ++i) {
    rb->buf[rb->head] = data[i];
    rb->head = (rb->head + 1) % RING_BUF_CAP;
  }
  return len;
}

UInt ringBufPeek(RingBuf const *rb, U8 *data, UInt len) {
  UInt avail = ringBufLen(rb);
  if (len > avail) {
    len = avail;
  }
  UInt tail = rb->tail;
  for (UInt i = 0; i < len; ++i) {
    data[i] = rb->buf[tail];
    tail = (tail + 1) % RING_BUF_CAP;
  }
  return len;
}

UInt ringBufSkip(RingBuf *rb, UInt len) {
  UInt avail = ringBufLen(rb);
  if (len > avail) {
    len = avail;
  }
  rb->tail = (rb->tail + len) % RING_BUF_CAP;
  return len;
}
