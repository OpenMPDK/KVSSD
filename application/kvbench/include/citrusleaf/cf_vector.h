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
#include <stdint.h>

#include <citrusleaf/cf_types.h>

#ifdef __cplusplus
extern "C" {
#endif


//==========================================================
// Constants & typedefs.
//

// Deprecated - use cf_vector_element_size().
#define VECTOR_ELEM_SZ(_v) ( _v->ele_sz )

// Public flags.
#define VECTOR_FLAG_BIGLOCK		0x01
#define VECTOR_FLAG_INITZERO	0x02 // vector elements start cleared to 0

// Return this to delete element during reduce.
#define VECTOR_REDUCE_DELETE 1

// Private data.
typedef struct cf_vector_s {
	uint8_t *vector;
	uint32_t ele_sz;
	uint32_t capacity; // number of elements currently allocated
	uint32_t count; // number of elements in table, largest element set
	uint32_t flags;
    pthread_mutex_t LOCK; // mutable
} cf_vector;


//==========================================================
// Public API.
//

cf_vector *cf_vector_create(uint32_t ele_sz, uint32_t capacity, uint32_t flags);
int cf_vector_init(cf_vector *v, uint32_t ele_sz, uint32_t capacity, uint32_t flags);
void cf_vector_init_with_buf(cf_vector *v, uint32_t ele_sz, uint32_t capacity, uint8_t *buf, uint32_t flags);

// Deprecated - use cf_vector_init_with_buf().
void cf_vector_init_smalloc(cf_vector *v, uint32_t ele_sz, uint8_t *sbuf, uint32_t sbuf_sz, uint32_t flags);

int cf_vector_get(const cf_vector *v, uint32_t idx, void *val);
bool cf_vector_get_sized(const cf_vector *v, uint32_t idx, void *val, uint32_t sz);
int cf_vector_set(cf_vector *v, uint32_t idx, const void *val);

void *cf_vector_getp(cf_vector *v, uint32_t val);
void *cf_vector_getp_vlock(cf_vector *v, uint32_t val, pthread_mutex_t **vlock);

int cf_vector_append(cf_vector *v, const void *val);
// Adds a an element to the end, only if it doesn't exist already. O(N).
int cf_vector_append_unique(cf_vector *v, const void *val);
int cf_vector_pop(cf_vector *v, void *val);

int cf_vector_delete(cf_vector *v, uint32_t val);
// Inclusive-exclusive, e.g. start 0 & end 3 removes indexes 0,1,2.
int cf_vector_delete_range(cf_vector *v, uint32_t start, uint32_t end);
void cf_vector_clear(cf_vector *v);

// Realloc to minimal space needed.
void cf_vector_compact(cf_vector *v);

void cf_vector_destroy(cf_vector *v);

static inline uint32_t
cf_vector_size(const cf_vector *v)
{
	return v->count;
}

static inline uint32_t
cf_vector_element_size(const cf_vector *v)
{
	return v->ele_sz;
}

#define cf_vector_inita(_v, _ele_sz, _ele_cnt, _flags) \
		cf_vector_init_with_buf(_v, _ele_sz, _ele_cnt, alloca((_ele_sz) * (_ele_cnt)), _flags);

#define cf_vector_define(_v, _ele_sz, _ele_cnt, _flags) \
		cf_vector _v; \
		uint8_t _v ## __mem[(_ele_sz) * (_ele_cnt)]; \
		cf_vector_init_with_buf(&_v, _ele_sz, _ele_cnt, _v ## __mem, _flags);

//----------------------------------------------------------
// Deprecated wrappers for helpers for a vector of pointers.
//

// Deprecated - use cf_vector_create().
static inline cf_vector *
cf_vector_pointer_create(uint32_t capacity, uint32_t flags)
{
	return cf_vector_create(sizeof(void *), capacity, flags);
}

// Deprecated - use cf_vector_init(v, sizeof(void *), ...).
static inline int
cf_vector_pointer_init(cf_vector *v, uint32_t capacity, uint32_t flags)
{
	return cf_vector_init(v, sizeof(void *), capacity, flags);
}

// Deprecated - use cf_vector_set_ptr().
static inline int
cf_vector_pointer_set(cf_vector *v, uint32_t idx, const void *val)
{
	return cf_vector_set(v, idx, &val);
}

// Deprecated - use cf_vector_get_ptr().
static inline void *
cf_vector_pointer_get(const cf_vector *v, uint32_t idx)
{
	void *p;

	if (! cf_vector_get_sized(v, idx, &p, sizeof(void *))) {
		return NULL;
	}

	return p;
}

// Deprecated - use cf_vector_append_ptr().
static inline int
cf_vector_pointer_append(cf_vector *v, const void *val)
{
	return cf_vector_append(v, &val);
}

//----------------------------------------------------------
// Helpers for a vector of pointers.
//

static inline int
cf_vector_set_ptr(cf_vector *v, uint32_t idx, const void *val)
{
	return cf_vector_set(v, idx, &val);
}

static inline void *
cf_vector_get_ptr(const cf_vector *v, uint32_t idx)
{
	void *p = NULL;

	cf_vector_get_sized(v, idx, &p, sizeof(void *));

	return p;
}

static inline int
cf_vector_append_ptr(cf_vector *v, const void *val)
{
	return cf_vector_append(v, &val);
}

//----------------------------------------------------------
// Helpers for a vector of uint32_t's.
//

static inline int
cf_vector_set_uint32(cf_vector *v, uint32_t idx, uint32_t val)
{
	return cf_vector_set(v, idx, &val);
}

static inline uint32_t
cf_vector_get_uint32(const cf_vector *v, uint32_t idx)
{
	uint32_t val = 0;

	cf_vector_get_sized(v, idx, &val, sizeof(uint32_t));

	return val;
}

static inline int
cf_vector_append_uint32(cf_vector *v, uint32_t val)
{
	return cf_vector_append(v, &val);
}


#ifdef __cplusplus
} // end extern "C"
#endif
