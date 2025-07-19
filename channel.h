#ifndef MODULE_CHANNEL
#define MODULE_CHANNEL

//==========================================================================
// Channel (high throuput concurrent queue)
//==========================================================================
// An linearizable blocking concurrent queue based on the design described in 
// "T. R. W. Scogland - Design and Evaluation of Scalable Concurrent Queues for Many-Core Architectures, 2015" 
// which can be found at https://synergy.cs.vt.edu/pubs/papers/scogland-queues-icpe15.pdf.
// 
// We differ from the implementation in the paper in that we support proper thread blocking via futexes and
// employ more useful semantics around closing.
// 
// The channel acts pretty much as a Go buffered channel augmented with additional
// non-blocking and ticket interfaces. These allow us to for example only push if the
// channel is not full or wait for item to be processed.
//
// The basic idea is to do a very fine grained locking: each item in the channel has a dedicated
// ticket lock. On push/pop we perform atomic fetch and add (FAA) one to the tail/head indices, which yields
// a number used to calculate our slot and operation id. This slot is potentially shared with other 
// pushes or pops because the queue has finite capacity. We go to that slot and wait on its ticket lock 
// to signal id corresponding to this push/pop operation. Only then we push/pop the item then advance 
// the ticket lock, allowing a next operation on that slot to proceed.
// 
// This procedure means that unless the queue is full/empty a single push/pop contains only 
// one atomic FAA on the critical path (ticket locks are uncontested), resulting in extremely
// high throughput pretty much only limited by the FAA contention.

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
    #include <atomic>
    #define CHAN_ATOMIC(T) std::atomic<T>
#else
    #include <stdatomic.h>
    #include <stdalign.h>
    #define CHAN_ATOMIC(T) _Atomic(T) 
#endif

#ifndef CHAN_CUSTOM
    #define CHANAPI 
    #define CHAN_CACHE_LINE 64
#endif

typedef int64_t isize;
typedef uint64_t Chan_ID;

typedef bool (*Sync_Wait_Func)(volatile void* state, uint32_t undesired, double timeout_or_negative_if_infinite);
typedef void (*Sync_Wake_Func)(volatile void* state);

typedef enum Channel_Close_Kind {
    //The channel is open and normal semantics apply.
    CHANNEL_OPEN = 0,

    //After completion of all queued operations closes the channel causing all operations to fail.
    //Can be though of as the least violent closing option as it does not stop anything prior to this call from completing.
    CHANNEL_CLOSE_EVENTUALLY,       
    
    //Closes the push side of the channel. All future push operations will fail. 
    //Pops will succeed only until the channel is empty, then they will fail. 
    //If the channel was full and there were queued pushes, these pushes will still block as usuall until sufficient space becomes availible.
    CHANNEL_CLOSE_PUSH,              
    CHANNEL_CLOSE_POP, //same as above except PUSH <-> POP
    
    //Closes the push side of the channel aborting queued pushes. All future push operations will fail. 
    //Pops will succeed only until the channel is empty, then they will fail. 
    //If the channel was full and there were queued pushes, these pushes will be aborted and fail.
    CHANNEL_CLOSE_PUSH_ABORT_QUEUED, 
    CHANNEL_CLOSE_POP_ABORT_QUEUED,  //same as above except PUSH <-> POP 

    //Aborts all not yet completed pushes and pops immediatelly. 
    //Breaks all invariants of the channel and discards all remaining items. 
    //After closing with this flag the channel cannot be reopened.
    CHANNEL_CLOSE_DESTRUCTIVE_ABORT, 
} Channel_Close_Kind;

typedef enum Channel_Status {
    CHANNEL_OK = 0,
    CHANNEL_FULL,
    CHANNEL_EMPTY,
    CHANNEL_CLOSED,
    CHANNEL_LOST_RACE,
} Channel_Status;

typedef struct Channel_Side {
    CHAN_ATOMIC(uint64_t) index;
    CHAN_ATOMIC(uint64_t) barrier;
    CHAN_ATOMIC(uint64_t) cancel_count;
} Channel_Side;

