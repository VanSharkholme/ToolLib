#ifndef TOOLLIB_PAL_TIMER_TYPES_H
#define TOOLLIB_PAL_TIMER_TYPES_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    PAL_TIMER1 = 0,
    PAL_TIMER2,
    PAL_TIMER3,
    PAL_TIMER4,
    PAL_TIMER5,
    PAL_TIMER6,
    PAL_TIMER7,
    PAL_TIMER8,
} PalTimerId;                       /**< Board-defined instance ID. */

/* Internal trigger input of the destination timer. */
typedef enum
{
    PAL_TIMER_ITR0,
    PAL_TIMER_ITR1,
    PAL_TIMER_ITR2,
    PAL_TIMER_ITR3,
} PalTimerTriggerId;

typedef enum
{
    PAL_TIMER_TRGO_RESET,         /**< Software update generation (UG). */
    PAL_TIMER_TRGO_ENABLE,        /**< Counter enable signal. */
    PAL_TIMER_TRGO_UPDATE,        /**< Update event. */
    PAL_TIMER_TRGO_COMPARE_PULSE, /**< Channel 1 capture/compare event pulse. */
} PalTimerMasterTriggerOutput;

typedef enum
{
    PAL_TIMER_STATUS_OK = 0,
    PAL_TIMER_STATUS_INVALID_ID,
    PAL_TIMER_STATUS_INVALID_ARGUMENT,
    PAL_TIMER_STATUS_UNSUPPORTED,
    PAL_TIMER_STATUS_NOT_INITIALIZED,
    PAL_TIMER_STATUS_ALREADY_INITIALIZED,
    PAL_TIMER_STATUS_INVALID_STATE,
    PAL_TIMER_STATUS_BUSY,
    PAL_TIMER_STATUS_NOT_ACTIVE,
    PAL_TIMER_STATUS_NO_DATA,
    PAL_TIMER_STATUS_TOO_LATE,
    PAL_TIMER_STATUS_PORT_ERROR,
} PalTimerStatus;

typedef enum
{
    PAL_TIMER_CHANNEL_1 = 0,
    PAL_TIMER_CHANNEL_2,
    PAL_TIMER_CHANNEL_3,
    PAL_TIMER_CHANNEL_4,
    PAL_TIMER_CHANNEL_COUNT,
    PAL_TIMER_CHANNEL_INVALID = 0xFFFFFFFFU,
} PalTimerChannel;

typedef enum
{
    PAL_TIMER_IMMEDIATE,
    PAL_TIMER_ON_UPDATE,
} PalTimerApply;

typedef enum
{
    PAL_TIMER_SLAVE_DISABLED,
    PAL_TIMER_TRIGGER_START,
    PAL_TIMER_TRIGGER_RESET,
} PalTimerSlaveMode;

typedef enum
{
    PAL_TIMER_CHANNEL_MODE_OUTPUT_COMPARE = 0,
    PAL_TIMER_CHANNEL_MODE_INPUT_CAPTURE,
} PalTimerChannelMode;

typedef enum
{
    PAL_TIMER_CHANNEL_SUPPORT_NONE              = 0,
    PAL_TIMER_CHANNEL_SUPPORT_CAPTURE           = 1 << 0,
    PAL_TIMER_CHANNEL_SUPPORT_COMPARE           = 1 << 1,
    PAL_TIMER_CHANNEL_SUPPORT_BOTH_EDGE_CAPTURE = 1 << 2,
} PalTimerChannelSupport;

typedef enum
{
    PAL_TIMER_CAPTURE_RISING = 1,
    PAL_TIMER_CAPTURE_FALLING,
    PAL_TIMER_CAPTURE_BOTH,
} PalTimerCaptureEdge;

typedef uint32_t PalTimerCompareValue;

typedef struct
{
    uint32_t tick_hz;
    uint32_t arr; /**< Raw inclusive auto-reload value. */
} PalTimerTimebase;

typedef struct
{
    PalTimerChannelMode mode;

    union
    {
        PalTimerCompareValue compare_value;
        PalTimerCaptureEdge capture_edge;
    };
} PalTimerChannelConfig;

typedef void (*PalTimerUpdateCallback)(PalTimerId id, void* context);
typedef void (*PalTimerChannelCallback)(PalTimerId id, PalTimerChannel channel, void* context);

#endif
