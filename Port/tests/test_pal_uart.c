#include "pal_uart.h"
#include "pal_uart_host.h"
#include "test_runner.h"

static const PalUartId USART1 = (PalUartId)0x101;
static const PalUartId LPUART1 = (PalUartId)0x301;
static const PalUartId UNKNOWN = (PalUartId)0x202;

static const PalUartConfig config = {
    .baud_rate = 115200,
    .data_bits = PAL_UART_DATA_8BITS,
    .parity = PAL_UART_PARITY_NONE,
    .stop_bits = PAL_UART_STOP_1,
    .directions = PAL_UART_DIR_TX | PAL_UART_DIR_RX
};

typedef struct {
    unsigned calls;
    PalUartDirection direction;
    PalUartTransferResult result;
    size_t transferred;
    PalUartErrors errors;
    int rearm_rx;
} CompletionRecord;

static uint8_t rearmed_rx;

static void completion(PalUartId id, PalUartDirection direction,
                       PalUartTransferResult result, size_t transferred, PalUartErrors errors, void *context)
{
    TEST_ASSERT_EQUAL_UINT(USART1, id);
    CompletionRecord *record = context;
    ++record->calls;
    record->direction = direction;
    record->result = result;
    record->transferred = transferred;
    record->errors = errors;
    if (record->rearm_rx && direction == PAL_UART_DIR_RX) {
        record->rearm_rx = 0;
        TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK,
            PAL_UART_StartRx(id, PAL_UART_ASYNC_IRQ, &rearmed_rx, 1, completion, context));
    }
}

void setUp(void)
{
    PAL_UART_Host_Reset();
    PalUartConfig tx_only = config;
    tx_only.directions = PAL_UART_DIR_TX;
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK,
        PAL_UART_Host_Define(USART1, &config, PAL_UART_DIR_TX | PAL_UART_DIR_RX));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK,
        PAL_UART_Host_Define(LPUART1, &tx_only, PAL_UART_DIR_TX));
}

void tearDown(void) {}

static void test_sparse_ids_and_configured_directions(void)
{
    uint8_t byte = 0;
    size_t transferred = 1;
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_INVALID_ID, PAL_UART_Init(UNKNOWN));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Init(LPUART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_UNSUPPORTED,
                          PAL_UART_Read(LPUART1, &byte, 1, 0, &transferred));
    TEST_ASSERT_EQUAL_size_t(0, transferred);
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK,
                          PAL_UART_Write(LPUART1, &byte, 1, 0, &transferred));
}

