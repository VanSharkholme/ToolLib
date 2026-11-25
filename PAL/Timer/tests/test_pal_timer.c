#include "pal_timer.h"
#include "pal_timer_host.h"
#include "test_runner.h"

static const PalTimerId id = PAL_TIMER_HOST_16BIT;
static const PalTimerChannel ic = PAL_TIMER_CHANNEL_1, oc = PAL_TIMER_CHANNEL_2;
static unsigned updates, matches;
static uint32_t sample;

#define OK(call) TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_OK, (call))

void setUp(void)
{
    PAL_Timer_Host_Reset();
    updates = matches = sample = 0;
}
void tearDown(void) {}

static void update(PalTimerId timer, void *context)
{
    TEST_ASSERT_EQUAL_UINT(id, timer);
    ++*(unsigned *)context;
}

static void capture(PalTimerId timer, PalTimerChannel channel, void *context)
{
    OK(PAL_Timer_GetCapture(timer, channel, context));
}

static void match(PalTimerId timer, PalTimerChannel channel, void *context)
{
    (void)timer;
    (void)channel;
    ++*(unsigned *)context;
}

static void assert_counter(PalTimerId timer, uint32_t expected)
{
    uint32_t value = UINT32_MAX;
    OK(PAL_Timer_GetCounter(timer, &value));
    TEST_ASSERT_EQUAL_UINT32(expected, value);
}

static void assert_capture(PalTimerId timer, PalTimerChannel channel, uint32_t expected)
{
    uint32_t value = UINT32_MAX;
    OK(PAL_Timer_GetCapture(timer, channel, &value));
    TEST_ASSERT_EQUAL_UINT32(expected, value);
}

static void advance(uint64_t ticks)
{
    OK(PAL_Timer_Host_Advance(id, ticks));
}

static void test_independent(void)
{
    OK(PAL_Timer_Init(id));
    OK(PAL_Timer_SetAutoReload(id, 9, PAL_TIMER_IMMEDIATE));
    OK(PAL_Timer_ConfigureChannel(id, ic, (PalTimerChannelConfig){
        .mode = PAL_TIMER_CHANNEL_MODE_INPUT_CAPTURE, .capture_edge = PAL_TIMER_CAPTURE_BOTH
    }));
    OK(PAL_Timer_ConfigureChannel(id, oc, (PalTimerChannelConfig){
        .mode = PAL_TIMER_CHANNEL_MODE_OUTPUT_COMPARE, .compare_value = 3
    }));
    OK(PAL_Timer_SetChannelCallback(id, ic, capture, &sample));
    OK(PAL_Timer_StartChannel(id, ic));
    OK(PAL_Timer_SetChannelCallback(id, oc, match, &matches));
    OK(PAL_Timer_StartChannel(id, oc));
    OK(PAL_Timer_Start(id, true, update, &updates));
    advance(3);
    OK(PAL_Timer_Host_LatchEdge(id, ic, PAL_TIMER_CAPTURE_RISING));
    OK(PAL_Timer_Stop(id));
    advance(30);
    assert_counter(id, 3);
    OK(PAL_Timer_Host_DispatchCaptures(id));
    TEST_ASSERT_EQUAL_UINT32(3, sample);
    OK(PAL_Timer_Start(id, false, update, &updates));
    advance(10);
    TEST_ASSERT_EQUAL_UINT(2, matches);
    TEST_ASSERT_EQUAL_UINT(1, updates);
    OK(PAL_Timer_ConfigureChannel(id, ic, (PalTimerChannelConfig){
        .mode = PAL_TIMER_CHANNEL_MODE_OUTPUT_COMPARE, .compare_value = 0
    }));
    advance(10);
    TEST_ASSERT_EQUAL_UINT(3, matches);
    assert_counter(id, 3);
    OK(PAL_Timer_StopChannel(id, oc));
    advance(10);
    TEST_ASSERT_EQUAL_UINT(3, matches);
    TEST_ASSERT_EQUAL_UINT(3, updates);
    OK(PAL_Timer_Stop(id));
    OK(PAL_Timer_Start(id, true, NULL, NULL));
    assert_counter(id, 0);
}

