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

#include <aerospike/as_hashmap.h>
#include <aerospike/as_iterator.h>
#include <aerospike/as_pair.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *	TYPES
 ******************************************************************************/

/**
 *	Iterator for as_hashmap.
 *
 *	To use the iterator, you can either initialize a stack allocated variable,
 *	use `as_hashmap_iterator_init()`:
 *
 *	~~~~~~~~~~{.c}
 *	as_hashmap_iterator it;
 *	as_hashmap_iterator_init(&it, &map);
 *	~~~~~~~~~~
 * 
 *	Or you can create a new heap allocated variable using 
 *	`as_hashmap_iterator_new()`:
 *
 *	~~~~~~~~~~{.c}
 *	as_hashmap_iterator * it = as_hashmap_iterator_new(&map);
 *	~~~~~~~~~~
 *	
 *	To iterate, use `as_hashmap_iterator_has_next()` and 
 *	`as_hashmap_iterator_next()`:
 *
 *	~~~~~~~~~~{.c}
 *	while ( as_hashmap_iterator_has_next(&it) ) {
 *		const as_val * val = as_hashmap_iterator_next(&it);
 *	}
 *	~~~~~~~~~~
 *
 *	When you are finished using the iterator, then you should release the 
 *	iterator and associated resources:
 *	
 *	~~~~~~~~~~{.c}
 *	as_hashmap_iterator_destroy(it);
 *	~~~~~~~~~~
 *	
 *
 *	The `as_hashmap_iterator` is a subtype of  `as_iterator`. This allows you
 *	to alternatively use `as_iterator` functions, by typecasting 
 *	`as_hashmap_iterator` to `as_iterator`.
 *
 *	~~~~~~~~~~{.c}
 *	as_hashmap_iterator it;
 *	as_iterator * i = (as_iterator *) as_hashmap_iterator_init(&it, &map);
 *
 *	while ( as_iterator_has_next(i) ) {
 *		const as_val * as_iterator_next(i);
 *	}
 *
 *	as_iterator_destroy(i);
 *	~~~~~~~~~~
 *	
 *	Each of the `as_iterator` functions proxy to the `as_hashmap_iterator`
 *	functions. So, calling `as_iterator_destroy()` is equivalent to calling
 *	`as_hashmap_iterator_destroy()`.
 *
 *	Notes:
 *
 *	as_hashmap_iterator_next() returns an as_pair pointer. The as_pair contains
 *	the key and value pointers of the current map element. This one as_pair
 *	"container" is re-used for all the iterations, i.e. the contents will be
 *	overwritten and are only valid until the next iteration.
 *
 *	@extends as_iterator
 */
typedef struct as_hashmap_iterator_s {

	as_iterator _;

	/**
	 *	The hashmap
	 */
	const as_hashmap * map;

	/**
	 *	Current entry
	 */
	as_hashmap_element * curr;

	/**
	 *	Internal counters
	 */
	uint32_t count;
	uint32_t table_pos;
	uint32_t extras_pos;

	/**
	 *	Last returned key & value
	 */
	as_pair pair;

} as_hashmap_iterator;

/******************************************************************************
 *	FUNCTIONS
 *****************************************************************************/

/**
 *	Initializes a stack allocated as_iterator for the given as_hashmap.
 *
 *	@param iterator 	The iterator to initialize.
 *	@param map			The map to iterate.
 *
 *	@return On success, the initialized iterator. Otherwise NULL.
 *
 *	@relatesalso as_hashmap_iterator
 */
as_hashmap_iterator * as_hashmap_iterator_init(as_hashmap_iterator * iterator, const as_hashmap * map);

/**
 *	Creates a heap allocated as_iterator for the given as_hashmap.
 *
 *	@param map 			The map to iterate.
 *
 *	@return On success, the new iterator. Otherwise NULL.
 *
 *	@relatesalso as_hashmap_iterator
 */
as_hashmap_iterator * as_hashmap_iterator_new(const as_hashmap * map);

/**
 *	Destroy the iterator and releases resources used by the iterator.
 *
 *	@param iterator 	The iterator to release
 *
 *	@relatesalso as_hashmap_iterator
 */
void as_hashmap_iterator_destroy(as_hashmap_iterator * iterator);


/******************************************************************************
 *	ITERATOR FUNCTIONS
 *****************************************************************************/

/**
 *	Tests if there are more values available in the iterator.
 *
 *	@param iterator 	The iterator to be tested.
 *
 *	@return true if there are more values. Otherwise false.
 *
 *	@relatesalso as_hashmap_iterator
 */
bool as_hashmap_iterator_has_next(const as_hashmap_iterator * iterator);

/**
 *	Attempts to get the next value from the iterator.
 *	This will return the next value, and iterate past the value.
 *
 *	@param iterator 	The iterator to get the next value from.
 *
 *	@return The next value in the list if available. Otherwise NULL.
 *
 *	@relatesalso as_hashmap_iterator
 */
const as_val * as_hashmap_iterator_next(as_hashmap_iterator * iterator);

#ifdef __cplusplus
} // end extern "C"
#endif
