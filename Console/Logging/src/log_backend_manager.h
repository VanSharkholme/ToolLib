#ifndef TOOLLIB_LOG_BACKEND_MANAGER_H
#define TOOLLIB_LOG_BACKEND_MANAGER_H

#include "log_backend.h"
#include "log_types.h"

/* Internal to the logging core. All operations except the completion callback
 * have one serialized caller. Backend state and its lock stay private. */
typedef struct
{
    bool all_finished;
    bool has_idle;
    bool has_in_progress;
} Log_BackendSummary;

/* Validate before acquiring the logger's buffer and port resources. */
Log_Status log_backend_manager_validate(Log_BackendTable backends, size_t count);

/* Copies the table and initializes instances in order. On failure, call deinit
 * to roll back successful instances; retain port/buffer until it succeeds. */
Log_Status log_backend_manager_init(Log_BackendTable backends, size_t count);

/* Reverse-order cleanup; retains failed instances for retry. An empty manager
 * is already clean and returns OK. In-flight transfers return BUSY. */
Log_Status log_backend_manager_deinit(void);

/* Begin a new record only after the previous record is finished (or after
 * initialization). Does not take ownership of the record's buffer. */
void log_backend_manager_begin_record(void);

/* Submit only idle backends. Use the same data/size on retries until all
 * backends finish. Backend calls occur outside the state lock. */
Log_BackendSummary log_backend_manager_process(const uint8_t *data, size_t size);

/* Snapshot all backend states in one critical section. */
Log_BackendSummary log_backend_manager_get_status(void);

#endif // TOOLLIB_LOG_BACKEND_MANAGER_H
