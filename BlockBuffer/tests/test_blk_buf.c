#include <stdint.h>
#include "ring_blk_buf.h"
#include "test_runner.h"

enum { CAPACITY = 64, BLOCK_COUNT = 8, GUARD_SIZE = 8 };
static struct
{
    uint8_t before[GUARD_SIZE];
    uint8_t data[CAPACITY];
    uint8_t after[GUARD_SIZE];
} storage;
static RingBlockBuffer rbb;
static Block pool[BLOCK_COUNT];

typedef struct
{
    unsigned acquires;
    unsigned releases;
    uintptr_t state;
    bool held;
} LockProbe;

static uintptr_t probe_acquire(Lock* lock)
{
    LockProbe* probe = lock->context;
    TEST_ASSERT_FALSE(probe->held);
    probe->held = true;
    probe->state = (uintptr_t)(0x1234u + ++probe->acquires);
    return probe->state;
}

static void probe_release(Lock* lock, uintptr_t state)
{
    LockProbe* probe = lock->context;
    TEST_ASSERT_TRUE(probe->held);
    TEST_ASSERT_TRUE(probe->state == state);
    probe->held = false;
    ++probe->releases;
}

static void assert_lock_probe_balanced(const LockProbe* probe, unsigned expected)
{
    TEST_ASSERT_EQUAL_UINT(expected, probe->acquires);
    TEST_ASSERT_EQUAL_UINT(expected, probe->releases);
    TEST_ASSERT_FALSE(probe->held);
}

void setUp(void)
{
    memset(&storage, 0xa5, sizeof storage);
    memset(&rbb, 0, sizeof rbb);
    memset(pool, 0, sizeof pool);
    TEST_ASSERT_EQUAL_INT(BUFFER_OK,
        RingBlockBuffer_Init(&rbb, storage.data, CAPACITY, pool, BLOCK_COUNT, NULL));
}

void tearDown(void)
{
    TEST_ASSERT_EACH_EQUAL_UINT8(0xa5, storage.before, GUARD_SIZE);
    TEST_ASSERT_EACH_EQUAL_UINT8(0xa5, storage.after, GUARD_SIZE);
}

/* Validate with bounded traversal, without trusting the list implementation.
 * Every descriptor must occur once, ranges must be in bounds and disjoint,
 * and tail must be the actual final used node (or the empty-list sentinel). */
static void assert_integrity(size_t expected_used)
{
    bool seen[BLOCK_COUNT] = {false};
    size_t used = 0;
    size_t free_count = 0;
    const SLinkedListNode *last = &rbb.used_list;
    for (int list = 0; list < 2; ++list)
    {
        const SLinkedListNode *node = list ? rbb.free_list.next : rbb.used_list.next;
        size_t steps = 0;
        while (node != NULL)
        {
            TEST_ASSERT_TRUE_MESSAGE(steps++ < rbb.max_block_num, "List contains a cycle");
            size_t i = 0;
            while (i < rbb.max_block_num && node != &pool[i].list_node)
                ++i;
            TEST_ASSERT_TRUE_MESSAGE(i < rbb.max_block_num, "Node is outside block pool");
            TEST_ASSERT_FALSE_MESSAGE(seen[i], "Descriptor appears in both lists or twice");
            seen[i] = true;
            if (list)
            {
                ++free_count;
                TEST_ASSERT_EQUAL_INT(BLOCK_FREE, pool[i].status);
                TEST_ASSERT_NULL(pool[i].data);
                TEST_ASSERT_EQUAL_size_t(0, pool[i].size);
            }
            else
            {
                ++used;
                last = node;
                TEST_ASSERT_NOT_EQUAL(BLOCK_FREE, pool[i].status);
                TEST_ASSERT_NOT_NULL(pool[i].data);
                uintptr_t offset = (uintptr_t)pool[i].data - (uintptr_t)storage.data;
                TEST_ASSERT_TRUE_MESSAGE(offset < rbb.buffer_size, "Data outside buffer");
                TEST_ASSERT_TRUE(pool[i].size > 0);
                TEST_ASSERT_TRUE_MESSAGE(pool[i].size <= rbb.buffer_size - offset,
                                         "Block extends past buffer end");
            }
            node = node->next;
        }
    }
    TEST_ASSERT_EQUAL_size_t(expected_used, used);
    TEST_ASSERT_EQUAL_size_t(rbb.max_block_num, used + free_count);
    TEST_ASSERT_EQUAL_PTR(last, rbb.tail);
    for (size_t i = 0; i < rbb.max_block_num; ++i)
        for (size_t j = i + 1; j < rbb.max_block_num; ++j)
            if (pool[i].status != BLOCK_FREE && pool[j].status != BLOCK_FREE)
                TEST_ASSERT_TRUE_MESSAGE(pool[i].data + pool[i].size <= pool[j].data ||
                                         pool[j].data + pool[j].size <= pool[i].data,
                                         "Live blocks overlap");
}

/* White-box fixture: reach ring layouts independently of allocation/free bugs.
 * This prepares real descriptors; none of the functions under test are mocked. */
static void seed_layout(const size_t *offsets, const size_t *sizes, size_t count)
{
    rbb.used_list.next = count ? &pool[0].list_node : NULL;
    rbb.tail = count ? &pool[count - 1].list_node : &rbb.used_list;
    rbb.free_list.next = count < BLOCK_COUNT ? &pool[count].list_node : NULL;
    for (size_t i = 0; i < BLOCK_COUNT; ++i)
    {
        pool[i].data = i < count ? storage.data + offsets[i] : NULL;
        pool[i].size = i < count ? sizes[i] : 0;
        pool[i].status = i < count ? BLOCK_ALLOCATED : BLOCK_FREE;
        bool has_next = i < count ? i + 1 < count : i + 1 < BLOCK_COUNT;
        pool[i].list_node.next = has_next ? &pool[i + 1].list_node : NULL;
    }
    assert_integrity(count);
}

static void seed_two(size_t first_offset, size_t first_size,
                     size_t second_offset, size_t second_size)
{
    const size_t offsets[] = {first_offset, second_offset};
    const size_t sizes[] = {first_size, second_size};
    seed_layout(offsets, sizes, 2);
}

