//
// Created by VanSharkholme on 2026/8/14.
//

#include "log_formatter.h"

#include <string.h>
#if !LOG_USE_CUSTOM_VSNPRINTF
#include <stdio.h>
#define LOG_FORMATTER_VSNPRINTF vsnprintf
#endif

#if LOG_ENABLE_COLOR
/**
 * @brief Write the color code corresponding to the log level into the buffer.
 * @param buffer The buffer to write the color code into.
 * @param buffer_size The size of the buffer.
 * @param level The log level.
 * @return The number of characters written to the buffer(without ending \0), or 0 if an error occurred.
 */
static size_t log_formatter_write_color(char *buffer, size_t buffer_size, Log_Level level)
{
    size_t offset = 0;
    char *color_str = NULL;
    size_t color_str_len = 0;
    switch (level)
    {
    case LOG_LEVEL_ERROR:
        color_str = LOG_COLOR_E;
        color_str_len = sizeof(LOG_COLOR_E) - 1;
        break;
    case LOG_LEVEL_WARNING:
        color_str = LOG_COLOR_W;
        color_str_len = sizeof(LOG_COLOR_W) - 1;
        break;
    case LOG_LEVEL_INFO:
        color_str = LOG_COLOR_I;
        color_str_len = sizeof(LOG_COLOR_I) - 1;
        break;
    case LOG_LEVEL_DEBUG:
        color_str = LOG_COLOR_D;
        color_str_len = sizeof(LOG_COLOR_D) - 1;
        break;
    default:
        color_str = NULL;
        color_str_len = 0;
        break;
    }
    if (color_str != NULL && color_str_len < buffer_size)
    {
        memcpy(buffer, color_str, color_str_len);
        offset = color_str_len;
        buffer[offset] = '\0'; // Null-terminate the string
    }
    return offset;
}
#endif

/**
 * @brief Write the log level into the buffer.
 * @param buffer The buffer to write the log level into.
 * @param buffer_size The size of the buffer.
 * @param level The log level.
 * @return The number of characters written to the buffer(without ending \0).
 */
static size_t log_formatter_write_level(char *buffer, size_t buffer_size, Log_Level level)
{
    size_t offset = 0;
    char level_char = '?';
    switch (level)
    {
    case LOG_LEVEL_ERROR:
        level_char = 'E';
        break;
    case LOG_LEVEL_WARNING:
        level_char = 'W';
        break;
    case LOG_LEVEL_INFO:
        level_char = 'I';
        break;
    case LOG_LEVEL_DEBUG:
        level_char = 'D';
        break;
    default:
        level_char = '?';
        break;
    }
    char tmp[3] = {'[', level_char, ']'};
    while (offset < sizeof(tmp) && offset < buffer_size - 1)
    {
        buffer[offset] = tmp[offset];
        offset++;
    }
    buffer[offset] = '\0'; // Null-terminate the string
    return offset;
}

#if LOG_ENABLE_FILE_LINE
/**
 * @brief Write the file name into the buffer. Only the filename will be written, all path prefix will be removed.
 * @param buffer The buffer to write the file name into.
 * @param buffer_size The size of the buffer.
 * @param file The file name to write, expanded from macro __FILE__.
 * @return The number of characters written to the buffer(without ending \0).
 */
static size_t log_formatter_write_file_name(char *buffer, size_t buffer_size, const char *file)
{
    size_t offset = 0;

    // Find the beginning of filename
    const char *file_name = file;
    const char *cur = NULL;
    for (cur = file; *cur != '\0'; cur++)
    {
        if (*cur == '/' || *cur == '\\')
        {
            file_name = cur + 1;
        }
    }
    size_t file_name_len = cur - file_name;
    bool truncated = file_name_len > LOG_MAX_FILE_NAME_LENGTH;
    size_t write_len = truncated? LOG_MAX_FILE_NAME_LENGTH - 3: file_name_len;
    while (offset < write_len && offset < buffer_size - 1)
    {
        buffer[offset++] = *file_name++;
    }
    if (truncated)
    {
        while (offset < LOG_MAX_FILE_NAME_LENGTH)
            buffer[offset++] = '.';
    }
    buffer[offset] = '\0'; // Null-terminate the string
    return offset;
}

/**
 * @brief Find out digit number of an uint32_t, fast for number within 10000.
 * @param n The number which digits are calculated.
 * @return The digit number under base 10 for n.
 */
