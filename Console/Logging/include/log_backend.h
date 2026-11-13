//
// Created by VanSharkholme on 2026/8/10.
//

#ifndef TOOLLIB_LOG_BACKEND_H
#define TOOLLIB_LOG_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum
{
    LOG_BACKEND_OK = 0,
    LOG_BACKEND_BUSY,
    LOG_BACKEND_ERROR
} Log_Backend_Status;

typedef enum
{
    LOG_BACKEND_TRANSFER_OK = 0,
    LOG_BACKEND_TRANSFER_ERROR,
    LOG_BACKEND_TRANSFER_CANCELLED,
} Log_Backend_TransferResult;

typedef struct
{
    /* Save the completion context per backend instance and pass it unchanged
     * to log_backend_tx_complete when an accepted async transfer ends. On failure,
     * release partially acquired resources before returning. Initialization
     * must not start a transfer or invoke the completion callback. */
    Log_Backend_Status (*init)(
        void* cb_context,
        void* backend_context
    );

    /* OK releases this instance and guarantees no later callbacks. A failure
     * must leave the instance/context valid for a subsequent deinit retry. */
    Log_Backend_Status (*deinit)(void* backend_context);

    Log_Backend_Status (*transmit)(
        const uint8_t* data,
        size_t length,
        void* backend_context
    );

    bool (*is_busy)(void* backend_context);

    void (*flush)(void* backend_context);
} Log_Backend_Ops;

typedef struct
{
    bool is_async;
    const Log_Backend_Ops *ops;
    void* context;
} Log_Backend;

/* Pointer to a backend descriptor that cannot be modified through this pointer. */
typedef const Log_Backend* Log_BackendPtr;

/* Pointer to a table of backend pointers. Table entries cannot be reassigned
 * through this pointer; the table length is supplied separately. */
typedef Log_BackendPtr const* Log_BackendTable;

/* Report exactly one terminal result per accepted asynchronous request. The
 * context is the opaque value supplied to ops->init; never inspect it. A
 * rejected request must not call this function. May run before transmit
 * returns OK or from an ISR supported by the configured lock/notification port. */
void log_backend_tx_complete(void *context, Log_Backend_TransferResult result);
#endif //TOOLLIB_LOG_BACKEND_H
