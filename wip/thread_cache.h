#include "../assert.h"
#include "../platform.h"
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef __cplusplus
    #include <atomic>
    #define ATOMIC(T)    std::atomic<T>
#else
    #include <stdatomic.h>
    #include <stdalign.h>
    #define ATOMIC(T)    _Atomic(T) 
#endif

typedef int64_t isize; 
typedef struct Thread_Cache_Thread Thread_Cache_Thread;

typedef struct Thread_Cache_Config {
    isize min_stack_space_or_negative;
    void (*thread_init)(void* context);
    void (*thread_deinit)(void* context);
    void (*thread_before_func)(void* context);
    void (*thread_after_func)(void* context);
    void* thread_context;
} Thread_Cache_Config;

typedef struct Thread_Cache {
    const char* name;
    ATOMIC(Thread_Cache_Thread*) threads;
    ATOMIC(uint64_t) threads_started;
    ATOMIC(uint64_t) threads_finished;
    ATOMIC(uint32_t) threads_init;
    ATOMIC(uint32_t) threads_deinit;
    ATOMIC(uint32_t) is_closed;

    Thread_Cache_Config config;
} Thread_Cache;

typedef enum {
    THREAD_CACHE_IDLE,
    THREAD_CACHE_RESERVED,
    THREAD_CACHE_STARTING,
    THREAD_CACHE_RUNNING,
    THREAD_CACHE_CLOSED,
} Thread_Cache_State;

typedef struct Thread_Cache_Thread {
    //constant for entire lifetime
    Thread_Cache_Thread* next;
    Thread_Cache_Thread* created_from;
    uint64_t stack_size;
    Thread_Cache* cache;

    //changes with every new launch   
    ATOMIC(uint64_t) launch_id_and_state; 

    //protected by lock
    Platform_Shared_Mutex lock;
        void (*func)(void* context);
        void* args;
        isize args_capacity;
        isize args_size;

        char* name;
        isize name_size;
        isize name_capacity;
    
        isize time_started_us;
        isize time_finished_us;
} Thread_Cache_Thread;

typedef struct Thread_Cache_Thread_Properties {
    isize min_stack_size;
    //affinitiy etc.
} Thread_Cache_Thread_Properties;

void thread_cache_deinit(Thread_Cache* cache);
Thread_Cache_Thread* thread_cache_init(Thread_Cache* cache, const Thread_Cache_Config* config_or_null, const char* main_thread_name_fmt, ...);
Thread_Cache_Thread* thread_cache_lunch(Thread_Cache* cache, isize min_stack_size, void (*func)(void* args), const void* args, isize args_size, const char* thread_name_fmt, ...);
Thread_Cache_Thread* thread_cache_get_all(Thread_Cache* cache);
Thread_Cache_Thread* thread_cache_create_paused(Thread_Cache* cache, isize min_stack_size);
Thread_Cache_Thread* thread_cache_self();
const char*          thread_cache_self_name();

#ifdef __cplusplus
    #define _THREAD_CACHE_USE_ATOMICS using namespace std
#else
    #define _THREAD_CACHE_USE_ATOMICS
#endif

#define _Thread_local 

_Thread_local Thread_Cache_Thread* t_thread_cache_thread = NULL; 
static void _thread_cache_run_func(void* context)
{
    _THREAD_CACHE_USE_ATOMICS;
    Thread_Cache_Thread* self = (Thread_Cache_Thread*) context;
    t_thread_cache_thread = self;
    
    Thread_Cache* cache = self->cache;
    Thread_Cache_Config* config = &cache->config;
    if(config->thread_init) 
        config->thread_init(config->thread_context);
    
    for(;;) {
        uint64_t launch_id_and_state = atomic_load_explicit(&self->launch_id_and_state, memory_order_acquire);
        uint64_t state = launch_id_and_state & 0xFF;
        uint64_t launch_id = launch_id_and_state >> 8;

        if(state == THREAD_CACHE_CLOSED)
            break;

        if(state != THREAD_CACHE_RUNNING)
            platform_futex_wait(&self->launch_id_and_state, launch_id_and_state, -1);
        else {
            platform_shared_mutex_shared_lock(&self->lock);
            if(config->thread_before_func) 
                config->thread_before_func(config->thread_context);
            if(self->func) 
                self->func(self->args);
            if(config->thread_after_func) 
                config->thread_after_func(config->thread_context);
            platform_shared_mutex_shared_unlock(&self->lock);

            atomic_store_explicit(&self->launch_id_and_state, (launch_id + 1) << 8 | THREAD_CACHE_IDLE, memory_order_relaxed);
            atomic_fetch_add(&cache->threads_finished, 1);
        }
    }

    if(config->thread_deinit) 
        config->thread_deinit(config->thread_context);

    platform_shared_mutex_deinit(&self->lock);
    free(self->args);
    free(self->name);
    free(self);

    atomic_fetch_add(&cache->threads_deinit, 1);
    platform_futex_wake_all(&cache->threads_deinit);
}

