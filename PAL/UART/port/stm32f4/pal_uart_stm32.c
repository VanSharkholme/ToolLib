#include "pal_uart_port.h"
#include "bsp_uart.h"
#include "usart.h"
#include "pal_uart_stm32.h"

/* PAL owns the request lifecycle; the Port tracks hardware cleanup.
 * HAL callbacks record events -> Complete marks pending -> Dispatch calls AsyncFinish when safe. */
typedef struct
{
    PalUartErrors errors;         /* Accumulated errors, separate from the end reason. */
    PalUartTransferResult result; /* Pending result; a later error may upgrade an undelivered success. */
    size_t transferred;           /* Progress, including a snapshot taken before DMA cleanup. */
    bool ending;                  /* Cleanup has started; hardware may still be stopping. */
    bool complete;                /* Completion is pending; notification still requires a safe boundary. */
    bool count_saved;         /* DMA progress is saved, or normal completion established the full count. */
} Stm32UartRequest;

typedef struct
{
    PalUartDevice pal_device;
    PalUartId id;
    UART_HandleTypeDef* huart;
    Stm32UartRequest tx;
    Stm32UartRequest rx;
    uint8_t* dma_rx_buffer;
    unsigned hal_depth; /* Active HAL scopes: Start, Cancel, error callbacks and IRQ handlers. */
    bool dispatching;   /* Prevent recursive dispatch from user callbacks; not a lock. */
} Stm32Uart;

/* PAL owns pal.state. This table only binds physical resources and storage. */
static Stm32Uart devices[BSP_UART_NUM] = {
    [PAL_UART1] = {
        .pal_device.port_context = &devices[PAL_UART1],
        .id = PAL_UART1,
        .huart = &huart1,
    },
    [PAL_UART2] = {
        .pal_device.port_context = &devices[PAL_UART2],
        .id = PAL_UART2,
        .huart = &huart2,
    },
    [PAL_UART3] = {
        .pal_device.port_context = &devices[PAL_UART3],
        .id = PAL_UART3,
        .huart = &huart6,
    },
};


static Stm32Uart* PAL_UART_STM32_FindByHalHandle(UART_HandleTypeDef* huart)
{
    if (huart == NULL) return NULL;

    for (size_t i = 0; i < BSP_UART_NUM; ++i)
        if (devices[i].huart == huart) return &devices[i];
    return NULL;
}

/* Select the PAL transfer, Port request and DMA handle for one direction. */
static PalUartAsyncTransfer* PAL_UART_STM32_Get_Transfer(Stm32Uart* device, PalUartDirection direction)
{
    return direction == PAL_UART_DIR_TX? &device->pal_device.tx: &device->pal_device.rx;
}

static Stm32UartRequest* PAL_UART_STM32_Get_Request(Stm32Uart* device, PalUartDirection direction)
{
    return direction == PAL_UART_DIR_TX? &device->tx: &device->rx;
}

static DMA_HandleTypeDef* PAL_UART_STM32_Get_DMA_Handle(Stm32Uart* device, PalUartDirection direction)
{
    return direction == PAL_UART_DIR_TX? device->huart->hdmatx: device->huart->hdmarx;
}

/* F401 reports PE, FE, NE and ORE through SR. */
static PalUartErrors PAL_UART_STM32_Convert_Errors(uint32_t code)
{
    PalUartErrors errors = PAL_UART_ERROR_NONE;
    if (code & HAL_UART_ERROR_PE) errors |= PAL_UART_ERROR_PARITY;
    if (code & HAL_UART_ERROR_FE) errors |= PAL_UART_ERROR_FRAMING;
    if (code & HAL_UART_ERROR_NE) errors |= PAL_UART_ERROR_NOISE;
    if (code & HAL_UART_ERROR_ORE) errors |= PAL_UART_ERROR_OVERRUN;
    if (code & HAL_UART_ERROR_DMA) errors |= PAL_UART_ERROR_DMA;
    if (code & ~(uint32_t)(
        HAL_UART_ERROR_PE | HAL_UART_ERROR_FE | HAL_UART_ERROR_NE |
        HAL_UART_ERROR_ORE | HAL_UART_ERROR_DMA
    ))
        errors |= PAL_UART_ERROR_OTHER;
    return errors;
}

/* WITH_ERROR denotes parity/framing/noise errors in addition to the end reason. */
static bool PAL_UART_STM32_Is_DataError(PalUartErrors errors)
{
    return (errors & (PAL_UART_ERROR_PARITY | PAL_UART_ERROR_FRAMING | PAL_UART_ERROR_NOISE)) != 0;
}

/* Priority: DMA controller error/ORE > other DMA RX errors > full reception.
 * PE/FE/NE can leave IRQ RX running, but this HAL terminates DMA RX on these errors. */
