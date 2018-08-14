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
#include <aerospike/as_list.h>
#include <aerospike/as_map.h>
#include <aerospike/as_string.h>
#include <aerospike/as_val.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *	TYPES
 *****************************************************************************/

/**
 *	An dynamic array implementation for as_list.
 *
 *	as_arryalist can either be initialize on the stack or the heap.
 *
 *	For stack allocation, you have two choices:
 *	- `as_arraylist_init()` - 	uses `cf_malloc()` to initialize the internal storage
 *								on the heap.
 *	- `as_arraylist_inita()` - 	uses `alloca()` to initialize the internal storage
 *								on the stack.
 *
 *	The key differences between the two is `as_arraylist_inita()` can't be 
 *	dynamically resized and is solely on the stack.
 *
 *	The following is using a `as_arraylist_inita()`:
 *	~~~~~~~~~~{.c}
 *	as_arraylist list;
 *	as_arraylist_inita(&list, 2);
 *	~~~~~~~~~~
 *
 *	You will notice that the code is quite similar to `as_arraylist_init()`:
 *	~~~~~~~~~~{.c}
 *	as_arraylist list;
 *	as_arraylist_init(&list, 2, 0);
 *	~~~~~~~~~~
 *
 *	If you need a new heap allocated list, then use `as_arraylist_new()`:
 *
 *	~~~~~~~~~~{.c}
 *	as_arraylist * list = as_arraylist_new(2, 0);
 *	~~~~~~~~~~
 *
 *	When you are finished using the list, then you should release the list and
 *	associated resources, using `as_arraylist_destroy()`:
 *	
 *	~~~~~~~~~~{.c}
 *	as_arraylist_destroy(list);
 *	~~~~~~~~~~
 *
 *	
 *	The `as_arraylist` is a subtype of `as_list`. This allows you to 
 *	alternatively use `as_list` functions, by typecasting `as_arraylist` to
 *	`as_list`.
 *
 *	~~~~~~~~~~{.c}
 *	as_arraylist list;
 *	as_list * l = (as_list *) as_arraylist_init(&list, 3, 0);
 *	as_list_append_int64(l, 1);
 *	as_list_append_int64(l, 2);
 *	as_list_append_int64(l, 3);
 *	as_list_destroy(l);
 *	~~~~~~~~~~
 *	
 *	Each of the `as_list` functions proxy to the `as_arraylist` functions.
 *	So, calling `as_list_destroy()` is equivalent to calling 
 *	`as_arraylist_destroy()`.
 *
 *	@extends as_list
 *	@ingroup aerospike_t
 */
typedef struct as_arraylist_s {

	/**
	 *	@private
	 *	as_arraylist is an as_list.
	 *	You can cast as_arraylist to as_list.
	 */
	as_list _;

	/**
	 *	Number of elements to add, when capacity is reached.
	 *	If 0 (zero), then capacity can't be expanded.
	 */
	uint32_t block_size;

	/**
	 *	The total number elements allocated.
	 */
	uint32_t capacity;

	/**
	 *	The number of elements used.
	 */
	uint32_t size;

	/**
	 *	The elements of the list.
	 */
	as_val ** elements;

	/**
	 *	If true, then as_arraylist.elements will be freed when
	 *	as_arraylist_destroy() is called.
	 */
	bool free;

} as_arraylist;

/**
 *	Status codes for various as_arraylist operations.
 */
typedef enum as_arraylist_status_e {
	
	/**
	 *	Normal operation.
	 */
	AS_ARRAYLIST_OK         = 0,

	/**
	 *	Unable to expand capacity, because cf_realloc() failed.
	 */
	AS_ARRAYLIST_ERR_ALLOC  = 1,

	/**
	 *	Unable to expand capacity, because as_arraylist.block_size is 0.
	 */
	AS_ARRAYLIST_ERR_MAX    = 2,

	/**
	 *	Illegal array index.
	 */
	AS_ARRAYLIST_ERR_INDEX  = 3

} as_arraylist_status;

/******************************************************************************
 *	MACROS
 ******************************************************************************/

/**
 *	Initialize a stack allocated as_arraylist, with element storage on 
 *	the stack.
 *
 *	This differs from as_arraylist_init(), in that as_arraylist_init() 
 *	allocates element storage on the heap.
 *
 *	@param __list 		The as_list to initialize
 *	@param __n			The number of elements to allocate to the list.
 *
 *	@return On success, the initialize list. Otherwise NULL.
 *	@relatesalso as_arraylist
 */