static void test_preload(void)
{
    OK(PAL_Timer_Init(id));
    OK(PAL_Timer_SetAutoReload(id, 9, PAL_TIMER_IMMEDIATE));
    OK(PAL_Timer_Start(id, false, update, &updates));
    advance(4);
    OK(PAL_Timer_SetTickFrequency(id, 2000000));
    OK(PAL_Timer_SetAutoReload(id, 5, PAL_TIMER_ON_UPDATE));
    PalTimerTimebase programmed;
    OK(PAL_Timer_GetTimebase(id, &programmed));
    TEST_ASSERT_EQUAL_UINT32(2000000, programmed.tick_hz);
    TEST_ASSERT_EQUAL_UINT32(5, programmed.arr);
    advance(5);
    assert_counter(id, 9);
    TEST_ASSERT_EQUAL_UINT32(1000000, PAL_Timer_Host_ActiveFrequency(id));
    advance(1);
    TEST_ASSERT_EQUAL_UINT32(2000000, PAL_Timer_Host_ActiveFrequency(id));
    advance(6);
    TEST_ASSERT_EQUAL_UINT(2, updates);
    OK(PAL_Timer_SetCounter(id, 7));
    OK(PAL_Timer_SetAutoReload(id, 3, PAL_TIMER_IMMEDIATE));
    advance(1);
    assert_counter(id, 8);
    TEST_ASSERT_EQUAL_UINT(2, updates);
    OK(PAL_Timer_Stop(id));
    OK(PAL_Timer_SetTickFrequency(id, 1000000));
    OK(PAL_Timer_Start(id, true, NULL, NULL));
    TEST_ASSERT_EQUAL_UINT32(2000000, PAL_Timer_Host_ActiveFrequency(id));
    advance(4);
    TEST_ASSERT_EQUAL_UINT32(1000000, PAL_Timer_Host_ActiveFrequency(id));
}

static void test_trigger(void)
{
    OK(PAL_Timer_Init(id));
    OK(PAL_Timer_SetAutoReload(id, 9, PAL_TIMER_IMMEDIATE));
    OK(PAL_Timer_ConfigureSlave(id, PAL_TIMER_TRIGGER_START, PAL_TIMER_ITR1));
    TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_INVALID_ARGUMENT, PAL_Timer_ConfigureSlave(id, PAL_TIMER_TRIGGER_RESET, 99));
    OK(PAL_Timer_SetOnePulse(id, true));
    OK(PAL_Timer_Start(id, true, NULL, NULL));
    advance(4);
    assert_counter(id, 0);
    TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_BUSY, PAL_Timer_ConfigureSlave(id, PAL_TIMER_SLAVE_DISABLED, 0));
    PAL_Timer_Host_Trigger(id, PAL_TIMER_ITR2);
    advance(2);
    assert_counter(id, 0);
    PAL_Timer_Host_Trigger(id, PAL_TIMER_ITR1);
    advance(7);
    PAL_Timer_Host_Trigger(id, PAL_TIMER_ITR1);
    advance(2);
    assert_counter(id, 9);
    advance(99);
    assert_counter(id, 0);
    PAL_Timer_Host_Trigger(id, PAL_TIMER_ITR1);
    advance(2);
    assert_counter(id, 2);
    OK(PAL_Timer_Stop(id));
    PAL_Timer_Host_Trigger(id, PAL_TIMER_ITR1);
    advance(7);
    assert_counter(id, 2);
    OK(PAL_Timer_ConfigureSlave(id, PAL_TIMER_TRIGGER_RESET, PAL_TIMER_ITR2));
    OK(PAL_Timer_SetOnePulse(id, false));
    OK(PAL_Timer_Start(id, false, update, &updates));
    advance(3);
    PAL_Timer_Host_Trigger(id, PAL_TIMER_ITR1);
    assert_counter(id, 5);
    PAL_Timer_Host_Trigger(id, PAL_TIMER_ITR2);
    assert_counter(id, 0);
    TEST_ASSERT_EQUAL_UINT(0, updates);
    advance(10);
    TEST_ASSERT_EQUAL_UINT(1, updates);
    OK(PAL_Timer_Stop(id));
    OK(PAL_Timer_ConfigureSlave(id, PAL_TIMER_SLAVE_DISABLED, (PalTimerTriggerId)99));
    OK(PAL_Timer_Start(id, true, NULL, NULL));
    advance(3);
    assert_counter(id, 3);
}

