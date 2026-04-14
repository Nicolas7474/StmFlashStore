#ifndef SYS_TICK_H_
#define SYS_TICK_H_

#include <stm32f4xx.h>
#include <stm32f469xx.h>

void SysTick_Init(void);
void SysTickDelayMs(int delay);

void TIM2_Init (void);

void TIM3config(uint16_t Timeout);

void PWM_TIM5_Init(void);

void TIM6_Init (uint16_t ms);

void Delay_us_TIM7(uint16_t us);

void TIM8_Init(void);

void NBdelay_ms(uint32_t ms);

extern volatile uint32_t msTicks;

extern unsigned int countWakeUp;
extern int flagmsTicks;  // extern


#endif /* SYS_TICK_H_ */

uint32_t GetSysTick(void);
