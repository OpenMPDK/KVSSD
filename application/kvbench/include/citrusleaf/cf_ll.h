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

/*
 * SYNOPSIS
 * LinkedList
 * Sometimes the answer is a doubly linked list. It's not that frequent, but
 * all the corner cases in a double linked list can be annoying.
 *
 * the current use pattern is the caller creates a structure that starts with a 'cf_ll_element',
 * ie, can be cast to a cf_ll_element. The caller allocates and frees the memory.
 * (There are far cooler ways to do this, so if you want to improve this, go ahead!
 *
 */

#include <pthread.h>
#include <inttypes.h>
#include <citrusleaf/cf_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 * CONSTANTS
 ******************************************************************************/

#define CF_LL_REDUCE_DELETE      (1)
#define CF_LL_REDUCE_INSERT      (2)
#define CF_LL_REDUCE_MATCHED     (3)
#define CF_LL_REDUCE_NOT_MATCHED (4)
/******************************************************************************
 * TYPES
 ******************************************************************************/

typedef struct cf_ll_s cf_ll;
typedef struct cf_ll_element_s cf_ll_element;
typedef struct cf_ll_iterator_s cf_ll_iterator;
typedef int (*cf_ll_reduce_fn) (cf_ll_element * e, void *udata);
typedef void (*cf_ll_destructor) (cf_ll_element * e);
 
/**
 * cf_ll_element
 * The element that must be included in structures
 * This element should be the FIRST element in the structure where it is being included
 */
struct cf_ll_element_s {
	cf_ll_element * 	next;
	cf_ll_element * 	prev;
};

/**
 * cf_ll_iterator
 * the linked list iterator
 */
struct cf_ll_iterator_s {
	cf_ll_element *     next;
	bool                forward;
};
/**
 * cf_ll
 * the linked list container
 */
struct cf_ll_s {
	cf_ll_element * 	head;
	cf_ll_element * 	tail;
 	cf_ll_destructor 	destroy_fn;
	uint32_t			sz;
	bool				uselock;
	pthread_mutex_t		LOCK;
};

/******************************************************************************
 * INLINE FUNCTIONS
 ******************************************************************************/

static inline cf_ll_element *cf_ll_get_head(cf_ll *ll) {
	return(ll->head);
}

static inline cf_ll_element *cf_ll_get_tail(cf_ll *ll) {
	return(ll->tail);
}

static inline cf_ll_element *cf_ll_get_next(cf_ll_element *e) {
	return(e->next);
}

static inline cf_ll_element *cf_ll_get_prev(cf_ll_element *e) {
	return(e->prev);
}

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/

/**
 * Insert to head
 */
void cf_ll_prepend(cf_ll *ll, cf_ll_element *e );

/**
 * Insert to tail
 */
void cf_ll_append(cf_ll *ll, cf_ll_element *e );

/**
 * Insert after element !! warning! consider threadsafety before using this call!
 */
void cf_ll_insert_after(cf_ll *ll, cf_ll_element *cur, cf_ll_element *ins);

/**
 * Insert before element !! warning! consider threadsafey before using this call!
 */
void cf_ll_insert_before(cf_ll *ll, cf_ll_element *cur, cf_ll_element *ins);

/**
 * delete element - the real joy of a doubly linked list
 * If a destructor function has been set, call it as well
 */
void cf_ll_delete(cf_ll *ll, cf_ll_element *e );

uint32_t cf_ll_size(cf_ll *ll);
/*
 * Create a iterator for linked list. Will move from head to tail 
 * if forward is true else from tail to head
 */
cf_ll_iterator * cf_ll_getIterator(cf_ll * ll, bool forward);

/*
 * Get next element of linked list pointed by iterator
 */
cf_ll_element * cf_ll_getNext(cf_ll_iterator *iter);

/*
 * Release iterator
 */
void cf_ll_releaseIterator(cf_ll_iterator *iter);

/*
 * Search an element in the linked list.
 */
cf_ll_element * cf_ll_search(cf_ll *ll, cf_ll_element *e, bool forward, cf_ll_reduce_fn fn);

/*
 * Get the linked list element through indexing
 */
cf_ll_element *cf_ll_index(cf_ll *ll, int index);
/**
 * The way these reduces work:
 * ** If you're reducing and you want to delete this element, return CF_LL_REDUCE_DELETE
 * and it'll be removed from the list - but iteration will not halt
 * ** If you return a negative value, the reduction will terminate immediatly and that
 * return value will be passed to the reducer
 * ** The 'forward' parameter specifies whether you want to traverse from front to back,
 * pass in 'false' to go tail-to-head
 */
int cf_ll_reduce( cf_ll *ll,  bool forward, cf_ll_reduce_fn fn, void *udata);

/**
 * Insert-before
 * Sometimes you want to iterate a list, and insert before a certain element.
 * Common when you're trying to keep a sorted list and you have some knowledge
 * that either the list is short, or you're doing inserts of a particular pattern
 * so that a sorted-table is not the right answer.
 * 
 * Similar to the reduce function: if you want to bail out of the insert, return a negative
 * If you want to insert "here", return the special code
 * At the end of the list, you will be passed a null element (thus meaning you'll always
 * be called at least once)
 */
int cf_ll_insert_reduce(cf_ll *ll, cf_ll_element *e, bool forward, cf_ll_reduce_fn fn, void *udata);

/**
 * Call this function on a head structure to initialize it to empty
 * Call with whether you want a locked version of a lockfree version
 * if you're handling your own locks
 *
 * If you're using a standard delete methodology, then don't need a destructor function,
 * and can leave it blank. But if you're using the reduce / delete pattern, then
 * there's not an easy way for the application-level delete to occur, because you can't
 * free the structure first then call delete. (you could insert the element on a queue,
 * but it would have to carefully be a reference counted object, and then you'd still
 * need the linked-list-reduceor to decrement the linked list....)
 * In that case, you need to have a destructor
 * function that gets fired every time a removal from the list occurs. even on explicit
 * deletes it's called, just to be fancy.
 *
 * Note that when the destructor is called, the lock for the linked list is held
 * (if you've allocated the linked list with a lock)
 */
int cf_ll_init(cf_ll *ll, cf_ll_destructor destroy_fn, bool uselock);

/******************************************************************************/

#ifdef __cplusplus
} // end extern "C"
#endif
