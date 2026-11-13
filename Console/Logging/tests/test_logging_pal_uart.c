#include "log_backend_pal_uart.h"
#include "test_runner.h"

#include <string.h>

typedef struct
{
    PalUartStatus init_result;
    PalUartStatus start_result;
    PalUartStatus deinit_result;
    PalUartCompletion completion;
    void *context;
    const uint8_t *data;
    size_t length;
    bool complete_inline;
    unsigned init_calls;
    unsigned start_calls;
    unsigned deinit_calls;
} FakeUart;

typedef struct
{
    unsigned calls;
    Log_Backend_TransferResult result;
} Completion;

static FakeUart uarts[2];
static Log_Backend backends[2];
static Log_Backend_PalUart_Context contexts[2];
static Completion completions[2];
static const uint8_t payload[] = "log";

static FakeUart *uart_for(PalUartId id)
{
    TEST_ASSERT_TRUE(id == PAL_UART1 || id == PAL_UART2);
    return &uarts[id == PAL_UART1 ? 0 : 1];
}

/* PAL fakes let the adapter's real callback be exercised without hardware.
 * The fixed manager completion entry point is captured below. */
PalUartStatus PAL_UART_Init(PalUartId id)
{
    FakeUart *uart = uart_for(id);
    ++uart->init_calls;
    return uart->init_result;
}

PalUartStatus PAL_UART_DeInit(PalUartId id)
{
    FakeUart *uart = uart_for(id);
    ++uart->deinit_calls;
    return uart->deinit_result;
}

PalUartStatus PAL_UART_StartTx(PalUartId id, PalUartAsyncMode mode,
                              const uint8_t *data, size_t length,
                              PalUartCompletion completion, void *context)
{
    FakeUart *uart = uart_for(id);
    TEST_ASSERT_EQUAL(PAL_UART_ASYNC_DMA, mode);
    TEST_ASSERT_NOT_NULL(completion);
    ++uart->start_calls;
    if (uart->start_result != PAL_UART_STATUS_OK)
        return uart->start_result;
    TEST_ASSERT_NULL(uart->completion);
    uart->data = data;
    uart->length = length;
    uart->completion = completion;
    uart->context = context;
    if (uart->complete_inline)
    {
        uart->completion = NULL;
        completion(id, PAL_UART_DIR_TX, PAL_UART_TRANSFER_OK, length, 0, context);
    }
    return PAL_UART_STATUS_OK;
}

void log_backend_tx_complete(void *context, Log_Backend_TransferResult result)
{
    TEST_ASSERT_TRUE(context == &completions[0] || context == &completions[1]);
    Completion *completion = context;
    ++completion->calls;
    completion->result = result;
}

static void finish(size_t index, PalUartTransferResult result)
{
    FakeUart *uart = &uarts[index];
    PalUartCompletion callback = uart->completion;
    TEST_ASSERT_NOT_NULL(callback);
    uart->completion = NULL;
    callback(contexts[index].id, PAL_UART_DIR_TX, result, uart->length, 0, uart->context);
}

void setUp(void)
{
    memset(uarts, 0, sizeof(uarts));
    memset(completions, 0, sizeof(completions));
    for (size_t i = 0; i < 2; ++i)
    {
        contexts[i] = (Log_Backend_PalUart_Context){.id = i == 0 ? PAL_UART1 : PAL_UART2};
        log_backend_pal_uart_configure(&backends[i], &contexts[i]);
    }
}

void tearDown(void)
{
}

static void test_uart_shared_ops_keep_completion_contexts_separate(void)
{
    TEST_ASSERT_EQUAL_PTR(backends[0].ops, backends[1].ops);
    for (size_t i = 0; i < 2; ++i)
    {
        TEST_ASSERT_TRUE(backends[i].is_async);
        TEST_ASSERT_EQUAL_PTR(&contexts[i], backends[i].context);
        TEST_ASSERT_EQUAL(LOG_BACKEND_OK,
            backends[i].ops->init(&completions[i], backends[i].context));
        TEST_ASSERT_EQUAL_PTR(&completions[i], contexts[i].tx_cplt_cb_context);
        TEST_ASSERT_EQUAL_UINT(1, uarts[i].init_calls);
        TEST_ASSERT_EQUAL(LOG_BACKEND_OK,
            backends[i].ops->transmit(payload, sizeof(payload), backends[i].context));
        TEST_ASSERT_EQUAL_PTR(&contexts[i], uarts[i].context);
        TEST_ASSERT_EQUAL_PTR(payload, uarts[i].data);
        TEST_ASSERT_EQUAL_UINT(sizeof(payload), uarts[i].length);
    }
    finish(1, PAL_UART_TRANSFER_OK);
    TEST_ASSERT_EQUAL_UINT(0, completions[0].calls);
    TEST_ASSERT_EQUAL_UINT(1, completions[1].calls);
    finish(0, PAL_UART_TRANSFER_ABORTED);
    TEST_ASSERT_EQUAL_UINT(1, completions[0].calls);
    TEST_ASSERT_EQUAL(LOG_BACKEND_TRANSFER_ERROR, completions[0].result);
    TEST_ASSERT_EQUAL(LOG_BACKEND_TRANSFER_OK, completions[1].result);
    for (size_t i = 0; i < 2; ++i)
    {
        TEST_ASSERT_EQUAL(LOG_BACKEND_OK, backends[i].ops->deinit(backends[i].context));
        TEST_ASSERT_EQUAL_UINT(1, uarts[i].deinit_calls);
    }
}

