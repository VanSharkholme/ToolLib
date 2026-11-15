/**
 * @file pal_uart_port.h
 * @brief Internal contract between the UART PAL and the selected platform port.
 *
 * @details PAL manages binding, availability and portable configuration. The
 * port supplies stable, initially zero-initialized device storage, physical ID
 * lookup and an opaque hardware context, and performs vendor-specific operations.
 *
 * @note The caller coordinates tasks, interrupts and callbacks. This contract
 * provides no locking, scheduling or reentrancy guarantee.
 */
#ifndef TOOLLIB_PAL_UART_PORT_H
#define TOOLLIB_PAL_UART_PORT_H

#include "pal_uart_types.h"
#include <stdbool.h>

/**
 * @brief Binding and hardware teardown responsibility of a UART device.
 */
typedef enum {
    PAL_UART_UNBOUND = 0, /**< No current PAL binding exists. */
    PAL_UART_OWNED, /**< PAL initialized the hardware; release with PAL_UART_DeInit(). */
    PAL_UART_ATTACHED /**< Hardware was initialized externally; release with PAL_UART_Detach(). */
} PalUartBinding;

typedef enum
{
    PAL_UART_ASYNC_IDLE = 0,
    PAL_UART_ASYNC_ACTIVE,
    PAL_UART_ASYNC_CANCEL_PENDING,
} PalUartAsyncState;


/**
 * @brief Common lifecycle and configuration state managed by PAL.
 *
 * @note Binding is retained when a configuration failure makes the device
 * unavailable, so the correct reconfiguration or release path remains usable.
 */
typedef struct {
    PalUartBinding binding; /**< Current binding and hardware teardown responsibility. */
    bool ready; /**< Whether the bound device is available for transfers. */
    PalUartConfig config; /**< Last successfully applied or attached portable configuration. */
} PalUartState;

/**
 * @brief Bookkeeping for one asynchronous transfer direction.
 *
 * @note The buffer and callback context must remain valid until the request
 * finishes. The caller must coordinate access from tasks and interrupts.
 */
typedef struct
{
    PalUartAsyncState state; /**< Current transfer state. */
    size_t length; /**< Transfer length in bytes, or buffer capacity for RX to idle. */
    PalUartAsyncMode mode; /**< Requested asynchronous transfer mode. */
    PalUartCompletion callback; /**< Completion callback associated with the request. */
    void *context; /**< User context passed to the completion callback. */
} PalUartAsyncTransfer;

/**
 * @brief Common UART device storage supplied by a platform port.
 *
 * @details The port supplies stable storage and initializes port_context. PAL
 * manages the common state; vendor-specific resources remain behind the opaque
 * context. Separate transfer records allow independent TX and RX requests.
 */
typedef struct {
    PalUartState state; /**< PAL-managed binding, availability and configuration. */
    void *port_context; /**< Opaque platform context used by the hardware hooks. */
    PalUartAsyncTransfer tx; /**< Asynchronous transmit request bookkeeping. */
    PalUartAsyncTransfer rx; /**< Asynchronous receive request bookkeeping. */
} PalUartDevice;

/**
 * @brief Looks up the device storage for a physical UART ID.
 *
 * @param[in] id Physical UART identifier; IDs need not be contiguous.
 * @return Pointer to stable device storage for a mapped ID.
 * @retval NULL The ID has no device mapping in the selected port.
 * @note Lookup has no hardware side effects and does not establish a binding.
 */
PalUartDevice *PAL_UART_Port_Find(PalUartId id);

/**
 * @brief Checks for outstanding transfers in the requested directions.
 *
 * @param[in] port_context Valid platform context obtained from a mapped device.
 * @param[in] directions TX, RX, or TXRX directions to check.
 * @retval PAL_UART_STATUS_OK No transfer is outstanding in the requested directions.
 * @retval PAL_UART_STATUS_BUSY A requested direction has an outstanding transfer.
 * @return A platform error status if the check cannot be completed.
 * @note Include port-owned asynchronous requests. A failed configuration is not
 * an outstanding transfer and must not prevent explicit recovery through
 * reconfiguration or deinitialization.
 */
PalUartStatus PAL_UART_Port_CheckIdle(void *port_context, PalUartDirection directions);

/**
 * @brief Initializes UART hardware using its board-defined default settings.
 *
 * @param[in] port_context Valid platform context obtained from a mapped device.
 * @param[out] applied Portable configuration applied on success; unchanged on failure.
 * @pre applied is non-NULL. PAL has checked that no binding already exists.
 * @retval PAL_UART_STATUS_OK Hardware initialization completed successfully.
 * @retval PAL_UART_STATUS_BUSY Hardware is already initialized externally.
 * @return An appropriate error status if initialization fails.
 * @note Clean up partial hardware initialization on failure. PAL establishes
 * the owned binding only after this function succeeds.
 */