#define as_arraylist_inita(__list, __n)\
	as_arraylist_init((__list), 0, 0);\
	(__list)->free = false;\
	(__list)->capacity = (__n);\
	(__list)->size = 0;\
	(__list)->elements = (as_val**) alloca(sizeof(as_val*) * (__n));

/*******************************************************************************
 *	INSTANCE FUNCTIONS
 ******************************************************************************/

/**
 *	Initialize a stack allocated as_arraylist, with element storage on the 
 *	heap.
 *
 *	This differs from as_arraylist_inita(), in that as_arraylist_inita() 
 *	allocates element storage on the stack.
 *
 *	@param list 		The as_list to initialize
 *	@param capacity		The number of elements to allocate to the list.
 *	@param block_size	The number of elements to grow the list by, when the 
 *						capacity has been reached.
 *
 *	@return On success, the initialize list. Otherwise NULL.
 *	@relatesalso as_arraylist
 */
as_arraylist * as_arraylist_init(as_arraylist * list, uint32_t capacity, uint32_t block_size);

/**
 *	Create and initialize a heap allocated list as as_arraylist.
 *	
 *	@param capacity		The number of elements to allocate to the list.
 *	@param block_size	The number of elements to grow the list by, when the 
 *						capacity has been reached.
 *  
 *	@return On success, the new list. Otherwise NULL.
 *	@relatesalso as_arraylist
 */
as_arraylist * as_arraylist_new(uint32_t capacity, uint32_t block_size);

/**
 *	Destoy the list and release resources.
 *
 *	@param list	The list to destroy.
 *	@relatesalso as_arraylist
 */
void as_arraylist_destroy(as_arraylist * list);

/*******************************************************************************
 *	VALUE FUNCTIONS
 ******************************************************************************/

/**
 *  The hash value of the list.
 *
 *	@param list 	The list.
 *
 *	@return The hash value of the list.
 *	@relatesalso as_arraylist
 */
uint32_t as_arraylist_hashcode(const as_arraylist * list);

/**
 *  The number of elements in the list.
 *
 *	@param list 	The list.
 *
 *	@return The number of elements in the list.
 *	@relatesalso as_arraylist
 */
uint32_t as_arraylist_size(const as_arraylist * list);

/*******************************************************************************
 *	ACCESSOR AND MODIFIER FUNCTIONS
 ******************************************************************************/

/**
 *	Append all elements of list2, in order, to list. No new list object is
 *	created.
 *
 *	@param list 	The list to append to.
 *	@param list2 	The list from which to append.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
int as_arraylist_concat(as_arraylist * list, const as_arraylist * list2);

/**
 *	Delete (and destroy) all elements at and beyond specified index. Capacity is
 *	not reduced.
 *
 *	@param list 	The list to trim.
 *	@param index	The index from which to trim.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
int as_arraylist_trim(as_arraylist * list, uint32_t index);

/**
 *	Get the first element of the list.
 *
 *	@param list 	The list to get the first element from.
 *
 *	@return The first element of the list. Otherwise NULL.
 *	@relatesalso as_arraylist
 */
as_val * as_arraylist_head(const as_arraylist * list);

/**
 *  Returns a new list containing all elements other than the head
 *
 *	@param list 	The list to get the elements from.
 *
 *	@return A new list of all elements after the first element.
 *	@relatesalso as_arraylist
 */
as_arraylist * as_arraylist_tail(const as_arraylist * list);

/**
 *  Return a new list with the first n elements removed.
 *
 *	@param list 	The list.
 *	@param n 		The number of elements to remove.
 *
 *	@return A new list of all elements after the first n elements.
 *	@relatesalso as_arraylist
 */
as_arraylist * as_arraylist_drop(const as_arraylist * list, uint32_t n);

/**
 *  Return a new list containing the first n elements.
 *
 *	@param list 	The list.
 *	@param n 		The number of elements to take.
 *
 *	@return A new list of the first n elements.
 *	@relatesalso as_arraylist
 */
as_arraylist * as_arraylist_take(const as_arraylist * list, uint32_t n);

/******************************************************************************
 *	GET FUNCTIONS
 ******************************************************************************/

