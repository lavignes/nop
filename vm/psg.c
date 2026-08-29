// A little SN76489 emulator

#include "vm.h"

enum {
  PSG_CLK = PSG_HZ / 16,
};

enum {
  LATCH = 1 << 7,
  LATCH_VOL = 1 << 4,
  LATCH_CH_MASK = 3 << 5,
  DATA_MASK = 0x0F,
};

enum {
  NOISE_WHITE = 1 << 2,
  NOISE_RATE_MASK = 0x03,

  NOISE_TAP = 0x0009,
  NOISE_SEED = 0x8000,
};

static I16 const VOL[16] = {
    8191, 6507, 5168, 4105, 3261, 2590, 2057, 1642,
    1298, 1031, 819,  651,  517,  411,  326,  0,
};

void psgReset(Psg *psg) {
  for (UInt i = 0; i < 3; ++i) {
    psg->tonePeriod[i] = 0;
    psg->toneCounter[i] = 0;
    psg->toneOut[i] = 0;
  }
  for (UInt i = 0; i < 4; ++i) {
    psg->vol[i] = 0x0F;
  }
  psg->noiseCtrl = 0;
  psg->noiseCounter = 0;
  psg->noiseShift = NOISE_SEED;
  psg->noiseFlip = 0;
  psg->latchCh = 0;
  psg->latchVol = FALSE;
  psg->clkRem = 0;
}

void psgWrite(Psg *psg, U8 val) {
  if (val & LATCH) {
    psg->latchCh = (val & LATCH_CH_MASK) >> 5;
    psg->latchVol = (val & LATCH_VOL) != 0;
    U8 data = val & DATA_MASK;
    if (psg->latchVol) {
      psg->vol[psg->latchCh] = data;
    } else if (psg->latchCh == 3) {
      psg->noiseCtrl = data & 0x07;
      psg->noiseShift = NOISE_SEED;
    } else {
      psg->tonePeriod[psg->latchCh] =
          (psg->tonePeriod[psg->latchCh] & 0x3F0) | data;
    }
    return;
  }
  U8 data = val & 0x3F;
  if (psg->latchVol) {
    psg->vol[psg->latchCh] = val & DATA_MASK;
  } else if (psg->latchCh == 3) {
    psg->noiseCtrl = val & 0x07;
    psg->noiseShift = NOISE_SEED;
  } else {
    psg->tonePeriod[psg->latchCh] =
        (psg->tonePeriod[psg->latchCh] & 0x00F) | (((U16)data) << 4);
  }
}

static U16 noiseReload(Psg const *psg) {
  switch (psg->noiseCtrl & NOISE_RATE_MASK) {
  case 0:
    return 0x10;
  case 1:
    return 0x20;
  case 2:
    return 0x40;
  default:
    return psg->tonePeriod[2] ? psg->tonePeriod[2] : 1;
  }
}

static void psgTick(Psg *psg) {
  for (UInt i = 0; i < 3; ++i) {
    if (psg->toneCounter[i] > 0) {
      --psg->toneCounter[i];
    }
    if (psg->toneCounter[i] == 0) {
      psg->toneCounter[i] = psg->tonePeriod[i];
      psg->toneOut[i] ^= 1;
    }
  }
  if (psg->noiseCounter > 0) {
    --psg->noiseCounter;
  }
  if (psg->noiseCounter == 0) {
    psg->noiseCounter = noiseReload(psg);
    psg->noiseFlip ^= 1;
    if (psg->noiseFlip) {
      U16 fb;
      if (psg->noiseCtrl & NOISE_WHITE) {
        U16 tap = psg->noiseShift & NOISE_TAP;
        tap ^= tap >> 8;
        tap ^= tap >> 4;
        tap ^= tap >> 2;
        tap ^= tap >> 1;
        fb = tap & 1;
      } else {
        fb = psg->noiseShift & 1;
      }
      psg->noiseShift = (psg->noiseShift >> 1) | (fb << 14);
    }
  }
}

I16 psgSample(Psg *psg) {
  psg->clkRem += PSG_CLK;
  UInt ticks = psg->clkRem / SAMPLE_RATE;
  psg->clkRem %= SAMPLE_RATE;
  for (UInt t = 0; t < ticks; ++t) {
    psgTick(psg);
  }

  Int mix = 0;
  for (UInt i = 0; i < 3; ++i) {
    mix += psg->toneOut[i] ? VOL[psg->vol[i]] : -VOL[psg->vol[i]];
  }
  mix += (psg->noiseShift & 1) ? VOL[psg->vol[3]] : -VOL[psg->vol[3]];
  return (I16)mix;
}
