//
// Created by VanSharkholme on 2026/9/30.
//

#ifndef TOOLLIB_LOCK_IRQ_H
#define TOOLLIB_LOCK_IRQ_H

#include "lock.h"

/* Platform hooks. Restore the token returned for this acquisition. */
uint32_t Lock_IRQ_Disable(const void *context);
void Lock_IRQ_Enable(const void *context, uint32_t saved);

void Lock_IRQ_Init(Lock *lock, void *context);

#endif //TOOLLIB_LOCK_IRQ_H
