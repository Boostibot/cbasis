#ifndef MODULE_THREAD_CACHE
#define MODULE_THREAD_CACHE

#include "../assert.h"
#include "../platform.h"
#include "../defines.h"
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

typedef int64_t isize; 

typedef struct Thread_Cache_Config {
    isize min_stack_space_or_negative;
    void (*thread_init)(void* context);
    void (*thread_deinit)(void* context);
    void (*thread_before_func)(void* context);
    void (*thread_after_func)(void* context);
    void* thread_context;
} Thread_Cache_Config;

typedef struct Thread_Cache_Thread {
    //constant for entire lifetime
    Thread_Cache_Thread* next;
    Thread_Cache_Thread* created_from;
    uint64_t stack_size;
    bool is_main;

    //changes with every new launch   
    PLATFORM_ATOMIC(uint64_t) launch_id_and_state; 

    //protected by lock
    Platform_Shared_Mutex lock;
        void (*func)(void* context);
        void* args;
        isize args_capacity;
        isize args_size;

        char* name;
        isize name_size;
        isize name_capacity;
    
        PLATFORM_ATOMIC(isize) time_started_us;
        PLATFORM_ATOMIC(isize) time_finished_us;
} Thread_Cache_Thread;

EXTERNAL Thread_Cache_Thread* thread_cache_init(const Thread_Cache_Config* config_or_null, const char* main_thread_name_fmt, ...);
EXTERNAL void                 thread_cache_deinit();
EXTERNAL Thread_Cache_Thread* thread_cache_launch(isize min_stack_size, void (*func)(void* args), const void* args, isize args_size, const char* thread_name_fmt, ...);
EXTERNAL Thread_Cache_Thread* thread_cache_get_all();
EXTERNAL Thread_Cache_Thread* thread_cache_create(isize min_stack_size);
EXTERNAL Thread_Cache_Thread* thread_cache_self();
EXTERNAL const char*          thread_cache_self_name();

#endif

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_THREAD_CACHE_IMPL)) && !defined(MODULE_THREAD_CACHE_HAS_IMPL)
#define MODULE_THREAD_CACHE_HAS_IMPL

enum {
    _THREAD_CACHE_IDLE = 0,
    _THREAD_CACHE_STARTING,
    _THREAD_CACHE_RUNNING,
    _THREAD_CACHE_CLOSED,
};

typedef struct Thread_Cache {
    PLATFORM_ATOMIC(Thread_Cache_Thread*) threads;
    PLATFORM_ATOMIC(uint64_t) threads_started;
    PLATFORM_ATOMIC(uint64_t) threads_finished;
    PLATFORM_ATOMIC(uint32_t) threads_init;
    PLATFORM_ATOMIC(uint32_t) threads_deinit;
    PLATFORM_ATOMIC(uint32_t) is_closed;

    Thread_Cache_Config config;
    Thread_Cache_Thread* main_thread;
} Thread_Cache;

INTERNAL Thread_Cache g_thread_cache = {0};
ATTRIBUTE_THREAD_LOCAL Thread_Cache_Thread* t_thread_cache_thread = NULL; 

INTERNAL void _thread_cache_run_func(void* context)
{
    PLATFORM_USE_ATOMICS;
    Thread_Cache_Thread* self = (Thread_Cache_Thread*) context;
    t_thread_cache_thread = self;
    
    Thread_Cache* cache = &g_thread_cache;
    Thread_Cache_Config* config = &cache->config;
    if(config->thread_init) 
        config->thread_init(config->thread_context);
    
    for(;;) {
        uint64_t launch_id_and_state = atomic_load_explicit(&self->launch_id_and_state, memory_order_acquire);
        uint64_t state = launch_id_and_state & 0xFF;
        uint64_t launch_id = launch_id_and_state >> 8;

        if(state == _THREAD_CACHE_CLOSED)
            break;

        if(state != _THREAD_CACHE_RUNNING)
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

            atomic_store_explicit(&self->time_started_us, platform_epoch_time(), memory_order_relaxed);
            atomic_store_explicit(&self->launch_id_and_state, (launch_id + 1) << 8 | _THREAD_CACHE_IDLE, memory_order_relaxed);
            atomic_fetch_add(&cache->threads_finished, 1);
        }
    }

    if(config->thread_deinit) 
        config->thread_deinit(config->thread_context);

    atomic_fetch_add(&cache->threads_deinit, 1);
    platform_futex_wake_all(&cache->threads_deinit);
}

