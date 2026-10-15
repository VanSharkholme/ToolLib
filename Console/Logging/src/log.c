//
// Created by VanSharkholme on 25-6-1.
//

#include "log.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#include "log_formatter.h"
#include "log_buffer.h"

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
    if (!log_context.initialized)
        return;
    if (level == LOG_LEVEL_NONE || level > log_context.runtime_level)
        return;
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

    Block *block = NULL;
    BufferStatus status = log_buffer_allocate(&block);
    if (status != BUFFER_OK)
        return;

    size_t written_chars = log_formatter_process((char *)block->data, block->size, level, tag, file, line, format, args);
    log_buffer_write(block, written_chars);
}

void log_backend_tx_complete(void *context)
{
    return;
}
