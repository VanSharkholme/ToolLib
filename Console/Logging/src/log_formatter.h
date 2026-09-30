//
// Created by VanSharkholme on 2026/8/14.
//

#ifndef H533_TEST_LOG_FORMATTER_H
#define H533_TEST_LOG_FORMATTER_H

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
#define LOG_COLOR_D

#define LOG_STR_FORMAT(color, label, str) LOG_COLOR_ ## color "[" label "] " str LOG_CTRL_RESET

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

#endif //H533_TEST_LOG_FORMATTER_H