static void test_capture(void)
{
    OK(PAL_Timer_Init(id));
    OK(PAL_Timer_ConfigureChannel(id, ic, (PalTimerChannelConfig){
        .mode = PAL_TIMER_CHANNEL_MODE_INPUT_CAPTURE, .capture_edge = PAL_TIMER_CAPTURE_BOTH
    }));
    OK(PAL_Timer_StartChannel(id, ic));
    OK(PAL_Timer_SetCounter(id, 20));
    OK(PAL_Timer_Host_LatchEdge(id, ic, PAL_TIMER_CAPTURE_FALLING));
    assert_capture(id, ic, 20);
    OK(PAL_Timer_SetCounter(id, 30));
    OK(PAL_Timer_Host_LatchEdge(id, ic, PAL_TIMER_CAPTURE_RISING));
    assert_capture(id, ic, 30);
    OK(PAL_Timer_ConfigureChannel(id, ic, (PalTimerChannelConfig){
        .mode = PAL_TIMER_CHANNEL_MODE_INPUT_CAPTURE, .capture_edge = PAL_TIMER_CAPTURE_FALLING
    }));
    OK(PAL_Timer_SetChannelCallback(id, ic, capture, &sample));
    OK(PAL_Timer_StartChannel(id, ic));
    OK(PAL_Timer_Host_LatchEdge(id, ic, PAL_TIMER_CAPTURE_RISING));
    OK(PAL_Timer_Host_DispatchCaptures(id));
    TEST_ASSERT_EQUAL_UINT32(0, sample);
    OK(PAL_Timer_Host_LatchEdge(id, ic, PAL_TIMER_CAPTURE_FALLING));
    OK(PAL_Timer_Host_DispatchCaptures(id));
    TEST_ASSERT_EQUAL_UINT32(30, sample);
}

static void test_binding(void)
{
    PalTimerTimebase base = {1000000, UINT32_MAX};
    PalTimerId wide = PAL_TIMER_HOST_32BIT;
    OK(PAL_Timer_Host_ExternalInit(wide, &base));
    OK(PAL_Timer_AttachInitialized(wide));
    PalTimerTimebase programmed;
    OK(PAL_Timer_GetTimebase(wide, &programmed));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, programmed.arr);
    TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_INVALID_STATE, PAL_Timer_DeInit(wide));
    OK(PAL_Timer_ConfigureChannel(wide, oc, (PalTimerChannelConfig){
        .mode = PAL_TIMER_CHANNEL_MODE_OUTPUT_COMPARE, .compare_value = 0
    }));
    OK(PAL_Timer_StartChannel(wide, oc));
    TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_BUSY, PAL_Timer_Detach(wide));
    OK(PAL_Timer_StopChannel(wide, oc));
    OK(PAL_Timer_SetCounter(wide, UINT32_MAX - 1u));
    OK(PAL_Timer_Start(wide, false, NULL, NULL));
    OK(PAL_Timer_Host_Advance(wide, 2));
    assert_counter(wide, 0);
    OK(PAL_Timer_Stop(wide));
    OK(PAL_Timer_Detach(wide));
    OK(PAL_Timer_AttachInitialized(wide));
    OK(PAL_Timer_Detach(wide));
    OK(PAL_Timer_Init(id));
    TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_UNSUPPORTED, PAL_Timer_Reconfigure(id, &base));
    TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_UNSUPPORTED, PAL_Timer_SetTickFrequency(id, 17));
    base.arr = UINT16_MAX;
    OK(PAL_Timer_Reconfigure(id, &base));
    OK(PAL_Timer_GetTimebase(id, &programmed));
    TEST_ASSERT_EQUAL_UINT32(UINT16_MAX, programmed.arr);
    OK(PAL_Timer_SetCounter(id, UINT16_MAX));
    OK(PAL_Timer_Start(id, false, NULL, NULL));
    advance(1);
    assert_counter(id, 0);
    OK(PAL_Timer_Stop(id));
    base = (PalTimerTimebase){2000000, 99};
    OK(PAL_Timer_Reconfigure(id, &base));
    TEST_ASSERT_EQUAL_UINT32(2000000, PAL_Timer_Host_ActiveFrequency(id));
    OK(PAL_Timer_DeInit(id));
    OK(PAL_Timer_Init(id));
}

