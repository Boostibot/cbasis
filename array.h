#ifndef MODULE_ARRAY
#define MODULE_ARRAY

// This freestanding file introduces a simple but powerful typed dynamic array concept.
// It works by defining struct for each type and then using type untyped macros to work
// with these structs. 
//
// This approach was chosen because: 
// 1) we need type safety! Array of int should be distinct type from Array of char
//    This disqualified the one Array struct for all types holding the type info supplied at runtime.
// 
// 2) we need to be able to work with empty arrays easily and safely. 
//    Empty arrays are the most common arrays so having them as a special and error prone case
//    is less than ideal. This disqualified the typed pointer to allocated array prefixed with header holding
//    the meta data. See how stb library implements "stretchy buffers".
//    This approach also introduces a lot of helper functions instead of simple array.count or whatever.
// 
// 3) we need to hold info about allocators used for the array. We should know how to deallocate any array using its allocator.
//
// 4) the array type must be fully explicit. There should never be the case where we return an array from a function and we dont know
//    what kind of array it is/if it even is a dynamic array. This is another issue with the stb style.
//
// This file is also fully freestanding. To compile the function definitions #define MODULE_IMPL_ALL and include it again in .c file. 

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef MODULE_ALL_COUPLED
    #include "defines.h"
    #include "assert.h"
    #include "profile.h"
    #include "allocator.h"
#endif

typedef int64_t isize;
typedef void* (*Allocator)(void* alloc, int mode, int64_t new_size, void* old_ptr, int64_t old_size, int64_t align, void* other);

typedef struct Untyped_Array {        
    Allocator* allocator;                
    uint8_t* data;                      
    isize count;                        
    isize capacity;                    
} Untyped_Array;

typedef struct Generic_Array {   
    Untyped_Array* array;
    uint32_t item_size;                        
    uint32_t item_align;                    
} Generic_Array;

#define Array_Aligned(Type, align)               \
    union {                                      \
        Untyped_Array untyped;                   \
        struct {                                 \
            Allocator* allocator;                \
            Type* data;                          \
            isize count;                         \
            isize capacity;                      \
        };                                       \
        uint8_t (*ALIGN)[align];                 \
    }                                            \

#define Array(Type) Array_Aligned(Type, __alignof(Type) > 0 ? __alignof(Type) : 8)

typedef Array(uint8_t)  u8_Array;
typedef Array(uint16_t) u16_Array;
typedef Array(uint32_t) u32_Array;
typedef Array(uint64_t) u64_Array;

typedef Array(int8_t)   i8_Array;
typedef Array(int16_t)  i16_Array;
typedef Array(int32_t)  i32_Array;
typedef Array(int64_t)  i64_Array;

typedef Array(float)    f32_Array;
typedef Array(double)   f64_Array;
typedef Array(void*)    ptr_Array;

typedef i64_Array isize_Array;
typedef u64_Array usize_Array;

#ifndef EXTERNAL
    #define EXTERNAL
#endif
EXTERNAL void generic_array_init(Generic_Array gen, Allocator* allocator);
EXTERNAL void generic_array_deinit(Generic_Array gen);
EXTERNAL void generic_array_set_capacity(Generic_Array gen, isize capacity); 
EXTERNAL bool generic_array_is_consistent(Generic_Array gen);
EXTERNAL void generic_array_resize(Generic_Array gen, isize to_size, bool zero_new);
EXTERNAL void generic_array_reserve(Generic_Array gen, isize to_capacity);
EXTERNAL void generic_array_append(Generic_Array gen, const void* data, isize data_count);

#ifdef __cplusplus
    #define array_make_generic(array_ptr) (Generic_Array{&(array_ptr)->untyped, sizeof *(array_ptr)->data, sizeof *(array_ptr)->ALIGN})
#else
    #define array_make_generic(array_ptr) ((Generic_Array){&(array_ptr)->untyped, sizeof *(array_ptr)->data, sizeof *(array_ptr)->ALIGN})
#endif 

#ifndef ASSERT
    #include <assert.h>
    #define ASSERT(x, ...)              assert(x)
    #define ASSERT_SLOW(x, ...)         assert(x)
    #define REQUIRE(x, ...)             assert(x)
    #define CHECK_BOUNDS(i, count, ...) assert(0 <= (i) && (i) <= count)
#endif  
    
//Initializes the array. If the array is already initialized deinitializes it first.
//Thus expects a properly formed array. Suppling a non-zeroed memory will cause errors!
//All data structers in this library need to be zero init to be valid!
#define array_init(array_ptr, allocator) \
    generic_array_init(array_make_generic(array_ptr), (allocator))

//Deallocates and resets the array
#define array_deinit(array_ptr) \
    generic_array_deinit(array_make_generic(array_ptr))

//If the array capacity is lower than to_capacity sets the capacity to to_capacity. 
//If setting of capacity is required and the new capcity is less then one geometric growth 
// step away from current capacity grows instead.
#define array_reserve(array_ptr, to_capacity) \
    generic_array_reserve(array_make_generic(array_ptr), (to_capacity)) 

//Sets the array size to the specied to_size. 
//If the to_size is smaller than current size simply dicards further items
//If the to_size is greater than current size zero initializes the newly added items
#define array_resize(array_ptr, to_size)              \
    generic_array_resize(array_make_generic(array_ptr), (to_size), true) 
   
//Just like array_resize except doesnt zero initialized newly added region
#define array_resize_for_overwrite(array_ptr, to_size)              \
    generic_array_resize(array_make_generic(array_ptr), (to_size), false) 

//Sets the array size to 0. Does not deallocate the array
#define array_clear(array_ptr) ((array_ptr)->count = 0)

