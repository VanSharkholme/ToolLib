#include "pal_timer_host.h"
#include "pal_timer_port.h"

#include <stddef.h>

typedef struct {
    uint32_t ccr;
    PalTimerCaptureEdge edge;
    bool pending;
    bool notify;
} HostChannel;

typedef struct {
    PalTimerDevice device;
    uint32_t width_mask;
    bool initialized;
    bool running;
    bool armed;
    bool one_pulse;
    PalTimerSlaveMode slave;
    PalTimerTriggerId itr;
    PalTimerMasterTriggerOutput trgo;
    uint32_t counter;
    uint32_t arr;
    uint32_t programmed_arr;
    uint32_t hz;
    uint32_t programmed_hz;
    HostChannel channels[PAL_TIMER_CHANNEL_COUNT];
} HostTimer;

static HostTimer timers[PAL_TIMER8 + 1] = {
    [PAL_TIMER_HOST_16BIT] = { .width_mask = UINT16_MAX },
    [PAL_TIMER_HOST_32BIT] = { .width_mask = UINT32_MAX },
};

void PAL_Timer_Host_Reset(void)
{
    timers[PAL_TIMER_HOST_16BIT] = (HostTimer){ .width_mask = UINT16_MAX };
    timers[PAL_TIMER_HOST_32BIT] = (HostTimer){ .width_mask = UINT32_MAX };
}

PalTimerDevice *PAL_Timer_Port_Find(PalTimerId id)
{
    if ((unsigned)id >= sizeof timers / sizeof timers[0] || !timers[id].width_mask) return NULL;
    timers[id].device.port_context = &timers[id];
    return &timers[id].device;
}

uint32_t PAL_Timer_Port_EnterCritical(void) { return 0; }
void PAL_Timer_Port_ExitCritical(uint32_t state) { (void)state; }

PalTimerStatus PAL_Timer_Port_SetFrequency(void *context, uint32_t hz)
{
    HostTimer *timer = context;
    const uint32_t clock = 84000000u;
    if (!hz || clock % hz || clock / hz > 65536u) return PAL_TIMER_STATUS_UNSUPPORTED;
    timer->programmed_hz = hz;
    return PAL_TIMER_STATUS_OK;
}

PalTimerStatus PAL_Timer_Port_Configure(void *context, const PalTimerTimebase *base)
{
    HostTimer *timer = context;
    if (base->arr == 0 || base->arr > timer->width_mask) return PAL_TIMER_STATUS_UNSUPPORTED;
    PalTimerStatus status = PAL_Timer_Port_SetFrequency(timer, base->tick_hz);
    if (status != PAL_TIMER_STATUS_OK) return status;
    timer->arr = timer->programmed_arr = base->arr;
    timer->hz = timer->programmed_hz;
    timer->counter = 0;
    return PAL_TIMER_STATUS_OK;
}

PalTimerStatus PAL_Timer_Host_ExternalInit(PalTimerId id, const PalTimerTimebase *base)
{
    if (!PAL_Timer_Port_Find(id)) return PAL_TIMER_STATUS_INVALID_ID;
    if (!base) return PAL_TIMER_STATUS_INVALID_ARGUMENT;
    HostTimer *timer = &timers[id];
    if (timer->initialized) return PAL_TIMER_STATUS_BUSY;
    PalTimerStatus status = PAL_Timer_Port_Configure(timer, base);
    if (status == PAL_TIMER_STATUS_OK) timer->initialized = true;
    return status;
}

PalTimerStatus PAL_Timer_Port_Init(void *context)
{
    HostTimer *timer = context;
    if (timer->initialized) return PAL_TIMER_STATUS_BUSY;
    const PalTimerTimebase defaults = {1000000u, 9999u};
    timer->initialized = true;
    return PAL_Timer_Port_Configure(timer, &defaults);
}

PalTimerStatus PAL_Timer_Port_AttachInitialized(void *context)
{
    HostTimer *timer = context;
    if (!timer->initialized) return PAL_TIMER_STATUS_INVALID_STATE;
    timer->slave = PAL_TIMER_SLAVE_DISABLED;
    return PAL_TIMER_STATUS_OK;
}

PalTimerStatus PAL_Timer_Port_DeInit(void *context)
{
    HostTimer *timer = context;
    *timer = (HostTimer){ .width_mask = timer->width_mask, .device.port_context = context };
    return PAL_TIMER_STATUS_OK;
}

void PAL_Timer_Port_GetTimebase(void *context, PalTimerTimebase *base)
{
    const HostTimer *timer = context;
    *base = (PalTimerTimebase){timer->programmed_hz, timer->programmed_arr};
}

uint32_t PAL_Timer_Host_ActiveFrequency(PalTimerId id) { return timers[id].hz; }