/**
 *  Return the value at the specified index.
 *
 *	@param list 	The list.
 *	@param index	The index of the element.
 *
 *	@return The value at given index, if it exists. Otherwise NULL.
 *	@relatesalso as_arraylist
 */
as_val * as_arraylist_get(const as_arraylist * list, uint32_t index);

/**
 *  Return an int64_t value at the specified index of the list.
 *
 *	@param list 	The list.
 *	@param index	The index of the element.
 *
 *	@return The value at given index, if it exists. Otherwise NULL.
 *	@relatesalso as_arraylist
 */
int64_t as_arraylist_get_int64(const as_arraylist * list, uint32_t index);

/**
 *  Return a double value at the specified index of the list.
 *
 *	@param list 	The list.
 *	@param index	The index of the element.
 *
 *	@return The value at given index, if it exists. Otherwise NULL.
 *	@relatesalso as_arraylist
 */
double as_arraylist_get_double(const as_arraylist * list, uint32_t index);

/**
 *  Return a NULL-terminated value at the specified index of the list.
 *
 *	@param list 	The list.
 *	@param index	The index of the element.
 *
 *	@return The value at given index, if it exists. Otherwise NULL.
 *	@relatesalso as_arraylist
 */
char * as_arraylist_get_str(const as_arraylist * list, uint32_t index);

/**
 *  Return an as_integer value at the specified index of the list.
 *
 *	@param list 	The list.
 *	@param index	The index of the element.
 *
 *	@return The value at given index, if it exists. Otherwise NULL.
 *	@relatesalso as_arraylist
 */
static inline as_integer * as_arraylist_get_integer(const as_arraylist * list, uint32_t index)
{
	return as_integer_fromval(as_arraylist_get(list, index));
}

/**
 *  Return an as_double value at the specified index of the list.
 *
 *	@param list 	The list.
 *	@param index	The index of the element.
 *
 *	@return The value at given index, if it exists. Otherwise NULL.
 *	@relatesalso as_arraylist
 */
static inline as_double* as_arraylist_get_as_double(const as_arraylist * list, uint32_t index)
{
	return as_double_fromval(as_arraylist_get(list, index));
}

/**
 *  Return an as_string value at the specified index of the list.
 *
 *	@param list 	The list.
 *	@param index	The index of the element.
 *
 *	@return The value at given index, if it exists. Otherwise NULL.
 *	@relatesalso as_arraylist
 */
static inline as_string * as_arraylist_get_string(const as_arraylist * list, uint32_t index)
{
	return as_string_fromval(as_arraylist_get(list, index));
}

/**
 *  Return an as_bytes value at the specified index of the list.
 *
 *	@param list 	The list.
 *	@param index	The index of the element.
 *
 *	@return The value at given index, if it exists. Otherwise NULL.
 *	@relatesalso as_arraylist
 */
static inline as_bytes * as_arraylist_get_bytes(const as_arraylist * list, uint32_t index)
{
	return as_bytes_fromval(as_arraylist_get(list, index));
}

/**
 *  Return an as_list value at the specified index of the list.
 *
 *	@param list 	The list.
 *	@param index	The index of the element.
 *
 *	@return The value at given index, if it exists. Otherwise NULL.
 *	@relatesalso as_arraylist
 */
static inline as_list * as_arraylist_get_list(const as_arraylist * list, uint32_t index)
{
	return as_list_fromval(as_arraylist_get(list, index));
}

/**
 *  Return an as_map value at the specified index of the list.
 *
 *	@param list 	The list.
 *	@param index	The index of the element.
 *
 *	@return The value at given index, if it exists. Otherwise NULL.
 *	@relatesalso as_arraylist
 */
static inline as_map * as_arraylist_get_map(const as_arraylist * list, uint32_t index)
{
	return as_map_fromval(as_arraylist_get(list, index));
}

/******************************************************************************
 *	SET FUNCTIONS
 ******************************************************************************/

