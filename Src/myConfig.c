//#include "stm32f469xx.h"
#include "stm32f4xx.h"
#include <myConfig.h>


void activateFPU(void) {

#if (__FPU_PRESENT == 1) && (__FPU_USED == 1)
	SCB->CPACR |= ((3UL << 20U)|(3UL << 22U));  /* set CP10 and CP11 Full Access */

	// Enable Lazy Stacking for better ISR performance : When an interrupt (like your TIM7 or TIM8 ISRs) occurs, the processor normally has to save all the FPU registers
	// to the stack. This is slow. Lazy Stacking tells the hardware only to save FPU registers if the ISR actually performs a floating-point operation.
	FPU->FPCCR |= FPU_FPCCR_LSPEN_Msk;
#endif
	// Enabling the hardware bits isn't enough; you must also tell your compiler (GCC, Clang, or Keil) to actually generate FPU instructions instead of using slow software libraries.
	// If you are using GCC (arm-none-eabi-gcc), add these flags to your build command:
	// -mfloat-abi=hard: Uses the hardware FPU for calculations and passing arguments.
	// -mfpu=fpv4-sp-d16: Specifies the specific FPU version on the STM32F4.
}


void SysClockConfig (void)
{
	#define PLL_M 	4
	#define PLL_N 	180
	#define PLL_P 	0  // PLLP = 2

	// 1. ENABLE HSE and wait for the HSE to become Ready
	RCC->CR |= RCC_CR_HSEON;  // RCC->CR |= 1<<16;
	while (!(RCC->CR & RCC_CR_HSERDY));  // while (!(RCC->CR & (1<<17)));

	// 2. Set the POWER ENABLE CLOCK and VOLTAGE REGULATOR
	RCC->APB1ENR |= RCC_APB1ENR_PWREN;  // RCC->APB1ENR |= 1<<28; RCC APB1 peripheral clock enable register (RCC_APB1ENR);
	PWR->CR |= PWR_CR_VOS;  // PWR->CR |= 3<<14; PWR power control register (PWR_CR);

	// 3. Configure the FLASH PREFETCH and the LATENCY Related Settings
	FLASH->ACR = FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_PRFTEN | FLASH_ACR_LATENCY_5WS;  // FLASH->ACR = (1<<8) | (1<<9)| (1<<10)| (5<<0);

	// 4. Configure the PRESCALARS HCLK, PCLK1, PCLK2
	// AHB PR
	RCC->CFGR |= RCC_CFGR_HPRE_DIV1;  // RCC->CFGR &= ~(1<<4); RCC clock configuration register (RCC_CFGR);
	// APB1 PR
	RCC->CFGR |= RCC_CFGR_PPRE1_DIV4;  // RCC->CFGR |= (5<<10);
	// APB2 PR
	RCC->CFGR |= RCC_CFGR_PPRE2_DIV2;  // RCC->CFGR |= (4<<13);

	// 5. Configure the MAIN PLL
	RCC->PLLCFGR = (PLL_M <<0) | (PLL_N << 6) | (PLL_P <<16) | (RCC_PLLCFGR_PLLSRC_HSE);  // (1<<22);

	// 6. Enable the PLL and wait for it to become ready
	RCC->CR |= RCC_CR_PLLON;  // RCC->CR |= (1<<24);
	while (!(RCC->CR & RCC_CR_PLLRDY));  // while (!(RCC->CR & (1<<25)));

	// 7. Select the Clock Source and wait for it to be set
	RCC->CFGR |= RCC_CFGR_SW_PLL;  // RCC->CFGR |= (2<<0);
	while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);  // while (!(RCC->CFGR & (2<<2)));


	// ------------- set RTC Wakeup Timer 1Hz ----------------------- //

	PWR->CR |= PWR_CR_DBP; // (1U<<8); Disable backup domain write protection
		while((PWR->CR & PWR_CR_DBP) == 0);
	RCC->BDCR |= RCC_BDCR_BDRST; // (1U<<16); BDRST: Backup domain software reset (1: Resets the entire Backup domain)
	RCC->BDCR &= ~RCC_BDCR_BDRST; // 0: Reset not activated
	RCC->BDCR |= RCC_BDCR_LSEON; //(1U<<0); Enable LSE Clock source and wait until LSERDY bit to set
		while ((RCC->BDCR & RCC_BDCR_LSERDY) == 0); // (1U<<1)
	RCC->BDCR |= RCC_BDCR_RTCSEL; //  (1U<<8) Select LSE as RTC Clock
	RCC->BDCR &= ~(1U<<9); // Select LSE as RTC Clock (2nd bit)
	RCC->BDCR |= (1U<<15); // Enable RTC Clock

	RTC->WPR = 0xCA; // Disable the write protection for RTC registers. After backup domain reset, all the RTC registers
	RTC->WPR = 0x53; // are write-protected. Writing to the RTC registers is enabled by writing a key into the Write Protection register.
	RTC->CR &= ~RTC_CR_WUTE; //(1U<<10); Clear WUTE in RTC_CR to disable the wakeup timer before configuring it
	RTC->ISR = RTC_ISR_WUTWF; // (1U<<2); The wakeup timer values can be changed when WUTE bit is cleared and WUTWF is set. (1: Wakeup timer configuration update allowed)

	// Poll WUTWF until it is set in RTC_ICSR to make sure the access to wakeup autoreload counter and to WUCKSEL[2:0] bits is allowed
	if((RTC->ISR & (1U<<6))==0)
		while((RTC->ISR & RTC_ISR_WUTWF)==0);

	// Configure the Wakeup Timer counter and auto clear value
	RTC->WUTR = 0; // When the wakeup timer is enabled (WUTE set to 1), the WUTF flag is set every (WUT[15:0] + 1) ck_wut cycles
	RTC->PRER = 0xFF;         // Configure the RTC PRER ; Synchronus value set as 255
	RTC->PRER |= (0x7F<<16);    // Asynchronus value set as 127
	RTC->CR |= RTC_CR_WUCKSEL;  // WUCKSEL[2:0]: Configure the clock source; 10x: ck_spre (usually 1 Hz) clock is selected

	EXTI->IMR |= (1U<<22); // Configure and enable the EXTI Line 22 in interrupt mode
	EXTI->RTSR |= (1U<<22); // Rising edge trigger enabled (for Event and Interrupt) for input line 10

	RTC->CR |= RTC_CR_WUTIE; // Wakeup timer interrupt enabled (1U<<14)
	RTC->CR |= RTC_CR_WUTE; // Enable the Wakeup Timer (1U<<10)
	RTC->WPR = RTC_WPR_KEY; // Enable the write protection for RTC registers (0xFF)

	NVIC_SetPriority(RTC_WKUP_IRQn, 10);
	NVIC_EnableIRQ(RTC_WKUP_IRQn);
}


