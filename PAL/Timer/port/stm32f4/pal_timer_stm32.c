#include "pal_timer_stm32.h"
#include <stddef.h>
#include "tim.h"
#include "bsp_timer.h"

#define CAPTURE_ENABLES (TIM_CCER_CC1E | TIM_CCER_CC2E | TIM_CCER_CC3E | TIM_CCER_CC4E)

typedef struct
{
    PalTimerDevice pal_device;
    PalTimerId id;
    TIM_HandleTypeDef* htim;
    TIM_TypeDef* instance;
    uint32_t slave_config; /* TS and SMS applied together when Start arms the timer. */
} Stm32Timer;

/* CubeMX handles for the BSP timer IDs. */
static Stm32Timer devices[BSP_TIMER_NUM] = {
    [PAL_TIMER1] = {
        .pal_device.port_context = &devices[PAL_TIMER1],
        .id = PAL_TIMER1,
        .htim = &htim2,
    },
    [PAL_TIMER2] = {
        .pal_device.port_context = &devices[PAL_TIMER2],
        .id = PAL_TIMER2,
        .htim = &htim3,
    },
};

_Static_assert(sizeof devices / sizeof devices[0] == BSP_TIMER_NUM, "BSP timer count must match the device table");

static uint32_t timer_clock(TIM_TypeDef* instance)
{
    uint32_t divider;
    uint32_t pclk;
    if ((uintptr_t)instance >= APB2PERIPH_BASE)
    {
        divider = (RCC->CFGR & RCC_CFGR_PPRE2) >> RCC_CFGR_PPRE2_Pos;
        pclk = HAL_RCC_GetPCLK2Freq();
    }
    else
    {
        divider = (RCC->CFGR & RCC_CFGR_PPRE1) >> RCC_CFGR_PPRE1_Pos;
        pclk = HAL_RCC_GetPCLK1Freq();
    }
    if (divider < 4u) return pclk;
    if ((RCC->DCKCFGR & RCC_DCKCFGR_TIMPRE) == 0u) return 2u * pclk;
    return divider <= 5u? HAL_RCC_GetHCLKFreq(): 4u * pclk;
}

static uint32_t hal_channel(PalTimerChannel channel)
{
    return TIM_CHANNEL_1 + 4u * ((unsigned)channel);
}

static PalTimerChannel pal_channel(HAL_TIM_ActiveChannel hal_channel)
{
    if (hal_channel == HAL_TIM_ACTIVE_CHANNEL_CLEARED)
        return PAL_TIMER_CHANNEL_INVALID;
    return POSITION_VAL(hal_channel);
}

static uint32_t channel_flag(PalTimerChannel channel)
{
    return TIM_SR_CC1IF << ((unsigned)channel);
}

static uint32_t capture_overrun_flag(PalTimerChannel channel)
{
    return TIM_SR_CC1OF << ((unsigned)channel);
}

static uint32_t channel_enable(PalTimerChannel channel)
{
    return TIM_CCER_CC1E << hal_channel(channel);
}

static volatile uint32_t* channel_mode_register(TIM_TypeDef* timer, PalTimerChannel channel)
{
    return channel <= PAL_TIMER_CHANNEL_2? &timer->CCMR1: &timer->CCMR2;
}

static unsigned channel_mode_shift(PalTimerChannel channel)
{
    return ((unsigned)channel % 2u) * 8u;
}

PalTimerDevice* PAL_Timer_Port_Find(PalTimerId id)
{
    if ((unsigned)id >= BSP_TIMER_NUM) return NULL;
    return &devices[id].pal_device;
}

static Stm32Timer* PAL_Timer_Port_Find_by_HAL_Handle(TIM_HandleTypeDef *htim)
{
    for (size_t i = 0; i < BSP_TIMER_NUM; i++)
    {
        if (devices[i].htim == htim)
            return &devices[i];
    }
    return NULL;
}

uint32_t PAL_Timer_Port_EnterCritical(void)
{
    uint32_t state = __get_PRIMASK();
    __disable_irq();
    return state;
}

void PAL_Timer_Port_ExitCritical(uint32_t state)
{
    __set_PRIMASK(state);
}

static bool get_prescaler(TIM_TypeDef* instance, uint32_t hz, uint32_t* psc)
{
    uint32_t clock = timer_clock(instance);
    if (hz == 0u || hz > clock || clock % hz != 0u) return false;
    uint32_t divider = clock / hz;
    if (divider > 65536u) return false;
    *psc = divider - 1u;
    return true;
}

