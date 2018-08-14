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

#include <aerospike/as_iterator.h>
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

union as_map_iterator_u;

struct as_map_hooks_s;

/**
 *	Callback function for `as_map_foreach()`. Called for each entry in the 
 *	map.
 *	
 *	@param key 		The key of the current entry.
 *	@param value 	The value of the current entry.
 *	@param udata	The user-data provided to the `as_list_foreach()`.
 *	
 *	@return true to continue iterating through the list. 
 *			false to stop iterating.
 */
typedef bool (* as_map_foreach_callback) (const as_val * key, const as_val * value, void * udata);

/**
 *	as_map is an interface for Map based data types.
 *
 *	Implementations:
 *	- as_hashmap
 *	
 *	@extends as_val
 *	@ingroup aerospike_t
 */
typedef struct as_map_s {

	/**
	 *	@private
	 *	as_map is a subtype of as_val.
	 *	You can cast as_map to as_val.
	 */
	as_val _;

	/**
	 *	Information for this instance of as_map.
	 */
	uint32_t flags;

	/**
	 *	Hooks for subtypes of as_map to implement.
	 */
	const struct as_map_hooks_s * hooks;

} as_map;

/**
 *	Map Function Hooks
 */
typedef struct as_map_hooks_s {

	/***************************************************************************
	 *	instance hooks
	 **************************************************************************/

	/**
	 *	Releases the subtype of as_map.
	 *
	 *	@param map 	The map instance to destroy.
	 *
	 *	@return true on success. Otherwise false.
	 */
	bool (* destroy)(as_map * map);

	/***************************************************************************
	 *	info hooks
	 **************************************************************************/

	/**
	 *	The hash value of an as_map.
	 *
	 *	@param map	The map to get the hashcode value for.
	 *
	 *	@return The hashcode value.
	 */
	uint32_t (* hashcode)(const as_map * map);
	
	/**
	 *	The size of the as_map.
	 *
	 *	@param map	The map to get the size of.
	 *
	 *	@return The number of entries in the map.
	 */
	uint32_t (* size)(const as_map * map);

	/***************************************************************************
	 *	accessor and modifier hooks
	 **************************************************************************/

	/**
	 *	Set a value of the given key in a map.
	 *
	 *	@param map 	The map to store the (key,value) pair.
	 *	@param key 	The key for the given value.
	 *	@param val 	The value for the given key.
	 *
	 *	@return 0 on success. Otherwise an error occurred.
	 */
	int (* set)(as_map * map, const as_val * key, const as_val * val);

	/**
	 *	Set a value at the given key of the map.
	 *
	 *	@param map 	The map to containing the (key,value) pair.
	 *	@param key 	The key of the value.
	 *
	 *	@return The value on success. Otherwise NULL.
	 */
	as_val * (* get)(const as_map * map, const as_val * key);

	/**
	 *	Clear all entries of the map.
	 *
	 *	@param map 	The map to clear.
	 *
	 *	@return 0 on success. Otherwise an error occurred.
	 */
	int (* clear)(as_map * map);

	/**
	 *	Remove the entry specified by the key.
	 *
	 *	@param map 	The map to remove the entry from.
	 *	@param key 	The key of the entry to be removed.
	 *
	 *	@return 0 on success. Otherwise an error occurred.
	 */
	int (* remove)(as_map * map, const as_val * key);

	/***************************************************************************
	 *	iteration hooks
	 **************************************************************************/
	
	/**
	 *	Iterate over each entry in the map can call the callback function.
	 *
	 *	@param map 		The map to iterate.
	 *	@param callback	The function to call for each entry in the map.
	 *	@param udata 	User-data to be passed to the callback.
	 *
	 *	@return true on success. Otherwise false.
	 */
	bool (* foreach)(const as_map * map, as_map_foreach_callback callback, void * udata);

	/**
	 *	Create and initialize a new heap allocated iterator to traverse over the entries map.
	 *
	 *	@param map 	The map to iterate.
	 *	
	 *	@return true on success. Otherwise false.
	 */
	union as_map_iterator_u * (* iterator_new)(const as_map * map);

	/**
	 *	Initialize a stack allocated iterator to traverse over the entries map.
	 *
	 *	@param map 	The map to iterate.
	 *	
	 *	@return true on success. Otherwise false.
	 */
	union as_map_iterator_u * (* iterator_init)(const as_map * map, union as_map_iterator_u * it);

} as_map_hooks;

/******************************************************************************
 *	INSTANCE FUNCTIONS
 *****************************************************************************/

/**
 *	@private
 *	Utilized by subtypes of as_map to initialize the parent.
 *
 *	@param map		The map to initialize
 *	@param free 	If TRUE, then as_map_destory() will free the map.
 *	@param flags	Map attributes.
 *	@param hooks	Implementaton for the map interface.
 *	
 *	@return The initialized as_map on success. Otherwise NULL.
 *	@relatesalso as_map
 */
as_map * as_map_cons(as_map * map, bool free, uint32_t flags, const as_map_hooks * hooks);

