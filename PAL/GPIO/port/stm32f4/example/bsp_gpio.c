#include "bsp_gpio.h"
#include "pal_gpio_stm32.h"

const PalGpioStm32Pin bsp_gpio_map[BSP_GPIO_COUNT] = {
    [BSP_GPIO_LED] = { GPIOC, GPIO_PIN_0 },
    [BSP_GPIO_BUTTON] = { GPIOB, GPIO_PIN_3 },
};
