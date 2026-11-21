#include "pal_spi_port.h"
#include "bsp_spi.h"
#include <string.h>
#include "spi.h"
#include "pal_spi_stm32.h"

#define PAL_SPI_STM32_INVALID_BAUD_PRESCALER 0xFFFFFFFFU

typedef struct
{
    PalSpiErrors errors;
    PalSpiTransferResult result;
    size_t transferred;
    const uint8_t* tx;
    uint8_t* rx;
    bool pending;
    bool cancelling;
    bool uses_tx_dma; /* Includes the dummy writer for two-line master RX. */
    bool dma_count_saved;
} Stm32SpiRequest;

typedef struct
{
    PalSpiDevice pal_device;
    PalSpiId id;
    SPI_HandleTypeDef* hspi;
    Stm32SpiRequest req;
    unsigned hal_depth;
    bool dispatching;
} Stm32Spi;

static Stm32Spi devices[BSP_SPI_NUM] =
{
    [PAL_SPI1] = {
        .pal_device.port_context = &devices[PAL_SPI1],
        .id = PAL_SPI1,
        .hspi = &hspi2,
    },
    [PAL_SPI2] = {
        .pal_device.port_context = &devices[PAL_SPI2],
        .id = PAL_SPI2,
        .hspi = &hspi3,
    },
};


static uint32_t KernelClock(const SPI_TypeDef* instance)
{
    if (instance == SPI1) return HAL_RCC_GetPCLK2Freq();
#if defined(SPI4)
    if (instance == SPI4) return HAL_RCC_GetPCLK2Freq();
#endif
    if (instance == SPI2 || instance == SPI3) return HAL_RCC_GetPCLK1Freq();
    return 0;
}

static const uint32_t prescalers[] = {
    SPI_BAUDRATEPRESCALER_2, SPI_BAUDRATEPRESCALER_4, SPI_BAUDRATEPRESCALER_8, SPI_BAUDRATEPRESCALER_16,
    SPI_BAUDRATEPRESCALER_32, SPI_BAUDRATEPRESCALER_64, SPI_BAUDRATEPRESCALER_128, SPI_BAUDRATEPRESCALER_256
};

static uint32_t SelectPrescaler(uint32_t clock, uint32_t maximum)
{
    for (unsigned i = 0; clock != 0 && i < sizeof prescalers / sizeof prescalers[0]; ++i)
        if (clock <= ((uint64_t)maximum << (i + 1))) return prescalers[i];
    return PAL_SPI_STM32_INVALID_BAUD_PRESCALER;
}

static uint32_t ClockRate(const SPI_HandleTypeDef* hspi)
{
    if (hspi->Init.Mode != SPI_MODE_MASTER) return 0;
    for (unsigned i = 0; i < sizeof prescalers / sizeof prescalers[0]; ++i)
        if (hspi->Init.BaudRatePrescaler == prescalers[i]) return KernelClock(hspi->Instance) >> (i + 1);
    return 0;
}