typedef struct Channel_Slot {
    CHAN_ATOMIC(Chan_ID) id;
    uint8_t data[];
} Channel_Slot;

typedef struct Channel {
    alignas(CHAN_CACHE_LINE) 
    Channel_Side head;

    alignas(CHAN_CACHE_LINE) 
    Channel_Side tail;

    alignas(CHAN_CACHE_LINE) 
    Channel_Slot* slots; 
    isize capacity; 
    isize item_size;
    isize slot_size;
    Sync_Wait_Func wait;
    Sync_Wake_Func wake;
    CHAN_ATOMIC(Channel_Close_Kind) close_kind;
    bool allocated;
} Channel;

typedef struct Channel_Result {
    void* item;
    Channel_Status status;
    uint64_t id;
    Channel_Slot* slot;
} Channel_Result;

CHANAPI void channel_init(Channel* chan, isize capacity, isize item_size, Sync_Wait_Func wait, Sync_Wake_Func wake);
CHANAPI isize channel_init_with_memory(Channel* chan, void* memory, isize memory_size, isize item_size, Sync_Wait_Func wait, Sync_Wake_Func wake);
CHANAPI void channel_deinit(Channel* chan);
CHANAPI bool channel_close(Channel* chan, Channel_Close_Kind kind);
CHANAPI bool channel_reopen(Channel* chan);

CHANAPI bool channel_push(Channel* chan, const void* item);
CHANAPI bool channel_pop(Channel* chan, void* item);
CHANAPI Channel_Status channel_try_push(Channel* chan, const void* item);
CHANAPI Channel_Status channel_try_pop(Channel* chan, void* item);

CHANAPI bool channel_push_begin(Channel* chan, Channel_Result* result);
CHANAPI bool channel_pop_begin(Channel* chan, Channel_Result* result);
CHANAPI bool channel_try_push_begin(Channel* chan, Channel_Result* result);
CHANAPI bool channel_try_pop_begin(Channel* chan, Channel_Result* result);
CHANAPI void channel_push_end(Channel* chan, const Channel_Result* result);
CHANAPI void channel_pop_end(Channel* chan, const Channel_Result* result);

CHANAPI isize channel_count(const Channel* chan);
CHANAPI isize channel_capacity(const Channel* chan);
CHANAPI Channel_Close_Kind channel_closed(const Channel* chan); 

typedef struct Channel_State {
    isize count;
    isize billance;
    Channel_Close_Kind closed;
    uint64_t head;
    uint64_t tail;
} Channel_State;

CHANAPI Channel_State channel_state(const Channel* chan);
CHANAPI void chan_futex_wake_all(volatile uint32_t* state);
CHANAPI void chan_futex_wake_single(volatile uint32_t* state);
CHANAPI bool chan_futex_wait(volatile uint32_t* state, uint32_t undesired, double timeout_or_negatove_if_infinite);
#endif

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_CHANNEL)) && !defined(MODULE_HAS_IMPL_CHANNEL)
#define MODULE_HAS_IMPL_CHANNEL

#ifdef MODULE_ALL_COUPLED
    #include "assert.h"
#endif

#ifndef ASSERT
    #include <assert.h>
    #define ASSERT(x, ...) assert(x)
    #define REQUIRE(x, ...) assert(x)    
#endif

#ifdef __cplusplus
    #define _CHAN_USE_ATOMICS \
        using std::memory_order; \
        using std::memory_order_acquire;\
        using std::memory_order_release;\
        using std::memory_order_seq_cst;\
        using std::memory_order_relaxed;\
        using std::memory_order_consume;
#else
    #define _CHAN_USE_ATOMICS
#endif

#if defined(_MSC_VER)
    #define _CHAN_INLINE_ALWAYS   __forceinline
    #define _CHAN_INLINE_NEVER    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    #define _CHAN_INLINE_ALWAYS   __attribute__((always_inline)) inline
    #define _CHAN_INLINE_NEVER    __attribute__((noinline))