/**
 *	Initialize a stack allocated map.
 *
 *	@param map		Stack allocated map to initialize.
 *	@param hooks	Implementation for the map interface.
 *	
 *	@return On success, the initialized map. Otherwise NULL.
 *	@relatesalso as_map
 */
as_map * as_map_init(as_map * map, const as_map_hooks * hooks);

/**
 *	Create and initialize a new heap allocated map.
 *	
 *	@param hooks	Implementation for the list interface.
 *	
 *	@return On success, a new list. Otherwise NULL.
 *	@relatesalso as_map
 */
as_map * as_map_new(const as_map_hooks * hooks);

/**
 *	Destroy the as_map and associated resources.
 *	@relatesalso as_map
 */
static inline void as_map_destroy(as_map * map) 
{
	as_val_destroy((as_val *) map);
}

/*******************************************************************************
 *	INFO FUNCTIONS
 ******************************************************************************/

/**
 *	Hash value for the map
 *
 *	@param map		The map
 *
 *	@return The hashcode value of the map.
 *	@relatesalso as_map
 */
static inline uint32_t as_map_hashcode(const as_map * map) 
{
	return as_util_hook(hashcode, 0, map);
}

/**
 *	Get the number of entries in the map.
 *
 *	@param map		The map
 *
 *	@return The size of the map.
 *	@relatesalso as_map
 */
static inline uint32_t as_map_size(const as_map * map) 
{
	return as_util_hook(size, 0, map);
}

/*******************************************************************************
 *	ACCESSOR AND MODIFIER FUNCTIONS
 ******************************************************************************/

/**
 *	Get the value for specified key.
 *
 *	@param map		The map.
 *	@param key		The key.
 *
 *	@return The value for the specified key on success. Otherwise NULL.
 *	@relatesalso as_map
 */
static inline as_val * as_map_get(const as_map * map, const as_val * key)
{
	return as_util_hook(get, NULL, map, key);
}

/**
 *	Set the value for specified key.
 *
 *	@param map		The map.
 *	@param key		The key.
 *	@param val		The value for the key.
 *	
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_map
 */
static inline int as_map_set(as_map * map, const as_val * key, const as_val * val) 
{
	return as_util_hook(set, 1, map, key, val);
}

/**
 *	Remove all entries from the map.
 *
 *	@param map		The map.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *	@relatesalso as_map
 */
static inline int as_map_clear(as_map * map)
{
	return as_util_hook(clear, 1, map);
}

/**
 *	Remove the entry specified by the key.
 *
 *	@param map 	The map to remove the entry from.
 *	@param key 	The key of the entry to be removed.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *
 *	@relatesalso as_map
 */
static inline int as_map_remove(as_map * map, const as_val * key)
{
	return as_util_hook(remove, 1, map, key);
}

/******************************************************************************
 *	ITERATION FUNCTIONS
 *****************************************************************************/

/**
 *	Call the callback function for each entry in the map.
 *
 *	@param map		The map.
 *	@param callback	The function to call for each entry.
 *	@param udata	User-data to be passed to the callback.
 *	
 *	@return true if iteration completes fully. false if iteration was aborted.
 *
 *	@relatesalso as_map
 */
static inline bool as_map_foreach(const as_map * map, as_map_foreach_callback callback, void * udata) 
{
	return as_util_hook(foreach, false, map, callback, udata);
}

/**
 *	Creates and initializes a new heap allocated iterator over the given map.
 *
 *	@param map 	The map to iterate.
 *
 *	@return On success, a new as_iterator. Otherwise NULL.
 *	@relatesalso as_map
 */
static inline union as_map_iterator_u * as_map_iterator_new(const as_map * map) 
{
	return as_util_hook(iterator_new, NULL, map);
}

/**
 *	Initialzies a stack allocated iterator over the given map.
 *
 *	@param map 	The map to iterate.
 *	@param it 	The iterator to initialize.
 *
 *	@return On success, the initializes as_iterator. Otherwise NULL.
 *	@relatesalso as_map
 */
static inline union as_map_iterator_u * as_map_iterator_init(union as_map_iterator_u * it, const as_map * map) 
{
	return as_util_hook(iterator_init, NULL, map, it);
}

/******************************************************************************
 *	CONVERSION FUNCTIONS
 *****************************************************************************/

/**
 *	Convert to an as_val.
 *	@relatesalso as_map
 */
static inline as_val * as_map_toval(const as_map * map) 
{
	return (as_val *) map;
}

/**
 *	Convert from an as_val.
 *	@relatesalso as_map
 */
static inline as_map * as_map_fromval(const as_val * val) 
{
	return as_util_fromval(val, AS_MAP, as_map);
}

/******************************************************************************
 *	as_val FUNCTIONS
 *****************************************************************************/

/**
 *	@private
 *	Internal helper function for destroying an as_val.
 */
void as_map_val_destroy(as_val * val);

/**
 *	@private
 *	Internal helper function for getting the hashcode of an as_val.
 */
uint32_t as_map_val_hashcode(const as_val * val);

/**
 *	@private
 *	Internal helper function for getting the string representation of an as_val.
 */
char * as_map_val_tostring(const as_val * val);

#ifdef __cplusplus
} // end extern "C"
#endif