static void test_init_validation_and_ownership(void)
{
    PalUartConfig invalid = config;
    invalid.baud_rate = 0;
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_INVALID_ARGUMENT,
                          PAL_UART_Reconfigure(USART1, &invalid));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_NOT_INITIALIZED,
                          PAL_UART_Reconfigure(USART1, &config));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_NOT_INITIALIZED, PAL_UART_AttachInitialized(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Init(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_ALREADY_INITIALIZED, PAL_UART_Init(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_BUSY, PAL_UART_Host_SetExternalReady(USART1, 1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_DeInit(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Host_SetExternalReady(USART1, 1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_BUSY, PAL_UART_Init(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_AttachInitialized(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Detach(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_AttachInitialized(USART1));
}

static void test_blocking_partial_read_and_write(void)
{
    uint8_t input[] = {1, 2, 3};
    uint8_t output[5] = {0};
    size_t transferred = 99;
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Init(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Host_FeedRx(USART1, input, sizeof input));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_TIMEOUT,
                          PAL_UART_Read(USART1, output, sizeof output, 0, &transferred));
    TEST_ASSERT_EQUAL_size_t(3, transferred);
    TEST_ASSERT_EQUAL_MEMORY(input, output, 3);
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK,
                          PAL_UART_Write(USART1, input, sizeof input, 100, &transferred));
    TEST_ASSERT_EQUAL_size_t(3, transferred);
    memset(output, 0, sizeof output);
    TEST_ASSERT_EQUAL_size_t(3, PAL_UART_Host_TakeTx(USART1, output, sizeof output));
    TEST_ASSERT_EQUAL_MEMORY(input, output, 3);
    transferred = 99;
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_INVALID_ARGUMENT,
                          PAL_UART_Write(USART1, NULL, 3, 0, &transferred));
    TEST_ASSERT_EQUAL_size_t(0, transferred);
}

static void test_async_mode_busy_completion_and_cancel(void)
{
    uint8_t tx[] = {4, 5, 6};
    uint8_t rx[2] = {0};
    uint8_t incoming[] = {9, 8};
    CompletionRecord record = {0};
    CompletionRecord rx_record = {0};
    size_t transferred = 0;
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Init(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_INVALID_ARGUMENT,
        PAL_UART_StartTx(USART1, (PalUartAsyncMode)99, tx, sizeof tx, completion, &record));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_UNSUPPORTED,
        PAL_UART_StartRx(USART1, PAL_UART_ASYNC_DMA, rx, sizeof rx, completion, &record));
    TEST_ASSERT_EQUAL_UINT(0, record.calls);
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK,
        PAL_UART_Host_SetDmaBound(USART1, PAL_UART_DIR_TX, 1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK,
        PAL_UART_StartTx(USART1, PAL_UART_ASYNC_DMA, tx, sizeof tx, completion, &record));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_BUSY,
        PAL_UART_Host_SetDmaBound(USART1, PAL_UART_DIR_TX, 0));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_BUSY,
        PAL_UART_StartTx(USART1, PAL_UART_ASYNC_IRQ, tx, sizeof tx, completion, &record));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_INVALID_STATE, PAL_UART_Detach(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_BUSY, PAL_UART_DeInit(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_BUSY,
                          PAL_UART_Write(USART1, tx, sizeof tx, 0, &transferred));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK,
        PAL_UART_StartRx(USART1, PAL_UART_ASYNC_IRQ, rx, sizeof rx, completion, &rx_record));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Host_FeedRx(USART1, incoming, sizeof incoming));
    TEST_ASSERT_EQUAL_UINT(1, rx_record.calls);
    TEST_ASSERT_EQUAL_INT(PAL_UART_TRANSFER_OK, rx_record.result);
    TEST_ASSERT_EQUAL_INT(PAL_UART_DIR_RX, rx_record.direction);
    TEST_ASSERT_EQUAL_MEMORY(incoming, rx, sizeof rx);
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Cancel(USART1, PAL_UART_DIR_TX));
    TEST_ASSERT_EQUAL_UINT(1, record.calls);
    TEST_ASSERT_EQUAL_INT(PAL_UART_TRANSFER_CANCELLED, record.result);
    TEST_ASSERT_EQUAL_size_t(0, record.transferred);
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_NOT_ACTIVE,
                          PAL_UART_Host_CompleteTx(USART1, PAL_UART_TRANSFER_OK, 3, PAL_UART_ERROR_NONE));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_NOT_ACTIVE, PAL_UART_Cancel(USART1, PAL_UART_DIR_TX));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_DeInit(USART1));
}

static void test_async_tx_error_and_buffer_lifetime(void)
{
    uint8_t tx[] = {1, 2, 3};
    uint8_t captured[3] = {0};
    CompletionRecord record = {0};
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Init(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK,
        PAL_UART_StartTx(USART1, PAL_UART_ASYNC_IRQ, tx, sizeof tx, completion, &record));
    tx[0] = 7;
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK,
        PAL_UART_Host_CompleteTx(USART1, PAL_UART_TRANSFER_ABORTED, 2, PAL_UART_ERROR_DMA));
    TEST_ASSERT_EQUAL_UINT(1, record.calls);
    TEST_ASSERT_EQUAL_INT(PAL_UART_TRANSFER_ABORTED, record.result);
    TEST_ASSERT_EQUAL_UINT(PAL_UART_ERROR_DMA, record.errors);
    TEST_ASSERT_EQUAL_size_t(2, record.transferred);
    TEST_ASSERT_EQUAL_size_t(2, PAL_UART_Host_TakeTx(USART1, captured, sizeof captured));
    TEST_ASSERT_EQUAL_UINT8(7, captured[0]);
}

