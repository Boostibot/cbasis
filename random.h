#ifndef MODULE_RANDOM
#define MODULE_RANDOM

#include <stdint.h>
#include <stdbool.h>

#ifndef ASSERT
    #include <assert.h>
    #define ASSERT(x) assert(x)
    #define REQUIRE(x) assert(x)
#endif

#ifndef EXTERNAL
    #define EXTERNAL
#endif

typedef int64_t isize;

//=================================================================================
// Convenience functions
//=================================================================================
// All of these functions use the generator set using random_generator_set().
// By default we use random_generator_fast() and a nondeterministic random seed value.
// The generator is init on first use to almost any function.

EXTERNAL bool     random_bool(); //random bool
EXTERNAL bool     random_prob(double probability); //random bool with probability
EXTERNAL float    random_f32(); //random float in range [0, 1)
EXTERNAL double   random_f64(); //random double in range [0, 1)
EXTERNAL uint64_t random_u64(); //random u64 in range [0, U64_MAX] 
EXTERNAL int64_t  random_i64(); //random i64 in range [I64_MIN, U64_MAX] 
EXTERNAL isize    random_range(isize from, isize to); //unbiased random integer in range [from, to). If from >= to returns from
EXTERNAL int32_t  random_range_i32(int32_t from, int32_t to); //unbiased random integer in range [from, to). If from >= to returns from
EXTERNAL uint32_t random_range_u32(uint32_t from, uint32_t to); //unbiased random integer in range [from, to). If from >= to returns from
EXTERNAL uint64_t random_range_i64(uint64_t from, uint64_t to); //unbiased random integer in range [from, to). If from >= to returns from
EXTERNAL uint64_t random_range_u64(uint64_t from, uint64_t to); //unbiased random integer in range [from, to). If from >= to returns from
EXTERNAL float    random_range_f32(float from, float to); //unbiased random float in interval [from, to). If from >= to returns from
EXTERNAL double   random_range_f64(double from, double to); //unbiased random float in interval [from, to). If from >= to returns from
EXTERNAL uint64_t random_bounded(uint64_t bound); //unbiased random integer in range [0, bound). If bound == 0 returns 0

EXTERNAL uint64_t random_seed(); //Generates a nondeterministic random seed. If os crypto stuff fails falls back on some other less secure method. This function doesn't have to return any particular distribution!
EXTERNAL void     random_bytes(void* buffer, isize size); //writes size bytes of random data into buffer
EXTERNAL bool     random_bytes_crypto(void* buffer, isize size); //writes size bytes of OS crypto random data into buffer. Can fail
EXTERNAL void     random_shuffle(void* items, isize item_count, isize item_size); //Randomly shuffles the provided array

//=================================================================================
// Custom Generator
//=================================================================================
// We allow overriding of the default RNG used or using a custom one explicitly (rare).
// We also provide two abstract types that should fit most customization needs: fast and secure.
// Fast is ment to be usable for gameplay code, numerics and should offer long period with extremely fast generation,
// without any rigorous worst case guarantees. Secure should be used for networking.

typedef struct Random_Generator {
    uint64_t (*random64)(void* context);
    void* context;
} Random_Generator;

//fast pseudorandom number generator. 
//Right now we use sfc64
typedef struct Random_Fast {
    uint64_t seed;
    uint64_t state[4];
} Random_Fast;

//Secure enough pseudorandom number generator. 
//Right now we use ChaCha20
typedef struct Random_Secure {
    uint64_t seed;
    uint64_t stream;
    uint64_t counter;
    uint64_t counter;
    uint32_t blocks[16];
} Random_Secure;

EXTERNAL Random_Generator random_generator_get();
EXTERNAL Random_Generator random_generator_set(Random_Generator gen); //sets a new generator, returns the previous one.
EXTERNAL Random_Generator random_generator_set_fast(Random_Fast* gen); //sets a new generator, returns the previous one.
EXTERNAL Random_Generator random_generator_set_secure(Random_Secure* gen); //sets a new generator, returns the previous one.

