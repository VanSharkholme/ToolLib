//
// Created by VanSharkholme on 25-6-1.
//

#include "log.h"
#include <string.h>
#include <stdarg.h>

typedef struct
{
    bool initialized;
    bool transmitting;

    Log_Level runtime_level;
    const Log_Backend* backend;

    Log_Stats stats;
} Log_Context;

static Log_Context log_context;

Log_Status log_init(const Log_Config* config)
{
    if (config == NULL || config->backend == NULL)
    {
        return LOG_STATUS_INVALID_ARGUMENT;
    }

    memset(&log_context, 0, sizeof(log_context));

    BufferStatus buffer_status = log_buffer_init();
    if (buffer_status != BUFFER_OK)
        return LOG_STATUS_BACKEND_ERROR;
    // log_port_init();

    Log_Backend_Status status = config->backend->init(
        log_backend_tx_complete,
        NULL,
        config->backend->context
    );

    if (status != LOG_BACKEND_OK)
        return LOG_STATUS_BACKEND_ERROR;

    log_context.backend = config->backend;
    log_context.runtime_level = config->runtime_level;
    log_context.initialized = true;

    return LOG_STATUS_OK;
}

Log_Status log_deinit(void)
{
    Log_Backend_Status status = log_context.backend->deinit();

    if (status != LOG_BACKEND_OK)
        return LOG_STATUS_BACKEND_ERROR;

    log_context.backend = NULL;

    // log_port_deinit();
    log_buffer_deinit();

    memset(&log_context, 0, sizeof(log_context));

    return LOG_STATUS_OK;
}

void log_write(Log_Level level, const char *tag, const char *file, uint32_t line, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    log_vwrite(level, tag, file, line, format, args);
    va_end(args);
}


void log_vwrite(Log_Level level, const char *tag, const char *file, uint32_t line, const char *format, va_list args)
{
    if (!log_context.initialized)
        return;

    if (level > log_context.runtime_level)
        return;


}






















#if defined(LOG_USE_RTT) && LOG_USE_RTT
#include "SEGGER_RTT.h"
#endif

char format_buffer[256];
#if defined(LOG_USE_UART) && LOG_USE_UART
#if defined(LOG_USE_NPF) && LOG_USE_NPF
#include "nanoprintf.h"
#else
#include <stdio.h>
#endif
#include "ringbuffer.h"
#include "usart.h"
static Log_Message_t log_ring_buf_mem[LOG_RINGBUFFER_CAPACITY];
static RingBuffer log_ring;
#if defined(LOG_USE_RTOS) && LOG_USE_RTOS
#include "FreeRTOS.h"
#include "cmsis_os2.h"
typedef StaticSemaphore_t osStaticSemaphoreDef_t;

osSemaphoreId_t LogBinSemHandle;
osStaticSemaphoreDef_t LogBinSCB;
const osSemaphoreAttr_t LogBinSem_attributes = {
    .name = "LogBinSem",
    .cb_mem = &LogBinSCB,
    .cb_size = sizeof(LogBinSCB),
};

#endif
#endif

static void log_vprintf(const char *format, va_list args)
{
#if defined(LOG_USE_UART) && LOG_USE_UART
    Log_Message_t msg;
#if defined(LOG_USE_NPF) && LOG_USE_NPF
    msg.len = npf_vsnprintf(msg.msg, sizeof(msg.msg), format, args);
#else
    msg.len = vsnprintf(msg.msg, sizeof(msg.msg), format, args);
#endif
    if (msg.len >= sizeof(msg.msg))
        return;
    RingBuffer_Push(&log_ring, &msg);
#endif
#if defined(LOG_USE_RTT) && LOG_USE_RTT
    SEGGER_RTT_vprintf(0, format, args);
#endif
}

void log_log(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    log_vprintf(format, args);
    va_end(args);
}

#if defined(LOG_USE_UART) && LOG_USE_UART
static void log_transmit_lock()
{
#if defined(LOG_USE_RTOS) && LOG_USE_RTOS
    osSemaphoreAcquire(LogBinSemHandle, osWaitForever);
#endif
}

void log_transmit_unlock()
{
#if defined(LOG_USE_RTOS) && LOG_USE_RTOS
    osSemaphoreRelease(LogBinSemHandle);
#endif
}

static void log_task()
{
    Log_Message_t msg;
    while (!RingBuffer_IsEmpty(&log_ring))
    {
        log_transmit_lock();
        RingBuffer_Pop(&log_ring, &msg);
        HAL_UART_Transmit_IT(&huart2, (uint8_t*)msg.msg, msg.len);
    }
}

#if defined(LOG_USE_RTOS) && LOG_USE_RTOS
void LogTask(void *argument)
{
    for (;;)
    {
        log_task();
    }
}
#endif

#endif