static PalSpiStatus PAL_SPI_STM32_BuildConfig(
    const SPI_TypeDef* instance, const SPI_InitTypeDef* base, const PalSpiConfig* config, SPI_InitTypeDef* out
)
{
    if (instance == NULL || base == NULL || config == NULL || out == NULL) return PAL_SPI_STATUS_INVALID_ARGUMENT;
    SPI_InitTypeDef candidate = *base;

    // Check role and calculate baud prescaler if master
    if (config->role == PAL_SPI_ROLE_MASTER)
    {
        candidate.Mode = SPI_MODE_MASTER;
        if (!config->max_clk_freq_hz) return PAL_SPI_STATUS_INVALID_ARGUMENT;
        uint32_t prescaler = SelectPrescaler(KernelClock(instance), config->max_clk_freq_hz);
        if (prescaler == PAL_SPI_STM32_INVALID_BAUD_PRESCALER) return PAL_SPI_STATUS_UNSUPPORTED;
        candidate.BaudRatePrescaler = prescaler;
        if (config->nss_control == PAL_SPI_NSS_HARDWARE)
            candidate.NSS = SPI_NSS_HARD_OUTPUT;
        else if (config->nss_control == PAL_SPI_NSS_SOFTWARE)
            candidate.NSS = SPI_NSS_SOFT;
        else
            return PAL_SPI_STATUS_INVALID_ARGUMENT;
    }
    else if (config->role == PAL_SPI_ROLE_SLAVE)
    {
        candidate.Mode = SPI_MODE_SLAVE;
        if (config->nss_control == PAL_SPI_NSS_HARDWARE)
            candidate.NSS = SPI_NSS_HARD_INPUT;
        else if (config->nss_control == PAL_SPI_NSS_SOFTWARE)
            candidate.NSS = SPI_NSS_SOFT;
        else
            return PAL_SPI_STATUS_INVALID_ARGUMENT;
    }
    else
    {
        return PAL_SPI_STATUS_INVALID_ARGUMENT;
    }

    // Check data size
    if (config->data_size < PAL_SPI_DATA_SIZE_4BIT || config->data_size > PAL_SPI_DATA_SIZE_32BIT)
        return PAL_SPI_STATUS_INVALID_ARGUMENT;
    if (config->data_size != 8 && config->data_size != 16) return PAL_SPI_STATUS_UNSUPPORTED;
    candidate.DataSize = config->data_size == 8 ? SPI_DATASIZE_8BIT : SPI_DATASIZE_16BIT;

    if (config->data_lines == PAL_SPI_DATA_LINES_SEPARATED)
    {
        if (config->directions == PAL_SPI_DIR_TX)
            candidate.Direction = SPI_DIRECTION_2LINES; /* PAL enforces TX-only access. */
        else if (config->directions == PAL_SPI_DIR_RX)
            candidate.Direction = SPI_DIRECTION_2LINES_RXONLY;
        else if (config->directions == PAL_SPI_DIR_TXRX)
            candidate.Direction = SPI_DIRECTION_2LINES;
        else
            return PAL_SPI_STATUS_INVALID_ARGUMENT;
    }
    else if (config->data_lines == PAL_SPI_DATA_LINES_SHARED)
    {
        if (config->directions < PAL_SPI_DIR_TX || config->directions > PAL_SPI_DIR_TXRX)
            return PAL_SPI_STATUS_INVALID_ARGUMENT;
        candidate.Direction = SPI_DIRECTION_1LINE;
    }
    else
    {
        return PAL_SPI_STATUS_INVALID_ARGUMENT;
    }

    if (config->clk_polarity == PAL_SPI_CLOCK_POLARITY_LOW)
        candidate.CLKPolarity = SPI_POLARITY_LOW;
    else if (config->clk_polarity == PAL_SPI_CLOCK_POLARITY_HIGH)
        candidate.CLKPolarity = SPI_POLARITY_HIGH;
    else
        return PAL_SPI_STATUS_INVALID_ARGUMENT;

    if (config->clk_phase == PAL_SPI_CLOCK_PHASE_1EDGE)
        candidate.CLKPhase = SPI_PHASE_1EDGE;
    else if (config->clk_phase == PAL_SPI_CLOCK_PHASE_2EDGE)
        candidate.CLKPhase = SPI_PHASE_2EDGE;
    else
        return PAL_SPI_STATUS_INVALID_ARGUMENT;

    if (config->nss_polarity == PAL_SPI_NSS_POLARITY_HIGH) return PAL_SPI_STATUS_UNSUPPORTED;
    if (config->nss_polarity != PAL_SPI_NSS_POLARITY_LOW) return PAL_SPI_STATUS_INVALID_ARGUMENT;

    if (config->bit_order == PAL_SPI_BIT_ORDER_MSB_FIRST)
        candidate.FirstBit = SPI_FIRSTBIT_MSB;
    else if (config->bit_order == PAL_SPI_BIT_ORDER_LSB_FIRST)
        candidate.FirstBit = SPI_FIRSTBIT_LSB;
    else
        return PAL_SPI_STATUS_INVALID_ARGUMENT;

    *out = candidate;
    return PAL_SPI_STATUS_OK;
}

static PalSpiStatus PAL_SPI_STM32_Convert_Status(HAL_StatusTypeDef hal_status)
{
    switch (hal_status)
    {
    case HAL_OK: return PAL_SPI_STATUS_OK;
    case HAL_ERROR: return PAL_SPI_STATUS_PORT_ERROR;
    case HAL_BUSY: return PAL_SPI_STATUS_BUSY;
    case HAL_TIMEOUT: return PAL_SPI_STATUS_TIMEOUT;
    default: return PAL_SPI_STATUS_PORT_ERROR;
    }
}

PalSpiDevice* PAL_SPI_Port_Find(PalSpiId id)
{
    if ((unsigned)id >= BSP_SPI_NUM || devices[id].hspi == NULL) return NULL;
    return &devices[id].pal_device;
}

PalSpiStatus PAL_SPI_Port_CheckIdle(void* port_context)
{
    const Stm32Spi* device = (Stm32Spi*)port_context;
    const SPI_HandleTypeDef* hspi = device->hspi;
    if (hspi->State != HAL_SPI_STATE_READY) return PAL_SPI_STATUS_BUSY;
    return PAL_SPI_STATUS_OK;
}

