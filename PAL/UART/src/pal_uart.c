#include "pal_uart.h"
#include "pal_uart_port.h"

static bool PAL_UART_ValidConfig(const PalUartConfig* config)
{
    return config != NULL && config->baud_rate != 0 &&
        (config->data_bits == PAL_UART_DATA_7BITS || config->data_bits == PAL_UART_DATA_8BITS) &&
        (config->parity == PAL_UART_PARITY_NONE || config->parity == PAL_UART_PARITY_EVEN ||
            config->parity == PAL_UART_PARITY_ODD) &&
        (config->stop_bits == PAL_UART_STOP_1 || config->stop_bits == PAL_UART_STOP_2) &&
        config->directions != 0 &&
        ((unsigned)config->directions & ~(unsigned)PAL_UART_DIR_TXRX) == 0;
}

/**
 * @brief Looks up a bound UART device by its ID. Calls PAL_UART_Port_Find and checks if the device is bound.
 * @param id PAL ID for UART devices.
 * @param out Found device, or NULL if not found.
 * @return PAL status.
 */
static PalUartStatus PAL_UART_LookupBound(PalUartId id, PalUartDevice** out)
{
    *out = PAL_UART_Port_Find(id);
    if (*out == NULL) return PAL_UART_STATUS_INVALID_ID;
    if ((*out)->state.binding == PAL_UART_UNBOUND) return PAL_UART_STATUS_NOT_INITIALIZED;
    return PAL_UART_STATUS_OK;
}

static PalUartStatus PAL_UART_Bind(PalUartId id, PalUartBinding binding)
{
    PalUartDevice* device = PAL_UART_Port_Find(id);
    if (device == NULL) return PAL_UART_STATUS_INVALID_ID;
    if (device->state.binding != PAL_UART_UNBOUND) return PAL_UART_STATUS_ALREADY_INITIALIZED;
    PalUartConfig config;
    PalUartStatus status = binding == PAL_UART_OWNED
                               ? PAL_UART_Port_Init(device->port_context, &config)
                               : PAL_UART_Port_AttachInitialized(device->port_context, &config);
    if (status == PAL_UART_STATUS_OK)
    {
        device->state = (PalUartState){.binding = binding, .ready = true, .config = config};
        device->tx = (PalUartAsyncTransfer){0};
        device->rx = (PalUartAsyncTransfer){0};
    }
    return status;
}

PalUartStatus PAL_UART_Init(PalUartId id)
{
    return PAL_UART_Bind(id, PAL_UART_OWNED);
}

PalUartStatus PAL_UART_AttachInitialized(PalUartId id)
{
    return PAL_UART_Bind(id, PAL_UART_ATTACHED);
}

PalUartStatus PAL_UART_Reconfigure(PalUartId id, const PalUartConfig* config)
{
    if (!PAL_UART_ValidConfig(config)) return PAL_UART_STATUS_INVALID_ARGUMENT;
    PalUartDevice* device;
    PalUartStatus status = PAL_UART_LookupBound(id, &device);
    if (status != PAL_UART_STATUS_OK) return status;
    if (device->tx.state != PAL_UART_ASYNC_IDLE || device->rx.state != PAL_UART_ASYNC_IDLE)
        return PAL_UART_STATUS_BUSY;
    status = PAL_UART_Port_CheckIdle(device->port_context, PAL_UART_DIR_TXRX);
    if (status != PAL_UART_STATUS_OK) return status;
    bool modified = false;
    status = PAL_UART_Port_Reconfigure(device->port_context, config, &modified);
    if (status == PAL_UART_STATUS_OK)
    {
        device->state.config = *config;
        device->state.ready = true;
    }
    else if (modified)
    {
        device->state.ready = false;
    }
    return status;
}

