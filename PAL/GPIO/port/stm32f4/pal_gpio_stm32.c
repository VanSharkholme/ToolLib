#include "pal_gpio_stm32.h"
#include "pal_gpio_port.h"
#include "bsp_gpio.h"

typedef struct {
    PalGpioCallback function;
    void *context;
} Callback;

static Callback callbacks[BSP_GPIO_COUNT];

static uint32_t enter_critical(void)
{
    uint32_t saved = __get_PRIMASK();
    __disable_irq();
    return saved;
}

static const PalGpioStm32Pin *find_pin(PalGpioId id)
{
    if (id >= BSP_GPIO_COUNT) return NULL;
    const PalGpioStm32Pin *pin = &bsp_gpio_map[id];
    if (pin->port == NULL || pin->pin == 0u || (pin->pin & (pin->pin - 1u)) != 0u) return NULL;
    return pin;
}

/* Call in a critical section because the RCC enable macros perform a shared RMW. */
static void enable_gpio_clock(GPIO_TypeDef *port)
{
#ifdef GPIOA
    if (port == GPIOA) { __HAL_RCC_GPIOA_CLK_ENABLE(); return; }
#endif
#ifdef GPIOB
    if (port == GPIOB) { __HAL_RCC_GPIOB_CLK_ENABLE(); return; }
#endif
#ifdef GPIOC
    if (port == GPIOC) { __HAL_RCC_GPIOC_CLK_ENABLE(); return; }
#endif
#ifdef GPIOD
    if (port == GPIOD) { __HAL_RCC_GPIOD_CLK_ENABLE(); return; }
#endif
#ifdef GPIOE
    if (port == GPIOE) { __HAL_RCC_GPIOE_CLK_ENABLE(); return; }
#endif
#ifdef GPIOF
    if (port == GPIOF) { __HAL_RCC_GPIOF_CLK_ENABLE(); return; }
#endif
#ifdef GPIOG
    if (port == GPIOG) { __HAL_RCC_GPIOG_CLK_ENABLE(); return; }
#endif
#ifdef GPIOH
    if (port == GPIOH) { __HAL_RCC_GPIOH_CLK_ENABLE(); return; }
#endif
#ifdef GPIOI
    if (port == GPIOI) { __HAL_RCC_GPIOI_CLK_ENABLE(); return; }
#endif
#ifdef GPIOJ
    if (port == GPIOJ) { __HAL_RCC_GPIOJ_CLK_ENABLE(); return; }
#endif
#ifdef GPIOK
    if (port == GPIOK) { __HAL_RCC_GPIOK_CLK_ENABLE(); return; }
#endif
}

static unsigned line_of(const PalGpioStm32Pin *pin)
{
    return POSITION_VAL((uint32_t)pin->pin);
}

static uint32_t read_field(uint32_t value, unsigned shift, uint32_t mask)
{
    return (value >> shift) & mask;
}

static void write_field(volatile uint32_t *reg, unsigned shift, uint32_t mask, uint32_t value)
{
    MODIFY_REG(*reg, mask << shift, value << shift);
}

static PalGpioMode read_mode(const PalGpioStm32Pin *pin)
{
    unsigned line = line_of(pin);
    return (PalGpioMode)read_field(pin->port->MODER, line * 2u, 3u);
}

static void write_mode(const PalGpioStm32Pin *pin, PalGpioMode value)
{
    unsigned line = line_of(pin);
    write_field(&pin->port->MODER, line * 2u, 3u, value);
}

static PalGpioOutputType read_output_type(const PalGpioStm32Pin *pin)
{
    unsigned line = line_of(pin);
    return (PalGpioOutputType)read_field(pin->port->OTYPER, line, 1u);
}

static void write_output_type(const PalGpioStm32Pin *pin, PalGpioOutputType value)
{
    unsigned line = line_of(pin);
    write_field(&pin->port->OTYPER, line, 1u, value);
}

