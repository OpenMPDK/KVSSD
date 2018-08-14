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

#include <aerospike/as_bin.h>
#include <aerospike/as_bytes.h>
#include <aerospike/as_integer.h>
#include <aerospike/as_key.h>
#include <aerospike/as_list.h>
#include <aerospike/as_map.h>
#include <aerospike/as_rec.h>
#include <aerospike/as_string.h>
#include <aerospike/as_geojson.h>
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

/**
 *	Records in Aerospike are collections of named bins. 
 *
 *	The bins in a record are analogous to columns in relational databases. 
 *	However, unlike columns, the bins themselves are not typed. Instead, bins 
 *	contain values which are typed. So, it is possible to have multiple records 
 *	with bins of the same name but different types for values.
 *
 *	The bin's value can only be of the types defined in `as_bin_value`.
 *	
 *	## Initialization
 *	
 *	There are several ways to initialize an `as_record`. 
 *
 *	You can create the `as_record` on the stack:
 *	
 *	~~~~~~~~~~{.c}
 *	as_record rec;
 *	~~~~~~~~~~
 *	
 *	Then initialize it using either the `as_record_init()` function or 
 *	`as_record_inita()` macro.
 *
 *	The `as_record_init()` function will initialize the variable, then 
 *	allocate the specified number of bins using `malloc()`. The following
 *	initializes `rec` with 2 bins.
 *
 *	~~~~~~~~~~{.c}
 *	as_record_init(&rec, 2);
 *	~~~~~~~~~~
 *
 *	The `as_record_inita()` macro will initialize the variable, then allocate
 *	the specified number of bins using `alloca()`. The following initializes 
 *	`rec` with 2 bins.
 *
 *	~~~~~~~~~~{.c}
 *	as_record_inita(&rec, 2);
 *	~~~~~~~~~~
 *	
 *	The `as_record_new()` function will allocate an `as_record` on the heap
 *	using `malloc()` then allocate the specified number of bins using 
 *	`malloc()`. The following creates a new `as_record` with 2 bins.
 *
 *	~~~~~~~~~~{.c}
 *	as_record * rec = as_record_new(2);
 *	~~~~~~~~~~
 *	
 *	## Destruction
 *
 *	When you no longer require an as_record, you should call `as_record_destroy()`
 *	to release the record and associated resources.
 *
 *	~~~~~~~~~~{.c}
 *	as_record_destroy(rec);
 *	~~~~~~~~~~
 *
 *	If the record has been ref-counted, then the ref-count will be decremented, 
 *	until it reaches 0 (zero), at which point, the record will be released.
 *
 *	## Setting Bin Values
 *
 *	The following are functions for setting values in bins of a record. Utilize 
 *	the appropriate setter for the data you want to store in a bin.
 *
 *   Function                    |  Description
 *	---------------------------- | ----------------------------------------------
 *	 `as_record_set_int64()`     | Set the bin value to a 64-bit integer.
 *	 `as_record_set_str()`       | Set the bin value to a NULL-terminated string.
 *	 `as_record_set_integer()`   | Set the bin value to an `as_integer`.
 *	 `as_record_set_double()`    | Set the bin value to an `as_double`.
 *	 `as_record_set_string()`    | Set the bin value to an `as_string`.
 *	 `as_record_set_geojson()`   | Set the bin value to an `as_geojson`.
 *	 `as_record_set_bytes()`     | Set the bin value to an `as_bytes`.
 *	 `as_record_set_list()`      | Set the bin value to an `as_list`.                    
 *	 `as_record_set_map()`       | Set the bin value to an `as_map`.
 *	 `as_record_set_nil()`       | Set the bin value to an `as_nil`.
 *	 `as_record_set()`           | Set the bin value to an `as_bin_value`.
 *
 *	## Getting Bin Values
 *
 *	The following are functions for getting values from bins of a record. 
 *	Utilize the appropriate getter for the data you want to read from a bin.
 *	
 *
 *   Function                    |  Description
 *	---------------------------- | ----------------------------------------------
 *	 `as_record_get_int64()`     | Get the bin as a 64-bit integer.
 *	 `as_record_get_str()`       | Get the bin as a NULL-terminated string.
 *	 `as_record_get_integer()`   | Get the bin as an `as_integer`.
 *	 `as_record_get_double()`    | Get the bin as an `as_double`.
 *	 `as_record_get_string()`    | Get the bin as an `as_string`.
 *	 `as_record_get_geojson()`   | Get the bin as an `as_geojson`.
 *	 `as_record_get_bytes()`     | Get the bin as an `as_bytes`.
 *	 `as_record_get_list()`      | Get the bin as an `as_list`. 
 *	 `as_record_get_map()`       | Get the bin as an `as_map`.
 *	 `as_record_get()`           | Get the bin as an `as_bin_value`.
 *
 *	If you are unsure of the type of data stored in the bin, then you should 
 *	use `as_record_get()`. You can then check the type of the value using
 *	`as_val_type()`.
 *
 *	~~~~~~~~~~{.c}
 *	as_bin_value * value = as_record_get(rec, "bin1");
 *	switch ( as_val_type(value) ) {
 *		case AS_NIL: break;
 *		case AS_INTEGER: break;
 *		case AS_DOUBLE: break;
 *		case AS_STRING: break;
 *		case AS_GEOJSON: break;
 *		case AS_BYTES: break;
 *		case AS_LIST: break;
 *		case AS_MAP: break;
 *		case AS_REC: break;
 *		case AS_UNDEF: break;
 *	}
 *	~~~~~~~~~~
 *
 *	## Traversing Bins
 *
 *	If you want to traverse the bins of a record, then you have two options:
 *	
 *	- as_record_foreach() — Calls a function for each bin traversed.
 *	- as_record_iterator — Uses an iterator pattern to traverse bins.
 *
 *	@extends as_rec
 *	@ingroup client_objects
 */