static PalUartTransferResult PAL_UART_STM32_Determine_RxResult(Stm32Uart* device, uint32_t code)
{
    bool data_error = PAL_UART_STM32_Is_DataError(device->rx.errors);
    if ((code & (HAL_UART_ERROR_DMA | HAL_UART_ERROR_ORE)) != 0)
        return data_error? PAL_UART_TRANSFER_ABORTED_WITH_ERROR: PAL_UART_TRANSFER_ABORTED;
    if (device->pal_device.rx.mode == PAL_UART_ASYNC_DMA && code != HAL_UART_ERROR_NONE)
        return data_error? PAL_UART_TRANSFER_ABORTED_WITH_ERROR: PAL_UART_TRANSFER_ABORTED;
    return device->rx.errors != 0? PAL_UART_TRANSFER_FINISHED_WITH_ERROR: PAL_UART_TRANSFER_OK;
}

/* IRQ uses UART remaining counts; DMA uses its counter or a saved snapshot.
 * UART counts do not track DMA progress. Partial TX counts do not guarantee wire completion. */
static size_t PAL_UART_STM32_Get_Count(Stm32Uart* device, PalUartDirection direction)
{
    PalUartAsyncTransfer* transfer = PAL_UART_STM32_Get_Transfer(device, direction);
    Stm32UartRequest* request = PAL_UART_STM32_Get_Request(device, direction);
    if (request->count_saved) return request->transferred;
    size_t remaining;
    if (transfer->mode == PAL_UART_ASYNC_DMA)
    {
        DMA_HandleTypeDef* dma = PAL_UART_STM32_Get_DMA_Handle(device, direction);
        if (dma == NULL) return 0;
        remaining = __HAL_DMA_GET_COUNTER(dma);
    }
    else
        remaining = direction == PAL_UART_DIR_TX? device->huart->TxXferCount: device->huart->RxXferCount;
    return remaining <= transfer->length? transfer->length - remaining: 0;
}

static bool PAL_UART_STM32_Stopped(Stm32Uart* device, PalUartDirection direction)
{
    UART_HandleTypeDef* huart = device->huart;
    if ((direction == PAL_UART_DIR_TX? huart->gState: huart->RxState) != HAL_UART_STATE_READY)
        return false;
    if (PAL_UART_STM32_Get_Transfer(device, direction)->mode != PAL_UART_ASYNC_DMA) return true;
    DMA_HandleTypeDef* dma = PAL_UART_STM32_Get_DMA_Handle(device, direction);
    /* UART READY alone does not prove DMA stopped; verify the channel before releasing the buffer. */
    return dma != NULL && dma->State == HAL_DMA_STATE_READY &&
        (dma->Instance->CR & DMA_SxCR_EN) == 0;
}

static void PAL_UART_STM32_Dispatch(Stm32Uart* device)
{
    /* HAL may touch the old request after its callback; wait for outer scopes and hardware cleanup. */
    if (device->hal_depth != 0 || device->dispatching) return;
    device->dispatching = true;
    bool delivered;
    do
    {
        delivered = false;
        for (unsigned i = 0; i < 2; ++i)
        {
            PalUartDirection direction = i == 0? PAL_UART_DIR_TX: PAL_UART_DIR_RX;
            Stm32UartRequest* request = PAL_UART_STM32_Get_Request(device, direction);
            if (!request->complete || !PAL_UART_STM32_Stopped(device, direction)) continue;
            Stm32UartRequest completed = *request;
            bool data_error = PAL_UART_STM32_Is_DataError(completed.errors);
            if (completed.result == PAL_UART_TRANSFER_OK && completed.errors != 0)
                completed.result = PAL_UART_TRANSFER_FINISHED_WITH_ERROR;
            if (completed.result == PAL_UART_TRANSFER_ABORTED && data_error)
                completed.result = PAL_UART_TRANSFER_ABORTED_WITH_ERROR;
            if (direction == PAL_UART_DIR_RX && device->pal_device.rx.mode == PAL_UART_ASYNC_DMA &&
                device->pal_device.state.config.data_bits == PAL_UART_DATA_7BITS)
            {
                /* DMA copies DR directly, including the parity bit in bit 7. */
                for (size_t n = 0; n < completed.transferred; ++n) device->dma_rx_buffer[n] &= 0x7f;
            }
            /* Clear Port state first; AsyncFinish clears PAL state before user code can resubmit. */
            *request = (Stm32UartRequest){0};
            delivered = true;
            PAL_UART_AsyncFinish(device->id, direction, completed.result, completed.transferred, completed.errors);
        }
    }
    while (delivered); /* Drain new synchronous completions produced by user callbacks. */
    device->dispatching = false;
}

/* Pair with ++hal_depth on both success and failure; leave the scope and try dispatching.
 * This does not wait for transfer completion. Zero depth alone does not trigger a callback. */
static void PAL_UART_STM32_LeaveHal(Stm32Uart* device)
{
    if (device->hal_depth != 0) --device->hal_depth;
    PAL_UART_STM32_Dispatch(device);
}

/* Prepare completion; Dispatch checks nesting depth and hardware state before notification. */
static void PAL_UART_STM32_Complete(Stm32Uart* device, PalUartDirection direction)
{
    Stm32UartRequest* request = PAL_UART_STM32_Get_Request(device, direction);
    request->transferred = PAL_UART_STM32_Get_Count(device, direction);
    request->complete = true;
    PAL_UART_STM32_Dispatch(device);
}

