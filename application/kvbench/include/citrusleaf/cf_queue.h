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

#include <pthread.h>
#include <citrusleaf/cf_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 * CONSTANTS
 ******************************************************************************/

#ifndef CF_QUEUE_ALLOCSZ
#define CF_QUEUE_ALLOCSZ 64
#endif

#define CF_QUEUE_OK 0
#define CF_QUEUE_ERR -1
#define CF_QUEUE_EMPTY -2
#define CF_QUEUE_NOMATCH -3 // used in reduce_pop methods

// mswait < 0 wait forever
// mswait == 0 wait not at all
// mswait > 0 wait that number of ms
#define CF_QUEUE_FOREVER -1
#define CF_QUEUE_NOWAIT 0
#define CF_QUEUE_WAIT_1SEC 1000
#define CF_QUEUE_WAIT_30SEC 30000

/******************************************************************************
 * TYPES
 ******************************************************************************/

typedef int (*cf_queue_reduce_fn) (void *buf, void *udata);

/**
 * cf_queue
 */
typedef struct cf_queue_s {
	/**
	 * Private data - please use API.
	 */
	bool            threadsafe;     // if false, no mutex lock
	bool            free_struct;    // free struct cf_queue in addition to elements
	unsigned int    alloc_sz;       // number of elements currently allocated
	unsigned int    read_offset;    // offset (in elements) of head
	unsigned int    write_offset;   // offset (in elements) past tail
	size_t          element_sz;     // number of bytes in an element
	pthread_mutex_t LOCK;           // the mutex lock
	pthread_cond_t  CV;             // the condvar
	uint8_t *       elements;       // the block of queue elements
} cf_queue;

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/

bool cf_queue_init(cf_queue* q, size_t element_sz, uint32_t capacity, bool threadsafe);

cf_queue *cf_queue_create(size_t element_sz, bool threadsafe);

void cf_queue_destroy(cf_queue *q);

/**
 * Get the number of elements currently in the queue.
 */
int cf_queue_sz(cf_queue *q);

/**
 * Push to the tail of the queue.
 */
int cf_queue_push(cf_queue *q, const void *ptr);

/**
 * Push element on the queue only if size < limit.
 */
bool cf_queue_push_limit(cf_queue *q, const void *ptr, uint32_t limit);

/**
 * Same as cf_queue_push() except it's a no-op if element is already queued.
 */
int cf_queue_push_unique(cf_queue *q, const void *ptr);

/**
 * Push to the front of the queue.
 */
int cf_queue_push_head(cf_queue *q, const void *ptr);

/**
 * Pops from the head of the queue.
 */
int cf_queue_pop(cf_queue *q, void *buf, int ms_wait);

/**
 * Run the entire queue, calling the callback, with the lock held.
 *
 * return -2 from the callback to delete an element and stop iterating
 * return -1 from the callback to stop iterating
 * return  0 from the callback to keep iterating
 */
int cf_queue_reduce(cf_queue *q, cf_queue_reduce_fn cb, void *udata);

/**
 * Find best element to pop from the queue via a reduce callback function.
 *
 * return  0 from the callback to keep iterating
 * return -1 from the callback to pop and stop iterating
 * return -2 from the callback if the element is the best to pop so far, but you
 * want to keep looking
 */
int cf_queue_reduce_pop(cf_queue *q, void *buf, int ms_wait, cf_queue_reduce_fn cb, void *udata);

/**
 * Same as cf_queue_reduce() but run the queue from the tail.
 */
int cf_queue_reduce_reverse(cf_queue *q, cf_queue_reduce_fn cb, void *udata);

/**
 * The most common reason to want to 'reduce' is delete. If 'buf' is NULL, this
 * will delete all. If 'only_one' is true, only the first occurrence matching
 * 'buf' will be deleted. (Do this if you know there can only be one occurrence
 * on the queue.)
 */
int cf_queue_delete(cf_queue *q, const void *ptr, bool only_one);

/**
 * Delete all items in queue.
 */
int cf_queue_delete_all(cf_queue *q);

void cf_queue_delete_offset(cf_queue *q, uint32_t index);

/******************************************************************************
 * MACROS
 ******************************************************************************/

#define CF_Q_SZ(__q) (__q->write_offset - __q->read_offset)

#define CF_Q_EMPTY(__q) (__q->write_offset == __q->read_offset)

#define CF_Q_ELEM_PTR(__q, __i) (&__q->elements[(__i % __q->alloc_sz) * __q->element_sz])

/******************************************************************************/

#ifdef __cplusplus
} // end extern "C"
#endif
