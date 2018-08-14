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

#include <aerospike/as_util.h>
#include <aerospike/as_val.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *	TYPES
 ******************************************************************************/

struct as_iterator_hooks_s;

/**
 *	Iterator Object
 */
typedef struct as_iterator_s {

	/**
	 *	@private
	 *	If TRUE, then free this instance.
	 */
	bool free;

	/**
	 *	Data for the iterator.
	 */
	void * data;

	/**
	 *	Hooks for subtypes of as_iterator.
	 */
	const struct as_iterator_hooks_s * hooks;

} as_iterator;

/**
 *	Iterator Function Hooks
 */
typedef struct as_iterator_hooks_s {

	/**
	 *	Releases the subtype of as_iterator.
	 */
	bool (* destroy)(as_iterator *);

	/**
	 *	Tests whether there is another element in the iterator.
	 */
	bool (* has_next)(const as_iterator *);

	/**
	 *	Read the next value.
	 */
	const as_val * (* next)(as_iterator *);

} as_iterator_hooks;

/******************************************************************************
 *	INSTANCE FUNCTIONS
 ******************************************************************************/

/**
 *	Initialize a stack allocated iterator.
 */
as_iterator * as_iterator_init(as_iterator * iterator, bool free, void * data, const as_iterator_hooks * hooks);

/**
 *	Destroys the iterator and releasing associated resources.
 */
void as_iterator_destroy(as_iterator * iterator);

/******************************************************************************
 *	VALUE FUNCTIONS
 ******************************************************************************/

/**
 *	Tests if there are more values available in the iterator.
 *
 *	@param iterator		The iterator to be tested.
 *
 *	@return true if there are more values, otherwise false.
 */
static inline bool as_iterator_has_next(const as_iterator * iterator)
{
	return as_util_hook(has_next, false, iterator);
}

/**
 *	Attempts to get the next value from the iterator.
 *	This will return the next value, and iterate past the value.
 *
 *	@param iterator		The iterator to get the next value from.
 *
 *	@return the next value available in the iterator.
 */
static inline const as_val * as_iterator_next(as_iterator * iterator)
{
	return as_util_hook(next, NULL, iterator);
}

#ifdef __cplusplus
} // end extern "C"
#endif
