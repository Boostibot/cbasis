#ifndef MODULE_SPMC_QUEUE
#define MODULE_SPMC_QUEUE

//This is SPMC (Single Producer Multiple Consumer) growing queue. 
// Another queue impelemntation that does basically the same thing is the
// Rigtorp queue, see here: https://rigtorp.se/ringbuffer/.

// It is faster than Chase-Lev or similar queues because it drastically reduces the need to
// read other thread's data, thus lowering contention. This is done by keeping an estimate
// of the other threads data and only updating that estimate when something exceptional
// happens, in this case the queue being perceived as empty or full.
//
// The queue functions marked with *_st should be read as Single Thread and as the name
// suggests should be called from a single thread at a time. The push has only st. variant
// while the pop has both st and non-st variant. The st. variant runs a bit faster because
// it doesnt have to use any synchronization with other popping threads, thus should be 
// used when we are only dealing with SPSC situation.

#ifndef SPMC_QUEUE_API
    #define SPMC_QUEUE_API                
#endif

#ifndef SPMC_QUEUE_CACHE_LINE
    #define SPMC_QUEUE_CACHE_LINE 128
#endif

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
    #include <atomic>
    #define SPMC_QUEUE_ATOMIC(T)    std::atomic<T>
#else
    #include <stdatomic.h>
    #include <stdalign.h>
    #define SPMC_QUEUE_ATOMIC(T)    _Atomic(T) 
#endif

typedef int64_t isize;

typedef struct SPMC_Queue_Block {
    struct SPMC_Queue_Block* next;
    uint64_t mask; //capacity - 1
    uint8_t data[];
} SPMC_Queue_Block;

typedef struct SPMC_Queue {
    alignas(SPMC_QUEUE_CACHE_LINE) struct {
        SPMC_QUEUE_ATOMIC(SPMC_Queue_Block*) block;
        SPMC_QUEUE_ATOMIC(uint64_t)          head;
        SPMC_QUEUE_ATOMIC(uint64_t)          estimate_tail;
        isize item_size;
    } pop;

    alignas(SPMC_QUEUE_CACHE_LINE) struct {
        SPMC_Queue_Block*           block;
        uint64_t                    estimate_head;
        SPMC_QUEUE_ATOMIC(uint64_t) tail;
        isize item_size;
        isize max_capacity; //zero or negative means no max capacity
    } push;
} SPMC_Queue;

typedef enum SPMC_Queue_Error{
    SPMC_QUEUE_OK = 0,
    SPMC_QUEUE_EMPTY,
    SPMC_QUEUE_FULL,
    SPMC_QUEUE_FAILED_RACE, //only returned from spmc_queue_pop_weak functions
} SPMC_Queue_Error;

//Contains the state indicator as well as block, tail, head 
// which hold values obtained *before* the call to the said function
//When doing push operation, head might be an estimate
//When doing a pop operation, tail might be an estimate
typedef struct SPMC_Queue_Result {
    uint64_t tail;
    uint64_t head;
    SPMC_Queue_Error error;
    uint32_t success; //the number of items that were successfully pushed/popped.
} SPMC_Queue_Result;

SPMC_QUEUE_API void spmc_queue_deinit(SPMC_Queue* queue);
SPMC_QUEUE_API void spmc_queue_init(SPMC_Queue* queue, isize item_size, isize max_capacity_or_negative_if_infinite);
SPMC_QUEUE_API void spmc_queue_reserve(SPMC_Queue* queue, isize to_size);
SPMC_QUEUE_API SPMC_Queue_Result spmc_queue_push_st(SPMC_Queue *q, const void* item, isize count);
SPMC_QUEUE_API SPMC_Queue_Result spmc_queue_pop_st(SPMC_Queue *q, void* item_or_null, isize count);
SPMC_QUEUE_API SPMC_Queue_Result spmc_queue_pop(SPMC_Queue *q, void* item_or_null, isize count);
SPMC_QUEUE_API SPMC_Queue_Result spmc_queue_pop_weak(SPMC_Queue *q, void* item_or_null, isize count, bool is_single_consumer);

