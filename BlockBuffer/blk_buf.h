//
// Created by VanSharkholme on 2026/9/4.
//

#ifndef BLK_BUF_H
#define BLK_BUF_H

#include <stdint.h>
#include "../LinkedList/linked_list.h"

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

typedef struct
{
    uint8_t* data;
    size_t size;
    BlockStatus status;
    SLinkedListNode list_node;
} Block;

typedef struct
{
    uint8_t* buffer;
    Block* block_pool;
    size_t buffer_size;
    size_t max_block_num;
    SLinkedListNode free_list;
    SLinkedListNode used_list;
    SLinkedListNode* tail;
} RingBlockBuffer;


BufferStatus RingBlockBuffer_Init(
    RingBlockBuffer* rbb, uint8_t* buffer, size_t buffer_size, Block* block_pool, size_t max_block_num
);
BufferStatus RingBlockBuffer_AllocateBlock(RingBlockBuffer* rbb, size_t block_size, Block** allocated_block);
BufferStatus RingBlockBuffer_WriteBlock(Block* block);
BufferStatus RingBlockBuffer_ReadBlock(RingBlockBuffer* rbb, Block** block);
BufferStatus RingBlockBuffer_FreeBlock(RingBlockBuffer* rbb, Block* block);


#endif //BLK_BUF_H