#else
    #define _CHAN_INLINE_ALWAYS   inline
    #define _CHAN_INLINE_NEVER
#endif

#define _CHAN_INDEX_CLOSED_BIT   1
#define _CHAN_INDEX_INCREMENT    2

#define _CHAN_ID_WAITING_BIT     1
#define _CHAN_ID_CLOSED_BIT      2
#define _CHAN_ID_FILLED_BIT      4

static inline bool _channel_id_equals(uint32_t id1, uint32_t id2)
{
    return ((id1 ^ id2) / _CHAN_ID_FILLED_BIT) == 0;
}

static inline Channel_Slot* _channel_slot(Channel* chan, uint64_t target)
{
    return (Channel_Slot*) ((uint8_t*) chan->slots + target*chan->slot_size);
}

_CHAN_INLINE_ALWAYS
static bool _channel_push_pop_begin(Channel* chan, Channel_Result* result, bool is_push) 
{
    _CHAN_USE_ATOMICS;

    Channel_Side* side = is_push ? &chan->tail : &chan->head;
    uint64_t index = atomic_fetch_add_explicit(&side->index, _CHAN_INDEX_INCREMENT, memory_order_relaxed);
    uint64_t ticket = index / _CHAN_INDEX_INCREMENT;
    uint64_t target = ticket % (uint64_t) chan->capacity;
    Chan_ID id = (ticket / (uint64_t) chan->capacity)*_CHAN_ID_FILLED_BIT*2;
    id += is_push ? 0 : _CHAN_ID_FILLED_BIT;

    Channel_Slot* slot = _channel_slot(chan, target);
    for(;;) {
        Chan_ID curr_id = atomic_load_explicit(&slot->id, memory_order_seq_cst);

        //check for closed first.
        if((index & _CHAN_INDEX_CLOSED_BIT) | (curr_id & _CHAN_ID_CLOSED_BIT)) {
            //if closed then load 
            uint64_t barrier = atomic_load_explicit(&side->barrier, memory_order_seq_cst);
            Channel_Close_Kind close_kind = atomic_load_explicit(&chan->close_kind, memory_order_seq_cst);
            ASSERT(close_kind != CHANNEL_OPEN);
            ASSERT(atomic_load_explicit(&side->index, memory_order_seq_cst) & _CHAN_INDEX_CLOSED_BIT);

            if(close_kind == CHANNEL_CLOSE_DESTRUCTIVE_ABORT || barrier <= ticket) {
                atomic_fetch_add_explicit(&side->cancel_count, _CHAN_INDEX_INCREMENT, memory_order_seq_cst);
                atomic_fetch_sub_explicit(&side->index, _CHAN_INDEX_INCREMENT, memory_order_seq_cst);

                result->status = CHANNEL_CLOSED;
                result->id = curr_id;
                result->slot = slot;
                result->item = NULL;
                return false;
            }
        }

        //then check if we match 
        if(_channel_id_equals(curr_id, id))
            break;
            
        if(chan->wake) {
            atomic_fetch_or(&slot->id, _CHAN_ID_WAITING_BIT);
            curr_id |= _CHAN_ID_WAITING_BIT;
        }

        if(chan->wait)
            chan->wait(&slot->id, curr_id, -1);
    }

    result->status = CHANNEL_OK;
    result->id = id;
    result->slot = slot;
    result->item = slot->data;
    return true;
}

