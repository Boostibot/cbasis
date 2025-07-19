#ifndef MODULE_CONC_QUEUE
#define MODULE_CONC_QUEUE

//This is SPMC (Single Producer Multiple Consumer) growing queue. 
// It also supports multiple producers but only with the help of a mutex.
// Another queue implementation that does basically the same thing is the
// Rigtorp queue, see here for explanation: https://rigtorp.se/ringbuffer/.
//
// It is faster than Chase-Lev or similar queues because it drastically reduces the need to
// read data of the "other side" (that is producer reading consumers data and vice versa)
// thus lowering contention/false sharing. This is done by keeping an estimate
// of the other threads data and only updating that estimate when exceptional state of
// empty/full is reached.
//
// The queue functions marked with *_st should be read as Single Thread and as the name
// suggests should be called from a single thread at a time. The push has primarily only 
// st. variant and the non-st variant is achieved with a mutex. 
// On the other hand pop has both st and non-st variant. The st. variant runs a bit faster b
// because it doesn't have to use any synchronization with other popping threads. Prefer the
// st. variants when possible. 
// 
// Further we allow pushing/popping of multiple items at once. This is practical as often the producer
// has to submit multiple work items anyway and at the same time is even more performant.
//
// If you want to, you can define CONC_QUEUE_API to be __forceinline static and use the sized API. 
// This should give should significant speedup as it will lead to the functions being inlined and 
// due to the count/item size/item pointer being known, elision of many branches. 
// In that case, however, be sure to not use these functions too much or wrap them in a type 
// specific function call to not explode the code size.

#ifndef CONC_QUEUE_API
    #define CONC_QUEUE_API                
#endif

#ifndef CONC_QUEUE_CACHE_LINE
    //on apple chips this is the case so we are being optimistic
    #define CONC_QUEUE_CACHE_LINE 128 
#endif

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
    #include <atomic>
    #include <mutex>
    #define CONC_QUEUE_ATOMIC(T) std::atomic<T>
#else
    #include <stdatomic.h>
    #include <stdalign.h>
    #define CONC_QUEUE_ATOMIC(T) _Atomic(T)   
#endif

typedef int64_t isize;

typedef struct Conc_Queue_Block {
    uint64_t mask; //capacity - 1
    struct Conc_Queue_Block* prev; //previous block of smaller capacity
    void*    alloced_block; //pointer to start of the allocated block
    uint64_t alloced_size; //size of the allocated block
    uint8_t data[]; //items aligned to cache line
} Conc_Queue_Block;

typedef struct Conc_Queue {
    alignas(CONC_QUEUE_CACHE_LINE) struct {
        CONC_QUEUE_ATOMIC(Conc_Queue_Block*) block;
        CONC_QUEUE_ATOMIC(uint64_t)          head;
        CONC_QUEUE_ATOMIC(uint64_t)          estimate_tail;
        isize item_size;
    } pop;

    alignas(CONC_QUEUE_CACHE_LINE) struct {
        Conc_Queue_Block*           block;
        uint64_t                    estimate_head;
        CONC_QUEUE_ATOMIC(uint64_t) tail;
        isize item_size;
        isize max_capacity; //zero or negative means no max capacity
        void* mutex; //used only if needs multiple producers.
    } push;
} Conc_Queue;

typedef enum Conc_Queue_Error{
    CONC_QUEUE_OK = 0,
    CONC_QUEUE_EMPTY,
    CONC_QUEUE_FULL,
    CONC_QUEUE_FAILED_RACE, //only returned from conc_queue_sized_pop_weak function
} Conc_Queue_Error;

//Contains the state indicator as well as block, tail, head 
// which hold values obtained *before* the call to the said function
//When doing push operation, head might be an estimate
//When doing a pop operation, tail might be an estimate
typedef struct Conc_Queue_Result {
    uint64_t tail;
    uint64_t head;
    Conc_Queue_Error error;
    uint32_t success; //the number of items that were successfully pushed/popped.
} Conc_Queue_Result;

//Contains the exact state of the queue at some point in execution history
typedef struct Conc_Queue_State {
    Conc_Queue_Block* block;
    uint64_t tail;
    uint64_t head;
    isize capacity;
    isize count;
} Conc_Queue_State;

