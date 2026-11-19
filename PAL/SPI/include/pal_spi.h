//
// Created by VanSharkholme on 2026/11/10.
//

#ifndef TOOLLIB_PAL_SPI_H
#define TOOLLIB_PAL_SPI_H

#include <stdbool.h>
#include "pal_spi_types.h"

#define PAL_SPI_WAIT_FOREVER UINT32_MAX

PalSpiStatus PAL_SPI_Init(PalSpiId id);

PalSpiStatus PAL_SPI_AttachInitialized(PalSpiId id);

PalSpiStatus PAL_SPI_Reconfigure(PalSpiId id, const PalSpiConfig* config);

PalSpiStatus PAL_SPI_DeInit(PalSpiId id);

PalSpiStatus PAL_SPI_Detach(PalSpiId id);

/* Lengths and completion counts are buffer bytes. Each 4..8, 9..16 or
 * 17..32 bit frame occupies one native uint8_t, uint16_t or uint32_t.
 * Supply whole frames in naturally aligned storage. Partial TX counts
 * describe bytes consumed from memory, including data queued in the FIFO. */
PalSpiStatus PAL_SPI_Transmit(
    PalSpiId id, const uint8_t* tx_data, size_t length, uint32_t timeout_ms, size_t* transferred
);

PalSpiStatus PAL_SPI_Receive(PalSpiId id, uint8_t* rx_data, size_t length, uint32_t timeout_ms, size_t* transferred);

PalSpiStatus PAL_SPI_TransmitReceive(
    PalSpiId id, const uint8_t* tx_data, uint8_t* rx_data, size_t length, uint32_t timeout_ms, size_t* transferred
);

PalSpiStatus PAL_SPI_StartTx(
    PalSpiId id, PalSpiAsyncMode mode, const uint8_t* tx_data, size_t length, PalSpiCompletion completion, void* context
);

PalSpiStatus PAL_SPI_StartRx(
    PalSpiId id, PalSpiAsyncMode mode, uint8_t* rx_data, size_t length, PalSpiCompletion completion, void* context
);

PalSpiStatus PAL_SPI_StartTxRx(
    PalSpiId id, PalSpiAsyncMode mode, const uint8_t* tx_data, uint8_t* rx_data, size_t length,
    PalSpiCompletion completion, void* context
);

PalSpiStatus PAL_SPI_Cancel(PalSpiId id);


#endif //TOOLLIB_PAL_SPI_H