static uint32_t log_formatter_get_num_digits(uint32_t n)
{
    if (n < 10000) {
        if (n < 10)   return 1;
        if (n < 100)  return 2;
        if (n < 1000) return 3;
        return 4;
    }
    // Cold path, hardly happens
    int d = 4;
    do {
        n /= 10000;
        d += 4;
    } while (n >= 10000);
    if (n >= 1000) return d;
    if (n >= 100)  return d - 1;
    if (n >= 10)   return d - 2;
    return d - 3;
}

/**
 * @brief Write the line number into the buffer.
 * @param buffer The buffer to write the line number into.
 * @param buffer_size The size of the buffer.
 * @param line An uint32_t line number expanded from macro __LINE__.
 * @return The number of characters written to the buffer(without ending \0).
 */
static size_t log_formatter_write_line_num(char *buffer, size_t buffer_size, uint32_t line, bool *truncated)
{
    size_t digits = log_formatter_get_num_digits(line);
    size_t offset = 0;
    if (digits < buffer_size)
    {
        offset = digits;
        *truncated = false;
    }
    else
    {
        offset = buffer_size - 1;
        *truncated = true;
    }
    char *convert_start = buffer + offset;
    *convert_start-- = '\0';
    while (convert_start >= buffer)
    {
        *convert_start-- = (char)('0' + (line % 10));
        line /= 10;
    }
    return offset;
}
#endif


static size_t log_formatter_write_tag(char *buffer, size_t buffer_size, const char *tag)
{
    size_t offset = 0;

    while (*tag && offset < LOG_MAX_TAG_LENGTH && offset < buffer_size)
    {
        buffer[offset++] = *tag++;
    }
    if (offset == LOG_MAX_TAG_LENGTH && *tag != '\0')
    {
        buffer[offset - 1] = '.';
        buffer[offset - 2] = '.';
        buffer[offset - 3] = '.';
    }
    buffer[offset] = '\0';
    return offset;
}

// /Color/[E][TAG(File:Line)] Message
size_t log_formatter_construct_prefix(
    char *buffer, size_t buffer_size,
    Log_Level level, const char *tag,
    const char *file, uint32_t line
)
{
    size_t offset = 0;
#if LOG_ENABLE_COLOR
    offset += log_formatter_write_color(buffer + offset, buffer_size - offset, level);
#endif
    offset += log_formatter_write_level(buffer + offset, buffer_size - offset, level);
    buffer[offset++] = '[';
    offset += log_formatter_write_tag(buffer + offset, buffer_size - offset, tag);
#if LOG_ENABLE_FILE_LINE
    buffer[offset++] = '(';
    offset += log_formatter_write_file_name(buffer + offset, buffer_size - offset, file);
    buffer[offset++] = ':';
    bool line_num_truncated = false;
    offset += log_formatter_write_line_num(buffer + offset, buffer_size - offset, line, &line_num_truncated);
    buffer[offset++] = ')';
#endif
    buffer[offset++] = ']';
    buffer[offset++] = ' ';
    return offset;
}

size_t log_formatter_write_tail(char *buffer, size_t buffer_size)
{
    size_t offset = 0;

#if LOG_ENABLE_COLOR
    memcpy(buffer + offset, LOG_CTRL_RESET, sizeof(LOG_CTRL_RESET));
    offset += sizeof(LOG_CTRL_RESET);
#endif

#if LOG_AUTO_NEWLINE
    buffer[offset++] = '\n';
#endif
    buffer[offset] = '\0';
}

size_t log_formatter_process(
    char *buffer, size_t buffer_size,
    Log_Level level, const char *tag,
    const char *file, uint32_t line,
    const char *format, va_list args
)
{
    size_t offset = log_formatter_construct_prefix(buffer, buffer_size, level, tag, file, line);
    size_t body_capacity = buffer_size - offset - LOG_LEN_CALC_TAIL_LEN;
    int content_needed_size = LOG_FORMATTER_VSNPRINTF(buffer + offset, body_capacity + 1, format, args);
    if (content_needed_size < 0)
    {
        return 0;
    }

    if (content_needed_size > body_capacity)
    {
        offset += body_capacity;
        buffer[offset - 1] = '.';
        buffer[offset - 2] = '.';
        buffer[offset - 3] = '.';
    }
    else
    {
        offset += content_needed_size;
    }
    offset += log_formatter_write_tail(buffer + offset, buffer_size - offset);


    return offset;
}
