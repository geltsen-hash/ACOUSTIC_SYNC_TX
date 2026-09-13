/*
 * functions.h
 *
 *  Acoustic Barker / M-Sequence Transmitter Functions
 *  Target: STM32G474CET3
 */

#ifndef INC_FUNCTIONS_H_
#define INC_FUNCTIONS_H_

#include "stm32g4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

#define DWT_CONTROL *(volatile unsigned long *)0xE0001000
#define SCB_DEMCR   *(volatile unsigned long *)0xE000EDFC

void DWT_Init(void);
void delay_micros(uint32_t us);
void delay_tick(uint32_t ticks);
void SEND_BIT_1(uint8_t bits);
void SEND_BIT_0(uint8_t bits);
void SEND_BYTE(uint8_t Byte);
void SEND_M_SEQ(void);

#endif /* INC_FUNCTIONS_H_ */
