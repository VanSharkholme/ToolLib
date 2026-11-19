//
// Created by VanSharkholme on 2026/11/10.
//
#include "pal_spi.h"
#include "pal_spi_port.h"

static bool PAL_SPI_ValidConfig(const PalSpiConfig* config)
{
    return config != NULL && config->role <= PAL_SPI_ROLE_SLAVE &&
        (config->role == PAL_SPI_ROLE_SLAVE || config->max_clk_freq_hz != 0) &&
        config->data_size >= PAL_SPI_DATA_SIZE_4BIT && config->data_size <= PAL_SPI_DATA_SIZE_32BIT &&
        config->data_lines <= PAL_SPI_DATA_LINES_SHARED &&
        config->directions >= PAL_SPI_DIR_TX && config->directions <= PAL_SPI_DIR_TXRX &&
        config->clk_polarity <= PAL_SPI_CLOCK_POLARITY_HIGH && config->clk_phase <= PAL_SPI_CLOCK_PHASE_2EDGE &&
        config->nss_control <= PAL_SPI_NSS_HARDWARE && config->nss_polarity <= PAL_SPI_NSS_POLARITY_HIGH &&
        config->bit_order <= PAL_SPI_BIT_ORDER_LSB_FIRST;
}

static PalSpiStatus PAL_SPI_LookupBound(PalSpiId id, PalSpiDevice** device)
{
    *device = PAL_SPI_Port_Find(id);
    if (*device == NULL) return PAL_SPI_STATUS_INVALID_ID;
    if ((*device)->state.binding == PAL_SPI_UNBOUND) return PAL_SPI_STATUS_NOT_INITIALIZED;
    return PAL_SPI_STATUS_OK;
}

static PalSpiStatus PAL_SPI_Bind(PalSpiId id, PalSpiBinding binding)
{
    PalSpiDevice* device = PAL_SPI_Port_Find(id);
    if (device == NULL) return PAL_SPI_STATUS_INVALID_ID;
    if (device->state.binding != PAL_SPI_UNBOUND) return PAL_SPI_STATUS_ALREADY_INITIALIZED;
    PalSpiConfig config;
    PalSpiStatus status = binding == PAL_SPI_OWNED
                               ? PAL_SPI_Port_Init(device->port_context, &config)
                               : PAL_SPI_Port_AttachInitialized(device->port_context, &config);
    if (status == PAL_SPI_STATUS_OK)
    {
        device->state = (PalSpiState){.binding = binding, .ready = true, .config = config};
        device->transfer = (PalSpiAsyncTransfer){0};
    }
    return status;
}

PalSpiStatus PAL_SPI_Init(PalSpiId id)
{
    return PAL_SPI_Bind(id, PAL_SPI_OWNED);
}

PalSpiStatus PAL_SPI_AttachInitialized(PalSpiId id)
{
    return PAL_SPI_Bind(id, PAL_SPI_ATTACHED);
}

PalSpiStatus PAL_SPI_Reconfigure(PalSpiId id, const PalSpiConfig* config)
{
    if (!PAL_SPI_ValidConfig(config)) return PAL_SPI_STATUS_INVALID_ARGUMENT;
    PalSpiDevice* device;
    PalSpiStatus status = PAL_SPI_LookupBound(id, &device);
    if (status != PAL_SPI_STATUS_OK) return status;
    if (device->transfer.state != PAL_SPI_ASYNC_IDLE) return PAL_SPI_STATUS_BUSY;
    status = PAL_SPI_Port_CheckIdle(device->port_context);
    if (status != PAL_SPI_STATUS_OK) return status;
    bool modified = false;
    PalSpiConfig applied = *config;
    status = PAL_SPI_Port_Reconfigure(device->port_context, &applied, &modified);
    if (status == PAL_SPI_STATUS_OK)
    {
        device->state.config = applied;
        device->state.ready = true;
    }
    else if (modified)
    {
        device->state.ready = false;
    }
    return status;
}

static PalSpiStatus PAL_SPI_Release(PalSpiId id, PalSpiBinding binding)
{
    PalSpiDevice* device;
    PalSpiStatus status = PAL_SPI_LookupBound(id, &device);
    if (status != PAL_SPI_STATUS_OK) return status;
    if (device->state.binding != binding) return PAL_SPI_STATUS_INVALID_STATE;
    if (device->transfer.state != PAL_SPI_ASYNC_IDLE) return PAL_SPI_STATUS_BUSY;
    if (binding == PAL_SPI_OWNED)
    {
        status = PAL_SPI_Port_DeInit(device->port_context);
        if (status != PAL_SPI_STATUS_OK)
        {
            device->state.ready = false;
            return status;
        }
    }
    device->state = (PalSpiState){0};
    return PAL_SPI_STATUS_OK;
}