CONC_QUEUE_API void conc_queue_deinit(Conc_Queue* queue);
CONC_QUEUE_API void conc_queue_init(Conc_Queue* queue, isize item_size, isize max_capacity_or_negative_if_infinite);
CONC_QUEUE_API void conc_queue_reserve(Conc_Queue* queue, isize to_size);
CONC_QUEUE_API isize conc_queue_count(const Conc_Queue* q); 
CONC_QUEUE_API isize conc_queue_capacity(const Conc_Queue* q);
CONC_QUEUE_API Conc_Queue_State conc_queue_state(const Conc_Queue* q); 

CONC_QUEUE_API Conc_Queue_Result conc_queue_push(Conc_Queue* q, const void* items_or_null, isize count);
CONC_QUEUE_API Conc_Queue_Result conc_queue_pop(Conc_Queue* q, void* items_or_null, isize count);
CONC_QUEUE_API Conc_Queue_Result conc_queue_push_st(Conc_Queue* q, const void* items_or_null, isize count);
CONC_QUEUE_API Conc_Queue_Result conc_queue_pop_st(Conc_Queue* q, void* items_or_null, isize count);
CONC_QUEUE_API Conc_Queue_Result conc_queue_clear(Conc_Queue* q);

CONC_QUEUE_API Conc_Queue_Result conc_queue_sized_push(Conc_Queue* q, isize item_size, const void* items_or_null, isize count);
CONC_QUEUE_API Conc_Queue_Result conc_queue_sized_push_st(Conc_Queue* q, isize item_size, const void* items_or_null, isize count);
CONC_QUEUE_API Conc_Queue_Result conc_queue_sized_pop(Conc_Queue* q, isize item_size, void* items_or_null, isize count);
CONC_QUEUE_API Conc_Queue_Result conc_queue_sized_pop_st(Conc_Queue* q, isize item_size, void* items_or_null, isize count);
CONC_QUEUE_API Conc_Queue_Result conc_queue_sized_pop_weak(Conc_Queue* q, isize item_size, void* items_or_null, isize count, bool is_single_consumer);

#endif

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_CONC_QUEUE_IMPL)) && !defined(MODULE_CONC_QUEUE_HAS_IMPL)
#define MODULE_CONC_QUEUE_HAS_IMPL

#ifdef MODULE_COUPLED
    #include "assert.h"
#endif

#ifndef ASSERT
    #include <assert.h>
    #define ASSERT(x, ...) assert(x)
#endif

#ifdef __cplusplus
    #define _CONC_QUEUE_USE_ATOMICS \
        using std::memory_order_acquire;\
        using std::memory_order_release;\
        using std::memory_order_seq_cst;\
        using std::memory_order_relaxed;\
        using std::memory_order_consume;
#else
    #define _CONC_QUEUE_USE_ATOMICS
#endif

#ifndef _conc_queue_mutex_init
    #ifdef __cplusplus
        #include <mutex>
        static inline void _conc_queue_mutex_init(void** mtx)   { *mtx = new std::mutex(); }
        static inline void _conc_queue_mutex_deinit(void** mtx) { delete ((std::mutex*) *mtx); *mtx = NULL; }
        static inline void _conc_queue_mutex_lock(void* mtx)    { ((std::mutex*) mtx)->lock(); }
        static inline void _conc_queue_mutex_unlock(void* mtx)  { ((std::mutex*) mtx)->unlock(); }
    #else
        #include <threads.h>
        static inline void _conc_queue_mutex_lock(void* mtx)    { mtx_lock((mtx_t*) mtx); }
        static inline void _conc_queue_mutex_unlock(void* mtx)  { mtx_unlock((mtx_t*) mtx); }
        static inline void _conc_queue_mutex_init(void** mtx)   { *mtx = calloc(sizeof(mtx_t), 1); mtx_init((mtx_t*) *mtx, mtx_plain); }
        static inline void _conc_queue_mutex_deinit(void** mtx) { 
            if(*mtx == NULL)
                return;
            mtx_destroy((mtx_t*) *mtx); 
            free(*mtx); 
            *mtx = NULL; 
        }
    #endif

    #define _conc_queue_mutex_init _conc_queue_mutex_init
#endif

