#include "log.h"
#include "log_backend_manager.h"
#include "log_buffer.h"
#include "log_formatter.h"
#include "log_port.h"
#include "test_runner.h"

#include <string.h>

enum { BACKEND_COUNT = 3, BUFFER_INIT = 1, PORT_INIT, PORT_DEINIT, BUFFER_DEINIT };

typedef struct
{
    int id;
    Log_Backend_Status init_result;
    Log_Backend_Status deinit_result;
    Log_Backend_Status transmit_result;
    unsigned init_calls;
    unsigned deinit_calls;
    unsigned transmit_calls;
    void *complete_context;
    bool complete_inline;
    Log_Backend_TransferResult inline_result;
} FakeBackend;

static FakeBackend fakes[BACKEND_COUNT];
static Log_Backend backends[BACKEND_COUNT];
static Log_BackendPtr table[BACKEND_COUNT];
static Log_Config config;
static int events[64];
static size_t event_count;
static BufferStatus buffer_init_result, buffer_deinit_result, buffer_read_result, buffer_free_result;
static Log_Port_Status port_init_result;
static unsigned allocate_calls, read_calls, notify_calls, free_calls;
static uint32_t irq_depth;
static bool readable;
static uint8_t payload[] = "test";
static Block block;
static Log_Status deinit_during_tx, init_during_tx;

/* Check that shared-state critical sections restore their previous state and
 * that backend calls are made outside the lock. This is not an SMP simulation. */
uint32_t Lock_IRQ_Disable(void)
{
    return irq_depth++;
}

void Lock_IRQ_Enable(uint32_t previous)
{
    TEST_ASSERT_EQUAL_UINT(previous + 1, irq_depth);
    irq_depth = previous;
}

static void record(int event)
{
    TEST_ASSERT_LESS_THAN(sizeof(events) / sizeof(events[0]), event_count);
    events[event_count++] = event;
}

static Log_Backend_Status fake_init(void *cb_context, void *backend_context)
{
    FakeBackend *fake = backend_context;
    record(100 + fake->id);
    ++fake->init_calls;
    fake->complete_context = cb_context;
    /* Initialization must not expose a partially configured logger. */
    log_write(LOG_LEVEL_INFO, "test", __FILE__, __LINE__, "during init");
    log_process();
    return fake->init_result;
}

static Log_Backend_Status fake_deinit(void *backend_context)
{
    FakeBackend *fake = backend_context;
    record(200 + fake->id);
    ++fake->deinit_calls;
    log_write(LOG_LEVEL_INFO, "test", __FILE__, __LINE__, "during cleanup");
    log_process();
    return fake->deinit_result;
}

static bool fake_is_busy(void *context)
{
    (void)context;
    return false;
}

static Log_Backend_Status fake_transmit(const uint8_t *data, size_t length, void *context)
{
    TEST_ASSERT_EQUAL_UINT(0, irq_depth);
    FakeBackend *fake = context;
    ++fake->transmit_calls;
    TEST_ASSERT_EQUAL_PTR(payload, data);
    TEST_ASSERT_EQUAL_UINT(sizeof(payload) - 1, length);
    /* Every backend is marked in progress before entering its transmit call. */
    deinit_during_tx = log_deinit();
    init_during_tx = log_init(&config);
    if (fake->transmit_result == LOG_BACKEND_OK && fake->complete_inline)
        log_backend_tx_complete(fake->complete_context, fake->inline_result);
    return fake->transmit_result;
}

static const Log_Backend_Ops fake_ops = {
    .init = fake_init,
    .deinit = fake_deinit,
    .transmit = fake_transmit,
    .is_busy = fake_is_busy,
};

/* Link-time stubs keep lifecycle tests independent of UART, RTOS and RBB. */
BufferStatus log_buffer_init(void)
{
    record(BUFFER_INIT);
    return buffer_init_result;
}

BufferStatus log_buffer_deinit(void)
{
    record(BUFFER_DEINIT);
    return buffer_deinit_result;
}

