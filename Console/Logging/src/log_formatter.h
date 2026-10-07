//
// Created by VanSharkholme on 2026/8/14.
//

#ifndef TOOLLIB_LOG_FORMATTER_H
#define TOOLLIB_LOG_FORMATTER_H

#include <stddef.h>
#include <stdarg.h>
#include "log_types.h"
#include "log_config.h"

//
// Control sequences, based on ANSI.
// Can be used to control color, and clear the screen
//
#define LOG_CTRL_RESET                "\x1B[0m"         // Reset to default colors
#define LOG_CTRL_CLEAR                "\x1B[2J"         // Clear screen, reposition cursor to top left

#define LOG_CTRL_TEXT_BLACK           "\x1B[2;30m"
#define LOG_CTRL_TEXT_RED             "\x1B[2;31m"
#define LOG_CTRL_TEXT_GREEN           "\x1B[2;32m"
#define LOG_CTRL_TEXT_YELLOW          "\x1B[2;33m"
#define LOG_CTRL_TEXT_BLUE            "\x1B[2;34m"
#define LOG_CTRL_TEXT_MAGENTA         "\x1B[2;35m"
#define LOG_CTRL_TEXT_CYAN            "\x1B[2;36m"
#define LOG_CTRL_TEXT_WHITE           "\x1B[2;37m"

#define LOG_CTRL_TEXT_BRIGHT_BLACK    "\x1B[1;30m"
#define LOG_CTRL_TEXT_BRIGHT_RED      "\x1B[1;31m"
#define LOG_CTRL_TEXT_BRIGHT_GREEN    "\x1B[1;32m"
#define LOG_CTRL_TEXT_BRIGHT_YELLOW   "\x1B[1;33m"
#define LOG_CTRL_TEXT_BRIGHT_BLUE     "\x1B[1;34m"
#define LOG_CTRL_TEXT_BRIGHT_MAGENTA  "\x1B[1;35m"
#define LOG_CTRL_TEXT_BRIGHT_CYAN     "\x1B[1;36m"
#define LOG_CTRL_TEXT_BRIGHT_WHITE    "\x1B[1;37m"

#define LOG_CTRL_BG_BLACK             "\x1B[24;40m"
#define LOG_CTRL_BG_RED               "\x1B[24;41m"
#define LOG_CTRL_BG_GREEN             "\x1B[24;42m"
#define LOG_CTRL_BG_YELLOW            "\x1B[24;43m"
#define LOG_CTRL_BG_BLUE              "\x1B[24;44m"
#define LOG_CTRL_BG_MAGENTA           "\x1B[24;45m"
#define LOG_CTRL_BG_CYAN              "\x1B[24;46m"
#define LOG_CTRL_BG_WHITE             "\x1B[24;47m"

#define LOG_CTRL_BG_BRIGHT_BLACK      "\x1B[4;40m"
#define LOG_CTRL_BG_BRIGHT_RED        "\x1B[4;41m"
#define LOG_CTRL_BG_BRIGHT_GREEN      "\x1B[4;42m"
#define LOG_CTRL_BG_BRIGHT_YELLOW     "\x1B[4;43m"
#define LOG_CTRL_BG_BRIGHT_BLUE       "\x1B[4;44m"
#define LOG_CTRL_BG_BRIGHT_MAGENTA    "\x1B[4;45m"
#define LOG_CTRL_BG_BRIGHT_CYAN       "\x1B[4;46m"
#define LOG_CTRL_BG_BRIGHT_WHITE      "\x1B[4;47m"

#define LOG_COLOR_E LOG_CTRL_TEXT_BRIGHT_RED
#define LOG_COLOR_W LOG_CTRL_TEXT_BRIGHT_YELLOW
#define LOG_COLOR_I LOG_CTRL_TEXT_BRIGHT_GREEN
#define LOG_COLOR_D LOG_CTRL_TEXT_WHITE


#if defined(LOG_ENABLE_COLOR) && LOG_ENABLE_COLOR
#define LOG_LEN_CALC_COLOR_HEAD_LEN 7
#define LOG_LEN_CALC_COLOR_TAIL_LEN 4
#else
#define LOG_LEN_CALC_COLOR_HEAD_LEN 0
#define LOG_LEN_CALC_COLOR_TAIL_LEN 0
#endif
#define LOG_LEN_CALC_COLOR_TOTAL_LEN (LOG_LEN_CALC_COLOR_HEAD_LEN + LOG_LEN_CALC_COLOR_TAIL_LEN)

#if defined(LOG_ENABLE_FILE_LINE) && LOG_ENABLE_FILE_LINE
#define LOG_LEN_CALC_MAX_FILENAME_LEN LOG_MAX_FILE_NAME_LENGTH
#define LOG_LEN_CALC_MAX_LINE_NUM_LEN 10
#define LOG_LEN_CALC_FILELINE_MISC_LEN 3 // '(' + ')' + ':'
#else
#define LOG_LEN_CALC_MAX_FILENAME_LEN 0
#define LOG_LEN_CALC_MAX_LINE_NUM_LEN 0
#define LOG_LEN_CALC_FILELINE_MISC_LEN 0
#endif
#define LOG_LEN_CALC_FILELINE_LEN (LOG_LEN_CALC_MAX_FILENAME_LEN + LOG_LEN_CALC_MAX_LINE_NUM_LEN + LOG_LEN_CALC_FILELINE_MISC_LEN)

#if defined(LOG_AUTO_NEWLINE) && LOG_AUTO_NEWLINE
#define LOG_LEN_CALC_NEW_LINE_LEN 1
#else
#define LOG_LEN_CALC_NEW_LINE_LEN 0
#endif

#define LOG_LEN_CALC_LEVEL_LEN 3 // [E/W/I/D]
#define LOG_LEN_CALC_TAIL_LEN (LOG_LEN_CALC_COLOR_TAIL_LEN+LOG_LEN_CALC_NEW_LINE_LEN+1) // color_reset + '\n'+'\0'
#define LOG_LEN_CALC_MISC_LEN 3 // '[' + ']' + ' '
#define LOG_LEN_CALC_MIN_SINGLE_LOG_LEN \
    (LOG_LEN_CALC_COLOR_HEAD_LEN + LOG_LEN_CALC_LEVEL_LEN + LOG_MAX_TAG_LENGTH +\
    LOG_LEN_CALC_FILELINE_LEN + LOG_MIN_CONTENT_LENGTH +\
    LOG_LEN_CALC_TAIL_LEN + LOG_LEN_CALC_MISC_LEN)

#if LOG_LEN_CALC_MIN_SINGLE_LOG_LEN > LOG_MAX_SINGLE_LOG_LENGTH
#error "Not enough space for logging, increase LOG_MAX_SINGLE_LOG_LENGTH or decrease LOG_MAX_CONTENT_LENGTH and LOG_MAX_TAG_LENGTH."
#endif

typedef enum
{
    RED,
    GREEN,
    YELLOW,
    BLUE,
    MAGENTA,
    CYAN,
    WHITE,
    GRAY
} LOG_TEXT_COLOR;

size_t log_formatter_process(
    char *buffer, size_t buffer_size,
    Log_Level level, const char *tag,
    const char *file, uint32_t line,
    const char *format, va_list args
);
#endif //TOOLLIB_LOG_FORMATTER_H
