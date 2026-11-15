#include "pal_uart_host.h"
#include "pal_uart_port.h"

#include <string.h>

#define HOST_UART_COUNT 8u
#define HOST_BUFFER_SIZE 1024u

typedef struct {
    const uint8_t *tx_data;
    uint8_t *rx_data;
    size_t length;
} PendingTransfer;

typedef struct {
    PalUartId id;
    PalUartConfig default_config;
    PalUartConfig config;
    uint8_t wired_directions;
    int defined;
    PalUartDevice pal;
    int hardware_ready;
    int tx_dma_bound;
    int rx_dma_bound;
    PalUartStatus next_tx_start_result;
    PalUartStatus next_rx_start_result;
    PalUartStatus next_reconfigure_result;
    bool next_reconfigure_modified;
    PendingTransfer tx;
    PendingTransfer rx;
    uint8_t rx_queue[HOST_BUFFER_SIZE];
    size_t rx_size;
    uint8_t tx_queue[HOST_BUFFER_SIZE];
    size_t tx_size;
} HostUart;

static HostUart devices[HOST_UART_COUNT];

static HostUart *PAL_UART_Host_Find(PalUartId id)
{
    for (size_t i = 0; i < HOST_UART_COUNT; ++i)
        if (devices[i].defined && devices[i].id == id) return &devices[i];
    return NULL;
}

static PalUartStatus PAL_UART_Host_Lookup(PalUartId id, HostUart **out)
{
    *out = PAL_UART_Host_Find(id);
    return *out == NULL ? PAL_UART_STATUS_INVALID_ID : PAL_UART_STATUS_OK;
}

static void PAL_UART_Host_ConsumeRx(HostUart *device, uint8_t *out, size_t length)
{
    memcpy(out, device->rx_queue, length);
    device->rx_size -= length;
    memmove(device->rx_queue, device->rx_queue + length, device->rx_size);
}

static void PAL_UART_Host_FinishRxIfReady(HostUart *device)
{
    PendingTransfer pending = device->rx;
    if (pending.length == 0 || device->rx_size < pending.length) return;
    PAL_UART_Host_ConsumeRx(device, pending.rx_data, pending.length);
    memset(&device->rx, 0, sizeof device->rx);
    PAL_UART_AsyncFinish(device->id, PAL_UART_DIR_RX, PAL_UART_TRANSFER_OK,
                         pending.length, PAL_UART_ERROR_NONE);
}

void PAL_UART_Host_Reset(void)
{
    memset(devices, 0, sizeof devices);
}

PalUartStatus PAL_UART_Host_Define(PalUartId id,
                                   const PalUartConfig *default_config,
                                   uint8_t wired_directions)
{
    if (default_config == NULL || default_config->baud_rate == 0 ||
        (default_config->data_bits != PAL_UART_DATA_7BITS &&
         default_config->data_bits != PAL_UART_DATA_8BITS) ||
        (default_config->parity != PAL_UART_PARITY_NONE &&
         default_config->parity != PAL_UART_PARITY_EVEN &&
         default_config->parity != PAL_UART_PARITY_ODD) ||
        (default_config->stop_bits != PAL_UART_STOP_1 &&
         default_config->stop_bits != PAL_UART_STOP_2) ||
        wired_directions == 0 ||
        (wired_directions & ~(PAL_UART_DIR_TX | PAL_UART_DIR_RX)) != 0 ||
        default_config->directions == 0 ||
        (default_config->directions & wired_directions) != default_config->directions)
        return PAL_UART_STATUS_INVALID_ARGUMENT;
    if (PAL_UART_Host_Find(id) != NULL) return PAL_UART_STATUS_ALREADY_INITIALIZED;
    for (size_t i = 0; i < HOST_UART_COUNT; ++i)
        if (!devices[i].defined) {
            devices[i].id = id;
            devices[i].default_config = *default_config;
            devices[i].wired_directions = wired_directions;
            devices[i].defined = 1;
            devices[i].pal.port_context = &devices[i];
            return PAL_UART_STATUS_OK;
        }
    return PAL_UART_STATUS_BUSY;
}