static void reschedule(PalTimerId timer, PalTimerChannel channel, void *context)
{
    unsigned *count = context;
    if (++*count == 1) {
        uint32_t now;
        OK(PAL_Timer_GetCounter(timer, &now));
        OK(PAL_Timer_SetCompare(timer, channel, now + 2u));
    }
    if (*count == 3) OK(PAL_Timer_StopChannel(timer, channel));
}

static void test_compare(void)
{
    OK(PAL_Timer_Init(id));
    OK(PAL_Timer_SetAutoReload(id, 9, PAL_TIMER_IMMEDIATE));
    OK(PAL_Timer_ConfigureChannel(id, oc, (PalTimerChannelConfig){
        .mode = PAL_TIMER_CHANNEL_MODE_OUTPUT_COMPARE, .compare_value = 3
    }));
    OK(PAL_Timer_SetChannelCallback(id, oc, reschedule, &matches));
    OK(PAL_Timer_StartChannel(id, oc));
    PalTimerChannelConfig invalid = {.mode = PAL_TIMER_CHANNEL_MODE_INPUT_CAPTURE, .capture_edge = 0};
    TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_INVALID_ARGUMENT, PAL_Timer_ConfigureChannel(id, oc, invalid));
    invalid.mode = (PalTimerChannelMode)2;
    TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_INVALID_ARGUMENT, PAL_Timer_ConfigureChannel(id, oc, invalid));
    OK(PAL_Timer_Start(id, true, NULL, NULL));
    advance(5);
    TEST_ASSERT_EQUAL_UINT(2, matches);
    advance(20);
    TEST_ASSERT_EQUAL_UINT(3, matches);
    assert_counter(id, 5);
}

static void test_id_bounds(void)
{
    const PalTimerId outside[] = {(PalTimerId)(PAL_TIMER8 + 1u), (PalTimerId)-1, PAL_TIMER1};
    uint32_t value = 19;
    OK(PAL_Timer_Init(id)); /* id is the last host table entry. */
    OK(PAL_Timer_SetAutoReload(id, 7, PAL_TIMER_IMMEDIATE));
    OK(PAL_Timer_SetOnePulse(id, false));
    OK(PAL_Timer_ConfigureChannel(id, oc, (PalTimerChannelConfig){
        .mode = PAL_TIMER_CHANNEL_MODE_OUTPUT_COMPARE, .compare_value = 0
    }));
    OK(PAL_Timer_SetChannelCallback(id, oc, match, &matches));
    OK(PAL_Timer_StartChannel(id, oc));
    OK(PAL_Timer_SetCompare(id, oc, 5));
    OK(PAL_Timer_ConfigureChannel(id, ic, (PalTimerChannelConfig){
        .mode = PAL_TIMER_CHANNEL_MODE_INPUT_CAPTURE, .capture_edge = PAL_TIMER_CAPTURE_RISING
    }));
    OK(PAL_Timer_StartChannel(id, ic));
    OK(PAL_Timer_SetCounter(id, 3));
    OK(PAL_Timer_Host_LatchEdge(id, ic, PAL_TIMER_CAPTURE_RISING));

    for (unsigned i = 0; i < sizeof outside / sizeof outside[0]; ++i) {
        PalTimerId bad = outside[i];
        TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_INVALID_ID, PAL_Timer_SetCounter(bad, 0));
        TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_INVALID_ID, PAL_Timer_SetAutoReload(bad, 100, PAL_TIMER_IMMEDIATE));
        TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_INVALID_ID, PAL_Timer_SetOnePulse(bad, true));
        TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_INVALID_ID, PAL_Timer_SetCompare(bad, oc, 42));
        TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_INVALID_ID, PAL_Timer_GetCapture(bad, ic, &value));
        TEST_ASSERT_EQUAL_UINT32(19, value);
        TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_INVALID_ID, PAL_Timer_GetCounter(bad, &value));
        TEST_ASSERT_EQUAL_UINT32(19, value);
        TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_INVALID_ID, PAL_Timer_Start(bad, true, NULL, NULL));
        TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_INVALID_ID, PAL_Timer_Stop(bad));
        TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_INVALID_ID, PAL_Timer_SetChannelCallback(bad, ic, NULL, NULL));
        TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_INVALID_ID, PAL_Timer_StartChannel(bad, ic));
        TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_INVALID_ID, PAL_Timer_StopChannel(bad, ic));
    }
    assert_capture(id, ic, 3);
    assert_counter(id, 3);
    OK(PAL_Timer_Start(id, false, update, &updates));
    advance(16);
    TEST_ASSERT_EQUAL_UINT(2, matches);
    TEST_ASSERT_EQUAL_UINT(2, updates);
}

