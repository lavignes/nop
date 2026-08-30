// A little 6522 emulator

#include <string.h>

#include "vm.h"

enum {
  VIA_ORB = 0x0,
  VIA_ORA = 0x1,
  VIA_DDRB = 0x2,
  VIA_DDRA = 0x3,
  VIA_T1CL = 0x4,
  VIA_T1CH = 0x5,
  VIA_T1LL = 0x6,
  VIA_T1LH = 0x7,
  VIA_T2CL = 0x8,
  VIA_T2CH = 0x9,
  VIA_SR = 0xA,
  VIA_ACR = 0xB,
  VIA_PCR = 0xC,
  VIA_IFR = 0xD,
  VIA_IER = 0xE,
  VIA_ORA_NH = 0xF,
};

enum {
  VIA_IRQ_CA2 = 1 << 0,
  VIA_IRQ_CA1 = 1 << 1,
  VIA_IRQ_SR = 1 << 2,
  VIA_IRQ_CB2 = 1 << 3,
  VIA_IRQ_CB1 = 1 << 4,
  VIA_IRQ_T2 = 1 << 5,
  VIA_IRQ_T1 = 1 << 6,
  VIA_IRQ_ANY = 1 << 7,
};

enum {
  VIA_ACR_PA_LATCH = 1 << 0,
  VIA_ACR_PB_LATCH = 1 << 1,
  VIA_ACR_T1_CONT = 1 << 6,
  VIA_ACR_T1_PB7 = 1 << 7,
};

enum {
  PC2_IN_NEG = 0,
  PC2_IN_NEG_INDEP = 1,
  PC2_IN_POS = 2,
  PC2_IN_POS_INDEP = 3,
  PC2_HANDSHAKE = 4,
  PC2_PULSE = 5,
  PC2_LOW = 6,
  PC2_HIGH = 7,
};

enum {
  SR_DISABLED = 0,
  SR_IN_T2 = 1,
  SR_IN_PHI2 = 2,
  SR_IN_EXT = 3,
  SR_OUT_FREE = 4,
  SR_OUT_T2 = 5,
  SR_OUT_PHI2 = 6,
  SR_OUT_EXT = 7,
};

void viaReset(Via *via) {
  memset(via, 0, sizeof(*via));
  via->ca2Out = TRUE;
  via->cb2Out = TRUE;
  via->ca1In = TRUE;
  via->ca2In = TRUE;
  via->cb2In = TRUE;
  via->cb1In = TRUE;
  via->cb1Out = TRUE;
}

static U8 readPortA(Via const *via) {
  U8 in = (via->acr & VIA_ACR_PA_LATCH) ? via->ira : via->paIn;
  return (via->ora & via->ddra) | (in & ~via->ddra);
}

static U8 readPortB(Via const *via) {
  U8 in = (via->acr & VIA_ACR_PB_LATCH) ? via->irb : via->pbIn;
  return (via->orb & via->ddrb) | (in & ~via->ddrb);
}

static void ca2Access(Via *via) {
  switch ((via->pcr >> 1) & 0x07) {
  case PC2_HANDSHAKE:
    via->ca2Out = FALSE;
    break;
  case PC2_PULSE:
    via->ca2Out = FALSE;
    via->ca2Pulse = 1;
    break;
  }
}

static void cb2Access(Via *via) {
  if (((via->acr >> 2) & 0x07) != SR_DISABLED) {
    return;
  }
  switch ((via->pcr >> 5) & 0x07) {
  case PC2_HANDSHAKE:
    via->cb2Out = FALSE;
    break;
  case PC2_PULSE:
    via->cb2Out = FALSE;
    via->cb2Pulse = 1;
    break;
  }
}

static void srStart(Via *via) {
  U8 mode = (via->acr >> 2) & 0x07;
  if (mode == SR_DISABLED) {
    return;
  }
  if ((mode == SR_IN_EXT) || (mode == SR_OUT_EXT)) {
    // For external clock mode, reset for next byte
    via->srCount = 0;
    via->srRun = TRUE;
    return;
  }
  via->srCount = 0;
  via->srRun = TRUE;
  if ((mode == SR_IN_PHI2) || (mode == SR_OUT_PHI2)) {
    via->srDiv = 2;
  } else {
    via->srDiv = 2 * ((U16)(via->t2ll + 2));
  }
}

static void srShift(Via *via) {
  U8 mode = (via->acr >> 2) & 0x07;
  if ((mode >= SR_IN_T2) && (mode <= SR_IN_EXT)) {
    U8 bit = via->cb2In ? 1 : 0;
    via->sr = (U8)((via->sr << 1) | bit);
  } else {
    U8 bit = (via->sr >> 7) & 1;
    via->cb2Out = bit != 0;
    via->sr = (U8)((via->sr << 1) | bit);
  }
  via->cb1Out = !via->cb1Out;
  if (++via->srCount >= 8) {
    via->ifr |= VIA_IRQ_SR;
    if (mode == SR_OUT_FREE) {
      via->srCount = 0;
    } else {
      via->srRun = FALSE;
    }
  }
}