static Block *allocate(size_t size)
{
    Block *block = NULL;
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_AllocateBlock(&rbb, size, &block));
    TEST_ASSERT_NOT_NULL(block);
    TEST_ASSERT_EQUAL_size_t(size, block->size);
    TEST_ASSERT_EQUAL_INT(BLOCK_ALLOCATED, block->status);
    return block;
}

static void assert_allocate_full_preserves_state(size_t size)
{
    RingBlockBuffer old_rbb;
    Block old_pool[BLOCK_COUNT];
    uint8_t old_data[CAPACITY];
    memcpy(&old_rbb, &rbb, sizeof rbb);
    memcpy(old_pool, pool, sizeof pool);
    memcpy(old_data, storage.data, sizeof old_data);
    Block *block = NULL;
    TEST_ASSERT_EQUAL_INT(BUFFER_FULL, RingBlockBuffer_AllocateBlock(&rbb, size, &block));
    TEST_ASSERT_NULL(block);
    TEST_ASSERT_EQUAL_MEMORY(&old_rbb, &rbb, sizeof rbb);
    TEST_ASSERT_EQUAL_MEMORY(old_pool, pool, sizeof pool);
    TEST_ASSERT_EQUAL_MEMORY(old_data, storage.data, sizeof old_data);
}

static void test_init_sets_fields_and_all_descriptors_free(void)
{
    TEST_ASSERT_EQUAL_PTR(storage.data, rbb.buffer);
    TEST_ASSERT_EQUAL_PTR(pool, rbb.block_pool);
    TEST_ASSERT_EQUAL_size_t(CAPACITY, rbb.buffer_size);
    TEST_ASSERT_EQUAL_size_t(BLOCK_COUNT, rbb.max_block_num);
    TEST_ASSERT_NULL(rbb.lock.acquire);
    TEST_ASSERT_NULL(rbb.lock.release);
    TEST_ASSERT_NULL(rbb.lock.context);
    assert_integrity(0);
}

static void test_init_rejects_null_arguments_and_zero_pool(void)
{
    TEST_ASSERT_EQUAL_INT(BUFFER_INVALID_ARGS, RingBlockBuffer_Init(NULL, storage.data, CAPACITY, pool, BLOCK_COUNT, NULL));
    TEST_ASSERT_EQUAL_INT(BUFFER_INVALID_ARGS, RingBlockBuffer_Init(&rbb, NULL, CAPACITY, pool, BLOCK_COUNT, NULL));
    TEST_ASSERT_EQUAL_INT(BUFFER_INVALID_ARGS, RingBlockBuffer_Init(&rbb, storage.data, CAPACITY, NULL, BLOCK_COUNT, NULL));
    TEST_ASSERT_EQUAL_INT(BUFFER_INVALID_ARGS, RingBlockBuffer_Init(&rbb, storage.data, CAPACITY, pool, 0, NULL));
    assert_integrity(0);
}

static void test_init_rejects_zero_capacity(void)
{
    /* Proposed boundary contract: a zero-byte buffer is invalid. */
    TEST_ASSERT_EQUAL_INT(BUFFER_INVALID_ARGS, RingBlockBuffer_Init(&rbb, storage.data, 0, pool, BLOCK_COUNT, NULL));
}

static void test_reinit_resets_used_descriptors(void)
{
    allocate(8);
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_Init(&rbb, storage.data, CAPACITY, pool, BLOCK_COUNT, NULL));
    assert_integrity(0);
}

static void test_init_rejects_partial_lock_without_mutating_instance(void)
{
    RingBlockBuffer old_rbb = rbb;
    Block old_pool[BLOCK_COUNT];
    memcpy(old_pool, pool, sizeof pool);
    Lock lock = {Lock_Custom, probe_acquire, NULL, NULL};
    TEST_ASSERT_EQUAL_INT(BUFFER_INVALID_ARGS,
        RingBlockBuffer_Init(&rbb, storage.data, CAPACITY, pool, BLOCK_COUNT, &lock));
    lock.acquire = NULL;
    lock.release = probe_release;
    TEST_ASSERT_EQUAL_INT(BUFFER_INVALID_ARGS,
        RingBlockBuffer_Init(&rbb, storage.data, CAPACITY, pool, BLOCK_COUNT, &lock));
    TEST_ASSERT_EQUAL_MEMORY(&old_rbb, &rbb, sizeof rbb);
    TEST_ASSERT_EQUAL_MEMORY(old_pool, pool, sizeof pool);
    assert_integrity(0);
}

static void test_init_copies_lock_configuration(void)
{
    LockProbe probe = {0};
    Lock lock = {Lock_Custom, probe_acquire, probe_release, &probe};
    TEST_ASSERT_EQUAL_INT(BUFFER_OK,
        RingBlockBuffer_Init(&rbb, storage.data, CAPACITY, pool, BLOCK_COUNT, &lock));
    lock.acquire = NULL;
    lock.release = NULL;
    lock.context = NULL;
    Block* block = allocate(4);
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_FreeBlock(&rbb, block));
    assert_lock_probe_balanced(&probe, 2);
    TEST_ASSERT_EQUAL_INT(BUFFER_OK,
        RingBlockBuffer_Init(&rbb, storage.data, CAPACITY, pool, BLOCK_COUNT, NULL));
    TEST_ASSERT_NULL(rbb.lock.acquire);
    TEST_ASSERT_NULL(rbb.lock.release);
    TEST_ASSERT_NULL(rbb.lock.context);
    assert_integrity(0);
}

static void test_init_null_lock_overwrites_uninitialized_instance(void)
{
    RingBlockBuffer uninitialized;
    Block descriptors[1];
    uint8_t data[8];
    memset(&uninitialized, 0xa5, sizeof uninitialized);
    TEST_ASSERT_EQUAL_INT(BUFFER_OK,
        RingBlockBuffer_Init(&uninitialized, data, sizeof data, descriptors, 1, NULL));
    TEST_ASSERT_NULL(uninitialized.lock.acquire);
    TEST_ASSERT_NULL(uninitialized.lock.release);
    TEST_ASSERT_NULL(uninitialized.lock.context);
    Block* block = NULL;
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_AllocateBlock(&uninitialized, 8, &block));
    TEST_ASSERT_EQUAL_PTR(&descriptors[0], block);
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_FreeBlock(&uninitialized, block));
}

