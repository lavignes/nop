// A little 6550A UART emulator

#include "vm.h"

void uartReset(Uart *uart) {}

Bool uartTick(Uart *uart, UInt cycles) { return FALSE; }

void uartWrite(Uart *uart, U8 val) {}

U8 uartRead(Uart *uart) { return 0; }
