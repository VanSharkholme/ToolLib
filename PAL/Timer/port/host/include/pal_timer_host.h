#ifndef TOOLLIB_PAL_TIMER_HOST_H
#define TOOLLIB_PAL_TIMER_HOST_H

#include "pal_timer_types.h"

enum { PAL_TIMER_HOST_32BIT = 2, PAL_TIMER_HOST_16BIT = 7 };

PalTimerMasterTriggerOutput PAL_Timer_Host_MasterTrigger(PalTimerId id);
void PAL_Timer_Host_Trigger(PalTimerId id, PalTimerTriggerId trigger);
uint32_t PAL_Timer_Host_ActiveFrequency(PalTimerId id);
void PAL_Timer_Host_Reset(void);
PalTimerStatus PAL_Timer_Host_ExternalInit(PalTimerId id, const PalTimerTimebase *timebase);
/* Advance virtual time and dispatch compare and update events synchronously. */
PalTimerStatus PAL_Timer_Host_Advance(PalTimerId id, uint64_t ticks);
/* Latch an input transition at the current CNT. Dispatch is separate so tests
 * can model several edges arriving while the capture IRQ is pending. */
PalTimerStatus PAL_Timer_Host_LatchEdge(PalTimerId id, PalTimerChannel channel, PalTimerCaptureEdge edge);
PalTimerStatus PAL_Timer_Host_DispatchCaptures(PalTimerId id);

#endif
