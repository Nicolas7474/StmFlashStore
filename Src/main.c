#include <stm32f469xx.h>
#include "myConfig.h"
#include "myTimers.h"
#include "stmFlashStore.h"


uint16_t buf_mode[5]; // globals for Debugger visualization
uint16_t buf_speed[1];
uint16_t buf_temp[1];

int main (void) {

	SysClockConfig();
	GPIO_Config();
	SysTick_Init();

	GPIOD->ODR^=GPIO_ODR_OD4; //orange
	GPIOD->ODR^=GPIO_ODR_OD5; // red
	GPIOG->ODR^=GPIO_ODR_OD6; // green

	ee_init();
	NBdelay_ms(50);
	uint16_t mode[5] = {1, 2, 3, 4, 5};
	uint16_t speed = 1234;
	uint16_t temp = 32;

	NBdelay_ms(100);

	if(!ee_write(EE_MODE, &mode, sizeof(mode)))
	{
		GPIOD->ODR^=GPIO_ODR_OD5; // red
	}
	if(!ee_write(EE_SPEED, &speed, sizeof(speed)))
	{
		GPIOG->ODR^=GPIO_ODR_OD6; // green
	}
	if(!ee_write(EE_TEMP, &temp, sizeof(temp)))
	{
		GPIOD->ODR^=GPIO_ODR_OD4; //orange
	}

	ee_read(EE_MODE, buf_mode, 10);
	ee_read(EE_SPEED, buf_speed,2);
	ee_read(EE_TEMP, buf_temp, 2);
}


