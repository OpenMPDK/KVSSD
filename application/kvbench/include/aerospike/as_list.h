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

#include <aerospike/as_bytes.h>
#include <aerospike/as_double.h>
#include <aerospike/as_integer.h>
#include <aerospike/as_iterator.h>
#include <aerospike/as_string.h>
#include <aerospike/as_util.h>
#include <aerospike/as_val.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *	TYPES
 *****************************************************************************/

union as_list_iterator_u;

struct as_list_hooks_s;

/**
 *	Callback function for `as_list_foreach()`. Called for each element in the 
 *	list.
 *	
 *	@param value 	The value of the current element.
 *	@param udata	The user-data provided to the `as_list_foreach()`.
 *	
 *	@return true to continue iterating through the list. 
 *			false to stop iterating.
 */
typedef bool (* as_list_foreach_callback) (as_val * value, void * udata);

/**
 *	as_list is an interface for List based data types.
 *
 *	Implementations:
 *	- as_arraylist
 *
 *	@extends as_val
 *	@ingroup aerospike_t
 */
typedef struct as_list_s {

	/**
	 *	@private
	 *	as_list is a subtype of as_val.
	 *	You can cast as_list to as_val.
	 */
	as_val _;

	/**
	 * Hooks for subtypes of as_list to implement.
	 */
	const struct as_list_hooks_s * hooks;

} as_list;

/**
 *	List Function Hooks
 */