static void test_allocate_rejects_invalid_arguments(void)
{
    Block *block = &pool[0];
    TEST_ASSERT_EQUAL_INT(BUFFER_INVALID_ARGS, RingBlockBuffer_AllocateBlock(NULL, 8, &block));
    TEST_ASSERT_NULL(block);
    TEST_ASSERT_EQUAL_INT(BUFFER_INVALID_ARGS, RingBlockBuffer_AllocateBlock(&rbb, 8, NULL));
    block = &pool[0];
    TEST_ASSERT_EQUAL_INT(BUFFER_INVALID_ARGS, RingBlockBuffer_AllocateBlock(&rbb, 0, &block));
    TEST_ASSERT_NULL(block);
    assert_integrity(0);
}

static void test_allocate_first_block_starts_at_buffer(void)
{
    Block *block = allocate(1);
    TEST_ASSERT_EQUAL_PTR(storage.data, block->data);
    assert_integrity(1);
}

static void test_allocate_exact_capacity(void)
{
    Block *block = allocate(CAPACITY);
    TEST_ASSERT_EQUAL_PTR(storage.data, block->data);
    memset(block->data, 0x3c, block->size);
    assert_integrity(1);
}

static void test_allocate_larger_than_capacity(void)
{
    assert_allocate_full_preserves_state(CAPACITY + 1);
    assert_integrity(0);
}

static void test_allocate_size_max(void)
{
    assert_allocate_full_preserves_state(SIZE_MAX);
    assert_integrity(0);
}

static void test_allocate_second_block_is_contiguous(void)
{
    Block *first = allocate(8);
    memset(first->data, 0x11, first->size);
    Block *second = allocate(16);
    TEST_ASSERT_EQUAL_PTR(storage.data + 8, second->data);
    memset(second->data, 0x22, second->size);
    TEST_ASSERT_EACH_EQUAL_UINT8(0x11, first->data, first->size);
    assert_integrity(2);
}

static void test_allocate_second_block_exact_remaining_capacity(void)
{
    allocate(8);
    TEST_ASSERT_EQUAL_PTR(storage.data + 8, allocate(CAPACITY - 8)->data);
    assert_integrity(2);
}

static void test_allocate_when_payload_full(void)
{
    allocate(CAPACITY);
    assert_allocate_full_preserves_state(1);
    assert_integrity(1);
}

static void test_allocate_when_descriptor_pool_full(void)
{
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_Init(&rbb, storage.data, CAPACITY, pool, 1, NULL));
    allocate(1);
    assert_allocate_full_preserves_state(1);
    assert_integrity(1);
}

static void test_allocate_linear_prefers_tail_space(void)
{
    seed_two(8, 8, 16, 8);
    TEST_ASSERT_EQUAL_PTR(storage.data + 24, allocate(8)->data);
    assert_integrity(3);
}

static void test_allocate_linear_exact_tail_space(void)
{
    seed_two(8, 8, 16, 8);
    TEST_ASSERT_EQUAL_PTR(storage.data + 24, allocate(40)->data);
    assert_integrity(3);
}

static void test_allocate_wraps_to_buffer_start(void)
{
    seed_two(16, 16, 48, 8);
    TEST_ASSERT_EQUAL_PTR(storage.data, allocate(12)->data);
    assert_integrity(3);
}

static void test_allocate_wrap_exact_head_space(void)
{
    seed_two(16, 16, 48, 8);
    TEST_ASSERT_EQUAL_PTR(storage.data, allocate(16)->data);
    assert_integrity(3);
}

static void test_allocate_rejects_fragmented_free_space(void)
{
    seed_two(8, 8, 48, 8);
    /* Eight bytes at either end cannot hold one contiguous 12-byte block. */
    assert_allocate_full_preserves_state(12);
    assert_integrity(2);
}

static void test_allocate_wrapped_gap_appends_to_used_list(void)
{
    seed_two(32, 8, 0, 8);
    Block *block = allocate(12);
    TEST_ASSERT_EQUAL_PTR(storage.data + 8, block->data);
    TEST_ASSERT_EQUAL_PTR(&block->list_node, pool[1].list_node.next);
    TEST_ASSERT_EQUAL_PTR(&block->list_node, rbb.tail);
    assert_integrity(3);
}

static void test_allocate_wrapped_gap_exact_fit(void)
{
    seed_two(32, 8, 0, 8);
    TEST_ASSERT_EQUAL_PTR(storage.data + 8, allocate(24)->data);
    assert_integrity(3);
}

static void test_allocate_wrapped_gap_too_small(void)
{
    seed_two(32, 8, 0, 8);
    assert_allocate_full_preserves_state(25);
    assert_integrity(2);
}

static void test_write_rejects_null(void)
{
    TEST_ASSERT_EQUAL_INT(BUFFER_INVALID_ARGS, RingBlockBuffer_WriteBlock(NULL, &pool[0], 1));
    TEST_ASSERT_EQUAL_INT(BUFFER_INVALID_ARGS, RingBlockBuffer_WriteBlock(&rbb, NULL, 1));
}

static void test_write_marks_block_without_changing_payload(void)
{
    Block *block = allocate(8);
    memset(block->data, 0x3c, block->size);
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_WriteBlock(&rbb, block, block->size));
    TEST_ASSERT_EQUAL_INT(BLOCK_WRITTEN, block->status);
    TEST_ASSERT_EQUAL_PTR(storage.data, block->data);
    TEST_ASSERT_EQUAL_size_t(8, block->size);
    TEST_ASSERT_EACH_EQUAL_UINT8(0x3c, block->data, block->size);
    assert_integrity(1);
}

static void test_write_shrink_tail_reuses_unused_suffix(void)
{
    Block* first = allocate(48);
    memset(first->data, 0x3c, 20);
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_WriteBlock(&rbb, first, 20));
    TEST_ASSERT_EQUAL_size_t(20, first->size);
    Block* second = allocate(44);
    TEST_ASSERT_EQUAL_PTR(storage.data + 20, second->data);
    memset(second->data, 0x5a, second->size);
    Block* read = NULL;
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_ReadBlock(&rbb, &read));
    TEST_ASSERT_EQUAL_PTR(first, read);
    TEST_ASSERT_EQUAL_size_t(20, read->size);
    TEST_ASSERT_EACH_EQUAL_UINT8(0x3c, read->data, read->size);
    TEST_ASSERT_EACH_EQUAL_UINT8(0x5a, second->data, second->size);
    assert_integrity(2);
}

