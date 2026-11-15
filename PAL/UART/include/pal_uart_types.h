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

/**
 * @brief Result of a UART API or Port operation.
 * @note For asynchronous start and cancel operations, OK means the request was
 * accepted. The terminal transfer result is reported through PalUartCompletion.
 * Blocking transfers report completion, timeout or I/O failure directly here.
 */
typedef enum
{
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
    PAL_UART_STATUS_PORT_ERROR,
    PAL_UART_STATUS_INVALID_STATE,
} PalUartStatus;

/** Detected errors accumulated over one request. Zero means none reported,
 * not that every category can be detected by the selected platform. */
typedef uint32_t PalUartErrors;
enum {
    PAL_UART_ERROR_NONE    = 0u,
    PAL_UART_ERROR_PARITY  = 1u << 0,
    PAL_UART_ERROR_FRAMING = 1u << 1,
    PAL_UART_ERROR_OVERRUN = 1u << 2,
    PAL_UART_ERROR_NOISE   = 1u << 3,
    PAL_UART_ERROR_DMA     = 1u << 4,
    PAL_UART_ERROR_OTHER   = 1u << 5,
};

/**
 * @brief Terminal result of an accepted asynchronous UART transfer.
 */
typedef enum
{
    PAL_UART_TRANSFER_OK = 0,    /**< The transfer completed successfully, including RX ending on IDLE when requested. */
    PAL_UART_TRANSFER_CANCELLED, /**< The transfer was cancelled by user. */
    PAL_UART_TRANSFER_FINISHED_WITH_ERROR, /**< The transfer completed with non-fatal errors. */
    PAL_UART_TRANSFER_ABORTED,  /**< The transfer terminated due to fatal errors. */
    PAL_UART_TRANSFER_ABORTED_WITH_ERROR, /**< The transfer terminated due to fatal errors and also detected parity, framing or noise errors. */
    PAL_UART_TRANSFER_RX_TIMEOUT, /**< The receive transfer timed out before the requested length was received. */
    PAL_UART_TRANSFER_RX_TIMEOUT_WITH_ERROR, /**< The receive transfer timed out and also detected parity, framing or noise errors. */
} PalUartTransferResult;

typedef enum { PAL_UART_DATA_7BITS = 7, PAL_UART_DATA_8BITS = 8 } PalUartDataBits;

typedef enum { PAL_UART_PARITY_NONE, PAL_UART_PARITY_EVEN, PAL_UART_PARITY_ODD } PalUartParity;

typedef enum { PAL_UART_STOP_1, PAL_UART_STOP_2 } PalUartStopBits;

typedef enum { PAL_UART_DIR_TX = 1u, PAL_UART_DIR_RX = 2u, PAL_UART_DIR_TXRX = 3u } PalUartDirection;

typedef enum { PAL_UART_ASYNC_NONE = 0, PAL_UART_ASYNC_IRQ, PAL_UART_ASYNC_DMA } PalUartAsyncMode;

typedef enum
{
    PAL_UART_ASYNC_SUPPORT_NONE = 0,
    PAL_UART_ASYNC_SUPPORT_IRQ,
    PAL_UART_ASYNC_SUPPORT_DMA,
    PAL_UART_ASYNC_SUPPORT_IRQ_DMA,
} PalUartAsyncSupport;

typedef struct
{
    uint32_t baud_rate;
    PalUartDataBits data_bits; /* Payload bits, excluding any parity bit. */
    PalUartParity parity;
    PalUartStopBits stop_bits;
    PalUartDirection directions; /* PAL_UART_DIR_TX | PAL_UART_DIR_RX */
} PalUartConfig;

/**
 * @brief Reports the terminal result of an asynchronous transfer.
 * @param id UART instance associated with the completed request.
 * @param direction Completed direction: PAL_UART_DIR_TX or PAL_UART_DIR_RX.
 * @param result Terminal transfer result, separate from API operation status.
 * @param transferred Bytes transferred; partial TX counts may include bytes handed
 * to the peripheral but not yet sent on the wire. Data may contain errors.
 * An idle-terminated RX may succeed with fewer bytes than the buffer capacity.
 * @param errors Accumulated detected errors; independent of the terminal result.
 * @param context User context supplied when the request was submitted.
 * @note The callback may run in interrupt context or before Start/Cancel returns.
 */
typedef void (*PalUartCompletion)(
    PalUartId id, PalUartDirection direction,
    PalUartTransferResult result, size_t transferred,
    PalUartErrors errors, void* context
);
#endif //TOOLLIB_PAL_UART_TYPES_H
