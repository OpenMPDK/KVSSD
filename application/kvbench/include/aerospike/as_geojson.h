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
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *	TYPES
 ******************************************************************************/

/**
 *	Container for NULL-terminates GeoJSON string values.
 *
 *	## Initialization
 *	
 *	An as_geojson should be initialized via one of the provided function.
 *	- as_geojson_init()
 *	- as_geojson_new()
 *
 *	To initialize a stack allocated as_geojson, use as_geojson_init():
 *
 *	~~~~~~~~~~{.c}
 *	as_geojson s;
 *	as_geojson_init(&s, "abc", false);
 *	~~~~~~~~~~
 *
 *	The 3rd argument indicates whether the string value should be `free()`d 
 *	when as_geojson is destroyed.
 *
 *	To create and initialize a heap allocated as_integer, use as_integer_new():
 *	
 *	~~~~~~~~~~{.c}
 *	as_geojson * s = as_geojson_new("abc", false);
 *	~~~~~~~~~~
 *
 *	## Destruction
 *
 *	When the as_geojson instance is no longer required, then you should
 *	release the resources associated with it via as_geojson_destroy():
 *
 *	~~~~~~~~~~{.c}
 *	as_geojson_destroy(s);
 *	~~~~~~~~~~
 *	
 *	## Usage
 *
 *	There are two functions for getting the boxed value contained by 
 *	as_geojson:
 *
 *	as_geojson_get() returns the contained value. If an error occurred, then
 *	NULL is returned. Possible errors is the as_integer instance is NULL.
 *
 *	~~~~~~~~~~{.c}
 *	char * sval = as_geojson_get(i);
 *	~~~~~~~~~~
 *
 *	as_geojson_getorelse() allows you to return a default value if an error 
 *	occurs:
 *
 *	~~~~~~~~~~{.c}
 *	char * sval = as_geojson_getorelse(i, "oops!");
 *	~~~~~~~~~~
 *
 *	## Conversions
 *
 *	as_geojson is derived from as_val, so it is generally safe to down cast:
 *
 *	~~~~~~~~~~{.c}
 *	as_val val = (as_val) s;
 *	~~~~~~~~~~
 *	
 *	However, upcasting is more error prone. When doing so, you should use 
 *	as_geojson_fromval(). If conversion fails, then the return value is NULL.
 *
 *	~~~~~~~~~~{.c}
 *	as_geojson * i = as_geojson_fromval(val);
 *	~~~~~~~~~~
 *
 *	@extends as_val
 *	@ingroup aerospike_t
 */
typedef struct as_geojson_s {
	
	/**
	 *	@private
	 *	as_boolean is a subtype of as_val.
	 *	You can cast as_boolean to as_val.
	 */
	as_val _;

	/**
	 *	If true, then `as_geojson.value` can be freed.
	 */
	bool free;

	/**
	 *	The string value.
	 */
	char * value;

	/**
	 *	The length of the string.
	 */
	size_t len;

} as_geojson;

/******************************************************************************
 *	INSTANCE FUNCTIONS
 ******************************************************************************/

/**
 *	Initialize a stack allocated `as_geojson`.
 *
 *	If free is true, then the string value will be freed when the as_geojson is destroyed.
 *
 *	@param string	The stack allocated as_geojson to initialize
 *	@param value	The NULL terminated string of character.
 *	@param free		If true, then the value will be freed when as_geojson is destroyed.
 *
 *	@return On success, the initialized string. Otherwise NULL.
 *
 *	@relatesalso as_geojson
 */
as_geojson * as_geojson_init(as_geojson * string, char * value, bool free);

/**
 *	Initialize a stack allocated `as_geojson` and its length.
 *
 *	If free is true, then the string value will be freed when the as_geojson is destroyed.
 *
 *	@param string	The stack allocated as_geojson to initialize
 *	@param value	The NULL terminated string of character.
 *	@param len		The length of the string.
 *	@param free		If true, then the value will be freed when as_geojson is destroyed.
 *
 *	@return On success, the initialized string. Otherwise NULL.
 *
 *	@relatesalso as_geojson
 */
as_geojson * as_geojson_init_wlen(as_geojson * string, char * value, size_t len, bool free);

/**
 *	Create and initialize a new heap allocated `as_geojson`.
 *
 *	If free is true, then the string value will be freed when the as_geojson is destroyed.
 *
 *	@param value	The NULL terminated string of character.
 *	@param free		If true, then the value will be freed when as_geojson is destroyed.
 *
 *	@return On success, the new string. Otherwise NULL.
 *
 *	@relatesalso as_geojson
 */
as_geojson * as_geojson_new(char * value, bool free);

/**
 *	Create and initialize a new heap allocated `as_geojson` and its length.
 *
 *	If free is true, then the string value will be freed when the as_geojson is destroyed.
 *
 *	@param value	The NULL terminated string of character.
 *	@param len		The length of the string.
 *	@param free		If true, then the value will be freed when as_geojson is destroyed.
 *
 *	@return On success, the new string. Otherwise NULL.
 *
 *	@relatesalso as_geojson
 */
as_geojson * as_geojson_new_wlen(char * value, size_t len, bool free);

/**
 *	Create and initialize a new heap allocated `as_geojson`.
 *
 *	Value is cf_strdup()'d and will be freed when the as_geojson is destroyed.
 *
 *	@param value	The NULL terminated string of character.
 *
 *	@return On success, the new string. Otherwise NULL.
 */
as_geojson * as_geojson_new_strdup(const char * value);

/**
 *	Destroy the as_geojson and associated resources.
 *
 *	@relatesalso as_geojson
 */
static inline void as_geojson_destroy(as_geojson * string) 
{
	as_val_destroy((as_val *) string);
}

/******************************************************************************
 *	VALUE FUNCTIONS
 ******************************************************************************/

/**
 *	The length of the string
 *
 *	@param string The string to get the length of. 
 *
 *	@return the length of the string in bytes.
 *
 *	@relatesalso as_geojson
 */
size_t as_geojson_len(as_geojson * string);

/**
 *	Get the string value. If string is NULL, then return the fallback value.
 *
 *	@relatesalso as_geojson
 */
static inline char * as_geojson_getorelse(const as_geojson * string, char * fallback) 
{
	return string ? string->value : fallback;
}

/**
 *	Get the string value.
 *
 *	@relatesalso as_geojson
 */
static inline char * as_geojson_get(const as_geojson * string) 
{
	return as_geojson_getorelse(string, NULL);
}

/******************************************************************************
 *	CONVERSION FUNCTIONS
 ******************************************************************************/

/**
 *	Convert to an as_val.
 *
 *	@relatesalso as_geojson
 */
static inline as_val * as_geojson_toval(const as_geojson * s) 
{
	return (as_val *) s;
}

/**
 *	Convert from an as_val.
 *
 *	@relatesalso as_geojson
 */
static inline as_geojson * as_geojson_fromval(const as_val * v) 
{
	return as_util_fromval(v, AS_GEOJSON, as_geojson);
}

/******************************************************************************
 *	as_val FUNCTIONS
 ******************************************************************************/

/**
 *	@private
 *	Internal helper function for destroying an as_val.
 */
void as_geojson_val_destroy(as_val * v);

/**
 *	@private
 *	Internal helper function for getting the hashcode of an as_val.
 */
uint32_t as_geojson_val_hashcode(const as_val * v);

/**
 *	@private
 *	Internal helper function for getting the string representation of an as_val.
 */
char * as_geojson_val_tostring(const as_val * v);

#ifdef __cplusplus
} // end extern "C"
#endif