static void test_write_shrink_non_tail_waits_until_it_becomes_tail(void)
{
    Block* first = allocate(32);
    Block* second = allocate(16);
    memset(second->data, 0x5a, second->size);
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_WriteBlock(&rbb, first, 8));
    TEST_ASSERT_EQUAL_size_t(8, first->size);
    assert_allocate_full_preserves_state(17);
    Block* third = allocate(16);
    TEST_ASSERT_EQUAL_PTR(storage.data + 48, third->data);
    TEST_ASSERT_EACH_EQUAL_UINT8(0x5a, second->data, second->size);
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_FreeBlock(&rbb, third));
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_FreeBlock(&rbb, second));
    Block* reused = allocate(56);
    TEST_ASSERT_EQUAL_PTR(storage.data + 8, reused->data);
    assert_integrity(2);
}

static void test_write_shrink_wrapped_tail_reuses_gap_before_head(void)
{
    Block* first = allocate(24);
    Block* second = allocate(32);
    memset(second->data, 0x77, second->size);
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_FreeBlock(&rbb, first));
    Block* wrapped = allocate(16);
    TEST_ASSERT_EQUAL_PTR(storage.data, wrapped->data);
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_WriteBlock(&rbb, wrapped, 8));
    Block* next = allocate(16);
    TEST_ASSERT_EQUAL_PTR(storage.data + 8, next->data);
    TEST_ASSERT_EACH_EQUAL_UINT8(0x77, second->data, second->size);
    assert_integrity(3);
}

static void test_write_invalid_lengths_preserve_reservation(void)
{
    Block* block = allocate(8);
    memset(block->data, 0x39, block->size);
    TEST_ASSERT_EQUAL_INT(BUFFER_INVALID_ARGS, RingBlockBuffer_WriteBlock(&rbb, block, 0));
    TEST_ASSERT_EQUAL_INT(BUFFER_INVALID_ARGS, RingBlockBuffer_WriteBlock(&rbb, block, 9));
    TEST_ASSERT_EQUAL_INT(BUFFER_INVALID_ARGS, RingBlockBuffer_WriteBlock(&rbb, block, SIZE_MAX));
    TEST_ASSERT_EQUAL_INT(BLOCK_ALLOCATED, block->status);
    TEST_ASSERT_EQUAL_size_t(8, block->size);
    TEST_ASSERT_EACH_EQUAL_UINT8(0x39, block->data, block->size);
    assert_integrity(1);
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_WriteBlock(&rbb, block, 8));
    TEST_ASSERT_EQUAL_INT(BUFFER_ERROR, RingBlockBuffer_WriteBlock(&rbb, block, 0));
    assert_integrity(1);
}

static void test_write_rejects_non_allocated_states(void)
{
    TEST_ASSERT_EQUAL_INT(BUFFER_ERROR, RingBlockBuffer_WriteBlock(&rbb, &pool[0], 1));
    Block* block = allocate(8);
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_WriteBlock(&rbb, block, block->size));
    TEST_ASSERT_EQUAL_INT(BUFFER_ERROR, RingBlockBuffer_WriteBlock(&rbb, block, block->size));
    Block* read = NULL;
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_ReadBlock(&rbb, &read));
    TEST_ASSERT_EQUAL_INT(BUFFER_ERROR, RingBlockBuffer_WriteBlock(&rbb, block, block->size));
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_FreeBlock(&rbb, block));
    TEST_ASSERT_EQUAL_INT(BUFFER_ERROR, RingBlockBuffer_WriteBlock(&rbb, block, block->size));
    assert_integrity(0);
}

static void test_foreign_block_cannot_be_written_or_freed(void)
{
    uint8_t other_data[CAPACITY];
    Block other_pool[2];
    RingBlockBuffer other;
    TEST_ASSERT_EQUAL_INT(BUFFER_OK,
        RingBlockBuffer_Init(&other, other_data, sizeof other_data, other_pool, 2, NULL));
    Block* block = allocate(8);
    TEST_ASSERT_EQUAL_INT(BUFFER_ERROR, RingBlockBuffer_WriteBlock(&other, block, block->size));
    TEST_ASSERT_EQUAL_INT(BUFFER_ERROR, RingBlockBuffer_FreeBlock(&other, block));
    TEST_ASSERT_EQUAL_INT(BLOCK_ALLOCATED, block->status);
    TEST_ASSERT_NULL(other.used_list.next);
    TEST_ASSERT_EQUAL_PTR(&other.used_list, other.tail);
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_FreeBlock(&rbb, block));
    assert_integrity(0);
}

static void test_read_rejects_invalid_arguments(void)
{
    Block *block = &pool[0];
    TEST_ASSERT_EQUAL_INT(BUFFER_INVALID_ARGS, RingBlockBuffer_ReadBlock(NULL, &block));
    TEST_ASSERT_NULL(block);
    TEST_ASSERT_EQUAL_INT(BUFFER_INVALID_ARGS, RingBlockBuffer_ReadBlock(&rbb, NULL));
    Block* written = allocate(4);
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_WriteBlock(&rbb, written, written->size));
    TEST_ASSERT_EQUAL_INT(BUFFER_INVALID_ARGS, RingBlockBuffer_ReadBlock(&rbb, NULL));
    TEST_ASSERT_EQUAL_INT(BLOCK_WRITTEN, written->status);
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_FreeBlock(&rbb, written));
    assert_integrity(0);
}

static void test_read_empty_buffer(void)
{
    Block *block = &pool[0];
    TEST_ASSERT_EQUAL_INT(BUFFER_EMPTY, RingBlockBuffer_ReadBlock(&rbb, &block));
    TEST_ASSERT_NULL(block);
    assert_integrity(0);
}

static void test_read_returns_written_payload(void)
{
    const uint8_t payload[] = {0, 1, 0x7f, 0x80, 0xff};
    Block *written = allocate(sizeof payload);
    memcpy(written->data, payload, sizeof payload);
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_WriteBlock(&rbb, written, written->size));
    Block *read = NULL;
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_ReadBlock(&rbb, &read));
    TEST_ASSERT_EQUAL_PTR(written, read);
    TEST_ASSERT_EQUAL_INT(BLOCK_READ, read->status);
    TEST_ASSERT_EQUAL_MEMORY(payload, read->data, sizeof payload);
    assert_integrity(1);
}