void GPIO_Config (void)
{
	// 1. Enable the GPIO CLOCK
	RCC->AHB1ENR |= (1<<0); // GPIO-A
	RCC->AHB1ENR |= (1<<1); // GPIO-B
	RCC->AHB1ENR |= (1<<3); // GPIO-D
	RCC->AHB1ENR |= (1<<6); // GPIO-G
	RCC->AHB1ENR |= (1<<10); // GPIO-K

	// 2. Set the Pins as OUTPUT / INPUT
	GPIOA->MODER &= ~(3<<0);  // pin PA0(bits 1:0)
	GPIOB->MODER &= ~(3<<24); // PB12 input mode
	// error GPIOG->MODER |= (1<<21);  //
	GPIOK->MODER |= (1<<6);  // pin PK3(bits 7:6) as Output (01)
	GPIOG->MODER &= ~(3<<26);  // pin PG13 as Output
	GPIOG->MODER |= (1<<26);  // pin PG13 as Output
	GPIOG->MODER |= (1<<12);  // pin PG6(bits 13:12) as Output (01) - Green Led
	GPIOD->MODER |= (1<<8);  // pin PD4 as Output (01) - Orange Led
	GPIOD->MODER |= (1<<10);  // pin PD4 as Output (01) - Red Led
	GPIOA->MODER &= ~(3U << (8 * 2)); // PA8 in input mode

	// 3. Configure the OUTPUT MODE
	GPIOA->PUPDR &= ~(1<<0); // input, 00: No pull-up, pull-down
	GPIOA->PUPDR &= ~(1<<1); // input, 00: No pull-up, pull-down
	GPIOA->OSPEEDR &= ~(1<<30 | 1<<31);
	GPIOB->PUPDR &= ~(1<<24 | 1<<25); // PB12 input, 00: No pull-up, no pull-down
	GPIOG->OTYPER = 1;
	GPIOG->OSPEEDR = 0;
	GPIOK->OTYPER = 1;
	GPIOK->OSPEEDR = 0;
	GPIOG->OTYPER &= ~(1 << 13); // PG13 Push Pull Output
	GPIOG->OSPEEDR &= ~(3U << (26)); // PG13 Low Speed
	GPIOG->BSRR = (1U << 13); // set the PG13 to #1
	GPIOG->OTYPER &= ~(1 << 12); // green Led PG6
	GPIOD->OTYPER &= ~(1 << 4); // orange Led PD4 in P-P
	GPIOD->OTYPER &= ~(1 << 10); // rouge Led PD5 in P-P
	GPIOA->PUPDR &= ~(3U << (8 * 2)); // PA8 01: Pull-up
	GPIOA->PUPDR |= (1 << (8 * 2)); // PA8 01: Pull-up
}