BufferStatus log_buffer_allocate(Block **out)
{
    ++allocate_calls;
    *out = NULL;
    return BUFFER_FULL;
}

BufferStatus log_buffer_read(Block **out)
{
    ++read_calls;
    if (buffer_read_result != BUFFER_OK)
    {
        *out = NULL;
        return buffer_read_result;
    }
    *out = readable ? &block : NULL;
    readable = false;
    return *out != NULL ? BUFFER_OK : BUFFER_EMPTY;
}

BufferStatus log_buffer_free(Block *value)
{
    TEST_ASSERT_EQUAL_PTR(&block, value);
    ++free_calls;
    return buffer_free_result;
}

BufferStatus log_buffer_write(Block *value, size_t size)
{
    (void)value;
    (void)size;
    return BUFFER_OK;
}

Log_Port_Status log_port_init(void)
{
    record(PORT_INIT);
    return port_init_result;
}

void log_port_deinit(void)
{
    record(PORT_DEINIT);
}

void log_port_notify(void)
{
    TEST_ASSERT_EQUAL_UINT(0, irq_depth);
    ++notify_calls;
}

size_t log_formatter_process(char *buffer, size_t size, Log_Level level,
                             const char *tag, const char *file, uint32_t line,
                             const char *format, va_list args)
{
    (void)buffer;
    (void)size;
    (void)level;
    (void)tag;
    (void)file;
    (void)line;
    (void)format;
    (void)args;
    return 0;
}

void setUp(void)
{
    memset(fakes, 0, sizeof(fakes));
    for (size_t i = 0; i < BACKEND_COUNT; ++i)
    {
        fakes[i].id = (int)i;
        fakes[i].transmit_result = LOG_BACKEND_ERROR;
        backends[i] = (Log_Backend){
            .ops = &fake_ops,
            .context = &fakes[i]
        };
        table[i] = &backends[i];
    }
    config = (Log_Config){.backends = table, .backend_count = BACKEND_COUNT,
                          .runtime_level = LOG_LEVEL_INFO};
    event_count = 0;
    allocate_calls = read_calls = notify_calls = free_calls = 0;
    irq_depth = 0;
    buffer_init_result = buffer_deinit_result = BUFFER_OK;
    buffer_read_result = buffer_free_result = BUFFER_OK;
    port_init_result = LOG_PORT_OK;
    readable = false;
    block = (Block){.data = payload, .size = sizeof(payload) - 1};
}

void tearDown(void)
{
    for (size_t i = 0; i < BACKEND_COUNT; ++i)
        fakes[i].deinit_result = LOG_BACKEND_OK;
    buffer_deinit_result = BUFFER_OK;
    (void)log_deinit();
}

static void assert_events(const int *expected, size_t count)
{
    TEST_ASSERT_EQUAL_UINT(count, event_count);
    TEST_ASSERT_EQUAL_INT_ARRAY(expected, events, count);
}

static void assert_logging_disabled(void)
{
    log_write(LOG_LEVEL_INFO, "test", __FILE__, __LINE__, "disabled");
    log_process();
    TEST_ASSERT_EQUAL_UINT(0, allocate_calls);
    TEST_ASSERT_EQUAL_UINT(0, read_calls);
    TEST_ASSERT_EQUAL_UINT(0, notify_calls);
}

static void test_single_backend_lifecycle(void)
{
    config.backend_count = 1;
    TEST_ASSERT_EQUAL(LOG_STATUS_OK, log_init(&config));
    TEST_ASSERT_NOT_NULL(fakes[0].complete_context);
    TEST_ASSERT_EQUAL_UINT(0, allocate_calls);
    TEST_ASSERT_EQUAL_UINT(0, read_calls);
    log_write(LOG_LEVEL_INFO, "test", __FILE__, __LINE__, "ready");
    TEST_ASSERT_EQUAL_UINT(1, allocate_calls);
    TEST_ASSERT_EQUAL(LOG_STATUS_OK, log_deinit());
    const int expected[] = {BUFFER_INIT, PORT_INIT, 100, 200, PORT_DEINIT, BUFFER_DEINIT};
    assert_events(expected, sizeof(expected) / sizeof(expected[0]));
}

