# Timer PAL

Bind a timer with `PAL_Timer_Init` (board defaults) or attach an initialized,
stopped, disarmed timer with `PAL_Timer_AttachInitialized`. Pair them with
DeInit and Detach respectively. Stop the counter and enabled channels before
releasing the binding. The caller serializes access to each timer.

## Counter and channels

All public timer operations return PalTimerStatus. GetCounter(id, &value) and
GetCapture(id, channel, &value) write the result on success, including zero.
A failed read leaves the output unchanged. A successful capture read reports
the current CCR value. Observe channel notifications to detect new edges.

SetAutoReload takes the raw, inclusive ARR. Select PAL_TIMER_IMMEDIATE or
PAL_TIMER_ON_UPDATE. A running upcounter already above a newly lowered ARR
can count to its hardware width limit before wrapping. ARR zero blocks counting
on F4. SetTickFrequency takes the desired counter Hz and requires an exact,
representable prescaler. The new divider takes effect on the next hardware
update. GetTimebase reads programmed settings including pending writes.
PalTimerTimebase.arr holds the raw, inclusive ARR (9999 for 10000 ticks).
Reconfigure loads ARR and PSC together and resets CNT while stopped.

Start selects KEEP_COUNTER or CLEAR_COUNTER and accepts a nullable update
callback plus context. CLEAR_COUNTER writes only CNT. Update notification occurs
on natural rollover. SetOnePulse enables hardware stop on update, including when
interrupts are disabled. Stop disarms triggers and freezes CNT. Enabled capture
channels can still latch the frozen count.

ConfigureChannel takes a PalTimerChannelConfig by value. Input capture uses
its capture_edge, and output compare loads its initial compare_value into CCR.
The configured channel starts disabled. StartChannel
enables it with an optional callback. StopChannel preserves the configuration.
GetCapture reads the latest CCR, which is meaningful after the first edge.
Reading CCR acknowledges a pending capture on F4. Dual-edge mode captures both
polarities. IRQ flags can coalesce. SetCompare sets a persistent timing-only
match immediately. Keep CCR within the active ARR for ordinary matches; F4 also
sets the compare flag at overflow if CCR exceeds ARR. Callbacks may read CNT or
CCR, write the next compare point or stop their own channel.

## STM32F401

Select TOOLLIB_TIMER_PORT=stm32f4 and link pal_timer. The board table maps
PAL_TIMER1 to TIM2 and PAL_TIMER2 to TIM3. BSP_Timer_Get_Channel_Support supplies per-channel capabilities at configuration time.
TIM2 CH1 and CH4 support timing-only compare, and CH2 and CH3 support capture
including both edges. TIM3 supports timing-only compare on CH1–4. The TIM2 and TIM3 interrupt
vectors call `HAL_TIM_IRQHandler` with their respective HAL handles.

The application's `HAL_TIM_PeriodElapsedCallback` calls `HAL_IncTick()` for
TIM10 and forwards PAL timer updates to `PAL_Timer_STM32_Update_Callback(htim)`.
The Port implements `HAL_TIM_IC_CaptureCallback` and
`HAL_TIM_OC_DelayElapsedCallback` to dispatch channel notifications. HAL clears
CCxIF before calling them. The capture callback clears the channel's overcapture
flag (CCxOF) before notifying the user, preserving any newly pending CCxIF.

ConfigureMasterTrigger selects the master TRGO source while stopped and disarmed:
software UG (RESET), counter enable, update event or CH1 compare pulse. The
compare pulse works with channel interrupts disabled. Configure CH1 separately.

ConfigureSlave selects disabled, trigger-start or reset while stopped and
disarmed. Select the destination timer's PAL_TIMER_ITR0 through PAL_TIMER_ITR3 input.
On F401, TIM3 ITR1 receives TIM2 TRGO, and TIM2 ITR2 receives TIM3 TRGO.
TIM2 ITR1 is reserved. Arm the slave before starting the master. Start arms a trigger-start
timer; a trigger starts the counter and subsequent triggers during that run
leave CNT alone. OPM expiry leaves the trigger armed for another shot. In reset
mode Start begins counting and each trigger resets CNT. Stop disarms either
mode, retaining the route for the next Start. Disable slave mode after Stop to
restore software start. CubeMX attachment requires SMS disabled at handoff;
use ConfigureSlave to select the route after binding.

## Host

The host port supplies 16- and 32-bit virtual timers. Host_Advance advances in
counter ticks. Host_ActiveFrequency exposes the active prescaler result for
tests. Host_Trigger injects a specific ITR at the destination; only the selected input
acts on an armed timer. Master events are not routed automatically.
Host_LatchEdge and Host_DispatchCaptures allow tests to separate a hardware
edge from IRQ delivery.
