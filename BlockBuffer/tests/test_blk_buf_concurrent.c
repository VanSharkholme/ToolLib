#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sched.h>
#endif

#include "ring_blk_buf.h"

enum { PRODUCERS = 3, CONSUMERS = 3, ITEMS_PER_PRODUCER = 1000,
       TOTAL = PRODUCERS * ITEMS_PER_PRODUCER, CAPACITY = 256, BLOCK_COUNT = 32 };

typedef struct
{
    RingBlockBuffer rbb;
    uint8_t storage[CAPACITY];
    Block pool[BLOCK_COUNT];
    pthread_mutex_t mutex;
    atomic_uint next_ticket;
    atomic_uint consumed;
    atomic_bool failed;
    atomic_uchar seen[TOTAL];
} Stress;

static uintptr_t mutex_enter(void* context)
{
    if (pthread_mutex_lock(context) != 0)
    {
        abort();
    }
    return 0;
}

static void mutex_exit(void* context, uintptr_t state)
{
    (void)state;
    if (pthread_mutex_unlock(context) != 0)
    {
        abort();
    }
}

static void yield_cpu(void)
{
#ifdef _WIN32
    Sleep(0);
#else
    sched_yield();
#endif
}

static uint8_t pattern(unsigned ticket, size_t index)
{
    return (uint8_t)(ticket * 37u + (unsigned)index * 13u);
}

static void* produce(void* arg)
{
    Stress* stress = arg;
    for (unsigned n = 0; n < ITEMS_PER_PRODUCER && !atomic_load(&stress->failed); ++n)
    {
        unsigned ticket = atomic_fetch_add(&stress->next_ticket, 1);
        size_t size = 8u + (ticket * 11u % 24u);
        Block* block = NULL;
        BufferStatus status;
        do
        {
            status = RingBlockBuffer_AllocateBlock(&stress->rbb, size, &block);
            if (status == BUFFER_FULL)
            {
                yield_cpu();
            }
        } while (status == BUFFER_FULL && !atomic_load(&stress->failed));
        if (atomic_load(&stress->failed))
        {
            return NULL;
        }
        if (status != BUFFER_OK || block == NULL)
        {
            atomic_store(&stress->failed, true);
            return NULL;
        }
        memcpy(block->data, &ticket, sizeof ticket);
        for (size_t i = sizeof ticket; i < size; ++i)
        {
            block->data[i] = pattern(ticket, i);
        }
        if (RingBlockBuffer_WriteBlock(&stress->rbb, block) != BUFFER_OK)
        {
            atomic_store(&stress->failed, true);
            return NULL;
        }
    }
    return NULL;
}

static void* consume(void* arg)
{
    Stress* stress = arg;
    while (atomic_load(&stress->consumed) < TOTAL && !atomic_load(&stress->failed))
    {
        Block* block = NULL;
        BufferStatus status = RingBlockBuffer_ReadBlock(&stress->rbb, &block);
        if (status == BUFFER_EMPTY)
        {
            yield_cpu();
            continue;
        }
        if (status != BUFFER_OK || block == NULL || block->size < sizeof(unsigned))
        {
            atomic_store(&stress->failed, true);
            return NULL;
        }
        unsigned ticket;
        memcpy(&ticket, block->data, sizeof ticket);
        if (ticket >= TOTAL || block->size != 8u + (ticket * 11u % 24u))
        {
            atomic_store(&stress->failed, true);
            return NULL;
        }
        for (size_t i = sizeof ticket; i < block->size; ++i)
        {
            if (block->data[i] != pattern(ticket, i))
            {
                atomic_store(&stress->failed, true);
                return NULL;
            }
        }
        if (atomic_exchange(&stress->seen[ticket], 1) != 0 ||
            RingBlockBuffer_FreeBlock(&stress->rbb, block) != BUFFER_OK)
        {
            atomic_store(&stress->failed, true);
            return NULL;
        }
        atomic_fetch_add(&stress->consumed, 1);
    }
    return NULL;
}

int main(void)
{
    Stress* stress = calloc(1, sizeof *stress);
    if (stress == NULL || pthread_mutex_init(&stress->mutex, NULL) != 0)
    {
        return 1;
    }
    atomic_init(&stress->next_ticket, 0);
    atomic_init(&stress->consumed, 0);
    atomic_init(&stress->failed, false);
    for (size_t i = 0; i < TOTAL; ++i)
    {
        atomic_init(&stress->seen[i], 0);
    }
    RingBlockBufferSync sync = {mutex_enter, mutex_exit, &stress->mutex};
    if (RingBlockBuffer_Init(&stress->rbb, stress->storage, CAPACITY,
                             stress->pool, BLOCK_COUNT, &sync) != BUFFER_OK)
    {
        return 1;
    }

    pthread_t producers[PRODUCERS], consumers[CONSUMERS];
    for (size_t i = 0; i < CONSUMERS; ++i)
    {
        if (pthread_create(&consumers[i], NULL, consume, stress) != 0)
        {
            return 1;
        }
    }
    for (size_t i = 0; i < PRODUCERS; ++i)
    {
        if (pthread_create(&producers[i], NULL, produce, stress) != 0)
        {
            return 1;
        }
    }
    for (size_t i = 0; i < PRODUCERS; ++i)
    {
        pthread_join(producers[i], NULL);
    }
    for (size_t i = 0; i < CONSUMERS; ++i)
    {
        pthread_join(consumers[i], NULL);
    }

    bool passed = !atomic_load(&stress->failed) && atomic_load(&stress->consumed) == TOTAL &&
                  stress->rbb.used_list.next == NULL && stress->rbb.tail == &stress->rbb.used_list;
    for (size_t i = 0; i < TOTAL; ++i)
    {
        passed = passed && atomic_load(&stress->seen[i]) == 1;
    }
    pthread_mutex_destroy(&stress->mutex);
    free(stress);
    if (!passed)
    {
        fputs("concurrent block lifecycle failed\n", stderr);
        return 1;
    }
    return 0;
}