static void test_table_copied_and_contexts_distinct(void)
{
    TEST_ASSERT_EQUAL(LOG_STATUS_OK, log_init(&config));
    for (size_t i = 0; i < BACKEND_COUNT; ++i)
    {
        TEST_ASSERT_NOT_NULL(fakes[i].complete_context);
        TEST_ASSERT_EQUAL_PTR(backends[0].ops, backends[i].ops);
        for (size_t j = 0; j < i; ++j)
            TEST_ASSERT_TRUE(fakes[i].complete_context != fakes[j].complete_context);
        table[i] = NULL;
    }
    /* Both the caller's table and config can go away after initialization. */
    config.backends = NULL;
    config.backend_count = 0;
    TEST_ASSERT_EQUAL(LOG_STATUS_OK, log_deinit());
    const int expected[] = {BUFFER_INIT, PORT_INIT, 100, 101, 102,
                            202, 201, 200, PORT_DEINIT, BUFFER_DEINIT};
    assert_events(expected, sizeof(expected) / sizeof(expected[0]));
    TEST_ASSERT_EQUAL_UINT(0, allocate_calls);
    TEST_ASSERT_EQUAL_UINT(0, read_calls);
}

static void test_invalid_tables_acquire_no_resources(void)
{
    TEST_ASSERT_EQUAL(LOG_STATUS_INVALID_ARGUMENT, log_init(NULL));
    config.backends = NULL;
    TEST_ASSERT_EQUAL(LOG_STATUS_INVALID_ARGUMENT, log_init(&config));
    config.backends = table;
    config.backend_count = 0;
    TEST_ASSERT_EQUAL(LOG_STATUS_INVALID_ARGUMENT, log_init(&config));
    config.backend_count = LOG_MAX_BACKENDS + 1;
    TEST_ASSERT_EQUAL(LOG_STATUS_INVALID_ARGUMENT, log_init(&config));
    config.backend_count = BACKEND_COUNT;
    table[2] = NULL;
    TEST_ASSERT_EQUAL(LOG_STATUS_INVALID_ARGUMENT, log_init(&config));
    table[2] = table[0];
    TEST_ASSERT_EQUAL(LOG_STATUS_INVALID_ARGUMENT, log_init(&config));
    table[2] = &backends[2];
    backends[2].ops = NULL;
    TEST_ASSERT_EQUAL(LOG_STATUS_INVALID_ARGUMENT, log_init(&config));
    Log_Backend_Ops invalid_ops = fake_ops;
    backends[2].ops = &invalid_ops;
    invalid_ops.init = NULL;
    TEST_ASSERT_EQUAL(LOG_STATUS_INVALID_ARGUMENT, log_init(&config));
    invalid_ops.init = fake_init;
    invalid_ops.deinit = NULL;
    TEST_ASSERT_EQUAL(LOG_STATUS_INVALID_ARGUMENT, log_init(&config));
    TEST_ASSERT_EQUAL_UINT(0, event_count);
    TEST_ASSERT_EQUAL(LOG_STATUS_NOT_INITIALIZED, log_deinit());
}

static void test_buffer_init_failure(void)
{
    buffer_init_result = BUFFER_ERROR;
    TEST_ASSERT_EQUAL(LOG_STATUS_BUFFER_ERROR, log_init(&config));
    const int expected[] = {BUFFER_INIT};
    assert_events(expected, 1);
    assert_logging_disabled();
    TEST_ASSERT_EQUAL(LOG_STATUS_NOT_INITIALIZED, log_deinit());
    buffer_init_result = BUFFER_OK;
    TEST_ASSERT_EQUAL(LOG_STATUS_OK, log_init(&config));
}

