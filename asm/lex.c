#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "asm.h"

static U8 const ASCII_TO_PETSCII[256] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x14, 0x09, 0x0D, 0x0B,
    0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23,
    0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B,
    0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
    0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF, 0xD0, 0xD1, 0xD2, 0xD3,
    0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
    0x60, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B,
    0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
    0x58, 0x59, 0x5A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F, 0x80, 0x81, 0x82, 0x83,
    0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B,
    0x9C, 0x9D, 0x9E, 0x9F, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
    0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB0, 0xB1, 0xB2, 0xB3,
    0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
    0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB,
    0xCC, 0xCD, 0xCE, 0xCF, 0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7,
    0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE0, 0xE1, 0xE2, 0xE3,
    0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
    0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFB,
    0xFC, 0xFD, 0xFE, 0xFF,
};

static struct {
  U8 tok;
  char const *name;
} const TOK_NAMES[] = {
    {TOK_EOF, "end of file"},
    {'\n', "end of line"},

    {TOK_ID, "identifier"},
    {TOK_NUM, "number"},
    {TOK_STR, "string"},

    {TOK_CPU, "@CPU"},
    {TOK_ENCODING, "@ENCODING"},
    {TOK_DB, "@DB"},
    {TOK_DW, "@DW"},
    {TOK_DS, "@DS"},
    {TOK_INCLUDE, "@INCLUDE"},
    {TOK_INCBIN, "@INCBIN"},
    {TOK_IF, "@IF"},
    {TOK_ELSE, "@ELSE"},
    {TOK_END, "@END"},
    {TOK_MACRO, "@MACRO"},
    {TOK_REPEAT, "@REPEAT"},

    {TOK_DEFINED, "@DEFINED"},
    {TOK_STRLEN, "@STRLEN"},

    {TOK_ASL, "`<<`"},
    {TOK_ASR, "`>>`"},
    {TOK_LSR, "`~>`"},
    {TOK_LTE, "`<=`"},
    {TOK_GTE, "`>=`"},
    {TOK_EQ, "`==`"},
    {TOK_NEQ, "`!='"},
    {TOK_AND, "`&&`"},
    {TOK_OR, "`||`"},

    {TOK_X, "`X` register"},
    {TOK_Y, "`Y` register"},

    {TOK_ARG, "macro argument"},
    {TOK_ARGC, "macro argument count"},
    {TOK_SHIFT, "macro argument shift"},
};

char const *tokName(U8 tok) {
  for (UInt i = 0; i < (sizeof(TOK_NAMES) / sizeof(TOK_NAMES[0])); ++i) {
    if (TOK_NAMES[i].tok == tok) {
      return TOK_NAMES[i].name;
    }
  }
  char buf[16];
  snprintf(buf, sizeof(buf), "`%c`", isprint(tok) ? tok : '?');
  return intern(buf);
}

void locTokCat(LocTok **toks, UInt *len, UInt *cap, LocTok tok) {
  if (!*toks) {
    *len = 0;
    *cap = 8;
    *toks = malloc(sizeof(LocTok) * *cap);
  }
  if (*len == *cap) {
    *cap *= 2;
    *toks = realloc(*toks, sizeof(LocTok) * *cap);
  }
  (*toks)[*len] = tok;
  ++*len;
}

void repeatTokCat(RepeatTok **toks, UInt *len, UInt *cap, RepeatTok tok) {
  if (!*toks) {
    *len = 0;
    *cap = 8;
    *toks = malloc(sizeof(RepeatTok) * *cap);
  }
  if (*len == *cap) {
    *cap *= 2;
    *toks = realloc(*toks, sizeof(RepeatTok) * *cap);
  }
  (*toks)[*len] = tok;
  ++*len;
}

void macroTokCat(MacroTok **toks, UInt *len, UInt *cap, MacroTok tok) {
  if (!*toks) {
    *len = 0;
    *cap = 8;
    *toks = malloc(sizeof(MacroTok) * *cap);
  }
  if (*len == *cap) {
    *cap *= 2;
    *toks = realloc(*toks, sizeof(MacroTok) * *cap);
  }
  (*toks)[*len] = tok;
  ++*len;
}

