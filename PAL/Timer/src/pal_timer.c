#include "pal_timer.h"
#include "pal_timer_port.h"

#include <stddef.h>

static PalTimerStatus lookup_bound(PalTimerId id, PalTimerDevice **device)
{
    *device = PAL_Timer_Port_Find(id);
    if (*device == NULL) return PAL_TIMER_STATUS_INVALID_ID;
    if ((*device)->binding == PAL_TIMER_UNBOUND) return PAL_TIMER_STATUS_NOT_INITIALIZED;
    return PAL_TIMER_STATUS_OK;
}

static PalTimerStatus bind(PalTimerId id, PalTimerBinding binding)
{
    PalTimerDevice *device = PAL_Timer_Port_Find(id);
    if (device == NULL) return PAL_TIMER_STATUS_INVALID_ID;
    if (device->binding != PAL_TIMER_UNBOUND) return PAL_TIMER_STATUS_ALREADY_INITIALIZED;

    PalTimerStatus status = binding == PAL_TIMER_OWNED ? PAL_Timer_Port_Init(device->port_context) :
        PAL_Timer_Port_AttachInitialized(device->port_context);
    if (status == PAL_TIMER_STATUS_OK) {
        device->binding = binding;
    }
    return status;
}

PalTimerStatus PAL_Timer_Init(PalTimerId id)
{
    return bind(id, PAL_TIMER_OWNED);
}

PalTimerStatus PAL_Timer_AttachInitialized(PalTimerId id)
{
    return bind(id, PAL_TIMER_ATTACHED);
}

static PalTimerStatus release(PalTimerId id, PalTimerBinding binding)
{
    PalTimerDevice *device;
    PalTimerStatus status = lookup_bound(id, &device);
    if (status != PAL_TIMER_STATUS_OK) return status;
    if (device->binding != binding) return PAL_TIMER_STATUS_INVALID_STATE;
    if (PAL_Timer_Port_Active(device->port_context) || device->update != NULL) return PAL_TIMER_STATUS_BUSY;
    for (unsigned i = 0; i < PAL_TIMER_CHANNEL_COUNT; ++i) {
        if (device->channels[i].enabled) return PAL_TIMER_STATUS_BUSY;
    }

    if (binding == PAL_TIMER_OWNED) {
        status = PAL_Timer_Port_DeInit(device->port_context);
        if (status != PAL_TIMER_STATUS_OK) return status;
    }
    *device = (PalTimerDevice){ .port_context = device->port_context };
    return PAL_TIMER_STATUS_OK;
}

PalTimerStatus PAL_Timer_DeInit(PalTimerId id)
{
    return release(id, PAL_TIMER_OWNED);
}

PalTimerStatus PAL_Timer_Detach(PalTimerId id)
{
    return release(id, PAL_TIMER_ATTACHED);
}

PalTimerStatus PAL_Timer_Reconfigure(PalTimerId id, const PalTimerTimebase *timebase)
{
    if (timebase == NULL || timebase->tick_hz == 0 || timebase->arr == 0) return PAL_TIMER_STATUS_INVALID_ARGUMENT;
    PalTimerDevice *device;
    PalTimerStatus status = lookup_bound(id, &device);
    if (status != PAL_TIMER_STATUS_OK) return status;
    if (PAL_Timer_Port_Active(device->port_context)) return PAL_TIMER_STATUS_BUSY;
    return PAL_Timer_Port_Configure(device->port_context, timebase);
}

PalTimerStatus PAL_Timer_GetTimebase(PalTimerId id, PalTimerTimebase *timebase)
{
    if (timebase == NULL) return PAL_TIMER_STATUS_INVALID_ARGUMENT;
    PalTimerDevice *device;
    PalTimerStatus status = lookup_bound(id, &device);
    if (status == PAL_TIMER_STATUS_OK) PAL_Timer_Port_GetTimebase(device->port_context, timebase);
    return status;
}

PalTimerStatus PAL_Timer_SetTickFrequency(PalTimerId id, uint32_t hz)
{
    PalTimerDevice *device;
    PalTimerStatus status = lookup_bound(id, &device);
    if (status != PAL_TIMER_STATUS_OK) return status;
    return PAL_Timer_Port_SetFrequency(device->port_context, hz);
}

PalTimerStatus PAL_Timer_ConfigureMasterTrigger(PalTimerId id, PalTimerMasterTriggerOutput source)
{
    if ((unsigned)source > PAL_TIMER_TRGO_COMPARE_PULSE) return PAL_TIMER_STATUS_INVALID_ARGUMENT;
    PalTimerDevice *device;
    PalTimerStatus status = lookup_bound(id, &device);
    if (status != PAL_TIMER_STATUS_OK) return status;
    if (PAL_Timer_Port_Active(device->port_context)) return PAL_TIMER_STATUS_BUSY;
    return PAL_Timer_Port_ConfigureMasterTrigger(device->port_context, source);
}

PalTimerStatus PAL_Timer_ConfigureSlave(PalTimerId id, PalTimerSlaveMode mode, PalTimerTriggerId trigger)
{
    if ((unsigned)mode > PAL_TIMER_TRIGGER_RESET ||
        (mode != PAL_TIMER_SLAVE_DISABLED && (unsigned)trigger > PAL_TIMER_ITR3)) return PAL_TIMER_STATUS_INVALID_ARGUMENT;
    PalTimerDevice *device;
    PalTimerStatus status = lookup_bound(id, &device);
    if (status != PAL_TIMER_STATUS_OK) return status;
    if (PAL_Timer_Port_Active(device->port_context)) return PAL_TIMER_STATUS_BUSY;
    return PAL_Timer_Port_ConfigureSlave(device->port_context, mode, trigger);
}

