#include "log_formatter.h"
#include "log_backend_manager.h"
#include "test_runner.h"

#include <string.h>

#if LOG_AUTO_NEWLINE
#define NEWLINE "\n"
#else
#define NEWLINE ""
#endif

typedef struct
{
    void *completion;
    const uint8_t *expected_data;
    size_t expected_size;
    unsigned calls;
    Log_Backend_Status result;
    bool inline_complete;
} FakeBackend;

static FakeBackend fakes[2];
static Log_Backend backends[2];
static Log_BackendPtr table[2];
static char record[LOG_MAX_SINGLE_LOG_LENGTH];

uint32_t Lock_IRQ_Disable(const void *context) { (void)context; return 0; }
void Lock_IRQ_Enable(const void *context, uint32_t previous) { (void)context; (void)previous; }
void log_port_notify(void) {}

static Log_Backend_Status fake_init(void *completion, void *context)
{
    ((FakeBackend *)context)->completion = completion;
    return LOG_BACKEND_OK;
}

static Log_Backend_Status fake_deinit(void *context)
{
    (void)context;
    return LOG_BACKEND_OK;
}

static Log_Backend_Status fake_transmit(const uint8_t *data, size_t size, void *context)
{
    FakeBackend *fake = context;
    ++fake->calls;
    TEST_ASSERT_EQUAL_PTR(fake->expected_data, data);
    TEST_ASSERT_EQUAL_UINT(fake->expected_size, size);
    if (fake->inline_complete && fake->result == LOG_BACKEND_OK)
        log_backend_tx_complete(fake->completion, LOG_BACKEND_TRANSFER_OK);
    return fake->result;
}

static const Log_Backend_Ops ops = {
    .init = fake_init, .deinit = fake_deinit, .transmit = fake_transmit
};

static size_t format_record(Log_Level level, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    size_t size = log_formatter_process(record, sizeof(record), level,
                                        "TAG", "file.c", 42, format, args);
    va_end(args);
    return size;
}

void setUp(void)
{
    memset(fakes, 0, sizeof(fakes));
    for (size_t i = 0; i < 2; ++i)
    {
        backends[i] = (Log_Backend){.ops = &ops, .context = &fakes[i]};
        table[i] = &backends[i];
    }
}

void tearDown(void)
{
    (void)log_backend_manager_deinit();
}

static void test_levels_preserve_newline_and_body(void)
{
    const Log_Level levels[] = {LOG_LEVEL_ERROR, LOG_LEVEL_WARNING,
                               LOG_LEVEL_INFO, LOG_LEVEL_DEBUG, LOG_LEVEL_NONE};
    const char *plain[] = {"[E][TAG] body" NEWLINE, "[W][TAG] body" NEWLINE,
                           "[I][TAG] body" NEWLINE, "[D][TAG] body" NEWLINE,
                           "[?][TAG] body" NEWLINE};
    const char *colors[] = {LOG_COLOR_E, LOG_COLOR_W, LOG_COLOR_I, LOG_COLOR_D, ""};
    for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); ++i)
    {
        size_t size = format_record(levels[i], "%s", "body");
        char original[sizeof(record)];
        memcpy(original, record, sizeof(record));
        size_t head = LOG_ENABLE_COLOR ? strlen(colors[i]) : 0;
        size_t tail = LOG_ENABLE_COLOR ? sizeof(LOG_CTRL_RESET) - 1 : 0;
        TEST_ASSERT_EQUAL_UINT(head + strlen(plain[i]) + tail, size);
        if (head != 0)
            TEST_ASSERT_EQUAL_MEMORY(colors[i], record, head);
        if (tail != 0)
            TEST_ASSERT_EQUAL_MEMORY(LOG_CTRL_RESET, record + size - tail, tail);
        TEST_ASSERT_EQUAL_CHAR('\0', record[size]);
        const uint8_t *slice = (const uint8_t *)record;
        log_formatter_strip_color(&slice, &size);
        TEST_ASSERT_EQUAL_PTR(record + head, slice);
        TEST_ASSERT_EQUAL_UINT(strlen(plain[i]), size);
        TEST_ASSERT_EQUAL_MEMORY(plain[i], slice, size);
        TEST_ASSERT_EQUAL_MEMORY(original, record, sizeof(record));
    }
}

static void assert_slice(const char *input, size_t size, size_t skip, size_t length)
{
    const uint8_t *slice = (const uint8_t *)input;
    log_formatter_strip_color(&slice, &size);
    TEST_ASSERT_EQUAL_PTR(input + skip, slice);
    TEST_ASSERT_EQUAL_UINT(length, size);
}

static void test_short_and_unrecognized_boundaries(void)
{
    assert_slice("", 0, 0, 0);
    assert_slice("\x1b", 1, 0, 1);
    assert_slice("\x1b[", 2, 0, 2);
    assert_slice("\x1b[0", 3, 0, 3);
    assert_slice("plain", 5, 0, 5);
    assert_slice("\x1b[99mX", 6, 0, 6);
    const char suffix[] = "text" LOG_CTRL_RESET;
    assert_slice(suffix, sizeof(suffix) - 1, 0,
                 LOG_ENABLE_COLOR ? 4 : sizeof(suffix) - 1);
    const char prefix[] = LOG_COLOR_I "text";
    size_t skip = LOG_ENABLE_COLOR ? sizeof(LOG_COLOR_I) - 1 : 0;
    assert_slice(prefix, sizeof(prefix) - 1, skip, sizeof(prefix) - 1 - skip);
}