static struct {
  char const *name;
  U8 tok;
} const DIRECTIVES[] = {
    {"CPU", TOK_CPU},         {"ENCODING", TOK_ENCODING},
    {"DB", TOK_DB},           {"DW", TOK_DW},
    {"DS", TOK_DS},           {"INCLUDE", TOK_INCLUDE},
    {"INCBIN", TOK_INCBIN},   {"IF", TOK_IF},
    {"ELSE", TOK_ELSE},       {"END", TOK_END},
    {"MACRO", TOK_MACRO},     {"REPEAT", TOK_REPEAT},

    {"DEFINED", TOK_DEFINED}, {"STRLEN", TOK_STRLEN},

    {"ARGC", TOK_ARGC},       {"SHIFT", TOK_SHIFT},
};

static struct {
  char const *name;
  U8 tok;
} const DIGRAPHS[] = {
    {"<<", TOK_ASL}, {">>", TOK_ASR}, {"~>", TOK_LSR},
    {"<=", TOK_LTE}, {">=", TOK_GTE}, {"==", TOK_EQ},
    {"!=", TOK_NEQ}, {"&&", TOK_AND}, {"||", TOK_OR},
};

void argsEnqueue(Arg **args, UInt *len, UInt *cap, Arg arg) {
  if (!*args) {
    *len = 0;
    *cap = 8;
    *args = malloc(sizeof(Arg) * *cap);
  }
  if (*len == *cap) {
    *cap *= 2;
    *args = realloc(*args, sizeof(Arg) * *cap);
  }
  (*args)[*len] = arg;
  ++*len;
}

void argsDequeue(Arg **args, UInt *len) {
  if (!*len) {
    return;
  }
  --*len;
  memmove(*args, *args + 1, *len * sizeof(Arg));
}

NORETURN void lexFatalLocV(Lex const *lex, Loc loc, char const *fmt,
                           va_list args) {
  lexWarnLocV(lex, loc, fmt, args);
  exit(EXIT_FAILURE);
}

NORETURN void lexFatalLoc(Lex const *lex, Loc loc, char const *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  lexFatalLocV(lex, loc, fmt, args);
  va_end(args);
}

NORETURN void lexFatalV(Lex const *lex, char const *fmt, va_list args) {
  lexWarnV(lex, fmt, args);
  exit(EXIT_FAILURE);
}

NORETURN void lexFatal(Lex const *lex, char const *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  lexFatalV(lex, fmt, args);
  va_end(args);
}

void lexWarnLocV(Lex const *lex, Loc loc, char const *fmt, va_list args) {
  switch (lex->kind) {
  case LEX_FILE:
  case LEX_IF_ELSE:
    fprintf(stderr, "%s:%" UINT_FMT ":%" UINT_FMT ": ", loc.name, loc.line,
            loc.col);
    break;
  case LEX_MACRO:
    fprintf(stderr,
            "%s:%" UINT_FMT ":%" UINT_FMT ": in macro \"%s\"\n\t%s:%" UINT_FMT
            ":%" UINT_FMT ": ",
            lex->loc.name, lex->loc.line, lex->loc.col, lex->macro.name,
            loc.name, loc.line, loc.col);
    break;
  case LEX_REPEAT:
    fprintf(stderr,
            "%s:%" UINT_FMT ":%" UINT_FMT ": at repeat index %" UINT_FMT
            "\n\t%s:%" UINT_FMT ":%" UINT_FMT ": ",
            lex->repeat.toks[lex->repeat.toksIdx].loc.name,
            lex->repeat.toks[lex->repeat.toksIdx].loc.line,
            lex->repeat.toks[lex->repeat.toksIdx].loc.col, lex->repeat.idx,
            loc.name, loc.line, loc.col);
    break;
  default:
    UNREACHABLE();
  }
  logsV(fmt, args);
}

void lexWarnLoc(Lex const *lex, Loc loc, char const *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  lexWarnLocV(lex, loc, fmt, args);
  va_end(args);
}