CONC_QUEUE_API void conc_queue_deinit(Conc_Queue* queue)
{
    _CONC_QUEUE_USE_ATOMICS;
    for(Conc_Queue_Block* curr = queue->push.block; curr; ) {
        Conc_Queue_Block* prev = curr->prev;
        free(curr->alloced_block);
        curr = prev;
    }

    memset(queue, 0, sizeof *queue);
    _conc_queue_mutex_deinit(&queue->push.mutex);
    atomic_store(&queue->pop.block, NULL);
}

CONC_QUEUE_API void conc_queue_init(Conc_Queue* queue, isize item_size, isize max_capacity_or_negative_if_infinite)
{
    _CONC_QUEUE_USE_ATOMICS;
    ASSERT(0 <= item_size);
    conc_queue_deinit(queue);
    _conc_queue_mutex_init(&queue->push.mutex);
    queue->push.max_capacity = max_capacity_or_negative_if_infinite;
    queue->push.item_size = item_size;
    queue->pop.item_size = item_size;
    atomic_store(&queue->pop.block, NULL);
}

CONC_QUEUE_API isize conc_queue_capacity(const Conc_Queue* q)
{
    _CONC_QUEUE_USE_ATOMICS;
    Conc_Queue_Block *block = atomic_load_explicit(&q->pop.block, memory_order_relaxed);
    return block ? (isize) block->mask + 1 : 0;
}

CONC_QUEUE_API isize conc_queue_count(const Conc_Queue* q)
{
    _CONC_QUEUE_USE_ATOMICS;
    uint64_t head_old = atomic_load_explicit(&q->pop.head, memory_order_relaxed);
    uint64_t tail_old = atomic_load_explicit(&q->push.tail, memory_order_relaxed);
    for(;;) {
        atomic_thread_fence(memory_order_seq_cst);
        uint64_t head = atomic_load_explicit(&q->pop.head, memory_order_relaxed);
        uint64_t tail = atomic_load_explicit(&q->push.tail, memory_order_relaxed);

        if(head == head_old && tail == tail_old) {
            isize diff = (isize) (tail - head);
            ASSERT(diff >= 0);
            return diff;
        }

        head_old = head;
        tail_old = tail;
    }
}

CONC_QUEUE_API Conc_Queue_State conc_queue_state(const Conc_Queue* q)
{
    _CONC_QUEUE_USE_ATOMICS;
    Conc_Queue_Block* block_old = atomic_load_explicit(&q->pop.block, memory_order_relaxed);
    uint64_t head_old = atomic_load_explicit(&q->pop.head, memory_order_relaxed);
    uint64_t tail_old = atomic_load_explicit(&q->push.tail, memory_order_relaxed);
    for(;;) {
        atomic_thread_fence(memory_order_seq_cst);
        Conc_Queue_Block* block = atomic_load_explicit(&q->pop.block, memory_order_relaxed);
        uint64_t head = atomic_load_explicit(&q->pop.head, memory_order_relaxed);
        uint64_t tail = atomic_load_explicit(&q->push.tail, memory_order_relaxed);

        if(head == head_old && tail == tail_old && block == block_old) {
            Conc_Queue_State state = {block, head, tail};
            state.count = (isize) (tail - head);
            state.capacity = state.block ? state.block->mask + 1 : 0;
            ASSERT(state.count >= 0);

            return state;
        }

        block_old = block;
        head_old = head;
        tail_old = tail;
    }
}

//prevent inlinling
#if defined(_MSC_VER)
    #define ATTRIBUTE_INLINE_NEVER  __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    #define ATTRIBUTE_INLINE_NEVER  __attribute__((noinline))                         