//Appends item_count items to the end of the array growing it
#define array_append(array_ptr, items, item_count) (\
        /* Here is a little hack to typecheck the items array.*/ \
        /* We do a comparison that emmits a warning on incompatible types but doesnt get executed */ \
        (void) sizeof((array_ptr)->data == (items)), \
        generic_array_append(array_make_generic(array_ptr), (items), (item_count)) \
    ) \
    
//Discards current items in the array and replaces them with the provided items
#define array_assign(array_ptr, items, item_count) (\
        array_clear(array_ptr), \
        array_append((array_ptr), (items), (item_count)) \
    )\

//Appends a single item to the end of the array
#define array_push(array_ptr, item_value) (           \
        generic_array_reserve(array_make_generic(array_ptr), (array_ptr)->count + 1), \
        (array_ptr)->data[(array_ptr)->count++] = (item_value) \
    ) \

//Removes a single item from the end of the array
#define array_pop(array_ptr) (\
        CHECK_BOUNDS(0, (array_ptr)->count, "cannot pop from empty array!"), \
        (array_ptr)->data[--(array_ptr)->count] \
    ) \

//Removes the item at index and puts the last item in its place to fill the hole
#define array_remove_unordered(array_ptr, index) (\
        CHECK_BOUNDS(0, (array_ptr)->count, "cannot remove from empty array!"), \
        (array_ptr)->data[(index)] = (array_ptr)->data[--(array_ptr)->count] \
    ) \

//Returns the value of the last item. The array must not be empty!
#define array_last(array) (\
        CHECK_BOUNDS(0, (array).count, "cannot get last from empty array!"), \
        &(array).data[(array).count - 1] \
    ) \
    
#define array_set_capacity(array_ptr, capacity) \
    generic_array_set_capacity(array_make_generic(array_ptr), (capacity))
#endif

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_ARRAY)) && !defined(MODULE_HAS_IMPL_ARRAY)
#define MODULE_HAS_IMPL_ARRAY

#ifndef PROFILE_SCOPE
    #define PROFILE_SCOPE(...)
#endif

EXTERNAL bool generic_array_is_consistent(Generic_Array gen)
{
    bool is_capacity_correct = 0 <= gen.array->capacity;
    bool is_size_correct = (0 <= gen.array->count && gen.array->count <= gen.array->capacity);
    if(gen.array->capacity > 0)
        is_capacity_correct = is_capacity_correct && gen.array->allocator != NULL;

    bool is_data_correct = (gen.array->data == NULL) == (gen.array->capacity == 0);
    bool item_size_correct = gen.item_size > 0;
    bool alignment_correct = ((gen.item_align & (gen.item_align-1)) == 0) && gen.item_align > 0; //if is power of two and bigger than zero
    bool result = is_capacity_correct && is_size_correct && is_data_correct && item_size_correct && alignment_correct;
    ASSERT(result);
    return result;
}

EXTERNAL void generic_array_init(Generic_Array gen, Allocator* allocator)
{
    generic_array_deinit(gen);
    gen.array->allocator = allocator;
    ASSERT_SLOW(generic_array_is_consistent(gen));
}

EXTERNAL void generic_array_deinit(Generic_Array gen)
{
    ASSERT_SLOW(generic_array_is_consistent(gen));
    if(gen.array->capacity > 0)
        (*gen.array->allocator)(gen.array->allocator, 0, 0, gen.array->data, gen.array->capacity * gen.item_size, gen.item_align, NULL);
    
    memset(gen.array, 0, sizeof *gen.array);
}

EXTERNAL void generic_array_set_capacity(Generic_Array gen, isize capacity)
{
    PROFILE_SCOPE()
    {
        ASSERT(generic_array_is_consistent(gen));
        REQUIRE(capacity >= 0 && gen.array->allocator != NULL);

        isize old_byte_size = gen.item_size * gen.array->capacity;
        isize new_byte_size = gen.item_size * capacity;
        gen.array->data = (uint8_t*) (*gen.array->allocator)(gen.array->allocator, 0, new_byte_size, gen.array->data, old_byte_size, gen.item_align, NULL);

        //trim the size if too big
        gen.array->capacity = capacity;
        if(gen.array->count > gen.array->capacity)
            gen.array->count = gen.array->capacity;
        
        ASSERT_SLOW(generic_array_is_consistent(gen));
    }
}

EXTERNAL void generic_array_resize(Generic_Array gen, isize to_size, bool zero_new)
{
    generic_array_reserve(gen, to_size);
    if(zero_new && to_size > gen.array->count)
        memset(gen.array->data + gen.array->count*gen.item_size, 0, (size_t) ((to_size - gen.array->count)*gen.item_size));
        
    gen.array->count = to_size;
    ASSERT_SLOW(generic_array_is_consistent(gen));
}

EXTERNAL void generic_array_reserve(Generic_Array gen, isize to_fit)
{
    ASSERT_SLOW(generic_array_is_consistent(gen));
    if(gen.array->capacity > to_fit)
        return;
        
    isize new_capacity = to_fit;
    isize growth_step = gen.array->capacity * 3/2 + 8;
    if(new_capacity < growth_step)
        new_capacity = growth_step;

    generic_array_set_capacity(gen, new_capacity + 1);
}

EXTERNAL void generic_array_append(Generic_Array gen, const void* data, isize data_count)
{
    REQUIRE(data_count >= 0 && (data || data_count == 0));
    generic_array_reserve(gen, gen.array->count+data_count);
    memcpy(gen.array->data + gen.item_size * gen.array->count, data, (size_t) (gen.item_size * data_count));
    gen.array->count += data_count;
    ASSERT_SLOW(generic_array_is_consistent(gen));
}
#endif
