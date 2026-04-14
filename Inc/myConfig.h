#include <stm32f469xx.h>

void SysClockConfig (void);

void activateFPU(void);

void GPIO_Config (void);

void InterruptGPIO_Config (void);

void USB_OTG_FS_Init(void);

void ITM_Init(void);
void SWV_SendString(const char *str);