_CHAN_INLINE_ALWAYS
static bool _channel_try_push_pop_begin(Channel* chan, Channel_Result* result, bool is_push) 
{
    _CHAN_USE_ATOMICS;

    Channel_Side* side = is_push ? &chan->tail : &chan->head;
    uint64_t index = atomic_load_explicit(&side->index, memory_order_relaxed);
    uint64_t ticket = index / _CHAN_INDEX_INCREMENT;
    uint64_t target = ticket % (uint64_t) chan->capacity;
    Chan_ID id = (ticket / (uint64_t) chan->capacity)*_CHAN_ID_FILLED_BIT*2;
    id += is_push ? 0 : _CHAN_ID_FILLED_BIT;
    
    Channel_Status status = CHANNEL_OK;
    Channel_Slot* slot = _channel_slot(chan, target);
    Chan_ID curr_id = atomic_load_explicit(&slot->id, memory_order_seq_cst);
    if((index & _CHAN_INDEX_CLOSED_BIT) | (curr_id & _CHAN_ID_CLOSED_BIT)) {
        uint64_t barrier = atomic_load_explicit(&side->barrier, memory_order_seq_cst);
        Channel_Close_Kind close_kind = atomic_load_explicit(&chan->close_kind, memory_order_seq_cst);
        if(close_kind == CHANNEL_CLOSE_DESTRUCTIVE_ABORT || barrier <= ticket) {
            status = CHANNEL_CLOSED;
            goto failed;
        }
    }

    if(_channel_id_equals(curr_id, id) == false) {
        status = CHANNEL_CLOSED;
        goto failed;
    }
        
    if(atomic_compare_exchange_strong_explicit(&side->index, &index, index+_CHAN_INDEX_INCREMENT, 
            memory_order_relaxed, memory_order_relaxed) == false) {
        status = CHANNEL_CLOSED;
        goto failed;
    }
    
    result->status = CHANNEL_OK;
    result->id = id;
    result->slot = slot;
    result->item = slot->data;
    return true;

    failed:
    result->status = status;
    result->id = curr_id;
    result->slot = slot;
    result->item = NULL;
    return false;
}

_CHAN_INLINE_ALWAYS
static void _channel_push_pop_end(Channel* chan, const Channel_Result* result, bool is_push)
{
    _CHAN_USE_ATOMICS;
    Chan_ID new_id = (Chan_ID) (result->id + _CHAN_ID_FILLED_BIT);
    if(chan->wake == NULL) 
        atomic_store_explicit(&result->slot->id, new_id, is_push ? memory_order_seq_cst : memory_order_relaxed);
    else {
        Chan_ID prev_id = atomic_exchange_explicit(&result->slot->id, new_id, memory_order_seq_cst);
        if(prev_id & _CHAN_ID_WAITING_BIT)
            chan->wake(&result->slot->id);
    }
}

_CHAN_INLINE_ALWAYS
static Channel_Result _channel_push_pop(Channel* chan, void* item, bool is_push) 
{
    Channel_Result res = {0};
    if(_channel_push_pop_begin(chan, &res, is_push)) {
        if(is_push)
            memcpy(res.slot->data, item, chan->item_size);
        else
            memcpy(item, res.slot->data, chan->item_size);
        _channel_push_pop_end(chan, &res, is_push);
    }

    return res;
}

_CHAN_INLINE_ALWAYS
static Channel_Result _channel_try_push_pop(Channel* chan, void* item, bool is_push) 
{
    Channel_Result res = {0};
    if(_channel_try_push_pop_begin(chan, &res, is_push)) {
        if(is_push)
            memcpy(res.slot->data, item, chan->item_size);
        else
            memcpy(item, res.slot->data, chan->item_size);
        _channel_push_pop_end(chan, &res, is_push);
    }

    return res;
}

static inline void _channel_close_wakeup_ticket_range(Channel* chan, uint64_t from, uint64_t to)
{
    _CHAN_USE_ATOMICS;
    for(uint64_t ticket = from; ticket < to; ticket++) {
        uint64_t target = ticket % (uint64_t) chan->capacity;
        Channel_Slot* slot = _channel_slot(chan, target);

        atomic_fetch_or_explicit(&slot->id, _CHAN_ID_CLOSED_BIT, memory_order_relaxed);
        if(chan->wake) {
            Chan_ID id = atomic_load_explicit(&slot->id, memory_order_relaxed);
            if(id & _CHAN_ID_WAITING_BIT)
                chan->wake(&slot->id);
        }
    }
}

