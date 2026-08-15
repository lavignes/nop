#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vm.h"

static Symbol *symAdd(Dbg *dbg, char const *name, Int val) {
  Symbol *sym = malloc(sizeof(Symbol));
  sym->name = strdup(name);
  sym->val = val;
  sym->next = dbg->symHead;
  dbg->symHead = sym;
  ++dbg->symCount;
  return sym;
}

Symbol const *symFind(Dbg const *dbg, char const *name, UInt namelen) {
  for (Symbol const *sym = dbg->symHead; sym; sym = sym->next) {
    if (strlen(sym->name) != namelen) {
      continue;
    }
    if (strncmp(sym->name, name, namelen) == 0) {
      return sym;
    }
  }
  return NULL;
}

Symbol const *symValFind(Dbg const *dbg, Int val) {
  for (Symbol const *sym = dbg->symHead; sym; sym = sym->next) {
    if (val == sym->val) {
      return sym;
    }
  }
  return NULL;
}

void symLoad(Dbg *dbg, char const *filename) {
  FILE *file = fopen(filename, "r");
  if (!file) {
    fprintf(stderr, "Could not open labellist file: %s\n", filename);
    return;
  }
  char line[256];
  for (UInt lineno = 0; fgets(line, sizeof(line), file); ++lineno) {
    char name[64];
    Int val;
    Int block;
    if (sscanf(line, "%63[^,], 0x%" INT_FMTX ", %" INT_FMT, name, &val,
               &block) != 3) {
      fprintf(stderr, "Invalid labellist line %" UINT_FMT ": %s", lineno, line);
      continue;
    }
    if (!isalpha(name[0]) && (name[0] != '_')) {
      continue;
    }
    if (block != 0) {
      continue;
    }
    symAdd(dbg, name, val);
  }
  fclose(file);
}