PalSpiStatus PAL_SPI_DeInit(PalSpiId id)
{
    return PAL_SPI_Release(id, PAL_SPI_OWNED);
}

PalSpiStatus PAL_SPI_Detach(PalSpiId id)
{
    return PAL_SPI_Release(id, PAL_SPI_ATTACHED);
}

static PalSpiStatus PAL_SPI_Find_Check_Device(PalSpiId id, PalSpiDirection direction, PalSpiDevice** device)
{
    PalSpiStatus status = PAL_SPI_LookupBound(id, device);
    if (status != PAL_SPI_STATUS_OK) return status;
    if (!(*device)->state.ready) return PAL_SPI_STATUS_NOT_INITIALIZED;
    const PalSpiConfig* config = &(*device)->state.config;
    if ((config->directions & direction) != direction ||
        (direction == PAL_SPI_DIR_TXRX && config->data_lines == PAL_SPI_DATA_LINES_SHARED)) return PAL_SPI_STATUS_UNSUPPORTED;
    if ((*device)->transfer.state != PAL_SPI_ASYNC_IDLE) return PAL_SPI_STATUS_BUSY;
    status = PAL_SPI_Port_CheckIdle((*device)->port_context);
    if (status != PAL_SPI_STATUS_OK) return status;
    return PAL_SPI_STATUS_OK;
}

PalSpiStatus PAL_SPI_Transmit(PalSpiId id, const uint8_t* tx_data, size_t length, uint32_t timeout_ms, size_t* transferred)
{
    if (transferred != NULL) *transferred = 0;
    if (tx_data == NULL || length == 0 || transferred == NULL) return PAL_SPI_STATUS_INVALID_ARGUMENT;
    PalSpiDevice* device;
    PalSpiStatus status = PAL_SPI_Find_Check_Device(id, PAL_SPI_DIR_TX, &device);
    if (status != PAL_SPI_STATUS_OK) return status;
    return PAL_SPI_Port_Transmit(device->port_context, tx_data, length, timeout_ms, transferred);
}

PalSpiStatus PAL_SPI_Receive(PalSpiId id, uint8_t* rx_data, size_t length, uint32_t timeout_ms, size_t* transferred)
{
    if (transferred != NULL) *transferred = 0;
    if (rx_data == NULL || length == 0 || transferred == NULL) return PAL_SPI_STATUS_INVALID_ARGUMENT;
    PalSpiDevice* device;
    PalSpiStatus status = PAL_SPI_Find_Check_Device(id, PAL_SPI_DIR_RX, &device);
    if (status != PAL_SPI_STATUS_OK) return status;
    return  PAL_SPI_Port_Receive(device->port_context, rx_data, length, timeout_ms, transferred);
}

PalSpiStatus PAL_SPI_TransmitReceive(
    PalSpiId id, const uint8_t* tx_data, uint8_t* rx_data, size_t length, uint32_t timeout_ms, size_t* transferred
)
{
    if (transferred != NULL) *transferred = 0;
    if (tx_data == NULL || rx_data == NULL || length == 0 || transferred == NULL) return PAL_SPI_STATUS_INVALID_ARGUMENT;
    PalSpiDevice* device;
    PalSpiStatus status = PAL_SPI_Find_Check_Device(id, PAL_SPI_DIR_TXRX, &device);
    if (status != PAL_SPI_STATUS_OK) return status;
    return PAL_SPI_Port_TransmitReceive(device->port_context, tx_data, rx_data, length, timeout_ms, transferred);
}

static PalSpiStatus PAL_SPI_CheckAsyncParam(
    PalSpiAsyncMode mode, PalSpiDirection direction, const uint8_t* tx_data, const uint8_t* rx_data, size_t length
)
{
    if (mode != PAL_SPI_ASYNC_IRQ && mode != PAL_SPI_ASYNC_DMA) return PAL_SPI_STATUS_INVALID_ARGUMENT;
    if (length == 0) return PAL_SPI_STATUS_INVALID_ARGUMENT;
    if (((direction & PAL_SPI_DIR_TX) > 0) && tx_data == NULL) return PAL_SPI_STATUS_INVALID_ARGUMENT;
    if (((direction & PAL_SPI_DIR_RX) > 0) && rx_data == NULL) return PAL_SPI_STATUS_INVALID_ARGUMENT;
    return PAL_SPI_STATUS_OK;
}

