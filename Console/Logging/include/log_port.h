//
// Created by VanSharkholme on 2026/8/10.
//

#ifndef TOOLLIB_LOG_PORT_H
#define TOOLLIB_LOG_PORT_H

#include "log_types.h"
#include "log_config.h"

typedef enum
{
    LOG_PORT_OK = 0,
    LOG_PORT_ERROR
} Log_Port_Status;

/* On failure, release any resources acquired by this initialization attempt. */
Log_Port_Status log_port_init(void);
void log_port_deinit(void);

#if defined(LOG_ENABLE_TIMESTAMP) && LOG_ENABLE_TIMESTAMP
uint32_t log_port_get_time_ms(void);
#endif

#if defined(LOG_USE_EVENT_WAKEUP) && LOG_USE_EVENT_WAKEUP
void log_port_notify(void);
bool log_port_wait(uint32_t timeout_ms);
#endif


#endif //TOOLLIB_LOG_PORT_H