static void test_driver_start_rejection_does_not_complete(void)
{
    uint8_t tx = 3;
    CompletionRecord record = {0};
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Init(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK,
        PAL_UART_Host_SetNextStartResult(USART1, PAL_UART_DIR_TX, PAL_UART_STATUS_IO_ERROR));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_IO_ERROR,
        PAL_UART_StartTx(USART1, PAL_UART_ASYNC_IRQ, &tx, 1, completion, &record));
    TEST_ASSERT_EQUAL_UINT(0, record.calls);
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_NOT_ACTIVE, PAL_UART_Cancel(USART1, PAL_UART_DIR_TX));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK,
        PAL_UART_StartTx(USART1, PAL_UART_ASYNC_IRQ, &tx, 1, completion, &record));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK,
        PAL_UART_Host_CompleteTx(USART1, PAL_UART_TRANSFER_OK, 1, PAL_UART_ERROR_NONE));
    TEST_ASSERT_EQUAL_UINT(1, record.calls);
}

static void test_reconfigure_directions_and_preserve_on_rejection(void)
{
    PalUartConfig tx_only = config;
    tx_only.directions = PAL_UART_DIR_TX;
    uint8_t byte = 0;
    size_t transferred = 1;

    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Init(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Reconfigure(USART1, &tx_only));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_UNSUPPORTED,
                          PAL_UART_Read(USART1, &byte, 1, 0, &transferred));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Reconfigure(USART1, &config));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_TIMEOUT,
                          PAL_UART_Read(USART1, &byte, 1, 0, &transferred));

    PalUartConfig invalid = config;
    invalid.parity = (PalUartParity)-1;
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_INVALID_ARGUMENT,
                          PAL_UART_Reconfigure(USART1, &invalid));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Init(LPUART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_UNSUPPORTED,
                          PAL_UART_Reconfigure(LPUART1, &config));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_UNSUPPORTED,
                          PAL_UART_Read(LPUART1, &byte, 1, 0, &transferred));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK,
                          PAL_UART_Write(LPUART1, &byte, 1, 0, &transferred));
}

static void test_reconfigure_busy_driver_failure_and_attached_handle(void)
{
    PalUartConfig tx_only = config;
    tx_only.directions = PAL_UART_DIR_TX;
    uint8_t byte = 1;
    size_t transferred = 0;
    CompletionRecord record = {0};

    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Init(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK,
        PAL_UART_StartTx(USART1, PAL_UART_ASYNC_IRQ, &byte, 1, completion, &record));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_BUSY,
                          PAL_UART_Reconfigure(USART1, &tx_only));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Cancel(USART1, PAL_UART_DIR_TX));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK,
        PAL_UART_Host_SetNextReconfigureResult(USART1, PAL_UART_STATUS_IO_ERROR));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_IO_ERROR,
                          PAL_UART_Reconfigure(USART1, &tx_only));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_TIMEOUT,
                          PAL_UART_Read(USART1, &byte, 1, 0, &transferred));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_DeInit(USART1));

    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Host_SetExternalReady(USART1, 1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_AttachInitialized(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Reconfigure(USART1, &tx_only));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_UNSUPPORTED,
                          PAL_UART_Read(USART1, &byte, 1, 0, &transferred));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Detach(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_AttachInitialized(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_UNSUPPORTED,
                          PAL_UART_Read(USART1, &byte, 1, 0, &transferred));
}