PalTimerStatus PAL_Timer_Port_SetAutoReload(PalTimerId id, uint32_t arr, PalTimerApply apply)
{
    if ((unsigned)id >= sizeof timers / sizeof timers[0] || !timers[id].width_mask) return PAL_TIMER_STATUS_INVALID_ID;
    if ((unsigned)apply > PAL_TIMER_ON_UPDATE) return PAL_TIMER_STATUS_INVALID_ARGUMENT;
    HostTimer *timer = &timers[id];
    timer->programmed_arr = arr & timer->width_mask;
    if (apply == PAL_TIMER_IMMEDIATE) timer->arr = timer->programmed_arr;
    return PAL_TIMER_STATUS_OK;
}

PalTimerStatus PAL_Timer_Port_GetCounter(PalTimerId id, uint32_t *value)
{
    if ((unsigned)id >= sizeof timers / sizeof timers[0] || !timers[id].width_mask) return PAL_TIMER_STATUS_INVALID_ID;
    if (value == NULL) return PAL_TIMER_STATUS_INVALID_ARGUMENT;
    *value = timers[id].counter;
    return PAL_TIMER_STATUS_OK;
}
PalTimerStatus PAL_Timer_Port_SetCounter(PalTimerId id, uint32_t value)
{
    if ((unsigned)id >= sizeof timers / sizeof timers[0] || !timers[id].width_mask) return PAL_TIMER_STATUS_INVALID_ID;
    timers[id].counter = value & timers[id].width_mask;
    return PAL_TIMER_STATUS_OK;
}
PalTimerStatus PAL_Timer_Port_SetOnePulse(PalTimerId id, bool enable)
{
    if ((unsigned)id >= sizeof timers / sizeof timers[0] || !timers[id].width_mask) return PAL_TIMER_STATUS_INVALID_ID;
    timers[id].one_pulse = enable;
    return PAL_TIMER_STATUS_OK;
}

bool PAL_Timer_Port_Active(void *context)
{
    const HostTimer *timer = context;
    return timer->running || timer->armed;
}

PalTimerStatus PAL_Timer_Port_ConfigureMasterTrigger(void *context, PalTimerMasterTriggerOutput source)
{
    HostTimer *timer = context;
    timer->trgo = source;
    return PAL_TIMER_STATUS_OK;
}

PalTimerMasterTriggerOutput PAL_Timer_Host_MasterTrigger(PalTimerId id)
{
    return timers[id].trgo;
}

PalTimerStatus PAL_Timer_Port_ConfigureSlave(void *context, PalTimerSlaveMode mode, PalTimerTriggerId trigger)
{
    HostTimer *timer = context;
    if (mode != PAL_TIMER_SLAVE_DISABLED) timer->itr = trigger;
    timer->slave = mode;
    return PAL_TIMER_STATUS_OK;
}

void PAL_Timer_Port_Start(void *context, bool clear_counter, bool notify)
{
    HostTimer *timer = context;
    (void)notify;
    if (clear_counter) timer->counter = 0;
    timer->armed = timer->slave != PAL_TIMER_SLAVE_DISABLED;
    timer->running = timer->slave != PAL_TIMER_TRIGGER_START;
}

void PAL_Timer_Port_Stop(void *context)
{
    HostTimer *timer = context;
    timer->running = timer->armed = false;
}

void PAL_Timer_Host_Trigger(PalTimerId id, PalTimerTriggerId trigger)
{
    HostTimer *timer = &timers[id];
    if (!timer->armed || trigger != timer->itr) return;
    if (timer->slave == PAL_TIMER_TRIGGER_START) timer->running = true;
    else timer->counter = 0;
}

PalTimerStatus PAL_Timer_Port_ConfigureChannel(void *context, PalTimerChannel ch, PalTimerChannelConfig config)
{
    HostTimer *timer = context;
    HostChannel *channel = &timer->channels[ch];
    if (config.mode == PAL_TIMER_CHANNEL_MODE_INPUT_CAPTURE) {
        channel->edge = config.capture_edge;
    } else {
        channel->edge = 0;
        channel->ccr = config.compare_value & timer->width_mask;
    }
    channel->pending = false;
    channel->notify = false;
    return PAL_TIMER_STATUS_OK;
}

void PAL_Timer_Port_SetChannelInterrupt(void *context, PalTimerChannel ch, bool notify)
{
    HostTimer *timer = context;
    timer->channels[ch].notify = notify;
}

void PAL_Timer_Port_EnableChannel(void *context, PalTimerChannel ch, bool enable, bool notify)
{
    HostTimer *timer = context;
    timer->channels[ch].notify = enable && notify;
    timer->channels[ch].pending = false;
}