#endif
static Conc_Queue_Block* _conc_queue_reserve(Conc_Queue* queue, isize to_size)
{
    _CONC_QUEUE_USE_ATOMICS;
    Conc_Queue_Block* old_block = queue->push.block;
    Conc_Queue_Block* out_block = old_block;
    isize old_cap = old_block ? (isize) (old_block->mask + 1) : 0;
    isize item_size = queue->push.item_size;
    isize max_capacity = queue->push.max_capacity >= 0 ? queue->push.max_capacity : INT64_MAX;

    if(old_cap < to_size && to_size <= max_capacity)
    {
        uint64_t new_cap = 64;
        while((isize) new_cap < to_size)
            new_cap *= 2;

        const uint64_t C = CONC_QUEUE_CACHE_LINE;
        const uint64_t O = offsetof(Conc_Queue_Block, data);
        
        uint64_t new_block_size = sizeof(Conc_Queue_Block) + new_cap*item_size + C;
        uint8_t* new_block_alloced = (uint8_t*) calloc(new_block_size, 1);
        if(new_block_alloced)
        {
            //offset in such a way that the data of the new block is aligned to cache line
            uint64_t new_block_aligned = ((uint64_t) new_block_alloced + O + C - 1)/C*C - O;
            Conc_Queue_Block* new_block = (Conc_Queue_Block*) new_block_aligned;
            new_block->alloced_size = new_block_size;
            new_block->alloced_block = new_block_alloced;
            new_block->prev = old_block;
            new_block->mask = new_cap - 1;
            if(old_block)
            {
                uint64_t head = atomic_load_explicit(&queue->pop.head, memory_order_seq_cst);
                uint64_t tail = atomic_load_explicit(&queue->push.tail, memory_order_seq_cst);
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

CONC_QUEUE_API Conc_Queue_Result conc_queue_sized_push_st(Conc_Queue* q, isize item_size, const void* items_or_null, isize count)
{
    _CONC_QUEUE_USE_ATOMICS;

    Conc_Queue_Block *block = q->push.block;
    uint64_t mask = block->mask;
    uint64_t tail = atomic_load_explicit(&q->push.tail, memory_order_relaxed);
    uint64_t head = q->push.estimate_head;

    if (block == NULL || (int64_t)(tail - head) + count > (int64_t) mask+1) { 
        head = atomic_load_explicit(&q->pop.head, memory_order_relaxed);
        q->push.estimate_head = head;
        if (block == NULL || (int64_t)(tail - head) + count > (int64_t) mask+1) { 
            Conc_Queue_Block* new_block = _conc_queue_reserve(q, tail - head + count);
            //if allocation failed (normally or because we set max capacity)
            if(new_block == block) {
                Conc_Queue_Result out = {tail, head, CONC_QUEUE_FULL};
                return out;
            }

            block = new_block;
            mask = block->mask;
        }
    }

    if(items_or_null) {
        isize i0 = tail & mask;
        isize i1 = (tail + count) & mask;
        if(count == 1 || i0 < i1)
            memcpy(block->data + i0*item_size, items_or_null, count*item_size);
        else {
            isize count0 = mask+1 - i0;
            isize count1 = count - count0;
            const void* to0 = items_or_null;
            const void* to1 = (uint8_t*) items_or_null + count0*item_size;
            memcpy(block->data + i0*item_size, to0, count0*item_size);
            memcpy(block->data + i1*item_size, to1, count1*item_size);
        }
    }

    atomic_store_explicit(&q->push.tail, tail + count, memory_order_seq_cst);
    Conc_Queue_Result out = {tail, head, CONC_QUEUE_OK, (uint32_t) count};
    return out;
}


CONC_QUEUE_API Conc_Queue_Result conc_queue_sized_pop_weak(Conc_Queue* q, isize item_size, void* items_or_null, isize count, bool is_single_consumer)
{
    _CONC_QUEUE_USE_ATOMICS;
    uint64_t head = atomic_load_explicit(&q->pop.head, memory_order_relaxed);
    uint64_t tail = atomic_load_explicit(&q->pop.estimate_tail, memory_order_relaxed);
    
    //if empty or not enough items reload tail estimate
    if ((int64_t) (tail - head) < count) {
        tail = atomic_load_explicit(&q->push.tail, memory_order_seq_cst);
        atomic_store_explicit(&q->pop.estimate_tail, tail, memory_order_relaxed);

        //if completely empty just quit
        if ((int64_t) (tail - head) <= 0) {
            Conc_Queue_Result out = {tail, head, CONC_QUEUE_EMPTY};
            return out;
        }
    }
    
    // Load block with seq cst because we must ensure we dont get updated head,tail and old block! 
    // Then we would assume there are items to pop, copy over uninitialized memory from old block and succeed. (bad!)
    // If you dont like this you can instead store all of the fields of queue (head, estimate_tail, tail...)
    //  in the block tailer instead. That way it will be again impossible to get head, tail and old block.
    // I dont bother with this as I primarily care about x86 and I find the code written like this be easier to read. 
    isize popped = count;
    if(count == 1) {
        if(items_or_null) {
            Conc_Queue_Block *block = atomic_load_explicit(&q->pop.block, memory_order_seq_cst);
            isize i0 = tail & block->mask;
            memcpy(block->data + i0*item_size, items_or_null, item_size);
        }
    }
    else {
        isize rem_items = (int64_t) (tail - head);
        if(popped > rem_items)
            popped = rem_items;

        if(items_or_null) {
            Conc_Queue_Block *block = atomic_load_explicit(&q->pop.block, memory_order_seq_cst);
            uint64_t mask = block->mask;
            isize i0 = tail & mask;
            isize i1 = (tail + popped) & mask;
            if(i0 < i1)
                memcpy(block->data + i0*item_size, items_or_null, popped*item_size);
            else {
                isize count0 = mask+1 - i0;
                isize count1 = popped - count0;
                void* to0 = items_or_null;
                void* to1 = (uint8_t*) items_or_null + count0*item_size;
                memcpy(to0, block->data + i0*item_size, count0*item_size);
                memcpy(to1, block->data + i1*item_size, count1*item_size);
            }
        }
    }
    
    Conc_Queue_Result out = {tail, head, CONC_QUEUE_OK, (uint32_t) popped};
    if(is_single_consumer) 
        atomic_store_explicit(&q->pop.head, head + popped, memory_order_relaxed);
    else if (!atomic_compare_exchange_strong_explicit(&q->pop.head, &head, head + popped, memory_order_relaxed, memory_order_relaxed)) {
        out.error = CONC_QUEUE_FAILED_RACE;
        out.success = 0;
    }

    return out;
}

CONC_QUEUE_API Conc_Queue_Result conc_queue_sized_push(Conc_Queue* q, isize item_size, const void* items_or_null, isize count)
{
    _conc_queue_mutex_lock(q->push.mutex);
    Conc_Queue_Result res = conc_queue_sized_push_st(q, item_size, items_or_null, count);
    _conc_queue_mutex_unlock(q->push.mutex);
    return res;
}

CONC_QUEUE_API Conc_Queue_Result conc_queue_sized_pop(Conc_Queue* q, isize item_size, void* items, isize count)
{
    for(;;) {
        Conc_Queue_Result result = conc_queue_sized_pop_weak(q, item_size, items, count, false);
        if(result.error != CONC_QUEUE_FAILED_RACE)
            return result;
    }
}

CONC_QUEUE_API Conc_Queue_Result conc_queue_sized_pop_st(Conc_Queue* q, isize item_size, void* items, isize count)
{
    return conc_queue_sized_pop_weak(q, item_size, items, count, true);
}

CONC_QUEUE_API void conc_queue_reserve(Conc_Queue* q, isize to_size)
{
    _conc_queue_mutex_lock(q->push.mutex);
    _conc_queue_reserve(q, to_size);
    _conc_queue_mutex_unlock(q->push.mutex);
}

CONC_QUEUE_API Conc_Queue_Result conc_queue_push(Conc_Queue* q, const void* items_or_null, isize count) {
    return conc_queue_sized_push(q, q->push.item_size, items_or_null, count); 
}
CONC_QUEUE_API Conc_Queue_Result conc_queue_pop(Conc_Queue* q, void* items_or_null, isize count) {
    return conc_queue_sized_pop(q, q->pop.item_size, items_or_null, count); 
}
CONC_QUEUE_API Conc_Queue_Result conc_queue_push_st(Conc_Queue* q, const void* items_or_null, isize count) {
    return conc_queue_sized_push_st(q, q->push.item_size, items_or_null, count); 
}
CONC_QUEUE_API Conc_Queue_Result conc_queue_pop_st(Conc_Queue* q, void* items_or_null, isize count) {
    return conc_queue_sized_pop_st(q, q->pop.item_size, items_or_null, count); 
}
CONC_QUEUE_API Conc_Queue_Result conc_queue_clear(Conc_Queue* q) {
    return conc_queue_sized_pop(q, q->pop.item_size, NULL, 0xFFFFFFFFFFFF); //full 48 bits worth of items - should be enough but not cause overflows 
}

#endif