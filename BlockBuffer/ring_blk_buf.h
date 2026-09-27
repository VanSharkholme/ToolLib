//
// Created by VanSharkholme on 2026/9/4.
//

#ifndef TOOLLIB_BLK_BUF_H
#define TOOLLIB_BLK_BUF_H

#include <stdint.h>
#include "linked_list.h"

typedef enum
{
    BUFFER_OK           = 0,
    BUFFER_ERROR        = -1,
    BUFFER_INVALID_ARGS = -2,
    BUFFER_FULL         = -3,
    BUFFER_EMPTY        = -4,
} BufferStatus;

typedef enum
{
    BLOCK_FREE,
    BLOCK_ALLOCATED,
    BLOCK_WRITTEN,
    BLOCK_READ,
} BlockStatus;

/* Callbacks must acquire/release mutual exclusion and payload visibility. */
typedef uintptr_t (*RingBlockBufferCriticalEnter)(void* context);
typedef void (*RingBlockBufferCriticalExit)(void* context, uintptr_t state);

typedef struct
{
    RingBlockBufferCriticalEnter enter;
    RingBlockBufferCriticalExit exit;
    void* context;
} RingBlockBufferSync;

typedef struct
{
    uint8_t* data;
    size_t size;
    BlockStatus status;
    SLinkedListNode list_node;
} Block;

typedef struct RingBlockBuffer
{
    uint8_t* buffer;
    Block* block_pool;
    size_t buffer_size;
    size_t max_block_num;
    SLinkedListNode free_list;
    SLinkedListNode used_list;
    SLinkedListNode* tail;
    RingBlockBufferSync sync;
} RingBlockBuffer;

/* NULL sync selects externally serialized / single-context operation. */
BufferStatus RingBlockBuffer_Init(
    RingBlockBuffer* rbb, uint8_t* buffer, size_t buffer_size, Block* block_pool, size_t max_block_num,
    const RingBlockBufferSync* sync
);
BufferStatus RingBlockBuffer_AllocateBlock(RingBlockBuffer* rbb, size_t block_size, Block** allocated_block);
/* Commits 1..reserved-size bytes, publishes the payload, and ends producer ownership.
 * Before this call block->size is the reservation; afterward it is the readable length. */
BufferStatus RingBlockBuffer_WriteBlock(RingBlockBuffer* rbb, Block* block, size_t actual_size);
BufferStatus RingBlockBuffer_ReadBlock(RingBlockBuffer* rbb, Block** block);
BufferStatus RingBlockBuffer_FreeBlock(RingBlockBuffer* rbb, Block* block);


#endif //TOOLLIB_BLK_BUF_H