static PalUartStatus PAL_UART_Release(PalUartId id, PalUartBinding binding)
{
    PalUartDevice* device;
    PalUartStatus status = PAL_UART_LookupBound(id, &device);
    if (status != PAL_UART_STATUS_OK) return status;
    if (device->state.binding != binding) return PAL_UART_STATUS_INVALID_STATE;
    if (device->tx.state != PAL_UART_ASYNC_IDLE || device->rx.state != PAL_UART_ASYNC_IDLE)
        return PAL_UART_STATUS_BUSY;
    status = PAL_UART_Port_CheckIdle(device->port_context, PAL_UART_DIR_TXRX);
    if (status != PAL_UART_STATUS_OK) return status;
    if (binding == PAL_UART_OWNED)
    {
        status = PAL_UART_Port_DeInit(device->port_context);
        if (status != PAL_UART_STATUS_OK)
        {
            device->state.ready = false;
            return status;
        }
    }
    device->state = (PalUartState){0};
    return PAL_UART_STATUS_OK;
}

PalUartStatus PAL_UART_DeInit(PalUartId id)
{
    return PAL_UART_Release(id, PAL_UART_OWNED);
}

PalUartStatus PAL_UART_Detach(PalUartId id)
{
    return PAL_UART_Release(id, PAL_UART_ATTACHED);
}

/**
 * @brief Prepares a UART device for a transfer.
 * @param[in] id PAL ID for UART devices.
 * @param[in] direction Transfer direction to use.
 * @param[out] out PAL device pointer to be filled with the found device, or NULL if not found.
 * @return PAL status.
 * @note This function checks if the device is bound, ready, and supports the requested transfer direction.
 * It also checks if there are any active transfers in the requested direction.
 */
static PalUartStatus PAL_UART_PrepareTransfer(
    PalUartId id, PalUartDirection direction,
    PalUartDevice** out
)
{
    PalUartStatus status = PAL_UART_LookupBound(id, out);
    if (status != PAL_UART_STATUS_OK) return status;
    if (!(*out)->state.ready) return PAL_UART_STATUS_INVALID_STATE;
    if (((*out)->state.config.directions & direction) == 0) return PAL_UART_STATUS_UNSUPPORTED;
    if (direction == PAL_UART_DIR_TX && (*out)->tx.state != PAL_UART_ASYNC_IDLE) return PAL_UART_STATUS_BUSY;
    if (direction == PAL_UART_DIR_RX && (*out)->rx.state != PAL_UART_ASYNC_IDLE) return PAL_UART_STATUS_BUSY;
    return PAL_UART_Port_CheckIdle((*out)->port_context, direction);
}

PalUartStatus PAL_UART_Write(
    PalUartId id, const uint8_t* data, size_t length,
    uint32_t timeout_ms, size_t* transferred
)
{
    if (transferred != NULL) *transferred = 0;
    if (data == NULL || length == 0 || transferred == NULL) return PAL_UART_STATUS_INVALID_ARGUMENT;
    PalUartDevice* device;
    PalUartStatus status = PAL_UART_PrepareTransfer(id, PAL_UART_DIR_TX, &device);
    if (status != PAL_UART_STATUS_OK) return status;
    return PAL_UART_Port_Write(device->port_context, data, length, timeout_ms, transferred);
}

PalUartStatus PAL_UART_Read(
    PalUartId id, bool to_idle, uint8_t* data,
    size_t length, uint32_t timeout_ms, size_t* transferred
)
{
    if (transferred != NULL) *transferred = 0;
    if (data == NULL || length == 0 || transferred == NULL) return PAL_UART_STATUS_INVALID_ARGUMENT;
    PalUartDevice* device;
    PalUartStatus status = PAL_UART_PrepareTransfer(id, PAL_UART_DIR_RX, &device);
    if (status != PAL_UART_STATUS_OK) return status;
    return PAL_UART_Port_Read(device->port_context, to_idle, data, length, timeout_ms, transferred);
}

/**
 * @brief Checks the validity of the asynchronous transfer parameters.
 * @param mode Async transfer mode, either PAL_UART_ASYNC_IRQ or PAL_UART_ASYNC_DMA.
 * @param data The data to be transferred.
 * @param length The length of the data to be transferred.
 * @param completion The completion callback function.
 * @return PAL_UART_STATUS_OK if the parameters are valid, otherwise an error code.
 */
static PalUartStatus PAL_UART_CheckAsyncParam(
    PalUartAsyncMode mode, const void* data,
    size_t length, PalUartCompletion completion
)
{
    if (data == NULL || length == 0 ||
        (mode != PAL_UART_ASYNC_IRQ && mode != PAL_UART_ASYNC_DMA))
        return PAL_UART_STATUS_INVALID_ARGUMENT;
    return PAL_UART_STATUS_OK;
}

