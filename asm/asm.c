#include <stdlib.h>
#include <string.h>

#include "asm.h"

static void help(char const *name) {
  fprintf(stderr, "Usage: %s [options] <asm-file>\n\n", name);
  fprintf(stderr, "Options:\n\n");
  fprintf(stderr, "  -h, --help              Show this help message\n");
}

int main(int argc, char const *const *argv) {
  if (argc < 2) {
    help(argv[0]);
    return EXIT_FAILURE;
  }
  for (int argi = 1; argi < argc; ++argi) {
    if ((strcmp(argv[argi], "-h") == 0) ||
        (strcmp(argv[argi], "--help") == 0)) {
      help(argv[0]);
      return EXIT_SUCCESS;
    }
    fprintf(stderr, "Unexpected option: %s\n", argv[argi]);
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

NORETURN void fatal(char const *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  fatalV(fmt, args);
  va_end(args);
}

NORETURN void fatalV(char const *fmt, va_list args) {
  vfprintf(stderr, fmt, args);
  exit(EXIT_FAILURE);
}
