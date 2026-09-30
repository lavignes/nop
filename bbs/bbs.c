#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/epoll.h>
#include <sys/fcntl.h>

#include "bbs.h"

#define MAX_EVENTS 128

static void help(char const *name) {
  fprintf(stderr,
          "Usage: %s [options]\n"
          "\n"
          "Options:\n"
          "\n"
          "  -p, --port <port>  Listen on port (default: 8080)\n"
          "  -h, --help         Show this help message\n",
          name);
}

static int setNonBlocking(int fd);
static int epollAdd(int epollFd, int fd);

int main(int argc, char const *const *argv) {
  U16 port = 8080;

  for (int argi = 1; argi < argc; ++argi) {
    if ((strcmp(argv[argi], "-h") == 0) ||
        (strcmp(argv[argi], "--help") == 0)) {
      help(argv[0]);
      return EXIT_SUCCESS;
    }
    if ((strcmp(argv[argi], "-p") == 0) ||
        (strcmp(argv[argi], "--port") == 0)) {
      ++argi;
      if (argi == argc) {
        fprintf(stderr, "No port number specified\n");
        return EXIT_FAILURE;
      }
      int p = atoi(argv[argi]);
      if ((p <= 0) || (p > U16_MAX)) {
        fprintf(stderr, "Invalid port number: %s\n", argv[argi]);
        return EXIT_FAILURE;
      }
      port = (U16)p;
      continue;
    }
  }

  int serverFd = socket(AF_INET, SOCK_STREAM, 0);
  if (serverFd < 0) {
    perror("Failed to create server socket");
    return EXIT_FAILURE;
  }
  int opt = 1;
  if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    perror("Failed to set socket options");
    close(serverFd);
    return EXIT_FAILURE;
  }

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  if (bind(serverFd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("Failed to bind server socket");
    close(serverFd);
    return EXIT_FAILURE;
  }

  if (listen(serverFd, SESSIONS_MAX) < 0) {
    perror("Failed to listen on server socket");
    close(serverFd);
    return EXIT_FAILURE;
  }

  if (setNonBlocking(serverFd) < 0) {
    close(serverFd);
    return EXIT_FAILURE;
  }

  int epollFd = epoll_create1(0);
  if (epollFd < 0) {
    perror("Failed to create epoll instance");
    close(serverFd);
    return EXIT_FAILURE;
  }

  if (epollAdd(epollFd, serverFd) < 0) {
    close(serverFd);
    close(epollFd);
    return EXIT_FAILURE;
  }

  sessionInit();

  while (TRUE) {
    struct epoll_event events[MAX_EVENTS];
    int n = epoll_wait(epollFd, events, MAX_EVENTS, -1);
    if (n < 0) {
      perror("Failed to wait for epoll events");
      break;
    }

    for (int i = 0; i < n; ++i) {
      if (events[i].data.fd == serverFd) {
        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        int clientFd =
            accept(serverFd, (struct sockaddr *)&clientAddr, &clientLen);
        if (clientFd < 0) {
          perror("Failed to accept client connection");
          continue;
        }

        Session *session = sessionAlloc(clientFd, &clientAddr);
        if (!session) {
          fprintf(stderr, "Failed to allocate session for client\n");
          close(clientFd);
          continue;
        }

        int opt = SESSION_TX_CAP;
        if (setsockopt(clientFd, SOL_SOCKET, SO_SNDBUF, &opt, sizeof(opt)) <
            0) {
          perror("Failed to set send buffer size");
          sessionFree(session);
          close(clientFd);
          continue;
        }
        opt = SESSION_RX_CAP;
        if (setsockopt(clientFd, SOL_SOCKET, SO_RCVBUF, &opt, sizeof(opt)) <
            0) {
          perror("Failed to set receive buffer size");
          sessionFree(session);
          close(clientFd);
          continue;
        }

        if (setNonBlocking(clientFd) < 0) {
          sessionFree(session);
          close(clientFd);
          continue;
        }
        if (epollAdd(epollFd, clientFd) < 0) {
          sessionFree(session);
          close(clientFd);
          continue;
        }

      } else {
      }
    }
  }

  close(serverFd);
  close(epollFd);
  return EXIT_SUCCESS;
}

static int setNonBlocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    perror("Failed to get file descriptor flags");
    return -1;
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    perror("Failed to set file descriptor to non-blocking");
    return -1;
  }
  return 0;
}

static int epollAdd(int epollFd, int fd) {
  struct epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.fd = fd;
  if (epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &ev) < 0) {
    perror("Failed to add file descriptor to epoll");
    return -1;
  }
  return 0;
}
