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

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *	TYPES
 ******************************************************************************/

/**
 *	Container for integer values.
 *
 *	## Initialization
 *	
 *	An as_integer should be initialized via one of the provided function.
 *	- as_integer_init()
 *	- as_integer_new()
 *
 *	To initialize a stack allocated as_integer, use as_integer_init():
 *
 *	~~~~~~~~~~{.c}
 *	as_integer i;
 *	as_integer_init(&i, 100);
 *	~~~~~~~~~~
 *
 *	To create and initialize a heap allocated as_integer, use as_integer_new():
 *	
 *	~~~~~~~~~~{.c}
 *	as_integer * i = as_integer_new(100);
 *	~~~~~~~~~~
 *
 *	## Destruction
 *
 *	When the as_integer instance is no longer required, then you should
 *	release the resources associated with it via as_integer_destroy():
 *
 *	~~~~~~~~~~{.c}
 *	as_integer_destroy(i);
 *	~~~~~~~~~~
 *	
 *	## Usage
 *
 *	There are two functions for getting the boxed value contained by 
 *	as_integer:
 *
 *	as_integer_get() returns the contained value. If an error occurred, then
 *	0 (zero) is returned. Possible errors is the as_integer instance is NULL.
 *
 *	~~~~~~~~~~{.c}
 *	int64_t ival = as_integer_get(i);
 *	~~~~~~~~~~
 *
 *	as_integer_getorelse() allows you to return a default value if an error 
 *	occurs:
 *
 *	~~~~~~~~~~{.c}
 *	int64_t ival = as_integer_getorelse(i, -1);
 *	~~~~~~~~~~
 *
 *	## Conversions
 *
 *	as_integer is derived from as_val, so it is generally safe to down cast:
 *
 *	~~~~~~~~~~{.c}
 *	as_val val = (as_val) i;
 *	~~~~~~~~~~
 *	
 *	However, upcasting is more error prone. When doing so, you should use 
 *	as_integer_fromval(). If conversion fails, then the return value is NULL.
 *
 *	~~~~~~~~~~{.c}
 *	as_integer * i = as_integer_fromval(val);
 *	~~~~~~~~~~
 *
 *
 *
 *	@extends as_val
 *	@ingroup aerospike_t
 */
typedef struct as_integer_s {

	/**
	 *	@private
	 *	as_boolean is a subtype of as_val.
	 *	You can cast as_boolean to as_val.
	 */
	as_val  _;

	/**
	 *	The integer value
	 */
	int64_t value;

} as_integer;

/******************************************************************************
 *	FUNCTIONS
 ******************************************************************************/

/**
 *	Initialize a stack allocated `as_integer` with the given integer value.
 *
 *	~~~~~~~~~~{.c}
 *	as_integer i;
 *	as_integer_init(&i, 123);
 *	~~~~~~~~~~
 *
 *	When the `as_integer` is no longer needed, you should release it an it's 
 *	resources:
 *
 *	~~~~~~~~~~{.c}
 *	as_integer_destroy(&i);
 *	~~~~~~~~~~
 *
 *	@param integer		The `as_integer` to initialize.
 *	@param value		The integer value.
 *
 *	@return On success, the initialized value. Otherwise NULL.
 *
 *	@relatesalso as_integer
 */
as_integer * as_integer_init(as_integer * integer, int64_t value);

/**
 *	Creates a new heap allocated as_integer.
 *
 *	~~~~~~~~~~{.c}
 *	as_integer * i = as_integer_new(123);
 *	~~~~~~~~~~
 *
 *	When the `as_integer` is no longer needed, you should release it an it's 
 *	resources:
 *
 *	~~~~~~~~~~{.c}
 *	as_integer_destroy(&i);
 *	~~~~~~~~~~
 *
 *	@param value		The integer value.
 *
 *	@return On success, the initialized value. Otherwise NULL.
 *
 *	@relatesalso as_integer
 */
as_integer * as_integer_new(int64_t value);

/**
 *	Destroy the `as_integer` and release resources.
 *
 *	~~~~~~~~~~{.c}
 *	as_integer_destroy(i);
 *	~~~~~~~~~~
 *
 *	@param integer	The integer to destroy.
 *
 *	@relatesalso as_integer
 */
static inline void as_integer_destroy(as_integer * integer) {
	as_val_destroy((as_val *) integer);
}

/******************************************************************************
 *	VALUE FUNCTIONS
 ******************************************************************************/

/**
 *	Get the int64_t value. If integer is NULL, then return the fallback value.
 *
 *	@relatesalso as_integer
 */
static inline int64_t as_integer_getorelse(const as_integer * integer, int64_t fallback) {
	return integer ? integer->value : fallback;
}

/**
 *	Get the int64_t value.
 *
 *	@relatesalso as_integer
 */
static inline int64_t as_integer_get(const as_integer * integer) {
	return as_integer_getorelse(integer, 0);
}

/**
 *	Get the int64_t value.
 *	@deprecated Use as_integer_get() instead.
 *
 *	@relatesalso as_integer
 */
static inline int64_t as_integer_toint(const as_integer * integer) {
	return as_integer_getorelse(integer, 0);
}

/******************************************************************************
 *	CONVERSION FUNCTIONS
 ******************************************************************************/

/**
 *	Convert to an as_val.
 *
 *	@relatesalso as_integer
 */
static inline as_val * as_integer_toval(const as_integer * i) {
	return (as_val *) i;
}

/**
 *	Convert from an as_val.
 *
 *	@relatesalso as_integer
 */
static inline as_integer * as_integer_fromval(const as_val * v) {
	return as_util_fromval(v, AS_INTEGER, as_integer);
}

/******************************************************************************
 *	as_val FUNCTIONS
 ******************************************************************************/

/**
 *	@private
 *	Internal helper function for destroying an as_val.
 */
void as_integer_val_destroy(as_val * v);

/**
 *	@private
 *	Internal helper function for getting the hashcode of an as_val.
 */
uint32_t as_integer_val_hashcode(const as_val * v);

/**
 *	@private
 *	Internal helper function for getting the string representation of an as_val.
 */
char * as_integer_val_tostring(const as_val * v);

#ifdef __cplusplus
} // end extern "C"
#endif
