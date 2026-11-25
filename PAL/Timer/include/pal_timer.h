#ifndef TOOLLIB_PAL_TIMER_H
#define TOOLLIB_PAL_TIMER_H

#include "pal_timer_types.h"

/* Exclusive binding. The caller serializes operations on each timer. */
PalTimerStatus PAL_Timer_Init(PalTimerId id);
PalTimerStatus PAL_Timer_AttachInitialized(PalTimerId id);
PalTimerStatus PAL_Timer_DeInit(PalTimerId id);
PalTimerStatus PAL_Timer_Detach(PalTimerId id);

/* Reconfigure a stopped, disarmed base and load both settings before starting.
 * GetTimebase reports the programmed values, including pending preloads. */
PalTimerStatus PAL_Timer_Reconfigure(PalTimerId id, const PalTimerTimebase *timebase);
PalTimerStatus PAL_Timer_GetTimebase(PalTimerId id, PalTimerTimebase *timebase);
PalTimerStatus PAL_Timer_SetTickFrequency(PalTimerId id, uint32_t hz);

/* Fast operations require a bound ID and register-width values. ARR is the
 * inclusive maximum. Use nonzero ARR for counting. IMMEDIATE preserves CNT
 * even when the new ARR is below it. Reads store the register value on OK;
 * on error the output is untouched. A deferred write returns OK when staged. */
PalTimerStatus PAL_Timer_SetAutoReload(PalTimerId id, uint32_t arr, PalTimerApply apply);
PalTimerStatus PAL_Timer_GetCounter(PalTimerId id, uint32_t *value);
PalTimerStatus PAL_Timer_SetCounter(PalTimerId id, uint32_t value);
PalTimerStatus PAL_Timer_SetOnePulse(PalTimerId id, bool enable);

/* Start from a stopped/disarmed counter. CLEAR writes CNT only. In trigger
 * start mode Start arms the input and waits. Stop disarms and preserves CNT,
 * all channel enables, pending channel events and channel callbacks. */
PalTimerStatus PAL_Timer_Start(PalTimerId id, bool clear_counter, PalTimerUpdateCallback callback, void *context);
PalTimerStatus PAL_Timer_Stop(PalTimerId id);
/* Configure a stopped, disarmed timer. The master selects its TRGO signal.
 * The slave selects a local ITR input, ignored in DISABLED mode. */
PalTimerStatus PAL_Timer_ConfigureMasterTrigger(PalTimerId id, PalTimerMasterTriggerOutput source);
PalTimerStatus PAL_Timer_ConfigureSlave(PalTimerId id, PalTimerSlaveMode mode, PalTimerTriggerId trigger);

/* Successful configuration stops this channel and clears its saved callback. */
PalTimerStatus PAL_Timer_ConfigureChannel(PalTimerId id, PalTimerChannel channel, PalTimerChannelConfig config);
/* Save callback and context together. On an enabled channel, NULL masks the
 * channel interrupt. A pending event may be delivered to the replacement. */
PalTimerStatus PAL_Timer_SetChannelCallback(PalTimerId id, PalTimerChannel channel, PalTimerChannelCallback callback, void *context);
/* Start uses the saved callback. Stop retains it for the next start. */
PalTimerStatus PAL_Timer_StartChannel(PalTimerId id, PalTimerChannel channel);
PalTimerStatus PAL_Timer_StopChannel(PalTimerId id, PalTimerChannel channel);

/* Valid configured channel required. Capture reads the latest hardware latch
 * (on F4 reading it acknowledges CCxIF). Compare writes take effect immediately.
 * IRQ callbacks may read CNT/CCR, change CCR or stop their own channel. */
PalTimerStatus PAL_Timer_GetCapture(PalTimerId id, PalTimerChannel channel, uint32_t *value);
PalTimerStatus PAL_Timer_SetCompare(PalTimerId id, PalTimerChannel channel, uint32_t value);

#endif