static void test_status(void)
{
    uint32_t value = 42;
    TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_NOT_INITIALIZED, PAL_Timer_Start(id, false, NULL, NULL));
    TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_NOT_INITIALIZED, PAL_Timer_Stop(id));
    TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_NOT_INITIALIZED, PAL_Timer_SetChannelCallback(id, ic, NULL, NULL));
    TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_NOT_INITIALIZED, PAL_Timer_StartChannel(id, ic));
    TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_NOT_INITIALIZED, PAL_Timer_StopChannel(id, ic));
    OK(PAL_Timer_Init(id));
    OK(PAL_Timer_SetAutoReload(id, 5, PAL_TIMER_IMMEDIATE));
    OK(PAL_Timer_ConfigureChannel(id, ic, (PalTimerChannelConfig){
        .mode = PAL_TIMER_CHANNEL_MODE_INPUT_CAPTURE, .capture_edge = PAL_TIMER_CAPTURE_RISING
    }));
    OK(PAL_Timer_SetChannelCallback(id, ic, capture, &value));
    OK(PAL_Timer_StartChannel(id, ic));
    OK(PAL_Timer_Host_LatchEdge(id, ic, PAL_TIMER_CAPTURE_RISING));
    TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_INVALID_ARGUMENT, PAL_Timer_GetCapture(id, ic, NULL));
    TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_INVALID_ARGUMENT, PAL_Timer_GetCapture(id, (PalTimerChannel)-1, &value));
    TEST_ASSERT_EQUAL_UINT32(42, value);
    TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_INVALID_ARGUMENT, PAL_Timer_StopChannel(id, (PalTimerChannel)-1));
    TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_INVALID_ARGUMENT, PAL_Timer_SetChannelCallback(id, (PalTimerChannel)4, NULL, NULL));
    TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_INVALID_ARGUMENT, PAL_Timer_StartChannel(id, (PalTimerChannel)4));
    OK(PAL_Timer_Host_DispatchCaptures(id));
    TEST_ASSERT_EQUAL_UINT32(0, value);

    TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_INVALID_ARGUMENT, PAL_Timer_GetCounter(id, NULL));
    TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_INVALID_ARGUMENT, PAL_Timer_SetCompare(id, (PalTimerChannel)4, 100));
    TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_INVALID_ARGUMENT, PAL_Timer_SetAutoReload(id, 1, (PalTimerApply)2));
    OK(PAL_Timer_Start(id, false, update, &updates));
    advance(2);
    assert_counter(id, 2);
    advance(4);
    TEST_ASSERT_EQUAL_UINT(1, updates);
    assert_counter(id, 0);
    OK(PAL_Timer_Stop(id));
    OK(PAL_Timer_Stop(id));
    OK(PAL_Timer_StopChannel(id, ic));
    OK(PAL_Timer_StopChannel(id, ic));
}

static void first_match(PalTimerId timer, PalTimerChannel channel, void *context)
{
    TEST_ASSERT_EQUAL_UINT(PAL_TIMER_CHANNEL_1, channel);
    match(timer, channel, context);
}

