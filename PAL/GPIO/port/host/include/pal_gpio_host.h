#ifndef TOOLLIB_PAL_GPIO_HOST_H
#define TOOLLIB_PAL_GPIO_HOST_H

#include "pal_gpio.h"

enum {
    PAL_GPIO_HOST_INPUT, PAL_GPIO_HOST_OUTPUT, PAL_GPIO_HOST_AF_INPUT,
    PAL_GPIO_HOST_SHARED_LINE, PAL_GPIO_HOST_COUNT
};

void PAL_GPIO_Host_Reset(void);
/* Drive the pad; dispatch a matching transition synchronously. */
PalGpioStatus PAL_GPIO_Host_SetLevel(PalGpioId id, bool high);

#endif