static Bool hasIrq(Via const *via) { return (via->ifr & via->ier & 0x7F) != 0; }

U8 viaRead(Via *via, U16 reg) {
  switch (reg & 0x0F) {
  case VIA_ORB:
    via->ifr &= ~VIA_IRQ_CB1;
    if (((via->pcr >> 5) & 0x07) != PC2_IN_NEG_INDEP &&
        ((via->pcr >> 5) & 0x07) != PC2_IN_POS_INDEP) {
      via->ifr &= ~VIA_IRQ_CB2;
    }
    cb2Access(via);
    return readPortB(via);
  case VIA_ORA:
    via->ifr &= ~VIA_IRQ_CA1;
    if (((via->pcr >> 1) & 0x07) != PC2_IN_NEG_INDEP &&
        ((via->pcr >> 1) & 0x07) != PC2_IN_POS_INDEP) {
      via->ifr &= ~VIA_IRQ_CA2;
    }
    ca2Access(via);
    return readPortA(via);
  case VIA_DDRB:
    return via->ddrb;
  case VIA_DDRA:
    return via->ddra;
  case VIA_T1CL:
    via->ifr &= ~VIA_IRQ_T1;
    return via->t1c & 0xFF;
  case VIA_T1CH:
    return via->t1c >> 8;
  case VIA_T1LL:
    return via->t1ll;
  case VIA_T1LH:
    return via->t1lh;
  case VIA_T2CL:
    via->ifr &= ~VIA_IRQ_T2;
    return via->t2c & 0xFF;
  case VIA_T2CH:
    return via->t2c >> 8;
  case VIA_SR:
    via->ifr &= ~VIA_IRQ_SR;
    srStart(via);
    return via->sr;
  case VIA_ACR:
    return via->acr;
  case VIA_PCR:
    return via->pcr;
  case VIA_IFR:
    return via->ifr | (hasIrq(via) ? VIA_IRQ_ANY : 0);
  case VIA_IER:
    return via->ier | 0x80;
  case VIA_ORA_NH:
    return readPortA(via);
  }
  return 0;
}

void viaWrite(Via *via, U16 reg, U8 val) {
  switch (reg & 0x0F) {
  case VIA_ORB:
    via->orb = val;
    via->ifr &= ~VIA_IRQ_CB1;
    if (((via->pcr >> 5) & 0x07) != PC2_IN_NEG_INDEP &&
        ((via->pcr >> 5) & 0x07) != PC2_IN_POS_INDEP) {
      via->ifr &= ~VIA_IRQ_CB2;
    }
    cb2Access(via);
    break;
  case VIA_ORA:
    via->ora = val;
    via->ifr &= ~VIA_IRQ_CA1;
    if (((via->pcr >> 1) & 0x07) != PC2_IN_NEG_INDEP &&
        ((via->pcr >> 1) & 0x07) != PC2_IN_POS_INDEP) {
      via->ifr &= ~VIA_IRQ_CA2;
    }
    ca2Access(via);
    break;
  case VIA_DDRB:
    via->ddrb = val;
    break;
  case VIA_DDRA:
    via->ddra = val;
    break;
  case VIA_T1CL:
  case VIA_T1LL:
    via->t1ll = val;
    break;
  case VIA_T1CH:
    via->t1lh = val;
    via->t1c = (((U16)via->t1lh) << 8) | via->t1ll;
    via->ifr &= ~VIA_IRQ_T1;
    via->t1Active = TRUE;
    break;
  case VIA_T1LH:
    via->t1lh = val;
    via->ifr &= ~VIA_IRQ_T1;
    break;
  case VIA_T2CL:
    via->t2ll = val;
    break;
  case VIA_T2CH:
    via->t2c = (((U16)val) << 8) | via->t2ll;
    via->ifr &= ~VIA_IRQ_T2;
    via->t2Active = TRUE;
    break;
  case VIA_SR:
    via->sr = val;
    via->ifr &= ~VIA_IRQ_SR;
    srStart(via);
    break;
  case VIA_ACR:
    via->acr = val;
    U8 mode = (val >> 2) & 0x07;
    if ((mode == SR_IN_EXT) || (mode == SR_OUT_EXT)) {
      via->srCount = 0;
      via->srRun = TRUE;
    }
    break;
  case VIA_PCR:
    via->pcr = val;
    if (((val >> 1) & 0x07) == PC2_LOW) {
      via->ca2Out = FALSE;
    } else if (((val >> 1) & 0x07) == PC2_HIGH) {
      via->ca2Out = TRUE;
    }
    if (((val >> 5) & 0x07) == PC2_LOW) {
      via->cb2Out = FALSE;
    } else if (((val >> 5) & 0x07) == PC2_HIGH) {
      via->cb2Out = TRUE;
    }
    break;
  case VIA_IFR:
    via->ifr &= ~(val & 0x7F);
    break;
  case VIA_IER:
    if (val & 0x80) {
      via->ier |= (val & 0x7F);
    } else {
      via->ier &= ~(val & 0x7F);
    }
    break;
  case VIA_ORA_NH:
    via->ora = val;
    break;
  }
}