PalUartStatus PAL_UART_Port_Init(void *port_context, PalUartConfig *applied);

/**
 * @brief Validates externally initialized UART hardware and reads its settings.
 *
 * @param[in] port_context Valid platform context obtained from a mapped device.
 * @param[out] current Current portable configuration on success; unchanged on failure.
 * @pre current is non-NULL. PAL has checked that no binding already exists.
 * @retval PAL_UART_STATUS_OK An idle asynchronous UART was validated and decoded.
 * @retval PAL_UART_STATUS_NOT_INITIALIZED Hardware has not been initialized or enabled.
 * @retval PAL_UART_STATUS_BUSY A transfer is still outstanding.
 * @retval PAL_UART_STATUS_UNSUPPORTED The existing mode or format cannot be supported.
 * @return An appropriate platform error status for other validation failures.
 * @note Do not reinitialize the hardware or replace its existing configuration.
 */
PalUartStatus PAL_UART_Port_AttachInitialized(void *port_context, PalUartConfig *current);

/**
 * @brief Deinitializes UART hardware owned by PAL.
 *
 * @param[in] port_context Valid platform context obtained from a mapped device.
 * @pre PAL has verified owned binding and no outstanding TX or RX transfers.
 * @retval PAL_UART_STATUS_OK Hardware teardown completed successfully.
 * @return An appropriate platform error status if teardown fails.
 * @note PAL releases the binding on success. On failure it retains ownership
 * and marks the device unavailable so teardown can be retried.
 */
PalUartStatus PAL_UART_Port_DeInit(void *port_context);

/**
 * @brief Applies portable settings to a bound UART device.
 *
 * @param[in] port_context Valid platform context obtained from a mapped device.
 * @param[in] config Validated portable UART configuration to apply.
 * @param[out] modified Whether running configuration has been modified during this call.
 * @pre config and modified are non-NULL. PAL has checked binding and idle TX/RX.
 * @retval PAL_UART_STATUS_OK The requested configuration was applied successfully.
 * @retval PAL_UART_STATUS_UNSUPPORTED Required board resources or settings are unsupported.
 * @return An appropriate error status if validation or hardware configuration fails.
 * @note Set modified to false before validation and to true before the first
 * write to running configuration, including vendor handle configuration fields.
 * Failure before modification preserves availability; failure after modification
 * makes the device unavailable. No automatic rollback is required. Preserve
 * vendor-specific settings outside the portable configuration.
 */
PalUartStatus PAL_UART_Port_Reconfigure(void *port_context,
                                      const PalUartConfig *config, bool *modified);

/**
 * @brief Transmits bytes using a blocking platform operation.
 *
 * @param[in] port_context Valid platform context obtained from a mapped device.
 * @param[in] data Buffer containing at least length bytes to transmit.
 * @param[in] length Requested number of bytes; greater than zero.
 * @param[in] timeout_ms Timeout in milliseconds; PAL_UART_WAIT_FOREVER requests no timeout.
 * @param[out] transferred Actual byte count, including a partial count on timeout.
 * @pre PAL has validated arguments, binding, availability, TX direction and idle state.
 * @pre data and transferred are non-NULL; the output count was initialized to zero.
 * @retval PAL_UART_STATUS_OK All requested bytes were transmitted.
 * @retval PAL_UART_STATUS_TIMEOUT The operation timed out; inspect transferred.
 * @retval PAL_UART_STATUS_UNSUPPORTED A platform limit or required resource is unsupported.
 * @return An appropriate platform status for other driver outcomes.
 * @note The port checks hardware-specific limits and reports the driver's actual count.
 */
PalUartStatus PAL_UART_Port_Write(void *port_context, const uint8_t *data, size_t length,
                                uint32_t timeout_ms, size_t *transferred);

/**
 * @brief Receives bytes using a blocking platform operation.
 *
 * @param[in] port_context Valid platform context obtained from a mapped device.
 * @param[in] to_idle Whether IDLE after receiving data may complete the read early.
 * @param[out] data Writable buffer with space for at least length bytes.
 * @param[in] length Required byte count, or buffer capacity if to_idle; greater than zero.
 * @param[in] timeout_ms Whole-read timeout in milliseconds; PAL_UART_WAIT_FOREVER requests no timeout.
 * @param[out] transferred Actual byte count, including a partial count on timeout.
 * @pre PAL has validated arguments, binding, availability, RX direction and idle state.
 * @pre data and transferred are non-NULL; the output count was initialized to zero.
 * @retval PAL_UART_STATUS_OK The buffer filled, or to_idle ended on IDLE after receiving data.
 * @retval PAL_UART_STATUS_TIMEOUT The operation timed out; inspect transferred.
 * @retval PAL_UART_STATUS_UNSUPPORTED The operation or a platform requirement is unsupported.
 * @return An appropriate platform status for other driver outcomes.
 * @note IDLE before any data does not finish reception. IDLE itself is neither
 * a timeout nor an error. Reject unsupported idle reception before consuming data.
 */
