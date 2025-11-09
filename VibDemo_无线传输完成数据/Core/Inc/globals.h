#ifndef GLOBALS_H
#define GLOBALS_H

#include <stdint.h>

#define SMP_LEN  7

extern int16_t   rawBuf[2][SMP_LEN];
extern uint8_t   bufIdx;
extern uint8_t   bufFull;
extern float     rmsBuf[2];
#endif
