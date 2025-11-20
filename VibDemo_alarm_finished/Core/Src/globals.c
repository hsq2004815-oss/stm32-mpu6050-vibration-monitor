
#include <stdio.h>
#include "main.h"


int16_t rawBuf[2][SMP_LEN] = {0};  // Ë«»º³å
uint8_t bufIdx = 0;
uint8_t bufFull = 0;
float   rmsBuf[2] = {0};





int fputc(int ch, FILE *f)
{
         HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
         return ch;
}