PalUartStatus PAL_UART_Host_SetExternalReady(PalUartId id, int ready)
{
    HostUart *device;
    PalUartStatus status = PAL_UART_Host_Lookup(id, &device);
    if (status != PAL_UART_STATUS_OK) return status;
    if (device->pal.state.binding != PAL_UART_UNBOUND || device->tx.length || device->rx.length)
        return PAL_UART_STATUS_BUSY;
    if (ready && !device->hardware_ready)
        device->config = device->default_config;
    if (!ready)
        memset(&device->config, 0, sizeof device->config);
    device->hardware_ready = ready != 0;
    return PAL_UART_STATUS_OK;
}

PalUartStatus PAL_UART_Host_SetNextReconfigureResult(PalUartId id,
                                                        PalUartStatus status)
{
    HostUart *device;
    PalUartStatus found = PAL_UART_Host_Lookup(id, &device);
    if (found != PAL_UART_STATUS_OK) return found;
    if (status != PAL_UART_STATUS_BUSY && status != PAL_UART_STATUS_IO_ERROR)
        return PAL_UART_STATUS_INVALID_ARGUMENT;
    device->next_reconfigure_result = status;
    device->next_reconfigure_modified = false;
    return PAL_UART_STATUS_OK;
}

PalUartStatus PAL_UART_Host_SetDmaBound(PalUartId id, PalUartDirection direction,
                                          int bound)
{
    HostUart *device;
    PalUartStatus status = PAL_UART_Host_Lookup(id, &device);
    if (status != PAL_UART_STATUS_OK) return status;
    if (direction != PAL_UART_DIR_TX && direction != PAL_UART_DIR_RX)
        return PAL_UART_STATUS_INVALID_ARGUMENT;
    PendingTransfer *pending = direction == PAL_UART_DIR_TX ? &device->tx : &device->rx;
    if (pending->length != 0) return PAL_UART_STATUS_BUSY;
    int *binding = direction == PAL_UART_DIR_TX ? &device->tx_dma_bound : &device->rx_dma_bound;
    *binding = bound != 0;
    return PAL_UART_STATUS_OK;
}

PalUartStatus PAL_UART_Host_SetNextStartResult(PalUartId id,
                                                   PalUartDirection direction,
                                                   PalUartStatus status)
{
    HostUart *device;
    PalUartStatus found = PAL_UART_Host_Lookup(id, &device);
    if (found != PAL_UART_STATUS_OK) return found;
    if ((direction != PAL_UART_DIR_TX && direction != PAL_UART_DIR_RX) ||
        (status != PAL_UART_STATUS_BUSY && status != PAL_UART_STATUS_IO_ERROR))
        return PAL_UART_STATUS_INVALID_ARGUMENT;
    PalUartStatus *next = direction == PAL_UART_DIR_TX
        ? &device->next_tx_start_result : &device->next_rx_start_result;
    *next = status;
    return PAL_UART_STATUS_OK;
}


PalUartStatus PAL_UART_Host_SetNextModifiedReconfigureResult(PalUartId id,
                                                            PalUartStatus status)
{
    PalUartStatus result = PAL_UART_Host_SetNextReconfigureResult(id, status);
    if (result == PAL_UART_STATUS_OK)
        PAL_UART_Host_Find(id)->next_reconfigure_modified = true;
    return result;
}

PalUartDevice *PAL_UART_Port_Find(PalUartId id)
{
    HostUart *device = PAL_UART_Host_Find(id);
    return device == NULL ? NULL : &device->pal;
}

PalUartStatus PAL_UART_Port_CheckIdle(void *port_context, PalUartDirection directions)
{
    HostUart *device = port_context;
    if (((directions & PAL_UART_DIR_TX) && device->tx.length) ||
        ((directions & PAL_UART_DIR_RX) && device->rx.length))
        return PAL_UART_STATUS_BUSY;
    return PAL_UART_STATUS_OK;
}

