/*
 * Copyright 2008-2017 Aerospike, Inc.
 *
 * Portions may be licensed to Aerospike, Inc. under one or more contributor
 * license agreements.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy of
 * the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations under
 * the License.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 * Types
 *****************************************************************************/

/**
 *	Random seeds used in xorshift128+ algorithm: http://xorshift.di.unimi.it
 *	Not thread-safe.  Instantiate once per thread.
 */
typedef struct as_random_s {
	uint64_t seed0;
	uint64_t seed1;
	bool initialized;
} as_random;

/**
 *	Thread local random instance.  Do not access directly.
 */
extern __thread as_random as_rand;

/*******************************************************************************
 * Functions
 ******************************************************************************/

/**
 *	Initialize random instance.
 */
void
as_random_init(as_random* random);

/**
 *	Get thread local random instance.
 */
static inline as_random*
as_random_instance()
{
	as_random* random = &as_rand;
	
	if (! random->initialized) {
		as_random_init(random);
	}
	return random;
}

/**
 *	Get random unsigned 64 bit integer from given as_random instance
 *	using xorshift128+ algorithm: http://xorshift.di.unimi.it
 */
static inline uint64_t
as_random_next_uint64(as_random* random)
{
	// Use xorshift128+ algorithm.
	uint64_t s1 = random->seed0;
	const uint64_t s0 = random->seed1;
	random->seed0 = s0;
	s1 ^= s1 << 23;
	random->seed1 = (s1 ^ s0 ^ (s1 >> 18) ^ (s0 >> 5));
	return random->seed1 + s0;
}
	
/**
 *	Get random unsigned 32 bit integer from given as_random instance.
 */
static inline uint32_t
as_random_next_uint32(as_random* random)
{
	return (uint32_t)as_random_next_uint64(random);
}

/**
 *	Get random bytes of specified length from given as_random instance.
 */
void
as_random_next_bytes(as_random* random, uint8_t* bytes, uint32_t len);
	
/**
 *	Get random unsigned 64 bit integer from thread local instance.
 */
uint64_t
as_random_get_uint64();

/**
 *	Get random unsigned 32 bit integer from thread local instance.
 */
static inline uint32_t
as_random_get_uint32()
{
	return (uint32_t)as_random_get_uint64();
}

/**
 *	Get random bytes of specified length from thread local instance.
 */
static inline void
as_random_get_bytes(uint8_t* bytes, uint32_t len)
{
	as_random* random = as_random_instance();
	as_random_next_bytes(random, bytes, len);
}

#ifdef __cplusplus
} // end extern "C"
#endif