typedef struct as_record_s {

	/**
	 *	@private
	 *	as_record is "derived" from as_rec.
	 *	So you can actually type cast as_record to as_rec.
	 */
	as_rec _;

	/**
	 *	The key of the record.
	 *	This is populated when a record is read from the database.
	 *	This should not be set by the user.
	 */
	as_key key;

	/**
	 *	The generation of the record.
	 */
	uint16_t gen;

	/**
	 *	The time-to-live (expiration) of the record in seconds.
	 *	There are also special values that can be set in the record TTL:
	 *	(*) ZERO (defined as AS_RECORD_DEFAULT_TTL), which means that the
	 *	    record will adopt the default TTL value from the namespace.
	 *	(*) 0xFFFFFFFF (also, -1 in a signed 32 bit int)
	 *	    (defined as AS_RECORD_NO_EXPIRE_TTL), which means that the record
	 *	    will get an internal "void_time" of zero, and thus will never expire.
	 *	(*) 0xFFFFFFFE (also, -2 in a signed 32 bit int)
	 *	    (defined as AS_RECORD_NO_CHANGE_TTL), which means that the record
	 *	    ttl will not change when the record is updated.
	 *
	 *	Note that the TTL value will be employed ONLY on write/update calls.
	 */
	uint32_t ttl;

	/**
	 *	The bins of the record.
	 */
	as_bins bins;

} as_record;

/**
 * When the record is given a TTL value of ZERO, it will adopt the TTL value
 * that is the default TTL value for the namespace (defined in the config file).
 */
#define AS_RECORD_DEFAULT_TTL 0

/**
 * When the record is given a TTL value of 0xFFFFFFFF, it will set the internal
 * void_time value (the absolute clock time value that shows when a record
 * will expire) to zero, which means the record will never expire
 */
#define AS_RECORD_NO_EXPIRE_TTL 0xFFFFFFFF

/**
 * When the record is given a TTL value of 0xFFFFFFFE, the TTL will not change
 * when a record is updated.
 */
#define AS_RECORD_NO_CHANGE_TTL 0xFFFFFFFE

/******************************************************************************
 *	MACROS
 *****************************************************************************/

/**
 * Initialize a stack allocated `as_record` then allocate `__nbins` capacity 
 * for as_record.bins on the stack.
 *
 *	~~~~~~~~~~{.c}
 *	as_record record;
 *	as_record_inita(&record, 2);
 *	as_record_set_int64(&record, "bin1", 123);
 *	as_record_set_int64(&record, "bin2", 456);
 *	~~~~~~~~~~
 *
 *	When you are finished using the `as_record` instance, you should release the 
 *	resources allocated to it by calling `as_record_destroy()`.
 *
 *	@param __rec		The `as_record *` to initialize.
 *	@param __nbins		The number of `as_record.bins.entries` to allocate on the 
 *						stack.
 *	
 *	@relates as_record
 */