PalTimerStatus PAL_Timer_Start(PalTimerId id, bool clear_counter, PalTimerUpdateCallback callback, void *context)
{
    PalTimerDevice *device;
    PalTimerStatus status = lookup_bound(id, &device);
    if (status != PAL_TIMER_STATUS_OK) return status;
    uint32_t key = PAL_Timer_Port_EnterCritical();
    device->update = callback;
    device->update_context = context;
    PAL_Timer_Port_Start(device->port_context, clear_counter, callback != NULL);
    PAL_Timer_Port_ExitCritical(key);
    return PAL_TIMER_STATUS_OK;
}

PalTimerStatus PAL_Timer_Stop(PalTimerId id)
{
    PalTimerDevice *device;
    PalTimerStatus status = lookup_bound(id, &device);
    if (status != PAL_TIMER_STATUS_OK) return status;
    uint32_t key = PAL_Timer_Port_EnterCritical();
    PAL_Timer_Port_Stop(device->port_context);
    device->update = NULL;
    device->update_context = NULL;
    PAL_Timer_Port_ExitCritical(key);
    return PAL_TIMER_STATUS_OK;
}

PalTimerStatus PAL_Timer_ConfigureChannel(PalTimerId id, PalTimerChannel ch, PalTimerChannelConfig config)
{
    if (config.mode != PAL_TIMER_CHANNEL_MODE_OUTPUT_COMPARE && config.mode != PAL_TIMER_CHANNEL_MODE_INPUT_CAPTURE)
        return PAL_TIMER_STATUS_INVALID_ARGUMENT;
    if (config.mode == PAL_TIMER_CHANNEL_MODE_INPUT_CAPTURE && (unsigned)config.capture_edge - 1u >= PAL_TIMER_CAPTURE_BOTH)
        return PAL_TIMER_STATUS_INVALID_ARGUMENT;
    if ((unsigned)ch >= PAL_TIMER_CHANNEL_COUNT) return PAL_TIMER_STATUS_INVALID_ARGUMENT;
    PalTimerDevice *device;
    PalTimerStatus status = lookup_bound(id, &device);
    if (status != PAL_TIMER_STATUS_OK) return status;
    uint32_t key = PAL_Timer_Port_EnterCritical();
    status = PAL_Timer_Port_ConfigureChannel(device->port_context, ch, config);
    if (status == PAL_TIMER_STATUS_OK) device->channels[ch] = (PalTimerChannelState){0};
    PAL_Timer_Port_ExitCritical(key);
    return status;
}

PalTimerStatus PAL_Timer_StartChannel(PalTimerId id, PalTimerChannel ch, PalTimerChannelCallback callback, void *context)
{
    PalTimerDevice *device;
    PalTimerStatus status = lookup_bound(id, &device);
    if (status != PAL_TIMER_STATUS_OK) return status;
    if ((unsigned)ch >= PAL_TIMER_CHANNEL_COUNT) return PAL_TIMER_STATUS_INVALID_ARGUMENT;
    uint32_t key = PAL_Timer_Port_EnterCritical();
    device->channels[ch] = (PalTimerChannelState){ .callback = callback, .context = context, .enabled = true };
    PAL_Timer_Port_EnableChannel(device->port_context, ch, true, callback != NULL);
    PAL_Timer_Port_ExitCritical(key);
    return PAL_TIMER_STATUS_OK;
}

PalTimerStatus PAL_Timer_StopChannel(PalTimerId id, PalTimerChannel ch)
{
    PalTimerDevice *device;
    PalTimerStatus status = lookup_bound(id, &device);
    if (status != PAL_TIMER_STATUS_OK) return status;
    if ((unsigned)ch >= PAL_TIMER_CHANNEL_COUNT) return PAL_TIMER_STATUS_INVALID_ARGUMENT;
    uint32_t key = PAL_Timer_Port_EnterCritical();
    PAL_Timer_Port_EnableChannel(device->port_context, ch, false, false);
    device->channels[ch] = (PalTimerChannelState){0};
    PAL_Timer_Port_ExitCritical(key);
    return PAL_TIMER_STATUS_OK;
}

PalTimerStatus PAL_Timer_SetAutoReload(PalTimerId id, uint32_t arr, PalTimerApply apply)
{
    return PAL_Timer_Port_SetAutoReload(id, arr, apply);
}

PalTimerStatus PAL_Timer_GetCounter(PalTimerId id, uint32_t *value)
{
    return PAL_Timer_Port_GetCounter(id, value);
}

PalTimerStatus PAL_Timer_SetCounter(PalTimerId id, uint32_t value)
{
    return PAL_Timer_Port_SetCounter(id, value);
}

PalTimerStatus PAL_Timer_SetOnePulse(PalTimerId id, bool enable)
{
    return PAL_Timer_Port_SetOnePulse(id, enable);
}

PalTimerStatus PAL_Timer_GetCapture(PalTimerId id, PalTimerChannel channel, uint32_t *value)
{
    return PAL_Timer_Port_GetCapture(id, channel, value);
}

PalTimerStatus PAL_Timer_SetCompare(PalTimerId id, PalTimerChannel channel, uint32_t value)
{
    return PAL_Timer_Port_SetCompare(id, channel, value);
}