static PalGpioSpeed read_speed(const PalGpioStm32Pin *pin)
{
    unsigned line = line_of(pin);
    return (PalGpioSpeed)read_field(pin->port->OSPEEDR, line * 2u, 3u);
}

static void write_speed(const PalGpioStm32Pin *pin, PalGpioSpeed value)
{
    unsigned line = line_of(pin);
    write_field(&pin->port->OSPEEDR, line * 2u, 3u, value);
}

static PalGpioPull read_pull(const PalGpioStm32Pin *pin)
{
    unsigned line = line_of(pin);
    return (PalGpioPull)read_field(pin->port->PUPDR, line * 2u, 3u);
}

static void write_pull(const PalGpioStm32Pin *pin, PalGpioPull value)
{
    unsigned line = line_of(pin);
    write_field(&pin->port->PUPDR, line * 2u, 3u, value);
}

static uint8_t read_alternate(const PalGpioStm32Pin *pin)
{
    unsigned line = line_of(pin);
    return (uint8_t)read_field(pin->port->AFR[line / 8u], (line % 8u) * 4u, 15u);
}

static void write_alternate(const PalGpioStm32Pin *pin, uint8_t value)
{
    unsigned line = line_of(pin);
    write_field(&pin->port->AFR[line / 8u], (line % 8u) * 4u, 15u, value);
}

static bool routed_here(const PalGpioStm32Pin *pin)
{
    unsigned line = line_of(pin);
    return read_field(SYSCFG->EXTICR[line / 4u], (line % 4u) * 4u, 15u) == GPIO_GET_INDEX(pin->port);
}

static bool interrupt_enabled(const PalGpioStm32Pin *pin)
{
    return (EXTI->IMR & pin->pin) != 0u && routed_here(pin);
}

static PalGpioEdge line_edge(uint16_t mask)
{
    unsigned rising = (EXTI->RTSR & mask) ? PAL_GPIO_EDGE_RISING : 0u;
    unsigned falling = (EXTI->FTSR & mask) ? PAL_GPIO_EDGE_FALLING : 0u;
    return (PalGpioEdge)(rising | falling);
}

static PalGpioStatus check_interrupt(const PalGpioStm32Pin *pin, PalGpioMode mode,
                                     bool enabled, PalGpioEdge edge)
{
    if (!enabled) return PAL_GPIO_STATUS_OK;
    if (mode != PAL_GPIO_INPUT) return PAL_GPIO_STATUS_INVALID_STATE;
    if (((EXTI->IMR | EXTI->EMR) & pin->pin) && !routed_here(pin)) return PAL_GPIO_STATUS_BUSY;
    if ((EXTI->EMR & pin->pin) && line_edge(pin->pin) != edge) return PAL_GPIO_STATUS_BUSY;
    return PAL_GPIO_STATUS_OK;
}

static void apply_interrupt(const PalGpioStm32Pin *pin, bool enabled, PalGpioEdge edge)
{
    bool active = interrupt_enabled(pin);
    if (active == enabled && (!enabled || line_edge(pin->pin) == edge)) return;
    CLEAR_BIT(EXTI->IMR, pin->pin);
    if (enabled) {
        unsigned line = line_of(pin);
        write_field(&SYSCFG->EXTICR[line / 4u], (line % 4u) * 4u, 15u, GPIO_GET_INDEX(pin->port));
        MODIFY_REG(EXTI->RTSR, pin->pin, (edge & PAL_GPIO_EDGE_RISING) ? pin->pin : 0u);
        MODIFY_REG(EXTI->FTSR, pin->pin, (edge & PAL_GPIO_EDGE_FALLING) ? pin->pin : 0u);
    }
    EXTI->PR = pin->pin;
    if (enabled) SET_BIT(EXTI->IMR, pin->pin);
}