PalUartStatus PAL_UART_Port_Init(void *port_context, PalUartConfig *applied)
{
    HostUart *device = port_context;
    if (device->hardware_ready) return PAL_UART_STATUS_BUSY;
    device->config = device->default_config;
    device->hardware_ready = 1;
    *applied = device->config;
    return PAL_UART_STATUS_OK;
}

PalUartStatus PAL_UART_Port_AttachInitialized(void *port_context, PalUartConfig *current)
{
    HostUart *device = port_context;
    if (!device->hardware_ready) return PAL_UART_STATUS_NOT_INITIALIZED;
    PalUartStatus status = PAL_UART_Port_CheckIdle(device, PAL_UART_DIR_TXRX);
    if (status != PAL_UART_STATUS_OK) return status;
    *current = device->config;
    return PAL_UART_STATUS_OK;
}

PalUartStatus PAL_UART_Port_Reconfigure(void *port_context,
                                      const PalUartConfig *config, bool *modified)
{
    HostUart *device = port_context;
    *modified = false;
    if ((config->directions & device->wired_directions) != config->directions)
        return PAL_UART_STATUS_UNSUPPORTED;
    PalUartStatus status = device->next_reconfigure_result;
    bool fail_after_write = device->next_reconfigure_modified;
    device->next_reconfigure_result = PAL_UART_STATUS_OK;
    device->next_reconfigure_modified = false;
    if (status != PAL_UART_STATUS_OK && !fail_after_write) return status;
    *modified = true;
    device->config = *config;
    return status;
}

PalUartStatus PAL_UART_Port_DeInit(void *port_context)
{
    HostUart *device = port_context;
    device->hardware_ready = 0;
    device->next_reconfigure_result = PAL_UART_STATUS_OK;
    device->next_reconfigure_modified = false;
    memset(&device->config, 0, sizeof device->config);
    return PAL_UART_STATUS_OK;
}



PalUartStatus PAL_UART_Port_Write(void *port_context, const uint8_t *data, size_t length,
                                 uint32_t timeout_ms, size_t *transferred)
{
    HostUart *device = port_context;
    (void)timeout_ms; /* The fake does not wait in real time. */
    size_t space = HOST_BUFFER_SIZE - device->tx_size;
    size_t count = length < space ? length : space;
    memcpy(device->tx_queue + device->tx_size, data, count);
    device->tx_size += count;
    *transferred = count;
    return count == length ? PAL_UART_STATUS_OK : PAL_UART_STATUS_TIMEOUT;
}

PalUartStatus PAL_UART_Port_Read(
    void *port_context, bool to_idle, uint8_t *data,
    size_t length, uint32_t timeout_ms, size_t *transferred
)
{
    /* The host fake has no idle-line event model. Reject before consuming data. */
    if (to_idle) return PAL_UART_STATUS_UNSUPPORTED;
    HostUart *device = port_context;
    (void)timeout_ms; /* The fake does not wait in real time. */
    size_t count = length < device->rx_size ? length : device->rx_size;
    if (count) PAL_UART_Host_ConsumeRx(device, data, count);
    *transferred = count;
    return count == length ? PAL_UART_STATUS_OK : PAL_UART_STATUS_TIMEOUT;
}

PalUartStatus PAL_UART_Port_StartTx(void *port_context, PalUartAsyncMode mode,
                                     const uint8_t *data, size_t length)
{
    HostUart *device = port_context;
    PalUartStatus status;
    if (mode == PAL_UART_ASYNC_DMA && !device->tx_dma_bound)
        return PAL_UART_STATUS_UNSUPPORTED;
    if (device->next_tx_start_result != PAL_UART_STATUS_OK) {
        status = device->next_tx_start_result;
        device->next_tx_start_result = PAL_UART_STATUS_OK;
        return status;
    }
    device->tx = (PendingTransfer){.tx_data = data, .length = length};
    return PAL_UART_STATUS_OK;
}

