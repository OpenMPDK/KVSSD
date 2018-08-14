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

//==========================================================
// Includes.
//

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include <citrusleaf/cf_atomic.h>

#ifdef __cplusplus
extern "C" {
#endif


//==========================================================
// Typedefs & constants.
//

// Return codes.
#define CF_RCHASH_ERR_FOUND -4
#define CF_RCHASH_ERR_NOT_FOUND -3
#define CF_RCHASH_ERR -1
#define CF_RCHASH_OK 0
#define CF_RCHASH_REDUCE_DELETE 1

// Bit-values for 'flags' parameter.
#define CF_RCHASH_BIG_LOCK  0x01 // thread-safe with single big lock
#define CF_RCHASH_MANY_LOCK 0x02 // thread-safe with lock per bucket

// User must provide the hash function at create time.
typedef uint32_t (*cf_rchash_hash_fn)(const void *key, uint32_t key_size);

// The "reduce" function called for every element. Returned value governs
// behavior during reduce as follows:
// - CF_RCHASH_OK - continue iterating
// - CF_RCHASH_REDUCE_DELETE - delete the current element, continue iterating
// - anything else (e.g. CF_RCHASH_ERR) - stop iterating and return reduce_fn's
//   returned value
typedef int (*cf_rchash_reduce_fn)(const void *key, uint32_t key_size, void *object, void *udata);

// User may provide an object "destructor" at create time. The destructor is
// called - and the deleted element's object freed - from cf_rchash_delete(),
// cf_rchash_delete_object(), or cf_rchash_reduce(), if the ref-count hits 0.
// The destructor should not free the object itself - that is always done after
// releasing the object if its ref-count hits 0. The destructor should only
// clean up the object's "internals".
typedef void (*cf_rchash_destructor_fn)(void *object);

// Private data.
typedef struct cf_rchash_s {
	cf_rchash_hash_fn h_fn;
	cf_rchash_destructor_fn d_fn;
	uint32_t key_size; // if key_size == 0, use variable size functions
	uint32_t n_buckets;
	uint32_t flags;
	cf_atomic32 n_elements;
	void *table;
	pthread_mutex_t *bucket_locks;
	pthread_mutex_t big_lock;
} cf_rchash;


//==========================================================
// Public API - useful hash functions.
//

uint32_t cf_rchash_fn_u32(const void *key, uint32_t key_size);
uint32_t cf_rchash_fn_fnv32(const void *key, uint32_t key_size);
uint32_t cf_rchash_fn_zstr(const void *key, uint32_t key_size);


//==========================================================
// Public API.
//

int cf_rchash_create(cf_rchash **h_r, cf_rchash_hash_fn h_fn, cf_rchash_destructor_fn d_fn, uint32_t key_size, uint32_t n_buckets, uint32_t flags);
void cf_rchash_destroy(cf_rchash *h);
uint32_t cf_rchash_get_size(const cf_rchash *h);

int cf_rchash_put(cf_rchash *h, const void *key, uint32_t key_size, void *object);
int cf_rchash_put_unique(cf_rchash *h, const void *key, uint32_t key_size, void *object);

int cf_rchash_get(cf_rchash *h, const void *key, uint32_t key_size, void **object_r);

int cf_rchash_delete(cf_rchash *h, const void *key, uint32_t key_size);
int cf_rchash_delete_object(cf_rchash *h, const void *key, uint32_t key_size, void *object);

int cf_rchash_reduce(cf_rchash *h, cf_rchash_reduce_fn reduce_fn, void *udata);


#ifdef __cplusplus
} // end extern "C"
#endif