/* Reject unsupported DMA setups before starting: normal mode, byte width and single bursts only. */
static bool PAL_UART_STM32_CheckDma(DMA_HandleTypeDef* dma, PalUartDirection direction)
{
    if (dma == NULL || dma->Instance == NULL) return false;
    return dma->Init.Mode == DMA_NORMAL && !(dma->Instance->CR & DMA_SxCR_DBM) &&
        dma->Init.Direction == (direction == PAL_UART_DIR_TX ? DMA_MEMORY_TO_PERIPH : DMA_PERIPH_TO_MEMORY) &&
        dma->Init.PeriphDataAlignment == DMA_PDATAALIGN_BYTE && dma->Init.MemDataAlignment == DMA_MDATAALIGN_BYTE &&
        dma->Init.PeriphInc == DMA_PINC_DISABLE && dma->Init.MemInc == DMA_MINC_ENABLE &&
        dma->Init.FIFOMode == DMA_FIFOMODE_DISABLE;
}

static PalUartStatus PAL_UART_STM32_BuildConfig(
    const USART_TypeDef* instance, const UART_InitTypeDef* base,
    const PalUartConfig* config,
    UART_InitTypeDef* out
)
{
    if (config == NULL || config->baud_rate == 0)
        return PAL_UART_STATUS_INVALID_ARGUMENT;
    uint32_t clock = instance == USART1 || instance == USART6 ? HAL_RCC_GetPCLK2Freq() : HAL_RCC_GetPCLK1Freq();
    unsigned sample = base->OverSampling == UART_OVERSAMPLING_8 ? 8u : 16u;
    /* A valid BRR has a nonzero 12-bit mantissa. Test the range before HAL
     * truncates its divider into the 16-bit register. */
    if (!IS_UART_BAUDRATE(config->baud_rate) || config->baud_rate > clock / sample ||
        (uint64_t)config->baud_rate * (4096u * sample - 1u) < clock)
        return PAL_UART_STATUS_UNSUPPORTED;
    UART_InitTypeDef candidate = *base;

    uint32_t baud_rate = config->baud_rate;
    uint32_t word_length = 0;
    uint32_t stop_bits = 0;
    uint32_t parity = 0;
    uint32_t mode = 0;

    // Check parity
    switch (config->parity)
    {
    case PAL_UART_PARITY_NONE:
        parity = UART_PARITY_NONE;
        break;
    case PAL_UART_PARITY_EVEN:
        parity = UART_PARITY_EVEN;
        break;
    case PAL_UART_PARITY_ODD:
        parity = UART_PARITY_ODD;
        break;
    default:
        return PAL_UART_STATUS_INVALID_ARGUMENT;
    }

    // Check word length
    if (parity == UART_PARITY_NONE)
    {
        switch (config->data_bits)
        {
        case PAL_UART_DATA_8BITS:
            word_length = UART_WORDLENGTH_8B;
            break;
        case PAL_UART_DATA_7BITS:
            return PAL_UART_STATUS_UNSUPPORTED;
        default:
            return PAL_UART_STATUS_INVALID_ARGUMENT;
        }
    }
    else // STM32 USART wordlength includes parity bit
    {
        switch (config->data_bits)
        {
        case PAL_UART_DATA_8BITS:
            word_length = UART_WORDLENGTH_9B;
            break;
        case PAL_UART_DATA_7BITS:
            word_length = UART_WORDLENGTH_8B;
            break;
        default:
            return PAL_UART_STATUS_INVALID_ARGUMENT;
        }
    }

    // Check stop bits
    switch (config->stop_bits)
    {
    case PAL_UART_STOP_1:
        stop_bits = UART_STOPBITS_1;
        break;
    case PAL_UART_STOP_2:
        stop_bits = UART_STOPBITS_2;
        break;
    default:
        return PAL_UART_STATUS_INVALID_ARGUMENT;
    }

    // Check direction
    switch (config->directions)
    {
    case PAL_UART_DIR_TX:
        mode = UART_MODE_TX;
        break;
    case PAL_UART_DIR_RX:
        mode = UART_MODE_RX;
        break;
    case PAL_UART_DIR_TXRX:
        mode = UART_MODE_TX_RX;
        break;
    default:
        return PAL_UART_STATUS_INVALID_ARGUMENT;
    }

    candidate.BaudRate = baud_rate;
    candidate.WordLength = word_length;
    candidate.StopBits = stop_bits;
    candidate.Parity = parity;
    candidate.Mode = mode;
    *out = candidate;
    return PAL_UART_STATUS_OK;
}

static PalUartStatus PAL_UART_STM32_Convert_Status(HAL_StatusTypeDef status)
{
    switch (status)
    {
    case HAL_OK: return PAL_UART_STATUS_OK;
    case HAL_BUSY: return PAL_UART_STATUS_BUSY;
    case HAL_TIMEOUT: return PAL_UART_STATUS_TIMEOUT;
    default: return PAL_UART_STATUS_PORT_ERROR;
    }
}