INTERNAL Thread_Cache_Thread* _thread_cache_create(Thread_Cache* cache, isize stack_size_or_negative, bool launch_thread)
{
    PLATFORM_USE_ATOMICS;
    Thread_Cache_Thread* thread = (Thread_Cache_Thread*) calloc(1, sizeof(Thread_Cache_Thread));
    TEST(thread, "out of memory");

    thread->launch_id_and_state = _THREAD_CACHE_STARTING;
    thread->args_capacity = 256;
    thread->args = calloc(thread->args_capacity, 1);
    thread->name_capacity = 256;
    thread->name = (char*) calloc(thread->name_capacity, 1);
    thread->created_from = thread_cache_self();

    //launch thread
    if(launch_thread)
        if(platform_thread_launch(stack_size_or_negative, _thread_cache_run_func, thread, "Thread_Cache thread %i", (int) cache->threads_init + 1) != 0)
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

INTERNAL void _thread_cache_set_name(Thread_Cache_Thread* thread, const char* thread_name_fmt, va_list args) 
{
    ASSERT(thread->name_capacity > 0 && thread->name);
    ASSERT(thread->name_size < thread->name_capacity);

    int count = 0;
    if(thread_name_fmt != NULL) {
        va_list copy;
        va_copy(copy, args);
    
        count = vsnprintf(thread->name, thread->name_capacity, thread_name_fmt, args);
        if(count >= thread->name_capacity) {
            while(thread->name_capacity < count + 1)
                thread->name_capacity *= 2;

            thread->name = (char*) realloc(thread->name, thread->name_capacity);
            count = vsnprintf(thread->name, thread->name_capacity, thread_name_fmt, copy);
        }
    }
    
    ASSERT(thread->name_capacity > 0 && thread->name);
    ASSERT(thread->name_size < thread->name_capacity);
    thread->name_size = count;
    thread->name[thread->name_size] = '\0';
}

EXTERNAL Thread_Cache_Thread* thread_cache_launch(isize min_stack_size, void (*func)(void* args), const void* args, isize args_size, const char* thread_name_fmt, ...)
{
    PLATFORM_USE_ATOMICS;
    Thread_Cache* cache = &g_thread_cache;

    Thread_Cache_Thread* self_thread = thread_cache_self();
    if(atomic_load(&cache->is_closed) && (self_thread == NULL || self_thread->is_main))
        PANIC("Thread_Cache: thread_cache_lunch after deinit from outside thread");

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

            if(state == _THREAD_CACHE_IDLE && curr->stack_size >= min_stack_size) {
                //try to go to starting stage
                if(atomic_compare_exchange_strong(&curr->launch_id_and_state, &launch_id_and_state, (launch_id << 8) | _THREAD_CACHE_STARTING)) {
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
        thread = _thread_cache_create(cache, min_stack_size, true);

    platform_shared_mutex_unique_lock(&thread->lock);
    {
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
        va_list name_args;
        va_start(name_args, thread_name_fmt);
        _thread_cache_set_name(thread, thread_name_fmt, name_args);
        va_end(name_args);
    }
    platform_shared_mutex_unique_unlock(&thread->lock);

    atomic_store_explicit(&thread->time_started_us, platform_epoch_time(), memory_order_relaxed);
    atomic_store(&thread->launch_id_and_state, _THREAD_CACHE_RUNNING);
    platform_futex_wake_all(&thread->launch_id_and_state);
    
    atomic_fetch_add(&cache->threads_started, 1);
    return thread;
}

EXTERNAL void thread_cache_deinit()
{
    PLATFORM_USE_ATOMICS;
    
    Thread_Cache* cache = &g_thread_cache;
    uint64_t started = atomic_load(&cache->threads_started);
    uint64_t finished = atomic_load(&cache->threads_started);
    TEST(started == finished, "there are still %i threads running!", (int) (finished - started));

    //set all closed
    cache->is_closed = true;
    for(Thread_Cache_Thread* curr = atomic_load(&cache->threads); curr; curr = curr->next) {
        atomic_fetch_or(&curr->launch_id_and_state, _THREAD_CACHE_CLOSED);
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
    
    //free all thread data
    for(Thread_Cache_Thread* curr = cache->threads; curr; ) {
        Thread_Cache_Thread* next = curr->next; 
        platform_shared_mutex_deinit(&curr->lock);
        free(curr->args);
        free(curr->name);
        free(curr);

        curr = next;
    }

    memset(cache, 0, sizeof *cache);
    t_thread_cache_thread = NULL;
}

EXTERNAL Thread_Cache_Thread* thread_cache_init(const Thread_Cache_Config* config_or_null, const char* main_thread_name_fmt, ...)
{
    thread_cache_deinit();
    Thread_Cache* cache = &g_thread_cache;
    Thread_Cache_Thread* main_thred = _thread_cache_create(cache, 0, false);
    main_thred->is_main = true;

    va_list name_args;
    va_start(name_args, main_thread_name_fmt);
    _thread_cache_set_name(main_thred, main_thread_name_fmt, name_args);
    va_end(name_args);

    cache->main_thread = main_thred;
    if(config_or_null)
        cache->config = *config_or_null;
        
    t_thread_cache_thread = main_thred;
}

EXTERNAL Thread_Cache_Thread* thread_cache_create(isize stack_size_or_negative)
{
    return _thread_cache_create(&g_thread_cache, stack_size_or_negative, true);
}

EXTERNAL Thread_Cache_Thread* thread_cache_get_all()
{
    return g_thread_cache.threads;
}

EXTERNAL Thread_Cache_Thread* thread_cache_self()
{
    return t_thread_cache_thread;
}

EXTERNAL const char* thread_cache_self_name()
{
    if(t_thread_cache_thread)
        return t_thread_cache_thread->name;
    else
        return NULL;
}

#endif