static void test_embedded_ansi_is_preserved(void)
{
    const char expected[] = "[I][TAG] a" LOG_COLOR_E "b" LOG_CTRL_RESET NEWLINE;
    size_t size = format_record(LOG_LEVEL_INFO, "a%sb%s", LOG_COLOR_E, LOG_CTRL_RESET);
    const uint8_t *slice = (const uint8_t *)record;
    log_formatter_strip_color(&slice, &size);
    TEST_ASSERT_EQUAL_UINT(sizeof(expected) - 1, size);
    TEST_ASSERT_EQUAL_MEMORY(expected, slice, size);
}

static void test_truncation_keeps_tail_and_terminator(void)
{
    char body[LOG_MAX_SINGLE_LOG_LENGTH * 2];
    memset(body, 'x', sizeof(body) - 1);
    body[sizeof(body) - 1] = '\0';
    size_t size = format_record(LOG_LEVEL_INFO, "%s", body);
    TEST_ASSERT_EQUAL_UINT(sizeof(record) - 1, size);
    TEST_ASSERT_EQUAL_CHAR('\0', record[size]);
    const uint8_t *slice = (const uint8_t *)record;
    log_formatter_strip_color(&slice, &size);
    const char ending[] = "..." NEWLINE;
    TEST_ASSERT_EQUAL_MEMORY(ending, slice + size - (sizeof(ending) - 1), sizeof(ending) - 1);
}

static size_t prepare_backends(void)
{
    size_t size = format_record(LOG_LEVEL_INFO, "%s", "body");
    backends[1].strip_color = true;
    for (size_t i = 0; i < 2; ++i)
    {
        fakes[i].expected_data = (const uint8_t *)record;
        fakes[i].expected_size = size;
    }
#if LOG_ENABLE_COLOR
    fakes[1].expected_data += sizeof(LOG_COLOR_I) - 1;
    fakes[1].expected_size -= sizeof(LOG_COLOR_I) - 1 + sizeof(LOG_CTRL_RESET) - 1;
#endif
    TEST_ASSERT_EQUAL(LOG_STATUS_OK, log_backend_manager_init(table, 2));
    log_backend_manager_begin_record();
    return size;
}

static void test_backend_slices_survive_busy_and_cancel_retry(void)
{
    backends[0].is_async = true;
    backends[1].is_async = true;
    size_t size = prepare_backends();
    char original[sizeof(record)];
    memcpy(original, record, sizeof(record));
    fakes[1].result = LOG_BACKEND_BUSY;
    Log_BackendSummary summary = log_backend_manager_process((const uint8_t *)record, size);
    TEST_ASSERT_TRUE(summary.has_idle);
    TEST_ASSERT_TRUE(summary.has_in_progress);
    TEST_ASSERT_EQUAL(LOG_STATUS_BUSY, log_backend_manager_deinit());
    fakes[1].result = LOG_BACKEND_OK;
    summary = log_backend_manager_process((const uint8_t *)record, size);
    TEST_ASSERT_FALSE(summary.has_idle);
    log_backend_tx_complete(fakes[1].completion, LOG_BACKEND_TRANSFER_CANCELLED);
    summary = log_backend_manager_process((const uint8_t *)record, size);
    TEST_ASSERT_TRUE(summary.has_in_progress);
    TEST_ASSERT_EQUAL_UINT(1, fakes[0].calls);
    TEST_ASSERT_EQUAL_UINT(3, fakes[1].calls);
    TEST_ASSERT_EQUAL_MEMORY(original, record, sizeof(record));
    log_backend_tx_complete(fakes[0].completion, LOG_BACKEND_TRANSFER_OK);
    log_backend_tx_complete(fakes[1].completion, LOG_BACKEND_TRANSFER_OK);
    TEST_ASSERT_TRUE(log_backend_manager_get_status().all_finished);
}

static void test_backend_slices_with_inline_completion(void)
{
    backends[1].is_async = true;
    fakes[1].inline_complete = true;
    size_t size = prepare_backends();
    Log_BackendSummary summary = log_backend_manager_process((const uint8_t *)record, size);
    TEST_ASSERT_TRUE(summary.all_finished);
    TEST_ASSERT_FALSE(summary.has_in_progress);
    TEST_ASSERT_EQUAL_UINT(1, fakes[0].calls);
    TEST_ASSERT_EQUAL_UINT(1, fakes[1].calls);
}

static const TestCase cases[] = {
    TEST_CASE(test_levels_preserve_newline_and_body),
    TEST_CASE(test_short_and_unrecognized_boundaries),
    TEST_CASE(test_embedded_ansi_is_preserved),
    TEST_CASE(test_truncation_keeps_tail_and_terminator),
    TEST_CASE(test_backend_slices_survive_busy_and_cancel_retry),
    TEST_CASE(test_backend_slices_with_inline_completion),
};

int main(int argc, char **argv)
{
    return run_test_cases(argc, argv, __FILE__, cases, sizeof(cases) / sizeof(cases[0]));
}