PalUartDevice* PAL_UART_Port_Find(PalUartId id)
{
    if ((unsigned)id >= BSP_UART_NUM || devices[id].huart == NULL) return NULL;
    return &devices[id].pal_device;
}

PalUartStatus PAL_UART_Port_CheckIdle(void* port_context, PalUartDirection directions)
{
    const Stm32Uart* device = port_context;
    const UART_HandleTypeDef* huart = device->huart;
    /* HAL's general BUSY can remain after failed configuration. Only actual
     * transfers block recovery through Reconfigure or DeInit. */
    if (((directions & PAL_UART_DIR_TX) && huart->gState == HAL_UART_STATE_BUSY_TX) ||
        ((directions & PAL_UART_DIR_RX) && huart->RxState == HAL_UART_STATE_BUSY_RX))
        return PAL_UART_STATUS_BUSY;
    return PAL_UART_STATUS_OK;
}

PalUartStatus PAL_UART_Port_Init(void* port_context, PalUartConfig* applied)
{
    Stm32Uart* device = port_context;
    UART_HandleTypeDef* huart = device->huart;
    if (huart->gState != HAL_UART_STATE_RESET || huart->RxState != HAL_UART_STATE_RESET)
        return PAL_UART_STATUS_BUSY;

    USART_TypeDef* instance = BSP_UART_Get_Instance(device->id);
    const UART_InitTypeDef* defaults = BSP_UART_Get_Init(device->id);
    const PalUartConfig* config = BSP_UART_Get_Config(device->id);
    if (instance == NULL || defaults == NULL || config == NULL)
        return PAL_UART_STATUS_PORT_ERROR;
    UART_InitTypeDef candidate;
    PalUartStatus status = PAL_UART_STM32_BuildConfig(instance, defaults, config, &candidate);
    if (status != PAL_UART_STATUS_OK) return status;

    huart->Instance = instance;
    huart->Init = candidate;
    status = PAL_UART_STM32_Convert_Status(HAL_UART_Init(huart));
    if (status != PAL_UART_STATUS_OK)
    {
        /* A mapped, valid handle lets HAL tear down partial MSP/register setup. */
        if (HAL_UART_DeInit(huart) != HAL_OK) return PAL_UART_STATUS_PORT_ERROR;
        return status;
    }
    *applied = *config;
    return PAL_UART_STATUS_OK;
}

PalUartStatus PAL_UART_Port_AttachInitialized(void* port_context, PalUartConfig* current)
{
    const Stm32Uart* device = port_context;
    const UART_HandleTypeDef* huart = device->huart;
    if (huart->Instance == NULL ||
        (huart->gState == HAL_UART_STATE_RESET && huart->RxState == HAL_UART_STATE_RESET))
        return PAL_UART_STATUS_NOT_INITIALIZED;
    PalUartStatus status = PAL_UART_Port_CheckIdle(port_context, PAL_UART_DIR_TXRX);
    if (status != PAL_UART_STATUS_OK) return status;
    if (huart->gState != HAL_UART_STATE_READY || huart->RxState != HAL_UART_STATE_READY)
        return PAL_UART_STATUS_PORT_ERROR;
    if ((huart->Instance->CR1 & USART_CR1_UE) == 0)
        return PAL_UART_STATUS_NOT_INITIALIZED;
    if ((huart->Instance->CR2 & (USART_CR2_LINEN | USART_CR2_CLKEN)) != 0 ||
        (huart->Instance->CR3 & (USART_CR3_SCEN | USART_CR3_HDSEL | USART_CR3_IREN)) != 0)
        return PAL_UART_STATUS_UNSUPPORTED;

    PalUartConfig config = {.baud_rate = huart->Init.BaudRate};
    switch (huart->Init.Parity)
    {
    case UART_PARITY_NONE: config.parity = PAL_UART_PARITY_NONE;
        break;
    case UART_PARITY_EVEN: config.parity = PAL_UART_PARITY_EVEN;
        break;
    case UART_PARITY_ODD: config.parity = PAL_UART_PARITY_ODD;
        break;
    default: return PAL_UART_STATUS_UNSUPPORTED;
    }
    unsigned bits;
    switch (huart->Init.WordLength)
    {
    case UART_WORDLENGTH_8B: bits = 8;
        break;
    case UART_WORDLENGTH_9B: bits = 9;
        break;
    default: return PAL_UART_STATUS_UNSUPPORTED;
    }
    if (config.parity != PAL_UART_PARITY_NONE) --bits;
    if (bits != 7 && bits != 8) return PAL_UART_STATUS_UNSUPPORTED;
    config.data_bits = (PalUartDataBits)bits;
    switch (huart->Init.StopBits)
    {
    case UART_STOPBITS_1: config.stop_bits = PAL_UART_STOP_1;
        break;
    case UART_STOPBITS_2: config.stop_bits = PAL_UART_STOP_2;
        break;
    default: return PAL_UART_STATUS_UNSUPPORTED;
    }
    switch (huart->Init.Mode)
    {
    case UART_MODE_TX: config.directions = PAL_UART_DIR_TX;
        break;
    case UART_MODE_RX: config.directions = PAL_UART_DIR_RX;
        break;
    case UART_MODE_TX_RX: config.directions = PAL_UART_DIR_TXRX;
        break;
    default: return PAL_UART_STATUS_UNSUPPORTED;
    }
    if (config.baud_rate == 0) return PAL_UART_STATUS_UNSUPPORTED;
    *current = config;
    return PAL_UART_STATUS_OK;
}

