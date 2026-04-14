#include "stm32f469xx.h"
#include "myTimers.h"

int flagmsTicks = 0;  // extern

//******************************************************************************************************************************

// HAL_GetTick()This is often used to create a non-blocking delay or a timeout:

volatile uint32_t msTicks = 0; // Volatile ensures the compiler doesn't optimize out reads of this value
uint32_t SystemCoreClock = 180000000;
/*2. The Configuration
In your initialization code, you need to configure SysTick to trigger an interrupt every 1 millisecond.
Assuming your System Core Clock (HCLK) is already configured (e.g., to 180 MHz), you can use the CMSIS function SysTick_Config.
*/
void SysTick_Init(void) {
    // Configure SysTick to generate an interrupt every 1ms
    // The formula is: ticks = SystemCoreClock / 1000
    if (SysTick_Config(SystemCoreClock / 1000)) {
    	// The SysTick_Config function returns a value (typically 0 for success and 1 for failure
        // Capture error (should not happen with valid clock)
    	NVIC_SystemReset(); // Force the whole chip to reboot and try again
    }
}

/*3. The Handler (ISR)
The SysTick handler is predefined in the vector table. You just need to define it and increment your counter.
 */
void SysTick_Handler(void) {
	msTicks++;
	flagmsTicks = 1;
}

/*4. The GetTick Equivalent : now, create a function to return that value. This is a direct replacement for HAL_GetTick().*/
uint32_t GetSysTick(void) {
	return msTicks;
}

// ***********************************************************************************************************************************

// Records the starting tick count and waits until the required nb of ms has passed. It’s non-blocking, the CPU can still handle interrupts while waiting
void NBdelay_ms(uint32_t ms)
{
    uint32_t start = msTicks;
    while ((msTicks - start) < ms) {}
}


void RTC_WKUP_IRQHandler()
{
    EXTI->PR = (1U<<22);// This bit is set when the selected edge event arrives on the external interrupt line. This bit is cleared by programming it to ‘1’.
    if((RTC->ISR & RTC_ISR_WUTF)!=0) // (1U<<10 WUTF: this flag is set by hardware when the wakeup auto-reload counter reaches 0.
    {									// 1: Wakeup timer configuration update allowed
    	GPIOK->ODR ^= GPIO_ODR_OD3; //toggle PK3 (bleu)
    	RTC->ISR = ~RTC_ISR_WUTF; // (0U<<10); this flag is cleared by software by writing 0
    }
}
