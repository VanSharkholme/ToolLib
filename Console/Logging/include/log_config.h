//
// Created by VanSharkholme on 2026/8/10.
//

#ifndef TOOLLIB_LOG_CONFIG_H
#define TOOLLIB_LOG_CONFIG_H

#define LOG_ENABLE_COLOR            1
#define LOG_ENABLE_FILE_LINE        1
#define LOG_ENABLE_TIMESTAMP        1
#define LOG_AUTO_NEWLINE            1

#define LOG_BUFFER_SIZE             2048
#define LOG_MAX_RECORD_NUM          8
#define LOG_MAX_SINGLE_LOG_LENGTH   126
#define LOG_MAX_TAG_LENGTH          15
#define LOG_MIN_CONTENT_LENGTH      64
#define LOG_MIN_TAG_LENGTH          5
#define LOG_MAX_FILE_NAME_LENGTH    15

#define LOG_USE_ASYNC               1
#define LOG_USE_CUSTOM_VSNPRINTF    0

#endif //TOOLLIB_LOG_CONFIG_H