void InterruptGPIO_Config (void)
{
	RCC->APB2ENR |= (1<<14);  // Enable SYSCFGEN: System configuration controller clock enable
	SYSCFG->EXTICR[0] &= ~(0xF<<0);  // Bits[7:6:5:4] = (0:0:0:0) -> configure EXTI1 line for PA1; SYSCFG external interrupt configuration register 1 (SYSCFG_EXTICR1)
	EXTI->IMR |= (1<<0);  // Bit[0] = 1  --> Disable the Mask on EXTI 1 (Interrupt mask register (EXTI_IMR))
	EXTI->RTSR |= (1<<0);  // Enable Rising Edge Trigger for PA0 (Rising trigger selection register (EXTI_RTSR))
	EXTI->FTSR &= ~(1<<0);  // Disable Falling Edge Trigger for PA0 (Falling trigger selection register (EXTI_FTSR))
	NVIC_SetPriority(EXTI0_IRQn, 10);
	NVIC_EnableIRQ(EXTI0_IRQn);

	//SYSCFG->EXTICR[1] &= ~(0xF<<8);  // Bits[8:9:10:] = (0:0:0:0) -> configure EXTI1 line for PA1; SYSCFG external interrupt configuration register 1 (SYSCFG_EXTICR1)
	SYSCFG->EXTICR[3] &= ~(0xF<<0); // first, clear the  bits / EXTI12 is in EXTICR[3], bits [3:0] / SYSCFG external interrupt configuration register 3
	SYSCFG->EXTICR[3] |= (1<<0); // Set Port B (0001); These bits are written by software to select the source input for the EXTIx	external interrupt
	EXTI->IMR |= (1<<12);  // Bit[0] = 1  --> Disable the Mask on EXTI 1 (Interrupt mask register (EXTI_IMR))
	EXTI->RTSR |= (1<<12);  //  interrupt when button released - Enable Rising Edge Trigger for PB12 (Rising trigger selection register (EXTI_RTSR))
	EXTI->FTSR |= (1<<12); // Rotary encoder switch - interrupt when button pushed - - Enable Rising Edge Trigger
	NVIC_SetPriority(EXTI15_10_IRQn, 9);
	NVIC_EnableIRQ(EXTI15_10_IRQn);

	//Map PA8 to EXTI8
	SYSCFG->EXTICR[2] &= ~(0xF);
	SYSCFG->EXTICR[2] |=  (0x0); 	// Writing 0 to the first 4 bits selects Port A for Line 8.
	// Configure EXTI Line 8
	EXTI->IMR |= (1 << 8);    // Unmask (enable) Interrupt Line 13
	EXTI->FTSR |= (1 << 8);   // Enable Falling Edge Trigger (detects Grounding)
	EXTI->RTSR |= (1 << 8);  // Enable Rising Edge
	NVIC_SetPriority(EXTI9_5_IRQn, 15);
	NVIC_EnableIRQ(EXTI9_5_IRQn);
}