void lexWarnV(Lex const *lex, char const *fmt, va_list args) {
  switch (lex->kind) {
  case LEX_FILE:
    lexWarnLocV(lex, lex->loc, fmt, args);
    break;
  case LEX_MACRO:
    lexWarnLocV(lex, lex->macro.toks[lex->macro.toksIdx].loc, fmt, args);
    break;
  case LEX_REPEAT:
    lexWarnLocV(lex, lex->repeat.toks[lex->repeat.toksIdx].loc, fmt, args);
    break;
  case LEX_IF_ELSE:
    lexWarnLocV(lex, lex->ifElse.toks[lex->ifElse.toksIdx].loc, fmt, args);
    break;
  default:
    UNREACHABLE();
  }
}

void lexWarn(Lex const *lex, char const *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  lexWarnV(lex, fmt, args);
  va_end(args);
}

static NORETURN void fatalChar(Lex *lex, char const *fmt, ...) {
  fprintf(stderr, "%s:%" UINT_FMT ":%" UINT_FMT ": ", lex->loc.name,
          lex->file.charLine, lex->file.charCol);
  va_list args;
  va_start(args, fmt);
  panicV(fmt, args);
}

void strCat(char **dst, UInt *cap, char const *src) {
  if (!*dst) {
    *cap = 16;
    *dst = malloc(*cap);
    (*dst)[0] = 0;
  }
  UInt len = strlen(*dst);
  UInt srcLen = strlen(src);
  if ((len + srcLen + 1) >= *cap) {
    while ((len + srcLen + 1) >= *cap) {
      *cap *= 2;
    }
    *dst = realloc(*dst, *cap);
  }
  memcpy(*dst + len, src, srcLen + 1);
  (*dst)[len + srcLen] = 0;
}

static void pushChar(Lex *lex, U8 c) {
  strCat(&lex->file.txt, &lex->file.txtCap, (char[]){c, 0});
}

static U8 peekChar(Lex *lex) {
  if (lex->file.charStash) {
    return lex->file.charStash;
  }
  U8 c;
  int n = fread(&c, 1, 1, lex->file.hnd);
  if (n == 1) {
    lex->file.charStash = c;
    return c;
  }
  if (feof(lex->file.hnd)) {
    lex->file.charStash = TOK_EOF;
    return TOK_EOF;
  }
  int err = ferror(lex->file.hnd);
  fatalChar(lex, "Failed to read file: %s\n", strerror(err));
}

static void eatChar(Lex *lex) {
  U8 c = lex->file.charStash;
  lex->file.charStash = 0;
  ++lex->file.charCol;
  if (c == '\n') {
    ++lex->file.charLine;
    lex->file.charCol = 1;
  }
}