PalGpioStatus PAL_GPIO_SetConfig(PalGpioId id, const PalGpioConfig *config)
{
    const PalGpioStm32Pin *pin = find_pin(id);
    if (!pin) return PAL_GPIO_STATUS_INVALID_ID;
    if (!valid_config(config)) return PAL_GPIO_STATUS_INVALID_ARGUMENT;
    uint32_t saved = enter_critical();
    enable_gpio_clock(pin->port);
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    PalGpioStatus status = check_interrupt(pin, config->mode, config->interrupt_enabled, config->edge);
    if (status == PAL_GPIO_STATUS_OK) {
        if (!config->interrupt_enabled) apply_interrupt(pin, false, PAL_GPIO_EDGE_NONE);
        write_output_type(pin, config->output_type);
        write_speed(pin, config->speed);
        write_pull(pin, config->pull);
        write_alternate(pin, config->alternate);
        write_mode(pin, config->mode);
        if (config->interrupt_enabled) apply_interrupt(pin, true, config->edge);
    }
    __set_PRIMASK(saved);
    return status;
}

PalGpioStatus PAL_GPIO_GetConfig(PalGpioId id, PalGpioConfig *config)
{
    const PalGpioStm32Pin *pin = find_pin(id);
    if (!pin) return PAL_GPIO_STATUS_INVALID_ID;
    if (!config) return PAL_GPIO_STATUS_INVALID_ARGUMENT;
    uint32_t saved = enter_critical();
    enable_gpio_clock(pin->port);
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    config->mode = read_mode(pin);
    config->output_type = read_output_type(pin);
    config->speed = read_speed(pin);
    config->pull = read_pull(pin);
    config->alternate = read_alternate(pin);
    config->interrupt_enabled = interrupt_enabled(pin);
    config->edge = config->interrupt_enabled ? line_edge(pin->pin) : PAL_GPIO_EDGE_NONE;
    __set_PRIMASK(saved);
    return PAL_GPIO_STATUS_OK;
}

PalGpioStatus PAL_GPIO_SetMode(PalGpioId id, PalGpioMode mode)
{
    const PalGpioStm32Pin *pin = find_pin(id);
    if (!pin) return PAL_GPIO_STATUS_INVALID_ID;
    if ((unsigned)mode > PAL_GPIO_ANALOG) return PAL_GPIO_STATUS_INVALID_ARGUMENT;
    uint32_t saved = enter_critical();
    enable_gpio_clock(pin->port);
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    if (mode != PAL_GPIO_INPUT && interrupt_enabled(pin)) {
        __set_PRIMASK(saved);
        return PAL_GPIO_STATUS_BUSY;
    }
    write_mode(pin, mode);
    __set_PRIMASK(saved);
    return PAL_GPIO_STATUS_OK;
}

PalGpioStatus PAL_GPIO_GetMode(PalGpioId id, PalGpioMode *mode)
{
    const PalGpioStm32Pin *pin = find_pin(id);
    if (!pin) return PAL_GPIO_STATUS_INVALID_ID;
    if (!mode) return PAL_GPIO_STATUS_INVALID_ARGUMENT;
    uint32_t saved = enter_critical();
    enable_gpio_clock(pin->port);
    *mode = read_mode(pin);
    __set_PRIMASK(saved);
    return PAL_GPIO_STATUS_OK;
}

PalGpioStatus PAL_GPIO_SetOutputType(PalGpioId id, PalGpioOutputType output_type)
{
    const PalGpioStm32Pin *pin = find_pin(id);
    if (!pin) return PAL_GPIO_STATUS_INVALID_ID;
    if ((unsigned)output_type > PAL_GPIO_OPEN_DRAIN) return PAL_GPIO_STATUS_INVALID_ARGUMENT;
    uint32_t saved = enter_critical();
    enable_gpio_clock(pin->port);
    write_output_type(pin, output_type);
    __set_PRIMASK(saved);
    return PAL_GPIO_STATUS_OK;
}

