//
// Created by VanSharkholme on 2026/8/10.
//

#ifndef H533_TEST_LOG_BACKEND_H
#define H533_TEST_LOG_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum
{
    LOG_BACKEND_OK = 0,
    LOG_BACKEND_BUSY,
    LOG_BACKEND_ERROR
} Log_Backend_Status;

typedef void (*Log_Backend_TX_Complete_Callback)(void *context);

typedef struct
{
    Log_Backend_Status (*init)(
        Log_Backend_TX_Complete_Callback tx_cplt_cb,
        void *cb_context,
        void *backend_context
    );

    Log_Backend_Status (*deinit)(void);

    Log_Backend_Status (*transmit)(
        const uint8_t *data,
        size_t length,
        void *backend_context
    );

    bool (*is_busy)(void *backend_context);

    void (*flush)(void *backend_context);

    void *context;
} Log_Backend;

#endif //H533_TEST_LOG_BACKEND_H