PalTimerStatus PAL_Timer_Port_SetCompare(PalTimerId id, PalTimerChannel ch, uint32_t value)
{
    if ((unsigned)id >= sizeof timers / sizeof timers[0] || !timers[id].width_mask) return PAL_TIMER_STATUS_INVALID_ID;
    if ((unsigned)ch >= PAL_TIMER_CHANNEL_COUNT) return PAL_TIMER_STATUS_INVALID_ARGUMENT;
    timers[id].channels[ch].ccr = value & timers[id].width_mask;
    return PAL_TIMER_STATUS_OK;
}

PalTimerStatus PAL_Timer_Port_GetCapture(PalTimerId id, PalTimerChannel ch, uint32_t *value)
{
    if ((unsigned)id >= sizeof timers / sizeof timers[0] || !timers[id].width_mask) return PAL_TIMER_STATUS_INVALID_ID;
    if (value == NULL) return PAL_TIMER_STATUS_INVALID_ARGUMENT;
    if ((unsigned)ch >= PAL_TIMER_CHANNEL_COUNT) return PAL_TIMER_STATUS_INVALID_ARGUMENT;
    HostChannel *channel = &timers[id].channels[ch];
    channel->pending = false;
    *value = channel->ccr;
    return PAL_TIMER_STATUS_OK;
}

PalTimerStatus PAL_Timer_Host_LatchEdge(PalTimerId id, PalTimerChannel ch, PalTimerCaptureEdge edge)
{
    if (!PAL_Timer_Port_Find(id)) return PAL_TIMER_STATUS_INVALID_ID;
    if (!timers[id].initialized) return PAL_TIMER_STATUS_NOT_INITIALIZED;
    if ((unsigned)ch >= PAL_TIMER_CHANNEL_COUNT ||
        (edge != PAL_TIMER_CAPTURE_RISING && edge != PAL_TIMER_CAPTURE_FALLING)) return PAL_TIMER_STATUS_INVALID_ARGUMENT;
    HostTimer *timer = &timers[id];
    HostChannel *channel = &timer->channels[ch];
    if (timer->device.channels[ch].enabled && (channel->edge == edge || channel->edge == PAL_TIMER_CAPTURE_BOTH)) {
        channel->ccr = timer->counter;
        channel->pending = true;
    }
    return PAL_TIMER_STATUS_OK;
}

static void dispatch_channels(HostTimer *timer, PalTimerId id)
{
    for (unsigned i = 0; i < PAL_TIMER_CHANNEL_COUNT; ++i) {
        HostChannel *channel = &timer->channels[i];
        const PalTimerChannelState *state = &timer->device.channels[i];
        if (channel->pending && channel->notify && state->callback) {
            channel->pending = false;
            state->callback(id, (PalTimerChannel)i, state->context);
        }
    }
}

PalTimerStatus PAL_Timer_Host_DispatchCaptures(PalTimerId id)
{
    if (!PAL_Timer_Port_Find(id)) return PAL_TIMER_STATUS_INVALID_ID;
    if (!timers[id].initialized) return PAL_TIMER_STATUS_NOT_INITIALIZED;
    dispatch_channels(&timers[id], id);
    return PAL_TIMER_STATUS_OK;
}

PalTimerStatus PAL_Timer_Host_Advance(PalTimerId id, uint64_t ticks)
{
    if (!PAL_Timer_Port_Find(id)) return PAL_TIMER_STATUS_INVALID_ID;
    HostTimer *timer = &timers[id];
    if (!timer->initialized) return PAL_TIMER_STATUS_NOT_INITIALIZED;
    while (ticks && timer->running && timer->arr) {
        uint32_t top = timer->counter <= timer->arr ? timer->arr : timer->width_mask;
        uint64_t wrap = (uint64_t)(top - timer->counter) + 1u;
        uint64_t step = ticks < wrap ? ticks : wrap;
        for (unsigned i = 0; i < PAL_TIMER_CHANNEL_COUNT; ++i) {
            const HostChannel *channel = &timer->channels[i];
            if (!channel->edge && timer->device.channels[i].enabled && channel->ccr > timer->counter) {
                uint32_t distance = channel->ccr - timer->counter;
                if (distance < step) step = distance;
            }
        }
        ticks -= step;
        bool update = step == wrap;
        timer->counter = update ? 0u : timer->counter + (uint32_t)step;
        if (update) {
            timer->arr = timer->programmed_arr;
            timer->hz = timer->programmed_hz;
            if (timer->one_pulse) timer->running = false;
        }
        for (unsigned i = 0; i < PAL_TIMER_CHANNEL_COUNT; ++i) {
            HostChannel *channel = &timer->channels[i];
            if (timer->device.channels[i].enabled && !channel->edge &&
                (channel->ccr == timer->counter || (update && channel->ccr > timer->arr))) channel->pending = true;
        }
        dispatch_channels(timer, id);
        if (update && timer->device.update) timer->device.update(id, timer->device.update_context);
    }
    return PAL_TIMER_STATUS_OK;
}
