#ifndef TOOLLIB_PAL_UART_PORT_H
#define TOOLLIB_PAL_UART_PORT_H

#include "pal_uart_types.h"
#include <stdbool.h>

/* Internal contract. PAL owns these fields; the selected port supplies stable,
 * zero-initialized storage and an opaque hardware context for each physical ID.
 * Calls, interrupts and callbacks must be coordinated by the caller. No locks
 * or scheduling are provided by PAL or by this contract. */
typedef enum {
    PAL_UART_UNBOUND = 0,
    PAL_UART_OWNED,
    PAL_UART_ATTACHED
} PalUartBinding;

typedef struct {
    PalUartBinding binding;
    bool ready;
    PalUartConfig config; /* Last successfully applied or attached settings. */
} PalUartState;

typedef struct {
    PalUartState state;
    void *port_context;
} PalUartDevice;

/* Lookup has no hardware side effects. IDs need not be contiguous. */
PalUartDevice *PAL_UART_Port_Find(PalUartId id);
/* Reports outstanding transfers in the requested directions, including port-
 * owned async requests. Failed configuration is not an outstanding transfer:
 * this check must allow explicit reconfiguration/deinitialization recovery. */
PalUartStatus PAL_UART_Port_CheckIdle(void *port_context, PalUartDirection directions);
/* Init rejects externally initialized hardware. On failure it cleans up its
 * partial initialization. Config outputs are written only on success. Attach
 * validates and decodes the existing hardware without reinitializing it. */
PalUartStatus PAL_UART_Port_Init(void *port_context, PalUartConfig *applied);
PalUartStatus PAL_UART_Port_AttachInitialized(void *port_context, PalUartConfig *current);
PalUartStatus PAL_UART_Port_DeInit(void *port_context);
/* Set modified=false before validation and true before the first write to
 * running configuration (including vendor handle configuration fields).
 * Rejection before modification preserves availability; failure afterwards
 * invalidates it. No automatic rollback is required. */
PalUartStatus PAL_UART_Port_Reconfigure(void *port_context,
                                      const PalUartConfig *config, bool *modified);
/* Public PAL has validated arguments, binding, availability, direction and idle
 * state. The port checks hardware limits and returns the actual byte count. */
PalUartStatus PAL_UART_Port_Write(void *port_context, const uint8_t *data, size_t length,
                                uint32_t timeout_ms, size_t *transferred);
PalUartStatus PAL_UART_Port_Read(void *port_context, uint8_t *data, size_t length,
                               uint32_t timeout_ms, size_t *transferred);

/* Async request ownership stays in the port for this migration. PAL performs
 * common entry checks, then these existing ID-based hooks manage completion
 * and cancellation exactly once. Callbacks may run before Start* returns. */
PalUartStatus PAL_UART_Port_StartTx(PalUartId id, PalUartAsyncMode mode,
                                  const uint8_t *data, size_t length,
                                  PalUartCompletion completion, void *context);
PalUartStatus PAL_UART_Port_StartRx(PalUartId id, PalUartAsyncMode mode,
                                  uint8_t *data, size_t length,
                                  PalUartCompletion completion, void *context);
PalUartStatus PAL_UART_Port_Cancel(PalUartId id, PalUartDirection direction);

#endif