PalGpioStatus PAL_GPIO_GetOutputType(PalGpioId id, PalGpioOutputType *output_type)
{
    const PalGpioStm32Pin *pin = find_pin(id);
    if (!pin) return PAL_GPIO_STATUS_INVALID_ID;
    if (!output_type) return PAL_GPIO_STATUS_INVALID_ARGUMENT;
    uint32_t saved = enter_critical();
    enable_gpio_clock(pin->port);
    *output_type = read_output_type(pin);
    __set_PRIMASK(saved);
    return PAL_GPIO_STATUS_OK;
}

PalGpioStatus PAL_GPIO_SetSpeed(PalGpioId id, PalGpioSpeed speed)
{
    const PalGpioStm32Pin *pin = find_pin(id);
    if (!pin) return PAL_GPIO_STATUS_INVALID_ID;
    if ((unsigned)speed > PAL_GPIO_SPEED_VERY_HIGH) return PAL_GPIO_STATUS_INVALID_ARGUMENT;
    uint32_t saved = enter_critical();
    enable_gpio_clock(pin->port);
    write_speed(pin, speed);
    __set_PRIMASK(saved);
    return PAL_GPIO_STATUS_OK;
}

PalGpioStatus PAL_GPIO_GetSpeed(PalGpioId id, PalGpioSpeed *speed)
{
    const PalGpioStm32Pin *pin = find_pin(id);
    if (!pin) return PAL_GPIO_STATUS_INVALID_ID;
    if (!speed) return PAL_GPIO_STATUS_INVALID_ARGUMENT;
    uint32_t saved = enter_critical();
    enable_gpio_clock(pin->port);
    *speed = read_speed(pin);
    __set_PRIMASK(saved);
    return PAL_GPIO_STATUS_OK;
}

PalGpioStatus PAL_GPIO_SetPull(PalGpioId id, PalGpioPull pull)
{
    const PalGpioStm32Pin *pin = find_pin(id);
    if (!pin) return PAL_GPIO_STATUS_INVALID_ID;
    if ((unsigned)pull > PAL_GPIO_PULL_DOWN) return PAL_GPIO_STATUS_INVALID_ARGUMENT;
    uint32_t saved = enter_critical();
    enable_gpio_clock(pin->port);
    write_pull(pin, pull);
    __set_PRIMASK(saved);
    return PAL_GPIO_STATUS_OK;
}

PalGpioStatus PAL_GPIO_GetPull(PalGpioId id, PalGpioPull *pull)
{
    const PalGpioStm32Pin *pin = find_pin(id);
    if (!pin) return PAL_GPIO_STATUS_INVALID_ID;
    if (!pull) return PAL_GPIO_STATUS_INVALID_ARGUMENT;
    uint32_t saved = enter_critical();
    enable_gpio_clock(pin->port);
    *pull = read_pull(pin);
    __set_PRIMASK(saved);
    return PAL_GPIO_STATUS_OK;
}

PalGpioStatus PAL_GPIO_SetAlternate(PalGpioId id, uint8_t alternate)
{
    const PalGpioStm32Pin *pin = find_pin(id);
    if (!pin) return PAL_GPIO_STATUS_INVALID_ID;
    if ((unsigned)alternate > 15u) return PAL_GPIO_STATUS_INVALID_ARGUMENT;
    uint32_t saved = enter_critical();
    enable_gpio_clock(pin->port);
    write_alternate(pin, alternate);
    __set_PRIMASK(saved);
    return PAL_GPIO_STATUS_OK;
}

PalGpioStatus PAL_GPIO_GetAlternate(PalGpioId id, uint8_t *alternate)
{
    const PalGpioStm32Pin *pin = find_pin(id);
    if (!pin) return PAL_GPIO_STATUS_INVALID_ID;
    if (!alternate) return PAL_GPIO_STATUS_INVALID_ARGUMENT;
    uint32_t saved = enter_critical();
    enable_gpio_clock(pin->port);
    *alternate = read_alternate(pin);
    __set_PRIMASK(saved);
    return PAL_GPIO_STATUS_OK;
}

