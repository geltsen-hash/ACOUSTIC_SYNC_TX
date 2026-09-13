/*
 * functions.c
 *
 *  Acoustic Barker / M-Sequence Transmitter Functions
 *  Target: STM32G474CET3
 */

#include "functions.h"

extern uint32_t ticks_per_period;
extern uint32_t ticks_per_bit;
extern uint32_t ticks_per_stop_bit;

void DWT_Init(void){
	SCB_DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; // разрешаем использовать счётчик
	DWT_CONTROL |= DWT_CTRL_CYCCNTENA_Msk;   // запускаем счётчик
}

void delay_micros(uint32_t us){
	uint32_t us_count_tic = us * (SystemCoreClock / 1000000);
	uint32_t start = DWT->CYCCNT;
	while((DWT->CYCCNT - start) < us_count_tic);
}

void delay_tick(uint32_t ticks){
	uint32_t start = DWT->CYCCNT;
	while((DWT->CYCCNT - start) < ticks);
}

void SEND_BIT_1(uint8_t bits){
	TIM1->BDTR |= TIM_BDTR_MOE;             // timer on
	GPIOB->ODR &= ~(1 << 11);               // ENA драйвера моста 0 - ON
	TIM1->EGR = 0x1;                        // counter reset 0
	delay_tick(ticks_per_bit * bits);       // wait for periods * ticks_per_period
	TIM1->BDTR &= ~TIM_BDTR_MOE;            // timer off
	GPIOB->ODR |= (1 << 11);                // ENA драйвера моста 1 - OFF
}

void SEND_BIT_0(uint8_t bits){
	delay_tick(ticks_per_bit * bits);
}

void SEND_BYTE(uint8_t Byte){
	// стартовый бит
	TIM1->BDTR |= TIM_BDTR_MOE;
	TIM1->EGR = 0x1;
	delay_tick(ticks_per_bit);
	TIM1->BDTR &= ~TIM_BDTR_MOE;

	// данные
	for(int i = 0; i < 8; i++){
		if(((Byte >> i) & 1u) == 1){
			TIM1->BDTR |= TIM_BDTR_MOE;
			TIM1->EGR = 0x1;
			delay_tick(ticks_per_bit);
			TIM1->BDTR &= ~TIM_BDTR_MOE;
		}
		else {
			delay_tick(ticks_per_bit);
		}
	}
	// стоповый бит
	delay_tick(ticks_per_stop_bit);
}

void SEND_M_SEQ(void){
	// 31-битная М-последовательность: 1001001111101110001010110100001
	SEND_BIT_1(1); // 1
	SEND_BIT_0(2); // 00
	SEND_BIT_1(1); // 1
	SEND_BIT_0(2); // 00
	SEND_BIT_1(5); // 11111
	SEND_BIT_0(1); // 0
	SEND_BIT_1(3); // 111
	SEND_BIT_0(3); // 000
	SEND_BIT_1(1); // 1
	SEND_BIT_0(1); // 0
	SEND_BIT_1(1); // 1
	SEND_BIT_0(1); // 0
	SEND_BIT_1(2); // 11
	SEND_BIT_0(1); // 0
	SEND_BIT_1(1); // 1
	SEND_BIT_0(4); // 0000
	SEND_BIT_1(1); // 1
}


