#include "blk_buf.h"

static uintptr_t RingBlockBuffer_Enter(RingBlockBuffer* rbb)
{
    return rbb->sync.enter != NULL ? rbb->sync.enter(rbb->sync.context) : 0;
}

static void RingBlockBuffer_Exit(RingBlockBuffer* rbb, uintptr_t state)
{
    if (rbb->sync.exit != NULL)
    {
        rbb->sync.exit(rbb->sync.context, state);
    }
}

static bool RingBlockBuffer_IsPoolBlock(const RingBlockBuffer* rbb, const Block* block)
{
    uintptr_t first = (uintptr_t)rbb->block_pool;
    uintptr_t address = (uintptr_t)block;
    if (address < first)
    {
        return false;
    }

    uintptr_t offset = address - first;
    if (offset % sizeof(Block) != 0)
    {
        return false;
    }

    size_t index = (size_t)(offset / sizeof(Block));
    return index < rbb->max_block_num && block == &rbb->block_pool[index];
}

static void RingBlockBuffer_UsedListAppend(RingBlockBuffer* rbb, Block* block)
{
    if (rbb == NULL || block == NULL)
    {
        return;
    }
    SLinkedList_InsertAt(rbb->tail, &block->list_node);
    rbb->tail = &block->list_node;
}

static bool RingBlockBuffer_UsedListRemove(RingBlockBuffer* rbb, Block* block)
{
    if (rbb == NULL || block == NULL)
    {
        return false;
    }
    SLinkedListNode* prev = &rbb->used_list;
    SLinkedListNode* target = &block->list_node;
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
        return true;
    }
    return false;
}

BufferStatus RingBlockBuffer_Init(
    RingBlockBuffer* rbb, uint8_t* buffer, size_t buffer_size, Block* block_pool, size_t max_block_num,
    const RingBlockBufferSync* sync
)
{
    if (rbb == NULL || buffer == NULL || block_pool == NULL || buffer_size == 0 || max_block_num == 0)
    {
        return BUFFER_INVALID_ARGS;
    }
    if (sync != NULL && (sync->enter == NULL) != (sync->exit == NULL))
    {
        return BUFFER_INVALID_ARGS;
    }

    rbb->buffer = buffer;
    rbb->buffer_size = buffer_size;
    rbb->block_pool = block_pool;
    rbb->max_block_num = max_block_num;
    rbb->sync = sync != NULL ? *sync : (RingBlockBufferSync){0};

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
    if (allocated_block != NULL)
    {
        *allocated_block = NULL;
    }
    if (rbb == NULL || allocated_block == NULL || block_size == 0)
    {
        return BUFFER_INVALID_ARGS;
    }

    BufferStatus result = BUFFER_FULL;
    uint8_t* data = NULL;
    uintptr_t state = RingBlockBuffer_Enter(rbb);
    if (SLinkedList_IsEmpty(&rbb->free_list))
    {
        goto done;
    }

    if (SLinkedList_IsEmpty(&rbb->used_list))
    {
        if (block_size > rbb->buffer_size)
        {
            goto done;
        }
        data = rbb->buffer;
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
                data = tail->data + tail->size;
            }
            else if ((size_t)(head->data - rbb->buffer) >= block_size)
            {
                data = rbb->buffer;
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
                data = tail->data + tail->size;
            }
        }
    }
    if (data != NULL)
    {
        Block* block = SLinkedList_Entry(SLinkedList_Pop(&rbb->free_list), Block, list_node);
        block->data = data;
        block->size = block_size;
        block->status = BLOCK_ALLOCATED;
        RingBlockBuffer_UsedListAppend(rbb, block);
        *allocated_block = block;
        result = BUFFER_OK;
    }

done:
    RingBlockBuffer_Exit(rbb, state);
    return result;
}

BufferStatus RingBlockBuffer_WriteBlock(RingBlockBuffer* rbb, Block* block)
{
    if (rbb == NULL || block == NULL)
    {
        return BUFFER_INVALID_ARGS;
    }

    BufferStatus result = BUFFER_ERROR;
    uintptr_t state = RingBlockBuffer_Enter(rbb);
    if (RingBlockBuffer_IsPoolBlock(rbb, block) && block->status == BLOCK_ALLOCATED)
    {
        block->status = BLOCK_WRITTEN;
        result = BUFFER_OK;
    }
    RingBlockBuffer_Exit(rbb, state);
    return result;
}

BufferStatus RingBlockBuffer_ReadBlock(RingBlockBuffer* rbb, Block** block)
{
    if (block != NULL)
    {
        *block = NULL;
    }
    if (rbb == NULL || block == NULL)
    {
        return BUFFER_INVALID_ARGS;
    }

    BufferStatus result = BUFFER_EMPTY;
    uintptr_t state = RingBlockBuffer_Enter(rbb);
    if (SLinkedList_IsEmpty(&rbb->used_list))
    {
        goto done;
    }

    SLinkedListNode* current = rbb->used_list.next;
    while (current != NULL)
    {
        Block* b = SLinkedList_Entry(current, Block, list_node);
        if (b->status == BLOCK_WRITTEN)
        {
            *block = b;
            b->status = BLOCK_READ;
            result = BUFFER_OK;
            break;
        }
        current = current->next;
    }

done:
    RingBlockBuffer_Exit(rbb, state);
    return result;
}

BufferStatus RingBlockBuffer_FreeBlock(RingBlockBuffer* rbb, Block* block)
{
    if (rbb == NULL || block == NULL)
    {
        return BUFFER_INVALID_ARGS;
    }

    BufferStatus result = BUFFER_ERROR;
    uintptr_t state = RingBlockBuffer_Enter(rbb);
    if (!RingBlockBuffer_IsPoolBlock(rbb, block) || block->status == BLOCK_FREE)
    {
        goto done;
    }

    if (!RingBlockBuffer_UsedListRemove(rbb, block))
    {
        goto done;
    }
    block->status = BLOCK_FREE;
    block->data = NULL;
    block->size = 0;
    SLinkedList_InsertAt(&rbb->free_list, &block->list_node);
    result = BUFFER_OK;

done:
    RingBlockBuffer_Exit(rbb, state);
    return result;
}
