#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef bool Bool;
#define TRUE true
#define FALSE false

typedef uint8_t U8;
typedef int8_t I8;
#define U8_MAX UINT8_MAX
#define I8_MAX INT8_MAX
#define I8_MIN INT8_MIN

typedef uint16_t U16;
typedef int16_t I16;
#define U16_MAX UINT16_MAX
#define I16_MAX INT16_MAX
#define I16_MIN INT16_MIN

#undef UINT_MAX
#undef INT_MAX
#undef INT_MIN

typedef uintptr_t UInt;
typedef intptr_t Int;
#define UINT_MAX UINTPTR_MAX
#define INT_MAX INTPTR_MAX
#define INT_MIN INTPTR_MIN

#define UINT_FMT PRIuPTR
#define INT_FMT PRIiPTR
#define INT_FMTX PRIXPTR

static FILE *src;
static Bool emit = FALSE;

static void help(char const *name) {
  fprintf(stderr, "Usage: %s [options] <asmfile>\n\n", name);
  fprintf(stderr, "Options:\n\n");
  fprintf(stderr, "  -h, --help              Show this help message\n");
  fprintf(stderr,
          "  -o, --output            Specify output file (default: stdout)\n");
  fprintf(stderr, "  -l, --labellist <file>  Specify labellist file\n");
}

static void parse();

int main(int argc, char const *const *argv) {
  FILE *output = stdout;
  FILE *labellist = NULL;

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
    if ((strcmp(argv[argi], "-o") == 0) ||
        (strcmp(argv[argi], "--output") == 0)) {
      ++argi;
      if (argi == argc) {
        fprintf(stderr, "No output file specified\n");
        return EXIT_FAILURE;
      }
      output = fopen(argv[argi], "wb");
      if (!output) {
        fprintf(stderr, "Could not open output file: %s\n", argv[argi]);
        return EXIT_FAILURE;
      }
      continue;
    }
    if ((strcmp(argv[argi], "-l") == 0) ||
        (strcmp(argv[argi], "--labellist") == 0)) {
      ++argi;
      if (argi == argc) {
        fprintf(stderr, "No labellist file specified\n");
        return EXIT_FAILURE;
      }
      labellist = fopen(argv[argi], "wb");
      if (!labellist) {
        fprintf(stderr, "Could not open labellist file: %s\n", argv[argi]);
        return EXIT_FAILURE;
      }
      continue;
    }
    src = fopen(argv[argi], "rb");
    if (!src) {
      fprintf(stderr, "Could not open asm file: %s\n", argv[argi]);
      return EXIT_FAILURE;
    }
  }

  parse();
  emit = TRUE;
  rewind(src);
  parse();

  return EXIT_SUCCESS;
}

enum {
  TOK_EOE = 0x04,
  TOK_ERR = 0x256,
  TOK_NUM,
  TOK_ID,
  TOK_DIR,
  TOK_STR,
  TOK_REM, // ;
  TOK_SRA, // >>
  TOK_SRL, // ~>
  TOK_SLL, // <<
  TOK_AND, // &&
  TOK_OR,  // ||
  TOK_LTE, // <=
  TOK_GTE, // >=
  TOK_EQ,  // ==
  TOK_NEQ, // !=
};

static struct {
  char const *name;
  UInt type;
} const DIGRAPHS[] = {
    {"<<", TOK_SLL}, {"~>", TOK_SRL}, {">>", TOK_SRA},
    {"&&", TOK_AND}, {"||", TOK_OR},  {"<=", TOK_LTE},
    {">=", TOK_GTE}, {"==", TOK_EQ},  {"!=", TOK_NEQ},
};

typedef struct {
  char const *start;
  UInt len;
  UInt type;
  Int val;
  UInt line;
  UInt col;
} Tok;

static char *tokStr = NULL;
static UInt tokStrLen = 0;
static Tok tokStash = {NULL, 0, TOK_EOE, 0};

