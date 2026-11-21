#ifndef TOOLLIB_EXAMPLE_BSP_TIMER_H
#define TOOLLIB_EXAMPLE_BSP_TIMER_H

#include "pal_timer.h"

#define BSP_TIMER_NUM 2
#define BSP_TIMER_CAPTURE PAL_TIMER1  /* TIM2 */
#define BSP_TIMER_ONE_SHOT PAL_TIMER2 /* TIM3 */

/* Both timers must be bound, stopped and disarmed. TIM2 uses software start,
 * ARR >= 100 and CH1 for the compare pulse. */
PalTimerStatus BSP_TIMER_ArmFromTim2(PalTimerUpdateCallback callback, void *context);

#endif