typedef struct as_list_hooks_s {

	/***************************************************************************
	 *	instance hooks
	 **************************************************************************/

	/**
	 *	Releases the subtype of as_list.
	 *
	 *	@param map 	The map instance to destroy.
	 *
	 *	@return true on success. Otherwise false.
	 */
	bool (* destroy)(as_list * list);

	/***************************************************************************
	 *	info hooks
	 **************************************************************************/

	/**
	 *	The hash value of an as_list.
	 *
	 *	@param list	The list to get the hashcode value for.
	 *
	 *	@return The hashcode value.
	 */
	uint32_t (* hashcode)(const as_list * list);

	/**
	 *	The size of the as_list.
	 *
	 *	@param map	The map to get the size of.
	 *
	 *	@return The number of entries in the map.
	 */
	uint32_t (* size)(const as_list * list);

	/***************************************************************************
	 *	get hooks
	 **************************************************************************/

	/**
	 *	Get the value at a given index of the list.
	 *
	 *	@param list 	The list to get the value from.
	 *	@param index 	The index of the value.
	 *
	 *	@return The value at the given index on success. Otherwie NULL.
	 */
	as_val * (* get)(const as_list * list, uint32_t index);

	/**
	 *	Get the int64_t value at a given index of the list.
	 *
	 *	@param list 	The list to get the value from.
	 *	@param index 	The index of the value.
	 *	
	 *	@return The value at the given index on success. Otherwie NULL.
	 */
	int64_t (* get_int64)(const as_list * list, uint32_t index);

	/**
	 *	Get the double value at a given index of the list.
	 *
	 *	@param list 	The list to get the value from.
	 *	@param index 	The index of the value.
	 *
	 *	@return The value at the given index on success. Otherwie NULL.
	 */
	double (* get_double)(const as_list * list, uint32_t index);

	/**
	 *	Get the NULL-terminated string value at a given index of the list.
	 *
	 *	@param list 	The list to get the value from.
	 *	@param index 	The index of the value.
	 *
	 *	@return The value at the given index on success. Otherwie NULL.
	 */
	char * (* get_str)(const as_list * list, uint32_t index);

	/***************************************************************************
	 *	set hooks
	 **************************************************************************/

	/**
	 *	Set a value at the given index of the list.
	 *
	 *	@param list 	The list to get the value from.
	 *	@param index 	The index of the value.
	 *	@param value 	The value for the given index.
	 *
	 *	@return The value at the given index on success. Otherwie NULL.
	 */
	int (* set)(as_list * list, uint32_t index, as_val * value);

	/**
	 *	Set an int64_t value at the given index of the list.
	 *
	 *	@param list 	The list to get the value from.
	 *	@param index 	The index of the value.
	 *	@param value 	The value for the given index.
	 *
	 *	@return The value at the given index on success. Otherwie NULL.
	 */
	int (* set_int64)(as_list * list, uint32_t index, int64_t value);

	/**
	 *	Set a double value at the given index of the list.
	 *
	 *	@param list 	The list to get the value from.
	 *	@param index 	The index of the value.
	 *	@param value 	The value for the given index.
	 *
	 *	@return The value at the given index on success. Otherwie NULL.
	 */
	int (* set_double)(as_list * list, uint32_t index, double value);

	/**
	 *	Set a NULL-terminated string value at the given index of the list.
	 *
	 *	@param list 	The list to get the value from.
	 *	@param index 	The index of the value.
	 *	@param value 	The value for the given index.
	 *
	 *	@return The value at the given index on success. Otherwie NULL.
	 */
	int (* set_str)(as_list * list, uint32_t index, const char * value);

	/***************************************************************************
	 *	insert hooks
	 **************************************************************************/

	/**
	 *	Insert a value at the given index of the list.
	 *
	 *	@param list 	The list to insert the value into.
	 *	@param index 	The index of the value.
	 *	@param value 	The value for the given index.
	 *
	 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
	 */
	int (* insert)(as_list * list, uint32_t index, as_val * value);

	/**
	 *	Insert an int64_t value at the given index of the list.
	 *
	 *	@param list 	The list to insert the value into.
	 *	@param index 	The index of the value.
	 *	@param value 	The value for the given index.
	 *
	 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
	 */
	int (* insert_int64)(as_list * list, uint32_t index, int64_t value);

	/**
	 *	Insert a double value at the given index of the list.
	 *
	 *	@param list 	The list to insert the value into.
	 *	@param index 	The index of the value.
	 *	@param value 	The value for the given index.
	 *
	 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
	 */
	int (* insert_double)(as_list * list, uint32_t index, double value);

	/**
	 *	Insert a NULL-terminated string value at the given index of the list.
	 *
	 *	@param list 	The list to insert the value into.
	 *	@param index 	The index of the value.
	 *	@param value 	The value for the given index.
	 *
	 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
	 */
	int (* insert_str)(as_list * list, uint32_t index, const char * value);

	/***************************************************************************
	 *	append hooks
	 **************************************************************************/

	/**
	 *	Append a value to the list.
	 *
	 *	@param list		The list to append to.
	 *	@param value	The value to append to the list.
	 *
	 *	@return 0 on success. Otherwise an error occurred.
	 */
	int (* append)(as_list * list, as_val * value);

	/**
	 *	Append an int64_t value to the list.
	 *
	 *	@param list		The list to append to.
	 *	@param value	The value to append to the list.
	 *
	 *	@return 0 on success. Otherwise an error occurred.
	 */
	int (* append_int64)(as_list * list, int64_t value);

	/**
	 *	Append a double value to the list.
	 *
	 *	@param list		The list to append to.
	 *	@param value	The value to append to the list.
	 *
	 *	@return 0 on success. Otherwise an error occurred.
	 */
	int (* append_double)(as_list * list, double value);

	/**
	 *	Append a NULL-terminates string value to the list.
	 *
	 *	@param list		The list to append to.
	 *	@param value	The value to append to the list.
	 *
	 *	@return 0 on success. Otherwise an error occurred.
	 */
	int (* append_str)(as_list * list, const char * value);
	
	/***************************************************************************
	 *	prepend hooks
	 **************************************************************************/

	/**
	 *	Prepend the value to the list.
	 *
	 *	@param list		The list to prepend to.
	 *	@param value	The value to prepend to the list.
	 *
	 *	@return 0 on success. Otherwise an error occurred.
	 */
	int (* prepend)(as_list * list, as_val * value);

	/**
	 *	Prepend an int64_t value to the list.
	 *
	 *	@param list		The list to prepend to.
	 *	@param value	The value to prepend to the list.
	 *
	 *	@return 0 on success. Otherwise an error occurred.
	 */
	int (* prepend_int64)(as_list * list, int64_t value);

	/**
	 *	Prepend a double value to the list.
	 *
	 *	@param list		The list to prepend to.
	 *	@param value	The value to prepend to the list.
	 *
	 *	@return 0 on success. Otherwise an error occurred.
	 */
	int (* prepend_double)(as_list * list, double value);

	/**
	 *	Prepend a NULL-terminates string value to the list.
	 *
	 *	@param list		The list to prepend to.
	 *	@param value	The value to prepend to the list.
	 *
	 *	@return 0 on success. Otherwise an error occurred.
	 */
	int (* prepend_str)(as_list * list, const char * value);

	/***************************************************************************
	 *	remove hook
	 **************************************************************************/

	/**
	 *	Remove element at specified index.
	 *
	 *	Any elements beyond specified index will be shifted so their indexes
	 *	decrease by 1. The element at specified index will be destroyed.
	 *
	 *	@param list 	The list.
	 *	@param index 	The index of the element to remove.
	 *
	 *	@return 0 on success. Otherwise an error occurred.
	 */
	int (* remove)(as_list * list, uint32_t index);

	/***************************************************************************
	 *	accessor and modifier hooks
	 **************************************************************************/

	/**
	 *	Append all elements of list2, in order, to list. No new list object is
	 *	created.
	 *
	 *	@param list 	The list to append to.
	 *	@param list2 	The list from which to append.
	 *
	 *	@return 0 on success. Otherwise an error occurred.
	 */
	int (* concat)(as_list * list, const as_list * list2);

	/**
	 *	Delete (and destroy) all elements at and beyond specified index. Capacity is
	 *	not reduced.
	 *
	 *	@param list 	The list to trim.
	 *	@param index	The index from which to trim.
	 *
	 *	@return 0 on success. Otherwise an error occurred.
	 */
	int (* trim)(as_list * list, uint32_t index);

	/**
	 *	Return the first element in the list.
	 *
	 *	@param list 	The list to get the value from.
	 *
	 *	@return The first value in the list. Otherwise NULL.
	 */
	as_val * (* head)(const as_list * list);

	/**
	 *	Return all but the first element of the list, returning a new list.
	 *
	 *	@param list 	The list to get the list from.
	 *
	 *	@return The tail of the list. Otherwise NULL.
	 */
	as_list * (* tail)(const as_list * list);

	/**
	 *	Drop the first n element of the list, returning a new list.
	 *
	 *	@param list 	The list.
	 *	@param n 		The number of element to drop.
	 *
	 *	@return A new list containing the remaining elements. Otherwise NULL.
	 */
	as_list * (* drop)(const as_list * list, uint32_t n);

	/**
	 *	Take the first n element of the list, returning a new list.
	 *
	 *	@param list 	The list.
	 *	@param n 		The number of element to take.
	 *	
	 *	@return A new list containing the remaining elements. Otherwise NULL.
	 */
	as_list * (* take)(const as_list * list, uint32_t n);

	/***************************************************************************
	 *	iteration hooks
	 **************************************************************************/

	/**
	 *	Iterate over each element in the list can call the callback function.
	 *
	 *	@param map 		The map to iterate.
	 *	@param callback	The function to call for each element in the list.
	 *	@param udata 	User-data to be passed to the callback.
	 *
	 *	@return true on success. Otherwise false.
	 */
	bool (* foreach)(const as_list * list, as_list_foreach_callback callback, void * udata);

	/**
	 *	Create and initialize a new heap allocated iterator to traverse over the list.
	 *
	 *	@param list 	The list to iterate.
	 *	
	 *	@return true on success. Otherwise false.
	 */
	union as_list_iterator_u * (* iterator_new)(const as_list * list);

	/**
	 *	Initializes a stack allocated iterator to traverse over the list.
	 *
	 *	@param list 	The list to iterate.
	 *	
	 *	@return true on success. Otherwise false.
	 */
	union as_list_iterator_u * (* iterator_init)(const as_list * list, union as_list_iterator_u * it);

} as_list_hooks;