PalSpiStatus PAL_SPI_Port_Init(void* port_context, PalSpiConfig* applied)
{
    Stm32Spi* device = (Stm32Spi*)port_context;
    SPI_HandleTypeDef* hspi = device->hspi;
    if (hspi->State != HAL_SPI_STATE_RESET) return PAL_SPI_STATUS_BUSY;
    SPI_TypeDef* instance = BSP_SPI_Get_Instance(device->id);
    const SPI_InitTypeDef* defaults = BSP_SPI_Get_Init(device->id);
    const PalSpiConfig* config = BSP_SPI_Get_Config(device->id);
    if (instance == NULL || defaults == NULL || config == NULL)
        return PAL_SPI_STATUS_PORT_ERROR;
    SPI_InitTypeDef candidate;
    PalSpiStatus status = PAL_SPI_STM32_BuildConfig(instance, defaults, config, &candidate);
    if (status != PAL_SPI_STATUS_OK) return status;

    hspi->Instance = instance;
    hspi->Init = candidate;
    status = PAL_SPI_STM32_Convert_Status(HAL_SPI_Init(hspi));
    if (status != PAL_SPI_STATUS_OK)
    {
        if (HAL_SPI_DeInit(hspi) != HAL_OK) return PAL_SPI_STATUS_PORT_ERROR;
        return status;
    }
    *applied = *config;
    applied->actual_clk_freq_hz = ClockRate(hspi);
    return PAL_SPI_STATUS_OK;
}

PalSpiStatus PAL_SPI_Port_AttachInitialized(void* port_context, PalSpiConfig* current)
{
    /** Check if the SPI instance is initialized and ready. */
    const Stm32Spi* device = (Stm32Spi*)port_context;
    const SPI_HandleTypeDef* hspi = device->hspi;
    if (hspi->Instance == NULL || hspi->State == HAL_SPI_STATE_RESET)
        return PAL_SPI_STATUS_NOT_INITIALIZED;
    if (hspi->Instance != BSP_SPI_Get_Instance(device->id)) return PAL_SPI_STATUS_PORT_ERROR;
    PalSpiStatus status = PAL_SPI_Port_CheckIdle(port_context);
    if (status != PAL_SPI_STATUS_OK) return status;

    /** Retrieve the current configuration from the SPI handle. */
    PalSpiConfig config = {0};
    /** Role and clock frequency */
    if (hspi->Init.Mode == SPI_MODE_MASTER)
    {
        config.role = PAL_SPI_ROLE_MASTER;
        config.actual_clk_freq_hz = ClockRate(hspi);
        if (config.actual_clk_freq_hz == 0) return PAL_SPI_STATUS_PORT_ERROR;
        config.max_clk_freq_hz = config.actual_clk_freq_hz;
    }
    else if (hspi->Init.Mode == SPI_MODE_SLAVE)
        config.role = PAL_SPI_ROLE_SLAVE;
    else
        return PAL_SPI_STATUS_PORT_ERROR;
    /** Data size */
    if (hspi->Init.DataSize == SPI_DATASIZE_8BIT) config.data_size = PAL_SPI_DATA_SIZE_8BIT;
    else if (hspi->Init.DataSize == SPI_DATASIZE_16BIT) config.data_size = PAL_SPI_DATA_SIZE_16BIT;
    else return PAL_SPI_STATUS_UNSUPPORTED;
    /** Data lines and directions */
    if (hspi->Init.Direction == SPI_DIRECTION_1LINE)
    {
        config.data_lines = PAL_SPI_DATA_LINES_SHARED; // Shared line
        config.directions = PAL_SPI_DIR_TXRX;          // Both directions are available sequentially
    }
    else
    {
        config.data_lines = PAL_SPI_DATA_LINES_SEPARATED; // Separate lines
        if (hspi->Init.Direction == SPI_DIRECTION_2LINES_RXONLY)
            config.directions = PAL_SPI_DIR_RX;
        else if (hspi->Init.Direction == SPI_DIRECTION_2LINES)
            config.directions = PAL_SPI_DIR_TXRX;
        else
            return PAL_SPI_STATUS_PORT_ERROR;
    }
    /** Clock polarity and phase */
    if (hspi->Init.CLKPolarity == SPI_POLARITY_LOW)
        config.clk_polarity = PAL_SPI_CLOCK_POLARITY_LOW;
    else if (hspi->Init.CLKPolarity == SPI_POLARITY_HIGH)
        config.clk_polarity = PAL_SPI_CLOCK_POLARITY_HIGH;
    else
        return PAL_SPI_STATUS_PORT_ERROR;
    if (hspi->Init.CLKPhase == SPI_PHASE_1EDGE)
        config.clk_phase = PAL_SPI_CLOCK_PHASE_1EDGE;
    else if (hspi->Init.CLKPhase == SPI_PHASE_2EDGE)
        config.clk_phase = PAL_SPI_CLOCK_PHASE_2EDGE;
    else
        return PAL_SPI_STATUS_PORT_ERROR;
    /** NSS control and polarity */
    if (hspi->Init.NSS == SPI_NSS_SOFT)
        config.nss_control = PAL_SPI_NSS_SOFTWARE;
    else if (hspi->Init.NSS == (config.role == PAL_SPI_ROLE_MASTER ? SPI_NSS_HARD_OUTPUT : SPI_NSS_HARD_INPUT))
        config.nss_control = PAL_SPI_NSS_HARDWARE;
    else
        return PAL_SPI_STATUS_PORT_ERROR;
    config.nss_polarity = PAL_SPI_NSS_POLARITY_LOW;
    /** Bit order */
    if (hspi->Init.FirstBit == SPI_FIRSTBIT_MSB)
        config.bit_order = PAL_SPI_BIT_ORDER_MSB_FIRST;
    else if (hspi->Init.FirstBit == SPI_FIRSTBIT_LSB)
        config.bit_order = PAL_SPI_BIT_ORDER_LSB_FIRST;
    else
        return PAL_SPI_STATUS_PORT_ERROR;

    *current = config;
    return PAL_SPI_STATUS_OK;
}