/**
 *  Set a value at the specified index of the list.
 *
 *  Notice that in order to maintain proper object/memory management, we
 *  just first destroy (as_val_destroy()) the old object at element position(i)
 *  before assigning the new element.  Also note that the object at element
 *  position (i) is assumed to exist, so all element positions must be
 *  appropriately initialized to zero.
 *
 *	@param list 	The list.
 *	@param index	Position in the list.
 *	@param value	The value to set at the given index.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
int as_arraylist_set(as_arraylist * list, uint32_t index, as_val * value);

/**
 *  Set an int64_t value at the specified index of the list.
 *
 *	@param list 	The list.
 *	@param index	Position in the list.
 *	@param value	The value to set at the given index.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
int as_arraylist_set_int64(as_arraylist * list, uint32_t index, int64_t value);

/**
 *  Set a double value at the specified index of the list.
 *
 *	@param list 	The list.
 *	@param index	Position in the list.
 *	@param value	The value to set at the given index.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
int as_arraylist_set_double(as_arraylist * list, uint32_t index, double value);

/**
 *  Set a NULL-terminated string value at the specified index of the list.
 *
 *	@param list 	The list.
 *	@param index	Position in the list.
 *	@param value	The value to set at the given index.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
int as_arraylist_set_str(as_arraylist * list, uint32_t index, const char * value);

/**
 *  Set an as_integer value at the specified index of the list.
 *
 *	@param list 	The list.
 *	@param index	Position in the list.
 *	@param value	The value to set at the given index.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
static inline int as_arraylist_set_integer(as_arraylist * list, uint32_t index, as_integer * value)
{
	return as_arraylist_set(list, index, (as_val *) value);
}

/**
 *  Set an as_double value at the specified index of the list.
 *
 *	@param list 	The list.
 *	@param index	Position in the list.
 *	@param value	The value to set at the given index.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
static inline int as_arraylist_set_as_double(as_arraylist * list, uint32_t index, as_double * value)
{
	return as_arraylist_set(list, index, (as_val *) value);
}

/**
 *  Set an as_string value at the specified index of the list.
 *
 *	@param list 	The list.
 *	@param index	Position in the list.
 *	@param value	The value to set at the given index.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
static inline int as_arraylist_set_string(as_arraylist * list, uint32_t index, as_string * value)
{
	return as_arraylist_set(list, index, (as_val *) value);
}

/**
 *  Set an as_bytes value at the specified index of the list.
 *
 *	@param list 	The list.
 *	@param index	Position in the list.
 *	@param value	The value to set at the given index.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
static inline int as_arraylist_set_bytes(as_arraylist * list, uint32_t index, as_bytes * value)
{
	return as_arraylist_set(list, index, (as_val *) value);
}

/**
 *  Set an as_list value at the specified index of the list.
 *
 *	@param list 	The list.
 *	@param index	Position in the list.
 *	@param value	The value to set at the given index.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
static inline int as_arraylist_set_list(as_arraylist * list, uint32_t index, as_list * value)
{
	return as_arraylist_set(list, index, (as_val *) value);
}

/**
 *  Set an as_map value at the specified index of the list.
 *
 *	@param list 	The list.
 *	@param index	Position in the list.
 *	@param value	The value to set at the given index.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
static inline int as_arraylist_set_map(as_arraylist * list, uint32_t index, as_map * value)
{
	return as_arraylist_set(list, index, (as_val *) value);
}

/******************************************************************************
 *	INSERT FUNCTIONS
 ******************************************************************************/