static U8 peekFile(Lex *lex) {
  if (lex->file.stash) {
    return lex->file.stash;
  }
repeek:
  while (TRUE) {
    U8 c = peekChar(lex);
    if ((c == TOK_EOF) || !isspace(c) || (c == '\n')) {
      break;
    }
    eatChar(lex);
  }
  if (peekChar(lex) == ';') {
    while (TRUE) {
      U8 c = peekChar(lex);
      if ((c == TOK_EOF) || (c == '\n')) {
        break;
      }
      eatChar(lex);
    }
  }
  lex->loc.line = lex->file.charLine;
  lex->loc.col = lex->file.charCol;
  if (peekChar(lex) == TOK_EOF) {
    eatChar(lex);
    lex->file.stash = TOK_EOF;
    return TOK_EOF;
  }
  if (peekChar(lex) == '\\') {
    eatChar(lex);
    if (peekChar(lex) == '\n') {
      eatChar(lex);
      goto repeek;
    }
    lex->file.stash = '\\';
    return '\\';
  }
  if (peekChar(lex) == '@') {
    eatChar(lex);
    // macro arg?
    U8 c = peekChar(lex);
    if (isdigit(c)) {
      for (; isdigit(c); c = peekChar(lex)) {
        pushChar(lex, toupper(c));
        eatChar(lex);
      }
      lex->file.num = atoi(lex->file.txt);
      lex->file.stash = TOK_ARG;
      return TOK_ARG;
    }
    // directive
    for (c = peekChar(lex); isalnum(c); c = peekChar(lex)) {
      pushChar(lex, toupper(c));
      eatChar(lex);
    }
    for (UInt i = 0; i < (sizeof(DIRECTIVES) / sizeof(DIRECTIVES[0])); ++i) {
      if (strcmp(lex->file.txt, DIRECTIVES[i].name) == 0) {
        lex->file.stash = DIRECTIVES[i].tok;
        return DIRECTIVES[i].tok;
      }
    }
    lexFatal(lex, "Unknown directive: @%s\n", lex->file.txt);
  }
  if (peekChar(lex) == '"') {
    eatChar(lex);
    while (TRUE) {
      U8 c = peekChar(lex);
      switch (c) {
      case TOK_EOF:
        fatalChar(lex, "Unexpected EOF in string literal\n");
      case '"':
        eatChar(lex);
        goto stringDone;
      case '\\':
        eatChar(lex);
        c = peekChar(lex);
        switch (c) {
        case TOK_EOF:
          fatalChar(lex, "Unexpected EOF in string literal\n");
        case 'n':
          pushChar(lex, '\n');
          break;
        case 'r':
          pushChar(lex, '\r');
          break;
        case 't':
          pushChar(lex, '\t');
          break;
        case '"':
          pushChar(lex, '"');
          break;
        case '\\':
          pushChar(lex, '\\');
          break;
        case '0':
          pushChar(lex, '\0');
          break;
        default:
          fatalChar(lex, "Unknown escape sequence: \\%c\n", c);
        }
        eatChar(lex);
        continue;
      default:
        pushChar(lex, c);
        eatChar(lex);
        continue;
      }
    }
  stringDone:
    if (getEncoding() == ENCODING_PETSCII) {
      UInt len = strlen(lex->file.txt);
      for (UInt i = 0; i < len; ++i) {
        lex->file.txt[i] = ASCII_TO_PETSCII[(U8)lex->file.txt[i]];
      }
    }
    lex->file.stash = TOK_STR;
    return TOK_STR;
  }
  if (peekChar(lex) == '\'') {
    eatChar(lex);
    U8 c = peekChar(lex);
    switch (c) {
    case TOK_EOF:
      fatalChar(lex, "Unexpected EOF in character literal\n");
    case '\\':
      eatChar(lex);
      c = peekChar(lex);
      switch (c) {
      case TOK_EOF:
        fatalChar(lex, "Unexpected EOF in character literal\n");
      case 'n':
        lex->file.num = '\n';
        break;
      case 'r':
        lex->file.num = '\r';
        break;
      case 't':
        lex->file.num = '\t';
        break;
      case '\'':
        lex->file.num = '\'';
        break;
      case '\\':
        lex->file.num = '\\';
        break;
      case '0':
        lex->file.num = '\0';
        break;
      default:
        fatalChar(lex, "Unknown escape sequence: \\%c\n", c);
      }
    default:
      if (getEncoding() == ENCODING_PETSCII) {
        lex->file.num = ASCII_TO_PETSCII[c];
      } else {
        lex->file.num = c;
      }
      break;
    }
    eatChar(lex);
    if (peekChar(lex) != '\'') {
      fatalChar(lex, "Expected closing quote for character literal\n");
    }
    eatChar(lex);
    lex->file.stash = TOK_NUM;
    return TOK_NUM;
  }
  U8 c = peekChar(lex);
  if (isdigit(c) || (c == '%') || (c == '$')) {
    I32 radix = 10;
    if (c == '%') {
      radix = 2;
      eatChar(lex);
      // edge case, this is a modulus
      c = peekChar(lex);
      if ((c != '0') && (c != '1')) {
        lex->file.stash = '%';
        return '%';
      }
    } else if (c == '$') {
      radix = 16;
      eatChar(lex);
      c = peekChar(lex);
    }
    while (TRUE) {
      // underscores in numbers
      if (c == '_') {
        eatChar(lex);
        c = peekChar(lex);
        continue;
      }
      if (!isalnum(c)) {
        break;
      }
      pushChar(lex, c);
      eatChar(lex);
      c = peekChar(lex);
    }
    lex->file.num = strtol(lex->file.txt, NULL, radix);
    lex->file.stash = TOK_NUM;
    return TOK_NUM;
  }
  while (TRUE) {
    if (c == TOK_EOF) {
      break;
    }
    if (isascii(c) && !isalnum(c) && (c != '_') && (c != '.')) {
      break;
    }
    pushChar(lex, c);
    eatChar(lex);
    c = peekChar(lex);
  }
  UInt len = strlen(lex->file.txt);
  // digraph?
  if (len == 0) {
    eatChar(lex);
    U8 nc = peekChar(lex);
    for (UInt i = 0; i < (sizeof(DIGRAPHS) / sizeof(DIGRAPHS[0])); ++i) {
      char const *dg = DIGRAPHS[i].name;
      if ((dg[0] == c) && (dg[1] == nc)) {
        eatChar(lex);
        lex->file.stash = DIGRAPHS[i].tok;
        return DIGRAPHS[i].tok;
      }
    }
  }
  // id len 1?
  if (len == 1) {
    U8 upper = toupper(lex->file.txt[0]);
    switch (upper) {
    case 'X':
      lex->file.stash = TOK_X;
      return TOK_X;
    case 'Y':
      lex->file.stash = TOK_Y;
      return TOK_Y;
    default:
      lex->file.stash = TOK_ID;
      return TOK_ID;
    }
  }
  // id?
  if (len != 0) {
    lex->file.stash = TOK_ID;
    return TOK_ID;
  }
  // else upper char
  lex->file.stash = toupper(c);
  return toupper(c);
}

