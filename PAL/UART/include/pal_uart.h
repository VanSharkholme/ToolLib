#ifndef TOOLLIB_PAL_UART_H
#define TOOLLIB_PAL_UART_H

#include "pal_uart_types.h"

/* Caller coordinates tasks, interrupts and callbacks for each device. These
 * functions provide no locking or reentrancy guarantee. Binding grants PAL
 * exclusive use of the vendor handle until release. */
#define PAL_UART_WAIT_FOREVER UINT32_MAX

/* Initialize one peripheral using the Port's board-defined default settings. */
PalUartStatus PAL_UART_Init(PalUartId id);
/* Call only after application/vendor-generated initialization has completed.
 * This path does not change the vendor configuration. */
PalUartStatus PAL_UART_AttachInitialized(PalUartId id);
/* Replace the portable UART settings, including TX/RX directions. The Port
 * rejects unavailable board resources before changing the active settings.
 * Requires a bound, idle peripheral; also valid for attached handles. Failure
 * before modifying the running configuration preserves availability; failure
 * after modification blocks transfers with INVALID_STATE until successful
 * reconfiguration or release. Binding and the last good config are retained. */
PalUartStatus PAL_UART_Reconfigure(PalUartId id, const PalUartConfig *config);
/* Deinitialize an instance created by Init. Attached instances return
 * INVALID_STATE; active transfers on the correct path return BUSY. */
PalUartStatus PAL_UART_DeInit(PalUartId id);
/* Release an AttachInitialized binding without changing the vendor-owned
 * hardware or configuration. Init-owned instances return INVALID_STATE;
 * active transfers on the correct path return BUSY. Both release functions
 * return NOT_INITIALIZED for an instance that is not currently bound. */
PalUartStatus PAL_UART_Detach(PalUartId id);

/* Reads exactly length bytes or returns TIMEOUT with a partial count. The
 * transferred output is set to zero before each valid port call. */
PalUartStatus PAL_UART_Write(PalUartId id, const uint8_t *data, size_t length,
                             uint32_t timeout_ms, size_t *transferred);
PalUartStatus PAL_UART_Read(PalUartId id, uint8_t *data, size_t length,
                            uint32_t timeout_ms, size_t *transferred);

/* One outstanding transfer per direction. Buffers must remain valid until the
 * callback; DMA buffers must also satisfy target memory/cache constraints.
 * Accepted requests invoke the callback exactly once, including on cancel.
 * A rejected request never invokes it. The Port checks actual resource binding
 * and driver state. Callbacks may run in interrupt context. */
PalUartStatus PAL_UART_StartTx(PalUartId id, PalUartAsyncMode mode,
                                const uint8_t *data, size_t length,
                                PalUartCompletion completion, void *context);
PalUartStatus PAL_UART_StartRx(PalUartId id, PalUartAsyncMode mode,
                                uint8_t *data, size_t length,
                                PalUartCompletion completion, void *context);
PalUartStatus PAL_UART_Cancel(PalUartId id, PalUartDirection direction);

#endif