static void test_read_skips_allocated_and_already_read_blocks(void)
{
    const size_t offsets[] = {0, 8, 16};
    const size_t sizes[] = {8, 8, 8};
    seed_layout(offsets, sizes, 3);
    pool[1].status = BLOCK_READ;
    pool[2].status = BLOCK_WRITTEN;
    Block *read = NULL;
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_ReadBlock(&rbb, &read));
    TEST_ASSERT_EQUAL_PTR(&pool[2], read);
    TEST_ASSERT_EQUAL_INT(BLOCK_ALLOCATED, pool[0].status);
    TEST_ASSERT_EQUAL_INT(BLOCK_READ, pool[1].status);
    TEST_ASSERT_EQUAL_INT(BLOCK_READ, pool[2].status);
    assert_integrity(3);
}

static void test_read_written_blocks_in_list_order(void)
{
    seed_two(32, 8, 0, 8);
    pool[0].status = pool[1].status = BLOCK_WRITTEN;
    Block *read = NULL;
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_ReadBlock(&rbb, &read));
    TEST_ASSERT_EQUAL_PTR(&pool[0], read);
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_ReadBlock(&rbb, &read));
    TEST_ASSERT_EQUAL_PTR(&pool[1], read);
    assert_integrity(2);
}

static void test_read_with_only_unwritten_block_returns_empty(void)
{
    allocate(8);
    Block *read = NULL;
    TEST_ASSERT_EQUAL_INT(BUFFER_EMPTY, RingBlockBuffer_ReadBlock(&rbb, &read));
    assert_integrity(1);
}

static void test_read_does_not_return_same_block_twice(void)
{
    Block *block = allocate(8);
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_WriteBlock(&rbb, block, block->size));
    Block *read = NULL;
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_ReadBlock(&rbb, &read));
    TEST_ASSERT_EQUAL_INT(BUFFER_EMPTY, RingBlockBuffer_ReadBlock(&rbb, &read));
    assert_integrity(1);
}

static void test_free_rejects_invalid_arguments(void)
{
    TEST_ASSERT_EQUAL_INT(BUFFER_INVALID_ARGS, RingBlockBuffer_FreeBlock(NULL, &pool[0]));
    TEST_ASSERT_EQUAL_INT(BUFFER_INVALID_ARGS, RingBlockBuffer_FreeBlock(&rbb, NULL));
    assert_integrity(0);
}

static void test_free_rejects_already_free_descriptor(void)
{
    TEST_ASSERT_EQUAL_INT(BUFFER_ERROR, RingBlockBuffer_FreeBlock(&rbb, &pool[0]));
    assert_integrity(0);
}

static void test_free_only_block_restores_empty_buffer(void)
{
    Block *block = allocate(8);
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_FreeBlock(&rbb, block));
    TEST_ASSERT_NULL(rbb.used_list.next);
    TEST_ASSERT_EQUAL_PTR(&rbb.used_list, rbb.tail);
    assert_integrity(0);
}

static void test_free_head_preserves_remaining_blocks(void)
{
    seed_two(0, 8, 8, 8);
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_FreeBlock(&rbb, &pool[0]));
    TEST_ASSERT_EQUAL_PTR(&pool[1].list_node, rbb.used_list.next);
    assert_integrity(1);
}

static void test_free_tail_updates_tail_pointer(void)
{
    seed_two(0, 8, 8, 8);
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_FreeBlock(&rbb, &pool[1]));
    TEST_ASSERT_EQUAL_PTR(&pool[0].list_node, rbb.tail);
    assert_integrity(1);
}

static void test_free_middle_preserves_neighbors(void)
{
    const size_t offsets[] = {0, 8, 16};
    const size_t sizes[] = {8, 8, 8};
    seed_layout(offsets, sizes, 3);
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_FreeBlock(&rbb, &pool[1]));
    TEST_ASSERT_EQUAL_PTR(&pool[2].list_node, pool[0].list_node.next);
    assert_integrity(2);
}

static void test_double_free_preserves_lists(void)
{
    const size_t offsets[] = {0, 8, 16};
    const size_t sizes[] = {8, 8, 8};
    seed_layout(offsets, sizes, 3);
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_FreeBlock(&rbb, &pool[1]));
    TEST_ASSERT_EQUAL_INT(BUFFER_ERROR, RingBlockBuffer_FreeBlock(&rbb, &pool[1]));
    assert_integrity(2);
}

static void test_write_and_free_reject_pool_boundary_and_unlisted_blocks(void)
{
    Block fake = {0};
    fake.status = BLOCK_ALLOCATED;
    fake.data = storage.data;
    fake.size = 1;
    Block* before_pool = (Block*)((uintptr_t)pool - 1u);
    Block* interior = (Block*)((uintptr_t)pool + 1u);
    Block* one_past = &pool[BLOCK_COUNT];
    RingBlockBuffer previous = rbb;
    Block previous_pool[BLOCK_COUNT];
    memcpy(previous_pool, pool, sizeof pool);
    TEST_ASSERT_EQUAL_INT(BUFFER_ERROR, RingBlockBuffer_WriteBlock(&rbb, &fake, 1));
    TEST_ASSERT_EQUAL_INT(BUFFER_ERROR, RingBlockBuffer_FreeBlock(&rbb, &fake));
    TEST_ASSERT_EQUAL_INT(BUFFER_ERROR, RingBlockBuffer_WriteBlock(&rbb, before_pool, 1));
    TEST_ASSERT_EQUAL_INT(BUFFER_ERROR, RingBlockBuffer_FreeBlock(&rbb, before_pool));
    TEST_ASSERT_EQUAL_INT(BUFFER_ERROR, RingBlockBuffer_WriteBlock(&rbb, interior, 1));
    TEST_ASSERT_EQUAL_INT(BUFFER_ERROR, RingBlockBuffer_FreeBlock(&rbb, interior));
    TEST_ASSERT_EQUAL_INT(BUFFER_ERROR, RingBlockBuffer_WriteBlock(&rbb, one_past, 1));
    TEST_ASSERT_EQUAL_INT(BUFFER_ERROR, RingBlockBuffer_FreeBlock(&rbb, one_past));
    TEST_ASSERT_EQUAL_MEMORY(&previous, &rbb, sizeof rbb);
    TEST_ASSERT_EQUAL_MEMORY(previous_pool, pool, sizeof pool);
    TEST_ASSERT_EQUAL_INT(BLOCK_ALLOCATED, fake.status);
    assert_integrity(0);
}

