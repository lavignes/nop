// stdlib
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// POSIX
#include <signal.h>
#include <termios.h>
#include <unistd.h>

#include <SDL3/SDL.h>

#include "vm.h"

static Emu emu = {0};

volatile sig_atomic_t sigintFlag = 0;
static struct termios termiosOrig;
static Bool termRawMode = FALSE;

static SDL_Window *win = NULL;
static SDL_Renderer *render = NULL;
static SDL_Texture *tex = NULL;

static Bool quit = FALSE;
static U64 cycleCount = 0;
static U64 vdpAccum = 0;
static U64 nextFrameNs = 0;

static U64 const FRAME_NS = 1000000000ULL * 342 * 262 / VDP_HZ;

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
  fprintf(stderr, "  -i, --image <file>      Attach a disk image\n");
}

static void emuReset(Bool random);
static void emuTick();

int main(int argc, char const *const *argv) {
  FILE *rom;
  char const *labellist = NULL;
  char const *diskPath = NULL;
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
    if ((strcmp(argv[argi], "-i") == 0) ||
        (strcmp(argv[argi], "--image") == 0)) {
      ++argi;
      if (argi == argc) {
        fprintf(stderr, "No disk image specified\n");
        return EXIT_FAILURE;
      }
      diskPath = argv[argi];
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
    symLoad(&emu.dbg, labellist);
  }

  if (!emu.dbg.debug) {
    termRawModeOn();
  }

  SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
  int exitCode = EXIT_FAILURE;
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    fprintf(stderr, "SDL_InitSubSystem failed: %s\n", SDL_GetError());
    goto cleanupSDL;
  }
  if (!SDL_CreateWindowAndRenderer("nop", SCREEN_WIDTH * 3, SCREEN_HEIGHT * 3,
                                   SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                       SDL_WINDOW_RESIZABLE,
                                   &win, &render)) {
    fprintf(stderr, "SDL_CreateWindowAndRenderer failed: %s\n", SDL_GetError());
    goto cleanupWindow;
  }
  if (!SDL_ShowWindow(win)) {
    fprintf(stderr, "SDL_ShowWindow failed: %s\n", SDL_GetError());
    goto cleanupWindow;
  }
  if (!SDL_SetRenderVSync(render, 1)) {
    fprintf(stderr, "SDL_SetRenderVSync failed: %s\n", SDL_GetError());
  }
  tex = SDL_CreateTexture(render, SDL_PIXELFORMAT_RGBA8888,
                          SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH,
                          SCREEN_HEIGHT);
  if (!tex) {
    fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
    goto cleanupTexture;
  }

  if (diskPath && !sdOpen(&emu.sd, diskPath)) {
    fprintf(stderr, "Could not open disk image: %s\n", diskPath);
  }

  emuReset(random);

  while (!quit) {
    emuTick();
  }
  exitCode = EXIT_SUCCESS;
cleanupTexture:
  if (tex) {
    SDL_DestroyTexture(tex);
  }
cleanupWindow:
  if (render) {
    SDL_DestroyRenderer(render);
  }
  if (win) {
    SDL_DestroyWindow(win);
  }
cleanupSDL:
  SDL_Quit();
  return exitCode;
}

static void vblank() {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
    case SDL_EVENT_QUIT:
      quit = TRUE;
      break;
    case SDL_EVENT_KEY_DOWN:
      if (event.key.key == SDLK_F5) {
        emuReset(TRUE);
      } else {
        ps2Key(&emu.ps2, event.key.scancode, TRUE);
      }
      break;
    case SDL_EVENT_KEY_UP:
      if (event.key.key != SDLK_F5) {
        ps2Key(&emu.ps2, event.key.scancode, FALSE);
      }
      break;
    }
  }

  void *pixels;
  int pitch;
  SDL_LockTexture(tex, NULL, &pixels, &pitch);
  memcpy(pixels, emu.vdp.pix, sizeof(emu.vdp.pix));
  SDL_UnlockTexture(tex);
  SDL_RenderClear(render);
  SDL_RenderTexture(render, tex, NULL, NULL);
  SDL_RenderPresent(render);

  static U64 last = 0;
  static UInt fps = 0;
  ++fps;
  U64 now = SDL_GetTicksNS();
  if ((now - last) >= 1000000000ULL) {
    char title[64];
    snprintf(title, sizeof(title), "nop - %" UINT_FMT " fps - %.2f MHz", fps,
             ((F64)cycleCount) / 1000000.0);
    SDL_SetWindowTitle(win, title);
    fps = 0;
    cycleCount = 0;
    last = now;
  }

  if (now < nextFrameNs) {
    SDL_DelayNS(nextFrameNs - now);
    nextFrameNs += FRAME_NS;
  } else {
    nextFrameNs = now + FRAME_NS;
  }
}

static void emuReset(Bool random) {
  emu.nmi = FALSE;
  if (random) {
    for (UInt i = 0; i < sizeof(emu.ram); ++i) {
      emu.ram[i] = (U8)rand();
    }
  }
  cpuReset(&emu.cpu, &emu);
  vdpReset(&emu.vdp, &emu, random);
  viaReset(&emu.via0);
  ps2Reset(&emu.ps2);
  sdReset(&emu.sd);
}

static void emuTick() {
  if (sigintFlag) {
    sigintFlag = 0;
    emu.dbg.debug = TRUE;
  }
  dbgTick(&emu.dbg, &emu);
  UInt cycles = cpuTick(&emu.cpu, &emu);
  cycleCount += cycles;
  vdpAccum += ((U64)cycles) * VDP_HZ;
  while (vdpAccum >= CPU_HZ) {
    vdpAccum -= CPU_HZ;
    if (vdpTick(&emu.vdp, &emu)) {
      vblank();
    }
  }
  viaTick(&emu.via0, cycles);
  if (ps2Pending(&emu.ps2) && !viaCA1Pending(&emu.via0)) {
    viaSetPortA(&emu.via0, ps2Next(&emu.ps2));
    viaCA1(&emu.via0);
  }
  sdTick(&emu.sd, &emu.via0);
  emu.cpu.irq = viaIrq(&emu.via0);
  if (emu.nmi) {
    emu.nmi = FALSE;
    emu.cpu.nmi = TRUE;
  }
}

U8 emuRead(Emu const *emu, U16 addr) {
  switch (addr) {
  case RAM_START_ADDR ... RAM_END_ADDR:
    return emu->ram[addr - RAM_START_ADDR];
  case ROM_START_ADDR ... ROM_END_ADDR:
    return emu->rom[addr - ROM_START_ADDR];
  default:
    return 0;
  }
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
