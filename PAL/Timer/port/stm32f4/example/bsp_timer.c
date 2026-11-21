#include "bsp_timer.h"
#include <stddef.h>

/* The first TIM2 CH1 match after Start produces the TIM3 start trigger. */
PalTimerStatus BSP_TIMER_ArmFromTim2(PalTimerUpdateCallback callback, void *context)
{
    PalTimerStatus status = PAL_Timer_ConfigureMasterTrigger(BSP_TIMER_CAPTURE, PAL_TIMER_TRGO_COMPARE_PULSE);
    if (status != PAL_TIMER_STATUS_OK) return status;
    PalTimerChannelConfig compare = {.mode = PAL_TIMER_CHANNEL_MODE_OUTPUT_COMPARE, .compare_value = 100u};
    status = PAL_Timer_ConfigureChannel(BSP_TIMER_CAPTURE, PAL_TIMER_CHANNEL_1, compare);
    if (status != PAL_TIMER_STATUS_OK) return status;
    status = PAL_Timer_ConfigureSlave(BSP_TIMER_ONE_SHOT, PAL_TIMER_TRIGGER_START, PAL_TIMER_ITR1);
    if (status != PAL_TIMER_STATUS_OK) return status;
    status = PAL_Timer_SetOnePulse(BSP_TIMER_ONE_SHOT, true);
    if (status != PAL_TIMER_STATUS_OK) return status;
    status = PAL_Timer_StartChannel(BSP_TIMER_CAPTURE, PAL_TIMER_CHANNEL_1, NULL, NULL);
    if (status != PAL_TIMER_STATUS_OK) return status;
    status = PAL_Timer_Start(BSP_TIMER_ONE_SHOT, PAL_TIMER_CLEAR_COUNTER, callback, context);
    if (status == PAL_TIMER_STATUS_OK) {
        status = PAL_Timer_Start(BSP_TIMER_CAPTURE, PAL_TIMER_CLEAR_COUNTER, NULL, NULL);
        if (status == PAL_TIMER_STATUS_OK) return status;
        PAL_Timer_Stop(BSP_TIMER_ONE_SHOT);
    }
    PAL_Timer_StopChannel(BSP_TIMER_CAPTURE, PAL_TIMER_CHANNEL_1);
    return status;
}
