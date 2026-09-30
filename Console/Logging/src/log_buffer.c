//
// Created by VanSharkholme on 2026/8/10.
//

#include "log_buffer.h"

static RingBlockBuffer log_rbb;
static uint8_t log_rbb_buffer[LOG_BUFFER_SIZE];
static Block log_rbb_block_pool[LOG_MAX_RECORD_NUM];
static Lock log_rbb_lock;

BufferStatus log_buffer_init(void)
{
    Lock_Init(&log_rbb_lock, Lock_IRQ, NULL);
    BufferStatus status = RingBlockBuffer_Init(
        &log_rbb, log_rbb_buffer, LOG_BUFFER_SIZE,
        log_rbb_block_pool, LOG_MAX_RECORD_NUM,
        &log_rbb_lock
    );
    return status;
}

BufferStatus log_buffer_deinit(void)
{
    return BUFFER_OK;
}