//return the upper/lower estimate of the number of items in the queue. 
//When called from push thread both of these are exact and interchangable
SPMC_QUEUE_API isize spmc_queue_count_upper(const SPMC_Queue *q); 
SPMC_QUEUE_API isize spmc_queue_count_lower(const SPMC_Queue *q); 
SPMC_QUEUE_API isize spmc_queue_count(const SPMC_Queue *q); //returns the count of items in the queue sometime in the execution history.
SPMC_QUEUE_API isize spmc_queue_capacity(const SPMC_Queue *q);

typedef struct SPMC_Queue_State {
    SPMC_Queue_Block* block;
    isize capacity;
    isize count;
    uint64_t tail;
    uint64_t head;
} SPMC_Queue_State;

//returns the current exact state of the queue.
SPMC_QUEUE_API SPMC_Queue_State spmc_queue_state(const SPMC_Queue *q); 
#endif

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_SPMC_QUEUE_IMPL)) && !defined(MODULE_SPMC_QUEUE_HAS_IMPL)
#define MODULE_SPMC_QUEUE_HAS_IMPL

#ifdef MODULE_COUPLED
    #include "assert.h"
#endif

#ifndef ASSERT
    #include <assert.h>
    #define ASSERT(x, ...) assert(x)
#endif

#ifdef __cplusplus
    #define _SPMC_QUEUE_USE_ATOMICS \
        using std::memory_order_acquire;\
        using std::memory_order_release;\
        using std::memory_order_seq_cst;\
        using std::memory_order_relaxed;\
        using std::memory_order_consume;
#else
    #define _SPMC_QUEUE_USE_ATOMICS
#endif

SPMC_QUEUE_API void spmc_queue_deinit(SPMC_Queue* queue)
{
    for(SPMC_Queue_Block* curr = queue->push.block; curr; ) {
        SPMC_Queue_Block* next = curr->next;
        free(curr);
        curr = next;
    }

    memset(queue, 0, sizeof *queue);
    atomic_store(&queue->pop.block, NULL);
}

SPMC_QUEUE_API void spmc_queue_init(SPMC_Queue* queue, isize item_size, isize max_capacity_or_negative_if_infinite)
{
    ASSERT(0 < item_size && item_size <= UINT32_MAX);
    spmc_queue_deinit(queue);
    queue->push.max_capacity = max_capacity_or_negative_if_infinite;
    queue->push.item_size = item_size;
    queue->pop.item_size = item_size;
    atomic_store(&queue->pop.block, NULL);
}

SPMC_QUEUE_API isize spmc_queue_count_upper(const SPMC_Queue *q)
{
    _SPMC_QUEUE_USE_ATOMICS;
    uint64_t head = atomic_load_explicit(&q->pop.head, memory_order_relaxed);
    atomic_thread_fence(memory_order_acquire);
    uint64_t tail = atomic_load_explicit(&q->push.tail, memory_order_relaxed);
    int64_t diff = (int64_t) (tail - head);
    return diff >= 0 ? diff : 0;
}

SPMC_QUEUE_API isize spmc_queue_count_lower(const SPMC_Queue *q)
{
    _SPMC_QUEUE_USE_ATOMICS;
    uint64_t tail = atomic_load_explicit(&q->push.tail, memory_order_relaxed);
    atomic_thread_fence(memory_order_acquire);
    uint64_t head = atomic_load_explicit(&q->pop.head, memory_order_relaxed);
    isize diff = (isize) (tail - head);
    return diff >= 0 ? diff : 0;
}

SPMC_QUEUE_API isize spmc_queue_capacity(const SPMC_Queue *q)
{
    _SPMC_QUEUE_USE_ATOMICS;
    SPMC_Queue_Block *block = atomic_load_explicit(&q->pop.block, memory_order_relaxed);
    return block ? (isize) block->mask + 1 : 0;
}

