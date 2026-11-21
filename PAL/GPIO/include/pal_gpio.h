#ifndef TOOLLIB_PAL_GPIO_H
#define TOOLLIB_PAL_GPIO_H

#include "pal_gpio_types.h"

PalGpioStatus PAL_GPIO_SetConfig(PalGpioId id, const PalGpioConfig *config);
PalGpioStatus PAL_GPIO_GetConfig(PalGpioId id, PalGpioConfig *config);

PalGpioStatus PAL_GPIO_SetMode(PalGpioId id, PalGpioMode mode);
PalGpioStatus PAL_GPIO_GetMode(PalGpioId id, PalGpioMode *mode);
PalGpioStatus PAL_GPIO_SetOutputType(PalGpioId id, PalGpioOutputType output_type);
PalGpioStatus PAL_GPIO_GetOutputType(PalGpioId id, PalGpioOutputType *output_type);
PalGpioStatus PAL_GPIO_SetSpeed(PalGpioId id, PalGpioSpeed speed);
PalGpioStatus PAL_GPIO_GetSpeed(PalGpioId id, PalGpioSpeed *speed);
PalGpioStatus PAL_GPIO_SetPull(PalGpioId id, PalGpioPull pull);
PalGpioStatus PAL_GPIO_GetPull(PalGpioId id, PalGpioPull *pull);
PalGpioStatus PAL_GPIO_SetAlternate(PalGpioId id, uint8_t alternate);
PalGpioStatus PAL_GPIO_GetAlternate(PalGpioId id, uint8_t *alternate);

/* Only ordinary inputs support interrupts. Disabled lines read back EDGE_NONE. */
PalGpioStatus PAL_GPIO_SetInterrupt(PalGpioId id, bool enabled, PalGpioEdge edge);
PalGpioStatus PAL_GPIO_GetInterrupt(PalGpioId id, bool *enabled, PalGpioEdge *edge);
/* A NULL callback unregisters it. On STM32 callbacks execute in IRQ context. */
PalGpioStatus PAL_GPIO_SetCallback(PalGpioId id, PalGpioCallback callback, void *context);

PalGpioStatus PAL_GPIO_Read(PalGpioId id, bool *high);
PalGpioStatus PAL_GPIO_Write(PalGpioId id, bool high);
PalGpioStatus PAL_GPIO_Toggle(PalGpioId id);

#endif
