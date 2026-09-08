#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "asm.h"

static void help(char const *name) {
  fprintf(
      stderr,
      "Usage: %s [options] <asm-file>\n\n"
      "Options:\n\n"
      "  -o, --output <path>          Write output to file (default: stdout)\n"
      "  -d, --debug <path>           Emit debug symbols to file\n"
      "  -D, ----define <NAME=VALUE>  Predefine symbol\n"
      "  -h, --help                   Show this help message\n",
      name);
}

static FILE *outFile = NULL;
static char const *outFilePath = NULL;
static char const *symFilePath = NULL;

#define STACK_SIZE 32
static Lex STACK[STACK_SIZE];
static Lex *ls;

static Bool emit = FALSE;
static Bool defining = FALSE;
static char const *scope = NULL;
static U16 pc = 0;

static Macro *macros = NULL;
static UInt macrosLen = 0;
static UInt macrosCap = 0;

static FILE *openFile(char const *path, char const *mode);
static void closeFile(FILE *hnd);
static void pushFile(FILE *hnd, char const *path);

static void pass();
static void rewindPass();

int main(int argc, char const *const *argv) {
  if (argc < 2) {
    help(argv[0]);
    return EXIT_FAILURE;
  }
  outFile = stdout;
  for (int argi = 1; argi < argc; ++argi) {
    if ((strcmp(argv[argi], "-h") == 0) ||
        (strcmp(argv[argi], "--help") == 0)) {
      help(argv[0]);
      return EXIT_SUCCESS;
    }
    if ((strcmp(argv[argi], "-o") == 0) ||
        (strcmp(argv[argi], "--output") == 0)) {
      ++argi;
      if (argi == argc) {
        fprintf(stderr, "No output file specified\n");
        return EXIT_FAILURE;
      }
      outFilePath = argv[argi];
      continue;
    }
    if ((strcmp(argv[argi], "-d") == 0) ||
        (strcmp(argv[argi], "--debug") == 0)) {
      ++argi;
      if (argi == argc) {
        fprintf(stderr, "No symbol file specified\n");
        return EXIT_FAILURE;
      }
      symFilePath = argv[argi];
      continue;
    }
    FILE *hnd = openFile(argv[argi], "rb");
    pushFile(hnd, argv[argi]);
    ++argi;
    if (argi != argc) {
      fprintf(stderr, "Unexpected option: %s\n", argv[argi]);
      return EXIT_FAILURE;
    }
  }

  pass();
  rewindPass();
  emit = TRUE;
  pass();

  if (outFilePath) {
    outFile = openFile(outFilePath, "wb+");
  } else {
    outFilePath = "<stdout>";
  }

  closeFile(outFile);
  return EXIT_SUCCESS;
}

NORETURN void panic(char const *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  panicV(fmt, args);
  va_end(args);
}

NORETURN void panicV(char const *fmt, va_list args) {
  vfprintf(stderr, fmt, args);
  exit(EXIT_FAILURE);
}

NORETURN void fatal(char const *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  lexFatal(ls, fmt, args);
  va_end(args);
}

NORETURN void fatalLoc(Loc loc, char const *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  lexFatalLoc(ls, loc, fmt, args);
  va_end(args);
}

U8 peek() { return lexPeek(ls); }

void eat() { lexEat(ls); }

void expect(U8 tok) {
  U8 seen = peek();
  if (seen != tok) {
    fatal("Expected %s, but got %s\n", tokName(tok), tokName(seen));
  }
}

U16 getPC() { return pc; }

static void expectEOL() {
  U8 seen = peek();
  switch (peek()) {
  case TOK_EOF:
  case '\n':
    return;
  default:
    fatal("Expected end of line\n");
  }
}

static void pass() {
  while (peek() != TOK_EOF) {
    switch (peek()) {
    case '\n':
      eat();
      continue;
    case '*':
      eat();
      expect('=');
      eat();
      pc = exprEatSolvedU16();
      expectEOL();
      eat();
      continue;
    }
  }
}

static void rewindPass() {
  lexRewind(ls);
  macros = NULL;
  pc = 0;
  defining = FALSE;
  scope = NULL;
}

static FILE *openFile(char const *path, char const *mode) {
  FILE *hnd = fopen(path, mode);
  if (!hnd) {
    panic("Could not open file: %s\n", path);
  }
  return hnd;
}

static void closeFile(FILE *hnd) {
  if (fclose(hnd) == EOF) {
    panic("Failed to close file: %s\n", strerror(errno));
  }
}

static void pushFile(FILE *hnd, char const *path) {
  ++ls;
  if (ls == (STACK + STACK_SIZE)) {
    panic("Too many nested files\n");
  }
  lexFileInit(ls, path, hnd);
}
