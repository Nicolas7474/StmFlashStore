#ifndef SYS_TICK_H_
#define SYS_TICK_H_

#include <stm32f4xx.h>
#include <stm32f469xx.h>

void SysTick_Init(void);

void NBdelay_ms(uint32_t ms);

extern volatile uint32_t msTicks;

extern unsigned int countWakeUp;

#endif /* SYS_TICK_H_ */

uint32_t GetSysTick(void);
