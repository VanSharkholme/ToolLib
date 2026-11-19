//
// Created by VanSharkholme on 2026/11/10.
//

#ifndef TOOLLIB_PAL_SPI_TYPES_H
#define TOOLLIB_PAL_SPI_TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    PAL_SPI1, PAL_SPI2, PAL_SPI3, PAL_SPI4,
    PAL_SPI5, PAL_SPI6, PAL_SPI7, PAL_SPI8,
} PalSpiId;

typedef enum
{
    PAL_SPI_STATUS_OK = 0,
    PAL_SPI_STATUS_INVALID_ID,
    PAL_SPI_STATUS_INVALID_ARGUMENT,
    PAL_SPI_STATUS_UNSUPPORTED,
    PAL_SPI_STATUS_NOT_INITIALIZED,
    PAL_SPI_STATUS_ALREADY_INITIALIZED,
    PAL_SPI_STATUS_BUSY,
    PAL_SPI_STATUS_NOT_ACTIVE,
    PAL_SPI_STATUS_TIMEOUT,
    PAL_SPI_STATUS_IO_ERROR,
    PAL_SPI_STATUS_PORT_ERROR,
    PAL_SPI_STATUS_INVALID_STATE,
} PalSpiStatus;

typedef enum: uint8_t
{
    PAL_SPI_ROLE_MASTER = 0,
    PAL_SPI_ROLE_SLAVE,
} PalSpiRole;

typedef enum: uint8_t
{
    PAL_SPI_DATA_LINES_SEPARATED = 0,
    PAL_SPI_DATA_LINES_SHARED,
} PalSpiDataLines;

typedef enum: uint8_t
{
    PAL_SPI_DIR_TX = 1u << 0,
    PAL_SPI_DIR_RX = 1u << 1,
    PAL_SPI_DIR_TXRX = PAL_SPI_DIR_TX | PAL_SPI_DIR_RX,
} PalSpiDirection;

typedef enum: uint8_t
{
    PAL_SPI_ASYNC_NONE = 0,
    PAL_SPI_ASYNC_IRQ,
    PAL_SPI_ASYNC_DMA,
} PalSpiAsyncMode;

typedef enum: uint8_t
{
    PAL_SPI_ASYNC_SUPPORT_NONE = 0,
    PAL_SPI_ASYNC_SUPPORT_IRQ,
    PAL_SPI_ASYNC_SUPPORT_DMA,
    PAL_SPI_ASYNC_SUPPORT_IRQ_DMA,
} PalSpiAsyncSupport;

typedef enum: uint8_t
{
    PAL_SPI_CLOCK_POLARITY_LOW = 0,
    PAL_SPI_CLOCK_POLARITY_HIGH,
} PalSpiClockPolarity;

typedef enum: uint8_t
{
    PAL_SPI_CLOCK_PHASE_1EDGE = 0,
    PAL_SPI_CLOCK_PHASE_2EDGE,
} PalSpiClockPhase;

typedef enum: uint8_t
{
    PAL_SPI_NSS_SOFTWARE = 0,
    PAL_SPI_NSS_HARDWARE,
} PalSpiNssControl;

typedef enum: uint8_t
{
    PAL_SPI_NSS_POLARITY_LOW = 0,
    PAL_SPI_NSS_POLARITY_HIGH,
} PalSpiNssPolarity;

typedef enum: uint8_t
{
    PAL_SPI_BIT_ORDER_MSB_FIRST = 0,
    PAL_SPI_BIT_ORDER_LSB_FIRST,
} PalSpiBitOrder;

typedef enum
{
    PAL_SPI_TRANSFER_OK = 0, /**< The transfer completed successfully. */
    PAL_SPI_TRANSFER_CANCELLED, /**< The transfer was cancelled by user. */
    PAL_SPI_TRANSFER_ABORTED, /**< The transfer terminated due to fatal errors. */
} PalSpiTransferResult;

typedef enum
{
    PAL_SPI_ERROR_NONE = 0u,
    PAL_SPI_ERROR_OVERRUN = 1u << 0,
    PAL_SPI_ERROR_MODE_FAULT = 1u << 1,
    PAL_SPI_ERROR_CRC = 1u << 2,
    PAL_SPI_ERROR_DMA = 1u << 3,
    PAL_SPI_ERROR_OTHER = 1u << 4,
} PalSpiErrors;

typedef enum: uint8_t
{
    PAL_SPI_DATA_SIZE_4BIT = 4, PAL_SPI_DATA_SIZE_5BIT = 5,
    PAL_SPI_DATA_SIZE_6BIT = 6, PAL_SPI_DATA_SIZE_7BIT = 7,
    PAL_SPI_DATA_SIZE_8BIT = 8, PAL_SPI_DATA_SIZE_9BIT = 9,
    PAL_SPI_DATA_SIZE_10BIT = 10, PAL_SPI_DATA_SIZE_11BIT = 11,
    PAL_SPI_DATA_SIZE_12BIT = 12, PAL_SPI_DATA_SIZE_13BIT = 13,
    PAL_SPI_DATA_SIZE_14BIT = 14, PAL_SPI_DATA_SIZE_15BIT = 15,
    PAL_SPI_DATA_SIZE_16BIT = 16, PAL_SPI_DATA_SIZE_17BIT = 17,
    PAL_SPI_DATA_SIZE_18BIT = 18, PAL_SPI_DATA_SIZE_19BIT = 19,
    PAL_SPI_DATA_SIZE_20BIT = 20, PAL_SPI_DATA_SIZE_21BIT = 21,
    PAL_SPI_DATA_SIZE_22BIT = 22, PAL_SPI_DATA_SIZE_23BIT = 23,
    PAL_SPI_DATA_SIZE_24BIT = 24, PAL_SPI_DATA_SIZE_25BIT = 25,
    PAL_SPI_DATA_SIZE_26BIT = 26, PAL_SPI_DATA_SIZE_27BIT = 27,
    PAL_SPI_DATA_SIZE_28BIT = 28, PAL_SPI_DATA_SIZE_29BIT = 29,
    PAL_SPI_DATA_SIZE_30BIT = 30, PAL_SPI_DATA_SIZE_31BIT = 31,
    PAL_SPI_DATA_SIZE_32BIT = 32,
} PalSpiDataSize;

typedef struct
{
    uint32_t max_clk_freq_hz;
    uint32_t actual_clk_freq_hz; /**< Actual clock frequency corresponding to current prescaler configuration. */
    PalSpiRole role;
    PalSpiDataSize data_size;
    PalSpiDataLines data_lines;
    PalSpiDirection directions;
    PalSpiClockPolarity clk_polarity;
    PalSpiClockPhase clk_phase;
    PalSpiNssControl nss_control;
    PalSpiNssPolarity nss_polarity;
    PalSpiBitOrder bit_order;
} PalSpiConfig;

typedef void (*PalSpiCompletion)(
    PalSpiId id, PalSpiTransferResult result, size_t transferred,
    PalSpiErrors errors, void* context
);

#endif //TOOLLIB_PAL_SPI_TYPES_H