static void test_free_rejects_unlisted_block_inside_pool(void)
{
    pool[0].status = BLOCK_ALLOCATED;
    RingBlockBuffer previous = rbb;
    Block previous_pool[BLOCK_COUNT];
    memcpy(previous_pool, pool, sizeof pool);
    TEST_ASSERT_EQUAL_INT(BUFFER_ERROR, RingBlockBuffer_FreeBlock(&rbb, &pool[0]));
    TEST_ASSERT_EQUAL_MEMORY(&previous, &rbb, sizeof rbb);
    TEST_ASSERT_EQUAL_MEMORY(previous_pool, pool, sizeof pool);
    pool[0].status = BLOCK_FREE;
    assert_integrity(0);
}

static void test_write_and_free_reject_block_beyond_configured_pool_count(void)
{
    TEST_ASSERT_EQUAL_INT(BUFFER_OK,
        RingBlockBuffer_Init(&rbb, storage.data, CAPACITY, pool, 1, NULL));
    pool[1].status = BLOCK_ALLOCATED;
    TEST_ASSERT_EQUAL_INT(BUFFER_ERROR, RingBlockBuffer_WriteBlock(&rbb, &pool[1], 1));
    TEST_ASSERT_EQUAL_INT(BUFFER_ERROR, RingBlockBuffer_FreeBlock(&rbb, &pool[1]));
    TEST_ASSERT_EQUAL_INT(BLOCK_ALLOCATED, pool[1].status);
    Block* block = allocate(8);
    TEST_ASSERT_EQUAL_PTR(&pool[0], block);
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_WriteBlock(&rbb, block, block->size));
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_FreeBlock(&rbb, block));
    assert_integrity(0);
}

static void test_free_accepts_all_active_states(void)
{
    Block* allocated = allocate(8);
    Block* written = allocate(8);
    Block* read = allocate(8);
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_WriteBlock(&rbb, read, read->size));
    Block* got = NULL;
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_ReadBlock(&rbb, &got));
    TEST_ASSERT_EQUAL_PTR(read, got);
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_WriteBlock(&rbb, written, written->size));
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_FreeBlock(&rbb, allocated));
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_FreeBlock(&rbb, written));
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_FreeBlock(&rbb, read));
    assert_integrity(0);
}

static void test_lock_callbacks_are_balanced_on_success_and_failure(void)
{
    LockProbe first = {0};
    LockProbe second = {0};
    Lock first_lock = {Lock_Custom, probe_acquire, probe_release, &first};
    Lock second_lock = {Lock_Custom, probe_acquire, probe_release, &second};
    uint8_t other_data[8];
    Block other_pool[1];
    RingBlockBuffer other;
    TEST_ASSERT_EQUAL_INT(BUFFER_OK,
        RingBlockBuffer_Init(&rbb, storage.data, CAPACITY, pool, BLOCK_COUNT, &first_lock));
    TEST_ASSERT_EQUAL_INT(BUFFER_OK,
        RingBlockBuffer_Init(&other, other_data, sizeof other_data, other_pool, 1, &second_lock));
    Block* block = NULL;
    TEST_ASSERT_EQUAL_INT(BUFFER_INVALID_ARGS, RingBlockBuffer_AllocateBlock(&rbb, 0, &block));
    TEST_ASSERT_EQUAL_INT(BUFFER_INVALID_ARGS, RingBlockBuffer_WriteBlock(&rbb, NULL, 1));
    TEST_ASSERT_EQUAL_INT(BUFFER_INVALID_ARGS, RingBlockBuffer_ReadBlock(&rbb, NULL));
    TEST_ASSERT_EQUAL_INT(BUFFER_INVALID_ARGS, RingBlockBuffer_FreeBlock(&rbb, NULL));
    assert_lock_probe_balanced(&first, 0);
    TEST_ASSERT_EQUAL_INT(BUFFER_EMPTY, RingBlockBuffer_ReadBlock(&rbb, &block));
    TEST_ASSERT_EQUAL_INT(BUFFER_FULL, RingBlockBuffer_AllocateBlock(&rbb, CAPACITY + 1, &block));
    block = allocate(4);
    Block* allocated = block;
    TEST_ASSERT_EQUAL_INT(BUFFER_ERROR, RingBlockBuffer_WriteBlock(&other, block, block->size));
    TEST_ASSERT_EQUAL_INT(BUFFER_ERROR, RingBlockBuffer_FreeBlock(&other, block));
    TEST_ASSERT_EQUAL_INT(BUFFER_EMPTY, RingBlockBuffer_ReadBlock(&rbb, &block));
    TEST_ASSERT_NULL(block);
    block = allocated;
    TEST_ASSERT_EQUAL_INT(BUFFER_INVALID_ARGS, RingBlockBuffer_WriteBlock(&rbb, block, 0));
    TEST_ASSERT_EQUAL_INT(BLOCK_ALLOCATED, block->status);
    TEST_ASSERT_EQUAL_size_t(4, block->size);
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_WriteBlock(&rbb, block, block->size));
    TEST_ASSERT_EQUAL_INT(BUFFER_ERROR, RingBlockBuffer_WriteBlock(&rbb, block, block->size));
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_ReadBlock(&rbb, &block));
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_FreeBlock(&rbb, block));
    TEST_ASSERT_EQUAL_INT(BUFFER_ERROR, RingBlockBuffer_FreeBlock(&rbb, block));
    assert_lock_probe_balanced(&first, 10);
    assert_lock_probe_balanced(&second, 2);
    assert_integrity(0);
}

static void test_full_lifecycle_reuses_single_descriptor(void)
{
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_Init(&rbb, storage.data, CAPACITY, pool, 1, NULL));
    for (unsigned i = 0; i < 16; ++i)
    {
        Block *block = allocate(CAPACITY);
        TEST_ASSERT_EQUAL_PTR(storage.data, block->data);
        memset(block->data, (int)i, block->size);
        TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_WriteBlock(&rbb, block, block->size));
        Block *read = NULL;
        TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_ReadBlock(&rbb, &read));
        TEST_ASSERT_EQUAL_PTR(block, read);
        TEST_ASSERT_EACH_EQUAL_UINT8(i, read->data, read->size);
        TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_FreeBlock(&rbb, read));
        assert_integrity(0);
    }
}