static U8 peekMacro(Lex *lex) {
  if (lex->macro.toksIdx >= lex->macro.toksLen) {
    return TOK_EOF;
  }
  MacroTok const *tok = lex->macro.toks + lex->macro.toksIdx;
  U8 result;
  switch (tok->kind) {
  case MACRO_TOK:
    result = tok->tok;
    break;
  case MACRO_ID:
    result = TOK_ID;
    break;
  case MACRO_NUM:
    result = TOK_NUM;
    break;
  case MACRO_STR:
    result = TOK_STR;
    break;
  case MACRO_ARG: {
    if (lex->macro.argsIdx >= lex->macro.args[tok->num].bufLen) {
      lexFatalLoc(lex, tok->loc, "Argument %d is undefined\n", tok->num);
      return TOK_EOF;
    }
    tok = lex->macro.args[tok->num].buf + lex->macro.argsIdx;
    switch (tok->kind) {
    case MACRO_TOK:
      return tok->tok;
    case MACRO_ID:
      return TOK_ID;
    case MACRO_NUM:
      return TOK_NUM;
    case MACRO_STR:
      return TOK_STR;
    default:
      UNREACHABLE();
      return TOK_EOF;
    }
  }
  case MACRO_ARGC:
    result = TOK_NUM;
    break;
  case MACRO_SHIFT:
    if (lex->macro.argsLen == 0) {
      lexFatalLoc(lex, tok->loc, "No arguments to shift\n");
      return TOK_EOF;
    }
    result = '\n';
    break;
  default:
    UNREACHABLE();
    return TOK_EOF;
  }
  return result;
}

static U8 peekRepeat(Lex *lex) {
  if (lex->repeat.idx >= lex->repeat.cnt) {
    return TOK_EOF;
  }
  if (lex->repeat.toksIdx >= lex->repeat.toksLen) {
    return TOK_EOF;
  }
  RepeatTok const *tok = lex->repeat.toks + lex->repeat.toksIdx;
  switch (tok->kind) {
  case REPEAT_TOK:
    return tok->tok;
  case REPEAT_ID:
    return TOK_ID;
  case REPEAT_NUM:
  case REPEAT_IDX:
    return TOK_NUM;
  case REPEAT_STR:
    return TOK_STR;
  default:
    UNREACHABLE();
    return TOK_EOF;
  }
}

static U8 peekIfElse(Lex *lex) {
  if (lex->ifElse.toksIdx >= lex->ifElse.toksLen) {
    return TOK_EOF;
  }
  return lex->ifElse.toks[lex->ifElse.toksIdx].tok;
}

