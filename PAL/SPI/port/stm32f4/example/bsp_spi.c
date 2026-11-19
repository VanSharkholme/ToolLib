#include "bsp_spi.h"
#include "spi.h"

SPI_HandleTypeDef *BSP_SPI_Get_Handle(PalSpiId id)
{
    switch (id)
    {
    case PAL_SPI2: return &hspi2;
    case PAL_SPI3: return &hspi3;
    default: return NULL;
    }
}

SPI_TypeDef *BSP_SPI_Get_Instance(PalSpiId id)
{
    switch (id)
    {
    case PAL_SPI2: return SPI2;
    case PAL_SPI3: return SPI3;
    default: return NULL;
    }
}

const SPI_InitTypeDef *BSP_SPI_Get_Init(PalSpiId id)
{
    static const SPI_InitTypeDef defaults = {
        .TIMode = SPI_TIMODE_DISABLE,
        .CRCCalculation = SPI_CRCCALCULATION_DISABLE,
        .CRCPolynomial = 10,
    };
    return BSP_SPI_Get_Handle(id) ? &defaults : NULL;
}

const PalSpiConfig *BSP_SPI_Get_Config(PalSpiId id)
{
    static const PalSpiConfig defaults = {
        .max_clk_freq_hz = 5250000, /* APB1 42 MHz / 8 */
        .role = PAL_SPI_ROLE_MASTER,
        .data_size = PAL_SPI_DATA_SIZE_8BIT,
        .data_lines = PAL_SPI_DATA_LINES_SEPARATED,
        .directions = PAL_SPI_DIR_TXRX,
        .clk_polarity = PAL_SPI_CLOCK_POLARITY_LOW,
        .clk_phase = PAL_SPI_CLOCK_PHASE_1EDGE,
        .nss_control = PAL_SPI_NSS_HARDWARE,
        .nss_polarity = PAL_SPI_NSS_POLARITY_LOW,
        .bit_order = PAL_SPI_BIT_ORDER_MSB_FIRST,
    };
    return BSP_SPI_Get_Handle(id) ? &defaults : NULL;
}

PalSpiAsyncSupport BSP_SPI_Get_TxAsyncSupport(PalSpiId id)
{
    return BSP_SPI_Get_Handle(id) ? PAL_SPI_ASYNC_SUPPORT_IRQ_DMA : PAL_SPI_ASYNC_SUPPORT_NONE;
}

PalSpiAsyncSupport BSP_SPI_Get_RxAsyncSupport(PalSpiId id)
{
    return BSP_SPI_Get_Handle(id) ? PAL_SPI_ASYNC_SUPPORT_IRQ_DMA : PAL_SPI_ASYNC_SUPPORT_NONE;
}