static void test_public_api_wraparound_preserves_live_payloads(void)
{
    Block *first = allocate(16);
    Block *second = allocate(32);
    Block *third = allocate(8);
    memset(second->data, 0x22, second->size);
    memset(third->data, 0x33, third->size);
    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_FreeBlock(&rbb, first));
    assert_integrity(2);
    TEST_ASSERT_EQUAL_PTR(storage.data, allocate(12)->data);
    assert_integrity(3);
    TEST_ASSERT_EQUAL_PTR(storage.data + 12, allocate(4)->data);
    assert_integrity(4);
    assert_allocate_full_preserves_state(1);
    TEST_ASSERT_EACH_EQUAL_UINT8(0x22, second->data, second->size);
    TEST_ASSERT_EACH_EQUAL_UINT8(0x33, third->data, third->size);
}

static void test_public_api_repeated_mixed_size_fifo_cycles(void)
{
    struct Pending
    {
        Block *block;
        size_t size;
        uint8_t pattern;
    } pending[BLOCK_COUNT];
    size_t pending_count = 0;
    size_t successful_allocations = 0;
    size_t wrap_count = 0;
    uint8_t *previous_address = NULL;

    for (size_t step = 0; step < 512; ++step)
    {
        bool consume = pending_count > 0 &&
                       (pending_count == BLOCK_COUNT || step % 3 == 0);

        if (!consume)
        {
            const size_t requested_size = step * 11 % 23 + 1;
            Block *block = NULL;
            BufferStatus status = RingBlockBuffer_AllocateBlock(&rbb, requested_size, &block);
            if (status == BUFFER_OK)
            {
                const uint8_t pattern = (uint8_t)(successful_allocations + 1);
                if (previous_address != NULL && block->data < previous_address)
                    ++wrap_count;
                previous_address = block->data;
                size_t actual_size = 1 + (requested_size - 1) / 2;
                memset(block->data, pattern, actual_size);
                TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_WriteBlock(&rbb, block, actual_size));
                pending[pending_count++] = (struct Pending){block, actual_size, pattern};
                ++successful_allocations;
            }
            else
            {
                TEST_ASSERT_EQUAL_INT(BUFFER_FULL, status);
                consume = true;
            }
        }

        if (consume)
        {
            Block *read = NULL;
            TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_ReadBlock(&rbb, &read));
            TEST_ASSERT_EQUAL_PTR(pending[0].block, read);
            TEST_ASSERT_EQUAL_size_t(pending[0].size, read->size);
            TEST_ASSERT_EACH_EQUAL_UINT8(pending[0].pattern, read->data, read->size);
            TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_FreeBlock(&rbb, read));
            memmove(pending, pending + 1, (pending_count - 1) * sizeof pending[0]);
            --pending_count;
        }

        for (size_t i = 0; i < pending_count; ++i)
            TEST_ASSERT_EACH_EQUAL_UINT8(pending[i].pattern,
                                         pending[i].block->data,
                                         pending[i].size);
        assert_integrity(pending_count);
    }

    while (pending_count > 0)
    {
        Block *read = NULL;
        TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_ReadBlock(&rbb, &read));
        TEST_ASSERT_EQUAL_PTR(pending[0].block, read);
        TEST_ASSERT_EQUAL_size_t(pending[0].size, read->size);
        TEST_ASSERT_EACH_EQUAL_UINT8(pending[0].pattern, read->data, read->size);
        TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_FreeBlock(&rbb, read));
        memmove(pending, pending + 1, (pending_count - 1) * sizeof pending[0]);
        --pending_count;
        assert_integrity(pending_count);
    }

    TEST_ASSERT_GREATER_THAN_size_t(100, successful_allocations);
    TEST_ASSERT_GREATER_THAN_size_t(10, wrap_count);
    assert_integrity(0);
}

static uint32_t next_random(uint32_t* state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static void test_randomized_out_of_order_lifecycle(void)
{
    BlockStatus expected[BLOCK_COUNT];
    size_t expected_sizes[BLOCK_COUNT] = {0};
    uint8_t patterns[BLOCK_COUNT] = {0};
    uint32_t random = UINT32_C(0x71a5c39d);
    unsigned allocations = 0;
    unsigned writes = 0;
    unsigned reads = 0;
    unsigned cancellations = 0;
    for (size_t i = 0; i < BLOCK_COUNT; ++i)
        expected[i] = BLOCK_FREE;

    for (unsigned step = 0; step < 4096; ++step)
    {
        uint32_t draw = next_random(&random);
        unsigned action = (draw >> 16) % 4u;
        if (action == 0)
        {
            Block* block = NULL;
            size_t size = draw % 23u + 1u;
            BufferStatus status = RingBlockBuffer_AllocateBlock(&rbb, size, &block);
            if (status == BUFFER_OK)
            {
                size_t index = (size_t)(block - pool);
                TEST_ASSERT_TRUE(index < BLOCK_COUNT);
                TEST_ASSERT_EQUAL_INT(BLOCK_FREE, expected[index]);
                expected[index] = BLOCK_ALLOCATED;
                expected_sizes[index] = size;
                patterns[index] = (uint8_t)(step + 1u);
                memset(block->data, patterns[index], block->size);
                ++allocations;
            }
            else
            {
                TEST_ASSERT_EQUAL_INT(BUFFER_FULL, status);
                TEST_ASSERT_NULL(block);
            }
        }
        else if (action == 1)
        {
            for (size_t offset = 0; offset < BLOCK_COUNT; ++offset)
            {
                size_t index = (offset + draw / 4u) % BLOCK_COUNT;
                if (expected[index] == BLOCK_ALLOCATED)
                {
                    size_t actual_size = 1 + (draw % expected_sizes[index]);
                    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_WriteBlock(&rbb, &pool[index], actual_size));
                    expected_sizes[index] = actual_size;
                    expected[index] = BLOCK_WRITTEN;
                    ++writes;
                    break;
                }
            }
        }
        else if (action == 2)
        {
            Block* block = NULL;
            BufferStatus status = RingBlockBuffer_ReadBlock(&rbb, &block);
            if (status == BUFFER_OK)
            {
                size_t index = (size_t)(block - pool);
                TEST_ASSERT_TRUE(index < BLOCK_COUNT);
                TEST_ASSERT_EQUAL_INT(BLOCK_WRITTEN, expected[index]);
                TEST_ASSERT_EACH_EQUAL_UINT8(patterns[index], block->data, block->size);
                expected[index] = BLOCK_READ;
                ++reads;
            }
            else
            {
                TEST_ASSERT_EQUAL_INT(BUFFER_EMPTY, status);
                TEST_ASSERT_NULL(block);
                for (size_t i = 0; i < BLOCK_COUNT; ++i)
                    TEST_ASSERT_NOT_EQUAL(BLOCK_WRITTEN, expected[i]);
            }
        }
        else
        {
            for (size_t offset = 0; offset < BLOCK_COUNT; ++offset)
            {
                size_t index = (offset + draw / 4u) % BLOCK_COUNT;
                if (expected[index] != BLOCK_FREE)
                {
                    if (expected[index] != BLOCK_READ)
                        ++cancellations;
                    TEST_ASSERT_EQUAL_INT(BUFFER_OK, RingBlockBuffer_FreeBlock(&rbb, &pool[index]));
                    expected[index] = BLOCK_FREE;
                    expected_sizes[index] = 0;
                    break;
                }
            }
        }

        size_t live = 0;
        for (size_t i = 0; i < BLOCK_COUNT; ++i)
        {
            TEST_ASSERT_EQUAL_INT(expected[i], pool[i].status);
            TEST_ASSERT_EQUAL_size_t(expected_sizes[i], pool[i].size);
            if (expected[i] != BLOCK_FREE)
            {
                ++live;
                TEST_ASSERT_EACH_EQUAL_UINT8(patterns[i], pool[i].data, pool[i].size);
            }
        }
        assert_integrity(live);
    }

    TEST_ASSERT_GREATER_THAN_UINT(100, allocations);
    TEST_ASSERT_GREATER_THAN_UINT(50, writes);
    TEST_ASSERT_GREATER_THAN_UINT(20, reads);
    TEST_ASSERT_GREATER_THAN_UINT(20, cancellations);
}