PalUartStatus PAL_UART_Port_DeInit(void* port_context)
{
    Stm32Uart* device = port_context;
    return PAL_UART_STM32_Convert_Status(HAL_UART_DeInit(device->huart));
}

PalUartStatus PAL_UART_Port_Reconfigure(
    void* port_context,
    const PalUartConfig* config, bool* modified
)
{
    Stm32Uart* device = port_context;
    UART_HandleTypeDef* huart = device->huart;
    *modified = false;
    UART_InitTypeDef candidate;
    PalUartStatus status = PAL_UART_STM32_BuildConfig(huart->Instance, &huart->Init, config, &candidate);
    if (status != PAL_UART_STATUS_OK) return status;
    *modified = true;
    huart->Init = candidate;
    return PAL_UART_STM32_Convert_Status(HAL_UART_Init(huart));
}

PalUartStatus PAL_UART_Port_Write(
    void* port_context, const uint8_t* data, size_t length,
    uint32_t timeout_ms, size_t* transferred
)
{
    Stm32Uart* device = port_context;
    UART_HandleTypeDef* huart = device->huart;
    if (length > UINT16_MAX) return PAL_UART_STATUS_UNSUPPORTED;
    HAL_StatusTypeDef status = HAL_UART_Transmit(huart, data, (uint16_t)length, timeout_ms);
    if (status == HAL_OK || status == HAL_TIMEOUT)
        *transferred = length - huart->TxXferCount;
    return PAL_UART_STM32_Convert_Status(status);
}

PalUartStatus PAL_UART_Port_Read(
    void* port_context, bool to_idle, uint8_t* data, size_t length,
    uint32_t timeout_ms, size_t* transferred
)
{
    Stm32Uart* device = port_context;
    UART_HandleTypeDef* huart = device->huart;
    if (length > UINT16_MAX) return PAL_UART_STATUS_UNSUPPORTED;
    if (to_idle)
    {
        uint16_t RxLen = 0;
        HAL_StatusTypeDef status = HAL_UARTEx_ReceiveToIdle(huart, data, (uint16_t)length, &RxLen, timeout_ms);
        *transferred = RxLen;
        return PAL_UART_STM32_Convert_Status(status);
    }
    HAL_StatusTypeDef status = HAL_UART_Receive(huart, data, (uint16_t)length, timeout_ms);
    if (status == HAL_OK || status == HAL_TIMEOUT)
        *transferred = length - huart->RxXferCount;
    if (status == HAL_TIMEOUT && (huart->ErrorCode & HAL_UART_ERROR_ORE)) return PAL_UART_STATUS_IO_ERROR;
    return PAL_UART_STM32_Convert_Status(status);
}

static PalUartStatus CheckAsync(Stm32Uart* device, PalUartDirection direction, PalUartAsyncMode mode, size_t length)
{
    if (length > UINT16_MAX) return PAL_UART_STATUS_UNSUPPORTED;
    PalUartAsyncSupport support = direction == PAL_UART_DIR_TX ? BSP_UART_Get_TxAsyncSupport(device->id)
                                                               : BSP_UART_Get_RxAsyncSupport(device->id);
    if (mode == PAL_UART_ASYNC_IRQ)
        return (support & PAL_UART_ASYNC_SUPPORT_IRQ) ? PAL_UART_STATUS_OK : PAL_UART_STATUS_UNSUPPORTED;
    if (mode != PAL_UART_ASYNC_DMA || !(support & PAL_UART_ASYNC_SUPPORT_DMA))
        return PAL_UART_STATUS_UNSUPPORTED;
    DMA_HandleTypeDef* dma = PAL_UART_STM32_Get_DMA_Handle(device, direction);
    if (!PAL_UART_STM32_CheckDma(dma, direction) || dma->Parent != device->huart)
        return PAL_UART_STATUS_UNSUPPORTED;
    if (dma->State != HAL_DMA_STATE_READY || (dma->Instance->CR & DMA_SxCR_EN)) return PAL_UART_STATUS_BUSY;
    return PAL_UART_STATUS_OK;
}

