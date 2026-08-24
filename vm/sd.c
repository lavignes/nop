// A little SD card in SPI mode (SDSC, byte-addressed), bit-banged over a VIA

#include <stdio.h>
#include <string.h>

#include "vm.h"

enum {
  ST_CMD,
  ST_SEND,
  ST_RECV_TOKEN,
  ST_RECV_DATA,
};

enum {
  SD_CS = 1 << 0,   // PB0
  SD_SCK = 1 << 1,  // PB1
  SD_MOSI = 1 << 2, // PB2
  SD_MISO = 1 << 7, // PB7
};

enum {
  SD_CMD_LEN = 6,
};

enum {
  SD_CMD_GO_IDLE_STATE = 0,
  SD_CMD_SEND_OP_COND = 1,
  SD_CMD_SEND_IF_COND = 8,
  SD_CMD_SET_BLOCKLEN = 16,
  SD_CMD_READ_SINGLE_BLOCK = 17,
  SD_CMD_WRITE_BLOCK = 24,
  SD_CMD_APP_CMD = 55,
  SD_CMD_READ_OCR = 58,
  SD_ACMD_SD_SEND_OP_COND = 41,
};

enum {
  SD_R1_OK = 0x00,
  SD_R1_IDLE = 0x01,
  SD_R1_ILLEGAL_CMD = 0x04,
};

enum {
  SD_TOKEN_START = 0xFE,
  SD_DATA_ERR_READ_FAILED = 0x01,
  SD_DATA_ERR_OUT_OF_RANGE = 0x09,
};

enum {
  SD_DR_ACCEPTED = 0x05,
  SD_DR_WRITE_ERROR = 0x0D,
};

enum {
  SD_OCR_SDSC = 0x00FF8000,
};

enum {
  SD_BLOCK_LEN = 512,
  SD_CRC_LEN = 2,
  SD_IDLE_BYTE = 0xFF,
};

Bool sdOpen(Sd *sd, char const *path) {
  sd->file = fopen(path, "r+b");
  return sd->file != NULL;
}

void sdReset(Sd *sd) {
  FILE *file = sd->file;
  memset(sd, 0, sizeof(*sd));
  sd->file = file;
  sd->miso = TRUE;
  sd->outByte = SD_IDLE_BYTE;
  sd->idle = TRUE;
  sd->state = ST_CMD;
}

static void sdRespR1(Sd *sd, U8 r1) {
  sd->resp[0] = r1;
  sd->respLen = 1;
  sd->respPos = 0;
  sd->afterSend = ST_CMD;
  sd->state = ST_SEND;
}

static void sdRespR3(Sd *sd, U8 r1, U32 ocr) {
  sd->resp[0] = r1;
  sd->resp[1] = (U8)(ocr >> 24);
  sd->resp[2] = (U8)(ocr >> 16);
  sd->resp[3] = (U8)(ocr >> 8);
  sd->resp[4] = (U8)ocr;
  sd->respLen = 5;
  sd->respPos = 0;
  sd->afterSend = ST_CMD;
  sd->state = ST_SEND;
}

static void sdReadBlock(Sd *sd, U32 addr) {
  FILE *f = sd->file;
  sd->resp[0] = SD_R1_OK;
  sd->respPos = 0;
  sd->afterSend = ST_CMD;
  sd->state = ST_SEND;
  if (fseek(f, (long)addr, SEEK_SET) != 0) {
    sd->resp[1] = SD_DATA_ERR_OUT_OF_RANGE;
    sd->respLen = 2;
    return;
  }
  UInt n = fread(&sd->resp[2], 1, SD_BLOCK_LEN, f);
  if ((n < SD_BLOCK_LEN) && ferror(f)) {
    sd->resp[1] = SD_DATA_ERR_READ_FAILED;
    sd->respLen = 2;
    return;
  }
  while (n < SD_BLOCK_LEN) {
    sd->resp[2 + n++] = 0x00;
  }
  sd->resp[1] = SD_TOKEN_START;
  sd->resp[2 + SD_BLOCK_LEN] = SD_IDLE_BYTE;
  sd->resp[3 + SD_BLOCK_LEN] = SD_IDLE_BYTE;
  sd->respLen = 2 + SD_BLOCK_LEN + SD_CRC_LEN;
}

static Bool sdWriteBlock(Sd *sd) {
  FILE *f = sd->file;
  if (fseek(f, (long)sd->writeAddr, SEEK_SET) != 0) {
    return FALSE;
  }
  if (fwrite(sd->data, 1, SD_BLOCK_LEN, f) != SD_BLOCK_LEN) {
    return FALSE;
  }
  return fflush(f) == 0;
}