int main(int argc, char **argv)
{
    const TestCase cases[] = {
        TEST_CASE(test_init_sets_fields_and_all_descriptors_free),
        TEST_CASE(test_init_rejects_null_arguments_and_zero_pool),
        TEST_CASE(test_init_rejects_zero_capacity),
        TEST_CASE(test_reinit_resets_used_descriptors),
        TEST_CASE(test_init_rejects_partial_lock_without_mutating_instance),
        TEST_CASE(test_init_copies_lock_configuration),
        TEST_CASE(test_init_null_lock_overwrites_uninitialized_instance),
        TEST_CASE(test_allocate_rejects_invalid_arguments),
        TEST_CASE(test_allocate_first_block_starts_at_buffer),
        TEST_CASE(test_allocate_exact_capacity),
        TEST_CASE(test_allocate_larger_than_capacity),
        TEST_CASE(test_allocate_size_max),
        TEST_CASE(test_allocate_second_block_is_contiguous),
        TEST_CASE(test_allocate_second_block_exact_remaining_capacity),
        TEST_CASE(test_allocate_when_payload_full),
        TEST_CASE(test_allocate_when_descriptor_pool_full),
        TEST_CASE(test_allocate_linear_prefers_tail_space),
        TEST_CASE(test_allocate_linear_exact_tail_space),
        TEST_CASE(test_allocate_wraps_to_buffer_start),
        TEST_CASE(test_allocate_wrap_exact_head_space),
        TEST_CASE(test_allocate_rejects_fragmented_free_space),
        TEST_CASE(test_allocate_wrapped_gap_appends_to_used_list),
        TEST_CASE(test_allocate_wrapped_gap_exact_fit),
        TEST_CASE(test_allocate_wrapped_gap_too_small),
        TEST_CASE(test_write_rejects_null),
        TEST_CASE(test_write_marks_block_without_changing_payload),
        TEST_CASE(test_write_shrink_tail_reuses_unused_suffix),
        TEST_CASE(test_write_shrink_non_tail_waits_until_it_becomes_tail),
        TEST_CASE(test_write_shrink_wrapped_tail_reuses_gap_before_head),
        TEST_CASE(test_write_invalid_lengths_preserve_reservation),
        TEST_CASE(test_write_rejects_non_allocated_states),
        TEST_CASE(test_foreign_block_cannot_be_written_or_freed),
        TEST_CASE(test_read_rejects_invalid_arguments),
        TEST_CASE(test_read_empty_buffer),
        TEST_CASE(test_read_returns_written_payload),
        TEST_CASE(test_read_skips_allocated_and_already_read_blocks),
        TEST_CASE(test_read_written_blocks_in_list_order),
        TEST_CASE(test_read_with_only_unwritten_block_returns_empty),
        TEST_CASE(test_read_does_not_return_same_block_twice),
        TEST_CASE(test_free_rejects_invalid_arguments),
        TEST_CASE(test_free_rejects_already_free_descriptor),
        TEST_CASE(test_free_only_block_restores_empty_buffer),
        TEST_CASE(test_free_head_preserves_remaining_blocks),
        TEST_CASE(test_free_tail_updates_tail_pointer),
        TEST_CASE(test_free_middle_preserves_neighbors),
        TEST_CASE(test_double_free_preserves_lists),
        TEST_CASE(test_write_and_free_reject_pool_boundary_and_unlisted_blocks),
        TEST_CASE(test_free_rejects_unlisted_block_inside_pool),
        TEST_CASE(test_write_and_free_reject_block_beyond_configured_pool_count),
        TEST_CASE(test_free_accepts_all_active_states),
        TEST_CASE(test_lock_callbacks_are_balanced_on_success_and_failure),
        TEST_CASE(test_full_lifecycle_reuses_single_descriptor),
        TEST_CASE(test_public_api_wraparound_preserves_live_payloads),
        TEST_CASE(test_public_api_repeated_mixed_size_fifo_cycles),
        TEST_CASE(test_randomized_out_of_order_lifecycle),
    };
    return run_test_cases(argc, argv, __FILE__, cases, sizeof cases / sizeof cases[0]);
}
