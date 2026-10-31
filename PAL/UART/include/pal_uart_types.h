//
// Created by VanSharkholme on 2026/10/27.
//

#ifndef TOOLLIB_PAL_UART_TYPES_H
#define TOOLLIB_PAL_UART_TYPES_H

#include <stddef.h>
#include <stdint.h>
/* IDs denote physical peripheral instances. Target headers may name their IDs;
 * application modules should receive an ID from board configuration. */
typedef enum
{
    PAL_UART1, PAL_UART2, PAL_UART3, PAL_UART4,
    PAL_UART5, PAL_UART6, PAL_UART7, PAL_UART8,
    PAL_UART9, PAL_UART10, PAL_UART11, PAL_UART12,
    PAL_UART13, PAL_UART14, PAL_UART15, PAL_UART16,
} PalUartId;

typedef enum {
    PAL_UART_STATUS_OK = 0,
    PAL_UART_STATUS_INVALID_ID,
    PAL_UART_STATUS_INVALID_ARGUMENT,
    PAL_UART_STATUS_UNSUPPORTED,
    PAL_UART_STATUS_NOT_INITIALIZED,
    PAL_UART_STATUS_ALREADY_INITIALIZED,
    PAL_UART_STATUS_BUSY,
    PAL_UART_STATUS_NOT_ACTIVE,
    PAL_UART_STATUS_TIMEOUT,
    PAL_UART_STATUS_IO_ERROR,
    PAL_UART_STATUS_CANCELLED,
    PAL_UART_STATUS_PORT_ERROR,
    PAL_UART_STATUS_INVALID_STATE,
} PalUartStatus;

typedef enum { PAL_UART_DATA_7BITS = 7, PAL_UART_DATA_8BITS = 8 } PalUartDataBits;
typedef enum { PAL_UART_PARITY_NONE, PAL_UART_PARITY_EVEN, PAL_UART_PARITY_ODD } PalUartParity;
typedef enum { PAL_UART_STOP_1, PAL_UART_STOP_2 } PalUartStopBits;
typedef enum { PAL_UART_DIR_TX = 1u, PAL_UART_DIR_RX = 2u, PAL_UART_DIR_TXRX = 3u } PalUartDirection;
typedef enum { PAL_UART_ASYNC_IRQ, PAL_UART_ASYNC_DMA } PalUartAsyncMode;

typedef struct {
    uint32_t baud_rate;
    PalUartDataBits data_bits; /* Payload bits, excluding any parity bit. */
    PalUartParity parity;
    PalUartStopBits stop_bits;
    PalUartDirection directions; /* PAL_UART_DIR_TX | PAL_UART_DIR_RX */
} PalUartConfig;

typedef void (*PalUartCompletion)(PalUartId id, PalUartDirection direction,
                                  PalUartStatus status, size_t transferred,
                                  void *context);
#endif //TOOLLIB_PAL_UART_TYPES_H
