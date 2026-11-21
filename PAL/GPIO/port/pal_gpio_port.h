#ifndef TOOLLIB_PAL_GPIO_PORT_H
#define TOOLLIB_PAL_GPIO_PORT_H

#include "pal_gpio.h"
#include <stddef.h>

static inline bool valid_config(const PalGpioConfig *config)
{
    return config != NULL &&
           (unsigned)config->mode <= PAL_GPIO_ANALOG &&
           (unsigned)config->output_type <= PAL_GPIO_OPEN_DRAIN &&
           (unsigned)config->speed <= PAL_GPIO_SPEED_VERY_HIGH &&
           (unsigned)config->pull <= PAL_GPIO_PULL_DOWN &&
           (unsigned)config->alternate <= 15u &&
           (!config->interrupt_enabled ||
            (config->edge >= PAL_GPIO_EDGE_RISING && config->edge <= PAL_GPIO_EDGE_BOTH));
}

#endif