CHANAPI bool channel_close(Channel* chan, Channel_Close_Kind close_kind) 
{
    _CHAN_USE_ATOMICS;

    bool out = false;
    Channel_Close_Kind curr_close_kind = atomic_load(&chan->close_kind);
    if(close_kind != CHANNEL_OPEN 
        && curr_close_kind == CHANNEL_OPEN 
        && atomic_compare_exchange_strong(&chan->close_kind, &curr_close_kind, close_kind)) 
    {
        atomic_store(&chan->close_kind, close_kind);
        out = true;

        uint64_t tail = 0;
        uint64_t head = 0;
        uint64_t tail_barrier = 0;
        uint64_t head_barrier = 0;
        for(;;) {
            //get accurate head and tail
            for(;;) {
                tail = atomic_load(&chan->tail.index);
                head = atomic_load(&chan->head.index);
                if(tail == atomic_load(&chan->tail.index))
                    break;
            }

            //calculate barrier placement and the appropriate order in which to enforce them.
            // We always start from the more "restrictive" one. For example pops cannot happen
            // when the channel is empty, which is iff head >= tail. Thus if head >= tail
            // we can first enforce the the tail barrier and only then enforce the head barrier.
            // In the time between enforcing tail b. and head b. a new pop can happen - but it will
            // wait since there is nothing to pop (head >= tail), thus whem the head b. is finally enforced
            // it will get cancelled correctly.
            uint64_t cap = chan->capacity*_CHAN_INDEX_INCREMENT;
            uint64_t head_barrier = 0;
            uint64_t tail_barrier = 0;
            enum {
                FIRST_HEAD,
                FIRST_TAIL
            } first_barrier = FIRST_HEAD;

            #define _MIN(a, b) ((a) < (b) ? (a) : (b))
            #define _MAX(a, b) ((a) > (b) ? (a) : (b))

            switch(close_kind) {
                default: ASSERT(false);
                case CHANNEL_CLOSE_DESTRUCTIVE_ABORT:
                case CHANNEL_CLOSE_EVENTUALLY: {
                    head_barrier = _MAX(head, tail);
                    tail_barrier = head_barrier;
                    first_barrier = FIRST_TAIL;
                } break;

                case CHANNEL_CLOSE_PUSH: {
                    head_barrier = tail;
                    tail_barrier = tail; 
                    first_barrier = FIRST_TAIL;
                } break;

                case CHANNEL_CLOSE_POP: {
                    head_barrier = head;
                    tail_barrier = head + cap;
                    first_barrier = FIRST_HEAD;
                } break;

                case CHANNEL_CLOSE_PUSH_ABORT_QUEUED: {
                    tail_barrier = _MIN(head+cap, tail);
                    head_barrier = tail_barrier;
                    first_barrier = tail > head+cap ? FIRST_HEAD : FIRST_TAIL;
                } break;

                case CHANNEL_CLOSE_POP_ABORT_QUEUED: {
                    head_barrier = _MIN(head, tail);
                    tail_barrier = head_barrier;
                    first_barrier = head > tail ? FIRST_TAIL : FIRST_HEAD;
                } break;
            }

            #undef _MIN
            #undef _MAX

            head_barrier /= _CHAN_INDEX_INCREMENT;
            tail_barrier /= _CHAN_INDEX_INCREMENT;

            if(first_barrier == FIRST_TAIL) {
                atomic_store(&chan->tail.barrier, tail_barrier);
                if(atomic_compare_exchange_strong(&chan->tail.index, &tail, tail | _CHAN_INDEX_CLOSED_BIT) == false)
                    continue;
                
                atomic_store(&chan->head.barrier, head_barrier);
                atomic_fetch_or(&chan->head.index, _CHAN_INDEX_CLOSED_BIT);
            }
            else {
                atomic_store(&chan->head.barrier, head_barrier);
                if(atomic_compare_exchange_strong(&chan->head.index, &head, head | _CHAN_INDEX_CLOSED_BIT) == false)
                    continue;
                
                atomic_store(&chan->tail.barrier, tail_barrier);
                atomic_fetch_or(&chan->tail.index, _CHAN_INDEX_CLOSED_BIT);
            }
        }

        ASSERT(head_barrier <= tail_barrier);
        uint64_t head_ticket = head/_CHAN_INDEX_INCREMENT;
        uint64_t tail_ticket = tail/_CHAN_INDEX_INCREMENT;
        
        if(close_kind == CHANNEL_CLOSE_DESTRUCTIVE_ABORT) 
            _channel_close_wakeup_ticket_range(chan, 0, chan->capacity*_CHAN_INDEX_INCREMENT);
        else {
            _channel_close_wakeup_ticket_range(chan, head_barrier, head_ticket);
            _channel_close_wakeup_ticket_range(chan, tail_barrier, tail_ticket);
        }
    }

    return out;
}

