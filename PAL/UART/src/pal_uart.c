#include "pal_uart.h"
#include "pal_uart_port.h"

static bool PAL_UART_ValidConfig(const PalUartConfig *config)
{
    return config != NULL && config->baud_rate != 0 &&
        (config->data_bits == PAL_UART_DATA_7BITS || config->data_bits == PAL_UART_DATA_8BITS) &&
        (config->parity == PAL_UART_PARITY_NONE || config->parity == PAL_UART_PARITY_EVEN ||
         config->parity == PAL_UART_PARITY_ODD) &&
        (config->stop_bits == PAL_UART_STOP_1 || config->stop_bits == PAL_UART_STOP_2) &&
        config->directions != 0 &&
        ((unsigned)config->directions & ~(unsigned)PAL_UART_DIR_TXRX) == 0;
}

static PalUartStatus PAL_UART_LookupBound(PalUartId id, PalUartDevice **out)
{
    *out = PAL_UART_Port_Find(id);
    if (*out == NULL) return PAL_UART_STATUS_INVALID_ID;
    if ((*out)->state.binding == PAL_UART_UNBOUND) return PAL_UART_STATUS_NOT_INITIALIZED;
    return PAL_UART_STATUS_OK;
}

static PalUartStatus PAL_UART_Bind(PalUartId id, PalUartBinding binding)
{
    PalUartDevice *device = PAL_UART_Port_Find(id);
    if (device == NULL) return PAL_UART_STATUS_INVALID_ID;
    if (device->state.binding != PAL_UART_UNBOUND) return PAL_UART_STATUS_ALREADY_INITIALIZED;
    PalUartConfig config;
    PalUartStatus status = binding == PAL_UART_OWNED
        ? PAL_UART_Port_Init(device->port_context, &config)
        : PAL_UART_Port_AttachInitialized(device->port_context, &config);
    if (status == PAL_UART_STATUS_OK)
        device->state = (PalUartState){.binding = binding, .ready = true, .config = config};
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

PalUartStatus PAL_UART_Reconfigure(PalUartId id, const PalUartConfig *config)
{
    if (!PAL_UART_ValidConfig(config)) return PAL_UART_STATUS_INVALID_ARGUMENT;
    PalUartDevice *device;
    PalUartStatus status = PAL_UART_LookupBound(id, &device);
    if (status != PAL_UART_STATUS_OK) return status;
    status = PAL_UART_Port_CheckIdle(device->port_context, PAL_UART_DIR_TXRX);
    if (status != PAL_UART_STATUS_OK) return status;
    bool modified = false;
    status = PAL_UART_Port_Reconfigure(device->port_context, config, &modified);
    if (status == PAL_UART_STATUS_OK) {
        device->state.config = *config;
        device->state.ready = true;
    } else if (modified) {
        device->state.ready = false;
    }
    return status;
}

static PalUartStatus PAL_UART_Release(PalUartId id, PalUartBinding binding)
{
    PalUartDevice *device;
    PalUartStatus status = PAL_UART_LookupBound(id, &device);
    if (status != PAL_UART_STATUS_OK) return status;
    if (device->state.binding != binding) return PAL_UART_STATUS_INVALID_STATE;
    status = PAL_UART_Port_CheckIdle(device->port_context, PAL_UART_DIR_TXRX);
    if (status != PAL_UART_STATUS_OK) return status;
    if (binding == PAL_UART_OWNED) {
        status = PAL_UART_Port_DeInit(device->port_context);
        if (status != PAL_UART_STATUS_OK) {
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

static PalUartStatus PAL_UART_PrepareTransfer(PalUartId id, PalUartDirection direction,
                                             PalUartDevice **out)
{
    PalUartStatus status = PAL_UART_LookupBound(id, out);
    if (status != PAL_UART_STATUS_OK) return status;
    if (!(*out)->state.ready) return PAL_UART_STATUS_INVALID_STATE;
    if (((*out)->state.config.directions & direction) == 0) return PAL_UART_STATUS_UNSUPPORTED;
    return PAL_UART_Port_CheckIdle((*out)->port_context, direction);
}

PalUartStatus PAL_UART_Write(PalUartId id, const uint8_t *data, size_t length,
                           uint32_t timeout_ms, size_t *transferred)
{
    if (transferred != NULL) *transferred = 0;
    if (data == NULL || length == 0 || transferred == NULL) return PAL_UART_STATUS_INVALID_ARGUMENT;
    PalUartDevice *device;
    PalUartStatus status = PAL_UART_PrepareTransfer(id, PAL_UART_DIR_TX, &device);
    if (status != PAL_UART_STATUS_OK) return status;
    return PAL_UART_Port_Write(device->port_context, data, length, timeout_ms, transferred);
}

PalUartStatus PAL_UART_Read(PalUartId id, uint8_t *data, size_t length,
                          uint32_t timeout_ms, size_t *transferred)
{
    if (transferred != NULL) *transferred = 0;
    if (data == NULL || length == 0 || transferred == NULL) return PAL_UART_STATUS_INVALID_ARGUMENT;
    PalUartDevice *device;
    PalUartStatus status = PAL_UART_PrepareTransfer(id, PAL_UART_DIR_RX, &device);
    if (status != PAL_UART_STATUS_OK) return status;
    return PAL_UART_Port_Read(device->port_context, data, length, timeout_ms, transferred);
}

static PalUartStatus PAL_UART_CheckAsync(PalUartAsyncMode mode, const void *data,
                                        size_t length, PalUartCompletion completion)
{
    if (data == NULL || length == 0 || completion == NULL ||
        (mode != PAL_UART_ASYNC_IRQ && mode != PAL_UART_ASYNC_DMA))
        return PAL_UART_STATUS_INVALID_ARGUMENT;
    return PAL_UART_STATUS_OK;
}

PalUartStatus PAL_UART_StartTx(PalUartId id, PalUartAsyncMode mode,
                             const uint8_t *data, size_t length,
                             PalUartCompletion completion, void *context)
{
    PalUartStatus status = PAL_UART_CheckAsync(mode, data, length, completion);
    if (status != PAL_UART_STATUS_OK) return status;
    PalUartDevice *device;
    status = PAL_UART_PrepareTransfer(id, PAL_UART_DIR_TX, &device);
    if (status != PAL_UART_STATUS_OK) return status;
    return PAL_UART_Port_StartTx(id, mode, data, length, completion, context);
}

PalUartStatus PAL_UART_StartRx(PalUartId id, PalUartAsyncMode mode,
                             uint8_t *data, size_t length,
                             PalUartCompletion completion, void *context)
{
    PalUartStatus status = PAL_UART_CheckAsync(mode, data, length, completion);
    if (status != PAL_UART_STATUS_OK) return status;
    PalUartDevice *device;
    status = PAL_UART_PrepareTransfer(id, PAL_UART_DIR_RX, &device);
    if (status != PAL_UART_STATUS_OK) return status;
    return PAL_UART_Port_StartRx(id, mode, data, length, completion, context);
}

PalUartStatus PAL_UART_Cancel(PalUartId id, PalUartDirection direction)
{
    if (direction != PAL_UART_DIR_TX && direction != PAL_UART_DIR_RX)
        return PAL_UART_STATUS_INVALID_ARGUMENT;
    PalUartDevice *device;
    PalUartStatus status = PAL_UART_LookupBound(id, &device);
    if (status != PAL_UART_STATUS_OK) return status;
    return PAL_UART_Port_Cancel(id, direction);
}