PalSpiStatus PAL_SPI_Port_DeInit(void* port_context)
{
    Stm32Spi* device = (Stm32Spi*)port_context;
    return PAL_SPI_STM32_Convert_Status(HAL_SPI_DeInit(device->hspi));
}

PalSpiStatus PAL_SPI_Port_Reconfigure(
    void* port_context,
    PalSpiConfig* new_config, bool* modified
)
{
    Stm32Spi* device = (Stm32Spi*)port_context;
    SPI_HandleTypeDef* hspi = device->hspi;
    *modified = false;
    SPI_InitTypeDef candidate;
    PalSpiStatus status = PAL_SPI_STM32_BuildConfig(hspi->Instance, &hspi->Init, new_config, &candidate);
    if (status != PAL_SPI_STATUS_OK) return status;
    *modified = true;
    hspi->Init = candidate;

    status = PAL_SPI_STM32_Convert_Status(HAL_SPI_Init(hspi));
    if (status == PAL_SPI_STATUS_OK)
    {
        new_config->actual_clk_freq_hz = ClockRate(hspi);
    }
    return status;
}

static size_t FrameBytes(const SPI_HandleTypeDef* hspi)
{
    return hspi->Init.DataSize == SPI_DATASIZE_16BIT ? 2u : 1u;
}

static PalSpiStatus FrameCount(const SPI_HandleTypeDef* hspi, const uint8_t* tx, uint8_t* rx,
                               size_t length, uint16_t* frames)
{
    size_t width = FrameBytes(hspi);
    if (length == 0 || (length % width) != 0 || ((uintptr_t)tx % width) != 0 || ((uintptr_t)rx % width) != 0)
        return PAL_SPI_STATUS_INVALID_ARGUMENT;
    size_t count = length / width;
    if (count > UINT16_MAX) return PAL_SPI_STATUS_UNSUPPORTED;
    *frames = (uint16_t)count;
    return PAL_SPI_STATUS_OK;
}

/* The F4 polling and IRQ paths advance the buffer cursors in bytes. */
static size_t BufferProgress(const SPI_HandleTypeDef* hspi, const uint8_t* tx, uint8_t* rx, size_t length)
{
    size_t sent = tx ? (uintptr_t)hspi->pTxBuffPtr - (uintptr_t)tx : length;
    size_t received = rx ? (uintptr_t)hspi->pRxBuffPtr - (uintptr_t)rx : length;
    if (sent > length || received > length) return 0;
    return sent < received ? sent : received;
}

/* The F4 HAL requires DeInit/Init after master RXONLY or bidirectional RX.
 * MSP must restore the same GPIO, DMA and interrupt routing on re-init. */
static bool RestoreReceiver(Stm32Spi* device)
{
    SPI_HandleTypeDef* hspi = device->hspi;
    if (hspi->Init.Mode != SPI_MODE_MASTER || hspi->Init.Direction == SPI_DIRECTION_2LINES) return true;
    if (HAL_SPI_DeInit(hspi) == HAL_OK && HAL_SPI_Init(hspi) == HAL_OK) return true;
    device->pal_device.state.ready = false;
    return false;
}

/* HAL_SPI_Receive uses the RX buffer as its clock-generating TX source in
 * two-line master mode. Put a defined dummy value on MOSI. */