void USB_OTG_FS_Init(void) {

	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
	GPIOA->MODER &= ~(GPIO_MODER_MODER11 | GPIO_MODER_MODER12); //  Configure PA11 (DM) and PA12 (DP) as Alternate Function (AF10)
	GPIOA->MODER |= (GPIO_MODER_MODER11_1 | GPIO_MODER_MODER12_1);   // Mode: 10 (Alternate Function)
	GPIOA->AFR[1] &= ~(0xF << GPIO_AFRH_AFSEL11_Pos);
	GPIOA->AFR[1] |=  (10U << GPIO_AFRH_AFSEL11_Pos);
	GPIOA->AFR[1] &= ~(0xF << GPIO_AFRH_AFSEL12_Pos);
	GPIOA->AFR[1] |=  (10U << GPIO_AFRH_AFSEL12_Pos);
	GPIOA->PUPDR &= ~(1<<22 | 1<<24); // No pull-up, no pull-down
	GPIOA->OSPEEDR |= (GPIO_OSPEEDER_OSPEEDR11 | GPIO_OSPEEDER_OSPEEDR12); 	// Speed: 11 (Very High Speed)
	/* PA9 (VBUS) Configuration: the VBUS sensing on PA9 bypasses the standard AF multiplexer
	 and connects directly to the sensing block when enabled in the USB core (not needed in USB Device mode) */
	GPIOA->MODER &= ~(GPIO_MODER_MODER9); 	// Set to Input mode (00) - This is the "Default State"
	GPIOA->PUPDR &= ~(GPIO_PUPDR_PUPDR9); 	//Ensure No Pull-up/Pull-down to allow the 200 uA sensing circuit to work

	RCC->CR |= RCC_CR_HSEON; // Enable HSE (8MHz External Crystal)
	while(!(RCC->CR & RCC_CR_HSERDY));

	// Disable the PLL to modify the registers
	RCC->CR |= RCC_CR_HSION; 	// Enable HSI (Internal High Speed oscillator)
	while (!(RCC->CR & RCC_CR_HSIRDY));     //  Wait until HSI is ready
	RCC->CFGR &= ~RCC_CFGR_SW;   // Switch System Clock (SYSCLK) to HSI
	RCC->CFGR |= RCC_CFGR_SW_HSI;   // Clear SW bits and set them to 00 (HSI select)
	while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI);  // 4. Wait until HSI is actually being used as the system clock,  SWS bits (System Clock Switch Status) should report 00
	RCC->CR &= ~RCC_CR_PLLON; 	// The PLL is now "free" and can be safely disabled, Disable the main PLL
	while (RCC->CR & RCC_CR_PLLRDY);     // Wait until PLL is fully stopped
	WRITE_REG(RCC->PLLCFGR, (0)); // !! NEEDED !! Why !?

	// Configure Main PLL (M=4, N=180, P=2), f_VCO = 8MHz*(180/4) = 360MHz,  f_SYS = 360MHz/2 = 180MHz
	RCC->PLLCFGR |= RCC_PLLCFGR_PLLSRC_HSE 		//  main PLL clock source = HSE
			     |  RCC_PLLCFGR_PLLM_2
				 |  180 << RCC_PLLCFGR_PLLN_Pos
				 |  (6 << RCC_PLLCFGR_PLLQ_Pos)
				 |  (RCC_PLLCFGR_PLLR_1);
	RCC->PLLCFGR &= ~(RCC_PLLCFGR_PLLP_Msk);

	// Re-enable the PLL
	RCC->CR |= RCC_CR_PLLON;
	while (!(RCC->CR & RCC_CR_PLLRDY)); //  Wait for the PLL to lock

	//The Over-drive Sequence is strict. If you skip a step, the MCU will likely hang during the clock switch.
	RCC->APB1ENR |= RCC_APB1ENR_PWREN; // Enable Power Control clock,
	PWR->CR |= PWR_CR_VOS; //Set Regulator Voltage Scaling to Scale 1
	PWR->CR |= PWR_CR_ODEN; // Enable Over-Drive Mode
	while(!(PWR->CSR & PWR_CSR_ODRDY));
	PWR->CR |= PWR_CR_ODSWEN; // Switch to Over-Drive
	while(!(PWR->CSR & PWR_CSR_ODSWRDY));

	// Configure Flash Latency and ART Accelerator, it's critical to do this before switching the clock to 180MHz
	FLASH->ACR = FLASH_ACR_ICEN           // Instruction Cache Enable
			   | FLASH_ACR_DCEN          // Data Cache Enable
			   | FLASH_ACR_PRFTEN       // Prefetch Enable
			   | FLASH_ACR_LATENCY_5WS;    // Implement the 5-cycle latency & the power scaling required for 180MHz

	while (!(RCC->CR & RCC_CR_PLLRDY)); //  Wait for the PLL to lock
	RCC->CFGR &= ~RCC_CFGR_SW;
	RCC->CFGR |= RCC_CFGR_SW_PLL;   // Switch System Clock to PLL, Flash Latency is set correctly for new speed
	while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);

	//Configure Prescalers (AHB=1, APB1=4, APB2=2), work without these instructions though
	RCC->CFGR |= RCC_CFGR_HPRE_DIV1
			  |  RCC_CFGR_PPRE1_DIV4
			  |  RCC_CFGR_PPRE2_DIV2;

	// Configure PLLSAI for USB 48MHz
	RCC->PLLSAICFGR = (144 << RCC_PLLSAICFGR_PLLSAIN_Pos) | (RCC_PLLSAICFGR_PLLSAIP_1); // f_VCO_SAI = (f_HSE/M)*N = (8/4)*144 = 288MHz
	RCC->CR |= RCC_CR_PLLSAION; 	// PLLSAI enable
	while(!(RCC->CR & RCC_CR_PLLSAIRDY));
	RCC->DCKCFGR |= RCC_DCKCFGR_CK48MSEL; // select PLLSAI as the 48MHz source for USB

	RCC->AHB2ENR |= RCC_AHB2ENR_OTGFSEN; // USB OTG FS clock enable
	RCC->AHB1ENR |= RCC_AHB1ENR_OTGHSEN; // OTGHSEN: USB OTG HS clock enable

	/* 	Other configuration parameters are not useful here, TinyUSB takes care of interrupts activation and so forth :
		NVIC_EnableIRQ(OTG_FS_IRQn);
		USB_OTG_FS->GAHBCFG |= USB_OTG_GAHBCFG_GINT;     // Enable Global Interrupt bit
		USB_OTG_FS->GINTMSK |= (USB_OTG_GINTMSK_USBRST| USB_OTG_GINTMSK_ENUMDNEM); // Non-masked Interrupts
		USB_OTG_FS->GRSTCTL |= USB_OTG_GRSTCTL_CSRST; //  Core Reset (Global Register Block)
		USB_OTG_FS->GUSBCFG |= USB_OTG_GUSBCFG_FDMOD;   // Force Device Mode
		Etc...
	 */
}

void ITM_Init(void) {
    // Enable Trace in the Debug Exception and Monitor Control Register
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; // DEMCR: 32-bit Debug Exception and Monitor Control Register. Provides Vector Catching and Debug Monitor Control.
    // Unlock the ITM Lock Access Register (Magic Key)
    ITM->LAR = 0xC5ACCE55;
    // Enable the ITM Control Register and Stimulus Port 0
    ITM->TCR |= ITM_TCR_ITMENA_Msk;
    ITM->TER |= (1UL << 0);
}

void SWV_SendString(const char *str) {
    while (*str) {
        ITM_SendChar(*str++);
    }
}