static void test_port_init_failure(void)
{
    port_init_result = LOG_PORT_ERROR;
    TEST_ASSERT_EQUAL(LOG_STATUS_PORT_ERROR, log_init(&config));
    const int expected[] = {BUFFER_INIT, PORT_INIT, BUFFER_DEINIT};
    assert_events(expected, 3);
    assert_logging_disabled();
    TEST_ASSERT_EQUAL(LOG_STATUS_NOT_INITIALIZED, log_deinit());
    port_init_result = LOG_PORT_OK;
    TEST_ASSERT_EQUAL(LOG_STATUS_OK, log_init(&config));
}

static void test_each_backend_failure_rolls_back_in_reverse(void)
{
    for (int failed = 0; failed < BACKEND_COUNT; ++failed)
    {
        fakes[failed].init_result = LOG_BACKEND_ERROR;
        event_count = 0;
        TEST_ASSERT_EQUAL(LOG_STATUS_BACKEND_ERROR, log_init(&config));
        int expected[16] = {BUFFER_INIT, PORT_INIT};
        size_t count = 2;
        for (int i = 0; i <= failed; ++i)
            expected[count++] = 100 + i;
        for (int i = failed - 1; i >= 0; --i)
            expected[count++] = 200 + i;
        expected[count++] = PORT_DEINIT;
        expected[count++] = BUFFER_DEINIT;
        assert_events(expected, count);
        TEST_ASSERT_EQUAL_UINT(0, fakes[failed].deinit_calls);
        assert_logging_disabled();
        TEST_ASSERT_EQUAL(LOG_STATUS_NOT_INITIALIZED, log_deinit());
        fakes[failed].init_result = LOG_BACKEND_OK;
    }
    TEST_ASSERT_EQUAL(LOG_STATUS_OK, log_init(&config));
}

static void test_failed_rollback_preserves_resources_for_retry(void)
{
    fakes[2].init_result = LOG_BACKEND_ERROR;
    fakes[1].deinit_result = LOG_BACKEND_ERROR;
    TEST_ASSERT_EQUAL(LOG_STATUS_BACKEND_ERROR, log_init(&config));
    void *saved_context = fakes[1].complete_context;
    const int expected[] = {BUFFER_INIT, PORT_INIT, 100, 101, 102, 201, 200};
    assert_events(expected, 7);
    TEST_ASSERT_EQUAL(LOG_STATUS_BUSY, log_init(&config));
    TEST_ASSERT_EQUAL_PTR(saved_context, fakes[1].complete_context);
    assert_logging_disabled();
    event_count = 0;
    TEST_ASSERT_EQUAL(LOG_STATUS_BACKEND_ERROR, log_deinit());
    const int retry[] = {201};
    assert_events(retry, 1);
    fakes[1].deinit_result = LOG_BACKEND_OK;
    event_count = 0;
    TEST_ASSERT_EQUAL(LOG_STATUS_OK, log_deinit());
    const int cleaned[] = {201, PORT_DEINIT, BUFFER_DEINIT};
    assert_events(cleaned, 3);
    TEST_ASSERT_EQUAL_UINT(1, fakes[0].deinit_calls);
    TEST_ASSERT_EQUAL_UINT(0, fakes[2].deinit_calls);
    fakes[2].init_result = LOG_BACKEND_OK;
    TEST_ASSERT_EQUAL(LOG_STATUS_OK, log_init(&config));
}

static void test_deinit_retries_only_failed_backends(void)
{
    TEST_ASSERT_EQUAL(LOG_STATUS_OK, log_init(&config));
    fakes[0].deinit_result = LOG_BACKEND_BUSY;
    fakes[2].deinit_result = LOG_BACKEND_ERROR;
    event_count = 0;
    TEST_ASSERT_EQUAL(LOG_STATUS_BACKEND_ERROR, log_deinit());
    const int partial[] = {202, 201, 200};
    assert_events(partial, 3);
    assert_logging_disabled();
    TEST_ASSERT_EQUAL(LOG_STATUS_BUSY, log_init(&config));
    fakes[0].deinit_result = fakes[2].deinit_result = LOG_BACKEND_OK;
    event_count = 0;
    TEST_ASSERT_EQUAL(LOG_STATUS_OK, log_deinit());
    const int retry[] = {202, 200, PORT_DEINIT, BUFFER_DEINIT};
    assert_events(retry, 4);
    TEST_ASSERT_EQUAL_UINT(1, fakes[1].deinit_calls);
    TEST_ASSERT_EQUAL(LOG_STATUS_OK, log_init(&config));
}

