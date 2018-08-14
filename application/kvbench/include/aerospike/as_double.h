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

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *	TYPES
 ******************************************************************************/

/**
 *	Container for double values.
 *
 *	## Initialization
 *	
 *	An as_double should be initialized via one of the provided function.
 *	- as_double_init()
 *	- as_double_new()
 *
 *	To initialize a stack allocated as_double, use as_double_init():
 *
 *	~~~~~~~~~~{.c}
 *	as_double v;
 *	as_double_init(&v, 100);
 *	~~~~~~~~~~
 *
 *	To create and initialize a heap allocated as_double, use as_double_new():
 *	
 *	~~~~~~~~~~{.c}
 *	as_double* v = as_double_new(100);
 *	~~~~~~~~~~
 *
 *	## Destruction
 *
 *	When the as_double instance is no longer required, then you should
 *	release the resources associated with it via as_double_destroy():
 *
 *	~~~~~~~~~~{.c}
 *	as_double_destroy(v);
 *	~~~~~~~~~~
 *	
 *	## Usage
 *
 *	There are two functions for getting the boxed value contained by 
 *	as_double:
 *
 *	as_double_get() returns the contained value. If an error occurred, then
 *	0 (zero) is returned. Possible errors is the as_double instance is NULL.
 *
 *	~~~~~~~~~~{.c}
 *	double v = as_double_get(i);
 *	~~~~~~~~~~
 *
 *	as_double_getorelse() allows you to return a default value if an error 
 *	occurs:
 *
 *	~~~~~~~~~~{.c}
 *	double v = as_double_getorelse(i, -1.0);
 *	~~~~~~~~~~
 *
 *	## Conversions
 *
 *	as_double is derived from as_val, so it is generally safe to down cast:
 *
 *	~~~~~~~~~~{.c}
 *	as_val val = (as_val)v;
 *	~~~~~~~~~~
 *	
 *	However, upcasting is more error prone. When doing so, you should use 
 *	as_double_fromval(). If conversion fails, then the return value is NULL.
 *
 *	~~~~~~~~~~{.c}
 *	as_double* i = as_double_fromval(val);
 *	~~~~~~~~~~
 *
 *	@extends as_val
 *	@ingroup aerospike_t
 */
typedef struct as_double_s {

	/**
	 *	@private
	 *	as_double is a subtype of as_val.
	 */
	as_val  _;

	/**
	 *	The double value
	 */
	double value;

} as_double;

/******************************************************************************
 *	FUNCTIONS
 ******************************************************************************/

/**
 *	Initialize a stack allocated `as_double` with the given double value.
 *
 *	~~~~~~~~~~{.c}
 *	as_double v;
 *	as_double_init(&v, 123.45);
 *	~~~~~~~~~~
 *
 *	When the `as_double` is no longer needed, you should release it an it's 
 *	resources:
 *
 *	~~~~~~~~~~{.c}
 *	as_double_destroy(&i);
 *	~~~~~~~~~~
 *
 *	@param value_ptr	The `as_double` to initialize.
 *	@param value		The double value.
 *
 *	@return On success, the initialized value. Otherwise NULL.
 *
 *	@relatesalso as_double
 */
as_double*
as_double_init(as_double* value_ptr, double value);

/**
 *	Creates a new heap allocated as_double.
 *
 *	~~~~~~~~~~{.c}
 *	as_double* v = as_double_new(123.45);
 *	~~~~~~~~~~
 *
 *	When the `as_double` is no longer needed, you should release it an it's 
 *	resources:
 *
 *	~~~~~~~~~~{.c}
 *	as_double_destroy(&v);
 *	~~~~~~~~~~
 *
 *	@param value		The double value.
 *
 *	@return On success, the initialized value. Otherwise NULL.
 *
 *	@relatesalso as_double
 */
as_double*
as_double_new(double value);

/**
 *	Destroy the `as_double` and release resources.
 *
 *	~~~~~~~~~~{.c}
 *	as_double_destroy(v);
 *	~~~~~~~~~~
 *
 *	@param value	The double to destroy.
 *
 *	@relatesalso as_double
 */
static inline void
as_double_destroy(as_double* value)
{
	as_val_destroy((as_val*)value);
}

/******************************************************************************
 *	VALUE FUNCTIONS
 ******************************************************************************/

/**
 *	Get the double value. If double is NULL, then return the fallback value.
 *
 *	@relatesalso as_double
 */
static inline double
as_double_getorelse(const as_double* value, double fallback)
{
	return value ? value->value : fallback;
}

/**
 *	Get the double value.
 *
 *	@relatesalso as_double
 */
static inline double
as_double_get(const as_double* value)
{
	return as_double_getorelse(value, 0.0);
}

/******************************************************************************
 *	CONVERSION FUNCTIONS
 ******************************************************************************/

/**
 *	Convert to an as_val.
 *
 *	@relatesalso as_double
 */
static inline as_val*
as_double_toval(const as_double* value)
{
	return (as_val*)value;
}

/**
 *	Convert from an as_val.
 *
 *	@relatesalso as_double
 */
static inline as_double*
as_double_fromval(const as_val* value)
{
	return as_util_fromval(value, AS_DOUBLE, as_double);
}

/******************************************************************************
 *	as_val FUNCTIONS
 ******************************************************************************/

/**
 *	@private
 *	Internal helper function for destroying an as_val.
 */
void
as_double_val_destroy(as_val* value);

/**
 *	@private
 *	Internal helper function for getting the hashcode of an as_val.
 */
uint32_t
as_double_val_hashcode(const as_val* value);

/**
 *	@private
 *	Internal helper function for getting the string representation of an as_val.
 */
char*
as_double_val_tostring(const as_val* value);

#ifdef __cplusplus
} // end extern "C"
#endif