Thread_Cache_Thread* thread_cache_create_thread(Thread_Cache* cache, isize stack_size_or_negative)
{
    _THREAD_CACHE_USE_ATOMICS;
    Thread_Cache_Thread* thread = (Thread_Cache_Thread*) calloc(1, sizeof(Thread_Cache_Thread));
    TEST(thread, "out of memory");

    thread->launch_id_and_state = THREAD_CACHE_STARTING;
    thread->args_capacity = 256;
    thread->args = calloc(thread->args_capacity, 1);
    thread->name_capacity = 256;
    thread->name = (char*) calloc(thread->name_capacity, 1);
    thread->cache = cache;
    thread->created_from = thread_cache_self();

    //launch thread
    if(platform_thread_launch(stack_size_or_negative, _thread_cache_run_func, thread, 
        "Thread_Cache name:%s thread %i", cache->name ? cache->name : "[empty]", (int) cache->threads_init + 1) != 0)
        PANIC("Thread_Cache: failed to make os thread");
            
    //push to atomic Treiber stack
    for(;;) {
        Thread_Cache_Thread* first = atomic_load(&cache->threads);
        thread->next = first;
        if(atomic_compare_exchange_weak(&cache->threads, &first, thread))
            break;
    }
    atomic_fetch_add_explicit(&cache->threads_init, 1, memory_order_relaxed);
    return thread;
}

Thread_Cache_Thread* thread_cache_lunch_thread(Thread_Cache* cache, isize stack_size_or_negative, void (*func)(void* context), const void* args, isize args_size, const char* thread_name_fmt, ...)
{
    _THREAD_CACHE_USE_ATOMICS;

    if(atomic_load(&cache->is_closed) && thread_cache_self() == NULL)
        PANIC("Thread_Cache: thread_cache_lunch_thread after deinit from outside thread");

    //Attempt to find an idle thread. Go through all threads and simply CAS to try take one.
    //If something changed between the start of the search and end we restart.
    Thread_Cache_Thread* thread = NULL;
    for(uint32_t reps = 0; reps < 10; reps++) {
        Thread_Cache_Thread* first = atomic_load(&cache->threads);
        uint64_t finished = atomic_load(&cache->threads_finished);
        
        //if is idle and matches the 
        for(Thread_Cache_Thread* curr = first; curr; curr = curr->next) {
            uint64_t launch_id_and_state = atomic_load(&curr->launch_id_and_state);
            uint64_t state = launch_id_and_state & 0xFF;
            uint64_t launch_id = launch_id_and_state >> 8;

            if(state == THREAD_CACHE_IDLE && curr->stack_size >= stack_size_or_negative) {
                //try to go to starting stage
                if(atomic_compare_exchange_strong(&curr->launch_id_and_state, &launch_id_and_state, (launch_id << 8) | THREAD_CACHE_STARTING)) {
                    thread = curr;
                    goto outer_loop_end;
                }
            }
        }
        
        //if nothing changed exit
        atomic_thread_fence(memory_order_seq_cst);
        Thread_Cache_Thread* first_after = atomic_load(&cache->threads);
        uint64_t finished_after = atomic_load(&cache->threads_finished);

        if(finished == finished_after && first == first_after)
            break;
    }
    outer_loop_end:

    //if didnt find created one
    if(thread == NULL)
        thread = thread_cache_create_thread(cache, stack_size_or_negative);

    platform_shared_mutex_unique_lock(&thread->lock);
        thread->func = func;

        //copy over the argument data
        if(thread->args_capacity < args_size) {
            isize new_cap = thread->args_capacity;
            while(new_cap < args_size)
                new_cap *= 2;
            thread->args = realloc(thread->args, new_cap);
            thread->args_capacity = new_cap;
            TEST(thread->args, "out of memory");
        }
        memcpy(thread->args, args, args_size);
    
        //copy over the name
        if(thread_name_fmt) {
            va_list name_args;
            va_start(name_args, thread_name_fmt);
            vsnprintf(thread->name, sizeof thread->name, thread_name_fmt, name_args);
            va_end(name_args);
        }
        else {
            memset(thread->name, 0, sizeof thread->name);
        }
    platform_shared_mutex_unique_unlock(&thread->lock);

    atomic_store(&thread->launch_id_and_state, THREAD_CACHE_RUNNING);
    platform_futex_wake_all(&thread->launch_id_and_state);
    
    atomic_fetch_add(&cache->threads_started, 1);
    return thread;
}

Thread_Cache_Thread* thread_cache_self()
{
    return t_thread_cache_thread;
}
const char* thread_cache_self_name()
{
    if(t_thread_cache_thread)
        return t_thread_cache_thread->name;
    else
        return NULL;
}

void thread_cache_init(Thread_Cache* cache, const char* debug_name, const Thread_Cache_Config* config_or_null)
{
    thread_cache_deinit(cache);
    cache->name = debug_name;
    if(config_or_null)
        cache->config = *config_or_null;
}

void thread_cache_deinit(Thread_Cache* cache)
{
    _THREAD_CACHE_USE_ATOMICS;

    uint64_t started = atomic_load(&cache->threads_started);
    uint64_t finished = atomic_load(&cache->threads_started);
    TEST(started == finished, "there are still %i threads running!", (int) (finished - started));

    //set all closed
    cache->is_closed = true;
    for(Thread_Cache_Thread* curr = atomic_load(&cache->threads); curr; curr = curr->next) {
        atomic_fetch_or(&curr->launch_id_and_state, THREAD_CACHE_CLOSED);
        platform_futex_wake_all(&curr->launch_id_and_state);
    }

    //wait for all threads to exit
    for(;;) {
        uint32_t thread_init = atomic_load(&cache->threads_init);
        uint32_t threads_deinit = atomic_load(&cache->threads_deinit);
        if(thread_init == threads_deinit)
            break;

        platform_futex_wait(&cache->threads_deinit, threads_deinit, -1);
    }

    memset(cache, 0, sizeof *cache);
}