static inline isize _channel_calc_count(const Channel* chan, uint64_t head, uint64_t tail) {
    isize count = (isize) (tail/_CHAN_INDEX_INCREMENT) - (isize) (head/_CHAN_INDEX_INCREMENT); 
    if(count < 0)
        count = 0;
    if(count > chan->capacity)
        count = chan->capacity;
    return count;
}

CHANAPI Channel_Close_Kind channel_closed(const Channel* chan)
{
    return atomic_load(&chan->close_kind);
}

CHANAPI isize channel_capacity(const Channel* chan)
{
    return chan->capacity;
}

CHANAPI isize channel_count(const Channel* chan)
{
    _CHAN_USE_ATOMICS;
    for(;;) {
        uint64_t tail = atomic_load(&chan->tail.index);
        uint64_t head = atomic_load(&chan->head.index);
        if(tail == atomic_load(&chan->tail.index))
            return _channel_calc_count(chan, head, tail);
    }
}

CHANAPI Channel_State channel_state(const Channel* chan)
{
    _CHAN_USE_ATOMICS;
    for(;;) {
        uint64_t tail = atomic_load(&chan->tail.index);
        uint64_t head = atomic_load(&chan->head.index);
        Channel_Close_Kind closed = atomic_load(&chan->close_kind);

        if(tail == atomic_load(&chan->tail.index) && 
            head == atomic_load(&chan->head.index)) 
        {
            Channel_State state = {0};
            state.count = _channel_calc_count(chan, head, tail);
            state.billance = (isize) (tail/_CHAN_INDEX_INCREMENT) - (isize) (head/_CHAN_INDEX_INCREMENT);
            state.head = head;
            state.tail = tail;
            state.closed = closed;
            return state;
        }
    }
}

CHANAPI bool channel_reopen(Channel* chan) 
{
    _CHAN_USE_ATOMICS;
    bool out = false;

    Channel_Close_Kind close_kind = atomic_load(&chan->close_kind);
    if(close_kind != CHANNEL_CLOSE_DESTRUCTIVE_ABORT && close_kind != CHANNEL_OPEN) 
    {
        for(isize i = 0; i < chan->capacity; i++) {
            Channel_Slot* slot = _channel_slot(chan, i);
            Chan_ID id = atomic_load_explicit(&slot->id, memory_order_relaxed);
            atomic_store_explicit(&slot->id, id & ~_CHAN_ID_CLOSED_BIT, memory_order_relaxed);
        }

        atomic_store_explicit(&chan->head.barrier, 0, memory_order_relaxed);
        atomic_store_explicit(&chan->head.cancel_count, 0, memory_order_relaxed);
        atomic_store_explicit(&chan->tail.barrier, 0, memory_order_relaxed);
        atomic_store_explicit(&chan->tail.cancel_count, 0, memory_order_relaxed);

        atomic_store_explicit(&chan->close_kind, CHANNEL_OPEN, memory_order_seq_cst);
        out = true;
    }
    return out;
}

CHANAPI void channel_init(Channel* chan, isize capacity, isize item_size, Sync_Wait_Func wait, Sync_Wake_Func wake)
{
    REQUIRE(item_size >= 0 && capacity >= 0);
    isize slot_size = sizeof(Channel_Slot) + (item_size + 7)/8*8;

    isize memory_size = slot_size*capacity;
    void* memory = malloc(memory_size);
    channel_init_with_memory(chan, memory, memory_size, item_size, wait, wake);
    chan->allocated = true;
}