/*******************************************************************************
 *	INSTANCE FUNCTIONS
 ******************************************************************************/

/**
 *	@private
 *	Utilized by subtypes of as_list to initialize the parent.
 *
 *	@param list		The list to initialize.
 *	@param free		If true, then as_list_destroy() will free the list.
 *	@param hooks	Implementaton for the list interface.
 *
 *	@return On success, the initialized list. Otherwise NULL.
 *	@relatesalso as_list
 */
as_list * as_list_cons(as_list * list, bool free, const as_list_hooks * hooks);

/**
 *	Initialize a stack allocated list.
 *	
 *	@param list		Stack allocated list to initialize.
 *	@param hooks	Implementaton for the list interface.
 *	
 *	@return On succes, the initialized list. Otherwise NULL.
 *	@relatesalso as_list
 */
as_list * as_list_init(as_list * list, const as_list_hooks * hooks);

/**
 *	Create and initialize a new heap allocated list.
 *	
 *	@param hooks	Implementaton for the list interface.
 *	
 *	@return On succes, a new list. Otherwise NULL.
 *	@relatesalso as_list
 */
as_list * as_list_new(const as_list_hooks * hooks);

/**
 *	Destroy the list and associated resources.
 *
 *	@param list 	The list to destroy.
 *	@relatesalso as_list
 */