PalUartStatus PAL_UART_Port_StartRx(void *port_context, PalUartAsyncMode mode,
                                     uint8_t *data, size_t length, bool to_idle)
{
    /* Preserve queued data and next-start fault injection on unsupported requests. */
    if (to_idle) return PAL_UART_STATUS_UNSUPPORTED;
    HostUart *device = port_context;
    PalUartStatus status;
    if (mode == PAL_UART_ASYNC_DMA && !device->rx_dma_bound)
        return PAL_UART_STATUS_UNSUPPORTED;
    if (device->next_rx_start_result != PAL_UART_STATUS_OK) {
        status = device->next_rx_start_result;
        device->next_rx_start_result = PAL_UART_STATUS_OK;
        return status;
    }
    device->rx = (PendingTransfer){.rx_data = data, .length = length};
    PAL_UART_Host_FinishRxIfReady(device);
    return PAL_UART_STATUS_OK;
}

PalUartStatus PAL_UART_Port_Cancel(void *port_context, PalUartDirection direction)
{
    HostUart *device = port_context;
    PendingTransfer *slot = direction == PAL_UART_DIR_TX ? &device->tx : &device->rx;
    if (slot->length == 0) return PAL_UART_STATUS_NOT_ACTIVE;
    memset(slot, 0, sizeof *slot);
    PAL_UART_AsyncFinish(device->id, direction, PAL_UART_TRANSFER_CANCELLED, 0, PAL_UART_ERROR_NONE);
    return PAL_UART_STATUS_OK;
}

PalUartStatus PAL_UART_Host_FeedRx(PalUartId id, const uint8_t *data, size_t length)
{
    HostUart *device;
    PalUartStatus status = PAL_UART_Host_Lookup(id, &device);
    if (status != PAL_UART_STATUS_OK) return status;
    if (data == NULL || length == 0) return PAL_UART_STATUS_INVALID_ARGUMENT;
    if (length > HOST_BUFFER_SIZE - device->rx_size) return PAL_UART_STATUS_BUSY;
    memcpy(device->rx_queue + device->rx_size, data, length);
    device->rx_size += length;
    PAL_UART_Host_FinishRxIfReady(device);
    return PAL_UART_STATUS_OK;
}

PalUartStatus PAL_UART_Host_CompleteTx(PalUartId id, PalUartTransferResult result,
                                         size_t transferred, PalUartErrors errors)
{
    HostUart *device;
    PalUartStatus found = PAL_UART_Host_Lookup(id, &device);
    if (found != PAL_UART_STATUS_OK) return found;
    PendingTransfer pending = device->tx;
    if (pending.length == 0) return PAL_UART_STATUS_NOT_ACTIVE;
    if (transferred > pending.length ||
        (result != PAL_UART_TRANSFER_OK && result != PAL_UART_TRANSFER_ABORTED) ||
        (result == PAL_UART_TRANSFER_OK && transferred != pending.length))
        return PAL_UART_STATUS_INVALID_ARGUMENT;
    if (transferred > HOST_BUFFER_SIZE - device->tx_size) return PAL_UART_STATUS_BUSY;
    memcpy(device->tx_queue + device->tx_size, pending.tx_data, transferred);
    device->tx_size += transferred;
    memset(&device->tx, 0, sizeof device->tx);
    PAL_UART_AsyncFinish(id, PAL_UART_DIR_TX, result, transferred, errors);
    return PAL_UART_STATUS_OK;
}

size_t PAL_UART_Host_TakeTx(PalUartId id, uint8_t *out, size_t capacity)
{
    HostUart *device = PAL_UART_Host_Find(id);
    if (device == NULL || out == NULL || capacity == 0) return 0;
    size_t count = capacity < device->tx_size ? capacity : device->tx_size;
    memcpy(out, device->tx_queue, count);
    device->tx_size -= count;
    memmove(device->tx_queue, device->tx_queue + count, device->tx_size);
    return count;
}