#define as_record_inita(__rec, __nbins) \
	as_record_init(__rec, 0);\
	(__rec)->bins._free = false;\
	(__rec)->bins.capacity = (__nbins);\
	(__rec)->bins.size = 0;\
	(__rec)->bins.entries = (as_bin*) alloca(sizeof(as_bin) * (__nbins));

/******************************************************************************
 *	FUNCTIONS
 *****************************************************************************/

/**
 *	Create a new as_record on the heap.
 *
 *	~~~~~~~~~~{.c}
 *	as_record * r = as_record_new(2);
 *	as_record_set_int64(r, "bin1", 123);
 *	as_record_set_str(r, "bin1", "abc");
 *	~~~~~~~~~~
 *
 *	When you are finished using the `as_record` instance, you should release the 
 *	resources allocated to it by calling `as_record_destroy()`.
 *
 *	@param nbins 	The number of bins to initialize.
 *
 *	@return a pointer to the new as_record if successful, otherwise NULL.
 *
 *	@relates as_record
 */
as_record * as_record_new(uint16_t nbins);

/**
 *	Initializes an as_record created on the stack.
 *
 *	~~~~~~~~~~{.c}
 *	as_record r;
 *	as_record_init(&r, 2);
 *	as_record_set_int64(&r, "bin1", 123);
 *	as_record_set_str(&r, "bin1", "abc");
 *	~~~~~~~~~~
 *
 *	When you are finished using the `as_record` instance, you should release the 
 *	resources allocated to it by calling `as_record_destroy()`.
 *
 *	@param rec		The record to initialize.
 *	@param nbins	The number of bins to initialize.
 *
 *	@return a pointer to the initialized as_record if successful, otherwise NULL.
 *
 *	@relates as_record
 */
as_record * as_record_init(as_record * rec, uint16_t nbins);

/**
 *	Destroy the as_record and associated resources.
 *
 *	@param rec The record to destroy.
 *
 *	@relates as_record
 */
void as_record_destroy(as_record * rec);

/**
 *	Get the number of bins in the record.
 *
 *	@return the number of bins in the record.
 *
 *	@relates as_record
 */
uint16_t as_record_numbins(const as_record * rec);

/**
 *	Set specified bin's value to an as_bin_value.
 *
 *	@param rec		The record containing the bin.
 *	@param name		The name of the bin.
 *	@param value	The value of the bin.
 *
 *	@return true on success, false on failure.
 *
 *	@relates as_record
 */
bool as_record_set(as_record * rec, const as_bin_name name, as_bin_value * value);

/**
 *	Set specified bin's value to an int64_t.
 *
 *	~~~~~~~~~~{.c}
 *	as_record_set_int64(rec, "bin", 123);
 *	~~~~~~~~~~
 *
 *	@param rec		The record containing the bin.
 *	@param name		The name of the bin.
 *	@param value	The value of the bin.
 *
 *	@return true on success, false on failure.
 *
 *	@relates as_record
 */
bool as_record_set_int64(as_record * rec, const as_bin_name name, int64_t value);

/**
 *	Set specified bin's value to a double.
 *
 *	~~~~~~~~~~{.c}
 *	as_record_set_double(rec, "bin", 123.456);
 *	~~~~~~~~~~
 *
 *	@param rec		The record containing the bin.
 *	@param name		The name of the bin.
 *	@param value	The value of the bin.
 *
 *	@return true on success, false on failure.
 *
 *	@relates as_record
 */
bool as_record_set_double(as_record * rec, const as_bin_name name, double value);

/**
 *	Set specified bin's value to an NULL terminated string.
 *
 *	~~~~~~~~~~{.c}
 *	as_record_set_strp(rec, "bin", strdup("abc"), true);
 *	~~~~~~~~~~
 *
 *	@param rec		The record containing the bin.
 *	@param name		The name of the bin.
 *	@param value	The value of the bin. Must be in scope for the lifetime of the record.
 *	@param free		If true, then the value will be freed when the record is destroyed.
 *
 *	@return true on success, false on failure.
 *
 *	@relates as_record
 */