CHANAPI isize channel_init_with_memory(Channel* chan, void* memory, isize memory_size, isize item_size, Sync_Wait_Func wait, Sync_Wake_Func wake)
{
    REQUIRE(item_size >= 0 && memory_size >= 0);
    channel_deinit(chan);

    isize slot_size = sizeof(Channel_Slot) + (item_size + 7)/8*8;
    memset(memory, 0, memory_size);

    chan->allocated = false;
    chan->slots = (Channel_Slot*) memory;
    chan->capacity = memory_size/slot_size;
    chan->item_size = item_size;
    chan->slot_size = slot_size;
    chan->wait = wait;
    chan->wake = wake;

    atomic_store(&chan->head.index, 0);
    atomic_store(&chan->tail.index, 0);
    atomic_store(&chan->close_kind, CHANNEL_OPEN);
    return chan->capacity;
}

CHANAPI void channel_deinit(Channel* chan)
{
    if(chan->allocated)
        free(chan->slots);
    memset(chan, 0, sizeof *chan);
}

CHANAPI bool channel_push(Channel* chan, const void* item)                  { return !_channel_push_pop(chan, (void*) item, true).status; }
CHANAPI bool channel_pop(Channel* chan, void* item)                         { return !_channel_push_pop(chan, (void*) item, false).status; }
CHANAPI Channel_Status channel_try_push(Channel* chan, const void* item)    { return _channel_try_push_pop(chan, (void*) item, true).status; }
CHANAPI Channel_Status channel_try_pop(Channel* chan, void* item)           { return _channel_try_push_pop(chan, (void*) item, false).status; }
CHANAPI bool channel_push_begin(Channel* chan, Channel_Result* result)      { return _channel_push_pop_begin(chan, result, true); }
CHANAPI bool channel_pop_begin(Channel* chan, Channel_Result* result)       { return _channel_push_pop_begin(chan, result, false); }
CHANAPI bool channel_try_push_begin(Channel* chan, Channel_Result* result)  { return _channel_try_push_pop_begin(chan, result, true); }
CHANAPI bool channel_try_pop_begin(Channel* chan, Channel_Result* result)   { return _channel_try_push_pop_begin(chan, result, false); }
CHANAPI void channel_push_end(Channel* chan, const Channel_Result* result)  { return _channel_push_pop_end(chan, result, true); }
CHANAPI void channel_pop_end(Channel* chan, const Channel_Result* result)   { return _channel_push_pop_end(chan, result, true); }

//OS DETECTION
#define CHAN_OS_UNKNOWN     0 
#define CHAN_OS_WINDOWS     1
#define CHAN_OS_UNIX        2
#define CHAN_OS_APPLE_OSX   3

#if !defined(CHAN_OS)
    #undef CHAN_OS
    #if defined(_WIN32) || defined(_WIN64)
        #define CHAN_OS CHAN_OS_WINDOWS // Windows
    #elif defined(__linux__)
        #define CHAN_OS CHAN_OS_UNIX // Debian, Ubuntu, Gentoo, Fedora, openSUSE, RedHat, Centos and other
    #elif defined(__APPLE__) && defined(__MACH__) // Apple OSX and iOS (Darwin)
        #define CHAN_OS CHAN_OS_APPLE_OSX
    #else
        #define CHAN_OS CHAN_OS_UNKNOWN
    #endif
#endif 