static inline void as_list_destroy(as_list * list) 
{
	as_val_destroy((as_val *) list);
}

/******************************************************************************
 *	INFO FUNCTIONS
 *****************************************************************************/

/**
 *	Get the hashcode value for the list.
 *
 *	@param list 	The list.
 *
 *	@return The hashcode of the list.
 *	@relatesalso as_list
 */
static inline uint32_t as_list_hashcode(as_list * list) 
{
	return as_util_hook(hashcode, 0, list);
}

/**
 *	Number of elements in the list.
 *
 *	@param list 	The list.
 *
 *	@return The size of the list.
 *	@relatesalso as_list
 */
static inline uint32_t as_list_size(as_list * list) 
{
	return as_util_hook(size, 0, list);
}

/******************************************************************************
 *	ACCESSOR AND MODIFIER FUNCTIONS
 *****************************************************************************/

/**
 *	Append all elements of list2, in order, to list. No new list object is
 *	created.
 *
 *	@param list 	The list to append to.
 *	@param list2 	The list from which to append.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_concat(as_list * list, const as_list * list2)
{
	return as_util_hook(concat, 1, list, list2);
}

/**
 *	Delete (and destroy) all elements at and beyond specified index. Capacity is
 *	not reduced.
 *
 *	@param list 	The list to trim.
 *	@param index	The index from which to trim.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_trim(as_list * list, uint32_t index)
{
	return as_util_hook(trim, 1, list, index);
}

/**
 *	The first element in the list.
 *
 *	@param list		The list to get the head value from.
 *
 *	@return The first value of the list on success. Otherwise NULL.
 *	@relatesalso as_list
 */
static inline as_val * as_list_head(const as_list * list) 
{
	return as_util_hook(head, NULL, list);
}

/**
 *	All elements after the first element in the list.
 *
 *	@param list		The list to get the tail from.
 *
 *	@return On success, the tail of the list. Otherwise NULL.
 *	@relatesalso as_list
 */
static inline as_list * as_list_tail(const as_list * list) 
{
	return as_util_hook(tail, NULL, list);
}

/**
 *	Create a new list containing all elements, except the first n elements, of the list.
 *
 *	@param list 	The list to drop elements from.
 *	@param n		The number of elements to drop.
 *
 *	@return On success, a new list containing the remaining elements. Otherwise NULL.
 *	@relatesalso as_list
 */
static inline as_list * as_list_drop(const as_list * list, uint32_t n) 
{
	return as_util_hook(drop, NULL, list, n);
}

/**
 *	Creates a new list containing the first n elements of the list.
 *
 *	@param list 	The list to drop elements from.
 *	@param n		The number of elements to take.
 *
 *	@return On success, a new list containing the selected elements. Otherwise NULL.
 *	@relatesalso as_list
 */
static inline as_list * as_list_take(const as_list * list, uint32_t n) 
{
	return as_util_hook(take, NULL, list, n);
}

/******************************************************************************
 *	GET FUNCTIONS
 *****************************************************************************/

/**
 *	Get the value at specified index as an as_val.
 *
 *	@param list		The list to get the value from.
 *	@param i		The index of the value to get from the list.
 *
 *	@return On success, the value at the given index. Otherwise NULL.
 *	@relatesalso as_list
 */
static inline as_val * as_list_get(const as_list * list, uint32_t i)
{
	return as_util_hook(get, NULL, list, i);
}

/**
 *	Get the value at specified index as an int64_t.
 *
 *	@param list		The list to get the value from.
 *	@param i		The index of the value to get from the list.
 *
 *	@return On success, the value at the given index. Otherwise NULL.
 *	@relatesalso as_list
 */
static inline int64_t as_list_get_int64(const as_list * list, uint32_t i)
{
	return as_util_hook(get_int64, 0, list, i);
}

/**
 *	Get the value at specified index as a double.
 *
 *	@param list		The list to get the value from.
 *	@param i		The index of the value to get from the list.
 *
 *	@return On success, the value at the given index. Otherwise NULL.
 *	@relatesalso as_list
 */
static inline double as_list_get_double(const as_list * list, uint32_t i)
{
	return as_util_hook(get_double, 0.0, list, i);
}

/**
 *	Get the value at specified index as an NULL terminated string.
 *
 *	@param list		The list to get the value from.
 *	@param i		The index of the value to get from the list.
 *
 *	@return On success, the value at the given index. Otherwise NULL.
 *	@relatesalso as_list
 */