bool as_record_set_strp(as_record * rec, const as_bin_name name, const char * value, bool free);

/**
 *	Set specified bin's value to an NULL terminated string.
 *
 *	~~~~~~~~~~{.c}
 *	as_record_set_str(rec, "bin", "abc");
 *	~~~~~~~~~~
 *
 *	@param rec		The record containing the bin.
 *	@param name		The name of the bin.
 *	@param value	The value of the bin. Must be in scope for the lifetime of the record.
 *
 *	@return true on success, false on failure.
 *
 *	@relates as_record
 */
static inline bool as_record_set_str(as_record * rec, const as_bin_name name, const char * value)
{
	return as_record_set_strp(rec, name, value, false);
}

/**
 *	Set specified bin's value to an NULL terminated GeoJSON string.
 *
 *	~~~~~~~~~~{.c}
 *	as_record_set_geojson_strp(rec, "bin", strdup("abc"), true);
 *	~~~~~~~~~~
 *
 *	@param rec		The record containing the bin.
 *	@param name		The name of the bin.
 *	@param value	The value of the bin. Must be in scope for the lifetime of the record.
 *	@param free		If true, then the value will be freed when the record is destroyed.
 *
 *	@return true on success, false on failure.
 *
 *	@relates as_record
 */
bool as_record_set_geojson_strp(as_record * rec, const as_bin_name name, const char * value, bool free);

/**
 *	Set specified bin's value to an NULL terminated GeoJSON string.
 *
 *	~~~~~~~~~~{.c}
 *	as_record_set_geojson_str(rec, "bin", "abc");
 *	~~~~~~~~~~
 *
 *	@param rec		The record containing the bin.
 *	@param name		The name of the bin.
 *	@param value	The value of the bin. Must be in scope for the lifetime of the record.
 *
 *	@return true on success, false on failure.
 *
 *	@relates as_record
 */
static inline bool as_record_set_geojson_str(as_record * rec, const as_bin_name name, const char * value)
{
	return as_record_set_geojson_strp(rec, name, value, false);
}

/**
 *	Set specified bin's value to an NULL terminated string.
 *
 *	~~~~~~~~~~{.c}
 *	uint8_t * bytes = (uint8_t *) malloc(3);
 *	bytes[0] = 1;
 *	bytes[1] = 2;
 *	bytes[3] = 3;
 *
 *	as_record_set_raw(rec, "bin", bytes, 3, true);
 *	~~~~~~~~~~
 *
 *	@param rec		The record containing the bin.
 *	@param name		The name of the bin.
 *	@param value	The value of the bin. Must be in scope for the lifetime of the record.
 *	@param size		The size of the value.
 *	@param free		If true, then the value will be freed when the record is destroyed.
 *
 *	@return true on success, false on failure.
 *
 *	@relates as_record
 */
bool as_record_set_rawp(as_record * rec, const as_bin_name name, const uint8_t * value, uint32_t size, bool free);

/**
 *	Set specified bin's value to an as_bytes value of a specified type.
 *
 *	~~~~~~~~~~{.c}
 *	uint8_t * bytes = (uint8_t *) malloc(3);
 *	bytes[0] = 1;
 *	bytes[1] = 2;
 *	bytes[3] = 3;
 *
 *	as_record_set_raw(rec, "bin", bytes, 3, true);
 *	~~~~~~~~~~
 *
 *	@param rec		The record containing the bin.
 *	@param name		The name of the bin.
 *	@param value	The value of the bin. Must be in scope for the lifetime of the record.
 *	@param size		The size of the value.
 *	@param type 	The as_bytes_type designation (AS_BYTES_*)
 *	@param free		If true, then the value will be freed when the record is destroyed.
 *
 *	@return true on success, false on failure.
 *
 *	@relates as_record
 */
bool as_record_set_raw_typep(as_record * rec, const as_bin_name name, const uint8_t * value, uint32_t size, as_bytes_type type, bool free);

/**
 *	Set specified bin's value to an NULL terminated string.
 *
 *	~~~~~~~~~~{.c}
 *	uint8_t bytes[3] = {1,2,3};
 *	as_record_set_raw(rec, "bin", bytes, 3);
 *	~~~~~~~~~~
 *
 *	@param rec		The record containing the bin.
 *	@param name		The name of the bin.
 *	@param value	The value of the bin. Must be in scope for the lifetime of the record.
 *	@param size		The size of the value.
 *
 *	@return true on success, false on failure.
 *
 *	@relates as_record
 */