/**
 *  Insert a value at the specified index of the list.
 *
 *	Any elements at and beyond specified index will be shifted so their indexes
 *	increase by 1. It's ok to insert beyond the current end of the list.
 *
 *	@param list 	The list.
 *	@param index	Position in the list.
 *	@param value	The value to insert at the given index.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
int as_arraylist_insert(as_arraylist * list, uint32_t index, as_val * value);

/**
 *  Insert an int64_t value at the specified index of the list.
 *
 *	@param list 	The list.
 *	@param index	Position in the list.
 *	@param value	The value to insert at the given index.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
int as_arraylist_insert_int64(as_arraylist * list, uint32_t index, int64_t value);

/**
 *  Insert a double value at the specified index of the list.
 *
 *	@param list 	The list.
 *	@param index	Position in the list.
 *	@param value	The value to insert at the given index.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
int as_arraylist_insert_double(as_arraylist * list, uint32_t index, double value);

/**
 *  Insert a NULL-terminated string value at the specified index of the list.
 *
 *	@param list 	The list.
 *	@param index	Position in the list.
 *	@param value	The value to insert at the given index.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
int as_arraylist_insert_str(as_arraylist * list, uint32_t index, const char * value);

/**
 *  Insert an as_integer value at the specified index of the list.
 *
 *	@param list 	The list.
 *	@param index	Position in the list.
 *	@param value	The value to insert at the given index.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
static inline int as_arraylist_insert_integer(as_arraylist * list, uint32_t index, as_integer * value)
{
	return as_arraylist_insert(list, index, (as_val *) value);
}

/**
 *  Insert an as_double value at the specified index of the list.
 *
 *	@param list 	The list.
 *	@param index	Position in the list.
 *	@param value	The value to insert at the given index.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
static inline int as_arraylist_insert_as_double(as_arraylist * list, uint32_t index, as_double * value)
{
	return as_arraylist_insert(list, index, (as_val *) value);
}

/**
 *  Insert an as_string value at the specified index of the list.
 *
 *	@param list 	The list.
 *	@param index	Position in the list.
 *	@param value	The value to insert at the given index.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
static inline int as_arraylist_insert_string(as_arraylist * list, uint32_t index, as_string * value)
{
	return as_arraylist_insert(list, index, (as_val *) value);
}

/**
 *  Insert an as_bytes value at the specified index of the list.
 *
 *	@param list 	The list.
 *	@param index	Position in the list.
 *	@param value	The value to insert at the given index.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
static inline int as_arraylist_insert_bytes(as_arraylist * list, uint32_t index, as_bytes * value)
{
	return as_arraylist_insert(list, index, (as_val *) value);
}

/**
 *  Insert an as_list value at the specified index of the list.
 *
 *	@param list 	The list.
 *	@param index	Position in the list.
 *	@param value	The value to insert at the given index.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
static inline int as_arraylist_insert_list(as_arraylist * list, uint32_t index, as_list * value)
{
	return as_arraylist_insert(list, index, (as_val *) value);
}

/**
 *  Insert an as_map value at the specified index of the list.
 *
 *	@param list 	The list.
 *	@param index	Position in the list.
 *	@param value	The value to insert at the given index.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
static inline int as_arraylist_insert_map(as_arraylist * list, uint32_t index, as_map * value)
{
	return as_arraylist_insert(list, index, (as_val *) value);
}

/******************************************************************************
 *	APPEND FUNCTIONS
 ******************************************************************************/

/**
 *  Add the value to the end of the list.
 *
 *	@param list 	The list.
 *	@param value 	The value to prepend.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
int as_arraylist_append(as_arraylist * list, as_val * value);

/**
 *  Add an int64_t to the end of the list.
 *
 *	@param list 	The list.
 *	@param value 	The value to prepend.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
int as_arraylist_append_int64(as_arraylist * list, int64_t value);

/**
 *  Add a double to the end of the list.
 *
 *	@param list 	The list.
 *	@param value 	The value to prepend.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
int as_arraylist_append_double(as_arraylist * list, double value);

/**
 *  Add a NULL-terminated string to the end of the list.
 *
 *	@param list 	The list.
 *	@param value 	The value to prepend.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
int as_arraylist_append_str(as_arraylist * list, const char * value);

/**
 *  Add an as_integer to the end of the list.
 *
 *	@param list 	The list.
 *	@param value 	The value to prepend.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
static inline int as_arraylist_append_integer(as_arraylist * list, as_integer * value) 
{
	return as_arraylist_append(list, (as_val *) value);
}

/**
 *  Add an as_double to the end of the list.
 *
 *	@param list 	The list.
 *	@param value 	The value to prepend.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
static inline int as_arraylist_append_as_double(as_arraylist * list, as_double * value)
{
	return as_arraylist_append(list, (as_val *) value);
}

/**
 *  Add an as_string to the end of the list.
 *
 *	@param list 	The list.
 *	@param value 	The value to prepend.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
static inline int as_arraylist_append_string(as_arraylist * list, as_string * value) 
{
	return as_arraylist_append(list, (as_val *) value);
}

/**
 *  Add an as_bytes to the end of the list.
 *
 *	@param list 	The list.
 *	@param value 	The value to prepend.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
static inline int as_arraylist_append_bytes(as_arraylist * list, as_bytes * value) 
{
	return as_arraylist_append(list, (as_val *) value);
}

/**
 *  Add an as_list to the end of the list.
 *
 *	@param list 	The list.
 *	@param value 	The value to prepend.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
static inline int as_arraylist_append_list(as_arraylist * list, as_list * value) 
{
	return as_arraylist_append(list, (as_val *) value);
}

/**
 *  Add an as_map to the end of the list.
 *
 *	@param list 	The list.
 *	@param value 	The value to prepend.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
static inline int as_arraylist_append_map(as_arraylist * list, as_map * value) 
{
	return as_arraylist_append(list, (as_val *) value);
}

/******************************************************************************
 *	PREPEND FUNCTIONS
 ******************************************************************************/