static inline char * as_list_get_str(const as_list * list, uint32_t i)
{
	return as_util_hook(get_str, NULL, list, i);
}

/**
 *	Get the value at specified index as an as_integer.
 *
 *	@param list		The list to get the value from.
 *	@param i		The index of the value to get from the list.
 *
 *	@return On success, the value at the given index. Otherwise NULL.
 *	@relatesalso as_list
 */
static inline as_integer * as_list_get_integer(const as_list * list, uint32_t i)
{
	return as_integer_fromval(as_list_get(list, i));
}

/**
 *	Get the value at specified index as an as_double.
 *
 *	@param list		The list to get the value from.
 *	@param i		The index of the value to get from the list.
 *
 *	@return On success, the value at the given index. Otherwise NULL.
 *	@relatesalso as_list
 */
static inline as_double * as_list_get_as_double(const as_list * list, uint32_t i)
{
	return as_double_fromval(as_list_get(list, i));
}

/**
 *	Get the value at specified index as an as_val.
 *
 *	@param list		The list to get the value from.
 *	@param i		The index of the value to get from the list.
 *
 *	@return On success, the value at the given index. Otherwise NULL.
 *	@relatesalso as_list
 */
static inline as_string * as_list_get_string(const as_list * list, uint32_t i)
{
	return as_string_fromval(as_list_get(list, i));
}

/**
 *	Get the value at specified index as an as_val.
 *
 *	@param list		The list to get the value from.
 *	@param i		The index of the value to get from the list.
 *
 *	@return On success, the value at the given index. Otherwise NULL.
 *	@relatesalso as_list
 */
static inline as_bytes * as_list_get_bytes(const as_list * list, uint32_t i)
{
	return as_bytes_fromval(as_list_get(list, i));
}

/**
 *	Get the value at specified index as an as_val.
 *
 *	@param list		The list to get the value from.
 *	@param i		The index of the value to get from the list.
 *
 *	@return On success, the value at the given index. Otherwise NULL.
 *	@relatesalso as_list
 */
static inline as_list * as_list_get_list(const as_list * list, uint32_t i)
{
	as_val * v = as_list_get(list, i);
	return (as_list *) (v && v->type == AS_LIST ? v : NULL);
}

/**
 *	Get the value at specified index as an as_val.
 *
 *	@param list		The list to get the value from.
 *	@param i		The index of the value to get from the list.
 *
 *	@return On success, the value at the given index. Otherwise NULL.
 *	@relatesalso as_list
 */
static inline struct as_map_s * as_list_get_map(const as_list * list, uint32_t i)
{
	as_val * v = as_list_get(list, i);
	return (struct as_map_s *) (v && v->type == AS_MAP ? v : NULL);
}

/******************************************************************************
 *	SET FUNCTIONS
 *****************************************************************************/

/**
 *	Set the value at specified index as an as_val.
 *
 *	@param list		The list.
 *	@param i		The index of the value to set in the list.
 *	@param value	The value to set at the given index.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_set(as_list * list, uint32_t i, as_val * value)
{
	return as_util_hook(set, 1, list, i, value);
}

/**
 *	Set an int64_t at specified index as an as_val.
 *
 *	@param list		The list.
 *	@param i		The index of the value to set in the list.
 *	@param value	The value to set at the given index.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_set_int64(as_list * list, uint32_t i, int64_t value)
{
	return as_util_hook(set_int64, 1, list, i, value);
}

/**
 *	Set a double at specified index as an as_val.
 *
 *	@param list		The list.
 *	@param i		The index of the value to set in the list.
 *	@param value	The value to set at the given index.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_set_double(as_list * list, uint32_t i, double value)
{
	return as_util_hook(set_double, 1, list, i, value);
}

/**
 *	Set a NULL-terminated string at specified index as an as_val.
 *
 *	@param list		The list.
 *	@param i		The index of the value to set in the list.
 *	@param value	The value to set at the given index.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_set_str(as_list * list, uint32_t i, const char * value)
{
	return as_util_hook(set_str, 1, list, i, value);
}

/**
 *	Set an as_integer at specified index as an as_val.
 *
 *	@param list		The list.
 *	@param i		The index of the value to set in the list.
 *	@param value	The value to set at the given index.
 *
 *	@return 0 on success. Otherwise an error ocucrred.
 *	@relatesalso as_list
 */
