#pragma once

#include "../spmc_queue.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdlib.h>

#ifndef TEST
    #define TEST(x, ...) (!(x) ? fprintf(stderr, "TEST(" #x ") failed! " __VA_ARGS__), abort() : (void) 0)
#endif

static void test_spmc_sequential(isize count, isize reserve_to)
{
    SPMC_Queue q = {0};
    spmc_queue_init(&q, sizeof(isize), -1);

    //zero pop
    isize dummy = 0;
    TEST(spmc_queue_pop(&q, &dummy, 1).success == false);

    //test zero capacity and count
    TEST(spmc_queue_capacity(&q) == 0);
    TEST(spmc_queue_count(&q) == 0);
    spmc_queue_reserve(&q, reserve_to);
    TEST(spmc_queue_capacity(&q) >= reserve_to);
    TEST(spmc_queue_count(&q) == 0);

    //still pops should fail
    TEST(spmc_queue_pop(&q, &dummy, 1).success == false);
    TEST(spmc_queue_count(&q) == 0);

    //push count
    for(isize i = 0; i < count; i++)
        TEST(spmc_queue_push_st(&q, &i, 1).success);
    
    //push one more potentially causing realloc
    dummy = 10;
    TEST(spmc_queue_push_st(&q, &dummy, 1).success);
    TEST(spmc_queue_count(&q) == count + 1);
    TEST(spmc_queue_capacity(&q) >= count + 1);

    //pop count
    for(isize i = 0; i < count; i++)
    {
        isize popped = 0;
        TEST(spmc_queue_pop(&q, &popped, 1).success);
        TEST(popped == i);
    }

    //popping it back
    TEST(spmc_queue_pop(&q, &dummy, 1).success);
    TEST(dummy == 10);
        
    //pop empty
    TEST(spmc_queue_pop(&q, &dummy, 1).success == false);
    TEST(spmc_queue_pop(&q, &dummy, 1).success == false);
    TEST(spmc_queue_count(&q) == 0);
    TEST(spmc_queue_capacity(&q) >= count + 1);

    //push some before dealloc
    isize dummies[3] = {10, 10, 10};
    TEST(spmc_queue_push_st(&q, dummies, 3).success == 3);

    spmc_queue_deinit(&q);
}

typedef struct Test_SPMC_Buffer {
    isize* data; 
    isize count;
    isize capacity;
} Test_SPMC_Buffer;

typedef struct Test_SPMC_Thread {
    SPMC_QUEUE_ATOMIC(isize)* started; 
    SPMC_QUEUE_ATOMIC(isize)* finished; 
    SPMC_QUEUE_ATOMIC(isize)* run_test; 
    SPMC_Queue* queue;

    Test_SPMC_Buffer popped;
} Test_SPMC_Thread;

//Helper functions
static int  test_spmc_isize_comp_func(const void* a, const void* b);
static void test_spmc_launch_thread(void (*func)(void*), void* context);
static void test_spmc_buffer_push(Test_SPMC_Buffer* buffer, isize* val, isize count);

static void test_spmc_producer_consumers_thread_func(void *arg)
{
    Test_SPMC_Thread* thread = (Test_SPMC_Thread*) arg;
    atomic_fetch_add(thread->started, 1);
    
    //wait to run
    while(atomic_load_explicit(thread->run_test, memory_order_seq_cst) == 0); 
    
    ////run for as long as we can
    while(*thread->run_test == 1)
    {
        isize val = 0;
        if(spmc_queue_pop(thread->queue, &val, 1).success) {
            test_spmc_buffer_push(&thread->popped, &val, 1);
        }
    }
    //while(*thread->run_test == 1); 
    //printf("run: %lli\n", *thread->run_test);

    atomic_fetch_add(thread->finished, 1);
}