/**
 *  Add the value to the beginning of the list.
 *
 *	@param list 	The list.
 *	@param value 	The value to prepend.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
int as_arraylist_prepend(as_arraylist * list, as_val * value);

/**
 *  Add an int64_t to the beginning of the list.
 *
 *	@param list 	The list.
 *	@param value 	The value to prepend.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
int as_arraylist_prepend_int64(as_arraylist * list, int64_t value);

/**
 *  Add a double to the beginning of the list.
 *
 *	@param list 	The list.
 *	@param value 	The value to prepend.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
int as_arraylist_prepend_double(as_arraylist * list, double value);

/**
 *  Add a NULL-terminated string to the beginning of the list.
 *
 *	@param list 	The list.
 *	@param value 	The value to prepend.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
int as_arraylist_prepend_str(as_arraylist * list, const char * value);

/**
 *  Add an as_integer to the beginning of the list.
 *
 *	@param list 	The list.
 *	@param value 	The value to prepend.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
static inline int as_arraylist_prepend_integer(as_arraylist * list, as_integer * value) 
{
	return as_arraylist_prepend(list, (as_val *) value);
}

/**
 *  Add an as_double to the beginning of the list.
 *
 *	@param list 	The list.
 *	@param value 	The value to prepend.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
static inline int as_arraylist_prepend_as_double(as_arraylist * list, as_double * value)
{
	return as_arraylist_prepend(list, (as_val *) value);
}

/**
 *  Add an as_string to the beginning of the list.
 *
 *	@param list 	The list.
 *	@param value 	The value to prepend.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
static inline int as_arraylist_prepend_string(as_arraylist * list, as_string * value) 
{
	return as_arraylist_prepend(list, (as_val *) value);
}

/**
 *  Add an as_bytes to the beginning of the list.
 *
 *	@param list 	The list.
 *	@param value 	The value to prepend.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
static inline int as_arraylist_prepend_bytes(as_arraylist * list, as_bytes * value) 
{
	return as_arraylist_prepend(list, (as_val *) value);
}

/**
 *  Add an as_list to the beginning of the list.
 *
 *	@param list 	The list.
 *	@param value 	The value to prepend.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
static inline int as_arraylist_prepend_list(as_arraylist * list, as_list * value) 
{
	return as_arraylist_prepend(list, (as_val *) value);
}

/**
 *  Add an as_map to the beginning of the list.
 *
 *	@param list 	The list.
 *	@param value 	The value to prepend.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
static inline int as_arraylist_prepend_map(as_arraylist * list, as_map * value) 
{
	return as_arraylist_prepend(list, (as_val *) value);
}

/*******************************************************************************
 *	REMOVE FUNCTION
 ******************************************************************************/

/**
 *	Remove element at specified index.
 *
 *	Any elements beyond specified index will be shifted so their indexes
 *	decrease by 1. The element at specified index will be destroyed.
 *
 *	@param list 	The list.
 *	@param index 	The index of the element to remove.
 *
 *	@return AS_ARRAYLIST_OK on success. Otherwise an error occurred.
 *	@relatesalso as_arraylist
 */
int as_arraylist_remove(as_arraylist * list, uint32_t index);

/******************************************************************************
 *	ITERATION FUNCTIONS
 ******************************************************************************/

/** 
 *  Call the callback function for each element in the list.
 *
 *	@param list 	The list to iterate.
 *	@param callback	The function to call for each element in the list.
 *	@param udata	User-data to be sent to the callback.
 *
 *	@return true if iteration completes fully. false if iteration was aborted.
 *
 *	@relatesalso as_arraylist
 */
bool as_arraylist_foreach(const as_arraylist * list, as_list_foreach_callback callback, void * udata);

#ifdef __cplusplus
} // end extern "C"
#endif
