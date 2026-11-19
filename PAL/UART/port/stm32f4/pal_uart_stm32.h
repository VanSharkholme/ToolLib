#ifndef PAL_UART_STM32_H
#define PAL_UART_STM32_H

#include "stm32f4xx_hal.h"

/* Always pair: Before -> HAL_*_IRQHandler -> After.
 * Defer user completions until HAL IRQ returns; DMA hooks also snapshot abnormal progress.
 * An enclosing Start/Cancel scope defers notification further, until its LeaveHal.
 * These hooks provide neither locking nor task switching; callers coordinate PAL access. */
void PAL_UART_STM32_BeforeUartIRQ(UART_HandleTypeDef *huart);
void PAL_UART_STM32_AfterUartIRQ(UART_HandleTypeDef *huart);
void PAL_UART_STM32_BeforeDmaIRQ(DMA_HandleTypeDef *hdma);
void PAL_UART_STM32_AfterDmaIRQ(DMA_HandleTypeDef *hdma);

#endif