void lexFileInit(Lex *lex, char const *name, FILE *hnd) {
  lex->kind = LEX_FILE;
  lex->loc.name = name;
  lex->loc.line = 1;
  lex->loc.col = 1;
  lex->file.hnd = hnd;
  lex->file.stash = 0;
  lex->file.charStash = 0;
  lex->file.charLine = 1;
  lex->file.charCol = 1;
  lex->file.txt = NULL;
  lex->file.txtCap = 0;
  lex->file.num = 0;
  strCat(&lex->file.txt, &lex->file.txtCap, "");
}

void lexMacroInit(Lex *lex, Loc loc, char const *name, MacroTok const *toks,
                  UInt toksLen, Arg *args, UInt argsLen) {
  lex->kind = LEX_MACRO;
  lex->loc = loc;
  lex->macro.name = name;
  lex->macro.toks = toks;
  lex->macro.toksLen = toksLen;
  lex->macro.toksIdx = 0;
  lex->macro.args = args;
  lex->macro.argsLen = argsLen;
  lex->macro.argsIdx = 0;
}

void lexRepeatInit(Lex *lex, Loc loc, RepeatTok *toks, UInt toksLen, UInt cnt) {
  lex->kind = LEX_REPEAT;
  lex->loc = loc;
  lex->repeat.toks = toks;
  lex->repeat.toksLen = toksLen;
  lex->repeat.toksIdx = 0;
  lex->repeat.idx = 0;
  lex->repeat.cnt = cnt;
}

void lexIfElseInit(Lex *lex, Loc loc, LocTok *toks, UInt toksLen) {
  lex->kind = LEX_IF_ELSE;
  lex->loc = loc;
  lex->ifElse.toks = toks;
  lex->ifElse.toksLen = toksLen;
  lex->ifElse.toksIdx = 0;
}

void lexFini(Lex *lex) {
  switch (lex->kind) {
  case LEX_FILE:
    if (fclose(lex->file.hnd) == EOF) {
      int err = errno;
      fprintf(stderr, "%s:%" UINT_FMT ":%" UINT_FMT ": ", lex->loc.name,
              lex->file.charLine, lex->file.charCol);
      panic("Failed to close file: %s\n", strerror(err));
    }
    free(lex->file.txt);
    return;
  case LEX_MACRO:
    free(lex->macro.args);
    return;
  case LEX_REPEAT:
    free(lex->repeat.toks);
    return;
  case LEX_IF_ELSE:
    free(lex->ifElse.toks);
    return;
  }
}

U8 lexPeek(Lex *lex) {
  switch (lex->kind) {
  case LEX_FILE:
    return peekFile(lex);
  case LEX_MACRO:
    return peekMacro(lex);
  case LEX_REPEAT:
    return peekRepeat(lex);
  case LEX_IF_ELSE:
    return peekIfElse(lex);
  default:
    UNREACHABLE();
    return 0;
  }
}

void lexEat(Lex *lex) {
  switch (lex->kind) {
  case LEX_FILE:
    lex->file.stash = 0;
    lex->file.txt[0] = 0;
    return;
  case LEX_MACRO: {
    MacroTok const *tok = lex->macro.toks + lex->macro.toksIdx;
    switch (tok->kind) {
    case MACRO_SHIFT:
      argsDequeue(&lex->macro.args, &lex->macro.argsLen);
      break;
    case MACRO_ARG: {
      Arg *arg = lex->macro.args + tok->num;
      ++lex->macro.argsIdx;
      if (lex->macro.argsIdx < arg->bufLen) {
        return;
      }
      lex->macro.argsIdx = 0;
      break;
    }
    default:
      break;
    }
    ++lex->macro.toksIdx;
    break;
  }
  case LEX_REPEAT:
    ++lex->repeat.toksIdx;
    if (lex->repeat.toksIdx >= lex->repeat.toksLen) {
      lex->repeat.toksIdx = 0;
      ++lex->repeat.idx;
    }
    break;
  case LEX_IF_ELSE:
    ++lex->ifElse.toksIdx;
    break;
  default:
    UNREACHABLE();
    break;
  }
}

