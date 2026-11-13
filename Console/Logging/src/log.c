//
// Created by VanSharkholme on 25-6-1.
//

#include "log.h"
#include <string.h>
#include <stdarg.h>

#include "log_formatter.h"
#include "log_buffer.h"
#include "log_port.h"
#include "log_backend_manager.h"

typedef struct
{
    bool initialized;
    bool buffer_initialized;
    bool port_initialized;
    bool backend_manager_active;
    Log_Level runtime_level;
    Block* current_log;
    Log_Stats stats;
} Log_Context;

static Log_Context log_context;

/* Shared resources remain alive until the manager releases every backend. */
static Log_Status log_cleanup(void)
{
    log_context.initialized = false;
    if (log_context.backend_manager_active)
    {
        Log_Status status = log_backend_manager_deinit();
        if (status != LOG_STATUS_OK)
            return status;
        log_context.backend_manager_active = false;
    }
    if (log_context.port_initialized)
    {
        log_port_deinit();
        log_context.port_initialized = false;
    }
    if (log_context.buffer_initialized)
    {
        if (log_buffer_deinit() != BUFFER_OK)
            return LOG_STATUS_BUFFER_ERROR;
        log_context.buffer_initialized = false;
    }
    memset(&log_context, 0, sizeof(log_context));
    return LOG_STATUS_OK;
}

Log_Status log_init(const Log_Config* config)
{
    if (log_context.initialized)
        return LOG_STATUS_ALREADY_INITIALIZED;
    if (log_context.buffer_initialized || log_context.port_initialized ||
        log_context.backend_manager_active)
        return LOG_STATUS_BUSY;
    if (config == NULL)
        return LOG_STATUS_INVALID_ARGUMENT;
    Log_Status status = log_backend_manager_validate(config->backends, config->backend_count);
    if (status != LOG_STATUS_OK)
        return status;

    if (log_buffer_init() != BUFFER_OK)
        return LOG_STATUS_BUFFER_ERROR;
    log_context.buffer_initialized = true;
    if (log_port_init() != LOG_PORT_OK)
    {
        (void)log_cleanup();
        return LOG_STATUS_PORT_ERROR;
    }
    log_context.port_initialized = true;

    /* Even a partially initialized manager must be cleaned up on failure. */
    log_context.backend_manager_active = true;
    status = log_backend_manager_init(config->backends, config->backend_count);
    if (status != LOG_STATUS_OK)
    {
        (void)log_cleanup();
        return status;
    }
    log_context.runtime_level = config->runtime_level;
    log_context.initialized = true;
    return LOG_STATUS_OK;
}

Log_Status log_deinit(void)
{
    if (!log_context.buffer_initialized && !log_context.port_initialized &&
        !log_context.backend_manager_active)
        return LOG_STATUS_NOT_INITIALIZED;
    if (log_context.backend_manager_active &&
        log_backend_manager_get_status().has_in_progress)
        return LOG_STATUS_BUSY;
    return log_cleanup();
}

void log_write(Log_Level level, const char* tag, const char* file, uint32_t line, const char* format, ...)
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


void log_vwrite(Log_Level level, const char* tag, const char* file, uint32_t line, const char* format, va_list args)
{
    if (!log_context.initialized)
        return;

    if (level == LOG_LEVEL_NONE || level > log_context.runtime_level)
        return;

    Block* block = NULL;
    BufferStatus status = log_buffer_allocate(&block);
    if (status != BUFFER_OK)
        return;

    size_t written_chars = log_formatter_process((char*)block->data, block->size, level, tag, file, line, format, args);
    if (written_chars == 0)
    {
        log_buffer_free(block);
        return;
    }

    status = log_buffer_write(block, written_chars);
    if (status != BUFFER_OK)
    {
        log_buffer_free(block);
        return;
    }
#if LOG_USE_EVENT_WAKEUP
    log_port_notify();
#endif
}

Log_ProcessResult log_process(void)
{
    if (!log_context.initialized)
        return LOG_PROCESS_WAIT;

    if (log_context.current_log != NULL)
    {
        Log_BackendSummary summary = log_backend_manager_process(
            log_context.current_log->data, log_context.current_log->size
        );
        if (summary.has_idle)
            return LOG_PROCESS_RETRY;
        if (!summary.all_finished)
            return LOG_PROCESS_WAIT;

        if (log_buffer_free(log_context.current_log) != BUFFER_OK)
            return LOG_PROCESS_ERROR;
        log_context.current_log = NULL;
    }

    BufferStatus status = log_buffer_read(&log_context.current_log);
    if (status == BUFFER_EMPTY)
        return LOG_PROCESS_WAIT;
    if (status != BUFFER_OK)
        return LOG_PROCESS_ERROR;

    log_backend_manager_begin_record();
    Log_BackendSummary summary = log_backend_manager_process(
        log_context.current_log->data, log_context.current_log->size
    );
    if (summary.has_idle)
        return LOG_PROCESS_RETRY;
    if (summary.all_finished)
        return LOG_PROCESS_NEXT;
    return LOG_PROCESS_WAIT;
}