static void PrepareReceive(const SPI_HandleTypeDef* hspi, const uint8_t* tx, uint8_t* rx, size_t length)
{
    if (!tx && rx && hspi->Init.Mode == SPI_MODE_MASTER && hspi->Init.Direction == SPI_DIRECTION_2LINES)
        memset(rx, 0xff, length);
}

static PalSpiStatus Blocking(Stm32Spi* device, const uint8_t* tx, uint8_t* rx,
                             size_t length, uint32_t timeout, size_t* transferred)
{
    SPI_HandleTypeDef* hspi = device->hspi;
    uint16_t frames;
    PalSpiStatus status = FrameCount(hspi, tx, rx, length, &frames);
    if (status != PAL_SPI_STATUS_OK) return status;
    PrepareReceive(hspi, tx, rx, length);
    HAL_StatusTypeDef hal;
    if (tx && rx) hal = HAL_SPI_TransmitReceive(hspi, tx, rx, frames, timeout);
    else if (tx) hal = HAL_SPI_Transmit(hspi, tx, frames, timeout);
    else hal = HAL_SPI_Receive(hspi, rx, frames, timeout);
    status = PAL_SPI_STM32_Convert_Status(hal);
    if (hal == HAL_OK) *transferred = length;
    else if (hal == HAL_TIMEOUT || hal == HAL_ERROR)
    {
        *transferred = BufferProgress(hspi, tx, rx, length);
        status = hal == HAL_TIMEOUT || hspi->ErrorCode == HAL_SPI_ERROR_FLAG
            ? PAL_SPI_STATUS_TIMEOUT : PAL_SPI_STATUS_IO_ERROR;
    }
    if (rx && !tx && hal != HAL_BUSY && !RestoreReceiver(device)) return PAL_SPI_STATUS_PORT_ERROR;
    return status;
}

PalSpiStatus PAL_SPI_Port_Transmit(void* context, const uint8_t* tx, size_t length, uint32_t timeout, size_t* count)
{
    return Blocking(context, tx, NULL, length, timeout, count);
}

PalSpiStatus PAL_SPI_Port_Receive(void* context, uint8_t* rx, size_t length, uint32_t timeout, size_t* count)
{
    return Blocking(context, NULL, rx, length, timeout, count);
}

PalSpiStatus PAL_SPI_Port_TransmitReceive(void* context, const uint8_t* tx, uint8_t* rx, size_t length,
                                         uint32_t timeout, size_t* count)
{
    return Blocking(context, tx, rx, length, timeout, count);
}

static Stm32Spi* FindHandle(const void* handle, bool dma)
{
    if (!handle) return NULL;
    for (size_t i = 0; i < BSP_SPI_NUM; ++i)
    {
        SPI_HandleTypeDef* hspi = devices[i].hspi;
        if (!hspi) continue;
        if (dma ? (handle == hspi->hdmatx || handle == hspi->hdmarx) : handle == hspi)
            return &devices[i];
    }
    return NULL;
}

static bool DmaIdle(const DMA_HandleTypeDef* dma)
{
    return dma && dma->State == HAL_DMA_STATE_READY && (dma->Instance->CR & DMA_SxCR_EN) == 0;
}

static PalSpiStatus CheckChannel(Stm32Spi* device, PalSpiAsyncMode mode, bool tx)
{
    PalSpiAsyncSupport support = tx ? BSP_SPI_Get_TxAsyncSupport(device->id) : BSP_SPI_Get_RxAsyncSupport(device->id);
    if (mode == PAL_SPI_ASYNC_IRQ)
        return (support & PAL_SPI_ASYNC_SUPPORT_IRQ) ? PAL_SPI_STATUS_OK : PAL_SPI_STATUS_UNSUPPORTED;
    if (mode != PAL_SPI_ASYNC_DMA || !(support & PAL_SPI_ASYNC_SUPPORT_DMA)) return PAL_SPI_STATUS_UNSUPPORTED;
    DMA_HandleTypeDef* dma = tx ? device->hspi->hdmatx : device->hspi->hdmarx;
    if (!dma || !dma->Instance || dma->Parent != device->hspi) return PAL_SPI_STATUS_UNSUPPORTED;

    size_t width = FrameBytes(device->hspi);
    const DMA_InitTypeDef* init = &dma->Init;
    if (init->Mode != DMA_NORMAL || (dma->Instance->CR & DMA_SxCR_DBM) ||
        init->Direction != (tx ? DMA_MEMORY_TO_PERIPH : DMA_PERIPH_TO_MEMORY) ||
        init->PeriphInc != DMA_PINC_DISABLE || init->MemInc != DMA_MINC_ENABLE ||
        init->FIFOMode != DMA_FIFOMODE_DISABLE ||
        init->PeriphDataAlignment != (width == 1 ? DMA_PDATAALIGN_BYTE : DMA_PDATAALIGN_HALFWORD) ||
        init->MemDataAlignment != (width == 1 ? DMA_MDATAALIGN_BYTE : DMA_MDATAALIGN_HALFWORD))
        return PAL_SPI_STATUS_UNSUPPORTED;
    return DmaIdle(dma) ? PAL_SPI_STATUS_OK : PAL_SPI_STATUS_BUSY;
}