static void test_buffer_cleanup_failure_can_be_retried(void)
{
    TEST_ASSERT_EQUAL(LOG_STATUS_OK, log_init(&config));
    buffer_deinit_result = BUFFER_ERROR;
    TEST_ASSERT_EQUAL(LOG_STATUS_BUFFER_ERROR, log_deinit());
    TEST_ASSERT_EQUAL(LOG_STATUS_BUSY, log_init(&config));
    assert_logging_disabled();
    event_count = 0;
    buffer_deinit_result = BUFFER_OK;
    TEST_ASSERT_EQUAL(LOG_STATUS_OK, log_deinit());
    const int expected[] = {BUFFER_DEINIT};
    assert_events(expected, 1);
    TEST_ASSERT_EQUAL(LOG_STATUS_OK, log_init(&config));
}

static void test_port_failure_with_buffer_rollback_failure(void)
{
    port_init_result = LOG_PORT_ERROR;
    buffer_deinit_result = BUFFER_ERROR;
    TEST_ASSERT_EQUAL(LOG_STATUS_PORT_ERROR, log_init(&config));
    TEST_ASSERT_EQUAL(LOG_STATUS_BUSY, log_init(&config));
    assert_logging_disabled();
    event_count = 0;
    buffer_deinit_result = BUFFER_OK;
    TEST_ASSERT_EQUAL(LOG_STATUS_OK, log_deinit());
    const int expected[] = {BUFFER_DEINIT};
    assert_events(expected, 1);
}

static void test_reinitialization_does_not_reset_active_logger(void)
{
    TEST_ASSERT_EQUAL(LOG_STATUS_NOT_INITIALIZED, log_deinit());
    TEST_ASSERT_EQUAL(LOG_STATUS_OK, log_init(&config));
    void *saved_context = fakes[0].complete_context;
    size_t saved_event_count = event_count;
    TEST_ASSERT_EQUAL(LOG_STATUS_ALREADY_INITIALIZED, log_init(&config));
    TEST_ASSERT_EQUAL_UINT(saved_event_count, event_count);
    TEST_ASSERT_EQUAL_PTR(saved_context, fakes[0].complete_context);
    log_write(LOG_LEVEL_INFO, "test", __FILE__, __LINE__, "still ready");
    TEST_ASSERT_EQUAL_UINT(1, allocate_calls);
    TEST_ASSERT_EQUAL(LOG_STATUS_OK, log_deinit());
    TEST_ASSERT_EQUAL(LOG_STATUS_NOT_INITIALIZED, log_deinit());
    TEST_ASSERT_EQUAL(LOG_STATUS_OK, log_init(&config));
}

static void test_lifecycle_rejects_missing_transmit(void)
{
    config.backend_count = 1;
    Log_Backend_Ops invalid_ops = fake_ops;
    invalid_ops.transmit = NULL;
    invalid_ops.is_busy = NULL;
    backends[0].ops = &invalid_ops;
    TEST_ASSERT_EQUAL(LOG_STATUS_INVALID_ARGUMENT, log_init(&config));
    TEST_ASSERT_EQUAL_UINT(0, event_count);
    TEST_ASSERT_EQUAL(LOG_STATUS_NOT_INITIALIZED, log_deinit());
}

