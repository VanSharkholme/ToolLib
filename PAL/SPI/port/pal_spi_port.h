//
// Created by VanSharkholme on 2026/11/10.
//

#ifndef TOOLLIB_PAL_SPI_PORT_H
#define TOOLLIB_PAL_SPI_PORT_H

#include "pal_spi_types.h"
#include <stdbool.h>

typedef enum
{
    PAL_SPI_UNBOUND = 0, /**< No current PAL binding exists. */
    PAL_SPI_OWNED,       /**< PAL initialized the hardware; release with PAL_SPI_DeInit(). */
    PAL_SPI_ATTACHED     /**< Hardware was initialized externally; release with PAL_SPI_Detach(). */
} PalSpiBinding;

typedef enum
{
    PAL_SPI_ASYNC_IDLE = 0,
    PAL_SPI_ASYNC_ACTIVE,
    PAL_SPI_ASYNC_CANCEL_PENDING,
} PalSpiAsyncState;

typedef struct
{
    PalSpiBinding binding;
    bool ready;
    PalSpiConfig config;
} PalSpiState;

typedef struct
{
    PalSpiAsyncState state;
    size_t length;
    PalSpiAsyncMode mode;
    PalSpiCompletion callback;
    void* context;
} PalSpiAsyncTransfer;

typedef struct
{
    PalSpiState state;
    void* port_context;
    PalSpiAsyncTransfer transfer;
} PalSpiDevice;

PalSpiDevice* PAL_SPI_Port_Find(PalSpiId id);

PalSpiStatus PAL_SPI_Port_CheckIdle(void* port_context);

PalSpiStatus PAL_SPI_Port_Init(void* port_context, PalSpiConfig* applied);

PalSpiStatus PAL_SPI_Port_AttachInitialized(void* port_context, PalSpiConfig* current);

PalSpiStatus PAL_SPI_Port_DeInit(void* port_context);

PalSpiStatus PAL_SPI_Port_Reconfigure(void* port_context, PalSpiConfig* applied, bool* modified);

PalSpiStatus PAL_SPI_Port_Transmit(
    void* port_context, const uint8_t* tx_data, size_t length, uint32_t timeout_ms, size_t* transferred
);

PalSpiStatus PAL_SPI_Port_Receive(
    void* port_context, uint8_t* rx_data, size_t length, uint32_t timeout_ms, size_t* transferred
);

PalSpiStatus PAL_SPI_Port_TransmitReceive(
    void* port_context, const uint8_t* tx_data, uint8_t* rx_data, size_t length, uint32_t timeout_ms,
    size_t* transferred
);

PalSpiStatus PAL_SPI_Port_StartTx(void* port_context, PalSpiAsyncMode mode, const uint8_t* tx_data, size_t length);

PalSpiStatus PAL_SPI_Port_StartRx(void* port_context, PalSpiAsyncMode mode, uint8_t* rx_data, size_t length);

PalSpiStatus PAL_SPI_Port_StartTxRx(
    void* port_context, PalSpiAsyncMode mode, const uint8_t* tx_data, uint8_t* rx_data, size_t length
);

/* Claim ACTIVE -> CANCEL_PENDING under the IRQ guard; roll back a rejected abort before dispatch. */
PalSpiStatus PAL_SPI_Port_Cancel(void* port_context);

void PAL_SPI_AsyncFinish(
    PalSpiId id, PalSpiTransferResult result, size_t transferred,
    PalSpiErrors errors
);

#endif //TOOLLIB_PAL_SPI_PORT_H