PalUartStatus PAL_UART_Port_Read(
    void *port_context, bool to_idle, uint8_t *data,
    size_t length, uint32_t timeout_ms, size_t *transferred
);

/**
 * @brief Starts an asynchronous transmit operation in IRQ or DMA mode.
 *
 * @param[in] port_context Valid platform context obtained from a mapped device.
 * @param[in] mode Requested asynchronous transfer mode.
 * @param[in] data Buffer containing at least length bytes; valid and unmodified until completion.
 * @param[in] length Requested number of bytes; greater than zero.
 * @pre PAL has performed common argument, binding, availability, direction and idle checks.
 * @retval PAL_UART_STATUS_OK The request was accepted; it may already have completed.
 * @retval PAL_UART_STATUS_UNSUPPORTED The mode, resources or platform constraints are unsupported.
 * @return An appropriate platform status if the request cannot be started.
 * @note Check actual resource bindings and driver state before accepting the
 * request. Accepted requests notify completion exactly once, including cancellation;
 * rejected requests never invoke completion. Release request state before invoking
 * user code. The callback may execute in interrupt context or before this call returns.
 * DMA buffers must satisfy platform memory, alignment and cache requirements.
 */
PalUartStatus PAL_UART_Port_StartTx(void *port_context, PalUartAsyncMode mode,
                                  const uint8_t *data, size_t length);

/**
 * @brief Starts an asynchronous receive operation in IRQ or DMA mode.
 *
 * @param[in] port_context Valid platform context obtained from a mapped device.
 * @param[in] mode Requested asynchronous transfer mode.
 * @param[out] data Writable buffer with space for length bytes; remains valid until completion.
 * @param[in] length Required byte count, or buffer capacity if to_idle; greater than zero.
 * @param[in] to_idle Whether IDLE after receiving data may complete the request early.
 * @pre PAL has performed common argument, binding, availability, direction and idle checks.
 * @retval PAL_UART_STATUS_OK The request was accepted; it may already have completed.
 * @retval PAL_UART_STATUS_UNSUPPORTED The mode, resources or platform constraints are unsupported.
 * @return An appropriate platform status if the request cannot be started.
 * @note Check actual resource bindings and driver state before accepting the
 * request. Accepted requests notify completion exactly once, including cancellation;
 * rejected requests never invoke completion. Release request state before invoking
 * user code. The callback may execute in interrupt context or before this call returns.
 * DMA buffers must satisfy platform memory, alignment and cache requirements.
 */
/* RX to idle is one-shot: full capacity or IDLE after data completes the request
 * with its actual count. Ignore progress-only events such as DMA half-transfer;
 * do not report them as completion. IDLE is not RX_TIMEOUT. No receive timeout
 * is configured here. Unsupported idle reception must be rejected before
 * starting hardware or consuming data. Circular DMA is outside this contract. */
PalUartStatus PAL_UART_Port_StartRx(void *port_context, PalUartAsyncMode mode,
                                  uint8_t *data, size_t length, bool to_idle);

/**
 * @brief Requests cancellation of one outstanding asynchronous transfer direction.
 *
 * @param[in] port_context Valid platform context obtained from a mapped device.
 * @param[in] direction Single direction to cancel: PAL_UART_DIR_TX or PAL_UART_DIR_RX.
 * @pre PAL has validated the direction and verified that the device is bound.
 * @retval PAL_UART_STATUS_OK The cancellation request was accepted.
 * @retval PAL_UART_STATUS_NOT_ACTIVE No asynchronous request exists in that direction.
 * @retval PAL_UART_STATUS_UNSUPPORTED Cancellation is not implemented by the port.
 * @return An appropriate platform status if cancellation cannot be requested.
 * @note Cancellation must not release a buffer while hardware can still access it.
 * A cancelled request completes exactly once with PAL_UART_TRANSFER_CANCELLED and
 * its actual transferred count. Release request state before invoking its callback.
 */
PalUartStatus PAL_UART_Port_Cancel(void *port_context, PalUartDirection direction);

/* Finish one ACTIVE or CANCEL_PENDING request after hardware has stopped.
 * Clears its bookkeeping before invoking user code; the callback may submit
 * another request. Report cumulative detected errors, including on cancellation.
 * Call exactly once per accepted request; IDLE requests are ignored. */
void PAL_UART_AsyncFinish(PalUartId id, PalUartDirection direction,
                          PalUartTransferResult result, size_t transferred,
                          PalUartErrors errors);

#endif