static void sdDecode(Sd *sd) {
  U8 idx = sd->cmd[0] & 0x3F;
  U32 arg = ((U32)sd->cmd[1] << 24) | ((U32)sd->cmd[2] << 16) |
            ((U32)sd->cmd[3] << 8) | (U32)sd->cmd[4];
  Bool app = sd->appCmd;
  sd->appCmd = FALSE;
  U8 r1 = sd->idle ? SD_R1_IDLE : SD_R1_OK;

  if (app && (idx == SD_ACMD_SD_SEND_OP_COND)) {
    sd->idle = FALSE;
    sdRespR1(sd, SD_R1_OK);
    return;
  }
  switch (idx) {
  case SD_CMD_GO_IDLE_STATE:
    sd->idle = TRUE;
    sdRespR1(sd, SD_R1_IDLE);
    return;
  case SD_CMD_SEND_OP_COND:
    sd->idle = FALSE;
    sdRespR1(sd, SD_R1_OK);
    return;
  case SD_CMD_SEND_IF_COND:
    sdRespR1(sd, r1 | SD_R1_ILLEGAL_CMD);
    return;
  case SD_CMD_SET_BLOCKLEN:
    sdRespR1(sd, SD_R1_OK);
    return;
  case SD_CMD_READ_SINGLE_BLOCK:
    sdReadBlock(sd, arg);
    return;
  case SD_CMD_WRITE_BLOCK:
    sd->writeAddr = arg;
    sdRespR1(sd, SD_R1_OK);
    sd->afterSend = ST_RECV_TOKEN;
    return;
  case SD_CMD_APP_CMD:
    sd->appCmd = TRUE;
    sdRespR1(sd, r1);
    return;
  case SD_CMD_READ_OCR:
    sdRespR3(sd, r1, SD_OCR_SDSC);
    return;
  default:
    sdRespR1(sd, r1 | SD_R1_ILLEGAL_CMD);
    return;
  }
}

static U8 sdByte(Sd *sd, U8 in) {
  switch (sd->state) {
  case ST_SEND: {
    U8 b = sd->resp[sd->respPos++];
    if (sd->respPos >= sd->respLen) {
      sd->respPos = 0;
      sd->respLen = 0;
      sd->state = sd->afterSend;
    }
    return b;
  }
  case ST_RECV_TOKEN:
    if (in == SD_TOKEN_START) {
      sd->dataCnt = 0;
      sd->state = ST_RECV_DATA;
    }
    return SD_IDLE_BYTE;
  case ST_RECV_DATA:
    if (sd->dataCnt < SD_BLOCK_LEN) {
      sd->data[sd->dataCnt] = in;
    }
    ++sd->dataCnt;
    if (sd->dataCnt >= SD_BLOCK_LEN + SD_CRC_LEN) {
      sd->resp[0] = sdWriteBlock(sd) ? SD_DR_ACCEPTED : SD_DR_WRITE_ERROR;
      sd->resp[1] = 0x00;
      sd->resp[2] = 0x00;
      sd->resp[3] = SD_IDLE_BYTE;
      sd->respLen = 4;
      sd->respPos = 0;
      sd->afterSend = ST_CMD;
      sd->state = ST_SEND;
    }
    return SD_IDLE_BYTE;
  default:
    if ((sd->cmdCnt == 0) && ((in & 0xC0) != 0x40)) {
      return SD_IDLE_BYTE;
    }
    sd->cmd[sd->cmdCnt++] = in;
    if (sd->cmdCnt < SD_CMD_LEN) {
      return SD_IDLE_BYTE;
    }
    sd->cmdCnt = 0;
    sdDecode(sd);
    return SD_IDLE_BYTE;
  }
}

void sdTick(Sd *sd, Via *via) {
  Bool cs = (via->orb & SD_CS) != 0;
  Bool sck = (via->orb & SD_SCK) != 0;
  Bool mosi = (via->orb & SD_MOSI) != 0;
  if (!sd->file || cs) {
    sd->bitCnt = 0;
    sd->miso = TRUE;
  } else if (sck && !sd->lastSck) {
    sd->miso = ((sd->outByte >> 7) & 1) != 0;
    sd->outByte = (U8)((sd->outByte << 1) | 1);
    sd->inBits = (U8)((sd->inBits << 1) | (mosi ? 1 : 0));
    if (++sd->bitCnt == 8) {
      sd->bitCnt = 0;
      sd->outByte = sdByte(sd, sd->inBits);
      sd->inBits = 0;
    }
  }
  sd->lastSck = sck;
  via->pbIn = (U8)((via->pbIn & ~SD_MISO) | (sd->miso ? SD_MISO : 0));
}