PalUartStatus PAL_UART_Port_StartTx(void* context, PalUartAsyncMode mode, const uint8_t* data, size_t length)
{
    Stm32Uart* device = context;
    PalUartStatus status = CheckAsync(device, PAL_UART_DIR_TX, mode, length);
    if (status != PAL_UART_STATUS_OK) return status;
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    ++device->hal_depth;
    device->tx = (Stm32UartRequest){0};
    HAL_StatusTypeDef hal = mode == PAL_UART_ASYNC_DMA
        ? HAL_UART_Transmit_DMA(device->huart, data, (uint16_t)length)
        : HAL_UART_Transmit_IT(device->huart, data, (uint16_t)length);
    __set_PRIMASK(mask);
    PAL_UART_STM32_LeaveHal(device);
    return PAL_UART_STM32_Convert_Status(hal);
}

PalUartStatus PAL_UART_Port_StartRx(void* context, PalUartAsyncMode mode, uint8_t* data, size_t length, bool to_idle)
{
    Stm32Uart* device = context;
    PalUartStatus status = CheckAsync(device, PAL_UART_DIR_RX, mode, length);
    if (status != PAL_UART_STATUS_OK) return status;
    UART_HandleTypeDef* huart = device->huart;
    uint16_t size = (uint16_t)length;
    /* ReceiveToIdle enables the receiver before finishing its IDLE interrupt setup. */
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    ++device->hal_depth;
    device->rx = (Stm32UartRequest){0};
    device->dma_rx_buffer = mode == PAL_UART_ASYNC_DMA ? data : NULL;
    HAL_StatusTypeDef hal;
    if (mode == PAL_UART_ASYNC_DMA)
        hal = to_idle ? HAL_UARTEx_ReceiveToIdle_DMA(huart, data, size) : HAL_UART_Receive_DMA(huart, data, size);
    else
        hal = to_idle ? HAL_UARTEx_ReceiveToIdle_IT(huart, data, size) : HAL_UART_Receive_IT(huart, data, size);
    __set_PRIMASK(mask);
    PAL_UART_STM32_LeaveHal(device);
    return PAL_UART_STM32_Convert_Status(hal);
}

PalUartStatus PAL_UART_Port_Cancel(void* port_context, PalUartDirection direction)
{
    Stm32Uart* device = port_context;
    Stm32UartRequest* request = PAL_UART_STM32_Get_Request(device, direction);
    /* HAL may invoke AbortCplt synchronously, so save the cancellation reason first. */
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if (request->ending || PAL_UART_STM32_Get_Transfer(device, direction)->state != PAL_UART_ASYNC_CANCEL_PENDING)
    {
        __set_PRIMASK(mask);
        return PAL_UART_STATUS_BUSY;
    }
    request->transferred = PAL_UART_STM32_Get_Count(device, direction);
    request->count_saved = true;
    request->ending = true;
    request->result = PAL_UART_TRANSFER_CANCELLED;
    ++device->hal_depth;
    HAL_StatusTypeDef status = direction == PAL_UART_DIR_TX
                                   ? HAL_UART_AbortTransmit_IT(device->huart)
                                   : HAL_UART_AbortReceive_IT(device->huart);
    if (status != HAL_OK)
    {
        request->ending = false;
        request->count_saved = false;
    }
    __set_PRIMASK(mask);
    PAL_UART_STM32_LeaveHal(device);
    return PAL_UART_STM32_Convert_Status(status);
}

/******************************************************************************
 * Record events first; notify completion after hardware stops and HAL scopes exit.
 ******************************************************************************/