PalTimerStatus PAL_Timer_Port_Init(void* context)
{
    Stm32Timer* device = context;
    TIM_HandleTypeDef* handle = device->htim;
    if (handle->State != HAL_TIM_STATE_RESET) return PAL_TIMER_STATUS_BUSY;
    TIM_TypeDef* instance = BSP_Timer_Get_Instance(device->id);
    const TIM_Base_InitTypeDef* default_init = BSP_Timer_Get_Default_Init(device->id);
    const PalTimerTimebase* timebase = BSP_Timer_Get_Default_Timebase(device->id);
    if (instance == NULL || default_init == NULL || timebase == NULL)
        return PAL_TIMER_STATUS_PORT_ERROR;
    uint32_t psc;
    if (!get_prescaler(instance, timebase->tick_hz, &psc)) return PAL_TIMER_STATUS_UNSUPPORTED;

    handle->Instance = instance;
    handle->Init = *default_init;
    handle->Init.Prescaler = psc;
    handle->Init.Period = timebase->arr;
    handle->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(handle) != HAL_OK) return PAL_TIMER_STATUS_PORT_ERROR;
    device->instance = instance;
    CLEAR_BIT(device->instance->CR1, TIM_CR1_OPM | TIM_CR1_UDIS);
    device->slave_config = 0u;
    return PAL_TIMER_STATUS_OK;
}

PalTimerStatus PAL_Timer_Port_AttachInitialized(void* context)
{
    Stm32Timer* device = context;
    const TIM_HandleTypeDef* handle = device->htim;
    TIM_TypeDef* timer = handle->Instance;
    if (timer == NULL) return PAL_TIMER_STATUS_PORT_ERROR;
    if (handle->State != HAL_TIM_STATE_READY) return PAL_TIMER_STATUS_INVALID_STATE;
    if ((timer->CR1 & TIM_CR1_CEN) != 0u || timer->DIER != 0u ||
        (timer->CCER & (CAPTURE_ENABLES | TIM_CCER_CC1NE | TIM_CCER_CC2NE | TIM_CCER_CC3NE)) != 0u)
    {
        return PAL_TIMER_STATUS_BUSY;
    }
    if ((timer->CR1 & (TIM_CR1_DIR | TIM_CR1_CMS | TIM_CR1_UDIS)) != 0u || (timer->SMCR & (TIM_SMCR_SMS | TIM_SMCR_ECE))
        != 0u ||
        (IS_TIM_REPETITION_COUNTER_INSTANCE(timer) && timer->RCR != 0u))
    {
        return PAL_TIMER_STATUS_UNSUPPORTED;
    }

    uint32_t clock_hz = timer_clock(timer);
    uint32_t divider = timer->PSC + 1u;
    if (__HAL_TIM_GET_AUTORELOAD(handle) == 0u || clock_hz < divider || clock_hz % divider != 0u)
        return
            PAL_TIMER_STATUS_UNSUPPORTED;
    device->instance = timer;
    device->slave_config = 0u;
    return PAL_TIMER_STATUS_OK;
}

PalTimerStatus PAL_Timer_Port_DeInit(void* context)
{
    Stm32Timer* device = context;
    return HAL_TIM_Base_DeInit(device->htim) == HAL_OK? PAL_TIMER_STATUS_OK: PAL_TIMER_STATUS_PORT_ERROR;
}

bool PAL_Timer_Port_Active(void* context)
{
    const Stm32Timer* device = context;
    return (device->instance->CR1 & TIM_CR1_CEN) || (device->instance->SMCR & TIM_SMCR_SMS);
}

PalTimerStatus PAL_Timer_Port_Configure(void* context, const PalTimerTimebase* base)
{
    Stm32Timer* device = context;
    TIM_TypeDef* timer = device->instance;
    uint32_t psc;
    if ((!IS_TIM_32B_COUNTER_INSTANCE(timer) && base->arr > UINT16_MAX) ||
        !get_prescaler(timer, base->tick_hz, &psc))
        return PAL_TIMER_STATUS_UNSUPPORTED;
    /** URS bit set in PAL, but not restored to the state before attached when PAL detaches */
    SET_BIT(timer->CR1, TIM_CR1_URS);
    __HAL_TIM_SET_PRESCALER(device->htim, psc);
    timer->ARR = base->arr;
    timer->EGR = TIM_EGR_UG;
    return PAL_TIMER_STATUS_OK;
}

