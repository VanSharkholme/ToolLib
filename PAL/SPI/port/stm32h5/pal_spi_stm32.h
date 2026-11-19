#ifndef PAL_SPI_STM32_H
#define PAL_SPI_STM32_H

#include "stm32h5xx_hal.h"

/* Pair around the corresponding HAL handler, including each configured SPI DMA channel.
 * Completions may run in IRQ context. As with UART, callers serialize task-level PAL access. */
void PAL_SPI_STM32_BeforeSpiIRQ(SPI_HandleTypeDef* hspi);
void PAL_SPI_STM32_AfterSpiIRQ(SPI_HandleTypeDef* hspi);
void PAL_SPI_STM32_BeforeDmaIRQ(DMA_HandleTypeDef* hdma);
void PAL_SPI_STM32_AfterDmaIRQ(DMA_HandleTypeDef* hdma);

#endif //PAL_SPI_STM32_H