static Tok tokPeek(char const *str, UInt line) {
  if (!str) {
    tokStash.line = line;
    if (tokStash.type == TOK_EOE) {
      return tokStash;
    }
    str = tokStash.start;
  } else {
    tokStash.start = str;
  }
  tokStash.type = TOK_EOE;
  while (isspace((unsigned char)*str)) {
    ++str;
  }
  tokStash.col = str - tokStash.start + 1;
  if (*str == '\0') {
    return tokStash;
  }
  tokStash.start = str;
  if (*str == '"') {
    if (!tokStr) {
      tokStr = malloc(1024);
    }
    tokStrLen = 0;
    tokStr[tokStrLen] = '\0';
    ++str;
    while (*str != '"') {
      switch (*str) {
      case '\0':
        tokStash.type = TOK_ERR;
        tokStash.len = 0;
        return tokStash;
      case '\\':
        ++str;
        switch (*str) {
        case 'a':
          tokStr[tokStrLen++] = '\a';
          break;
        case 'b':
          tokStr[tokStrLen++] = '\b';
          break;
        case 'f':
          tokStr[tokStrLen++] = '\f';
          break;
        case 'n':
          tokStr[tokStrLen++] = '\n';
          break;
        case 'r':
          tokStr[tokStrLen++] = '\r';
          break;
        case 't':
          tokStr[tokStrLen++] = '\t';
          break;
        case 'v':
          tokStr[tokStrLen++] = '\v';
          break;
        case '\\':
          tokStr[tokStrLen++] = '\\';
          break;
        case '\'':
          tokStr[tokStrLen++] = '\'';
          break;
        case '"':
          tokStr[tokStrLen++] = '"';
          break;
        case '0':
          tokStr[tokStrLen++] = '\0';
          break;
        default:
          tokStash.type = TOK_ERR;
          tokStash.len = 0;
          return tokStash;
        }
      default:
        tokStr[tokStrLen++] = *str;
        break;
      }
      ++str;
    }
    ++str;
    tokStr[tokStrLen] = '\0';
    tokStash.type = TOK_STR;
    tokStash.len = str - tokStash.start;
    return tokStash;
  }
  if (*str == '\'') {
    ++str;
    switch (*str) {
    case '\0':
      tokStash.type = TOK_ERR;
      tokStash.len = 0;
      return tokStash;
    case '\\':
      ++str;
      switch (*str) {
      case 'a':
        tokStash.val = '\a';
        break;
      case 'b':
        tokStash.val = '\b';
        break;
      case 'f':
        tokStash.val = '\f';
        break;
      case 'n':
        tokStash.val = '\n';
        break;
      case 'r':
        tokStash.val = '\r';
        break;
      case 't':
        tokStash.val = '\t';
        break;
      case 'v':
        tokStash.val = '\v';
        break;
      case '\\':
        tokStash.val = '\\';
        break;
      case '\'':
        tokStash.val = '\'';
        break;
      case '"':
        tokStash.val = '"';
        break;
      case '0':
        tokStash.val = '\0';
        break;
      default:
        tokStash.type = TOK_ERR;
        tokStash.len = 0;
        return tokStash;
      }
    default:
      tokStash.val = (Int)(unsigned char)(*str);
      break;
    }
    ++str;
    if (*str != '\'') {
      tokStash.type = TOK_ERR;
      tokStash.len = 0;
      return tokStash;
    }
    ++str;
    tokStash.type = TOK_NUM;
    tokStash.len = 3;
    return tokStash;
  }
  if (isdigit((unsigned char)*str) || (*str == '$') || (*str == '%')) {
    if (*str == '%') {
      ++str;
      if ((*str != '0') && (*str != '1')) {
        // edge case, this is a modulus
        tokStash.type = '%';
        tokStash.len = 1;
        return tokStash;
      }
      while ((*str == '0') || (*str == '1')) {
        ++str;
      }
      tokStash.val = strtol(tokStash.start + 1, NULL, 2);
    } else if (*str == '$') {
      ++str;
      while (isxdigit((unsigned char)*str)) {
        ++str;
      }
      tokStash.val = strtol(tokStash.start + 1, NULL, 16);
    } else {
      while (isdigit((unsigned char)*str)) {
        ++str;
      }
      tokStash.val = strtol(tokStash.start, NULL, 10);
    }
    tokStash.len = str - tokStash.start;
    tokStash.type = TOK_NUM;
    return tokStash;
  }
  if (*str == '@') {
    ++str;
    while (isalnum((unsigned char)*str) || (*str == '_')) {
      ++str;
    }
    tokStash.len = str - tokStash.start;
    tokStash.type = TOK_DIR;
    return tokStash;
  }
  if (*str == ';') {
    ++str;
    while (*str != '\0') {
      ++str;
    }
    tokStash.len = str - tokStash.start;
    tokStash.type = TOK_REM;
    return tokStash;
  }
  if (isalpha((unsigned char)*str) || (*str == '_')) {
    ++str;
    while (isalnum((unsigned char)*str) || (*str == '_')) {
      ++str;
    }
    tokStash.len = str - tokStash.start;
    tokStash.type = TOK_ID;
    return tokStash;
  }
  for (UInt i = 0; i < (sizeof(DIGRAPHS) / sizeof(DIGRAPHS[0])); ++i) {
    char const *dg = DIGRAPHS[i].name;
    UInt len = strlen(dg);
    if (strncmp(str, dg, len) == 0) {
      tokStash.type = DIGRAPHS[i].type;
      tokStash.len = len;
      return tokStash;
    }
  }
  tokStash.type = (unsigned char)*str;
  tokStash.len = 1;
  return tokStash;
}

typedef struct Symbol Symbol;

struct Symbol {
  Symbol *next;
  char const *name;
  Int val;
};

static U16 symCount = 0;
static Symbol *symHead = NULL;

static Symbol *symAdd(char const *name, Int val) {
  Symbol *sym = malloc(sizeof(Symbol));
  sym->name = strdup(name);
  sym->val = val;
  sym->next = symHead;
  symHead = sym;
  ++symCount;
  return sym;
}

static Symbol const *symFind(char const *name, UInt namelen) {
  for (Symbol const *sym = symHead; sym; sym = sym->next) {
    if (strlen(sym->name) != namelen) {
      continue;
    }
    if (strncmp(sym->name, name, namelen) == 0) {
      return sym;
    }
  }
  return NULL;
}

static void parse() {
  UInt line = 1;
  static char *lineBuf = NULL;
  static size_t lineBufLen = 0;
  while (getline(&lineBuf, &lineBufLen, src) != -1) {
  }
}
