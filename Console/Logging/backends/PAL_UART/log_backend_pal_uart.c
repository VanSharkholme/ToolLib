//
// Created by VanSharkholme on 2026/8/10.
//

#include "log_backend_pal_uart.h"

static Log_Backend_Status log_backend_pal_uart_init(void* cb_context, void* backend_context);
static Log_Backend_Status log_backend_pal_uart_deinit(void* backend_context);
static Log_Backend_Status log_backend_pal_uart_transmit(const uint8_t* data, size_t length, void* backend_context);
static void Log_Backend_PalUart_Tx_Complete(
    PalUartId id, PalUartDirection direction,
    PalUartTransferResult result, size_t transferred,
    PalUartErrors errors, void* context
);

static const Log_Backend_Ops pal_uart_backend_ops_dma = {
    .init = log_backend_pal_uart_init,
    .deinit = log_backend_pal_uart_deinit,
    .transmit = log_backend_pal_uart_transmit,
    .is_busy = NULL,
    .flush = NULL
};

void log_backend_pal_uart_configure(Log_Backend* backend, Log_Backend_PalUart_Context* context)
{
    backend->is_async = true;
    backend->ops = &pal_uart_backend_ops_dma;
    backend->context = (void*)context;
}

static Log_Backend_Status log_backend_pal_uart_init(void* cb_context, void* backend_context)
{
    Log_Backend_PalUart_Context* pal_uart_context = (Log_Backend_PalUart_Context*)backend_context;
    PalUartId uart_id = pal_uart_context->id;
    PalUartStatus status = PAL_UART_Init(uart_id);
    if (status != PAL_UART_STATUS_OK)
        return LOG_BACKEND_ERROR;
    pal_uart_context->tx_cplt_cb_context = cb_context;
    return LOG_BACKEND_OK;
}

static Log_Backend_Status log_backend_pal_uart_deinit(void* backend_context)
{
    Log_Backend_PalUart_Context* pal_uart_context = (Log_Backend_PalUart_Context*)backend_context;
    PalUartId uart_id = pal_uart_context->id;
    PalUartStatus status = PAL_UART_DeInit(uart_id);
    if (status != PAL_UART_STATUS_OK)
        return LOG_BACKEND_ERROR;
    return LOG_BACKEND_OK;
}

static Log_Backend_Status log_backend_pal_uart_transmit(const uint8_t* data, size_t length, void* backend_context)
{
    Log_Backend_PalUart_Context* pal_uart_context = (Log_Backend_PalUart_Context*)backend_context;
    PalUartId uart_id = pal_uart_context->id;
    PalUartStatus status = PAL_UART_StartTx(
        uart_id, PAL_UART_ASYNC_DMA, data, length, Log_Backend_PalUart_Tx_Complete, backend_context
    );
    if (status == PAL_UART_STATUS_OK)
        return LOG_BACKEND_OK;
    else if (status == PAL_UART_STATUS_BUSY)
        return LOG_BACKEND_BUSY;
    else
        return LOG_BACKEND_ERROR;
}

static void Log_Backend_PalUart_Tx_Complete(
    PalUartId id, PalUartDirection direction,
    PalUartTransferResult result, size_t transferred,
    PalUartErrors errors, void* context
)
{
    if (direction != PAL_UART_DIR_TX)
        return;
    Log_Backend_TransferResult backend_result;
    switch (result)
    {
    case PAL_UART_TRANSFER_OK:
        backend_result = LOG_BACKEND_TRANSFER_OK;
        break;
    case PAL_UART_TRANSFER_CANCELLED:
        backend_result = LOG_BACKEND_TRANSFER_CANCELLED;
        break;
    default:
        backend_result = LOG_BACKEND_TRANSFER_ERROR;
        break;
    }
    Log_Backend_PalUart_Context* pal_uart_context = (Log_Backend_PalUart_Context*)context;
    log_backend_tx_complete(pal_uart_context->tx_cplt_cb_context, backend_result);
}