static void test_deinit_while_transmitting_preserves_resources(void)
{
    config.backend_count = 1;
    TEST_ASSERT_EQUAL(LOG_STATUS_OK, log_init(&config));
    event_count = 0;
    readable = true;
    log_process();
    TEST_ASSERT_EQUAL_UINT(1, fakes[0].transmit_calls);
    TEST_ASSERT_EQUAL(LOG_STATUS_BUSY, deinit_during_tx);
    TEST_ASSERT_EQUAL(LOG_STATUS_ALREADY_INITIALIZED, init_during_tx);
    TEST_ASSERT_EQUAL_UINT(0, event_count);
    log_write(LOG_LEVEL_INFO, "test", __FILE__, __LINE__, "still ready");
    TEST_ASSERT_EQUAL_UINT(1, allocate_calls);
    TEST_ASSERT_EQUAL(LOG_STATUS_OK, log_deinit());
}

static void test_sync_backends_send_each_record_once(void)
{
    for (size_t i = 0; i < BACKEND_COUNT; ++i)
        fakes[i].transmit_result = LOG_BACKEND_OK;
    TEST_ASSERT_EQUAL(LOG_STATUS_OK, log_init(&config));
    readable = true;
    TEST_ASSERT_EQUAL(LOG_PROCESS_NEXT, log_process());
    TEST_ASSERT_EQUAL_UINT(0, free_calls);
    Log_BackendSummary summary = log_backend_manager_get_status();
    TEST_ASSERT_TRUE(summary.all_finished);
    TEST_ASSERT_FALSE(summary.has_idle);
    TEST_ASSERT_FALSE(summary.has_in_progress);

    readable = true;
    TEST_ASSERT_EQUAL(LOG_PROCESS_NEXT, log_process());
    TEST_ASSERT_EQUAL_UINT(1, free_calls);
    TEST_ASSERT_EQUAL(LOG_PROCESS_WAIT, log_process());
    TEST_ASSERT_EQUAL_UINT(2, free_calls);
    for (size_t i = 0; i < BACKEND_COUNT; ++i)
        TEST_ASSERT_EQUAL_UINT(2, fakes[i].transmit_calls);
}

static void test_busy_retry_does_not_resend_finished_backends(void)
{
    fakes[0].transmit_result = LOG_BACKEND_OK;
    fakes[1].transmit_result = LOG_BACKEND_BUSY;
    /* Backend 2 terminates with ERROR and must not be resubmitted either. */
    TEST_ASSERT_EQUAL(LOG_STATUS_OK, log_init(&config));
    readable = true;
    TEST_ASSERT_EQUAL(LOG_PROCESS_RETRY, log_process());
    Log_BackendSummary summary = log_backend_manager_get_status();
    TEST_ASSERT_TRUE(summary.has_idle);
    TEST_ASSERT_FALSE(summary.has_in_progress);
    TEST_ASSERT_FALSE(summary.all_finished);
    TEST_ASSERT_EQUAL(LOG_PROCESS_RETRY, log_process());
    TEST_ASSERT_EQUAL_UINT(0, free_calls);
    TEST_ASSERT_EQUAL_UINT(1, read_calls);

    fakes[1].transmit_result = LOG_BACKEND_OK;
    TEST_ASSERT_EQUAL(LOG_PROCESS_WAIT, log_process());
    TEST_ASSERT_EQUAL_UINT(1, free_calls);
    TEST_ASSERT_EQUAL_UINT(1, fakes[0].transmit_calls);
    TEST_ASSERT_EQUAL_UINT(3, fakes[1].transmit_calls);
    TEST_ASSERT_EQUAL_UINT(1, fakes[2].transmit_calls);
}

