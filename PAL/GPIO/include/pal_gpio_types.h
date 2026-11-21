#ifndef TOOLLIB_PAL_GPIO_TYPES_H
#define TOOLLIB_PAL_GPIO_TYPES_H

#include <stdbool.h>
#include <stdint.h>

typedef uint16_t PalGpioId;
typedef enum {
    PAL_GPIO_STATUS_OK, PAL_GPIO_STATUS_INVALID_ID, PAL_GPIO_STATUS_INVALID_ARGUMENT,
    PAL_GPIO_STATUS_INVALID_STATE, PAL_GPIO_STATUS_BUSY
} PalGpioStatus;

typedef enum { PAL_GPIO_INPUT, PAL_GPIO_OUTPUT, PAL_GPIO_ALTERNATE, PAL_GPIO_ANALOG } PalGpioMode;
typedef enum { PAL_GPIO_PUSH_PULL, PAL_GPIO_OPEN_DRAIN } PalGpioOutputType;
typedef enum {
    PAL_GPIO_SPEED_LOW, PAL_GPIO_SPEED_MEDIUM, PAL_GPIO_SPEED_HIGH, PAL_GPIO_SPEED_VERY_HIGH
} PalGpioSpeed;
typedef enum { PAL_GPIO_NO_PULL, PAL_GPIO_PULL_UP, PAL_GPIO_PULL_DOWN } PalGpioPull;
typedef enum {
    PAL_GPIO_EDGE_NONE, PAL_GPIO_EDGE_RISING, PAL_GPIO_EDGE_FALLING, PAL_GPIO_EDGE_BOTH
} PalGpioEdge;

typedef struct {
    PalGpioMode mode;
    PalGpioOutputType output_type;
    PalGpioSpeed speed;
    PalGpioPull pull;
    uint8_t alternate;
    bool interrupt_enabled;
    PalGpioEdge edge;
} PalGpioConfig;

typedef void (*PalGpioCallback)(PalGpioId id, void *context);

#endif
