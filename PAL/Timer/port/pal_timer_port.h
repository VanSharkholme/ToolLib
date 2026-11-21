#ifndef TOOLLIB_PAL_TIMER_PORT_H
#define TOOLLIB_PAL_TIMER_PORT_H

#include "pal_timer.h"

typedef enum { PAL_TIMER_UNBOUND, PAL_TIMER_OWNED, PAL_TIMER_ATTACHED } PalTimerBinding;
typedef struct {
    PalTimerChannelCallback callback;
    void *context;
    bool enabled;
} PalTimerChannelState;

typedef struct {
    void *port_context;
    PalTimerBinding binding;
    PalTimerUpdateCallback update;
    void *update_context;
    PalTimerChannelState channels[PAL_TIMER_CHANNEL_COUNT];
} PalTimerDevice;

PalTimerDevice *PAL_Timer_Port_Find(PalTimerId id);
uint32_t PAL_Timer_Port_EnterCritical(void);
void PAL_Timer_Port_ExitCritical(uint32_t state);
PalTimerStatus PAL_Timer_Port_Init(void *context);
PalTimerStatus PAL_Timer_Port_AttachInitialized(void *context);
PalTimerStatus PAL_Timer_Port_DeInit(void *context);
bool PAL_Timer_Port_Active(void *context);
PalTimerStatus PAL_Timer_Port_Configure(void *context, const PalTimerTimebase *timebase);
void PAL_Timer_Port_GetTimebase(void *context, PalTimerTimebase *timebase);
PalTimerStatus PAL_Timer_Port_SetFrequency(void *context, uint32_t hz);
PalTimerStatus PAL_Timer_Port_ConfigureMasterTrigger(void *context, PalTimerMasterTriggerOutput source);
PalTimerStatus PAL_Timer_Port_ConfigureSlave(void *context, PalTimerSlaveMode mode, PalTimerTriggerId trigger);
void PAL_Timer_Port_Start(void *context, bool clear_counter, bool notify);
void PAL_Timer_Port_Stop(void *context);
/* Validate hardware capabilities before disabling the channel. */
PalTimerStatus PAL_Timer_Port_ConfigureChannel(void *context, PalTimerChannel channel, PalTimerChannelConfig config);
void PAL_Timer_Port_EnableChannel(void *context, PalTimerChannel channel, bool enable, bool notify);

PalTimerStatus PAL_Timer_Port_SetAutoReload(PalTimerId id, uint32_t arr, PalTimerApply apply);
PalTimerStatus PAL_Timer_Port_GetCounter(PalTimerId id, uint32_t *value);
PalTimerStatus PAL_Timer_Port_SetCounter(PalTimerId id, uint32_t value);
PalTimerStatus PAL_Timer_Port_SetOnePulse(PalTimerId id, bool enable);
PalTimerStatus PAL_Timer_Port_GetCapture(PalTimerId id, PalTimerChannel channel, uint32_t *value);
PalTimerStatus PAL_Timer_Port_SetCompare(PalTimerId id, PalTimerChannel channel, uint32_t value);
#endif