static void test_async_in_progress_retains_buffer_and_blocks_deinit(void)
{
    backends[1].is_async = true;
    for (size_t i = 0; i < BACKEND_COUNT; ++i)
        fakes[i].transmit_result = LOG_BACKEND_OK;
    TEST_ASSERT_EQUAL(LOG_STATUS_OK, log_init(&config));
    readable = true;
    TEST_ASSERT_EQUAL(LOG_PROCESS_WAIT, log_process());
    TEST_ASSERT_EQUAL(LOG_PROCESS_WAIT, log_process());
    Log_BackendSummary summary = log_backend_manager_get_status();
    TEST_ASSERT_TRUE(summary.has_in_progress);
    TEST_ASSERT_FALSE(summary.has_idle);
    TEST_ASSERT_FALSE(summary.all_finished);
    TEST_ASSERT_EQUAL_UINT(0, free_calls);
    TEST_ASSERT_EQUAL_UINT(1, read_calls);
    TEST_ASSERT_EQUAL(LOG_STATUS_BUSY, log_deinit());
    TEST_ASSERT_EQUAL(LOG_STATUS_ALREADY_INITIALIZED, log_init(&config));
    for (size_t i = 0; i < BACKEND_COUNT; ++i)
    {
        TEST_ASSERT_EQUAL_UINT(1, fakes[i].transmit_calls);
        TEST_ASSERT_EQUAL_UINT(0, fakes[i].deinit_calls);
    }
    log_backend_tx_complete(fakes[1].complete_context, LOG_BACKEND_TRANSFER_OK);
    TEST_ASSERT_EQUAL_UINT(1, notify_calls);
    TEST_ASSERT_EQUAL(LOG_PROCESS_WAIT, log_process());
    TEST_ASSERT_EQUAL_UINT(1, free_calls);
    TEST_ASSERT_EQUAL(LOG_STATUS_OK, log_deinit());
}

static void test_shared_ops_async_completions_are_isolated(void)
{
    for (size_t i = 0; i < BACKEND_COUNT; ++i)
    {
        backends[i].is_async = true;
        fakes[i].transmit_result = LOG_BACKEND_OK;
        TEST_ASSERT_EQUAL_PTR(&fake_ops, backends[i].ops);
    }
    TEST_ASSERT_EQUAL(LOG_STATUS_OK, log_init(&config));
    readable = true;
    TEST_ASSERT_EQUAL(LOG_PROCESS_WAIT, log_process());
    log_backend_tx_complete(fakes[2].complete_context, LOG_BACKEND_TRANSFER_OK);
    TEST_ASSERT_EQUAL(LOG_PROCESS_WAIT, log_process());
    TEST_ASSERT_EQUAL_UINT(0, free_calls);
    log_backend_tx_complete(fakes[0].complete_context, LOG_BACKEND_TRANSFER_ERROR);
    TEST_ASSERT_EQUAL(LOG_PROCESS_WAIT, log_process());
    TEST_ASSERT_EQUAL_UINT(0, free_calls);
    TEST_ASSERT_EQUAL(LOG_STATUS_BUSY, log_deinit());
    log_backend_tx_complete(fakes[1].complete_context, LOG_BACKEND_TRANSFER_OK);
    TEST_ASSERT_EQUAL(LOG_PROCESS_WAIT, log_process());
    TEST_ASSERT_EQUAL_UINT(1, free_calls);
    TEST_ASSERT_EQUAL_UINT(3, notify_calls);
    for (size_t i = 0; i < BACKEND_COUNT; ++i)
        TEST_ASSERT_EQUAL_UINT(1, fakes[i].transmit_calls);
}

static void test_inline_async_completion_is_not_overwritten(void)
{
    config.backend_count = 1;
    backends[0].is_async = true;
    fakes[0].transmit_result = LOG_BACKEND_OK;
    fakes[0].complete_inline = true;
    TEST_ASSERT_EQUAL(LOG_STATUS_OK, log_init(&config));
    readable = true;
    TEST_ASSERT_EQUAL(LOG_PROCESS_NEXT, log_process());
    TEST_ASSERT_TRUE(log_backend_manager_get_status().all_finished);
    TEST_ASSERT_EQUAL_UINT(1, notify_calls);
    TEST_ASSERT_EQUAL(LOG_PROCESS_WAIT, log_process());
    TEST_ASSERT_EQUAL_UINT(1, free_calls);
    TEST_ASSERT_EQUAL_UINT(1, fakes[0].transmit_calls);
}