EXTERNAL Random_Fast*     random_generator_fast(); //returns global fast generator
EXTERNAL Random_Fast 	  random_generator_fast_make(uint64_t seed);
EXTERNAL uint64_t 	      random_generator_fast_next(Random_Fast* fast);
EXTERNAL Random_Generator random_generator_fast_generator(Random_Fast* fast);

EXTERNAL Random_Secure*   random_generator_secure(); //returns global secure //returns global secure
EXTERNAL Random_Secure 	  random_generator_secure_make(uint64_t seed, uint64_t stream);
EXTERNAL uint64_t 	      random_generator_secure_next(Random_Secure* secure);
EXTERNAL Random_Generator random_generator_secure_generator(Random_Secure* secure);

EXTERNAL bool     random_bool_with(Random_Generator gen); 
EXTERNAL bool     random_prob_with(Random_Generator gen, double probability); 
EXTERNAL float    random_f32_with(Random_Generator gen); 
EXTERNAL double   random_f64_with(Random_Generator gen); 
EXTERNAL uint64_t random_u64_with(Random_Generator gen);  
EXTERNAL int64_t  random_i64_with(Random_Generator gen);  
EXTERNAL isize    random_range_with(Random_Generator gen, isize from, isize to);
EXTERNAL int32_t  random_range_i32_with(int32_t from, int32_t to);
EXTERNAL uint32_t random_range_u32_with(uint32_t from, uint32_t to);
EXTERNAL uint64_t random_range_i64_with(uint64_t from, uint64_t to);
EXTERNAL uint64_t random_range_u64_with(uint64_t from, uint64_t to);
EXTERNAL double   random_range_f64_with(Random_Generator gen, double from, double to); 
EXTERNAL float    random_range_f32_with(Random_Generator gen, float from, float to); 

EXTERNAL void	  random_bytes_with(Random_Generator gen, void* buffer, isize size);
EXTERNAL void     random_shuffle_with(Random_Generator gen, void* items, isize item_count, isize item_size); 

//=================================================================================
// Discrete random 
//=================================================================================
// This is mainly used to generate random enum values/indexes with a given chance 

typedef struct Discrete_Distribution{
    isize value;			    //set by user. This is what gets returned.
    isize chance;				//set by user. 
    int64_t _chance_cumulative; //set in random_discrete_make()
} Discrete_Distribution;

EXTERNAL void  random_discrete_make(Discrete_Distribution distribution[], isize distribution_size); //Fills the remaining values of Discrete_Distribution
EXTERNAL isize random_discrete_with(Random_Generator state, const Discrete_Distribution distribution[], isize distribution_size); //Samples the discrete random distribution using provided state. Returns value.
EXTERNAL isize random_discrete(const Discrete_Distribution distribution[], isize distribution_size); //Samples the discrete random distribution using global state. Returns value. 

//=================================================================================
// ChaCha
//=================================================================================
// ChaCha crypto secure encryption but we can also use it as PRNG. 
// rounds should be at least 8 but a common value is 20. 

EXTERNAL void random_chacha_generate(uint32_t output[16], uint32_t state[16], uint64_t nonce, uint64_t rounds);
EXTERNAL void random_chacha_state(uint32_t state[16], uint64_t seed, uint64_t stream);

//=================================================================================
// Small helper functions
//=================================================================================

//Seed can be any value
//Taken from: https://prng.di.unimi.it/splitmix64.c
static inline uint64_t random_splitmix(uint64_t* state) {
    uint64_t z = (*state += 0x9e3779b97f4a7c15);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
    z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
    return z ^ (z >> 31);
}

//The xoshiro256++ algorithm
//Seed must not be anywhere zero
//Taken from: https://prng.di.unimi.it/xoshiro256plusplus.c
static inline uint64_t random_xoshiro256pp(uint64_t s[4]) {
    #define ROTL(x, k) (((x) << (k)) | ((x) >> (64 - (k))))
    uint64_t out = ROTL(s[0] + s[3], 23) + s[0];
    uint64_t t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = ROTL(s[3], 45);
    return out;
}

static inline void random_xoshiro256pp_seed(uint64_t s[4], uint64_t seed) {
    s[0] = random_splitmix(&seed);
    s[1] = random_splitmix(&seed);
    s[2] = random_splitmix(&seed);
    s[3] = random_splitmix(&seed);
    for (int i = 0; i < 16; i++) 
        random_xoshiro256pp(s);
}

