//
// Created by VanSharkholme on 2026/9/30.
//

#ifndef TOOLLIB_LOCK_H
#define TOOLLIB_LOCK_H

#include <stdint.h>
#include <stddef.h>
#include "toollib_def.h"

typedef enum
{
    Lock_None,
    Lock_IRQ,
    Lock_Spinlock_IRQ,
    Lock_Custom,
} Lock_Type;

typedef struct Lock Lock;
typedef uintptr_t (*Lock_Acquire)(Lock *lock);
typedef void (*Lock_Release)(Lock *lock, uintptr_t status);

struct Lock
{
    Lock_Type type;
    Lock_Acquire acquire;
    Lock_Release release;
    void *context;
};

void Lock_Init(Lock *lock, Lock_Type type, void* context);

#endif //TOOLLIB_LOCK_H