static void test_spmc_producer_consumers(isize reserve_size, isize consumer_count, double time, double producer_pop_back_chance)
{
    SPMC_Queue queue = {0};
    spmc_queue_init(&queue, sizeof(isize), -1);
    spmc_queue_reserve(&queue, reserve_size);

    SPMC_QUEUE_ATOMIC(isize) started = 0;
    SPMC_QUEUE_ATOMIC(isize) finished = 0;
    SPMC_QUEUE_ATOMIC(isize) run_test = 0;
    
    //start all threads
    enum {MAX_THREADS = 64};
    Test_SPMC_Thread threads[MAX_THREADS] = {0};
    for(isize i = 0; i < consumer_count; i++)
    {
        threads[i].queue = &queue;
        threads[i].started = &started;
        threads[i].finished = &finished;
        threads[i].run_test = &run_test;

        //run the test func in separate thread in detached state
        test_spmc_launch_thread(test_spmc_producer_consumers_thread_func, &threads[i]);
    }
    
    isize produced_counter = 0;
    Test_SPMC_Thread producer = {0};

    //run test
    {
        while(started != consumer_count);
        run_test = 1;

        isize deadline = clock() + (isize)(time*CLOCKS_PER_SEC);
        while(clock() < deadline)
        {
            spmc_queue_push_st(&queue, &produced_counter, 1);
            produced_counter += 1;

            double random = (double) rand() / RAND_MAX;
            random = 1000;
            if(random < producer_pop_back_chance)
            {
                isize popped = 0;
                if(spmc_queue_pop(&queue, &popped, 1).success)
                    test_spmc_buffer_push(&producer.popped, &popped, 1);
            }
        }

        run_test = 2;
        while(finished != consumer_count);
    }

    //pop all remaining items
    {
        isize popped = 0;
        while(spmc_queue_pop(&queue, &popped, 1).success)
            test_spmc_buffer_push(&producer.popped, &popped, 1);
    }

    //Validate results
    {
        //copy all results into a single array
        Test_SPMC_Buffer buffer = {0};
        test_spmc_buffer_push(&buffer, producer.popped.data, producer.popped.count);
        
        isize popped_during_runtime = 0;
        for(isize i = 0; i < consumer_count; i++)
        {
            Test_SPMC_Buffer* curr = &threads[i].popped;
            test_spmc_buffer_push(&buffer, curr->data, curr->count);
            popped_during_runtime += curr->count; 

            //items in popped must be well ordered
            for(isize k = 1; k < curr->count; k++)
                TEST(curr->data[k - 1] < curr->data[k]);
        }

        TEST(buffer.count == produced_counter);
        
        //test if items are valid
        qsort(buffer.data, buffer.count, sizeof(isize), test_spmc_isize_comp_func);
        for(isize i = 0; i < produced_counter; i++)
            TEST(buffer.data[i] == i);

        printf("consumers:%lli pushed:%lli popped:%lli popped:%.2lf millions/s\n", consumer_count, produced_counter, popped_during_runtime, (double) popped_during_runtime/(time*1e6));
        free(buffer.data);
    }
    
    //deinit everything
    free(producer.popped.data);
    for(isize i = 0; i < consumer_count; i++)
        free(threads[i].popped.data);

    spmc_queue_deinit(&queue);
}

static void test_spmc_queue(double time)
{
    printf("test_spmc testing sequential\n");
    test_spmc_sequential(0, 0);
    test_spmc_sequential(1, 0);
    test_spmc_sequential(2, 1);
    test_spmc_sequential(10, 8);
    test_spmc_sequential(100, 100);
    test_spmc_sequential(1024, 1024);
    test_spmc_sequential(1024*1024, 1024);
    
    if(time > 0)
    {
        printf("test_spmc testing stress %.2lfs\n", time);
        enum {THREADS = 32};
        for(isize i = 1; i <= THREADS; i++) {
            test_spmc_producer_consumers(1000, i, time/THREADS, 0.1);
        }
    }
    printf("test_spmc done!\n");
}

//Helper functions IMPLS ================
static void test_spmc_buffer_push(Test_SPMC_Buffer* buffer, isize* val, isize count) 
{
    if(buffer->count + count > buffer->capacity)
    {
        isize new_capacity = buffer->capacity*3/2 + 8;
        if(new_capacity < buffer->count + count)
            new_capacity = buffer->count + count;
        buffer->data = (isize*) realloc(buffer->data, sizeof(isize) * new_capacity);
        buffer->capacity = new_capacity;
    }

    memcpy(buffer->data + buffer->count, val, count * sizeof(isize));
    buffer->count += count;
}

static void test_spmc_buffer_deinit(Test_SPMC_Buffer* buffer)
{
    free(buffer->data);
    memset(buffer, 0, sizeof *buffer);
}

static int test_spmc_isize_comp_func(const void* a, const void* b)
{
    isize x = *(const isize*) a;
    isize y = *(const isize*) b;

    return (x > y) - (x < y);
}

#ifdef __cplusplus
    #include <thread>
    static void test_spmc_launch_thread(void (*func)(void*), void* context)
    {
         std::thread(func, context).detach();
    }
#elif defined(_WIN32) || defined(_WIN64)
    #include <process.h>
    static void test_spmc_launch_thread(void (*func)(void*), void* context)
    {
        uintptr_t _beginthread(
            void( __cdecl *start_address )( void * ),
            unsigned stack_size,
            void *arglist
        );

        _beginthread(func, 0, context);
    }

#else
    #include <pthread.h>
    static void* test_spmc_launch_caster(void* func_and_context)
    {
        typedef void (*Void_Func)(void* context);

        Void_Func func = (Void_Func) ((void**) func_and_context)[0];
        void* context =              ((void**) func_and_context)[1];
        func(context);
        free(func_and_context);
        return NULL;
    }

    static void test_spmc_launch_thread(void (*func)(void* context), void* context)
    {
        void** func_and_context = (void**) malloc(sizeof(void*)*2);
        func_and_context[0] = func;
        func_and_context[1] = context;

        pthread_t handle = {0};
        pthread_attr_t attr = {0};
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        int error = pthread_create(&handle, &attr, test_spmc_launch_caster, func_and_context);
        pthread_attr_destroy(&attr);
        assert(error == 0);
    }
#endif