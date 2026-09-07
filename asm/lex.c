#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "asm.h"

static struct {
  char const *name;
  U8 tok;
} const DIRECTIVES[] = {
    {"DB", TOK_DB},           {"DW", TOK_DW},         {"DS", TOK_DS},
    {"INCLUDE", TOK_INCLUDE}, {"INCBIN", TOK_INCBIN}, {"IF", TOK_IF},
    {"ELSE", TOK_ELSE},       {"END", TOK_END},       {"MACRO", TOK_MACRO},

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

void argsEnqueue(MacroArg **args, UInt *len, UInt *cap, MacroArg arg) {
  if (!*args) {
    *len = 0;
    *cap = 8;
    *args = malloc(sizeof(MacroArg) * *cap);
  }
  if (*len == *cap) {
    *cap *= 2;
    *args = realloc(*args, sizeof(MacroArg) * *cap);
  }
  (*args)[*len] = arg;
  ++*len;
}

void argsDequeue(MacroArg **args, UInt *len) {
  if (!*len) {
    return;
  }
  --*len;
  memmove(*args, *args + 1, *len * sizeof(MacroArg));
}

static NORETURN void lexFatalLocV(Lex *lex, Loc loc, char const *fmt,
                                  va_list args) {
  switch (lex->kind) {
  case LEX_FILE:
  case LEX_IF_ELSE:
    fprintf(stderr, "%s:%" UINT_FMT ":%" UINT_FMT ": ", loc.id, loc.line,
            loc.col);
    break;
  case LEX_MACRO:
    fprintf(stderr,
            "%s:%" UINT_FMT ":%" UINT_FMT ": in macro %s\n\t%" UINT_FMT
            ":%" UINT_FMT ": ",
            lex->macro.name, lex->loc.line, lex->loc.col, loc.id, loc.line,
            loc.col);
    break;
  default:
    UNREACHABLE();
  }
  fatalV(fmt, args);
}

static NORETURN void lexFatalLoc(Lex *lex, Loc loc, char const *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  lexFatalLocV(lex, loc, fmt, args);
  va_end(args);
}

static NORETURN void lexFatalV(Lex *lex, char const *fmt, va_list args) {
  switch (lex->kind) {
  case LEX_FILE:
    lexFatalLocV(lex, lex->loc, fmt, args);
  case LEX_MACRO:
    lexFatalLocV(lex, lex->macro.toks[lex->macro.toksIdx].loc, fmt, args);
  case LEX_IF_ELSE:
    lexFatalLocV(lex, lex->ifElse.toks[lex->ifElse.toksIdx].loc, fmt, args);
  default:
    UNREACHABLE();
  }
}

static NORETURN void lexFatal(Lex *lex, char const *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  lexFatalV(lex, fmt, args);
  va_end(args);
}

static NORETURN void fatalChar(Lex *lex, char const *fmt, ...) {
  fprintf(stderr, "%s:%" UINT_FMT ":%" UINT_FMT ": ", lex->loc.id,
          lex->file.charLine, lex->file.charCol);
  va_list args;
  va_start(args, fmt);
  fatalV(fmt, args);
}

void strPush(char **dst, UInt *cap, char c) {
  if (*cap == 0) {
    *cap = 16;
    *dst = malloc(*cap);
    (*dst)[0] = 0;
  }
  UInt len = strlen(*dst);
  if (len + 1 == *cap) {
    *cap *= 2;
    *dst = realloc(*dst, *cap);
  }
  (*dst)[len] = c;
  (*dst)[len + 1] = 0;
}

void strCat(char **dst, UInt *cap, char const *src) {
  if (*cap == 0) {
    *cap = 16;
    *dst = malloc(*cap);
    (*dst)[0] = 0;
  }
  UInt len = strlen(*dst);
  UInt srcLen = strlen(src);
  if (len + srcLen + 1 >= *cap) {
    while (len + srcLen + 1 >= *cap) {
      *cap *= 2;
    }
    *dst = realloc(*dst, *cap);
  }
  memcpy(*dst + len, src, srcLen + 1);
}

static void pushChar(Lex *lex, U8 c) {
  strPush(&lex->file.txt, &lex->file.txtCap, c);
}

static U8 peek(Lex *lex) {
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

static void eat(Lex *lex) {
  lex->file.charStash = 0;
  ++lex->file.charCol;
  if (lex->file.charStash == '\n') {
    ++lex->file.charLine;
    lex->file.charCol = 1;
  }
}

static U8 peekFile(Lex *lex) {
  if (lex->file.stash) {
    return lex->file.stash;
  }
  while (TRUE) {
    U8 c = peek(lex);
    if ((c == TOK_EOF) || !isspace(c) || (c == '\n')) {
      break;
    }
    eat(lex);
  }
  if (peek(lex) == ';') {
    while (TRUE) {
      U8 c = peek(lex);
      if ((c == TOK_EOF) || (c == '\n')) {
        break;
      }
      eat(lex);
    }
  }
  lex->loc.line = lex->file.charLine;
  lex->loc.col = lex->file.charCol;
  if (peek(lex) == TOK_EOF) {
    eat(lex);
    lex->file.stash = TOK_EOF;
    return TOK_EOF;
  }
  if (peek(lex) == '\\') {
    if (peek(lex) == '\n') {
      eat(lex);
      return peek(lex); // yuck
    }
    lex->file.stash = '\\';
    return '\\';
  }
  if (peek(lex) == '@') {
    eat(lex);
    // macro arg?
    U8 c = peek(lex);
    if (isdigit(c)) {
      for (; isdigit(c); c = peek(lex)) {
        pushChar(lex, toupper(c));
        eat(lex);
      }
      lex->file.num = atoi(lex->file.txt);
      lex->file.stash = TOK_ARG;
      return TOK_ARG;
    }
    // directive
    for (c = peek(lex); isalnum(c); c = peek(lex)) {
      pushChar(lex, toupper(c));
      eat(lex);
    }
    for (UInt i = 0; i < (sizeof(DIRECTIVES) / sizeof(DIRECTIVES[0])); ++i) {
      if (strcmp(lex->file.txt, DIRECTIVES[i].name) == 0) {
        lex->file.stash = DIRECTIVES[i].tok;
        return DIRECTIVES[i].tok;
      }
    }
    lexFatal(lex, "Unknown directive: @%s\n", lex->file.txt);
  }
  if (peek(lex) == '"') {
    eat(lex);
    while (TRUE) {
      U8 c = peek(lex);
      switch (c) {
      case TOK_EOF:
        fatalChar(lex, "Unexpected EOF in string literal\n");
      case '"':
        eat(lex);
        goto stringDone;
      case '\\':
        eat(lex);
        c = peek(lex);
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
        eat(lex);
        continue;
      default:
        pushChar(lex, c);
        eat(lex);
        continue;
      }
    }
  stringDone:
    lex->file.stash = TOK_STR;
    return TOK_STR;
  }
  if (peek(lex) == '\'') {
    eat(lex);
    U8 c = peek(lex);
    switch (c) {
    case TOK_EOF:
      fatalChar(lex, "Unexpected EOF in character literal\n");
    case '\\':
      eat(lex);
      c = peek(lex);
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
      lex->file.num = c;
      break;
    }
    eat(lex);
    if (peek(lex) != '\'') {
      fatalChar(lex, "Expected closing quote for character literal\n");
    }
    eat(lex);
    lex->file.stash = TOK_NUM;
    return TOK_NUM;
  }
  U8 c = peek(lex);
  if (isdigit(c) || (c == '%') || (c == '$')) {
    I32 radix = 10;
    if (c == '%') {
      radix = 2;
      eat(lex);
      // edge case, this is a modulus
      c = peek(lex);
      if ((c != '0') && (c != '1')) {
        lex->file.stash = '%';
        return '%';
      }
    } else if (c == '$') {
      radix = 16;
      eat(lex);
      c = peek(lex);
    }
    while (TRUE) {
      // underscores in numbers
      if (c == '_') {
        eat(lex);
        c = peek(lex);
        continue;
      }
      if (!isalnum(c)) {
        break;
      }
      pushChar(lex, c);
      eat(lex);
      c = peek(lex);
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
    eat(lex);
    c = peek(lex);
  }
  UInt len = strlen(lex->file.txt);
  // digraph?
  if (len == 0) {
    eat(lex);
    U8 nc = peek(lex);
    for (UInt i = 0; i < (sizeof(DIGRAPHS) / sizeof(DIGRAPHS[0])); ++i) {
      char const *dg = DIGRAPHS[i].name;
      if ((dg[0] == c) && (dg[1] == nc)) {
        eat(lex);
        lex->file.stash = DIGRAPHS[i].tok;
        return DIGRAPHS[i].tok;
      }
    }
  }
  // id len 1?
  if (len == 1) {
    U8 upper = toupper(lex->file.txt[0]);
    switch (upper) {
    case 'A':
      lex->file.stash = TOK_Y;
      return TOK_Y;
    case 'X':
      lex->file.stash = TOK_Y;
      return TOK_Y;
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
  MacroTok *tok = lex->macro.toks + lex->macro.toksIdx;
  switch (tok->kind) {
  case MACRO_TOK:
    return tok->tok;
  case MACRO_ID:
    return TOK_ID;
  case MACRO_NUM:
    return TOK_NUM;
  case MACRO_STR:
    return TOK_STR;
  case MACRO_ARG: {
    if (lex->macro.argsIdx >= lex->macro.args[tok->num].bufLen) {
      lexFatalLoc(lex, tok->loc, "Argument %" UINT_FMT " is undefined\n",
                  tok->num);
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
    return TOK_NUM;
  case MACRO_SHIFT:
    if (lex->macro.argsLen == 0) {
      lexFatalLoc(lex, tok->loc, "No arguments to shift\n");
      return TOK_EOF;
    }
    return '\n';
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

void lexFileInit(Lex *lex, char *name, FILE *hnd);

void lexMacroInit(Lex *lex, char *name, MacroTok *toks, UInt toksLen,
                  MacroArg *args, UInt argsLen);

void lexIfElseInit(Lex *lex, LocTok *toks, UInt toksLen);

U8 lexPeek(Lex *lex) {
  switch (lex->kind) {
  case LEX_FILE:
    return peekFile(lex);
  case LEX_MACRO:
    return peekMacro(lex);
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
    MacroTok *tok = lex->macro.toks + lex->macro.toksIdx;
    switch (tok->kind) {
    case MACRO_SHIFT:
      argsDequeue(&lex->macro.args, &lex->macro.argsLen);
      break;
    case MACRO_ARG: {
      MacroArg *arg = lex->macro.args + tok->num;
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
  }
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
      fprintf(stderr, "%s:%" UINT_FMT ":%" UINT_FMT ": ", lex->loc.id,
              lex->file.charLine, lex->file.charCol);
      fatal("Failed to rewind file: %s\n", strerror(err));
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
  case LEX_IF_ELSE:
    return lex->ifElse.toks[lex->ifElse.toksIdx].loc;
  default:
    UNREACHABLE();
    return (Loc){0};
  }
}
