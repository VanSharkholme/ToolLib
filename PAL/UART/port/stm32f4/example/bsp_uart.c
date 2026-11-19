#include "bsp_uart.h"
#include "usart.h"

UART_HandleTypeDef *BSP_UART_Get_Handle(PalUartId id)
{
    switch (id)
    {
    case PAL_UART1: return &huart1;
    case PAL_UART2: return &huart2;
    case PAL_UART6: return &huart6;
    default: return NULL;
    }
}

USART_TypeDef *BSP_UART_Get_Instance(PalUartId id)
{
    switch (id)
    {
    case PAL_UART1: return USART1;
    case PAL_UART2: return USART2;
    case PAL_UART6: return USART6;
    default: return NULL;
    }
}

const UART_InitTypeDef *BSP_UART_Get_Init(PalUartId id)
{
    static const UART_InitTypeDef defaults = {
        .HwFlowCtl = UART_HWCONTROL_NONE,
        .OverSampling = UART_OVERSAMPLING_16,
    };
    return BSP_UART_Get_Handle(id) ? &defaults : NULL;
}

const PalUartConfig *BSP_UART_Get_Config(PalUartId id)
{
    static const PalUartConfig defaults = {
        .baud_rate = 115200,
        .data_bits = PAL_UART_DATA_8BITS,
        .parity = PAL_UART_PARITY_NONE,
        .stop_bits = PAL_UART_STOP_1,
        .directions = PAL_UART_DIR_TXRX,
    };
    return BSP_UART_Get_Handle(id) ? &defaults : NULL;
}

PalUartAsyncSupport BSP_UART_Get_TxAsyncSupport(PalUartId id)
{
    return BSP_UART_Get_Handle(id) ? PAL_UART_ASYNC_SUPPORT_IRQ_DMA : PAL_UART_ASYNC_SUPPORT_NONE;
}

PalUartAsyncSupport BSP_UART_Get_RxAsyncSupport(PalUartId id)
{
    return BSP_UART_Get_Handle(id) ? PAL_UART_ASYNC_SUPPORT_IRQ_DMA : PAL_UART_ASYNC_SUPPORT_NONE;
}
