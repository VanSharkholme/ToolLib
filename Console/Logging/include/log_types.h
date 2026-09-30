//
// Created by VanSharkholme on 2026/8/10.
//

#ifndef H533_TEST_LOG_TYPES_H
#define H533_TEST_LOG_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LOG_COMPILE_LEVEL_NONE 0
#define LOG_COMPILE_LEVEL_ERROR 1
#define LOG_COMPILE_LEVEL_WARNING 2
#define LOG_COMPILE_LEVEL_INFO 3
#define LOG_COMPILE_LEVEL_DEBUG 4

typedef enum
{
    LOG_LEVEL_NONE = LOG_COMPILE_LEVEL_NONE,
    LOG_LEVEL_ERROR = LOG_COMPILE_LEVEL_ERROR,
    LOG_LEVEL_WARNING = LOG_COMPILE_LEVEL_WARNING,
    LOG_LEVEL_INFO = LOG_COMPILE_LEVEL_INFO,
    LOG_LEVEL_DEBUG = LOG_COMPILE_LEVEL_DEBUG,
} Log_Level;

typedef enum
{
    LOG_STATUS_OK = 0,
    LOG_STATUS_INVALID_ARGUMENT,
    LOG_STATUS_NOT_INITIALIZED,
    LOG_STATUS_BUFFER_ERROR,
    LOG_STATUS_BUFFER_FULL,
    LOG_STATUS_FORMAT_ERROR,
    LOG_STATUS_BACKEND_ERROR
} Log_Status;

typedef struct
{
    uint32_t accepted;
    uint32_t transmitted;
    uint32_t dropped;
    uint32_t truncated;
    uint32_t format_errors;
    uint32_t backend_errors;
} Log_Stats;

#endif //H533_TEST_LOG_TYPES_H
