#ifndef STM32F4_BSP_SPI_H
#define STM32F4_BSP_SPI_H
#include "pal_spi_types.h"
#include "stm32f4xx_hal.h"

#define BSP_SPI_NUM (PAL_SPI3 + 1) /* ID slot count, including holes */

SPI_HandleTypeDef *BSP_SPI_Get_Handle(PalSpiId id);
SPI_TypeDef *BSP_SPI_Get_Instance(PalSpiId id);
const SPI_InitTypeDef *BSP_SPI_Get_Init(PalSpiId id);
const PalSpiConfig *BSP_SPI_Get_Config(PalSpiId id);
PalSpiAsyncSupport BSP_SPI_Get_TxAsyncSupport(PalSpiId id);
PalSpiAsyncSupport BSP_SPI_Get_RxAsyncSupport(PalSpiId id);
#endif //STM32F4_BSP_SPI_H
