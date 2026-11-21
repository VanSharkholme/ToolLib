# GPIO PAL

Include `pal_gpio.h` and link `pal_gpio`. A board ID can be used immediately.
Use SetConfig/GetConfig for a complete configuration, or the matching Set/Get
pair for each field. Output type, speed and AF number can be set before mode
selection. Configuration preserves the output latch. Read samples the pad;
Write and Toggle require ordinary output mode.

SetInterrupt(id, true, edge) enables an ordinary input or changes its active
edge. Disable with SetInterrupt(id, false, PAL_GPIO_EDGE_NONE). Disabled or
unrouted pins report false and EDGE_NONE. A different active source sharing
the line returns BUSY on enable. An EXTI event user also prevents incompatible
routing or trigger changes. A rejected complete configuration leaves all pin
fields intact. SetConfig can disable the interrupt and select output mode
in one call. Switching mode alone while the interrupt is active returns BUSY.

SetCallback(id, callback, context) registers a handler independently of the
interrupt mask. Pass NULL to unregister. Register before enabling if the first
edge matters. On STM32 callbacks run in interrupt context. Multiple edges can
coalesce into one pending bit.

## STM32F4 BSP integration

Select TOOLLIB_GPIO_PORT=stm32f4. Supply bsp_gpio.h with contiguous IDs
starting at zero and BSP_GPIO_COUNT, and define the constant bsp_gpio_map
declared in pal_gpio_stm32.h. Each entry names one unique GPIO instance and
one GPIO_PIN_n mask. See [example](port/stm32f4/example/bsp_gpio.c).
The port opens GPIO and SYSCFG clocks on demand and reads configuration from
hardware. Coordinate other code that touches the same registers.

The BSP enables NVIC and calls HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_n) for each
handled line (one bit per call, including on shared vectors). HAL clears
pending and calls the port's strong HAL_GPIO_EXTI_Callback. This callback
matches the routed port and line against the BSP map. Integrate existing HAL
callback users through per-ID registrations; firmware has one strong HAL hook.

## Host

Select TOOLLIB_GPIO_PORT=host. PAL_GPIO_Host_Reset clears simulated pins,
and PAL_GPIO_Host_SetLevel drives the physical level, delivering matching
edges synchronously. PAL_GPIO_HOST_AF_INPUT and PAL_GPIO_HOST_SHARED_LINE
model different ports sharing line 3; both reset to ordinary input mode.
