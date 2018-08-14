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

#include <citrusleaf/alloc.h>
#include <citrusleaf/cf_types.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *	TYPES
 *****************************************************************************/

/**
 *	A fast, non thread safe dynamic array implementation.
 *  as_vector is not part of the generic as_val family.
 */
typedef struct as_vector_s {
	/**
	 *	The items of the vector.
	 */
	void* list;
	
	/**
	 *	The total number items allocated.
	 */
	uint32_t capacity;
	
	/**
	 *	The number of items used.
	 */
	uint32_t size;
	
	/**
	 *	The size of a single item.
	 */
	uint32_t item_size;
	
	/**
	 *	Internal vector flags.
	 */
	uint32_t flags;
} as_vector;

/******************************************************************************
 *	MACROS
 ******************************************************************************/

/**
 *	Initialize a stack allocated as_vector, with item storage on the stack.
 *  as_vector_inita() will transfer stack memory to the heap if a resize is
 *  required.
 */
#define as_vector_inita(__vector, __item_size, __capacity)\
(__vector)->list = alloca((__capacity) * (__item_size));\
(__vector)->capacity = __capacity;\
(__vector)->item_size = __item_size;\
(__vector)->size = 0;\
(__vector)->flags = 0;

/*******************************************************************************
 *	INSTANCE FUNCTIONS
 ******************************************************************************/

/**
 *	Initialize a stack allocated as_vector, with item storage on the heap.
 */
void
as_vector_init(as_vector* vector, uint32_t item_size, uint32_t capacity);

/**
 *	Create a heap allocated as_vector, with item storage on the heap.
 */
as_vector*
as_vector_create(uint32_t item_size, uint32_t capacity);

/**
 *	Free vector.
 */
void
as_vector_destroy(as_vector* vector);

/**
 *	Empty vector without altering data.
 */
static inline void
as_vector_clear(as_vector* vector)
{
	vector->size = 0;
}

/**
 *  Get pointer to item given index.
 */
static inline void*
as_vector_get(as_vector* vector, uint32_t index)
{
	return (void *) ((uint8_t *)vector->list + (vector->item_size * index));
}

/**
 *  Get pointer to item pointer given index.
 */
static inline void*
as_vector_get_ptr(as_vector* vector, uint32_t index)
{
	return *(void**) ((uint8_t *)vector->list + (vector->item_size * index));
}

/**
 *  Double vector capacity.
 */
void
as_vector_increase_capacity(as_vector* vector);

/**
 *  Set item in vector.
 */
static inline void
as_vector_set(as_vector* vector, uint32_t index, void* value)
{
	memcpy((uint8_t *)vector->list + (index * vector->item_size), value, vector->item_size);
}

/**
 *  Append item to vector.
 */
static inline void
as_vector_append(as_vector* vector, void* value)
{
	if (vector->size >= vector->capacity) {
		as_vector_increase_capacity(vector);
	}
	memcpy((uint8_t *)vector->list + (vector->size * vector->item_size), value, vector->item_size);
	vector->size++;
}

/**
 *  Append item to vector if it doesn't already exist.
 */
bool
as_vector_append_unique(as_vector* vector, void* value);

/**
 *	Return shallow heap copy of vector.
 */
void*
as_vector_to_array(as_vector* vector, uint32_t* size);

/**
 *  Reserve a new slot in the vector.  Increase capacity if necessary.
 *	Return reference to item.  The item is initialized to zeroes.
 */
static inline void*
as_vector_reserve(as_vector* vector)
{
	if (vector->size >= vector->capacity) {
		as_vector_increase_capacity(vector);
	}
	void* item = (uint8_t *)vector->list + (vector->size * vector->item_size);
	memset(item, 0, vector->item_size);
	vector->size++;
	return item;
}

/**
 *  Remove item from vector.
 */
bool
as_vector_remove(as_vector* vector, uint32_t index);

#ifdef __cplusplus
} // end extern "C"
#endif
