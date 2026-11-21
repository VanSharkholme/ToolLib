#include "pal_gpio_host.h"
#include "pal_gpio_port.h"
#include <string.h>

typedef struct {
    PalGpioConfig config;
    bool latch;
    bool level;
    PalGpioCallback callback;
    void *context;
} HostPin;

static HostPin pins[PAL_GPIO_HOST_COUNT];
static const unsigned lines[PAL_GPIO_HOST_COUNT] = {2, 8, 3, 3};

void PAL_GPIO_Host_Reset(void)
{
    memset(pins, 0, sizeof pins);
}

static PalGpioStatus check_interrupt(PalGpioId id, PalGpioMode mode, bool enabled)
{
    if (!enabled) return PAL_GPIO_STATUS_OK;
    if (mode != PAL_GPIO_INPUT) return PAL_GPIO_STATUS_INVALID_STATE;
    for (PalGpioId other = 0; other < PAL_GPIO_HOST_COUNT; ++other) {
        if (other != id && lines[other] == lines[id] && pins[other].config.interrupt_enabled)
            return PAL_GPIO_STATUS_BUSY;
    }
    return PAL_GPIO_STATUS_OK;
}

PalGpioStatus PAL_GPIO_SetConfig(PalGpioId id, const PalGpioConfig *config)
{
    if (id >= PAL_GPIO_HOST_COUNT) return PAL_GPIO_STATUS_INVALID_ID;
    if (!valid_config(config)) return PAL_GPIO_STATUS_INVALID_ARGUMENT;
    PalGpioStatus status = check_interrupt(id, config->mode, config->interrupt_enabled);
    if (status != PAL_GPIO_STATUS_OK) return status;
    HostPin *pin = &pins[id];
    if (config->mode == PAL_GPIO_OUTPUT && pin->config.mode != PAL_GPIO_OUTPUT) pin->level = pin->latch;
    pin->config = *config;
    if (!config->interrupt_enabled) pin->config.edge = PAL_GPIO_EDGE_NONE;
    return PAL_GPIO_STATUS_OK;
}

PalGpioStatus PAL_GPIO_GetConfig(PalGpioId id, PalGpioConfig *config)
{
    if (id >= PAL_GPIO_HOST_COUNT) return PAL_GPIO_STATUS_INVALID_ID;
    if (!config) return PAL_GPIO_STATUS_INVALID_ARGUMENT;
    *config = pins[id].config;
    return PAL_GPIO_STATUS_OK;
}

PalGpioStatus PAL_GPIO_SetMode(PalGpioId id, PalGpioMode mode)
{
    if (id >= PAL_GPIO_HOST_COUNT) return PAL_GPIO_STATUS_INVALID_ID;
    if ((unsigned)mode > PAL_GPIO_ANALOG) return PAL_GPIO_STATUS_INVALID_ARGUMENT;
    if (mode != PAL_GPIO_INPUT && pins[id].config.interrupt_enabled) return PAL_GPIO_STATUS_BUSY;
    if (mode == PAL_GPIO_OUTPUT && pins[id].config.mode != PAL_GPIO_OUTPUT) pins[id].level = pins[id].latch;
    pins[id].config.mode = mode;
    return PAL_GPIO_STATUS_OK;
}

PalGpioStatus PAL_GPIO_GetMode(PalGpioId id, PalGpioMode *mode)
{
    if (id >= PAL_GPIO_HOST_COUNT) return PAL_GPIO_STATUS_INVALID_ID;
    if (!mode) return PAL_GPIO_STATUS_INVALID_ARGUMENT;
    *mode = pins[id].config.mode;
    return PAL_GPIO_STATUS_OK;
}

PalGpioStatus PAL_GPIO_SetOutputType(PalGpioId id, PalGpioOutputType output_type)
{
    if (id >= PAL_GPIO_HOST_COUNT) return PAL_GPIO_STATUS_INVALID_ID;
    if ((unsigned)output_type > PAL_GPIO_OPEN_DRAIN) return PAL_GPIO_STATUS_INVALID_ARGUMENT;
    pins[id].config.output_type = output_type;
    return PAL_GPIO_STATUS_OK;
}

PalGpioStatus PAL_GPIO_GetOutputType(PalGpioId id, PalGpioOutputType *output_type)
{
    if (id >= PAL_GPIO_HOST_COUNT) return PAL_GPIO_STATUS_INVALID_ID;
    if (!output_type) return PAL_GPIO_STATUS_INVALID_ARGUMENT;
    *output_type = pins[id].config.output_type;
    return PAL_GPIO_STATUS_OK;
}

PalGpioStatus PAL_GPIO_SetSpeed(PalGpioId id, PalGpioSpeed speed)
{
    if (id >= PAL_GPIO_HOST_COUNT) return PAL_GPIO_STATUS_INVALID_ID;
    if ((unsigned)speed > PAL_GPIO_SPEED_VERY_HIGH) return PAL_GPIO_STATUS_INVALID_ARGUMENT;
    pins[id].config.speed = speed;
    return PAL_GPIO_STATUS_OK;
}

PalGpioStatus PAL_GPIO_GetSpeed(PalGpioId id, PalGpioSpeed *speed)
{
    if (id >= PAL_GPIO_HOST_COUNT) return PAL_GPIO_STATUS_INVALID_ID;
    if (!speed) return PAL_GPIO_STATUS_INVALID_ARGUMENT;
    *speed = pins[id].config.speed;
    return PAL_GPIO_STATUS_OK;
}