static void test_release_requires_matching_initialization_path(void)
{
    uint8_t byte = 1;
    size_t transferred = 0;
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_INVALID_ID, PAL_UART_DeInit(UNKNOWN));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_INVALID_ID, PAL_UART_Detach(UNKNOWN));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_NOT_INITIALIZED, PAL_UART_DeInit(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_NOT_INITIALIZED, PAL_UART_Detach(USART1));

    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Init(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_INVALID_STATE, PAL_UART_Detach(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK,
                          PAL_UART_Write(USART1, &byte, 1, 0, &transferred));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_DeInit(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_NOT_INITIALIZED, PAL_UART_DeInit(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Init(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_DeInit(USART1));

    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Host_SetExternalReady(USART1, 1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_AttachInitialized(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_INVALID_STATE, PAL_UART_DeInit(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK,
                          PAL_UART_Write(USART1, &byte, 1, 0, &transferred));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Detach(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_NOT_INITIALIZED, PAL_UART_Detach(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_AttachInitialized(USART1));
}

static void test_detach_waits_for_transfers_and_blocks_access_after_release(void)
{
    uint8_t tx = 1;
    uint8_t rx = 0;
    size_t transferred = 99;
    CompletionRecord tx_record = {0};
    CompletionRecord rx_record = {0};
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Host_SetExternalReady(USART1, 1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_AttachInitialized(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK,
        PAL_UART_StartTx(USART1, PAL_UART_ASYNC_IRQ, &tx, 1, completion, &tx_record));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK,
        PAL_UART_StartRx(USART1, PAL_UART_ASYNC_IRQ, &rx, 1, completion, &rx_record));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_INVALID_STATE, PAL_UART_DeInit(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_BUSY, PAL_UART_Detach(USART1));
    TEST_ASSERT_EQUAL_UINT(0, tx_record.calls);
    TEST_ASSERT_EQUAL_UINT(0, rx_record.calls);

    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Cancel(USART1, PAL_UART_DIR_TX));
    TEST_ASSERT_EQUAL_UINT(1, tx_record.calls);
    TEST_ASSERT_EQUAL_INT(PAL_UART_TRANSFER_CANCELLED, tx_record.result);
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_BUSY, PAL_UART_Detach(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Host_FeedRx(USART1, &tx, 1));
    TEST_ASSERT_EQUAL_UINT(1, rx_record.calls);
    TEST_ASSERT_EQUAL_INT(PAL_UART_TRANSFER_OK, rx_record.result);
    TEST_ASSERT_EQUAL_UINT8(tx, rx);
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Detach(USART1));

    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_NOT_INITIALIZED,
                          PAL_UART_Write(USART1, &tx, 1, 0, &transferred));
    TEST_ASSERT_EQUAL_size_t(0, transferred);
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_NOT_INITIALIZED,
                          PAL_UART_Read(USART1, &rx, 1, 0, &transferred));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_NOT_INITIALIZED,
        PAL_UART_StartTx(USART1, PAL_UART_ASYNC_IRQ, &tx, 1, completion, &tx_record));
    TEST_ASSERT_EQUAL_UINT(1, tx_record.calls);
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_NOT_INITIALIZED,
                          PAL_UART_Reconfigure(USART1, &config));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_AttachInitialized(USART1));
}


static void test_modified_failure_recovery_and_owned_release(void)
{
    uint8_t byte = 1;
    size_t count = 99;
    CompletionRecord record = {0};
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Init(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK,
        PAL_UART_Host_SetNextModifiedReconfigureResult(USART1, PAL_UART_STATUS_IO_ERROR));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_IO_ERROR, PAL_UART_Reconfigure(USART1, &config));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_INVALID_STATE,
        PAL_UART_Write(USART1, &byte, 1, 0, &count));
    TEST_ASSERT_EQUAL_size_t(0, count);
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_INVALID_STATE,
        PAL_UART_StartTx(USART1, PAL_UART_ASYNC_IRQ, &byte, 1, completion, &record));
    TEST_ASSERT_EQUAL_UINT(0, record.calls);
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_INVALID_STATE, PAL_UART_Detach(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK,
        PAL_UART_Host_SetNextReconfigureResult(USART1, PAL_UART_STATUS_IO_ERROR));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_IO_ERROR, PAL_UART_Reconfigure(USART1, &config));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_INVALID_STATE,
        PAL_UART_Read(USART1, &byte, 1, 0, &count));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Reconfigure(USART1, &config));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK,
        PAL_UART_Write(USART1, &byte, 1, 0, &count));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK,
        PAL_UART_Host_SetNextModifiedReconfigureResult(USART1, PAL_UART_STATUS_IO_ERROR));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_IO_ERROR, PAL_UART_Reconfigure(USART1, &config));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_DeInit(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Init(USART1));
}