// SFC: Chris Doty-Humphrey's Chaotic PRNG author of PractRand 
// see here for summary: https://pracrand.sourceforge.net/RNG_engines.txt
// impl extracted from PractRand download: https://sourceforge.net/projects/pracrand/
// src\RNGs\sfc.cpp Uint64 PractRand::RNGs::Raw::sfc64::raw64()
static inline uint64_t random_sfc64(uint64_t s[4]) {
    uint64_t out = s[1] + s[2] + s[0]++;
    s[1] = s[2] ^ (s[2] >> 11);
    s[2] = s[3] + (s[3] << 3);
    s[3] = ROTL(s[3], 24) + out;
    return out;
}

static inline void random_sfc64_seed(uint64_t s[4], uint64_t seed) {
    s[0] = 1;
    s[1] = seed;
    s[2] = seed;
    s[3] = seed;
    for (int i = 0; i < 16; i++) 
        random_sfc64(s);
}

static inline double random_bits_to_f64(uint64_t random) {
    return (double) (random >> 11) * 0x1.0p-53;
}

static inline float random_bits_to_f32(uint32_t random) {
    return (float) (random >> 8) * 0x1.0p-24f;
}

#endif

#define MODULE_IMPL_ALL
#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_RANDOM)) && !defined(MODULE_HAS_IMPL_RANDOM)
    #define MODULE_HAS_IMPL_RANDOM

    #include <time.h>
    #include <stdlib.h>
    #include <string.h>

    //compiler specific
    #if defined(__GNUC__) || defined(__clang__)
        #define _RAND_THREAD_LOCAL __thread
        #define _RAND_RETURN_ADDR() __builtin_return_address(0)
        #define _RAND_NOINLINE __attribute__((noinline))
        inline static uint64_t _rand_mul128(uint64_t a, uint64_t b, uint64_t* hi) {
            __uint128_t m = (__uint128_t) a * (__uint128_t) b;
            *hi = (uint64_t) (m >> 64);
            return (uint64_t) m;
        }
    #elif defined(_MSC_VER)
        #include <intrin.h>
        #define _RAND_THREAD_LOCAL __declspec(thread)
        #define _RAND_RETURN_ADDR() _ReturnAddress()
        #define _RAND_NOINLINE       __declspec(noinline)
        #if defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
        inline static uint64_t _rand_mul128(uint64_t a, uint64_t b, uint64_t* hi) {
            return _umul128(a, b, hi);
        }
        #else
            #define _RAND_USE_FALLBACK_MUL
        #endif
    #else
        #define _RAND_THREAD_LOCAL __Thread_local
        #define _RAND_RETURN_ADDR() 0
        #define _RAND_NOINLINE
        #define _RAND_USE_FALLBACK_MUL
    #endif

    #ifdef _RAND_USE_FALLBACK_MUL
        //fallback to generic karatsuba based multiplication
        static inline uint64_t _rand_mul128(uint64_t x, uint64_t y, uint64_t* hi) {
            uint64_t x0 = (uint32_t) x;
            uint64_t x1 = x >> 32;  
            uint64_t y0 = (uint32_t) y;
            uint64_t y1 = y >> 32;

            uint64_t z0 = x0*y0;
            uint64_t z2 = x1*y1;
            uint64_t z1 = (x0 + x1)*(y0 + y1) - z0 - z2;

            uint64_t lo = z0 + (z1 << 32);
            uint64_t carry = (lo < z0);  // detect overflow
            *hi = z2 + (z1 >> 32) + carry;
            return lo;
        }
    #endif
    
    //platform specific
    #define _RAND_OS_UNKNOWN 0
    #define _RAND_OS_WIN 1
    #define _RAND_OS_LINUX 2
    #define _RAND_OS_IOS 3
    #if !defined(_RAND_OS)
        #if defined(_WIN32) || defined(_WIN64)
            #define _RAND_OS _RAND_OS_WIN // Windows
        #elif defined(__linux__)
            #define _RAND_OS _RAND_OS_LINUX // Debian, Ubuntu, Gentoo, Fedora, openSUSE, RedHat, Centos and other
        #elif defined(__APPLE__) && defined(__MACH__) // Apple OSX and iOS (Darwin)
            #define _RAND_OS _RAND_OS_IOS 
        #else
            #define _RAND_OS _RAND_OS_UNKNOWN
        #endif
    #endif 

    #if _RAND_OS == _RAND_OS_IOS 
        #include <Security/Security.h>
        EXTERNAL bool random_bytes_crypto(void* buffer, isize size) {
            REQUIRE((size > 0 && buffer) || size == 0);
            return SecRandomCopyBytes(kSecRandomDefault, (size_t) size, buffer) == 0;
        }
    #elif _RAND_OS == _RAND_OS_WIN
        #pragma comment(lib, "bcrypt.lib")
        EXTERNAL bool random_bytes_crypto(void* buffer, isize size) {
            REQUIRE((size > 0 && buffer) || size == 0);
            typedef long NTSTATUS;
            typedef unsigned char* PUCHAR;
            typedef unsigned long ULONG;
            typedef void* BCRYPT_ALG_HANDLE;
            #define BCRYPT_USE_SYSTEM_PREFERRED_RNG 2
            NTSTATUS __stdcall BCryptGenRandom(BCRYPT_ALG_HANDLE hAlgorithm, PUCHAR pbBuffer, ULONG cbBuffer, ULONG dwFlags);

            for(isize i = 0; i < size; i += UINT32_MAX) {
                isize toread = i + UINT32_MAX > size ? size - i : MAX;  
                if(BCryptGenRandom(NULL, (PUCHAR) buffer + i, (ULONG) toread, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
                    return false;
            }
            return true;
        }
    #elif _RAND_OS == _RAND_OS_LINUX
        #include <unistd.h>
        EXTERNAL bool random_bytes_crypto(void* buffer, isize size) {
            REQUIRE((size > 0 && buffer) || size == 0);
            enum {MAX = 256};
            for(isize i = 0; i < size; i += MAX) {
                isize toread = i + MAX > size ? size - i : MAX;  
                if(getentropy(buffer + i, (size_t) toread) != 0)
                    return false;
            }
        }
    #else
        EXTERNAL bool random_bytes_crypto(void* buffer, isize size) {
            return false;
        }
        #error "unrecognized platform"
    #endif

    //hacky
    _RAND_NOINLINE static uint64_t _rand_hash(uint64_t z) {
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
        z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
        return z ^ (z >> 31);
    }

    EXTERNAL uint64_t random_seed() {
        uint64_t out = 0;
        if(random_bytes_crypto(&out, sizeof out))
            return out;

        //crypto failed. We try to get a hopefully random number from someplace else.
        // We use current time, incrementing integer, pointer to a thread local, 
        // pointer to a stack frame to obtain something quite random.
        #if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
            struct timespec ts;
            timespec_get(&ts, TIME_UTC);
            uint64_t now = (uint64_t) ts.tv_sec*1000000000 + ts.tv_nsec;
        #else
            uint64_t now = clock();
        #endif
        static _RAND_THREAD_LOCAL uint64_t counter = 0;
        uint64_t instant_id = _rand_hash(now + counter);
        uint64_t thread_id = _rand_hash((uint64_t) &counter);
        uint64_t stack_id = _rand_hash((uint64_t) &out);
        uint64_t return_id = _rand_hash((uint64_t) _RAND_RETURN_ADDR());
        out = instant_id ^ thread_id ^ stack_id ^ return_id;
        counter += 1;
        return out;
    }

    EXTERNAL float random_f32_with(Random_Generator gen) {
        uint64_t random = gen.random64(gen.context);
        return random_bits_to_f32((uint32_t) ((random >> 32) ^ random));
    }
    EXTERNAL double random_f64_with(Random_Generator gen) {
        return random_bits_to_f64(gen.random64(gen.context));
    }
    EXTERNAL bool random_bool_with(Random_Generator gen) {
        return (int64_t) gen.random64(gen.context) < 0;
    }
    EXTERNAL int64_t random_i64_with(Random_Generator gen) {
        return (int64_t) gen.random64(gen.context);
    }
    EXTERNAL uint64_t random_u64_with(Random_Generator gen) {
        return (uint64_t) gen.random64(gen.context);
    }
    EXTERNAL int32_t random_i32_with(Random_Generator gen) {
        return (int32_t) gen.random64(gen.context);
    }
    EXTERNAL uint32_t random_u32_with(Random_Generator gen) {
        return (uint32_t) gen.random64(gen.context);
    }
    EXTERNAL bool random_prob_with(Random_Generator gen, double prob) {
        return random_f64_with(gen) < prob;
    }

    //Daniel Lemire's nearly-divisionless unbiased bounded random numbers.
    // blog: https://lemire.me/blog/2016/06/27/a-fast-alternative-to-the-modulo-reduction
    // blog: https://lemire.me/blog/2016/06/30/fast-random-shuffling
    // paper: https://arxiv.org/pdf/1805.10941
    static inline uint64_t _random_bounded_with(Random_Generator gen, uint64_t range) {
        uint64_t x = gen.random64(gen.context);
        uint64_t h, l = _rand_mul128(x, range, &h);
        if (l < range) {
            uint64_t t = (uint64_t) (-(int64_t)range) % range;
            while (l < t) {
                x = gen.random64(gen.context);
                l = _rand_mul128(x, range, &h);
            }
        }
        return h;
    }

    EXTERNAL uint64_t random_bounded_with(Random_Generator gen, uint64_t range) {
        return _random_bounded_with(gen, range);
    }

    //This is to make clang a bit more sane and not generate a ton of duplicate assembly
    _RAND_NOINLINE static uint64_t _random_range_with(Random_Generator gen, uint64_t from, uint64_t to) {
        return _random_bounded_with(gen, to - from) + from;
    }
    EXTERNAL isize    random_range_with(Random_Generator gen, isize from, isize to)			    { return from <= to ? (isize)    _random_range_with(gen, from, to) : from; }
    EXTERNAL int32_t  random_range_i32_with(Random_Generator gen, int32_t from, int32_t to) 	{ return from <= to ? (int32_t)  _random_range_with(gen, from, to) : from; }
    EXTERNAL uint32_t random_range_u32_with(Random_Generator gen, uint32_t from, uint32_t to) 	{ return from <= to ? (uint32_t) _random_range_with(gen, from, to) : from; }
    EXTERNAL int64_t  random_range_i64_with(Random_Generator gen, int64_t from, int64_t to) 	{ return from <= to ? (int64_t)  _random_range_with(gen, from, to) : from; }
    EXTERNAL uint64_t random_range_u64_with(Random_Generator gen, uint64_t from, uint64_t to) 	{ return from <= to ? (uint64_t) _random_range_with(gen, from, to) : from; }
    EXTERNAL double   random_range_f64_with(Random_Generator gen, double from, double to)       { return from <= to ? random_f64_with(gen)*(to - from) + from : from; }
    EXTERNAL float    random_range_f32_with(Random_Generator gen, float from, float to)         { return from <= to ? random_f32_with(gen)*(to - from) + from : from; }

    _RAND_NOINLINE
    EXTERNAL void random_shuffle_with(Random_Generator gen, void* items, isize item_count, isize item_size) {
        REQUIRE(item_count >= 0 && item_size >= 0);
        REQUIRE(items != NULL || item_count == 0 || item_size == 0);

        enum {TEMP = 64};
        uint8_t temp[TEMP]; (void) temp;
        #define SWAP_FOR_FIXED_SIZE(SIZE) \
            for (isize i = 0; i < item_count - 1; i++) { \
                isize offset = (isize) random_bounded_with(gen, (uint64_t) (item_count - i)); \
                isize j = offset + i; \
                memcpy(temp,                      (uint8_t*) items + i*SIZE, SIZE); \
                memcpy((uint8_t*) items + i*SIZE, (uint8_t*) items + j*SIZE, SIZE); \
                memcpy((uint8_t*) items + j*SIZE, temp, SIZE); \
            } (void) 0 \
        
        //special case for some common sizes
        switch(item_size) {
            case 1: SWAP_FOR_FIXED_SIZE(1); return;
            case 2: SWAP_FOR_FIXED_SIZE(2); return;
            case 4: SWAP_FOR_FIXED_SIZE(4); return;
            case 8: SWAP_FOR_FIXED_SIZE(8); return;
            case 12: SWAP_FOR_FIXED_SIZE(12); return;
            case 16: SWAP_FOR_FIXED_SIZE(16); return;
        }

        size_t repeats = (size_t) item_size / TEMP;
        size_t remainder = (size_t) item_size % TEMP;
        size_t exact = (size_t) item_size - remainder;
        
        //rest is handled with a generic memcpy version 
        for (isize i = 0; i < item_count - 1; i++) {
            isize offset = (isize) random_bounded_with(gen, (uint64_t) (item_count - i));
            isize j = offset +  i;

            uint8_t* a = (uint8_t*) items + i*item_size;
            uint8_t* b = (uint8_t*) items + j*item_size;
            for(isize k = 0; k < repeats; k ++) {
                memcpy(temp,       a + k*TEMP, TEMP);
                memcpy(a + k*TEMP, b + k*TEMP, TEMP);
                memcpy(b + k*TEMP, temp,       TEMP);
            }
                
            memcpy(temp,        a + exact, remainder);
            memcpy(a + exact,   b + exact, remainder);
            memcpy(b + exact,   temp,      remainder);
        }
    }

    
    _RAND_NOINLINE EXTERNAL void random_bytes_with(Random_Generator gen, void* into, isize size) {
        REQUIRE(size >= 0);
        size_t whole = (size_t) size / 8;
        size_t remainder = (size_t) size % 8;
        for(size_t i = 0; i < whole; i++) {
            size_t r = random_u64_with(gen);
            memcpy((uint8_t*) into + i*8, &r, 8);
        } 

        if(remainder) {
            size_t r = random_u64_with(gen);
            memcpy((uint8_t*) into + whole*8, &r, remainder);
        }
    }
    
    EXTERNAL void random_discrete_make(Discrete_Distribution distribution[], isize distribution_size) {
        isize _chance_cumulative = 0;
        for(isize i = 0; i < distribution_size; i++)
        {
            _chance_cumulative += distribution[i].chance;
            distribution[i]._chance_cumulative = _chance_cumulative;
        }
    }

    _RAND_NOINLINE EXTERNAL isize random_discrete_with(Random_Generator gen, const Discrete_Distribution distribution[], isize distribution_size) {
        if(distribution_size <= 0)  
            return 0;

        isize range_lo = 0;
        isize range_hi = distribution[distribution_size - 1]._chance_cumulative;
        isize random = random_range_with(gen, range_lo, range_hi);

        isize low_i = 0;
        isize count = distribution_size;

        while (count > 0) {
            isize step = count / 2;
            isize curr = low_i + step;
            if(distribution[curr]._chance_cumulative < random)
            {
                low_i = curr + 1;
                count -= step + 1;
            }
            else
                count = step;
        }
        
        ASSERT(0 <= low_i && low_i < distribution_size);
        isize value = distribution[low_i].value;
        return value;
    }

    typedef struct _Rand_Thread_State {
        Random_Generator curr;
        Random_Generator def;
        Random_PRNG prng;
        bool init;
    } _Rand_Thread_State;
    
    //We do a little cute trick here to not have to do any
    // "if !init then init" code in the common random_range_u64 etc. functions.
    //We simply statically init the generators to a special function that once called performs, 
    // the initializations and replaces itself with the real, now properly init generator
    static uint64_t _thread_state_init_generator(void* context);
    static _RAND_THREAD_LOCAL _Rand_Thread_State _rand_state = {_thread_state_init_generator};

    _RAND_NOINLINE static _Rand_Thread_State* _rand_thread_state() {
        _Rand_Thread_State* state = &_rand_state;
        if(state->init == false) {
            state->prng = random_generator_prng(random_seed());
            state->def = random_generator_from_prng(&state->prng);
            state->curr = state->def;
            state->init = true;
        }
        return state;
    }
    static uint64_t _thread_state_init_generator(void* context) {
        _Rand_Thread_State* state = _rand_thread_state();
        return state->curr.random64(state->curr.context);
    }    
    static inline uint64_t _rand_crypto_rand64(void* context) {
        uint64_t out = 0;
        (void) context;
        if(random_bytes_crypto(&out, sizeof out) == false) 
            abort();
        return out;
    }
    EXTERNAL Random_Generator random_generator_set(Random_Generator gen) {
        Random_Generator* curr = &_rand_thread_state()->curr;
        Random_Generator prev = *curr;
        *curr = gen;
        return prev;
    }
    EXTERNAL Random_Generator random_generator_current() { 
        return _rand_thread_state()->curr; 
    }
    EXTERNAL Random_Generator random_generator_default() { 
        return _rand_thread_state()->def; 
    }
    EXTERNAL Random_Generator random_generator_crypto() {
        Random_Generator out = {_rand_crypto_rand64, 0};
        return out;
    }
    _RAND_NOINLINE EXTERNAL Random_PRNG random_generator_prng(uint64_t seed) {
        Random_PRNG out; (void) out;
        random_sfc64_seed(out.state, seed);
        return out;
    }
    EXTERNAL Random_Generator random_generator_from_prng(Random_PRNG* prng) {
        typedef uint64_t (*Random64)(void* context);
        Random_Generator out = {(Random64) (void*) random_sfc64, prng->state};
        return out;
    }
    EXTERNAL Random_Generator random_generator_set_prng(Random_PRNG* prng) {
        Random_Generator gen = random_generator_from_prng(prng);
        return random_generator_set(gen);
    }

    _RAND_NOINLINE static uint64_t _random_range(uint64_t from, uint64_t to) {
        return _random_bounded_with(_rand_state.curr, to - from) + from;
    }
    EXTERNAL bool     random_prob(double prob)						{ return random_prob_with(_rand_state.curr, prob); } 
    EXTERNAL bool     random_bool()									{ return random_bool_with(_rand_state.curr); } 
    EXTERNAL float    random_f32() 									{ return random_f32_with(_rand_state.curr); } 
    EXTERNAL double   random_f64() 									{ return random_f64_with(_rand_state.curr); } 
    EXTERNAL uint64_t random_u64() 									{ return random_u64_with(_rand_state.curr); } 
    EXTERNAL int64_t  random_i64() 									{ return random_i64_with(_rand_state.curr); }
    EXTERNAL isize    random_range(isize from, isize to)			{ return from <= to ? (isize)    _random_range(from, to) : from; }
    EXTERNAL int32_t  random_range_i32(int32_t from, int32_t to) 	{ return from <= to ? (int32_t)  _random_range(from, to) : from; }
    EXTERNAL uint32_t random_range_u32(uint32_t from, uint32_t to) 	{ return from <= to ? (uint32_t) _random_range(from, to) : from; }
    EXTERNAL int64_t  random_range_i64(int64_t from, int64_t to) 	{ return from <= to ? (int64_t)  _random_range(from, to) : from; }
    EXTERNAL uint64_t random_range_u64(uint64_t from, uint64_t to) 	{ return from <= to ? (uint64_t) _random_range(from, to) : from; }
    EXTERNAL float    random_range_f32(float from, float to) 		{ return random_range_f32_with(_rand_state.curr, from, to); }
    EXTERNAL double   random_range_f64(double from, double to) 		{ return random_range_f64_with(_rand_state.curr, from, to); }
    EXTERNAL uint64_t random_bounded(uint64_t bound)				{ return random_bounded_with(_rand_state.curr, bound); }

    EXTERNAL void random_bytes(void* buffer, isize size) { 
        return random_bytes_with(_rand_state.curr, buffer, size); 
    }
    EXTERNAL void random_shuffle(void* items, isize item_count, isize item_size) {
        return random_shuffle_with(_rand_state.curr, items, item_count, item_size);
    }
    EXTERNAL int64_t random_discrete(const Discrete_Distribution distribution[], int64_t distribution_size) {
        return random_discrete_with(_rand_state.curr, distribution, distribution_size);
    }
#endif