Bool viaTick(Via *via, UInt cycles) {
  for (UInt i = 0; i < cycles; ++i) {
    if (via->t1Active) {
      if (via->t1c == 0) {
        via->ifr |= VIA_IRQ_T1;
        if (via->acr & VIA_ACR_T1_CONT) {
          via->t1c = (((U16)via->t1lh) << 8) | via->t1ll;
        } else {
          via->t1Active = FALSE;
          via->t1c = 0xFFFF;
        }
      } else {
        --via->t1c;
      }
    }
    if (via->t2Active) {
      if (via->t2c == 0) {
        via->ifr |= VIA_IRQ_T2;
        via->t2Active = FALSE;
        via->t2c = 0xFFFF;
      } else {
        --via->t2c;
      }
    }
    if (via->srRun) {
      U8 mode = (via->acr >> 2) & 0x07;
      if ((mode != SR_IN_EXT) && (mode != SR_OUT_EXT)) {
        if (--via->srDiv == 0) {
          srShift(via);
          if ((mode == SR_IN_PHI2) || (mode == SR_OUT_PHI2)) {
            via->srDiv = 2;
          } else {
            via->srDiv = 2 * (U16)(via->t2ll + 2);
          }
        }
      }
    }
    if (via->ca2Pulse && (--via->ca2Pulse == 0)) {
      via->ca2Out = TRUE;
    }
    if (via->cb2Pulse && (--via->cb2Pulse == 0)) {
      via->cb2Out = TRUE;
    }
  }
  return hasIrq(via);
}

void viaSetPort(Via *via, ViaPort port, U8 val) {
  if (port == VIA_PORT_A) {
    via->paIn = val;
  } else {
    via->pbIn = val;
  }
}

U8 viaGetPort(Via const *via, ViaPort port) {
  if (port == VIA_PORT_A) {
    return (via->ora & via->ddra) | (via->paIn & ~via->ddra);
  }
  return (via->orb & via->ddrb) | (via->pbIn & ~via->ddrb);
}

void viaSetC1(Via *via, ViaPort port, Bool level) {
  if (port == VIA_PORT_A) {
    Bool rising = (via->pcr & 0x01) != 0;
    Bool active = rising ? (!via->ca1In && level) : (via->ca1In && !level);
    via->ca1In = level;
    if (!active) {
      return;
    }
    if (via->acr & VIA_ACR_PA_LATCH) {
      via->ira = via->paIn;
    }
    via->ifr |= VIA_IRQ_CA1;
    if (((via->pcr >> 1) & 0x07) == PC2_HANDSHAKE) {
      via->ca2Out = TRUE;
    }
    return;
  }
  Bool rising = (via->pcr & 0x10) != 0;
  Bool active = rising ? (!via->cb1In && level) : (via->cb1In && !level);
  U8 mode = (via->acr >> 2) & 0x07;
  via->cb1In = level;
  if (!active) {
    return;
  }
  if (via->srRun && ((mode == SR_IN_EXT) || (mode == SR_OUT_EXT))) {
    srShift(via);
    return;
  }
  if (via->acr & VIA_ACR_PB_LATCH) {
    via->irb = via->pbIn;
  }
  via->ifr |= VIA_IRQ_CB1;
}

Bool viaGetC1(Via const *via, ViaPort port) {
  return (port == VIA_PORT_A) ? via->ca1In : via->cb1Out;
}

Bool viaC1Irq(Via const *via, ViaPort port) {
  U8 flag = (port == VIA_PORT_A) ? VIA_IRQ_CA1 : VIA_IRQ_CB1;
  return (via->ifr & flag) != 0;
}

void viaSetC2(Via *via, ViaPort port, Bool level) {
  if (port == VIA_PORT_A) {
    U8 mode = (via->pcr >> 1) & 0x07;
    if (mode > PC2_IN_POS_INDEP) {
      return;
    }
    Bool rising = ((mode == PC2_IN_POS) || (mode == PC2_IN_POS_INDEP));
    Bool active = rising ? (!via->ca2In && level) : (via->ca2In && !level);
    via->ca2In = level;
    if (active) {
      via->ifr |= VIA_IRQ_CA2;
    }
    return;
  }
  if (((via->acr >> 2) & 0x07) != SR_DISABLED) {
    via->cb2In = level;
    return;
  }
  U8 mode = (via->pcr >> 5) & 0x07;
  if (mode > PC2_IN_POS_INDEP) {
    via->cb2In = level;
    return;
  }
  Bool rising = ((mode == PC2_IN_POS) || (mode == PC2_IN_POS_INDEP));
  Bool active = rising ? (!via->cb2In && level) : (via->cb2In && !level);
  via->cb2In = level;
  if (active) {
    via->ifr |= VIA_IRQ_CB2;
  }
}

Bool viaGetC2(Via const *via, ViaPort port) {
  return (port == VIA_PORT_A) ? via->ca2Out : via->cb2Out;
}

Bool viaC2Irq(Via const *via, ViaPort port) {
  U8 flag = (port == VIA_PORT_A) ? VIA_IRQ_CA2 : VIA_IRQ_CB2;
  return (via->ifr & flag) != 0;
}