void lexRewind(Lex *lex) {
  lexEat(lex);
  switch (lex->kind) {
  case LEX_FILE:
    if (fseek(lex->file.hnd, lex->file.stash, SEEK_SET) != 0) {
      int err = errno;
      fprintf(stderr, "%s:%" UINT_FMT ":%" UINT_FMT ": ", lex->loc.name,
              lex->file.charLine, lex->file.charCol);
      panic("Failed to rewind file: %s\n", strerror(err));
    }
    break;
  default:
    UNREACHABLE();
    break;
  }
  lex->loc.line = 1;
  lex->loc.col = 1;
  lex->file.charStash = 0;
  lex->file.charLine = 1;
  lex->file.charCol = 1;
}

char const *lexTxt(Lex const *lex) {
  switch (lex->kind) {
  case LEX_FILE:
    return lex->file.txt;
  case LEX_MACRO: {
    MacroTok const *tok = lex->macro.toks + lex->macro.toksIdx;
    switch (tok->kind) {
    case MACRO_STR:
    case MACRO_ID:
      return tok->txt;
    case MACRO_ARG:
      tok = lex->macro.args[tok->num].buf + lex->macro.argsIdx;
      switch (tok->kind) {
      case MACRO_STR:
      case MACRO_ID:
        return tok->txt;
      default:
        UNREACHABLE();
        return NULL;
      }
    }
  case LEX_REPEAT: {
    RepeatTok const *tok = lex->repeat.toks + lex->repeat.toksIdx;
    switch (tok->kind) {
    case REPEAT_STR:
    case REPEAT_ID:
      return tok->txt;
    default:
      UNREACHABLE();
      return NULL;
    }
  }
  case LEX_IF_ELSE:
    return lex->ifElse.toks[lex->ifElse.toksIdx].txt;
  default:
    UNREACHABLE();
    return NULL;
  }
  }
}

I32 lexNum(Lex const *lex) {
  switch (lex->kind) {
  case LEX_FILE:
    return lex->file.num;
  case LEX_MACRO: {
    MacroTok const *tok = lex->macro.toks + lex->macro.toksIdx;
    switch (tok->kind) {
    case MACRO_NUM:
      return tok->num;
    case MACRO_ARG:
      tok = lex->macro.args[tok->num].buf + lex->macro.argsIdx;
      switch (tok->kind) {
      case MACRO_NUM:
        return tok->num;
      default:
        UNREACHABLE();
        return 0;
      }
    case MACRO_ARGC:
      return lex->macro.argsLen;
    default:
      UNREACHABLE();
      return 0;
    }
  }
  case LEX_REPEAT: {
    RepeatTok const *tok = lex->repeat.toks + lex->repeat.toksIdx;
    switch (tok->kind) {
    case REPEAT_NUM:
      return tok->num;
    case REPEAT_IDX:
      return lex->repeat.idx;
    default:
      UNREACHABLE();
      return 0;
    }
  }
  case LEX_IF_ELSE:
    return lex->ifElse.toks[lex->ifElse.toksIdx].num;
  default:
    UNREACHABLE();
    return 0;
  }
}

Loc lexLoc(Lex const *lex) {
  switch (lex->kind) {
  case LEX_FILE:
    return lex->loc;
  case LEX_MACRO:
    return lex->macro.toks[lex->macro.toksIdx].loc;
  case LEX_REPEAT:
    return lex->repeat.toks[lex->repeat.toksIdx].loc;
  case LEX_IF_ELSE:
    return lex->ifElse.toks[lex->ifElse.toksIdx].loc;
  default:
    UNREACHABLE();
    return (Loc){0};
  }
}

char const *lexLbl(Lex const *lex) {
  char const *txt = lexTxt(lex);
  UInt len = strlen(txt);
  char const *offset = memchr(txt, '.', len);
  if (!offset) {
    return intern(txt);
  }
  UInt scopeLen = offset - txt;
  UInt nameLen = len - scopeLen - 1;
  if (!nameLen) {
    lexFatal(lex, "Label name cannot be empty\n");
  }
  if (scopeLen > 0) {
    return intern(txt);
  }
  char const *scope = getScope();
  scopeLen = strlen(scope);
  char *buf = malloc(scopeLen + len + 1);
  memcpy(buf, scope, scopeLen);
  memcpy(buf + scopeLen, txt, len);
  buf[scopeLen + len] = 0;
  char const *lbl = intern(buf);
  free(buf);
  return lbl;
}
