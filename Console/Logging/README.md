# Logging backend initialization

Configure a fixed table of backend instances at startup:

```c
/* Supplied by the application; each instance has its own persistent context. */
extern const Log_Backend uart_backend;
extern const Log_Backend usb_backend;

static Log_BackendPtr const backends[] = {
    &uart_backend,
    &usb_backend,
};

Log_Status application_log_init(void)
{
    const Log_Config config = {
        .backends = backends,
        .backend_count = sizeof(backends) / sizeof(backends[0]),
        .runtime_level = LOG_LEVEL_INFO,
    };
    return log_init(&config);
}
```

`Log_Config.backend` has been replaced by `backends` and `backend_count`.
`Log_BackendPtr` names a pointer to a const backend descriptor;
`Log_BackendTable` names a pointer to const entries of those pointers, equivalent
to `const Log_Backend *const *`. The `backends` field uses `Log_BackendTable`.
One to `LOG_MAX_BACKENDS` instances are accepted (default maximum: 4).
The core copies the pointers, so the table and config may be local variables.
Backend instances and their contexts must remain valid and unchanged until
cleanup succeeds. Each instance references a shared, read-only `Log_Backend_Ops`
table through `ops`. The table must implement `init`, `deinit` and `transmit`;
null tables and duplicate instance pointers are rejected. `is_async` and
`context` remain per-instance fields. Instances sharing operations still need
distinct mutable contexts and must not contend for the same hardware resource.

The core passes a distinct, stable callback context to each instance's `init`.
Save only the context per instance and pass it unchanged to the fixed
`log_backend_tx_complete(context, result)` entry point when reporting completion.
There is no per-instance callback function pointer. Backend initialization must
not start transfers or report completion. A failed backend or port initialization must
clean up its own partial allocation before returning. Successful backend
deinitialization guarantees that no later callbacks will occur; failed
deinitialization must leave the instance valid for a retry.

Initialization runs in buffer, port, backend-table order. Any failure rolls back
successful backends in reverse order. The original initialization error is
returned. If rollback fails, the remaining instances and shared resources stay
alive, logging is disabled, and `log_init` returns `LOG_STATUS_BUSY` until
`log_deinit` successfully finishes cleanup. Calling `log_deinit` after a failed
init is safe: `LOG_STATUS_NOT_INITIALIZED` means no cleanup remains.

`log_deinit` also runs backends in reverse order, retaining failed instances for
retry without deinitializing successful ones again. The port and buffer are
released only after all backends are released. An in-flight transfer returns
`LOG_STATUS_BUSY` without altering the active logger. Other cleanup failures
disable writing and processing. Successful teardown discards queued logs;
it does not flush them. Serialize initialization and teardown against all
producers and processing calls.

## Internal responsibilities

`src/log_backend_manager.c` owns the backend runtime array, initialization and
reverse-order cleanup, state lock, submission of idle backends and the fixed
completion entry point declared in `log_backend.h`. Its internal header exposes lifecycle operations, record
initialization, processing and a summary (`all_finished`, `has_idle`,
`has_in_progress`). State setters and runtime types are not public APIs.
Backends must not inspect the opaque completion context. The manager does not
own or release log buffers.

`src/log.c` owns log production and buffer lifetime, sequences buffer/port/manager
initialization and teardown, and translates backend summaries into process
results. A single consumer calls `log_process`: continue immediately on `NEXT`,
wait with a finite retry timeout on `RETRY`, wait for a notification on `WAIT`,
and handle buffer errors on `ERROR`. Multiple synchronous backends are supported;
BUSY retries do not resubmit completed or failed backends. A new record resets
the backend states only after the previous buffer has been released.

Asynchronous completion updates the instance state and notifies the log task
when event wakeup is enabled. Success and error are terminal states. The
current cancellation policy resets the instance to idle and retries the entire
record; already transmitted prefixes may therefore be duplicated. The CMSIS
port remains unfinished, so passing host tests does not establish RTOS or
hardware readiness. The configuration example assumes application-provided
backend instances, not a supplied USB implementation.

Build `test_logging_lifecycle` and `test_logging_pal_uart`, then run
`ctest -L logging` in the configured build directory. Lifecycle tests link the logging core against fake
backends and buffer/port/formatter stubs to inject lifecycle failures and check
multi-backend synchronous sends, BUSY retries, buffer retention and in-flight
teardown protection without requiring a UART or RTOS. They also exercise
out-of-order, inline and cancelled asynchronous completions through the fixed
entry point. UART adapter tests use PAL stubs and a captured completion entry
point to verify shared operations, separate contexts and PAL result mapping.
Test IRQ hooks check critical-section pairing; they do not validate real
interrupt concurrency.
