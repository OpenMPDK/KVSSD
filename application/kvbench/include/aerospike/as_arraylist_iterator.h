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

#include <aerospike/as_arraylist.h>
#include <aerospike/as_iterator.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *	TYPES
 ******************************************************************************/

/**
 *	Iterator for as_arraylist.
 *
 *	To use the iterator, you can either initialize a stack allocated variable,
 *	using `as_arraylist_iterator_init()`:
 *
 *	~~~~~~~~~~{.c}
 *	as_arraylist_iterator it;
 *	as_arraylist_iterator_init(&it, &list);
 *	~~~~~~~~~~
 * 
 *	Or you can create a new heap allocated variable, using 
 *	`as_arraylist_iterator_new()`:
 *
 *	~~~~~~~~~~{.c}
 *	as_arraylist_iterator * it = as_arraylist_iterator_new(&list);
 *	~~~~~~~~~~
 *
 *	To iterate, use `as_arraylist_iterator_has_next()` and 
 *	`as_arraylist_iterator_next()`:
 *
 *	~~~~~~~~~~{.c}
 *	while ( as_arraylist_iterator_has_next(&it) ) {
 *		const as_val * val = as_arraylist_iterator_next(&it);
 *	}
 *	~~~~~~~~~~
 *
 *	When you are finished using the iterator, then you should release the 
 *	iterator and associated resources:
 *
 *	~~~~~~~~~~{.c}
 *	as_arraylist_iterator_destroy(it);
 *	~~~~~~~~~~
 *	
 *
 *	The `as_arraylist_iterator` is a subtype of  `as_iterator`. This allows you
 *	to alternatively use `as_iterator` functions, by typecasting 
 *	`as_arraylist_iterator` to `as_iterator`.
 *
 *	~~~~~~~~~~{.c}
 *	as_arraylist_iterator it;
 *	as_iterator * i = (as_iterator *) as_arraylist_iterator_init(&it, &list);
 *
 *	while ( as_iterator_has_next(i) ) {
 *		const as_val * as_iterator_next(i);
 *	}
 *
 *	as_iterator_destroy(i);
 *	~~~~~~~~~~
 *
 *	Each of the `as_iterator` functions proxy to the `as_arraylist_iterator`
 *	functions. So, calling `as_iterator_destroy()` is equivalent to calling
 *	`as_arraylist_iterator_destroy()`.
 *
 *	@extends as_iterator
 */
typedef struct as_arraylist_iterator_s {

	/**
	 *	as_arraylist_iterator is an as_iterator.
	 *	You can cast as_arraylist_iterator to as_iterator.
	 */
	as_iterator _;

	/**
	 *	The as_arraylist being iterated over
	 */
	const as_arraylist * list;

	/**
	 *	The current position of the iteration
	 */
	uint32_t pos;

} as_arraylist_iterator;

/******************************************************************************
 *	FUNCTIONS
 *****************************************************************************/

/**
 *	Initializes a stack allocated as_iterator for as_arraylist.
 *
 *	@param iterator 	The iterator to initialize.
 *	@param list 		The list to iterate.
 *
 *	@return On success, the initialized iterator. Otherwise NULL.
 *
 *	@relatesalso as_arraylist_iterator
 */
as_arraylist_iterator * as_arraylist_iterator_init(as_arraylist_iterator * iterator, const as_arraylist * list);

/**
 *	Creates a new heap allocated as_iterator for as_arraylist.
 *
 *	@param list 		The list to iterate.
 *
 *	@return On success, the new iterator. Otherwise NULL.
 *
 *	@relatesalso as_arraylist_iterator
 */
as_arraylist_iterator * as_arraylist_iterator_new(const as_arraylist * list);

/**
 *	Destroy the iterator and releases resources used by the iterator.
 *
 *	@param iterator 	The iterator to release
 *
 *	@relatesalso as_arraylist_iterator
 */
void as_arraylist_iterator_destroy(as_arraylist_iterator * iterator);

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
 *	@relatesalso as_arraylist_iterator
 */
bool as_arraylist_iterator_has_next(const as_arraylist_iterator * iterator);

/**
 *	Attempts to get the next value from the iterator.
 *	This will return the next value, and iterate past the value.
 *
 *	@param iterator 	The iterator to get the next value from.
 *
 *	@return The next value in the list if available. Otherwise NULL.
 *
 *	@relatesalso as_arraylist_iterator
 */
const as_val * as_arraylist_iterator_next(as_arraylist_iterator * iterator);

#ifdef __cplusplus
} // end extern "C"
#endif