static inline bool as_record_set_raw(as_record * rec, const as_bin_name name, const uint8_t * value, uint32_t size)
{
	return as_record_set_rawp(rec, name, value, size, false);
}

/**
 *	Set specified bin's value to an as_integer.
 *
 *	~~~~~~~~~~{.c}
 *	as_record_set_integer(rec, "bin", as_integer_new(123));
 *	~~~~~~~~~~
 *
 *	@param rec		The record containing the bin.
 *	@param name		The name of the bin.
 *	@param value	The value of the bin. Must be in scope for the lifetime of the record.
 *
 *	@return true on success, false on failure.
 *
 *	@relates as_record
 */
bool as_record_set_integer(as_record * rec, const as_bin_name name, as_integer * value);

/**
 *	Set specified bin's value to an as_double.
 *
 *	~~~~~~~~~~{.c}
 *	as_record_set_as_double(rec, "bin", as_double_new(123.456));
 *	~~~~~~~~~~
 *
 *	@param rec		The record containing the bin.
 *	@param name		The name of the bin.
 *	@param value	The value of the bin. Must be in scope for the lifetime of the record.
 *
 *	@return true on success, false on failure.
 *
 *	@relates as_record
 */
bool as_record_set_as_double(as_record * rec, const as_bin_name name, as_double * value);

/**
 *	Set specified bin's value to an as_string.
 *
 *	~~~~~~~~~~{.c}
 *	as_record_set_string(rec, "bin", as_string_new("abc", false));
 *	~~~~~~~~~~
 *
 *	@param rec		The record containing the bin.
 *	@param name		The name of the bin.
 *	@param value	The value of the bin. Must be in scope for the lifetime of the record.
 *
 *	@return true on success, false on failure.
 *
 *	@relates as_record
 */
bool as_record_set_string(as_record * rec, const as_bin_name name, as_string * value);

/**
 *	Set specified bin's value to an as_geojson.
 *
 *	~~~~~~~~~~{.c}
 *	as_record_set_geojson(rec, "bin", as_geojson_new("abc", false));
 *	~~~~~~~~~~
 *
 *	@param rec		The record containing the bin.
 *	@param name		The name of the bin.
 *	@param value	The value of the bin. Must be in scope for the lifetime of the record.
 *
 *	@return true on success, false on failure.
 *
 *	@relates as_record
 */
bool as_record_set_geojson(as_record * rec, const as_bin_name name, as_geojson * value);

/**
 *	Set specified bin's value to an as_bytes.
 *
 *	~~~~~~~~~~{.c}
 *	as_record_set_integer(rec, "bin", bytes);
 *	~~~~~~~~~~
 *
 *	@param rec		The record containing the bin.
 *	@param name		The name of the bin.
 *	@param value	The value of the bin. Must be in scope for the lifetime of the record.
 *
 *	@return true on success, false on failure.
 *
 *	@relates as_record
 */
bool as_record_set_bytes(as_record * rec, const as_bin_name name, as_bytes * value);

/**
 *	Set specified bin's value to an as_list.
 *
 *	~~~~~~~~~~{.c}
 *	as_arraylist list;
 *	as_arraylist_init(&list);
 *	as_arraylist_add_int64(&list, 1);
 *	as_arraylist_add_int64(&list, 2);
 *	as_arraylist_add_int64(&list, 3);
 *
 *	as_record_set_list(rec, "bin", &list);
 *	~~~~~~~~~~
 *
 *	@param rec		The record containing the bin.
 *	@param name		The name of the bin.
 *	@param value	The value of the bin. Must be in scope for the lifetime of the record.
 *
 *	@return true on success, false on failure.
 *
 *	@relates as_record
 */
bool as_record_set_list(as_record * rec, const as_bin_name name, as_list * value);