#if CHAN_OS == CHAN_OS_WINDOWS
    #pragma comment(lib, "synchronization.lib")
    #include <process.h>
    
    //Instead of including windows.h we 
    typedef int BOOL;
    typedef unsigned long DWORD;
    void __stdcall WakeByAddressSingle(void*);
    void __stdcall WakeByAddressAll(void*);
    BOOL __stdcall WaitOnAddress(volatile void* Address, void* CompareAddress, size_t AddressSize, DWORD dwMilliseconds);
    
    CHANAPI void chan_futex_wake_all(volatile uint32_t* state) {
        WakeByAddressAll((void*) state);
    }
    
    CHANAPI void chan_futex_wake_single(volatile uint32_t* state) {
        WakeByAddressSingle((void*) state);
    }
    
    CHANAPI bool chan_futex_wait(volatile uint32_t* state, uint32_t undesired, double timeout_or_negatove_if_infinite)
    {
        DWORD wait = 0;
        if(timeout_or_negatove_if_infinite < 0)
            wait = (DWORD) -1; //INFINITE
        else
            wait = (DWORD) (timeout_or_negatove_if_infinite*1000);

        bool value_changed = (bool) WaitOnAddress(state, &undesired, sizeof undesired, wait);
        if(!value_changed)
            chan_debug_log("futex timed out", value_changed);
        return value_changed;
    }
#elif CHAN_OS == CHAN_OS_UNIX
    #include <linux/futex.h> 
    #include <sys/syscall.h> 
    #include <unistd.h>
    #include <sched.h>
    #include <errno.h>

    CHANAPI void chan_futex_wake_all(volatile uint32_t* state) {
        syscall(SYS_futex, (void*) state, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, INT32_MAX, NULL, NULL, 0);
    }
    
    CHANAPI void chan_futex_wake_single(volatile uint32_t* state) {
        syscall(SYS_futex, (void*) state, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, 1, NULL, NULL, 0);
    }
    
    CHANAPI bool chan_futex_wait(volatile uint32_t* state, uint32_t undesired, double timeout_or_negatove_if_infinite)
    {
        struct timespec tm = {0};
        struct timespec* tm_ptr = NULL;
        if(timeout_or_negatove_if_infinite >= 0)
        {
            int64_t nanosecs = (int64_t) (timeout_or_negatove_if_infinite*1000000000LL);
            tm.tv_sec = nanosecs / 1000000000LL; 
            tm.tv_nsec = nanosecs % 1000000000LL; 
            tm_ptr = &tm;
        }
        long ret = syscall(SYS_futex, (void*) state, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, undesired, tm_ptr, NULL, 0);
        if (ret == -1 && errno == ETIMEDOUT) 
            return false;
        return true;
    }

#elif CHAN_OS == CHAN_OS_APPLE_OSX
    #error Add OSX support. The following is just a sketch that probably does not even compile (missing headers). \
         I do not have a OSX machine so testing this code is difficult

    //Taken from: https://github.com/colrdavidson/Odin/blob/auto_tracing/src/spall_native_auto.h#L575
    // and from: https://outerproduct.net/futex-dictionary.html#macos
    int __ulock_wait(uint32_t operation, void *addr, uint64_t value, uint32_t timeout_us);
    int __ulock_wake(uint32_t operation, void *addr, uint64_t wake_value);

    #define UL_COMPARE_AND_WAIT		1
    #define ULF_WAKE_ALL			0x00000100
    #define ULF_NO_ERRNO			0x01000000

    CHANAPI void chan_futex_wake_all(volatile uint32_t* state) {
        __ulock_wake(UL_COMPARE_AND_WAIT | ULF_WAKE_ALL | ULF_NO_ERRNO, state, 0);
    }
    
    CHANAPI void chan_futex_wake_single(volatile uint32_t* state) {
        __ulock_wake(UL_COMPARE_AND_WAIT | ULF_NO_ERRNO, state, 0);
    }
    
    CHANAPI bool chan_futex_wait(volatile uint32_t* state, uint32_t undesired, double timeout_or_negatove_if_infinite)
    {
        uint32_t timeout = 0;
        if(timeout_or_negatove_if_infinite >= 0)
        {
            uint64_t microsecs = (uint64_t) (timeout_or_negatove_if_infinite*1000000LL);
            if(microsecs == 0)
                timeout = 1;
            else if (microsecs > UINT32_MAX)
                timeout = UINT32_MAX;
            else
                microsecs = (uint32_t) microsecs;
        }

        int ret = __ulock_wait(UL_COMPARE_AND_WAIT | ULF_NO_ERRNO, state, undesired, timeout);
        return ret >= 0;
    }
#endif
#endif