#ifndef TOOLLIB_PAL_UART_HOST_H
#define TOOLLIB_PAL_UART_HOST_H

#include "pal_uart.h"

/* Single-threaded host fake for module tests. Callbacks run synchronously
 * from these helpers. DMA binding and driver start failures are injectable. */
void PAL_UART_Host_Reset(void);
/* Configure the fake board's generated defaults and wired directions. */
PalUartStatus PAL_UART_Host_Define(PalUartId id,
                                   const PalUartConfig *default_config,
                                   uint8_t wired_directions);
PalUartStatus PAL_UART_Host_SetExternalReady(PalUartId id, int ready);
/* status must be BUSY or IO_ERROR; consumed by the next eligible reconfigure. */
PalUartStatus PAL_UART_Host_SetNextReconfigureResult(PalUartId id,
                                                        PalUartStatus status);
/* Like SetNextReconfigureResult, but applies the hardware settings before failing. */
PalUartStatus PAL_UART_Host_SetNextModifiedReconfigureResult(PalUartId id,
                                                            PalUartStatus status);
PalUartStatus PAL_UART_Host_SetDmaBound(PalUartId id, PalUartDirection direction,
                                          int bound);
/* status must be BUSY or IO_ERROR; consumed by the next eligible start. */
PalUartStatus PAL_UART_Host_SetNextStartResult(PalUartId id,
                                                   PalUartDirection direction,
                                                   PalUartStatus status);
PalUartStatus PAL_UART_Host_FeedRx(PalUartId id, const uint8_t *data, size_t length);
PalUartStatus PAL_UART_Host_CompleteTx(PalUartId id, PalUartStatus status,
                                         size_t transferred);
size_t PAL_UART_Host_TakeTx(PalUartId id, uint8_t *out, size_t capacity);

#endif