void PAL_Timer_Port_GetTimebase(void* context, PalTimerTimebase* base)
{
    const Stm32Timer* device = context;
    base->tick_hz = timer_clock(device->instance) / (device->instance->PSC + 1u);
    base->arr = __HAL_TIM_GET_AUTORELOAD(device->htim);
}

PalTimerStatus PAL_Timer_Port_SetFrequency(void* context, uint32_t hz)
{
    Stm32Timer* device = context;
    uint32_t psc;
    if (!get_prescaler(device->instance, hz, &psc)) return PAL_TIMER_STATUS_UNSUPPORTED;
    __HAL_TIM_SET_PRESCALER(device->htim, psc);
    return PAL_TIMER_STATUS_OK;
}

PalTimerStatus PAL_Timer_Port_GetCounter(PalTimerId id, uint32_t* value)
{
    if ((unsigned)id >= BSP_TIMER_NUM) return PAL_TIMER_STATUS_INVALID_ID;
    if (value == NULL) return PAL_TIMER_STATUS_INVALID_ARGUMENT;
    *value = __HAL_TIM_GET_COUNTER(devices[id].htim);
    return PAL_TIMER_STATUS_OK;
}

PalTimerStatus PAL_Timer_Port_SetCounter(PalTimerId id, uint32_t value)
{
    if ((unsigned)id >= BSP_TIMER_NUM) return PAL_TIMER_STATUS_INVALID_ID;
    __HAL_TIM_SET_COUNTER(devices[id].htim, value);
    return PAL_TIMER_STATUS_OK;
}

/* Atomic peripheral bit writes preserve a CEN transition made by OPM or TRGI
 * between a software load and store of CR1. */
static void write_cr1_bit(TIM_TypeDef* timer, unsigned bit, bool value)
{
    uintptr_t offset = (uintptr_t)&timer->CR1 - PERIPH_BASE;
    *(volatile uint32_t*)(PERIPH_BB_BASE + offset * 32u + bit * 4u) = value;
}

PalTimerStatus PAL_Timer_Port_SetAutoReload(PalTimerId id, uint32_t arr, PalTimerApply apply)
{
    if ((unsigned)id >= BSP_TIMER_NUM) return PAL_TIMER_STATUS_INVALID_ID;
    if ((unsigned)apply > PAL_TIMER_ON_UPDATE) return PAL_TIMER_STATUS_INVALID_ARGUMENT;
    TIM_TypeDef* timer = devices[id].instance;
    write_cr1_bit(timer, TIM_CR1_ARPE_Pos, apply == PAL_TIMER_ON_UPDATE);
    timer->ARR = arr;
    return PAL_TIMER_STATUS_OK;
}

PalTimerStatus PAL_Timer_Port_SetOnePulse(PalTimerId id, bool enable)
{
    if ((unsigned)id >= BSP_TIMER_NUM) return PAL_TIMER_STATUS_INVALID_ID;
    TIM_TypeDef* timer = devices[id].instance;
    write_cr1_bit(timer, TIM_CR1_OPM_Pos, enable);
    return PAL_TIMER_STATUS_OK;
}

PalTimerStatus PAL_Timer_Port_ConfigureMasterTrigger(void* context, PalTimerMasterTriggerOutput source)
{
    const Stm32Timer* device = context;
    if (!IS_TIM_MASTER_INSTANCE(device->instance)) return PAL_TIMER_STATUS_UNSUPPORTED;
    static const uint32_t mms[] = {
        [PAL_TIMER_TRGO_RESET] = TIM_TRGO_RESET,
        [PAL_TIMER_TRGO_ENABLE] = TIM_TRGO_ENABLE,
        [PAL_TIMER_TRGO_UPDATE] = TIM_TRGO_UPDATE,
        [PAL_TIMER_TRGO_COMPARE_PULSE] = TIM_TRGO_OC1,
    };
    MODIFY_REG(device->instance->CR2, TIM_CR2_MMS, mms[source]);
    return PAL_TIMER_STATUS_OK;
}

/* STM32F401 RM0368 internal trigger tables: TIM2 ITR1 and TIM4/5 ITR3
 * are reserved. TIM1, TIM3 and TIM9 have all four inputs. */