static inline int as_list_set_integer(as_list * list, uint32_t i, as_integer * value)
{
	return as_list_set(list, i, (as_val *) value);
}

/**
 *	Set an as_double at specified index as an as_val.
 *
 *	@param list		The list.
 *	@param i		The index of the value to set in the list.
 *	@param value	The value to set at the given index.
 *
 *	@return 0 on success. Otherwise an error ocucrred.
 *	@relatesalso as_list
 */
static inline int as_list_set_as_double(as_list * list, uint32_t i, as_double * value)
{
	return as_list_set(list, i, (as_val *) value);
}

/**
 *	Set an as_string at specified index as an as_val.
 *
 *	@param list		The list.
 *	@param i		The index of the value to set in the list.
 *	@param value	The value to set at the given index.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_set_string(as_list * list, uint32_t i, as_string * value)
{
	return as_list_set(list, i, (as_val *) value);
}

/**
 *	Set an as_bytes at specified index as an as_val.
 *
 *	@param list		The list.
 *	@param i		The index of the value to set in the list.
 *	@param value	The value to set at the given index.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_set_bytes(as_list * list, uint32_t i, as_bytes * value)
{
	return as_list_set(list, i, (as_val *) value);
}

/**
 *	Set an as_list at specified index as an as_val.
 *
 *	@param list		The list.
 *	@param i		The index of the value to set in the list.
 *	@param value	The value to set at the given index.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_set_list(as_list * list, uint32_t i, as_list * value)
{
	return as_list_set(list, i, (as_val *) value);
}

/**
 *	Set an as_map at specified index as an as_val.
 *
 *	@param list		The list.
 *	@param i		The index of the value to set in the list.
 *	@param value	The value to set at the given index.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_set_map(as_list * list, uint32_t i, struct as_map_s * value)
{
	return as_list_set(list, i, (as_val *) value);
}

/******************************************************************************
 *	INSERT FUNCTIONS
 *****************************************************************************/

/**
 *  Insert a value at the specified index of the list.
 *
 *	Any elements at and beyond specified index will be shifted so their indexes
 *	increase by 1. It's ok to insert beyond the current end of the list.
 *
 *	@param list 	The list.
 *	@param i		The index at which to insert.
 *	@param value	The value to insert at the given index.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_insert(as_list * list, uint32_t i, as_val * value)
{
	return as_util_hook(insert, 1, list, i, value);
}

/**
 *	Insert an int64_t at specified index as an as_val.
 *
 *	@param list		The list.
 *	@param i		The index at which to insert.
 *	@param value	The value to insert at the given index.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_insert_int64(as_list * list, uint32_t i, int64_t value)
{
	return as_util_hook(insert_int64, 1, list, i, value);
}

/**
 *	Insert a double at specified index as an as_val.
 *
 *	@param list		The list.
 *	@param i		The index at which to insert.
 *	@param value	The value to insert at the given index.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_insert_double(as_list * list, uint32_t i, double value)
{
	return as_util_hook(insert_double, 1, list, i, value);
}

/**
 *	Insert a NULL-terminated string at specified index as an as_val.
 *
 *	@param list		The list.
 *	@param i		The index at which to insert.
 *	@param value	The value to insert at the given index.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_insert_str(as_list * list, uint32_t i, const char * value)
{
	return as_util_hook(insert_str, 1, list, i, value);
}

/**
 *	Insert an as_integer at specified index as an as_val.
 *
 *	@param list		The list.
 *	@param i		The index at which to insert.
 *	@param value	The value to insert at the given index.
 *
 *	@return 0 on success. Otherwise an error ocucrred.
 *	@relatesalso as_list
 */
static inline int as_list_insert_integer(as_list * list, uint32_t i, as_integer * value)
{
	return as_list_insert(list, i, (as_val *) value);
}

/**
 *	Insert an as_double at specified index as an as_val.
 *
 *	@param list		The list.
 *	@param i		The index at which to insert.
 *	@param value	The value to insert at the given index.
 *
 *	@return 0 on success. Otherwise an error ocucrred.
 *	@relatesalso as_list
 */
static inline int as_list_insert_as_double(as_list * list, uint32_t i, as_double * value)
{
	return as_list_insert(list, i, (as_val *) value);
}

