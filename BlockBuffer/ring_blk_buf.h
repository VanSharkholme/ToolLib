//
// Created by VanSharkholme on 2026/9/4.
//

/**
 * @file ring_blk_buf.h
 * @brief Ring buffer API for variable-sized, contiguous payload blocks.
 *
 * The caller supplies payload storage and block descriptors; no memory is
 * allocated dynamically. A producer reserves a block, fills its payload, and
 * publishes it. A consumer claims a published block, reads it, and frees it.
 * Optional per-instance lock callbacks serialize metadata access and publish
 * payload writes; payload access itself takes place outside the critical section.
 */

#ifndef TOOLLIB_BLK_BUF_H
#define TOOLLIB_BLK_BUF_H

#include <stdint.h>
#include "linked_list.h"
#include "lock.h"

/** @brief Result of a ring block buffer operation. */
typedef enum
{
    BUFFER_OK           = 0,  /**< Operation completed successfully. */
    BUFFER_ERROR        = -1, /**< Block membership or state does not permit the operation. */
    BUFFER_INVALID_ARGS = -2, /**< A required pointer, size, or lock callback pair is invalid. */
    BUFFER_FULL         = -3, /**< No free descriptor or sufficient contiguous payload space. */
    BUFFER_EMPTY        = -4, /**< No published, unread block is available. */
} BufferStatus;

/** @brief Block lifecycle: free, reserved, published, then claimed for reading. */
typedef enum
{
    BLOCK_FREE,      /**< Descriptor is available for a new reservation. */
    BLOCK_ALLOCATED, /**< Producer owns the reservation and may fill its payload. */
    BLOCK_WRITTEN,   /**< Payload is published and available to a consumer. */
    BLOCK_READ,      /**< Consumer has claimed the payload; storage is still occupied. */
} BlockStatus;


/**
 * @brief Descriptor for one contiguous payload region.
 * @note All metadata is library-owned and must not be modified by callers.
 *       Only payload bytes may be written while the producer owns a reservation.
 */
typedef struct
{
    uint8_t* data;            /**< Payload address, or NULL when free. */
    size_t size;             /**< Reserved capacity before publication; readable length afterward, in bytes. */
    BlockStatus status;      /**< Current ownership and publication state. */
    SLinkedListNode list_node; /**< Intrusive node in either the free or used list. */
} Block;

/**
 * @brief Ring block buffer state backed by caller-owned storage.
 * @note Members are managed by the library after initialization. Each active
 *       instance requires its own payload storage and descriptor pool; storage
 *       must not overlap that of another active instance.
 */
typedef struct RingBlockBuffer
{
    uint8_t* buffer;          /**< Start of caller-owned payload storage. */
    Block* block_pool;        /**< Caller-owned array of block descriptors. */
    size_t buffer_size;       /**< Payload storage capacity in bytes. */
    size_t max_block_num;     /**< Number of descriptors in block_pool. */
    SLinkedListNode free_list; /**< Sentinel for descriptors available for allocation. */
    SLinkedListNode used_list; /**< Sentinel for active blocks in allocation order. */
    SLinkedListNode* tail;    /**< Last used node, or &used_list when the used list is empty. */
    Lock lock;               /**< Copied lock configuration; absent callbacks disable internal locking. */
} RingBlockBuffer;

/**
 * @brief Initialize an empty ring block buffer using caller-owned storage.
 * @param[out] rbb Instance to initialize.
 * @param[in] buffer Writable payload storage containing at least buffer_size bytes.
 * @param[in] buffer_size Nonzero payload storage capacity in bytes.
 * @param[out] block_pool Writable array containing at least max_block_num descriptors.
 * @param[in] max_block_num Nonzero descriptor count.
 * @param[in] lock Optional lock configuration to copy, or NULL for no internal lock.
 * @retval BUFFER_OK The instance and all descriptors have been initialized.
 * @retval BUFFER_INVALID_ARGS A required pointer is NULL, a capacity is zero,
 *         or exactly one lock callback is supplied. The instance is not modified.
 *
 * @pre Storage and any lock context must remain valid for the instance's lifetime.
 * @pre No operation or payload access may be in progress during initialization.
 *      Reinitialization discards all reservations; old block pointers must no
 *      longer be used.
 * @note Initialization does not acquire the lock or clear payload bytes. The
 *       original Lock object need not outlive this call because it is copied.
 * @note Both lock callbacks must be supplied, or neither. NULL or two absent
 *       callbacks require single-context use or external serialization of all
 *       buffer operations. Supplied callbacks receive the instance's Lock copy
 *       and must provide mutual exclusion and acquire/release memory ordering
 *       for metadata and payload publication. They must not fail or recursively
 *       call this API, and must be valid in every calling context.
 */
