//
// Created by VanSharkholme on 25-6-1.
//

#ifndef LOG_H
#define LOG_H

#include <stdint.h>
#include <stdarg.h>
#include "log_buffer.h"
#include "log_backend.h"
#include "log_types.h"
#include "log_config.h"

typedef struct
{
    const Log_Backend* backend;
    Log_Level runtime_level;
    bool enable_color;
    bool enable_timestamp;
} Log_Config;


Log_Status log_init(const Log_Config* config);
Log_Status log_deinit(void);

void log_write(
    Log_Level level, const char *tag,
    const char *file, uint32_t line,
    const char *format, ...
);

void log_vwrite(
    Log_Level level, const char *tag,
    const char *file, uint32_t line,
    const char *format, va_list args
);

void log_process(void);

void log_backend_tx_complete(void *context);

void log_set_level(Log_Level level);

Log_Level log_get_level(void);

void Log_get_stats(Log_Stats* stats);

void Log_reset_stats(void);

bool Log_flush(uint32_t timeout_ms);

#ifndef LOG_COMPILE_LEVEL
#define LOG_COMPILE_LEVEL LOG_COMPILE_LEVEL_DEBUG
#endif

#if LOG_COMPILE_LEVEL >= LOG_COMPILE_LEVEL_ERROR
#define LOG_ERROR(tag, format, ...) \
    log_write(LOG_LEVEL_ERROR, tag, __FILE__, __LINE__, \
              format, ##__VA_ARGS__)
#else
#define LOG_ERROR(tag, format, ...) ((void)0)
#endif

#if LOG_COMPILE_LEVEL >= LOG_COMPILE_LEVEL_WARNING
#define LOG_WARN(tag, format, ...) \
    log_write(LOG_LEVEL_WARNING, tag, __FILE__, __LINE__, \
              format, ##__VA_ARGS__)
#else
#define LOG_WARN(tag, format, ...) ((void)0)
#endif

#if LOG_COMPILE_LEVEL >= LOG_COMPILE_LEVEL_INFO
#define LOG_INFO(tag, format, ...) \
    log_write(LOG_LEVEL_INFO, tag, __FILE__, __LINE__, \
              format, ##__VA_ARGS__)
#else
#define LOG_INFO(tag, format, ...) ((void)0)
#endif

#if LOG_COMPILE_LEVEL >= LOG_COMPILE_LEVEL_DEBUG
#define LOG_DEBUG(tag, format, ...) \
    log_write(LOG_LEVEL_DEBUG, tag, __FILE__, __LINE__, \
              format, ##__VA_ARGS__)
#else
#define LOG_DEBUG(tag, format, ...) ((void)0)
#endif

// void log_log(const char* format, ...);
// void log_transmit_unlock();



// #if defined(LOG_USE_NPF) && LOG_USE_NPF
// #include "nanoprintf.h"
// #define LOG_INFO(label, msg, ...) log_log(LOG_STR_FORMAT(I, label, msg) __VA_OPT__(, NPF_MAP_ARGS(__VA_ARGS__)))
// #define LOG_DEBUG(label, msg, ...) log_log(LOG_STR_FORMAT(D, label, msg) __VA_OPT__(, NPF_MAP_ARGS(__VA_ARGS__)))
// #define LOG_WARN(label, msg, ...) log_log(LOG_STR_FORMAT(W, label, msg) __VA_OPT__(, NPF_MAP_ARGS(__VA_ARGS__)))
// #define LOG_ERROR(label, msg, ...) log_log(LOG_STR_FORMAT(E, label, msg) __VA_OPT__(, NPF_MAP_ARGS(__VA_ARGS__)))
// #else
// #define LOG_INFO(label, msg, ...) log_log(LOG_STR_FORMAT(I, label, msg), ##__VA_ARGS__)
// #define LOG_DEBUG(label, msg, ...) log_log(LOG_STR_FORMAT(D, label, msg), ##__VA_ARGS__)
// #define LOG_WARN(label, msg, ...) log_log(LOG_STR_FORMAT(W, label, msg), ##__VA_ARGS__)
// #define LOG_ERROR(label, msg, ...) log_log(LOG_STR_FORMAT(E, label, msg), ##__VA_ARGS__)
// #endif


#endif //LOG_H