static void test_modified_failure_preserves_attached_ownership_and_hardware(void)
{
    uint8_t byte = 1;
    size_t count = 0;
    PalUartConfig tx_only = config;
    tx_only.directions = PAL_UART_DIR_TX;
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Host_SetExternalReady(USART1, 1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_AttachInitialized(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK,
        PAL_UART_Host_SetNextModifiedReconfigureResult(USART1, PAL_UART_STATUS_IO_ERROR));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_IO_ERROR, PAL_UART_Reconfigure(USART1, &tx_only));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_INVALID_STATE, PAL_UART_DeInit(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Detach(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_AttachInitialized(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_UNSUPPORTED,
        PAL_UART_Read(USART1, &byte, 1, 0, &count));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK,
        PAL_UART_Write(USART1, &byte, 1, 0, &count));
}

static void test_opposite_direction_blocking_io_and_partial_counts(void)
{
    uint8_t tx = 7, rx = 0, incoming = 8;
    size_t count = 99;
    CompletionRecord tx_record = {0}, rx_record = {0};
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Init(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK,
        PAL_UART_StartTx(USART1, PAL_UART_ASYNC_IRQ, &tx, 1, completion, &tx_record));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_TIMEOUT, PAL_UART_Read(USART1, &rx, 1, 0, &count));
    TEST_ASSERT_EQUAL_size_t(0, count);
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_BUSY, PAL_UART_DeInit(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Cancel(USART1, PAL_UART_DIR_TX));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK,
        PAL_UART_StartRx(USART1, PAL_UART_ASYNC_IRQ, &rx, 1, completion, &rx_record));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Write(USART1, &tx, 1, 0, &count));
    TEST_ASSERT_EQUAL_size_t(1, count);
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_BUSY, PAL_UART_Read(USART1, &rx, 1, 0, &count));
    TEST_ASSERT_EQUAL_size_t(0, count);
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_BUSY, PAL_UART_Reconfigure(USART1, &config));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Host_FeedRx(USART1, &incoming, 1));
    TEST_ASSERT_EQUAL_UINT(1, rx_record.calls);
    TEST_ASSERT_EQUAL_UINT8(8, rx);
    /* An ordinary read timeout did not invalidate the device. */
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Write(USART1, &tx, 1, 0, &count));
}

static void test_inline_completion_can_rearm_request(void)
{
    uint8_t incoming = 42, received = 0;
    CompletionRecord record = {.rearm_rx = 1};
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Init(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Host_FeedRx(USART1, &incoming, 1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK,
        PAL_UART_StartRx(USART1, PAL_UART_ASYNC_IRQ, &received, 1, completion, &record));
    TEST_ASSERT_EQUAL_UINT(1, record.calls);
    TEST_ASSERT_EQUAL_UINT8(42, received);
    TEST_ASSERT_EQUAL_UINT(PAL_UART_ERROR_NONE, record.errors);
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_BUSY, PAL_UART_DeInit(USART1));
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_Cancel(USART1, PAL_UART_DIR_RX));
    TEST_ASSERT_EQUAL_UINT(2, record.calls);
    TEST_ASSERT_EQUAL_INT(PAL_UART_TRANSFER_CANCELLED, record.result);
    TEST_ASSERT_EQUAL_INT(PAL_UART_STATUS_OK, PAL_UART_DeInit(USART1));
}

static const TestCase cases[] = {
    TEST_CASE(test_inline_completion_can_rearm_request),
    TEST_CASE(test_modified_failure_recovery_and_owned_release),
    TEST_CASE(test_modified_failure_preserves_attached_ownership_and_hardware),
    TEST_CASE(test_opposite_direction_blocking_io_and_partial_counts),

    TEST_CASE(test_sparse_ids_and_configured_directions),
    TEST_CASE(test_init_validation_and_ownership),
    TEST_CASE(test_blocking_partial_read_and_write),
    TEST_CASE(test_async_mode_busy_completion_and_cancel),
    TEST_CASE(test_async_tx_error_and_buffer_lifetime),
    TEST_CASE(test_driver_start_rejection_does_not_complete),
    TEST_CASE(test_reconfigure_directions_and_preserve_on_rejection),
    TEST_CASE(test_reconfigure_busy_driver_failure_and_attached_handle),
    TEST_CASE(test_release_requires_matching_initialization_path),
    TEST_CASE(test_detach_waits_for_transfers_and_blocks_access_after_release)
};

int main(int argc, char **argv)
{
    return run_test_cases(argc, argv, __FILE__, cases, sizeof cases / sizeof cases[0]);
}