/**
 *	Insert an as_string at specified index as an as_val.
 *
 *	@param list		The list.
 *	@param i		The index at which to insert.
 *	@param value	The value to insert at the given index.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_insert_string(as_list * list, uint32_t i, as_string * value)
{
	return as_list_insert(list, i, (as_val *) value);
}

/**
 *	Insert an as_bytes at specified index as an as_val.
 *
 *	@param list		The list.
 *	@param i		The index at which to insert.
 *	@param value	The value to insert at the given index.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_insert_bytes(as_list * list, uint32_t i, as_bytes * value)
{
	return as_list_insert(list, i, (as_val *) value);
}

/**
 *	Insert an as_list at specified index as an as_val.
 *
 *	@param list		The list.
 *	@param i		The index at which to insert.
 *	@param value	The value to insert at the given index.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_insert_list(as_list * list, uint32_t i, as_list * value)
{
	return as_list_insert(list, i, (as_val *) value);
}

/**
 *	Insert an as_map at specified index as an as_val.
 *
 *	@param list		The list.
 *	@param i		The index at which to insert.
 *	@param value	The value to insert at the given index.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_insert_map(as_list * list, uint32_t i, struct as_map_s * value)
{
	return as_list_insert(list, i, (as_val *) value);
}

/******************************************************************************
 *	APPEND FUNCTIONS
 *****************************************************************************/

/**
 *	Append a value to the list.
 *
 *	@param list		The list.
 *	@param value	The value to append to the list.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_append(as_list * list, as_val * value) 
{
	return as_util_hook(append, 1, list, value);
}

/**
 *	Append an int64_t to the list.
 *
 *	@param list		The list.
 *	@param value	The value to append to the list.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_append_int64(as_list * list, int64_t value) 
{
	return as_util_hook(append_int64, 1, list, value);
}

/**
 *	Append a double to the list.
 *
 *	@param list		The list.
 *	@param value	The value to append to the list.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_append_double(as_list * list, double value)
{
	return as_util_hook(append_double, 1, list, value);
}

/**
 *	Append a NULL-terminated string to the list.
 *
 *	@param list		The list.
 *	@param value	The value to append to the list.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_append_str(as_list * list, const char * value) 
{
	return as_util_hook(append_str, 1, list, value);
}

/**
 *	Append an as_integer to the list.
 *
 *	@param list		The list.
 *	@param value	The value to append to the list.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_append_integer(as_list * list, as_integer * value) 
{
	return as_list_append(list, (as_val *) value);
}

/**
 *	Append an as_double to the list.
 *
 *	@param list		The list.
 *	@param value	The value to append to the list.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_append_as_double(as_list * list, as_double * value)
{
	return as_list_append(list, (as_val *) value);
}

/**
 *	Append an as_string to the list.
 *
 *	@param list		The list.
 *	@param value	The value to append to the list.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_append_string(as_list * list, as_string * value) 
{
	return as_list_append(list, (as_val *) value);
}

/**
 *	Append an as_bytes to the list.
 *
 *	@param list		The list.
 *	@param value	The value to append to the list.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_append_bytes(as_list * list, as_bytes * value) 
{
	return as_list_append(list, (as_val *) value);
}

/**
 *	Append an as_list to the list.
 *
 *	@param list		The list.
 *	@param value	The value to append to the list.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_append_list(as_list * list, as_list * value) 
{
	return as_list_append(list, (as_val *) value);
}

/**
 *	Append an as_map to the list.
 *
 *	@param list		The list.
 *	@param value	The value to append to the list.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_append_map(as_list * list, struct as_map_s * value) 
{
	return as_list_append(list, (as_val *) value);
}

/******************************************************************************
 *	PREPEND FUNCTIONS
 *****************************************************************************/