PalSpiStatus PAL_SPI_StartTx(
    PalSpiId id, PalSpiAsyncMode mode, const uint8_t* tx_data, size_t length,
    PalSpiCompletion completion, void* context
)
{
    PalSpiStatus status = PAL_SPI_CheckAsyncParam(mode, PAL_SPI_DIR_TX, tx_data, NULL, length);
    if (status != PAL_SPI_STATUS_OK) return status;
    PalSpiDevice* device;
    status = PAL_SPI_Find_Check_Device(id, PAL_SPI_DIR_TX, &device);
    if (status != PAL_SPI_STATUS_OK) return status;
    device->transfer.state = PAL_SPI_ASYNC_ACTIVE;
    device->transfer.mode = mode;
    device->transfer.callback = completion;
    device->transfer.context = context;
    device->transfer.length = length;
    status = PAL_SPI_Port_StartTx(device->port_context, mode, tx_data, length);
    if (status != PAL_SPI_STATUS_OK)
    {
        device->transfer = (PalSpiAsyncTransfer){0};
    }
    return status;
}

PalSpiStatus PAL_SPI_StartRx(
    PalSpiId id, PalSpiAsyncMode mode, uint8_t* rx_data, size_t length,
    PalSpiCompletion completion, void* context
)
{
    PalSpiStatus status = PAL_SPI_CheckAsyncParam(mode, PAL_SPI_DIR_RX, NULL, rx_data, length);
    if (status != PAL_SPI_STATUS_OK) return status;
    PalSpiDevice* device;
    status = PAL_SPI_Find_Check_Device(id, PAL_SPI_DIR_RX, &device);
    if (status != PAL_SPI_STATUS_OK) return status;
    device->transfer.state = PAL_SPI_ASYNC_ACTIVE;
    device->transfer.mode = mode;
    device->transfer.callback = completion;
    device->transfer.context = context;
    device->transfer.length = length;
    status = PAL_SPI_Port_StartRx(device->port_context, mode, rx_data, length);
    if (status != PAL_SPI_STATUS_OK)
    {
        device->transfer = (PalSpiAsyncTransfer){0};
    }
    return status;
}

PalSpiStatus PAL_SPI_StartTxRx(
    PalSpiId id, PalSpiAsyncMode mode, const uint8_t* tx_data, uint8_t* rx_data, size_t length,
    PalSpiCompletion completion, void* context
)
{
    PalSpiStatus status = PAL_SPI_CheckAsyncParam(mode, PAL_SPI_DIR_TXRX, tx_data, rx_data, length);
    if (status != PAL_SPI_STATUS_OK) return status;
    PalSpiDevice* device;
    status = PAL_SPI_Find_Check_Device(id, PAL_SPI_DIR_TXRX, &device);
    if (status != PAL_SPI_STATUS_OK) return status;
    device->transfer.state = PAL_SPI_ASYNC_ACTIVE;
    device->transfer.mode = mode;
    device->transfer.callback = completion;
    device->transfer.context = context;
    device->transfer.length = length;
    status = PAL_SPI_Port_StartTxRx(device->port_context, mode, tx_data, rx_data, length);
    if (status != PAL_SPI_STATUS_OK)
        device->transfer = (PalSpiAsyncTransfer){0};
    return status;
}

PalSpiStatus PAL_SPI_Cancel(PalSpiId id)
{
    PalSpiDevice* device;
    PalSpiStatus status = PAL_SPI_LookupBound(id, &device);
    if (status != PAL_SPI_STATUS_OK) return status;
    if (device->transfer.state == PAL_SPI_ASYNC_IDLE) return PAL_SPI_STATUS_NOT_ACTIVE;
    if (device->transfer.state == PAL_SPI_ASYNC_CANCEL_PENDING) return PAL_SPI_STATUS_BUSY;
    /* The port claims the active request atomically with respect to IRQ completion.
     * A callback can already have submitted the next request when Cancel returns. */
    return PAL_SPI_Port_Cancel(device->port_context);
}

void PAL_SPI_AsyncFinish(
    PalSpiId id, PalSpiTransferResult result, size_t transferred,
    PalSpiErrors errors
)
{
    PalSpiDevice* device;
    if (PAL_SPI_LookupBound(id, &device) != PAL_SPI_STATUS_OK) return;
    PalSpiAsyncTransfer *transfer = &device->transfer;
    if (transfer->state != PAL_SPI_ASYNC_ACTIVE && transfer->state != PAL_SPI_ASYNC_CANCEL_PENDING) return;
    PalSpiAsyncTransfer completed = device->transfer;
    if (transferred > completed.length) transferred = device->transfer.length;
    *transfer = (PalSpiAsyncTransfer){0};
    if (completed.callback != NULL)
        completed.callback(id, result, transferred, errors, completed.context);
}