BufferStatus RingBlockBuffer_Init(
    RingBlockBuffer* rbb, uint8_t* buffer, size_t buffer_size, Block* block_pool, size_t max_block_num,
    const Lock* lock
);
/**
 * @brief Reserve a contiguous payload region and give it to a producer.
 * @param[in,out] rbb Initialized buffer instance.
 * @param[in] block_size Nonzero reservation size in bytes.
 * @param[out] allocated_block Receives the reserved block; set to NULL on failure
 *            whenever the output pointer itself is non-NULL.
 * @retval BUFFER_OK A BLOCK_ALLOCATED block with size equal to block_size is returned.
 * @retval BUFFER_INVALID_ARGS rbb or allocated_block is NULL, or block_size is zero.
 * @retval BUFFER_FULL Descriptors are exhausted or no suitable contiguous region
 *         is available, including when block_size exceeds the entire buffer.
 *
 * @pre A non-NULL rbb must have been successfully initialized.
 * @note Fill up to block->size payload bytes outside the critical section, then
 *       publish with RingBlockBuffer_WriteBlock() or cancel with
 *       RingBlockBuffer_FreeBlock(). The producer exclusively owns the reservation.
 * @note Allocation extends the current tail or wraps to the buffer start. It
 *       does not combine separated free regions or search internal gaps.
 */
BufferStatus RingBlockBuffer_AllocateBlock(RingBlockBuffer* rbb, size_t block_size, Block** allocated_block);
/**
 * @brief Publish a filled reservation and end producer ownership.
 * @param[in,out] rbb Initialized buffer instance.
 * @param[in,out] block Producer-owned reservation to publish.
 * @param[in] actual_size Number of payload bytes to publish, from 1 through the
 *            reserved block->size inclusive.
 * @retval BUFFER_OK The size is set to actual_size and the state to BLOCK_WRITTEN.
 * @retval BUFFER_INVALID_ARGS rbb or block is NULL, or a valid allocated block
 *         is supplied with a zero or over-capacity actual_size.
 * @retval BUFFER_ERROR block is outside this descriptor pool or is not BLOCK_ALLOCATED.
 *
 * @pre A non-NULL rbb must have been successfully initialized. The producer must
 *      finish writing the payload before publication and must not alter metadata.
 * @note This call copies no payload bytes. Success ends producer ownership:
 *       the producer must stop accessing both the descriptor and its payload.
 *       Failure leaves the reservation unchanged for a retry or cancellation.
 * @note Shrinking the current tail makes its unused suffix available to the next
 *       allocation. Shrinking an earlier block does not immediately reclaim its
 *       internal gap; the suffix can become available when that block becomes tail.
 */
BufferStatus RingBlockBuffer_WriteBlock(RingBlockBuffer* rbb, Block* block, size_t actual_size);
/**
 * @brief Claim the first published, unread block in allocation order.
 * @param[in,out] rbb Initialized buffer instance.
 * @param[out] block Receives the claimed block; set to NULL on failure whenever
 *            the output pointer itself is non-NULL.
 * @retval BUFFER_OK A block is returned and its state changes to BLOCK_READ.
 * @retval BUFFER_INVALID_ARGS rbb or block is NULL.
 * @retval BUFFER_EMPTY The used list contains no BLOCK_WRITTEN block.
 *
 * @pre A non-NULL rbb must have been successfully initialized.
 * @note The scan skips allocated and already claimed blocks, so a later published
 *       reservation may be read before an earlier uncommitted reservation.
 * @note This call copies no payload bytes. The consumer exclusively owns the
 *       returned block, reads block->size bytes outside the critical section,
 *       then releases the block with RingBlockBuffer_FreeBlock().
 * @note The scan is O(max_block_num) and runs inside the configured critical section.
 */
BufferStatus RingBlockBuffer_ReadBlock(RingBlockBuffer* rbb, Block** block);
/**
 * @brief Release a claimed block or cancel an allocated or published block.
 * @param[in,out] rbb Initialized buffer instance.
 * @param[in,out] block Active block to remove from the used list and return to the pool.
 * @retval BUFFER_OK The block is free, with data set to NULL and size set to zero.
 * @retval BUFFER_INVALID_ARGS rbb or block is NULL.
 * @retval BUFFER_ERROR block is outside this pool, is already free, or is absent
 *         from the used list. No block metadata is changed.
 *
 * @pre A non-NULL rbb must have been successfully initialized. The caller must
 *      have exclusive ownership and ensure no producer or consumer is accessing
 *      the block or its payload, including when cancelling a published block.
 * @note Success invalidates all old pointers to the reservation. Descriptor reuse
 *       cannot be distinguished from a stale pointer by this API.
 * @note Payload bytes are not cleared. Removing an interior block does not make
 *       its gap immediately available to the tail-based allocation algorithm.
 * @note The used-list search is O(max_block_num) inside the configured critical section.
 */
BufferStatus RingBlockBuffer_FreeBlock(RingBlockBuffer* rbb, Block* block);


#endif //TOOLLIB_BLK_BUF_H
