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

#include <aerospike/as_map.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *	TYPES
 ******************************************************************************/

/**
 * Internal structure only for use by as_hashmap and as_hashmap_iterator.
 */
typedef struct as_hashmap_element_s {
	as_val * p_key;
	as_val * p_val;
	uint32_t next;
} as_hashmap_element;

/**
 *	A hashtable based implementation of `as_map`.
 *
 *	To use the map, you can either initialize a stack allocated map, 
 *	using `as_hashmap_init()`:
 *
 *	~~~~~~~~~~{.c}
 *	as_hashmap map;
 *	as_hashmap_init(&map, 32);
 *	~~~~~~~~~~
 *
 *	Or you can create a new heap allocated map using 
 *	`as_hashmap_new()`:
 *
 *	~~~~~~~~~~{.c}
 *	as_hashmap * map = as_hashmap_new(32);
 *	~~~~~~~~~~
 *
 *	When you are finished using the map, then you should release the 
 *	map and associated resources, using `as_hashmap_destroy()`:
 *	
 *	~~~~~~~~~~{.c}
 *	as_hashmap_destroy(map);
 *	~~~~~~~~~~
 *
 *
 *	The `as_hashmap` is a subtype of `as_map`. This allows you to alternatively
 *	use `as_map` functions, by typecasting `as_hashmap` to `as_map`.
 *
 *	~~~~~~~~~~{.c}
 *	as_hashmap map;
 *	as_map * l = (as_map*) as_hashmap_init(&map, 32);
 *	as_stringmap_set_int64(l, "a", 1);
 *	as_stringmap_set_int64(l, "b", 2);
 *	as_stringmap_set_int64(l, "c", 3);
 *	as_map_destroy(l);
 *	~~~~~~~~~~
 *	
 *	The `as_stringmap` functions are simplified functions for using string key.
 *	
 *	Each of the `as_map` functions proxy to the `as_hashmap` functions.
 *	So, calling `as_map_destroy()` is equivalent to calling 
 *	`as_hashmap_destroy()`.
 *
 *	Notes:
 *
 *	This hashmap implementation is NOT threadsafe.
 *
 *	Internally, the hashmap stores keys' and values' pointers - it does NOT copy
 *	the keys or values, so the caller must ensure these keys and values are not
 *	destroyed while the hashmap is still in use.
 *
 *	Further, the hashmap does not increment ref-counts of the keys or values.
 *	However when an element is removed from the hashmap, the hashmap will call
 *	as_val_destroy() on both the key and value. And when the hashmap is cleared
 *	or destroyed, as_val_destroy() will be called for all keys and values.
 *	Therefore if the caller inserts keys and values in the hashmap without extra
 *	ref-counts, the caller is effectively handing off ownership of these objects
 *	to the hashmap.
 *
 *	@extends as_map
 *	@ingroup aerospike_t
 */
typedef struct as_hashmap_s {
	
	/**
	 *	@private
	 *	as_hashmap is an as_map.
	 *	You can cast as_hashmap to as_map.
	 */
	as_map _;

	/**
	 * Number of elements in the map.
	 */
	uint32_t count;

	/**
	 * The "main" table - elements go here unless their key hash collides with
	 * that of an existing element's key.
	 */
	uint32_t table_capacity;
	as_hashmap_element * table;

	/**
	 * The "extra" slots - elements go here when their key hash collides with
	 * that of an existing element's key.
	 */
	uint32_t capacity_step;
	uint32_t extra_capacity;
	as_hashmap_element * extras;
	uint32_t insert_at;
	uint32_t free_q;

} as_hashmap;

/*******************************************************************************
 *	INSTANCE FUNCTIONS
 ******************************************************************************/

/**
 *	Initialize a stack allocated hashmap.
 *
 *	@param map 			The map to initialize.
 *	@param buckets		The number of hash buckets to allocate.
 *
 *	@return On success, the initialized map. Otherwise NULL.
 *
 *	@relatesalso as_hashmap
 */
as_hashmap * as_hashmap_init(as_hashmap * map, uint32_t buckets);

/**
 *	Creates a new map as a hashmap.
 *
 *	@param buckets		The number of hash buckets to allocate.
 *
 *	@return On success, the new map. Otherwise NULL.
 *
 *	@relatesalso as_hashmap
 */
as_hashmap * as_hashmap_new(uint32_t buckets);

/**
 *	Free the map and associated resources.
 *
 *	@param map 	The map to destroy.
 *
 *	@relatesalso as_hashmap
 */
void as_hashmap_destroy(as_hashmap * map);

/*******************************************************************************
 *	INFO FUNCTIONS
 ******************************************************************************/

/**
 *	The hash value of the map.
 *
 *	@param map 	The map.
 *
 *	@return The hash value of the map.
 *
 *	@relatesalso as_hashmap
 */
uint32_t as_hashmap_hashcode(const as_hashmap * map);

/**
 *	Get the number of entries in the map.
 *
 *	@param map 	The map.
 *
 *	@return The number of entries in the map.
 *
 *	@relatesalso as_hashmap
 */
uint32_t as_hashmap_size(const as_hashmap * map);

/*******************************************************************************
 *	ACCESSOR AND MODIFIER FUNCTIONS
 ******************************************************************************/

/**
 *	Get the value for specified key.
 *
 *	@param map 		The map.
 *	@param key		The key.
 *
 *	@return The value for the specified key. Otherwise NULL.
 *
 *	@relatesalso as_hashmap
 */
as_val * as_hashmap_get(const as_hashmap * map, const as_val * key);

/**
 *	Set the value for specified key.
 *
 *	@param map 		The map.
 *	@param key		The key.
 *	@param val		The value for the given key.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *
 *	@relatesalso as_hashmap
 */
int as_hashmap_set(as_hashmap * map, const as_val * key, const as_val * val);

/**
 *	Remove all entries from the map.
 *
 *	@param map		The map.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *
 *	@relatesalso as_hashmap
 */
int as_hashmap_clear(as_hashmap * map);

/**
 *	Remove the entry specified by the key.
 *
 *	@param map 	The map to remove the entry from.
 *	@param key 	The key of the entry to be removed.
 *
 *	@return 0 on success. Otherwise an error occurred.
 *
 *	@relatesalso as_hashmap
 */
int as_hashmap_remove(as_hashmap * map, const as_val * key);

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
 *	@relatesalso as_hashmap
 */
bool as_hashmap_foreach(const as_hashmap * map, as_map_foreach_callback callback, void * udata);

#ifdef __cplusplus
} // end extern "C"
#endif