SPMC_QUEUE_API SPMC_Queue_State spmc_queue_state(const SPMC_Queue *q)
{
    _SPMC_QUEUE_USE_ATOMICS;
    SPMC_Queue_State state = {0};
    uint64_t t0 = atomic_load_explicit(&q->push.tail, memory_order_relaxed);
    for(;;) {
        state.block = atomic_load_explicit(&q->pop.block, memory_order_relaxed);
        state.head = atomic_load_explicit(&q->pop.head, memory_order_acquire);
        state.tail = atomic_load_explicit(&q->push.tail, memory_order_acquire);

        //checks if anything happened between the tail load and head load.
        //If tail == t0 (prev value) than clearly nothing happened thus the
        // calculated tail and head values are "accurate": there was point in time
        // when tail = tail and head = head
        if(state.tail == t0)
            break;

        t0 = state.tail;
    }

    state.capacity = state.block ? state.block->mask + 1 : 0; 
    state.count = (int64_t) (state.tail - state.head);
    if(state.count < 0)
        state.count = 0;

    return state;
}

SPMC_QUEUE_API isize spmc_queue_count(const SPMC_Queue *q)
{
    return spmc_queue_state(q).count;
}

SPMC_QUEUE_API SPMC_Queue_Block* _spmc_queue_reserve(SPMC_Queue* queue, isize to_size)
{
    _SPMC_QUEUE_USE_ATOMICS;
    SPMC_Queue_Block* old_block = queue->push.block;
    SPMC_Queue_Block* out_block = old_block;
    isize old_cap = old_block ? (isize) (old_block->mask + 1) : 0;
    isize item_size = queue->push.item_size;
    isize max_capacity = queue->push.max_capacity >= 0 ? queue->push.max_capacity : INT64_MAX;

    if(old_cap < to_size && to_size <= max_capacity)
    {
        uint64_t new_cap = 64;
        while((isize) new_cap < to_size)
            new_cap *= 2;

        SPMC_Queue_Block* new_block = (SPMC_Queue_Block*) calloc(sizeof(SPMC_Queue_Block) + new_cap*item_size, 1);
        if(new_block)
        {
            new_block->next = old_block;
            new_block->mask = new_cap - 1;

            if(old_block)
            {
                uint64_t head = atomic_load_explicit(&queue->pop.head, memory_order_acquire);
                uint64_t tail = atomic_load_explicit(&queue->push.tail, memory_order_acquire);
                for(uint64_t i = head; (int64_t) (i - tail) < 0; i++) //i < tail 
                {
                    uint8_t* new_ptr = new_block->data + (i & new_block->mask)*item_size;
                    uint8_t* old_ptr = old_block->data + (i & old_block->mask)*item_size;
                    memcpy(new_ptr, old_ptr, item_size);
                }
            }

            queue->push.block = new_block;
            atomic_store_explicit(&queue->pop.block, new_block, memory_order_seq_cst);
            out_block = new_block;
        }
    }

    return out_block;
}

SPMC_QUEUE_API SPMC_Queue_Result spmc_queue_push_st(SPMC_Queue *q, const void* item, isize count)
{
    _SPMC_QUEUE_USE_ATOMICS;

    SPMC_Queue_Block *block = q->push.block;
    uint64_t tail = atomic_load_explicit(&q->push.tail, memory_order_relaxed);
    uint64_t head = q->push.estimate_head;

    if (block == NULL || (int64_t)(tail - head) + count > (int64_t) block->mask+1) { 
        head = atomic_load_explicit(&q->pop.head, memory_order_relaxed);
        q->push.estimate_head = head;
        if (block == NULL || (int64_t)(tail - head) + count > (int64_t) block->mask+1) { 
            SPMC_Queue_Block* new_block = _spmc_queue_reserve(q, tail - head + count);
            //if allocation failed (normally or because we set max capacity)
            if(new_block == block) {
                SPMC_Queue_Result out = {tail, head, SPMC_QUEUE_FULL};
                return out;
            }

            block = new_block;
        }
    }

    if(item) {
        isize item_size = q->push.item_size;
        for(isize i = 0; i < count; i++) {
            void* slot = block->data + ((tail+i) & block->mask)*item_size;
            memcpy(slot, (uint8_t*) item + item_size*i, item_size);
        }
    }

    atomic_store_explicit(&q->push.tail, tail + count, memory_order_release);
    SPMC_Queue_Result out = {tail, head, SPMC_QUEUE_OK, (uint32_t) count};
    return out;
}

