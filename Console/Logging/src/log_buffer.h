//
// Created by VanSharkholme on 2026/8/10.
//

#ifndef H533_TEST_LOG_BUFFER_H
#define H533_TEST_LOG_BUFFER_H

#include "log_config.h"
#include "ring_blk_buf.h"

BufferStatus log_buffer_init(void);
BufferStatus log_buffer_deinit(void);

#endif //H533_TEST_LOG_BUFFER_H