/**
 *	Set specified bin's value to an as_map.
 *
 *	~~~~~~~~~~{.c}
 *	as_hashmap map;
 *	as_hashmap_init(&map, 32);
 *	as_stringmap_set_int64(&map, "a", 1);
 *	as_stringmap_set_int64(&map, "b", 2);
 *	as_stringmap_set_int64(&map, "c", 3);
 *
 *	as_record_set_map(rec, "bin", &map);
 *	~~~~~~~~~~
 *
 *	@param rec		The record containing the bin.
 *	@param name		The name of the bin.
 *	@param value	The value of the bin. Must be in scope for the lifetime of the record.
 *
 *	@return true on success, false on failure.
 *
 *	@relates as_record
 */
bool as_record_set_map(as_record * rec, const as_bin_name name, as_map * value);

/**
 *	Set specified bin's value to as_nil.
 *
 *	~~~~~~~~~~{.c}
 *	as_record_set_nil(rec, "bin");
 *	~~~~~~~~~~
 *
 *	@param rec		The record containing the bin.
 *	@param name		The name of the bin.
 *
 *	@return true on success, false on failure.
 *
 *	@relates as_record
 */
bool as_record_set_nil(as_record * rec, const as_bin_name name);

/**
 *	Get specified bin's value.
 *
 *	~~~~~~~~~~{.c}
 *	as_val * value = as_record_get(rec, "bin");
 *	~~~~~~~~~~
 *
 *	@param rec		The record containing the bin.
 *	@param name		The name of the bin.
 *
 *	@return the value if it exists, otherwise NULL.
 *
 *	@relates as_record
 */
as_bin_value * as_record_get(const as_record * rec, const as_bin_name name);

/**
 *	Get specified bin's value as an int64_t.
 *
 *	~~~~~~~~~~{.c}
 *	int64_t value = as_record_get_int64(rec, "bin", INT64_MAX);
 *	~~~~~~~~~~
 *
 *	@param rec		The record containing the bin.
 *	@param name		The name of the bin.
 *	@param fallback	The default value to use, if the bin doesn't exist or is not an integer.
 *
 *	@return the value if it exists, otherwise 0.
 *
 *	@relates as_record
 */
int64_t as_record_get_int64(const as_record * rec, const as_bin_name name, int64_t fallback);

/**
 *	Get specified bin's value as a double.
 *
 *	~~~~~~~~~~{.c}
 *	double value = as_record_get_double(rec, "bin", -1.0);
 *	~~~~~~~~~~
 *
 *	@param rec		The record containing the bin.
 *	@param name		The name of the bin.
 *	@param fallback	The default value to use, if the bin doesn't exist or is not an integer.
 *
 *	@return the value if it exists, otherwise 0.
 *
 *	@relates as_record
 */
double as_record_get_double(const as_record * rec, const as_bin_name name, double fallback);

/**
 *	Get specified bin's value as an NULL terminated string.
 *
 *	~~~~~~~~~~{.c}
 *	char * value = as_record_get_str(rec, "bin");
 *	~~~~~~~~~~
 *
 *	@param rec		The record containing the bin.
 *	@param name		The name of the bin.
 *
 *	@return the value if it exists, otherwise NULL.
 *
 *	@relates as_record
 */
char * as_record_get_str(const as_record * rec, const as_bin_name name);

/**
 *	Get specified bin's value as an NULL terminated GeoJSON string.
 *
 *	~~~~~~~~~~{.c}
 *	char * value = as_record_get_geojson_str(rec, "bin");
 *	~~~~~~~~~~
 *
 *	@param rec		The record containing the bin.
 *	@param name		The name of the bin.
 *
 *	@return the value if it exists, otherwise NULL.
 *
 *	@relates as_record
 */
char * as_record_get_geojson_str(const as_record * rec, const as_bin_name name);

/**
 *	Get specified bin's value as an as_integer.
 *
 *	~~~~~~~~~~{.c}
 *	as_integer * value = as_record_get_integer(rec, "bin");
 *	~~~~~~~~~~
 *
 *	@param rec		The record containing the bin.
 *	@param name		The name of the bin.
 *
 *	@return the value if it exists, otherwise NULL.
 *
 *	@relates as_record
 */
as_integer * as_record_get_integer(const as_record * rec, const as_bin_name name);

/**
 *	Get specified bin's value as an as_double.
 *
 *	~~~~~~~~~~{.c}
 *	as_double * value = as_record_get_as_double(rec, "bin");
 *	~~~~~~~~~~
 *
 *	@param rec		The record containing the bin.
 *	@param name		The name of the bin.
 *
 *	@return the value if it exists, otherwise NULL.
 *
 *	@relates as_record
 */