ATTRIBUTE_INLINE_NEVER
SPMC_QUEUE_API SPMC_Queue_Result spmc_queue_pop_weak(SPMC_Queue *q, void* items, isize count, bool is_single_consumer)
{
    _SPMC_QUEUE_USE_ATOMICS;
    uint64_t head = atomic_load_explicit(&q->pop.head, memory_order_relaxed);
    uint64_t tail = atomic_load_explicit(&q->pop.estimate_tail, memory_order_relaxed);
    
    //if empty reload tail estimate
    if ((int64_t) (tail - head) <= 0) {
        tail = atomic_load_explicit(&q->push.tail, memory_order_acquire);
        atomic_store_explicit(&q->pop.estimate_tail, tail, memory_order_relaxed);
        if ((int64_t) (tail - head) <= 0) {
            SPMC_Queue_Result out = {tail, head, SPMC_QUEUE_EMPTY};
            return out;
        }
    }
    
    //seq cst because we must ensure we dont get updated head,tail and old block! 
    // Then we would assume there are items to pop, copy over uninitialized memory from old block and succeed. (bad!)
    // For x86 the generated assembly is identical even if we replace it by memory_order_acquire.
    // For weak memory model architectures it wont be. 
    // If you dont like this you can instead store all of the fields of queue (head, estimate_tail, tail...)
    //  in the block tailer instead. That way it will be again impossible to get head, tail and old block.
    //  I dont bother with this as I primarily care about x86 and I find the code written like this be easier to read. 
    SPMC_Queue_Block *block = atomic_load_explicit(&q->pop.block, memory_order_seq_cst);

    isize popped = (int64_t) (tail - head);
    if(popped > count)
        popped = count;

    if(items) {
        isize item_size = q->pop.item_size;
        for(isize i = 0; i < popped; i++) {
            void* slot = block->data + ((head+i) & block->mask)*item_size;
            memcpy(items, slot, item_size);
        }
    }
    
    SPMC_Queue_Result out = {tail, head, SPMC_QUEUE_OK, (uint32_t) popped};
    if(is_single_consumer) 
        atomic_store_explicit(&q->pop.head, head + popped, memory_order_relaxed);
    else if (!atomic_compare_exchange_strong_explicit(&q->pop.head, &head, head + popped, memory_order_relaxed, memory_order_relaxed)) {
        out.error = SPMC_QUEUE_FAILED_RACE;
        out.success = 0;
    }

    return out;
}


SPMC_QUEUE_API SPMC_Queue_Result spmc_queue_pop(SPMC_Queue *q, void* items, isize count)
{
    for(;;) {
        SPMC_Queue_Result result = spmc_queue_pop_weak(q, items, count, false);
        if(result.error != SPMC_QUEUE_FAILED_RACE)
            return result;
    }
}

SPMC_QUEUE_API SPMC_Queue_Result spmc_queue_pop_st(SPMC_Queue *q, void* items, isize count)
{
    return spmc_queue_pop_weak(q, items, count, true);
}

SPMC_QUEUE_API void spmc_queue_reserve(SPMC_Queue* queue, isize to_size)
{
    _spmc_queue_reserve(queue, to_size);
}

#endif