static bool ChannelsStopped(const Stm32Spi* device)
{
    if (device->pal_device.transfer.mode != PAL_SPI_ASYNC_DMA) return true;
    return (!device->req.uses_tx_dma || DmaIdle(device->hspi->hdmatx)) &&
           (!device->req.rx || DmaIdle(device->hspi->hdmarx));
}

/* All HAL callbacks only record an outcome. Dispatch after the outer HAL/IRQ
 * scope exits, including TX stream cleanup following an RX completion. */
static void LeaveHal(Stm32Spi* device)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    --device->hal_depth;
    if (!device->hal_depth && !device->dispatching)
    {
        SPI_HandleTypeDef* hspi = device->hspi;
        /* When every DMA channel is already idle HAL's Abort_IT(NO_XFER)
         * produces no abort callback on the SPI peripheral error path. */
        if (device->pal_device.transfer.mode == PAL_SPI_ASYNC_DMA && (device->req.tx || device->req.rx) &&
            !device->req.pending && hspi->ErrorCode != HAL_SPI_ERROR_NONE && ChannelsStopped(device) &&
            hspi->State == HAL_SPI_STATE_READY)
        {
            HAL_SPI_ErrorCallback(hspi);
        }
        device->dispatching = true;
        while (device->req.pending && device->hspi->State == HAL_SPI_STATE_READY && ChannelsStopped(device))
        {
            Stm32SpiRequest completed = device->req;
            device->req = (Stm32SpiRequest){0};
            __set_PRIMASK(mask);
            if (completed.rx && !completed.tx && !RestoreReceiver(device))
            {
                completed.result = PAL_SPI_TRANSFER_ABORTED;
                completed.errors |= PAL_SPI_ERROR_OTHER;
            }
            PAL_SPI_AsyncFinish(device->id, completed.result, completed.transferred, completed.errors);
            __disable_irq();
        }
        device->dispatching = false;
    }
    __set_PRIMASK(mask);
}

static PalSpiStatus Start(Stm32Spi* device, PalSpiAsyncMode mode, const uint8_t* tx, uint8_t* rx, size_t length)
{
    SPI_HandleTypeDef* hspi = device->hspi;
    uint16_t frames;
    PalSpiStatus status = FrameCount(hspi, tx, rx, length, &frames);
    if (status != PAL_SPI_STATUS_OK) return status;
    bool duplex_dma = mode == PAL_SPI_ASYNC_DMA && rx &&
        (tx || (hspi->Init.Mode == SPI_MODE_MASTER && hspi->Init.Direction == SPI_DIRECTION_2LINES));
    if (tx || duplex_dma) status = CheckChannel(device, mode, true);
    if (status == PAL_SPI_STATUS_OK && rx) status = CheckChannel(device, mode, false);
    if (status != PAL_SPI_STATUS_OK) return status;

    PrepareReceive(hspi, tx, rx, length);
    ++device->hal_depth;
    device->req = (Stm32SpiRequest){.tx = tx, .rx = rx, .uses_tx_dma = mode == PAL_SPI_ASYNC_DMA && (tx || duplex_dma)};
    uint32_t mask = __get_PRIMASK();
    __disable_irq(); /* Install both stream callbacks before either IRQ runs. */
    HAL_StatusTypeDef hal;
    if (mode == PAL_SPI_ASYNC_DMA)
    {
        if (tx && rx) hal = HAL_SPI_TransmitReceive_DMA(hspi, tx, rx, frames);
        else if (tx) hal = HAL_SPI_Transmit_DMA(hspi, tx, frames);
        else hal = HAL_SPI_Receive_DMA(hspi, rx, frames);
    }
    else
    {
        if (tx && rx) hal = HAL_SPI_TransmitReceive_IT(hspi, tx, rx, frames);
        else if (tx) hal = HAL_SPI_Transmit_IT(hspi, tx, frames);
        else hal = HAL_SPI_Receive_IT(hspi, rx, frames);
    }
    if (hal == HAL_OK && duplex_dma)
    {
        /* HAL leaves the TX error callback empty in the duplex path. */
        hspi->hdmatx->XferErrorCallback = hspi->hdmarx->XferErrorCallback;
    }
    if (hal == HAL_ERROR && mode == PAL_SPI_ASYNC_DMA)
    {
        if (duplex_dma && (hspi->Instance->CR2 & SPI_CR2_RXDMAEN))
        {
            CLEAR_BIT(hspi->Instance->CR2, SPI_CR2_RXDMAEN);
            DMA_HandleTypeDef* dma = hspi->hdmarx;
            /* Only RX was started. Abort without a tick wait inside this guard. */
            (void)HAL_DMA_Abort_IT(dma);
            if (!(dma->Instance->CR & DMA_SxCR_EN) &&
                __HAL_DMA_GET_FLAG(dma, __HAL_DMA_GET_TC_FLAG_INDEX(dma)))
                HAL_DMA_IRQHandler(dma);
            if (!DmaIdle(dma))
            {
                /* Retain buffer ownership until the stream's abort IRQ. */
                device->req.uses_tx_dma = false;
                device->req.pending = true;
                device->req.result = PAL_SPI_TRANSFER_ABORTED;
                device->req.errors = PAL_SPI_ERROR_DMA;
                device->req.dma_count_saved = true;
                hal = HAL_OK;
            }
        }
        hspi->State = HAL_SPI_STATE_READY;
    }
    if (hal != HAL_OK) device->req = (Stm32SpiRequest){0};
    __set_PRIMASK(mask);
    LeaveHal(device);
    return PAL_SPI_STM32_Convert_Status(hal);
}