as_double * as_record_get_as_double(const as_record * rec, const as_bin_name name);

/**
 *	Get specified bin's value as an as_string.
 *
 *	~~~~~~~~~~{.c}
 *	as_string * value = as_record_get_string(rec, "bin");
 *	~~~~~~~~~~
 *
 *	@param rec		The record containing the bin.
 *	@param name		The name of the bin.
 *
 *	@return the value if it exists, otherwise NULL.
 *
 *	@relates as_record
 */
as_string * as_record_get_string(const as_record * rec, const as_bin_name name);

/**
 *	Get specified bin's value as an as_geojson.
 *
 *	~~~~~~~~~~{.c}
 *	as_geojson * value = as_record_get_geojson(rec, "bin");
 *	~~~~~~~~~~
 *
 *	@param rec		The record containing the bin.
 *	@param name		The name of the bin.
 *
 *	@return the value if it exists, otherwise NULL.
 *
 *	@relates as_record
 */
as_geojson * as_record_get_geojson(const as_record * rec, const as_bin_name name);

/**
 *	Get specified bin's value as an as_bytes.
 *
 *	~~~~~~~~~~{.c}
 *	as_bytes * value = as_record_get_bytes(rec, "bin");
 *	~~~~~~~~~~
 *
 *	@param rec		The record containing the bin.
 *	@param name		The name of the bin.
 *
 *	@return the value if it exists, otherwise NULL.
 *
 *	@relates as_record
 */
as_bytes * as_record_get_bytes(const as_record * rec, const as_bin_name name);

/**
 *	Get specified bin's value as an as_list.
 *
 *	~~~~~~~~~~{.c}
 *	as_list * value = as_record_get_list(rec, "bin");
 *	~~~~~~~~~~
 *
 *	@param rec		The record containing the bin.
 *	@param name		The name of the bin.
 *
 *	@return the value if it exists, otherwise NULL.
 *
 *	@relates as_record
 */
as_list * as_record_get_list(const as_record * rec, const as_bin_name name);

/**
 *	Get specified bin's value as an as_map.
 *
 *	~~~~~~~~~~{.c}
 *	as_map * value = as_record_get_map(rec, "bin");
 *	~~~~~~~~~~
 *
 *	@param rec		The record containing the bin.
 *	@param name		The name of the bin.
 *
 *	@return the value if it exists, otherwise NULL.
 *
 *	@relates as_record
 */
as_map * as_record_get_map(const as_record * rec, const as_bin_name name);

/******************************************************************************
 *	ITERATION FUNCTIONS
 ******************************************************************************/

/**
 *	Iterate over each bin in the record and invoke the callback function.
 *	
 *	~~~~~~~~~~{.c}
 *	bool print_bin(const char * name, const as_val * value, void * udata) {
 *		char * sval = as_val_tostring(value);
 *		printf("bin: name=%s, value=%s\n", name, sval);
 *		free(sval);
 *		return true;
 *	}
 *
 *	as_record_foreach(rec, print_bin, NULL);
 *	~~~~~~~~~~
 *
 *	If the callback returns true, then iteration will continue to the next bin.
 *	Otherwise, the iteration will halt and `as_record_foreach()` will return
 *	false.
 *
 *	@param rec		The record containing the bins to iterate over.
 *	@param callback	The callback to invoke for each bin.
 *	@param udata	User-data provided for the callback.
 *
 *	@return true if iteration completes fully. false if iteration was aborted.
 *
 *	@relates as_record
 */
bool as_record_foreach(const as_record * rec, as_rec_foreach_callback callback, void * udata);

/******************************************************************************
 *	CONVERSION FUNCTIONS
 ******************************************************************************/

/**
 *	Convert to an as_val.
 *
 *	@relates as_record
 */
static inline as_val * as_record_toval(const as_record * rec) 
{
	return (as_val *) rec;
}

/**
 *	Convert from an as_val.
 *
 *	@relates as_record
 */
static inline as_record * as_record_fromval(const as_val * v) 
{
	return (as_record *) as_util_fromval(v, AS_REC, as_rec);
}

#ifdef __cplusplus
} // end extern "C"
#endif