PalGpioStatus PAL_GPIO_SetInterrupt(PalGpioId id, bool enabled, PalGpioEdge edge)
{
    const PalGpioStm32Pin *pin = find_pin(id);
    if (!pin) return PAL_GPIO_STATUS_INVALID_ID;
    if (enabled && (edge < PAL_GPIO_EDGE_RISING || edge > PAL_GPIO_EDGE_BOTH))
        return PAL_GPIO_STATUS_INVALID_ARGUMENT;
    uint32_t saved = enter_critical();
    enable_gpio_clock(pin->port);
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    PalGpioStatus status = check_interrupt(pin, read_mode(pin), enabled, edge);
    if (status == PAL_GPIO_STATUS_OK) apply_interrupt(pin, enabled, edge);
    __set_PRIMASK(saved);
    return status;
}

PalGpioStatus PAL_GPIO_GetInterrupt(PalGpioId id, bool *enabled, PalGpioEdge *edge)
{
    const PalGpioStm32Pin *pin = find_pin(id);
    if (!pin) return PAL_GPIO_STATUS_INVALID_ID;
    if (!enabled || !edge) return PAL_GPIO_STATUS_INVALID_ARGUMENT;
    uint32_t saved = enter_critical();
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    *enabled = interrupt_enabled(pin);
    *edge = *enabled ? line_edge(pin->pin) : PAL_GPIO_EDGE_NONE;
    __set_PRIMASK(saved);
    return PAL_GPIO_STATUS_OK;
}

PalGpioStatus PAL_GPIO_SetCallback(PalGpioId id, PalGpioCallback callback, void *context)
{
    if (!find_pin(id)) return PAL_GPIO_STATUS_INVALID_ID;
    uint32_t saved = enter_critical();
    callbacks[id] = (Callback){callback, context};
    __set_PRIMASK(saved);
    return PAL_GPIO_STATUS_OK;
}

void HAL_GPIO_EXTI_Callback(uint16_t mask)
{
    uint32_t saved = enter_critical();
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    for (size_t id = 0; id < BSP_GPIO_COUNT; ++id) {
        const PalGpioStm32Pin *pin = &bsp_gpio_map[id];
        if (pin->pin == mask && interrupt_enabled(pin)) {
            Callback callback = callbacks[id];
            __set_PRIMASK(saved);
            if (callback.function) callback.function((PalGpioId)id, callback.context);
            return;
        }
    }
    __set_PRIMASK(saved);
}

PalGpioStatus PAL_GPIO_Read(PalGpioId id, bool *high)
{
    const PalGpioStm32Pin *pin = find_pin(id);
    if (!pin) return PAL_GPIO_STATUS_INVALID_ID;
    if (!high) return PAL_GPIO_STATUS_INVALID_ARGUMENT;
    uint32_t saved = enter_critical();
    enable_gpio_clock(pin->port);
    *high = (pin->port->IDR & pin->pin) != 0u;
    __set_PRIMASK(saved);
    return PAL_GPIO_STATUS_OK;
}

static PalGpioStatus update_output(PalGpioId id, bool high, bool toggle)
{
    const PalGpioStm32Pin *pin = find_pin(id);
    if (!pin) return PAL_GPIO_STATUS_INVALID_ID;
    uint32_t saved = enter_critical();
    enable_gpio_clock(pin->port);
    if (read_mode(pin) != PAL_GPIO_OUTPUT) {
        __set_PRIMASK(saved);
        return PAL_GPIO_STATUS_INVALID_STATE;
    }
    if (toggle) high = (pin->port->ODR & pin->pin) == 0u;
    pin->port->BSRR = high ? pin->pin : ((uint32_t)pin->pin << 16);
    __set_PRIMASK(saved);
    return PAL_GPIO_STATUS_OK;
}

PalGpioStatus PAL_GPIO_Write(PalGpioId id, bool high)
{
    return update_output(id, high, false);
}

PalGpioStatus PAL_GPIO_Toggle(PalGpioId id)
{
    return update_output(id, false, true);
}
