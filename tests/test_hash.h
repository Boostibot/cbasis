#pragma once
#include "../hash.h"
#include "../array.h"
#include "../allocator_debug.h"
#include "../random.h"
#include "../time.h"
#include <string.h>

INTERNAL int u64_comp_func(const void* a_, const void* b_)
{
	u64 a = *(u64*) a_;
	u64 b = *(u64*) b_;
	return (a < b) - (a > b);
}

INTERNAL void test_hash_stress(f64 max_seconds)
{
	Debug_Allocator debug_alloc = debug_allocator_make(allocator_get_default(), DEBUG_ALLOC_LEAK_CHECK);
	{
		typedef enum {
			INIT,
			CLEAR,
			COPY,
			INSERT,
			INSERT_DUPLICIT,
			REMOVE,
			REHASH,
		} Action;

		Discrete_Distribution dist[] = {
			{INIT,				1},
			{CLEAR,				1},
			{COPY,				10},
			{INSERT,			2400},
			{INSERT_DUPLICIT,	1000},
			{REMOVE,			120},
			{REHASH,			10},
		};
		random_discrete_make(dist, ARRAY_COUNT(dist));

		enum {
			MAX_ITERS = 10*1000*1000,
			MIN_ITERS = 50, //for debugging
			
			//After each iteration generates NON_EXISTANT_KEYS_CHECKS keys not found in the truth array
			// and checks they cannot be found in the hash. These checks are not very necessary but alas we perform 
			// a few.
			NON_EXISTANT_KEYS_CHECKS = 2,
		};

		//We store everything twice to allow us to test copy operation by coping the current state1 into state2 
		// (or vice versa) and continuing working with the copied data (by swapping the structs)
		u64_Array truth_val_array = {debug_alloc.alloc};
		u64_Array truth_key_array = {debug_alloc.alloc};

		u64_Array other_truth_val_array = {debug_alloc.alloc};
		u64_Array other_truth_key_array = {debug_alloc.alloc};
		
		Hash table = {debug_alloc.alloc};
		Hash other_table = {debug_alloc.alloc};

		Array(Action) history = {debug_alloc.alloc};
		uint64_t seed = random_seed();
		*random_state() = random_state_make(seed);

		isize max_size = 0;
		isize max_capacity = 0;
		f64 start = clock_sec();
		for(isize i = 0; i < MAX_ITERS; i++)
		{
			if(clock_sec() - start >= max_seconds && i >= MIN_ITERS)
				break;

			Action action = (Action) random_discrete(dist, ARRAY_COUNT(dist));
			array_push(&history, action);

			//get allowed random value
			u64 random_val = 0;
			while(true) {
				random_val = random_u64();
				if(random_val - table.empty_value >= 2)
					break;
			}

			switch(action)
			{
				case INIT: {
					hash_deinit(&table);
					array_clear(&truth_key_array);
					array_clear(&truth_val_array);

					hash_init(&table, debug_alloc.alloc, 0);
				} break;

				case INSERT: {
					u64 val = random_val;
					u64 key = random_u64();

					array_push(&truth_key_array, key);
					array_push(&truth_val_array, val);

					isize inserted = hash_insert(&table, key, val);
					isize found = 0;
					TEST(hash_find(&table, key, &found));
					
					TEST(table.entries != NULL);
					TEST(found != -1 && inserted != -1 && "The inserted value must be findable");
				} break;

				case INSERT_DUPLICIT: {
					if(truth_key_array.count > 0)
					{
						u64 val = random_val;
						u64 key = truth_key_array.data[random_range(0, truth_key_array.count)];

						array_push(&truth_key_array, key);
						array_push(&truth_val_array, val);

						isize inserted = hash_insert(&table, key, val);
						TEST(inserted != -1);
						TEST(table.entries != NULL);
					}
				} break;

				case REMOVE: {
					if(truth_val_array.count > 0)
					{
						u64 removed_key = truth_key_array.data[random_range(0, truth_key_array.count)];
						i32 removed_truth_count = 0;
						for(isize j = 0; j < truth_key_array.count; j++)
							if(truth_key_array.data[j] == removed_key)
							{
								SWAP(&truth_key_array.data[j], array_last(truth_key_array));
								SWAP(&truth_val_array.data[j], array_last(truth_val_array));
								array_pop(&truth_key_array);
								array_pop(&truth_val_array);
								j -= 1;
								removed_truth_count += 1;
							}

						
						for(Hash_Iter it = {0}; hash_iterate(&table, removed_key, &it); );

						i32 removed_hash_count = 0;
						for(Hash_Iter it = {0}; hash_iterate(&table, removed_key, &it); removed_hash_count++)
							hash_remove(&table, it.index);

						TEST(removed_truth_count == removed_hash_count);
						TEST(hash_find(&table, removed_key, NULL) == false);
					}
				} break;

				case CLEAR: {
					hash_clear(&table);
					array_clear(&truth_key_array);
					array_clear(&truth_val_array);
				} break;

				case COPY: {
					//copy
					hash_copy_rehash(&other_table, &table, 0);
					array_assign(&other_truth_val_array, truth_val_array.data, truth_val_array.count);
					array_assign(&other_truth_key_array, truth_key_array.data, truth_key_array.count);
					
					//swap all acessors
					random_swap_any(&truth_val_array, &other_truth_val_array, sizeof truth_val_array);
					random_swap_any(&truth_key_array, &other_truth_key_array, sizeof truth_key_array);
					random_swap_any(&table, &other_table, sizeof table);
				} break;

				case REHASH: {
					hash_copy_rehash(&table, &table, 0);
				} break;
			}

			if(max_size < table.count)
				max_size = table.count;
			if(max_capacity < table.capacity)
				max_capacity = table.capacity;
					
			hash_test_consistency(&table, true);
			{
				ASSERT(truth_key_array.count == truth_val_array.count);
				TEST(truth_key_array.count == table.count);

				for(isize k = 0; k < truth_key_array.count; k++)
				{
					u64 key = truth_key_array.data[k];
					{
						u64_Array truth_found = {debug_alloc.alloc};
						u64_Array hash_found = {debug_alloc.alloc};
							
						for(isize j = 0; j < truth_key_array.count; j++)
							if(truth_key_array.data[j] == key)
								array_push(&truth_found, truth_val_array.data[j]);
								
						for(Hash_Iter it = {0}; hash_iterate(&table, key, &it); )
							array_push(&hash_found, it.entry->value);
								
						TEST(hash_found.count == truth_found.count);
						if(hash_found.count > 1)
						{
							qsort(hash_found.data, (size_t) hash_found.count, sizeof *hash_found.data, u64_comp_func);
							qsort(truth_found.data, (size_t) truth_found.count, sizeof *truth_found.data, u64_comp_func);
						}

						for(isize l = 0; l < hash_found.count; l++)
							TEST(hash_found.data[l] == truth_found.data[l]);

						array_deinit(&truth_found);
						array_deinit(&hash_found);
					}
				}
			}

			//Test integrity of some non existant keys
			for(isize k = 0; k < NON_EXISTANT_KEYS_CHECKS; k++)
			{
				u64 key = random_u64();
					
				//Only if the genrated key is unique 
				//(again extrenely statistically unlikely that it will fail truth_key_array.count/10^19 chance)
				bool key_found = false;
				for(isize j = 0; j < truth_key_array.count; j++)
					if(truth_key_array.data[j] == key)
					{
						key_found = true;
						break;
					}

				if(key_found == false)
					TEST(hash_find(&table, key, NULL) == false && "must not be found");
			}
		}

		array_deinit(&truth_key_array);
		array_deinit(&truth_val_array);
		array_deinit(&other_truth_key_array);
		array_deinit(&other_truth_val_array);
		array_deinit(&history);
		hash_deinit(&table);
		hash_deinit(&other_table);
	}
	debug_allocator_deinit(&debug_alloc);
}

INTERNAL void test_hash(f64 max_seconds)
{
	test_hash_stress(max_seconds/2);
}