static bool supports_itr(TIM_TypeDef* timer, PalTimerTriggerId trigger)
{
    if (!IS_TIM_SLAVE_INSTANCE(timer)) return false;
    if (timer == TIM2) return trigger != PAL_TIMER_ITR1;
    if (timer == TIM4 || timer == TIM5) return trigger != PAL_TIMER_ITR3;
    return true;
}

PalTimerStatus PAL_Timer_Port_ConfigureSlave(void* context, PalTimerSlaveMode mode, PalTimerTriggerId trigger)
{
    Stm32Timer* device = context;
    if (mode == PAL_TIMER_SLAVE_DISABLED)
    {
        device->slave_config = 0u;
        return PAL_TIMER_STATUS_OK;
    }
    if (!supports_itr(device->instance, trigger)) return PAL_TIMER_STATUS_UNSUPPORTED;
    uint32_t ts = (uint32_t)trigger << TIM_SMCR_TS_Pos;
    uint32_t sms = mode == PAL_TIMER_TRIGGER_START ? TIM_SLAVEMODE_TRIGGER : TIM_SLAVEMODE_RESET;
    device->slave_config = ts | sms;
    return PAL_TIMER_STATUS_OK;
}

void PAL_Timer_Port_Start(void* context, bool clear_counter, bool notify)
{
    const Stm32Timer* device = context;
    TIM_TypeDef* timer = device->instance;
    SET_BIT(timer->CR1, TIM_CR1_URS);
    if (clear_counter)
        __HAL_TIM_SET_COUNTER(device->htim, 0u);
    __HAL_TIM_CLEAR_FLAG(device->htim, TIM_FLAG_UPDATE);
    if (notify)
        __HAL_TIM_ENABLE_IT(device->htim, TIM_IT_UPDATE);
    else
        __HAL_TIM_DISABLE_IT(device->htim, TIM_IT_UPDATE);
    MODIFY_REG(timer->SMCR, TIM_SMCR_TS | TIM_SMCR_SMS, device->slave_config);
    if ((device->slave_config & TIM_SMCR_SMS) != TIM_SLAVEMODE_TRIGGER)
        SET_BIT(timer->CR1, TIM_CR1_CEN);
}

void PAL_Timer_Port_Stop(void* context)
{
    const Stm32Timer* device = context;
    TIM_TypeDef* timer = device->instance;
    CLEAR_BIT(timer->SMCR, TIM_SMCR_SMS);
    CLEAR_BIT(timer->CR1, TIM_CR1_CEN);
    __HAL_TIM_DISABLE_IT(device->htim, TIM_IT_UPDATE);
    __HAL_TIM_CLEAR_FLAG(device->htim, TIM_FLAG_UPDATE);
}

static volatile uint32_t* ccr(TIM_TypeDef* timer, PalTimerChannel channel)
{
    return (volatile uint32_t*)((uintptr_t)&timer->CCR1 + (unsigned)channel * sizeof(uint32_t));
}

PalTimerStatus PAL_Timer_Port_GetCapture(PalTimerId id, PalTimerChannel channel, uint32_t* value)
{
    if ((unsigned)id >= BSP_TIMER_NUM) return PAL_TIMER_STATUS_INVALID_ID;
    if (value == NULL) return PAL_TIMER_STATUS_INVALID_ARGUMENT;
    if ((unsigned)channel >= PAL_TIMER_CHANNEL_COUNT) return PAL_TIMER_STATUS_INVALID_ARGUMENT;
    *value = *ccr(devices[id].instance, channel);
    return PAL_TIMER_STATUS_OK;
}

PalTimerStatus PAL_Timer_Port_SetCompare(PalTimerId id, PalTimerChannel channel, uint32_t value)
{
    if ((unsigned)id >= BSP_TIMER_NUM) return PAL_TIMER_STATUS_INVALID_ID;
    if ((unsigned)channel >= PAL_TIMER_CHANNEL_COUNT) return PAL_TIMER_STATUS_INVALID_ARGUMENT;
    *ccr(devices[id].instance, channel) = value;
    return PAL_TIMER_STATUS_OK;
}

void PAL_Timer_Port_EnableChannel(void* context, PalTimerChannel channel, bool enable, bool notify)
{
    const Stm32Timer* device = context;
    TIM_TypeDef* timer = device->instance;
    uint32_t flag = channel_flag(channel);
    uint32_t input = (*channel_mode_register(timer, channel) >> channel_mode_shift(channel)) & 3u;
    __HAL_TIM_DISABLE_IT(device->htim, flag);
    CLEAR_BIT(timer->CCER, channel_enable(channel));
    __HAL_TIM_CLEAR_FLAG(device->htim, flag | capture_overrun_flag(channel));
    if (enable && input)
        SET_BIT(timer->CCER, channel_enable(channel));
    if (notify)
        __HAL_TIM_ENABLE_IT(device->htim, flag);
}

