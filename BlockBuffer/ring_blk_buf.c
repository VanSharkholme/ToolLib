/**
 * @file ring_blk_buf.c
 * @brief Contiguous ring allocation, block publication, and descriptor recycling.
 *
 * Free descriptors and active blocks are tracked by intrusive singly linked
 * lists. Allocation uses the active head and tail to locate contiguous space;
 * read and free operations scan the used list. Public operation contracts are
 * documented in ring_blk_buf.h.
 */

#include "ring_blk_buf.h"

/**
 * @brief Enter the instance's optional critical section.
 * @param[in,out] rbb Initialized, non-NULL instance.
 * @return Token from the acquire callback, or zero if no callback is installed.
 * @note The token must be passed unchanged to RingBlockBuffer_Exit().
 */
static uintptr_t RingBlockBuffer_Enter(RingBlockBuffer* rbb)
{
    return rbb->lock.acquire != NULL ? rbb->lock.acquire(&rbb->lock) : 0;
}

/**
 * @brief Leave the instance's optional critical section.
 * @param[in,out] rbb Initialized, non-NULL instance used for the matching entry.
 * @param[in] state Token returned by RingBlockBuffer_Enter().
 * @note Does nothing if no release callback is installed.
 */
static void RingBlockBuffer_Exit(RingBlockBuffer* rbb, uintptr_t state)
{
    if (rbb->lock.release != NULL)
    {
        rbb->lock.release(&rbb->lock, state);
    }
}

/**
 * @brief Check whether a pointer identifies a descriptor in this instance's pool.
 * @param[in] rbb Initialized, non-NULL instance with a valid descriptor pool.
 * @param[in] block Candidate descriptor address; it is not dereferenced.
 * @retval true The address matches an element of the configured descriptor pool.
 * @retval false The address is outside the pool or not on a descriptor boundary.
 * @note This O(1) check assumes monotonically addressed contiguous array storage
 *       with sizeof(Block) stride. Integer address comparisons avoid relational
 *       comparisons between unrelated C pointers. Membership does not establish
 *       ownership, lifecycle state, or freshness after descriptor reuse.
 */
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

/**
 * @brief Append an active block to allocation order and update the used-list tail.
 * @param[in,out] rbb Initialized instance, or NULL for no action.
 * @param[in,out] block Descriptor to append, or NULL for no action.
 * @pre For non-NULL arguments, the descriptor must be detached from any list and
 *      the caller must hold the instance's critical section when locking is enabled.
 * @note Does not acquire a lock or change the block's payload metadata or status.
 */
static void RingBlockBuffer_UsedListAppend(RingBlockBuffer* rbb, Block* block)
{
    if (rbb == NULL || block == NULL)
    {
        return;
    }
    SLinkedList_InsertAt(rbb->tail, &block->list_node);
    rbb->tail = &block->list_node;
}

/**
 * @brief Unlink a block from allocation order and repair the tail if needed.
 * @param[in,out] rbb Initialized instance containing the used list.
 * @param[in] block Descriptor whose list node is sought; the descriptor is not modified.
 * @retval true The node was found and unlinked.
 * @retval false Either argument is NULL or the node is absent from the used list.
 * @pre The caller must hold the instance's critical section when locking is enabled.
 * @note Searches in O(max_block_num). Removing the sole block resets the tail to
 *       the used-list sentinel. The removed node's next pointer is left unchanged.
 */
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

/** @see RingBlockBuffer_Init() for the public initialization contract. */
BufferStatus RingBlockBuffer_Init(
    RingBlockBuffer* rbb, uint8_t* buffer, size_t buffer_size, Block* block_pool, size_t max_block_num,
    const Lock* lock
)
{
    if (rbb == NULL || buffer == NULL || block_pool == NULL || buffer_size == 0 || max_block_num == 0)
    {
        return BUFFER_INVALID_ARGS;
    }
    if (lock != NULL && (lock->acquire == NULL) != (lock->release == NULL))
    {
        return BUFFER_INVALID_ARGS;
    }

    rbb->buffer = buffer;
    rbb->buffer_size = buffer_size;
    rbb->block_pool = block_pool;
    rbb->max_block_num = max_block_num;
    rbb->lock = lock != NULL ? *lock : (Lock){0};

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

/** @see RingBlockBuffer_AllocateBlock() for reservation and ownership requirements. */
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

        /* Situation 1: The active range does not wrap. Try space after the tail
         * first, then wrap to the buffer start if the prefix can fit the block.
         * Internal gaps are not searched. The tail marker shows its payload end.
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
        /* Situation 2: The active range wraps. Allocate only between the tail's
         * payload end and the head; the new block itself must not wrap.
         * The tail marker shows its payload end.
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

/** @see RingBlockBuffer_WriteBlock() for publication and length requirements. */
BufferStatus RingBlockBuffer_WriteBlock(RingBlockBuffer* rbb, Block* block, size_t actual_size)
{
    if (rbb == NULL || block == NULL)
    {
        return BUFFER_INVALID_ARGS;
    }

    BufferStatus result = BUFFER_ERROR;
    uintptr_t state = RingBlockBuffer_Enter(rbb);
    if (RingBlockBuffer_IsPoolBlock(rbb, block) && block->status == BLOCK_ALLOCATED)
    {
        if (actual_size == 0 || actual_size > block->size)
        {
            result = BUFFER_INVALID_ARGS;
        }
        else
        {
            block->size = actual_size;
            block->status = BLOCK_WRITTEN;
            result = BUFFER_OK;
        }
    }
    RingBlockBuffer_Exit(rbb, state);
    return result;
}

/** @see RingBlockBuffer_ReadBlock() for read ordering and consumer ownership. */
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

/** @see RingBlockBuffer_FreeBlock() for release and cancellation requirements. */
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
