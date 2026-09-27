# RingBlockBuffer

`RingBlockBuffer` uses caller-owned payload storage and an array of `Block` descriptors. It does not allocate memory. `Init` accepts an optional, per-instance synchronization configuration:

```c
RingBlockBufferSync sync = {enter_critical, exit_critical, lock_context};
RingBlockBuffer_Init(&rbb, storage, sizeof storage, blocks, block_count, &sync);
```

Pass `NULL` instead of `&sync` only when one execution context uses the instance or the caller serializes **all** RBB calls externally. The configuration is copied into the instance; the context and the storage must remain valid for its lifetime. Do not change the configuration or reuse the storage while operations are in progress. Reinitialization discards all reservations; first ensure no operation is in progress and no caller will use an old block pointer.

## Ownership and ordering

1. `AllocateBlock` returns an exclusive reservation. Its caller writes `block->data` outside the critical section.
2. `WriteBlock(rbb, block)` publishes that payload. After it succeeds, the producer must stop accessing the block and its payload.
3. `ReadBlock` returns one published block to one consumer. The consumer reads it outside the critical section and then calls `FreeBlock`.

`ReadBlock` scans allocation order and skips blocks that are still being filled or were already read. A later published block can therefore be read before an earlier reservation. `FreeBlock` also accepts an allocated or written block as a cancellation, but the caller must have exclusive ownership and ensure no producer or consumer is using it. A successful free invalidates every old pointer to that reservation, even if the descriptor address is later reused. The API cannot detect a stale pointer after descriptor reuse.

On failure, `AllocateBlock` and `ReadBlock` set a valid output pointer to `NULL`. `BUFFER_FULL` includes insufficient contiguous payload space and an exhausted descriptor pool. `BUFFER_EMPTY` includes a used list with no published, unread block.

## Synchronization contract

Both callbacks must be supplied, or neither. `enter(context)` must acquire exclusive access and return a `uintptr_t` token that `exit(context, token)` uses to restore the previous state. The pair must also provide acquire/release memory ordering for the metadata and payload publication. If a platform's lock primitives do not supply the needed compiler and hardware barriers, the adapter must add them. The callbacks cannot fail, must not call RBB recursively, and must be usable from every calling context chosen by the application. `Init` itself is not synchronized.

The library calls the pair around `AllocateBlock`, `WriteBlock`, `ReadBlock`, and `FreeBlock`, including unsuccessful calls that reach shared state. It does not keep a lock while the caller fills or reads payload. A mutex adapter is suitable for task or thread calls, but must not be used from an ISR. The application must also prevent a producer and consumer from concurrently using the same reservation outside these calls.

For a single-core Cortex-M system, an interrupt adapter can save PRIMASK, disable interrupts, then restore exactly the saved value. A sketch using CMSIS names is:

```c
static uintptr_t irq_enter(void *context)
{
    (void)context;
    uint32_t previous = __get_PRIMASK();
    __disable_irq();
    __DMB();
    return previous;
}

static void irq_exit(void *context, uintptr_t previous)
{
    (void)context;
    __DMB();
    __set_PRIMASK((uint32_t)previous);
}
```

Verify that the chosen CMSIS intrinsics also act as compiler barriers in the actual toolchain. Disabling local interrupts does not protect against another core or DMA. An ISR must never spin waiting for a lock held by code it interrupted.

For RTOS task-only use, the same callback shape can wrap the RTOS mutex: `enter` locks the mutex and returns zero; `exit` unlocks it and ignores the token. The `context` points to that instance's mutex. The mutex must supply acquire/release ordering, and callers must not use the adapter in an ISR.

For SMP use that includes ISR calls, the platform adapter must first save and disable **local** interrupts, then acquire an SMP-safe spinlock or hardware lock with acquire ordering. On exit it releases that lock with release ordering and restores the saved interrupt state. The lock must work on the actual memory topology and atomic instruction set. A task-only SMP application can instead use an SMP-safe mutex. No single built-in lock can safely cover all these environments.

`WriteBlock` checks descriptor-pool membership in O(1) without storing a pointer in every block. It compares integer representations of addresses, checks alignment to `sizeof(Block)` and the configured element count, then verifies equality with the selected pool element. This assumes the platform represents the array as a monotonically addressed, contiguous region with the normal struct stride. Do not replace this with relational comparisons between unrelated C pointers, which have undefined behavior. Each active RBB must have its own nonoverlapping descriptor pool. `FreeBlock` also verifies membership in the used list; `ReadBlock` and arbitrary `FreeBlock` scan that list and are O(max_block_num), including while interrupts are masked in the single-core adapter. Avoid large descriptor pools in latency-sensitive ISR paths until the worst-case time has been measured.

## Performance check on the target

Build the production configuration, then measure `WriteBlock`, empty `ReadBlock`, successful `AllocateBlock`, and worst-case `ReadBlock`/`FreeBlock` at representative descriptor counts. Compare the same workload with no synchronization, callback-based interrupt masking, and a local static-inline interrupt adapter. Record total operation time and the longest interval with interrupts disabled. Use a hardware cycle counter when the core has one; otherwise use a timer or GPIO trace. Run from the intended memory region because flash wait states affect call costs. Add a build-wide static synchronization backend only if the measured callback overhead is material for the application.