static void test_uart_maps_all_pal_terminal_results(void)
{
    const PalUartTransferResult results[] = {
        PAL_UART_TRANSFER_OK, PAL_UART_TRANSFER_CANCELLED,
        PAL_UART_TRANSFER_FINISHED_WITH_ERROR, PAL_UART_TRANSFER_ABORTED,
        PAL_UART_TRANSFER_ABORTED_WITH_ERROR, PAL_UART_TRANSFER_RX_TIMEOUT,
        PAL_UART_TRANSFER_RX_TIMEOUT_WITH_ERROR
    };
    TEST_ASSERT_EQUAL(LOG_BACKEND_OK, backends[0].ops->init(&completions[0], &contexts[0]));
    for (size_t i = 0; i < sizeof(results) / sizeof(results[0]); ++i)
    {
        TEST_ASSERT_EQUAL(LOG_BACKEND_OK,
            backends[0].ops->transmit(payload, sizeof(payload), &contexts[0]));
        finish(0, results[i]);
        Log_Backend_TransferResult expected = i == 0 ? LOG_BACKEND_TRANSFER_OK :
            i == 1 ? LOG_BACKEND_TRANSFER_CANCELLED : LOG_BACKEND_TRANSFER_ERROR;
        TEST_ASSERT_EQUAL(expected, completions[0].result);
        TEST_ASSERT_EQUAL_UINT(i + 1, completions[0].calls);
    }
}

static void test_uart_start_rejections_do_not_complete(void)
{
    TEST_ASSERT_EQUAL(LOG_BACKEND_OK, backends[0].ops->init(&completions[0], &contexts[0]));
    uarts[0].start_result = PAL_UART_STATUS_BUSY;
    TEST_ASSERT_EQUAL(LOG_BACKEND_BUSY,
        backends[0].ops->transmit(payload, sizeof(payload), &contexts[0]));
    uarts[0].start_result = PAL_UART_STATUS_IO_ERROR;
    TEST_ASSERT_EQUAL(LOG_BACKEND_ERROR,
        backends[0].ops->transmit(payload, sizeof(payload), &contexts[0]));
    TEST_ASSERT_EQUAL_UINT(0, completions[0].calls);
    TEST_ASSERT_NULL(uarts[0].completion);
}

static void test_uart_inline_completion_and_rx_filter(void)
{
    TEST_ASSERT_EQUAL(LOG_BACKEND_OK, backends[0].ops->init(&completions[0], &contexts[0]));
    uarts[0].complete_inline = true;
    TEST_ASSERT_EQUAL(LOG_BACKEND_OK,
        backends[0].ops->transmit(payload, sizeof(payload), &contexts[0]));
    TEST_ASSERT_EQUAL_UINT(1, completions[0].calls);
    TEST_ASSERT_EQUAL(LOG_BACKEND_TRANSFER_OK, completions[0].result);
    uarts[0].complete_inline = false;
    TEST_ASSERT_EQUAL(LOG_BACKEND_OK,
        backends[0].ops->transmit(payload, sizeof(payload), &contexts[0]));
    uarts[0].completion(PAL_UART1, PAL_UART_DIR_RX, PAL_UART_TRANSFER_OK,
                        sizeof(payload), 0, uarts[0].context);
    TEST_ASSERT_EQUAL_UINT(1, completions[0].calls);
    finish(0, PAL_UART_TRANSFER_OK);
    TEST_ASSERT_EQUAL_UINT(2, completions[0].calls);
}

static void test_uart_init_and_deinit_failures(void)
{
    uarts[1].init_result = PAL_UART_STATUS_IO_ERROR;
    TEST_ASSERT_EQUAL(LOG_BACKEND_ERROR, backends[1].ops->init(&completions[1], &contexts[1]));
    TEST_ASSERT_NULL(contexts[1].tx_cplt_cb_context);
    uarts[1].init_result = PAL_UART_STATUS_OK;
    TEST_ASSERT_EQUAL(LOG_BACKEND_OK, backends[1].ops->init(&completions[1], &contexts[1]));
    uarts[1].deinit_result = PAL_UART_STATUS_BUSY;
    TEST_ASSERT_EQUAL(LOG_BACKEND_ERROR, backends[1].ops->deinit(&contexts[1]));
    TEST_ASSERT_EQUAL_PTR(&completions[1], contexts[1].tx_cplt_cb_context);
    uarts[1].deinit_result = PAL_UART_STATUS_OK;
    TEST_ASSERT_EQUAL(LOG_BACKEND_OK, backends[1].ops->deinit(&contexts[1]));
}

static const TestCase cases[] = {
    TEST_CASE(test_uart_shared_ops_keep_completion_contexts_separate),
    TEST_CASE(test_uart_maps_all_pal_terminal_results),
    TEST_CASE(test_uart_start_rejections_do_not_complete),
    TEST_CASE(test_uart_inline_completion_and_rx_filter),
    TEST_CASE(test_uart_init_and_deinit_failures),
};

int main(int argc, char **argv)
{
    return run_test_cases(argc, argv, __FILE__, cases, sizeof(cases) / sizeof(cases[0]));
}
