#ifndef STM32F4_BSP_UART_H
#define STM32F4_BSP_UART_H
#include "pal_uart_types.h"
#include "stm32f4xx_hal.h"
#include <stdbool.h>

#define BSP_UART_NUM (PAL_UART6 + 1) /* ID slot count, including holes */

UART_HandleTypeDef *BSP_UART_Get_Handle(PalUartId id);
USART_TypeDef *BSP_UART_Get_Instance(PalUartId id);
const UART_InitTypeDef *BSP_UART_Get_Init(PalUartId id);
const PalUartConfig *BSP_UART_Get_Config(PalUartId id);
PalUartAsyncSupport BSP_UART_Get_TxAsyncSupport(PalUartId id);
PalUartAsyncSupport BSP_UART_Get_RxAsyncSupport(PalUartId id);
#endif //STM32F4_BSP_UART_H
