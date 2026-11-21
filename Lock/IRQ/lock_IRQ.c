//
// Created by VanSharkholme on 2026/9/30.
//

#include "lock_IRQ.h"

__weak uint32_t Lock_IRQ_Disable(const void *context)
{
    UNUSED(context);
    return 0;
}

__weak void Lock_IRQ_Enable(const void *context, uint32_t level)
{
    UNUSED(context);
    UNUSED(level);
}

static inline uintptr_t Lock_IRQ_Acquire(Lock *lock)
{
    return Lock_IRQ_Disable(lock->context);
}

static inline void Lock_IRQ_Release(Lock *lock, uintptr_t status)
{
    Lock_IRQ_Enable(lock->context, (uint32_t)status);
}

void Lock_IRQ_Init(Lock *lock, void *context)
{
    lock->type = Lock_IRQ;
    lock->acquire = Lock_IRQ_Acquire;
    lock->release = Lock_IRQ_Release;
    lock->context = context;
}