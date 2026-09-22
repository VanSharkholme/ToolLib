#include "blk_buf.h"

static void RingBlockBuffer_UsedListAppend(RingBlockBuffer* rbb, Block* block)
{
    if (rbb == NULL || block == NULL)
    {
        return;
    }
    SLinkedList_InsertAt(rbb->tail, &block->list_node);
    rbb->tail = &block->list_node;
}

static void RingBlockBuffer_UsedListRemove(RingBlockBuffer* rbb, Block* block)
{
    if (rbb == NULL || block == NULL)
    {
        return;
    }
    SLinkedListNode *prev = &rbb->used_list;
    SLinkedListNode *target = &block->list_node;
    while (prev != NULL && prev->next != target)
    {
        prev = prev->next;
    }
    if (prev != NULL)
    {
        prev->next = target->next;
        if (rbb->tail == target)
        {
            rbb->tail = prev;
        }
    }
}

BufferStatus RingBlockBuffer_Init(
    RingBlockBuffer* rbb, uint8_t* buffer, size_t buffer_size, Block* block_pool, size_t max_block_num
)
{
    if (rbb == NULL || buffer == NULL || block_pool == NULL || buffer_size == 0 || max_block_num == 0)
    {
        return BUFFER_INVALID_ARGS;
    }

    rbb->buffer = buffer;
    rbb->buffer_size = buffer_size;
    rbb->block_pool = block_pool;
    rbb->max_block_num = max_block_num;

    SLinkedList_Init(&rbb->free_list);
    SLinkedList_Init(&rbb->used_list);

    rbb->tail = &rbb->used_list;
    for (size_t i = 0; i < max_block_num; i++)
    {
        block_pool[i].data = NULL;
        block_pool[i].size = 0;
        block_pool[i].status = BLOCK_FREE;
        SLinkedList_InsertAt(&rbb->free_list, &block_pool[i].list_node);
    }

    return BUFFER_OK;
}

BufferStatus RingBlockBuffer_AllocateBlock(RingBlockBuffer* rbb, size_t block_size, Block** allocated_block)
{
    if (rbb == NULL || allocated_block == NULL || block_size == 0)
    {
        return BUFFER_INVALID_ARGS;
    }

    if (SLinkedList_IsEmpty(&rbb->free_list))
    {
        return BUFFER_FULL;
    }

    if (SLinkedList_IsEmpty(&rbb->used_list))
    {
        if (block_size > rbb->buffer_size)
        {
            return BUFFER_FULL;
        }
        *allocated_block = SLinkedList_Entry(SLinkedList_Pop(&rbb->free_list), Block, list_node);
        (*allocated_block)->data = rbb->buffer;
        (*allocated_block)->size = block_size;
        (*allocated_block)->status = BLOCK_ALLOCATED;
        RingBlockBuffer_UsedListAppend(rbb, *allocated_block);
    }
    else
    {
        Block* head = SLinkedList_First_Entry(&rbb->used_list, Block, list_node);
        Block* tail = SLinkedList_Entry(rbb->tail, Block, list_node);

        /* Situation 1:
         * +----------------+---------------------------------------+--------------+
         * |   free block   |               used blocks             |  free block  |
         * +----------------+---------------------------------------+--------------+
         *                  ^                                       ^
         *                 head                                    tail
         */
        if (head->data <= tail->data)
        {
            if ((size_t)(rbb->buffer + rbb->buffer_size - (tail->data + tail->size)) >= block_size)
            {
                *allocated_block = SLinkedList_Entry(SLinkedList_Pop(&rbb->free_list), Block, list_node);
                (*allocated_block)->data = tail->data + tail->size;
                (*allocated_block)->size = block_size;
                (*allocated_block)->status = BLOCK_ALLOCATED;
                RingBlockBuffer_UsedListAppend(rbb, *allocated_block);
            }
            else if ((size_t)(head->data - rbb->buffer) >= block_size)
            {
                *allocated_block = SLinkedList_Entry(SLinkedList_Pop(&rbb->free_list), Block, list_node);
                (*allocated_block)->data = rbb->buffer;
                (*allocated_block)->size = block_size;
                (*allocated_block)->status = BLOCK_ALLOCATED;
                RingBlockBuffer_UsedListAppend(rbb, *allocated_block);
            }
            else
            {
                return BUFFER_FULL;
            }
        }
        /* Situation 2:
         * +----------------+---------------------------------------+--------------+
         * |   used block   |               free blocks             |  used block  |
         * +----------------+---------------------------------------+--------------+
         *            ^                                             ^
         *           tail                                          head
         */
        else
        {
            if ((size_t)(head->data - (tail->data + tail->size)) >= block_size)
            {
                *allocated_block = SLinkedList_Entry(SLinkedList_Pop(&rbb->free_list), Block, list_node);
                (*allocated_block)->data = tail->data + tail->size;
                (*allocated_block)->size = block_size;
                (*allocated_block)->status = BLOCK_ALLOCATED;
                RingBlockBuffer_UsedListAppend(rbb, *allocated_block);
            }
            else
            {
                return BUFFER_FULL;
            }
        }
    }
    return BUFFER_OK;
}

BufferStatus RingBlockBuffer_WriteBlock(Block* block)
{
    if (block == NULL)
    {
        return BUFFER_INVALID_ARGS;
    }
    block->status = BLOCK_WRITTEN;
    return BUFFER_OK;
}

BufferStatus RingBlockBuffer_ReadBlock(RingBlockBuffer* rbb, Block** block)
{
    if (rbb == NULL || block == NULL)
    {
        return BUFFER_INVALID_ARGS;
    }

    if (SLinkedList_IsEmpty(&rbb->used_list))
    {
        return BUFFER_EMPTY;
    }

    SLinkedListNode* current = rbb->used_list.next;
    while (current != NULL)
    {
        Block* b = SLinkedList_Entry(current, Block, list_node);
        if (b->status == BLOCK_WRITTEN)
        {
            *block = b;
            break;
        }
        current = current->next;
    }

    if (current == NULL)
    {
        return BUFFER_EMPTY;
    }

    (*block)->status = BLOCK_READ;

    return BUFFER_OK;
}

BufferStatus RingBlockBuffer_FreeBlock(RingBlockBuffer* rbb, Block* block)
{
    if (rbb == NULL || block == NULL)
    {
        return BUFFER_INVALID_ARGS;
    }

    if (block->status == BLOCK_FREE)
    {
        return BUFFER_ERROR;
    }

    RingBlockBuffer_UsedListRemove(rbb, block);
    block->status = BLOCK_FREE;
    block->data = NULL;
    block->size = 0;
    SLinkedList_InsertAt(&rbb->free_list, &block->list_node);

    return BUFFER_OK;
}
