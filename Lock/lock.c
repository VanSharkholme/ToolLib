//
// Created by VanSharkholme on 2026/9/30.
//

#include "lock.h"
#include "lock_IRQ.h"
#include "lock_spinlock.h"

__weak void Lock_Custom_Init(Lock *lock, void *context)
{
    UNUSED(lock);
    UNUSED(context);
}

void Lock_Init(Lock *lock, Lock_Type type, void* context)
{
    if (lock == NULL)
        return;
    switch (type)
    {
    case Lock_IRQ:
        Lock_IRQ_Init(lock, context);
        break;
    case Lock_Spinlock_IRQ:
        break;
    case Lock_Custom:
        lock->type = Lock_Custom;
        Lock_Custom_Init(lock, context);
    default:
        break;
    }
}