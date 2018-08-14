/*
 * Copyright 2017 Aerospike, Inc.
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

//==========================================================
// Includes.
//

#include <stddef.h>
#include <stdint.h>


//==========================================================
// Public API.
//

// 32-bit Fowler-Noll-Vo hash function (FNV-1a).
static inline uint32_t
cf_hash_fnv32(const uint8_t* buf, size_t size)
{
	uint32_t hash = 2166136261;
	const uint8_t* end = buf + size;

	while (buf < end) {
		hash ^= (uint32_t)*buf++;
		hash *= 16777619;
	}

	return hash;
}


// 64-bit Fowler-Noll-Vo hash function (FNV-1a).
static inline uint64_t
cf_hash_fnv64(const uint8_t* buf, size_t size)
{
	uint64_t hash = 0xcbf29ce484222325ULL;
	const uint8_t* end = buf + size;

	while (buf < end) {
		hash ^= (uint64_t)*buf++;
		hash *= 0x100000001b3ULL;
	}

	return hash;
}


// 32-bit Jenkins One-at-a-Time hash function.
static inline uint32_t
cf_hash_jen32(const uint8_t* buf, size_t size)
{
	uint32_t hash = 0;
	const uint8_t* end = buf + size;

	while (buf < end) {
		hash += (uint32_t)*buf++;
		hash += hash << 10;
		hash ^= hash >> 6;
	}

	hash += hash << 3;
	hash ^= hash >> 11;
	hash += hash << 15;

	return hash;
}


// 64-bit Jenkins One-at-a-Time hash function.
static inline uint64_t
cf_hash_jen64(const uint8_t* buf, size_t size)
{
	uint64_t hash = 0;
	const uint8_t* end = buf + size;

	while (buf < end) {
		hash += (uint64_t)*buf++;
		hash += hash << 10;
		hash ^= hash >> 6;
	}

	hash += hash << 3;
	hash ^= hash >> 11;
	hash += hash << 15;

	return hash;
}


// 32-bit pointer hash.
static inline uint32_t
cf_hash_ptr32(const void* p_ptr)
{
	return (uint32_t)((*(const uint64_t*)p_ptr * 0xe221f97c30e94e1dULL) >> 32);
}
