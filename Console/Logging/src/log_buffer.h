//
// Created by VanSharkholme on 2026/8/10.
//

#ifndef TOOLLIB_LOG_BUFFER_H
#define TOOLLIB_LOG_BUFFER_H

#include "log_config.h"
#include "ring_blk_buf.h"

BufferStatus log_buffer_init(void);
BufferStatus log_buffer_deinit(void);
BufferStatus log_buffer_allocate(Block **buffer);
BufferStatus log_buffer_free(Block *buffer);
BufferStatus log_buffer_write(Block *buffer, size_t size);
BufferStatus log_buffer_read(Block **buffer);

#endif //TOOLLIB_LOG_BUFFER_H