PalUartStatus PAL_UART_StartTx(
    PalUartId id, PalUartAsyncMode mode,
    const uint8_t* data, size_t length,
    PalUartCompletion completion, void* context
)
{
    PalUartStatus status = PAL_UART_CheckAsyncParam(mode, data, length, completion);
    if (status != PAL_UART_STATUS_OK) return status;
    PalUartDevice* device;
    status = PAL_UART_PrepareTransfer(id, PAL_UART_DIR_TX, &device);
    if (status != PAL_UART_STATUS_OK) return status;
    device->tx.state = PAL_UART_ASYNC_ACTIVE;
    device->tx.mode = mode;
    device->tx.callback = completion;
    device->tx.context = context;
    device->tx.length = length;
    status = PAL_UART_Port_StartTx(device->port_context, mode, data, length);
    if (status != PAL_UART_STATUS_OK)
    {
        device->tx = (PalUartAsyncTransfer){0};
    }
    return status;
}

PalUartStatus PAL_UART_StartRx(
    PalUartId id, PalUartAsyncMode mode,
    uint8_t* data, size_t length,
    bool to_idle, PalUartCompletion completion, void* context
)
{
    PalUartStatus status = PAL_UART_CheckAsyncParam(mode, data, length, completion);
    if (status != PAL_UART_STATUS_OK) return status;
    PalUartDevice* device;
    status = PAL_UART_PrepareTransfer(id, PAL_UART_DIR_RX, &device);
    if (status != PAL_UART_STATUS_OK) return status;
    device->rx.state = PAL_UART_ASYNC_ACTIVE;
    device->rx.mode = mode;
    device->rx.callback = completion;
    device->rx.context = context;
    device->rx.length = length;
    status = PAL_UART_Port_StartRx(device->port_context, mode, data, length, to_idle);
    if (status != PAL_UART_STATUS_OK)
    {
        device->rx = (PalUartAsyncTransfer){0};
    }
    return status;
}

PalUartStatus PAL_UART_Cancel(PalUartId id, PalUartDirection direction)
{
    if (direction != PAL_UART_DIR_TX && direction != PAL_UART_DIR_RX)
        return PAL_UART_STATUS_INVALID_ARGUMENT;
    PalUartDevice* device;
    PalUartStatus status = PAL_UART_LookupBound(id, &device);
    if (status != PAL_UART_STATUS_OK) return status;
    PalUartAsyncTransfer* transfer = direction == PAL_UART_DIR_TX ? &device->tx : &device->rx;
    if (transfer->state == PAL_UART_ASYNC_IDLE) return PAL_UART_STATUS_NOT_ACTIVE;
    if (transfer->state == PAL_UART_ASYNC_CANCEL_PENDING) return PAL_UART_STATUS_BUSY;
    transfer->state = PAL_UART_ASYNC_CANCEL_PENDING;
    status = PAL_UART_Port_Cancel(device->port_context, direction);
    /* Rejection must not invoke completion or change the hardware request. */
    if (status != PAL_UART_STATUS_OK) transfer->state = PAL_UART_ASYNC_ACTIVE;
    return status;
}

void PAL_UART_AsyncFinish(PalUartId id, PalUartDirection direction,
                          PalUartTransferResult result, size_t transferred,
                          PalUartErrors errors)
{
    if (direction != PAL_UART_DIR_TX && direction != PAL_UART_DIR_RX) return;
    PalUartDevice* device;
    if (PAL_UART_LookupBound(id, &device) != PAL_UART_STATUS_OK) return;
    PalUartAsyncTransfer* transfer = direction == PAL_UART_DIR_TX ? &device->tx : &device->rx;
    if (transfer->state != PAL_UART_ASYNC_ACTIVE && transfer->state != PAL_UART_ASYNC_CANCEL_PENDING) return;
    PalUartAsyncTransfer completed = *transfer;
    if (transferred > completed.length) transferred = completed.length;
    *transfer = (PalUartAsyncTransfer){0};
    if (completed.callback != NULL)
        completed.callback(id, direction, result, transferred, errors, completed.context);
}