static void last_match(PalTimerId timer, PalTimerChannel channel, void *context)
{
    TEST_ASSERT_EQUAL_UINT(PAL_TIMER_CHANNEL_4, channel);
    match(timer, channel, context);
}

static void test_channel_mapping(void)
{
    unsigned first = 0, last = 0;
    OK(PAL_Timer_Init(id));
    OK(PAL_Timer_SetAutoReload(id, 9, PAL_TIMER_IMMEDIATE));
    PalTimerChannelConfig compare = {.mode = PAL_TIMER_CHANNEL_MODE_OUTPUT_COMPARE, .compare_value = 2};
    OK(PAL_Timer_ConfigureChannel(id, PAL_TIMER_CHANNEL_1, compare));
    compare.compare_value = 7;
    OK(PAL_Timer_ConfigureChannel(id, PAL_TIMER_CHANNEL_4, compare));
    OK(PAL_Timer_SetChannelCallback(id, PAL_TIMER_CHANNEL_1, first_match, &first));
    OK(PAL_Timer_StartChannel(id, PAL_TIMER_CHANNEL_1));
    OK(PAL_Timer_SetChannelCallback(id, PAL_TIMER_CHANNEL_4, last_match, &last));
    OK(PAL_Timer_StartChannel(id, PAL_TIMER_CHANNEL_4));
    OK(PAL_Timer_Start(id, true, NULL, NULL));
    advance(2);
    TEST_ASSERT_EQUAL_UINT(1, first);
    TEST_ASSERT_EQUAL_UINT(0, last);
    advance(5);
    TEST_ASSERT_EQUAL_UINT(1, first);
    TEST_ASSERT_EQUAL_UINT(1, last);

    PalTimerChannelConfig capture_config = {.mode = PAL_TIMER_CHANNEL_MODE_INPUT_CAPTURE,
                                           .capture_edge = PAL_TIMER_CAPTURE_RISING};
    OK(PAL_Timer_ConfigureChannel(id, PAL_TIMER_CHANNEL_1, capture_config));
    OK(PAL_Timer_ConfigureChannel(id, PAL_TIMER_CHANNEL_4, capture_config));
    OK(PAL_Timer_StartChannel(id, PAL_TIMER_CHANNEL_1));
    OK(PAL_Timer_StartChannel(id, PAL_TIMER_CHANNEL_4));
    OK(PAL_Timer_Host_LatchEdge(id, PAL_TIMER_CHANNEL_4, PAL_TIMER_CAPTURE_RISING));
    advance(1);
    OK(PAL_Timer_Host_LatchEdge(id, PAL_TIMER_CHANNEL_1, PAL_TIMER_CAPTURE_RISING));
    assert_capture(id, PAL_TIMER_CHANNEL_4, 7);
    assert_capture(id, PAL_TIMER_CHANNEL_1, 8);
}

static void test_master_trigger(void)
{
    const PalTimerId master = PAL_TIMER_HOST_32BIT;
    TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_NOT_INITIALIZED,
                          PAL_Timer_ConfigureMasterTrigger(master, PAL_TIMER_TRGO_COMPARE_PULSE));
    OK(PAL_Timer_Init(master));
    OK(PAL_Timer_Init(id));
    for (unsigned source = PAL_TIMER_TRGO_RESET; source <= PAL_TIMER_TRGO_COMPARE_PULSE; ++source) {
        OK(PAL_Timer_ConfigureMasterTrigger(master, (PalTimerMasterTriggerOutput)source));
        TEST_ASSERT_EQUAL_UINT(source, PAL_Timer_Host_MasterTrigger(master));
    }
    TEST_ASSERT_EQUAL_UINT(PAL_TIMER_TRGO_RESET, PAL_Timer_Host_MasterTrigger(id));
    TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_INVALID_ARGUMENT,
                          PAL_Timer_ConfigureMasterTrigger(master, (PalTimerMasterTriggerOutput)4));
    OK(PAL_Timer_ConfigureSlave(master, PAL_TIMER_TRIGGER_START, PAL_TIMER_ITR3));
    OK(PAL_Timer_Start(master, true, NULL, NULL));
    TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_BUSY, PAL_Timer_ConfigureMasterTrigger(master, PAL_TIMER_TRGO_ENABLE));
    PAL_Timer_Host_Trigger(master, PAL_TIMER_ITR3);
    OK(PAL_Timer_Host_Advance(master, 2));
    assert_counter(master, 2);
    TEST_ASSERT_EQUAL_INT(PAL_TIMER_STATUS_BUSY, PAL_Timer_ConfigureMasterTrigger(master, PAL_TIMER_TRGO_UPDATE));
    TEST_ASSERT_EQUAL_UINT(PAL_TIMER_TRGO_COMPARE_PULSE, PAL_Timer_Host_MasterTrigger(master));
}