static void test_cancel_retries_only_cancelled_backend(void)
{
    backends[1].is_async = true;
    for (size_t i = 0; i < BACKEND_COUNT; ++i)
        fakes[i].transmit_result = LOG_BACKEND_OK;
    TEST_ASSERT_EQUAL(LOG_STATUS_OK, log_init(&config));
    readable = true;
    TEST_ASSERT_EQUAL(LOG_PROCESS_WAIT, log_process());
    log_backend_tx_complete(fakes[1].complete_context, LOG_BACKEND_TRANSFER_CANCELLED);
    TEST_ASSERT_TRUE(log_backend_manager_get_status().has_idle);
    TEST_ASSERT_EQUAL(LOG_PROCESS_WAIT, log_process());
    TEST_ASSERT_EQUAL_UINT(1, fakes[0].transmit_calls);
    TEST_ASSERT_EQUAL_UINT(2, fakes[1].transmit_calls);
    TEST_ASSERT_EQUAL_UINT(1, fakes[2].transmit_calls);
    TEST_ASSERT_EQUAL_UINT(0, free_calls);
    log_backend_tx_complete(fakes[1].complete_context, LOG_BACKEND_TRANSFER_OK);
    TEST_ASSERT_EQUAL(LOG_PROCESS_WAIT, log_process());
    TEST_ASSERT_EQUAL_UINT(1, free_calls);
    TEST_ASSERT_EQUAL_UINT(2, notify_calls);
}

static void test_buffer_errors_do_not_wait_or_resend(void)
{
    TEST_ASSERT_EQUAL(LOG_STATUS_OK, log_init(&config));
    buffer_read_result = BUFFER_ERROR;
    TEST_ASSERT_EQUAL(LOG_PROCESS_ERROR, log_process());
    for (size_t i = 0; i < BACKEND_COUNT; ++i)
        TEST_ASSERT_EQUAL_UINT(0, fakes[i].transmit_calls);

    buffer_read_result = BUFFER_OK;
    readable = true;
    TEST_ASSERT_EQUAL(LOG_PROCESS_NEXT, log_process());
    buffer_free_result = BUFFER_ERROR;
    TEST_ASSERT_EQUAL(LOG_PROCESS_ERROR, log_process());
    TEST_ASSERT_EQUAL_UINT(2, read_calls);
    buffer_free_result = BUFFER_OK;
    TEST_ASSERT_EQUAL(LOG_PROCESS_WAIT, log_process());
    TEST_ASSERT_EQUAL_UINT(2, free_calls);
    for (size_t i = 0; i < BACKEND_COUNT; ++i)
        TEST_ASSERT_EQUAL_UINT(1, fakes[i].transmit_calls);
}

static const TestCase cases[] = {
    TEST_CASE(test_single_backend_lifecycle),
    TEST_CASE(test_table_copied_and_contexts_distinct),
    TEST_CASE(test_invalid_tables_acquire_no_resources),
    TEST_CASE(test_buffer_init_failure),
    TEST_CASE(test_port_init_failure),
    TEST_CASE(test_each_backend_failure_rolls_back_in_reverse),
    TEST_CASE(test_failed_rollback_preserves_resources_for_retry),
    TEST_CASE(test_deinit_retries_only_failed_backends),
    TEST_CASE(test_buffer_cleanup_failure_can_be_retried),
    TEST_CASE(test_port_failure_with_buffer_rollback_failure),
    TEST_CASE(test_reinitialization_does_not_reset_active_logger),
    TEST_CASE(test_lifecycle_rejects_missing_transmit),
    TEST_CASE(test_deinit_while_transmitting_preserves_resources),
    TEST_CASE(test_sync_backends_send_each_record_once),
    TEST_CASE(test_busy_retry_does_not_resend_finished_backends),
    TEST_CASE(test_async_in_progress_retains_buffer_and_blocks_deinit),
    TEST_CASE(test_shared_ops_async_completions_are_isolated),
    TEST_CASE(test_inline_async_completion_is_not_overwritten),
    TEST_CASE(test_cancel_retries_only_cancelled_backend),
    TEST_CASE(test_buffer_errors_do_not_wait_or_resend),
};

int main(int argc, char **argv)
{
    return run_test_cases(argc, argv, __FILE__, cases, sizeof(cases) / sizeof(cases[0]));
}