PalSpiStatus PAL_SPI_Port_StartTx(void* context, PalSpiAsyncMode mode, const uint8_t* tx, size_t length)
{
    return Start(context, mode, tx, NULL, length);
}

PalSpiStatus PAL_SPI_Port_StartRx(void* context, PalSpiAsyncMode mode, uint8_t* rx, size_t length)
{
    return Start(context, mode, NULL, rx, length);
}

PalSpiStatus PAL_SPI_Port_StartTxRx(void* context, PalSpiAsyncMode mode, const uint8_t* tx, uint8_t* rx, size_t length)
{
    return Start(context, mode, tx, rx, length);
}

static void SaveProgress(Stm32Spi* device)
{
    Stm32SpiRequest* req = &device->req;
    size_t length = device->pal_device.transfer.length;
    if (device->pal_device.transfer.mode == PAL_SPI_ASYNC_DMA)
    {
        if (req->dma_count_saved) return;
        size_t tx_left = req->uses_tx_dma ? __HAL_DMA_GET_COUNTER(device->hspi->hdmatx) : 0;
        size_t rx_left = req->rx ? __HAL_DMA_GET_COUNTER(device->hspi->hdmarx) : 0;
        size_t remaining = tx_left > rx_left ? tx_left : rx_left;
        size_t width = FrameBytes(device->hspi);
        req->transferred = remaining <= length / width ? length - remaining * width : 0;
        req->dma_count_saved = true;
    }
    else
        req->transferred = BufferProgress(device->hspi, req->tx, req->rx, length);
}

static PalSpiErrors SpiErrors(uint32_t code)
{
    PalSpiErrors errors = PAL_SPI_ERROR_NONE;
    if (code & HAL_SPI_ERROR_OVR) errors |= PAL_SPI_ERROR_OVERRUN;
    if (code & HAL_SPI_ERROR_MODF) errors |= PAL_SPI_ERROR_MODE_FAULT;
    if (code & HAL_SPI_ERROR_CRC) errors |= PAL_SPI_ERROR_CRC;
    if (code & HAL_SPI_ERROR_DMA) errors |= PAL_SPI_ERROR_DMA;
    if (code & ~(HAL_SPI_ERROR_OVR | HAL_SPI_ERROR_MODF | HAL_SPI_ERROR_CRC | HAL_SPI_ERROR_DMA))
        errors |= PAL_SPI_ERROR_OTHER;
    return errors;
}