PalTimerStatus PAL_Timer_Port_ConfigureChannel(void* context, PalTimerChannel channel, PalTimerChannelConfig config)
{
    Stm32Timer* device = context;
    bool capture = config.mode == PAL_TIMER_CHANNEL_MODE_INPUT_CAPTURE;
    PalTimerCaptureEdge edge = capture? config.capture_edge: 0;
    PalTimerChannelSupport required = capture? PAL_TIMER_CHANNEL_SUPPORT_CAPTURE: PAL_TIMER_CHANNEL_SUPPORT_COMPARE;
    if (edge == PAL_TIMER_CAPTURE_BOTH) required |= PAL_TIMER_CHANNEL_SUPPORT_BOTH_EDGE_CAPTURE;
    PalTimerChannelSupport support = BSP_Timer_Get_Channel_Support(device->id, channel);
    TIM_TypeDef* timer = device->instance;
    if ((support & required) != required || !IS_TIM_CCX_INSTANCE(timer, hal_channel(channel)))
        return PAL_TIMER_STATUS_UNSUPPORTED;
    PAL_Timer_Port_EnableChannel(device, channel, false, false);
    unsigned shift = channel_mode_shift(channel);
    MODIFY_REG(*channel_mode_register(timer, channel), 0xffu << shift, (capture ? 1u : 0u) << shift);
    uint32_t polarity = 0u;
    if (edge == PAL_TIMER_CAPTURE_FALLING) polarity = TIM_CCER_CC1P;
    if (edge == PAL_TIMER_CAPTURE_BOTH) polarity = TIM_CCER_CC1P | TIM_CCER_CC1NP;
    shift = hal_channel(channel);
    MODIFY_REG(timer->CCER, 0xfu << shift, polarity << shift);
    if (!capture) *ccr(timer, channel) = config.compare_value;
    return PAL_TIMER_STATUS_OK;
}

/************************************************************************************
 * Callbacks
 ************************************************************************************/
void PAL_Timer_STM32_Update_Callback(TIM_HandleTypeDef *htim)
{
    Stm32Timer *timer = PAL_Timer_Port_Find_by_HAL_Handle(htim);
    if (timer == NULL)
        return;
    PalTimerUpdateCallback update_callback = timer->pal_device.update;
    void *cb_context = timer->pal_device.update_context;
    if (update_callback != NULL)
        update_callback(timer->id, cb_context);
}

void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    Stm32Timer *timer = PAL_Timer_Port_Find_by_HAL_Handle(htim);
    if (timer == NULL || timer->pal_device.binding == PAL_TIMER_UNBOUND)
        return;
    PalTimerChannel channel = pal_channel(htim->Channel);
    if ((unsigned)channel >= PAL_TIMER_CHANNEL_COUNT)
        return;
    PalTimerChannelState *state = &timer->pal_device.channels[channel];
    if (state->enabled == false)
        return;
    /* HAL acknowledged CCxIF. Clear only CCxOF to preserve a later edge's pending CCxIF. */
    __HAL_TIM_CLEAR_FLAG(htim, capture_overrun_flag(channel));
    PalTimerChannelCallback channel_callback = state->callback;
    void *cb_context = state->context;
    if (channel_callback)
        channel_callback(timer->id, channel, cb_context);
}

void HAL_TIM_OC_DelayElapsedCallback(TIM_HandleTypeDef *htim)
{
    Stm32Timer *timer = PAL_Timer_Port_Find_by_HAL_Handle(htim);
    if (timer == NULL || timer->pal_device.binding == PAL_TIMER_UNBOUND)
        return;
    PalTimerChannel channel = pal_channel(htim->Channel);
    if ((unsigned)channel >= PAL_TIMER_CHANNEL_COUNT)
        return;
    PalTimerChannelState *state = &timer->pal_device.channels[channel];
    if (state->enabled == false)
        return;
    PalTimerChannelCallback channel_callback = state->callback;
    void *cb_context = state->context;
    if (channel_callback)
        channel_callback(timer->id, channel, cb_context);
}
