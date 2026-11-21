#ifndef TOOLLIB_PAL_GPIO_STM32_H
#define TOOLLIB_PAL_GPIO_STM32_H

#include "pal_gpio.h"
#include "stm32f4xx_hal.h"

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin; /* Exactly one GPIO_PIN_n bit. */
} PalGpioStm32Pin;

/* BSP supplies one unique physical pin per ID and BSP_GPIO_COUNT in bsp_gpio.h. */
extern const PalGpioStm32Pin bsp_gpio_map[];

#endif