/**
 *	Prepend a value to the list.
 *
 *	@param list		The list.
 *	@param value	The value to prepend to the list.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_prepend(as_list * list, as_val * value) 
{
	return as_util_hook(prepend, 1, list, value);
}

/**
 *	Prepend an int64_t value to the list.
 *
 *	@param list		The list.
 *	@param value	The value to prepend to the list.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_prepend_int64(as_list * list, int64_t value) 
{
	return as_util_hook(prepend_int64, 1, list, value);
}

/**
 *	Prepend a double value to the list.
 *
 *	@param list		The list.
 *	@param value	The value to prepend to the list.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_prepend_double(as_list * list, double value)
{
	return as_util_hook(prepend_double, 1, list, value);
}

/**
 *	Prepend a NULL-terminated string to the list.
 *
 *	@param list		The list.
 *	@param value	The value to prepend to the list.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_prepend_str(as_list * list, const char * value) 
{
	return as_util_hook(prepend_str, 1, list, value);
}

/**
 *	Prepend an as_integer to the list.
 *
 *	@param list		The list.
 *	@param value	The value to prepend to the list.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_prepend_integer(as_list * list, as_integer * value) 
{
	return as_list_prepend(list, (as_val *) value);
}

/**
 *	Prepend an as_double to the list.
 *
 *	@param list		The list.
 *	@param value	The value to prepend to the list.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_prepend_as_double(as_list * list, as_double * value)
{
	return as_list_prepend(list, (as_val *) value);
}

/**
 *	Prepend an as_string to the list.
 *
 *	@param list		The list.
 *	@param value	The value to prepend to the list.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_prepend_string(as_list * list, as_string * value) 
{
	return as_list_prepend(list, (as_val *) value);
}

/**
 *	Prepend an as_bytes to the list.
 *
 *	@param list		The list.
 *	@param value	The value to prepend to the list.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_prepend_bytes(as_list * list, as_bytes * value)
{
	return as_list_prepend(list, (as_val *) value);
}

/**
 *	Prepend an as_list to the list.
 *
 *	@param list		The list.
 *	@param value	The value to prepend to the list.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_prepend_list(as_list * list, as_list * value) 
{
	return as_list_prepend(list, (as_val *) value);
}

/**
 *	Prepend an as_map to the list.
 *
 *	@param list		The list.
 *	@param value	The value to prepend to the list.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_prepend_map(as_list * list, struct as_map_s * value) 
{
	return as_list_prepend(list, (as_val *) value);
}

/******************************************************************************
 *	REMOVE FUNCTION
 *****************************************************************************/

/**
 *	Remove element at specified index.
 *
 *	Any elements beyond specified index will be shifted so their indexes
 *	decrease by 1. The element at specified index will be destroyed.
 *
 *	@param list 	The list.
 *	@param index 	The index of the element to remove.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_list
 */
static inline int as_list_remove(as_list * list, uint32_t index)
{
	return as_util_hook(remove, 1, list, index);
}

/******************************************************************************
 *	ITERATION FUNCTIONS
 *****************************************************************************/

/**
 *	Call the callback function for each element in the list..
 *
 *	@param list		The list to iterate over.
 *	@param callback	The callback function call for each element.
 *	@param udata	User-data to send to the callback.
 *
 *	@return true if iteration completes fully. false if iteration was aborted.
 *
 *	@relatesalso as_list
 */
static inline bool as_list_foreach(const as_list * list, as_list_foreach_callback callback, void * udata) 
{
	return as_util_hook(foreach, false, list, callback, udata);
}

/**
 *	Creates and initializes a new heap allocated iterator over the given list.
 *
 *	@param list 	The list to iterate.
 *
 *	@return On success, a new as_iterator. Otherwise NULL.
 *	@relatesalso as_list
 */
static inline union as_list_iterator_u * as_list_iterator_new(const as_list * list) 
{
	return as_util_hook(iterator_new, NULL, list);
}


/**
 *	Initializes a stack allocated iterator over the given list.
 *
 *	@param list 	The list to iterate.
 *	@param it 		The iterator to initialize.
 *
 *	@return On success, the initializes as_iterator. Otherwise NULL.
 *	@relatesalso as_list
 */
static inline union as_list_iterator_u * as_list_iterator_init(union as_list_iterator_u * it, const as_list * list) 
{
	return as_util_hook(iterator_init, NULL, list, it);
}

/******************************************************************************
 *	CONVERSION FUNCTIONS
 *****************************************************************************/

/**
 *	Convert to an as_val.
 *	@relatesalso as_list
 */
static inline as_val * as_list_toval(as_list * list) 
{
	return (as_val *) list;
}

/**
 *	Convert from an as_val.
 *	@relatesalso as_list
 */
static inline as_list * as_list_fromval(as_val * v) 
{
	return as_util_fromval(v, AS_LIST, as_list);
}

/******************************************************************************
 * as_val FUNCTIONS
 *****************************************************************************/

/**
 *	@private
 *	Internal helper function for destroying an as_val.
 */
void as_list_val_destroy(as_val * v);

/**
 *	@private
 *	Internal helper function for getting the hashcode of an as_val.
 */
uint32_t as_list_val_hashcode(const as_val * v);

/**
 *	@private
 *	Internal helper function for getting the string representation of an as_val.
 */
char * as_list_val_tostring(const as_val * v);

#ifdef __cplusplus
} // end extern "C"
#endif