void HAL_UART_TxCpltCallback(UART_HandleTypeDef* huart)
{
    Stm32Uart* device = PAL_UART_STM32_FindByHalHandle(huart);
    if (device == NULL || device->pal_device.tx.state == PAL_UART_ASYNC_IDLE ||
        huart->gState != HAL_UART_STATE_READY || device->tx.ending)
        return;
    /* Normal DMA completion only arms UART TC; this callback reports full transmission. */
    device->tx.ending = true;
    device->tx.result = PAL_UART_TRANSFER_OK;
    device->tx.transferred = device->pal_device.tx.length;
    device->tx.count_saved = true;
    PAL_UART_STM32_Complete(device, PAL_UART_DIR_TX);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef* huart)
{
    Stm32Uart* device = PAL_UART_STM32_FindByHalHandle(huart);
    if (device == NULL || device->pal_device.rx.state == PAL_UART_ASYNC_IDLE ||
        huart->RxState != HAL_UART_STATE_READY || device->rx.ending)
        return;
    /* A faulty last byte may trigger RxCplt before ErrorCallback; include the current errors.
     * TX/RX share ErrorCode. Handle DMA faults separately to avoid poisoning IRQ RX. */
    uint32_t code = huart->ErrorCode & ~(uint32_t)HAL_UART_ERROR_DMA;
    device->rx.errors |= PAL_UART_STM32_Convert_Errors(code);
    device->rx.ending = true;
    device->rx.result = PAL_UART_STM32_Determine_RxResult(device, code);
    device->rx.transferred = device->pal_device.rx.length;
    device->rx.count_saved = true;
    PAL_UART_STM32_Complete(device, PAL_UART_DIR_RX);
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef* huart, uint16_t Size)
{
    Stm32Uart* device = PAL_UART_STM32_FindByHalHandle(huart);
    HAL_UART_RxEventTypeTypeDef event = HAL_UARTEx_GetRxEventType(huart);
    /* This one-shot API ignores half-transfer progress; only IDLE/TC end RX. */
    if (event == HAL_UART_RXEVENT_HT)
        return;
    if (device == NULL || device->pal_device.rx.state == PAL_UART_ASYNC_IDLE ||
        huart->RxState != HAL_UART_STATE_READY || device->rx.ending || Size == 0)
        return;
    /* IDLE/TC may reach RxEvent before ErrorCallback; include the current errors.
     * TX/RX share ErrorCode. Handle DMA faults separately to avoid poisoning IRQ RX. */
    uint32_t code = huart->ErrorCode & ~(uint32_t)HAL_UART_ERROR_DMA;
    device->rx.errors |= PAL_UART_STM32_Convert_Errors(code);
    device->rx.ending = true;
    device->rx.result = PAL_UART_STM32_Determine_RxResult(device, code);
    if (device->pal_device.rx.mode == PAL_UART_ASYNC_DMA && huart->hdmarx->State == HAL_DMA_STATE_TIMEOUT)
    {
        /* The F4 IDLE handler ignores the return from HAL_DMA_Abort. That
         * function already requested stream disable and masked all its IRQs.
         * Finish its cleanup, or let disable-complete raise TC when EN falls. */
        DMA_HandleTypeDef* dma = huart->hdmarx;
        device->rx.errors |= PAL_UART_ERROR_DMA;
        device->rx.result = PAL_UART_STM32_Is_DataError(device->rx.errors)
                                ? PAL_UART_TRANSFER_ABORTED_WITH_ERROR : PAL_UART_TRANSFER_ABORTED;
        if (dma->Instance->CR & DMA_SxCR_EN)
        {
            dma->XferAbortCallback = NULL;
            dma->State = HAL_DMA_STATE_ABORT;
            __HAL_DMA_ENABLE_IT(dma, DMA_IT_TC);
        }
        else
        {
            __HAL_DMA_CLEAR_FLAG(dma, __HAL_DMA_GET_TC_FLAG_INDEX(dma) | __HAL_DMA_GET_HT_FLAG_INDEX(dma) |
                                      __HAL_DMA_GET_TE_FLAG_INDEX(dma) | __HAL_DMA_GET_DME_FLAG_INDEX(dma) |
                                      __HAL_DMA_GET_FE_FLAG_INDEX(dma));
            dma->State = HAL_DMA_STATE_READY;
        }
    }
    device->rx.transferred = Size;
    /* Preserve HAL's actual count after normal DMA was stopped for IDLE/TC. */
    device->rx.count_saved = true;
    PAL_UART_STM32_Complete(device, PAL_UART_DIR_RX);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef* huart)
{
    Stm32Uart* device = PAL_UART_STM32_FindByHalHandle(huart);
    if (device == NULL) return;
    uint32_t code = huart->ErrorCode;
    if (code == HAL_UART_ERROR_NONE) return;
    /* Abort can call back synchronously; finish processing both directions before dispatch. */
    ++device->hal_depth;
    if ((code & HAL_UART_ERROR_DMA) != 0)
    {
        /* This HAL ends both UART DMA states without ensuring the peer DMA has stopped.
         * Conservatively terminate active DMA requests; leave an IRQ-mode peer running. */
        for (unsigned i = 0; i < 2; ++i)
        {
            PalUartDirection direction = i == 0? PAL_UART_DIR_TX: PAL_UART_DIR_RX;
            PalUartAsyncTransfer* transfer = PAL_UART_STM32_Get_Transfer(device, direction);
            Stm32UartRequest* request = PAL_UART_STM32_Get_Request(device, direction);
            if (transfer->state == PAL_UART_ASYNC_IDLE || transfer->mode != PAL_UART_ASYNC_DMA) continue;
            request->errors |= PAL_UART_ERROR_DMA;
            if (direction == PAL_UART_DIR_RX)
                request->errors |= PAL_UART_STM32_Convert_Errors(code);
            /* Upgrade an undelivered success if the same IRQ reports an error; retain cancellation. */
            bool provisional_success = request->complete &&
                (request->result == PAL_UART_TRANSFER_OK || request->result == PAL_UART_TRANSFER_FINISHED_WITH_ERROR);
            if (!request->ending || provisional_success)
            {
                request->ending = true;
                request->complete = false;
                request->result = PAL_UART_STM32_Is_DataError(request->errors)
                                      ? PAL_UART_TRANSFER_ABORTED_WITH_ERROR
                                      : PAL_UART_TRANSFER_ABORTED;
                HAL_StatusTypeDef status = direction == PAL_UART_DIR_TX
                                               ? HAL_UART_AbortTransmit_IT(huart)
                                               : HAL_UART_AbortReceive_IT(huart);
                if (status != HAL_OK) request->errors |= PAL_UART_ERROR_OTHER;
            }
            if (PAL_UART_STM32_Stopped(device, direction))
                PAL_UART_STM32_Complete(device, direction);
        }
    }
    else if (device->pal_device.rx.state != PAL_UART_ASYNC_IDLE)
    {
        Stm32UartRequest* request = &device->rx;
        request->errors |= PAL_UART_STM32_Convert_Errors(code);
        /* IRQ RX only accumulates PE/FE/NE; ORE or DMA RX errors initiate termination. */
        bool terminal = (code & HAL_UART_ERROR_ORE) != 0 ||
            device->pal_device.rx.mode == PAL_UART_ASYNC_DMA;
        if (terminal)
        {
            bool provisional_success = request->complete &&
                (request->result == PAL_UART_TRANSFER_OK || request->result == PAL_UART_TRANSFER_FINISHED_WITH_ERROR);
            if (!request->ending || provisional_success)
            {
                request->ending = true;
                request->result = PAL_UART_STM32_Determine_RxResult(device, code);
            }
            /* UART IRQ initiated RX DMA abort; its failure fallback can also call here. Verify hardware. */
            if (PAL_UART_STM32_Stopped(device, PAL_UART_DIR_RX))
                PAL_UART_STM32_Complete(device, PAL_UART_DIR_RX);
        }
    }
    PAL_UART_STM32_LeaveHal(device);
}

