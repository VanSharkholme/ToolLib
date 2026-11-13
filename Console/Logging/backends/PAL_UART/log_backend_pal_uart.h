//
// Created by VanSharkholme on 2026/8/10.
//

#ifndef TOOLLIB_LOG_BACKEND_UART_H
#define TOOLLIB_LOG_BACKEND_UART_H
#include "pal_uart.h"
#include "log_backend.h"

typedef struct
{
    PalUartId id;
    void *tx_cplt_cb_context;
} Log_Backend_PalUart_Context;

void log_backend_pal_uart_configure(Log_Backend *backend, Log_Backend_PalUart_Context *context);
#endif //TOOLLIB_LOG_BACKEND_UART_H