PalSpiStatus PAL_SPI_Port_Cancel(void* context)
{
    Stm32Spi* device = context;
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if ((!device->req.tx && !device->req.rx) || device->req.pending || device->req.cancelling ||
        device->hal_depth != 0 || device->pal_device.transfer.state != PAL_SPI_ASYNC_ACTIVE)
    {
        __set_PRIMASK(mask);
        return PAL_SPI_STATUS_BUSY;
    }
    ++device->hal_depth;
    SaveProgress(device);
    device->req.cancelling = true;
    device->pal_device.transfer.state = PAL_SPI_ASYNC_CANCEL_PENDING;
    /* Abort_IT may complete synchronously. Serialize the initial abort with
     * SPI/DMA handlers until HAL has installed both DMA abort callbacks. */
    /* Abort_IT waits for its TXE/RXNE ISR if either source is enabled.
     * Disarm both here so cancellation also works from a completion ISR. */
    __HAL_SPI_DISABLE_IT(device->hspi, SPI_IT_TXE | SPI_IT_RXNE | SPI_IT_ERR);
    __HAL_SPI_DISABLE(device->hspi);
    HAL_StatusTypeDef hal = HAL_SPI_Abort_IT(device->hspi);
    if (device->req.pending) hal = HAL_OK;
    if (hal != HAL_OK)
    {
        device->pal_device.transfer.state = PAL_SPI_ASYNC_ACTIVE;
        device->req.cancelling = false;
        device->req.dma_count_saved = false;
    }
    __set_PRIMASK(mask);
    LeaveHal(device);
    return PAL_SPI_STM32_Convert_Status(hal);
}

static void Completed(SPI_HandleTypeDef* hspi, bool tx, bool rx)
{
    Stm32Spi* device = FindHandle(hspi, false);
    if (!device || (device->req.tx != NULL) != tx || (device->req.rx != NULL) != rx ||
        device->req.pending || device->req.cancelling) return;
    device->req.result = PAL_SPI_TRANSFER_OK;
    device->req.transferred = device->pal_device.transfer.length;
    device->req.pending = true;
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef* hspi) { Completed(hspi, true, false); }
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef* hspi) { Completed(hspi, false, true); }
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef* hspi) { Completed(hspi, true, true); }

void HAL_SPI_AbortCpltCallback(SPI_HandleTypeDef* hspi)
{
    Stm32Spi* device = FindHandle(hspi, false);
    if (!device || !device->req.cancelling) return;
    CLEAR_BIT(hspi->Instance->CR2, SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN);
    device->req.errors |= SpiErrors(hspi->ErrorCode);
    device->req.result = PAL_SPI_TRANSFER_CANCELLED;
    device->req.pending = true;
}

static void StopPeer(DMA_HandleTypeDef* dma)
{
    /* SPI_DMAError closes SPI but may leave the other DMA channel active.
     * Its abort IRQ releases the channel and retries dispatch in AfterDmaIRQ. */
    if (dma->State == HAL_DMA_STATE_BUSY)
    {
        dma->XferAbortCallback = NULL;
        (void)HAL_DMA_Abort_IT(dma);
    }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef* hspi)
{
    Stm32Spi* device = FindHandle(hspi, false);
    if (!device || (!device->req.tx && !device->req.rx)) return;
    __HAL_SPI_DISABLE_IT(hspi, SPI_IT_ERR | SPI_IT_TXE | SPI_IT_RXNE);
    __HAL_SPI_DISABLE(hspi);
    SaveProgress(device);
    device->req.errors |= SpiErrors(hspi->ErrorCode);
    device->req.result = device->req.cancelling ? PAL_SPI_TRANSFER_CANCELLED : PAL_SPI_TRANSFER_ABORTED;
    device->req.pending = true;
    if (device->pal_device.transfer.mode == PAL_SPI_ASYNC_DMA)
    {
        if (device->req.uses_tx_dma) StopPeer(hspi->hdmatx);
        if (device->req.rx) StopPeer(hspi->hdmarx);
    }
}

void PAL_SPI_STM32_BeforeSpiIRQ(SPI_HandleTypeDef* hspi)
{
    Stm32Spi* device = FindHandle(hspi, false);
    if (!device) return;
    ++device->hal_depth;
    /* Stream NDTR survives HAL abort. SaveProgress reads it when the error is confirmed. */
}

void PAL_SPI_STM32_AfterSpiIRQ(SPI_HandleTypeDef* hspi)
{
    Stm32Spi* device = FindHandle(hspi, false);
    if (!device) return;
    LeaveHal(device);
}

void PAL_SPI_STM32_BeforeDmaIRQ(DMA_HandleTypeDef* dma)
{
    Stm32Spi* device = FindHandle(dma, true);
    if (!device) return;
    ++device->hal_depth;
    if (device->pal_device.transfer.mode == PAL_SPI_ASYNC_DMA && (device->req.tx || device->req.rx) &&
        __HAL_DMA_GET_FLAG(dma, __HAL_DMA_GET_TE_FLAG_INDEX(dma) | __HAL_DMA_GET_DME_FLAG_INDEX(dma)))
        SaveProgress(device);
}

void PAL_SPI_STM32_AfterDmaIRQ(DMA_HandleTypeDef* dma)
{
    Stm32Spi* device = FindHandle(dma, true);
    if (device) LeaveHal(device);
}