/* User cancellation and error cleanup share abort callbacks; preserve the saved end reason. */
static void PAL_UART_STM32_AbortComplete(UART_HandleTypeDef* huart, PalUartDirection direction)
{
    Stm32Uart* device = PAL_UART_STM32_FindByHalHandle(huart);
    if (device == NULL || PAL_UART_STM32_Get_Transfer(device, direction)->state == PAL_UART_ASYNC_IDLE ||
        !PAL_UART_STM32_Get_Request(device, direction)->ending)
        return;
    if (PAL_UART_STM32_Stopped(device, direction)) PAL_UART_STM32_Complete(device, direction);
}

void HAL_UART_AbortTransmitCpltCallback(UART_HandleTypeDef* huart)
{
    PAL_UART_STM32_AbortComplete(huart, PAL_UART_DIR_TX);
}

void HAL_UART_AbortReceiveCpltCallback(UART_HandleTypeDef* huart)
{
    PAL_UART_STM32_AbortComplete(huart, PAL_UART_DIR_RX);
}

/* Pair around HAL_UART_IRQHandler to defer notification; no interrupt masking or task switch. */
void PAL_UART_STM32_BeforeUartIRQ(UART_HandleTypeDef* huart)
{
    Stm32Uart* device = PAL_UART_STM32_FindByHalHandle(huart);
    if (device != NULL) ++device->hal_depth;
}

void PAL_UART_STM32_AfterUartIRQ(UART_HandleTypeDef* huart)
{
    /* HAL finished handling the old request; try notifying when the outermost scope exits. */
    Stm32Uart* device = PAL_UART_STM32_FindByHalHandle(huart);
    if (device != NULL) PAL_UART_STM32_LeaveHal(device);
}

static Stm32Uart* PAL_UART_STM32_FindByDma(DMA_HandleTypeDef* hdma)
{
    if (hdma == NULL) return NULL;
    for (size_t i = 0; i < BSP_UART_NUM; ++i)
        if (devices[i].huart != NULL &&
            (devices[i].huart->hdmatx == hdma || devices[i].huart->hdmarx == hdma))
            return &devices[i];
    return NULL;
}

/* Pair around HAL_DMA_IRQHandler and conservatively snapshot progress before HAL channel cleanup. */
void PAL_UART_STM32_BeforeDmaIRQ(DMA_HandleTypeDef* hdma)
{
    Stm32Uart* device = PAL_UART_STM32_FindByDma(hdma);
    if (device == NULL) return;
    ++device->hal_depth;
    /* Sample NDTR before the DMA IRQ can terminate the stream. */
    uint32_t flags = __HAL_DMA_GET_TE_FLAG_INDEX(hdma) | __HAL_DMA_GET_DME_FLAG_INDEX(hdma);
    if (__HAL_DMA_GET_FLAG(hdma, flags))
    {
        PalUartDirection direction = device->huart->hdmatx == hdma? PAL_UART_DIR_TX: PAL_UART_DIR_RX;
        if (PAL_UART_STM32_Get_Transfer(device, direction)->state != PAL_UART_ASYNC_IDLE &&
            PAL_UART_STM32_Get_Transfer(device, direction)->mode == PAL_UART_ASYNC_DMA)
        {
            Stm32UartRequest* request = PAL_UART_STM32_Get_Request(device, direction);
            if (!request->count_saved)
            {
                request->transferred = PAL_UART_STM32_Get_Count(device, direction);
                request->count_saved = true;
            }
        }
    }
}

void PAL_UART_STM32_AfterDmaIRQ(DMA_HandleTypeDef* hdma)
{
    /* Balance BeforeDmaIRQ; user callbacks may still execute in this IRQ context. */
    Stm32Uart* device = PAL_UART_STM32_FindByDma(hdma);
    if (device != NULL) PAL_UART_STM32_LeaveHal(device);
}
