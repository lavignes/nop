// stdlib
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// POSIX
#include <termios.h>
#include <unistd.h>

#include "vm.h"

Emu emu = {0};

volatile sig_atomic_t sigintFlag = 0;
static struct termios termiosOrig;
static Bool termRawMode = FALSE;

static void sigintHandler(int sig) {
  (void)sig;
  if (emu.dbg.debug) {
    exit(EXIT_SUCCESS);
  }
  sigintFlag = 1;
}

static void help(char const *name) {
  fprintf(stderr, "Usage: %s [options] <romfile>\n\n", name);
  fprintf(stderr, "Options:\n\n");
  fprintf(stderr, "  -h, --help              Show this help message\n");
  fprintf(stderr, "  -d, --debug             Start in debug mode\n");
  fprintf(stderr,
          "  -l, --labellist <file>  Load symbol labellist from file\n");
  fprintf(stderr,
          "  -r, --random            Initialize memory with random data\n");
}

static void emuTick();

int main(int argc, char const *const *argv) {
  FILE *rom;
  char const *labellist = NULL;
  Bool random = FALSE;

  atexit(termRawModeOff);
  signal(SIGINT, sigintHandler);

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
    if ((strcmp(argv[argi], "-d") == 0) ||
        (strcmp(argv[argi], "--debug") == 0)) {
      emu.dbg.debug = TRUE;
      continue;
    }
    if ((strcmp(argv[argi], "-l") == 0) ||
        (strcmp(argv[argi], "--labellist") == 0)) {
      ++argi;
      if (argi == argc) {
        fprintf(stderr, "No labellist file specified\n");
        return EXIT_FAILURE;
      }
      labellist = argv[argi];
      continue;
    }
    if ((strcmp(argv[argi], "-r") == 0) ||
        (strcmp(argv[argi], "--random") == 0)) {
      random = TRUE;
      continue;
    }
    rom = fopen(argv[argi], "rb");
    if (!rom) {
      fprintf(stderr, "Could not open ROM file: %s\n", argv[argi]);
      return EXIT_FAILURE;
    }
    ++argi;
    if (argi != argc) {
      fprintf(stderr, "Unexpected option: %s\n", argv[argi]);
      return EXIT_FAILURE;
    }
  }

  if (!rom) {
    fprintf(stderr, "No ROM file specified\n");
    return EXIT_FAILURE;
  }

  if (random) {
    srand((unsigned int)time(NULL));
    for (UInt i = 0; i < sizeof(emu.ram); ++i) {
      emu.ram[i] = (U8)rand();
    }
  }

  UInt read = fread(emu.rom, 1, sizeof(emu.rom), rom);
  if (read != sizeof(emu.rom)) {
    int err = ferror(rom);
    if (err) {
      fprintf(stderr, "Error reading ROM file: %s\n", strerror(err));
      return EXIT_FAILURE;
    }
  }
  fclose(rom);

  if (labellist) {
    symLoad(labellist);
  }

  if (!emu.dbg.debug) {
    termRawModeOn();
  }

  while (TRUE) {
    emuTick();
  }

  return EXIT_SUCCESS;
}

static void emuTick() {
  if (sigintFlag) {
    sigintFlag = 0;
    emu.dbg.debug = TRUE;
  }
  if (emu.dbg.nextpoint.next && (emu.cpu.pc == emu.dbg.nextpoint.addr)) {
    emu.dbg.debug = TRUE;
    emu.dbg.nextpoint.next = NULL;
  }
  for (Breakpoint const *bp = emu.dbg.bpHead; bp; bp = bp->next) {
    if (emu.cpu.pc != bp->addr) {
      continue;
    }
    fprintf(stderr, "Hit breakpoint %u at $%04X\r\n", bp->num, bp->addr);
    emu.dbg.debug = TRUE;
    break;
  }
  if (emu.dbg.debug) {
    dbgTick(&emu);
  }
  // TODO: check for interrupts here
  cpuTick(&emu.cpu, &emu);
}

void termRawModeOn() {
  if (termRawMode) {
    return;
  }
  if (tcgetattr(STDIN_FILENO, &termiosOrig) == -1) {
    return;
  }
  struct termios raw = termiosOrig;
  cfmakeraw(&raw);
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
    return;
  }
  termRawMode = TRUE;
}

void termRawModeOff() {
  if (!termRawMode) {
    return;
  }
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &termiosOrig);
  termRawMode = FALSE;
}