static void test_callback_swap(void)
{
    uint32_t first = UINT32_MAX, second = UINT32_MAX;
    OK(PAL_Timer_Init(id));
    OK(PAL_Timer_ConfigureChannel(id, ic, (PalTimerChannelConfig){
        .mode = PAL_TIMER_CHANNEL_MODE_INPUT_CAPTURE, .capture_edge = PAL_TIMER_CAPTURE_RISING
    }));
    OK(PAL_Timer_ConfigureChannel(id, oc, (PalTimerChannelConfig){
        .mode = PAL_TIMER_CHANNEL_MODE_OUTPUT_COMPARE, .compare_value = 2
    }));
    OK(PAL_Timer_SetChannelCallback(id, oc, match, &matches));
    OK(PAL_Timer_StartChannel(id, oc));
    OK(PAL_Timer_SetChannelCallback(id, ic, capture, &first));
    OK(PAL_Timer_StartChannel(id, ic));
    OK(PAL_Timer_Start(id, false, NULL, NULL));
    advance(1);
    OK(PAL_Timer_Host_LatchEdge(id, ic, PAL_TIMER_CAPTURE_RISING));
    OK(PAL_Timer_SetChannelCallback(id, ic, capture, &second));
    OK(PAL_Timer_Host_DispatchCaptures(id));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, first);
    TEST_ASSERT_EQUAL_UINT32(1, second);
    OK(PAL_Timer_SetChannelCallback(id, ic, NULL, NULL));
    advance(1);
    TEST_ASSERT_EQUAL_UINT(1, matches);
    OK(PAL_Timer_Host_LatchEdge(id, ic, PAL_TIMER_CAPTURE_RISING));
    OK(PAL_Timer_Host_DispatchCaptures(id));
    TEST_ASSERT_EQUAL_UINT32(1, second);
    OK(PAL_Timer_SetChannelCallback(id, ic, capture, &first));
    OK(PAL_Timer_Host_DispatchCaptures(id));
    TEST_ASSERT_EQUAL_UINT32(2, first);

    OK(PAL_Timer_Host_LatchEdge(id, ic, PAL_TIMER_CAPTURE_RISING));
    OK(PAL_Timer_SetChannelCallback(id, ic, match, &updates));
    OK(PAL_Timer_Host_DispatchCaptures(id));
    TEST_ASSERT_EQUAL_UINT(1, updates);
    OK(PAL_Timer_StopChannel(id, ic));
    OK(PAL_Timer_StartChannel(id, ic));
    OK(PAL_Timer_Host_LatchEdge(id, ic, PAL_TIMER_CAPTURE_RISING));
    OK(PAL_Timer_Host_DispatchCaptures(id));
    TEST_ASSERT_EQUAL_UINT(2, updates);
    assert_counter(id, 2);
}

int main(int argc, char **argv)
{
    const TestCase cases[] = {
        TEST_CASE(test_independent),
        TEST_CASE(test_preload),
        TEST_CASE(test_trigger),
        TEST_CASE(test_master_trigger),
        TEST_CASE(test_capture),
        TEST_CASE(test_binding),
        TEST_CASE(test_compare),
        TEST_CASE(test_id_bounds),
        TEST_CASE(test_status),
        TEST_CASE(test_channel_mapping),
        TEST_CASE(test_callback_swap),
    };
    return run_test_cases(argc, argv, __FILE__, cases, sizeof cases / sizeof cases[0]);
}
