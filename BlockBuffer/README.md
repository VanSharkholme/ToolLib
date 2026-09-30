# RingBlockBuffer

`RingBlockBuffer` uses caller-owned payload storage and an array of `Block` descriptors. It does not allocate memory. `Init` accepts an optional, per-instance `Lock`:

```c
Lock lock = {Lock_Custom, acquire_lock, release_lock, lock_context};
RingBlockBuffer_Init(&rbb, storage, sizeof storage, blocks, block_count, &lock);
```

Pass `NULL` instead of `&lock` only when one execution context uses the instance or the caller serializes **all** RBB calls externally. RBB copies the `Lock` into the instance, so the original `Lock` object need not outlive `Init`, but its `context` and the payload and descriptor storage must remain valid for the instance's lifetime. Do not change the instance's lock or reuse its storage while operations are in progress. Reinitialization discards all reservations; first ensure no operation is in progress and no caller will use an old block pointer.

## Ownership and ordering

1. `AllocateBlock(rbb, capacity, &block)` returns an exclusive reservation. Before publication, `block->size` is that reserved capacity; its caller writes up to that many bytes to `block->data` outside the critical section.
2. `WriteBlock(rbb, block, actual_size)` publishes `actual_size` bytes. The length must be between 1 and the reserved capacity, inclusive. On success, `block->size` becomes the readable length. After success, the producer must stop accessing the block and its payload.
3. `ReadBlock` returns one published block to one consumer. The consumer reads `block->size` bytes outside the critical section and then calls `FreeBlock`.

`WriteBlock` rejects a zero or over-capacity length with `BUFFER_INVALID_ARGS`; a foreign block or a block outside `BLOCK_ALLOCATED` returns `BUFFER_ERROR`. Failure leaves the reservation unchanged, so the producer can retry or cancel it with `FreeBlock`. The size and status change together under the configured critical section. Shrinking the current tail reservation makes its unused suffix available to the next allocation immediately. Shrinking an earlier reservation does not make an internal gap immediately reusable; that suffix can become available once later reservations are freed and the shrunk block becomes the tail.

The caller must treat `Block` metadata (`data`, `size`, `status`, and `list_node`) as library-owned. Only write payload bytes while holding the reservation. Because `Block` is public and the original capacity is not stored separately, the library cannot detect a caller that changes `block->size` directly before publication.

`ReadBlock` scans allocation order and skips blocks that are still being filled or were already read. A later published block can therefore be read before an earlier reservation. `FreeBlock` also accepts an allocated or written block as a cancellation, but the caller must have exclusive ownership and ensure no producer or consumer is using it. A successful free invalidates every old pointer to that reservation, even if the descriptor address is later reused. The API cannot detect a stale pointer after descriptor reuse.

On failure, `AllocateBlock` and `ReadBlock` set a valid output pointer to `NULL`. `BUFFER_FULL` includes insufficient contiguous payload space and an exhausted descriptor pool. `BUFFER_EMPTY` includes a used list with no published, unread block.

## Synchronization contract

Both callbacks must be supplied, or neither. `acquire(Lock *lock)` must acquire exclusive access and return a `uintptr_t` token that `release(Lock *lock, uintptr_t token)` uses to restore the previous state. The `Lock *` passed to each callback points to RBB's copy; callbacks can access the application state through `lock->context`. A `NULL` lock or a lock with both callbacks unset provides no internal synchronization. The pair must also provide acquire/release memory ordering for the metadata and payload publication. If a platform's lock primitives do not supply the needed compiler and hardware barriers, the adapter must add them. The callbacks cannot fail, must not call RBB recursively, and must be usable from every calling context chosen by the application. `Init` itself is not synchronized.

The library calls the pair around `AllocateBlock`, `WriteBlock`, `ReadBlock`, and `FreeBlock`, including unsuccessful calls that reach shared state. It does not keep a lock while the caller fills or reads payload. A mutex adapter is suitable for task or thread calls, but must not be used from an ISR. The application must also prevent a producer and consumer from concurrently using the same reservation outside these calls.

For a single-core Cortex-M system, `Lock_IRQ` calls the platform hooks `Lock_IRQ_Disable` and `Lock_IRQ_Enable`. Their current weak defaults do nothing, so the application must provide working replacements before using this lock for concurrent access. An implementation that saves PRIMASK, disables interrupts, and restores exactly the saved value can look like this with CMSIS names:

```c
uint32_t Lock_IRQ_Disable(void)
{
    uint32_t previous = __get_PRIMASK();
    __disable_irq();
    __DMB();
    return previous;
}

void Lock_IRQ_Enable(uint32_t previous)
{
    __DMB();
    __set_PRIMASK(previous);
}
```

Initialize a `Lock` with `Lock_Init(&lock, Lock_IRQ, NULL)` before passing it to RBB. Verify that the chosen CMSIS intrinsics also act as compiler barriers in the actual toolchain. Disabling local interrupts does not protect against another core or DMA. An ISR must never spin waiting for a lock held by code it interrupted.

For RTOS task-only use, a custom `Lock` can wrap the RTOS mutex: `acquire` locks the mutex and returns zero; `release` unlocks it and ignores the token. Its `context` points to the mutex. The mutex must supply acquire/release ordering, and callers must not use the adapter in an ISR.

For SMP use that includes ISR calls, a custom adapter must first save and disable **local** interrupts, then acquire an SMP-safe spinlock or hardware lock with acquire ordering. On exit it releases that lock with release ordering and restores the saved interrupt state. The lock must work on the actual memory topology and atomic instruction set. A task-only SMP application can instead use an SMP-safe mutex. The current `Lock_Spinlock_IRQ` branch is a placeholder and does not initialize a usable lock.

`WriteBlock` checks descriptor-pool membership in O(1) without storing a pointer in every block. It compares integer representations of addresses, checks alignment to `sizeof(Block)` and the configured element count, then verifies equality with the selected pool element. This assumes the platform represents the array as a monotonically addressed, contiguous region with the normal struct stride. Do not replace this with relational comparisons between unrelated C pointers, which have undefined behavior. Each active RBB must have its own nonoverlapping descriptor pool. `FreeBlock` also verifies membership in the used list; `ReadBlock` and arbitrary `FreeBlock` scan that list and are O(max_block_num), including while interrupts are masked in the single-core adapter. Avoid large descriptor pools in latency-sensitive ISR paths until the worst-case time has been measured.

## Performance check on the target

Build the production configuration, then measure `WriteBlock`, empty `ReadBlock`, successful `AllocateBlock`, and worst-case `ReadBlock`/`FreeBlock` at representative descriptor counts. Compare the same workload with no lock, a `Lock` using interrupt masking, and a local static-inline interrupt adapter. Record total operation time and the longest interval with interrupts disabled. Use a hardware cycle counter when the core has one; otherwise use a timer or GPIO trace. Run from the intended memory region because flash wait states affect call costs. Add a build-wide static lock backend only if the measured callback overhead is material for the application.