PalGpioStatus PAL_GPIO_SetPull(PalGpioId id, PalGpioPull pull)
{
    if (id >= PAL_GPIO_HOST_COUNT) return PAL_GPIO_STATUS_INVALID_ID;
    if ((unsigned)pull > PAL_GPIO_PULL_DOWN) return PAL_GPIO_STATUS_INVALID_ARGUMENT;
    pins[id].config.pull = pull;
    return PAL_GPIO_STATUS_OK;
}

PalGpioStatus PAL_GPIO_GetPull(PalGpioId id, PalGpioPull *pull)
{
    if (id >= PAL_GPIO_HOST_COUNT) return PAL_GPIO_STATUS_INVALID_ID;
    if (!pull) return PAL_GPIO_STATUS_INVALID_ARGUMENT;
    *pull = pins[id].config.pull;
    return PAL_GPIO_STATUS_OK;
}

PalGpioStatus PAL_GPIO_SetAlternate(PalGpioId id, uint8_t alternate)
{
    if (id >= PAL_GPIO_HOST_COUNT) return PAL_GPIO_STATUS_INVALID_ID;
    if ((unsigned)alternate > 15u) return PAL_GPIO_STATUS_INVALID_ARGUMENT;
    pins[id].config.alternate = alternate;
    return PAL_GPIO_STATUS_OK;
}

PalGpioStatus PAL_GPIO_GetAlternate(PalGpioId id, uint8_t *alternate)
{
    if (id >= PAL_GPIO_HOST_COUNT) return PAL_GPIO_STATUS_INVALID_ID;
    if (!alternate) return PAL_GPIO_STATUS_INVALID_ARGUMENT;
    *alternate = pins[id].config.alternate;
    return PAL_GPIO_STATUS_OK;
}

PalGpioStatus PAL_GPIO_SetInterrupt(PalGpioId id, bool enabled, PalGpioEdge edge)
{
    if (id >= PAL_GPIO_HOST_COUNT) return PAL_GPIO_STATUS_INVALID_ID;
    if (enabled && (edge < PAL_GPIO_EDGE_RISING || edge > PAL_GPIO_EDGE_BOTH))
        return PAL_GPIO_STATUS_INVALID_ARGUMENT;
    PalGpioStatus status = check_interrupt(id, pins[id].config.mode, enabled);
    if (status != PAL_GPIO_STATUS_OK) return status;
    pins[id].config.interrupt_enabled = enabled;
    pins[id].config.edge = enabled ? edge : PAL_GPIO_EDGE_NONE;
    return PAL_GPIO_STATUS_OK;
}

PalGpioStatus PAL_GPIO_GetInterrupt(PalGpioId id, bool *enabled, PalGpioEdge *edge)
{
    if (id >= PAL_GPIO_HOST_COUNT) return PAL_GPIO_STATUS_INVALID_ID;
    if (!enabled || !edge) return PAL_GPIO_STATUS_INVALID_ARGUMENT;
    *enabled = pins[id].config.interrupt_enabled;
    *edge = pins[id].config.edge;
    return PAL_GPIO_STATUS_OK;
}

PalGpioStatus PAL_GPIO_SetCallback(PalGpioId id, PalGpioCallback callback, void *context)
{
    if (id >= PAL_GPIO_HOST_COUNT) return PAL_GPIO_STATUS_INVALID_ID;
    pins[id].callback = callback;
    pins[id].context = context;
    return PAL_GPIO_STATUS_OK;
}

PalGpioStatus PAL_GPIO_Read(PalGpioId id, bool *high)
{
    if (id >= PAL_GPIO_HOST_COUNT) return PAL_GPIO_STATUS_INVALID_ID;
    if (!high) return PAL_GPIO_STATUS_INVALID_ARGUMENT;
    *high = pins[id].level;
    return PAL_GPIO_STATUS_OK;
}

PalGpioStatus PAL_GPIO_Write(PalGpioId id, bool high)
{
    if (id >= PAL_GPIO_HOST_COUNT) return PAL_GPIO_STATUS_INVALID_ID;
    if (pins[id].config.mode != PAL_GPIO_OUTPUT) return PAL_GPIO_STATUS_INVALID_STATE;
    pins[id].latch = high;
    pins[id].level = high;
    return PAL_GPIO_STATUS_OK;
}

PalGpioStatus PAL_GPIO_Toggle(PalGpioId id)
{
    if (id >= PAL_GPIO_HOST_COUNT) return PAL_GPIO_STATUS_INVALID_ID;
    return PAL_GPIO_Write(id, !pins[id].latch);
}

PalGpioStatus PAL_GPIO_Host_SetLevel(PalGpioId id, bool high)
{
    if (id >= PAL_GPIO_HOST_COUNT) return PAL_GPIO_STATUS_INVALID_ID;
    HostPin *pin = &pins[id];
    bool changed = pin->level != high;
    pin->level = high;
    PalGpioEdge edge = high ? PAL_GPIO_EDGE_RISING : PAL_GPIO_EDGE_FALLING;
    if (changed && pin->config.interrupt_enabled && (pin->config.edge & edge) && pin->callback)
        pin->callback(id, pin->context);
    return PAL_GPIO_STATUS_OK;
}
