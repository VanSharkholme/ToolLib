#ifndef TOOLLIB_PAL_TIMER_STM32_H
#define TOOLLIB_PAL_TIMER_STM32_H

#include "pal_timer_port.h"
#include "stm32f4xx_hal.h"

/* Forward PAL timer updates here from the application's HAL_TIM_PeriodElapsedCallback. */
void PAL_Timer_STM32_Update_Callback(TIM_HandleTypeDef *htim);

#endif
