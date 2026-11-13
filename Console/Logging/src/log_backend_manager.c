#include "log_backend_manager.h"

#include <string.h>

#include "lock.h"
#include "log_config.h"
#include "log_port.h"

typedef enum
{
    LOG_BACKEND_TX_STATE_IDLE = 0,
    LOG_BACKEND_TX_STATE_IN_PROGRESS,
    LOG_BACKEND_TX_STATE_COMPLETE,
    LOG_BACKEND_TX_STATE_ERROR
} Log_Backend_Tx_State;

typedef struct
{
    Log_BackendPtr backend;
    bool initialized;
    Log_Backend_Tx_State tx_state;
} Log_BackendRuntime;

typedef struct
{
    Log_BackendRuntime backends[LOG_MAX_BACKENDS];
    size_t count;
    bool initialized;
    Lock state_lock;
} Log_BackendManager;

static Log_BackendManager manager;

static void log_backend_set_tx_state(Log_BackendRuntime *runtime, Log_Backend_Tx_State state)
{
    uintptr_t key = manager.state_lock.acquire(&manager.state_lock);
    runtime->tx_state = state;
    manager.state_lock.release(&manager.state_lock, key);
}

/**
 * @brief Try to start a log backend transmission if it is idle.
 * If the backend is idle, its state will be set to in-progress.
 * @param runtime The log backend to check and start.
 * @return True if the given backend is idle.
 */
static bool log_backend_try_start(Log_BackendRuntime *runtime)
{
    uintptr_t key = manager.state_lock.acquire(&manager.state_lock);
    bool idle = runtime->tx_state == LOG_BACKEND_TX_STATE_IDLE;
    if (idle)
        runtime->tx_state = LOG_BACKEND_TX_STATE_IN_PROGRESS;
    manager.state_lock.release(&manager.state_lock, key);
    return idle;
}

void log_backend_tx_complete(void *context, Log_Backend_TransferResult result)
{
    Log_BackendRuntime *runtime = (Log_BackendRuntime *)context;
    switch (result)
    {
    case LOG_BACKEND_TRANSFER_OK:
        log_backend_set_tx_state(runtime, LOG_BACKEND_TX_STATE_COMPLETE);
        break;
    case LOG_BACKEND_TRANSFER_CANCELLED:
        log_backend_set_tx_state(runtime, LOG_BACKEND_TX_STATE_IDLE);
        break;
    default:
        log_backend_set_tx_state(runtime, LOG_BACKEND_TX_STATE_ERROR);
        break;
    }
#if LOG_USE_EVENT_WAKEUP
    log_port_notify();
#endif
}

Log_Status log_backend_manager_validate(Log_BackendTable backends, size_t count)
{
    if (backends == NULL || count == 0 || count > LOG_MAX_BACKENDS)
        return LOG_STATUS_INVALID_ARGUMENT;
    for (size_t i = 0; i < count; ++i)
    {
        Log_BackendPtr backend = backends[i];
        if (backend == NULL || backend->ops == NULL ||
            backend->ops->init == NULL || backend->ops->deinit == NULL ||
            backend->ops->transmit == NULL)
            return LOG_STATUS_INVALID_ARGUMENT;
        for (size_t j = 0; j < i; ++j)
        {
            if (backend == backends[j])
                return LOG_STATUS_INVALID_ARGUMENT;
        }
    }
    return LOG_STATUS_OK;
}

Log_Status log_backend_manager_init(Log_BackendTable backends, size_t count)
{
    if (manager.initialized)
        return LOG_STATUS_ALREADY_INITIALIZED;
    if (manager.count != 0)
        return LOG_STATUS_BUSY;
    Log_Status status = log_backend_manager_validate(backends, count);
    if (status != LOG_STATUS_OK)
        return status;

    Lock_Init(&manager.state_lock, Lock_IRQ, NULL);
    for (size_t i = 0; i < count; ++i)
        manager.backends[i].backend = backends[i];
    manager.count = count;

    for (size_t i = 0; i < count; ++i)
    {
        Log_BackendRuntime *runtime = &manager.backends[i];
        Log_BackendPtr backend = runtime->backend;
        if (backend->ops->init(runtime, backend->context) != LOG_BACKEND_OK)
            return LOG_STATUS_BACKEND_ERROR;
        runtime->initialized = true;
    }
    manager.initialized = true;
    return LOG_STATUS_OK;
}

Log_Status log_backend_manager_deinit(void)
{
    if (manager.count == 0)
        return LOG_STATUS_OK;
    if (log_backend_manager_get_status().has_in_progress)
        return LOG_STATUS_BUSY;

    manager.initialized = false;
    Log_Status result = LOG_STATUS_OK;
    for (size_t i = manager.count; i > 0; --i)
    {
        Log_BackendRuntime *runtime = &manager.backends[i - 1];
        if (!runtime->initialized)
            continue;
        Log_BackendPtr backend = runtime->backend;
        if (backend->ops->deinit(backend->context) != LOG_BACKEND_OK)
            result = LOG_STATUS_BACKEND_ERROR;
        else
            runtime->initialized = false;
    }
    if (result == LOG_STATUS_OK)
        memset(&manager, 0, sizeof(manager));
    return result;
}

/**
 * @brief Reset all backends to idle state. This should be called before starting a new log record.
 */
void log_backend_manager_begin_record(void)
{
    if (!manager.initialized)
        return;
    uintptr_t key = manager.state_lock.acquire(&manager.state_lock);
    for (size_t i = 0; i < manager.count; ++i)
        manager.backends[i].tx_state = LOG_BACKEND_TX_STATE_IDLE;
    manager.state_lock.release(&manager.state_lock, key);
}

Log_BackendSummary log_backend_manager_get_status(void)
{
    Log_BackendSummary summary = {.all_finished = true};
    if (manager.count == 0)
        return summary;

    uintptr_t key = manager.state_lock.acquire(&manager.state_lock);
    for (size_t i = 0; i < manager.count; ++i)
    {
        if (!manager.backends[i].initialized)
            continue;
        Log_Backend_Tx_State state = manager.backends[i].tx_state;
        summary.has_idle |= state == LOG_BACKEND_TX_STATE_IDLE;
        summary.has_in_progress |= state == LOG_BACKEND_TX_STATE_IN_PROGRESS;
        summary.all_finished &= state == LOG_BACKEND_TX_STATE_COMPLETE ||
                                state == LOG_BACKEND_TX_STATE_ERROR;
    }
    manager.state_lock.release(&manager.state_lock, key);
    return summary;
}

Log_BackendSummary log_backend_manager_process(const uint8_t *data, size_t size)
{
    if (!manager.initialized)
        return log_backend_manager_get_status();

    for (size_t i = 0; i < manager.count; ++i)
    {
        Log_BackendRuntime *runtime = &manager.backends[i];
        /** Skip if the backend is not idle. */
        if (!log_backend_try_start(runtime))
            continue;
        Log_BackendPtr backend = runtime->backend;
        Log_Backend_Status status = backend->ops->transmit(data, size, backend->context);
        if (status == LOG_BACKEND_BUSY)
            log_backend_set_tx_state(runtime, LOG_BACKEND_TX_STATE_IDLE);
        else if (status != LOG_BACKEND_OK)
            log_backend_set_tx_state(runtime, LOG_BACKEND_TX_STATE_ERROR);
        else if (!backend->is_async)
            log_backend_set_tx_state(runtime, LOG_BACKEND_TX_STATE_COMPLETE);
        /* An asynchronous completion may precede transmit returning OK.
         * Leave its state untouched in that case. */
    }
    return log_backend_manager_get_status();
}
