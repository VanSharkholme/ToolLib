//
// Created by VanSharkholme on 2026/8/10.
//
#include "log_port.h"
#include "lock.h"

#include "cmsis_os2.h"

#if LOG_USE_EVENT_WAKEUP
static osSemaphoreId_t log_port_bin_semaphore = NULL;
static const osSemaphoreAttr_t log_port_bin_semaphore_attr = {
    .name = "log_port_bin_semaphore"
};
#endif
#if !LOG_USE_CUSTOM_TASK_ENTRY
void log_task_entry(void* argument);
static const osThreadAttr_t log_task_attr = {
    .name = "log_task",
    .stack_size = LOG_DEFAULT_TASK_STACK_SIZE,
    .priority = osPriorityIdle
};
static osThreadId_t log_task_handle = NULL;
#endif

Log_Port_Status log_port_init(void)
{
#if LOG_USE_EVENT_WAKEUP
    log_port_bin_semaphore = osSemaphoreNew(1, 0, &log_port_bin_semaphore_attr);
    if (log_port_bin_semaphore == NULL)
    {
        return LOG_PORT_ERROR;
    }
#endif
#if !LOG_USE_CUSTOM_TASK_ENTRY
    log_task_handle = osThreadNew(log_task_entry, NULL, &log_task_attr);
    if (log_task_handle == NULL)
    {
        return LOG_PORT_ERROR;
    }
#endif
    return LOG_PORT_OK;
}

void log_port_deinit(void)
{
#if LOG_USE_EVENT_WAKEUP
    if (log_port_bin_semaphore != NULL) {
        osSemaphoreDelete(log_port_bin_semaphore);
        log_port_bin_semaphore = NULL;
    }
#endif
#if !LOG_USE_CUSTOM_TASK_ENTRY
    if (log_task_handle != NULL) {
        /** Can't delete here since there are cleanups in log_deinit after port deinit. */
        osThreadSuspend(log_task_handle);
        log_task_handle = NULL;
    }
#endif
}

#if LOG_USE_EVENT_WAKEUP
void log_port_notify(void)
{
    if (log_port_bin_semaphore != NULL) {
        osSemaphoreRelease(log_port_bin_semaphore);
    }
}

bool log_port_wait(uint32_t timeout_ms)
{
    if (log_port_bin_semaphore != NULL) {
        osStatus_t status = osSemaphoreAcquire(log_port_bin_semaphore, timeout_ms);
        return (status == osOK);
    }
    return false;
}
#endif
