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

/**
 *	Boolean value.
 *
 *	To use the boolean value, you should use one of the two constants:
 *
 *		as_boolean as_true;
 *		as_boolean as_false;
 *	
 *	Both `as_boolean_init()` and `as_boolean_new()` should be used sparingly.
 *
 *	@extends as_val
 *	@ingroup aerospike_t
 */
typedef struct as_boolean_s {

	/**
	 *	@private
	 *	as_boolean is a subtype of as_val.
	 *	You can cast as_boolean to as_val.
	 */
	as_val _;

	/**
	 *	The boolean value.
	 */
	bool value;

} as_boolean;

/******************************************************************************
 *	CONSTANTS
 *****************************************************************************/

/**
 *	True value.
 *
 *	Use this when you need to use an `as_boolean` containing `true`,
 *	rather than allocating a new `as_boolean`.
 */
extern const as_boolean as_true;

/**
 *	False value.
 *
 *	Use this when you need to use an `as_boolean` containing `true`,
 *	rather than allocating a new `as_boolean`.
 */
extern const as_boolean as_false;

/******************************************************************************
 *	INSTANCE FUNCTIONS
 ******************************************************************************/

/**
 *	Initialize a stack allocated `as_boolean` with the given boolean value.
 *
 *	@param boolean	The `as_boolean` to initialize.
 *	@param value	The bool value.
 *
 *	@return On success, the initialized value. Otherwise NULL.
 *
 *	@relatesalso as_boolean
 */
as_boolean * as_boolean_init(as_boolean * boolean, bool value);

/**
 *	Creates a new heap allocated `as_boolean` and initializes with
 *	the given boolean value.
 *
 *	@param value	The bool value.
 *
 *	@return On success, the newly allocated value. Otherwise NULL.
 *
 *	@relatesalso as_boolean
 */
as_boolean * as_boolean_new(bool value);

/**
 *	Destroy the `as_boolean` and release associated resources.
 *
 *	@param boolean 	The `as_boolean` to destroy.
 *
 *	@relatesalso as_boolean
 */
static inline void as_boolean_destroy(as_boolean * boolean) {
	as_val_destroy((as_val *) boolean);
}

/******************************************************************************
 *	VALUE FUNCTIONS
 ******************************************************************************/

/**
 *	Get the bool value. If boolean is NULL, then return the fallback value.
 *
 *	@relatesalso as_boolean
 */
static inline bool as_boolean_getorelse(const as_boolean * boolean, bool fallback) {
	return boolean ? boolean->value : fallback;
}

/**
 *	Get the bool value.
 *
 *	@relatesalso as_boolean
 */
static inline bool as_boolean_get(const as_boolean * boolean) {
	return as_boolean_getorelse(boolean, false);
}

/**
 *	Get the bool value.
 *	@deprecated Use as_boolean_get() instead.
 *
 *	@relatesalso as_boolean
 */
static inline bool as_boolean_tobool(const as_boolean * boolean) {
	return as_boolean_getorelse(boolean, false);
}

/******************************************************************************
 *	CONVERSION FUNCTIONS
 *****************************************************************************/

/**
 *	Convert to an as_val.
 *
 *	@relatesalso as_boolean
 */
static inline as_val * as_boolean_toval(const as_boolean * boolean) {
	return (as_val *) boolean;
}

/**
 *	Convert from an as_val.
 *
 *	@relatesalso as_boolean
 */
static inline as_boolean * as_boolean_fromval(const as_val * v) {
	return as_util_fromval(v, AS_BOOLEAN, as_boolean);
}

/******************************************************************************
 *	as_val FUNCTIONS
 *****************************************************************************/

/**
 *	@private
 *	Internal helper function for destroying an as_val.
 */
void as_boolean_val_destroy(as_val * v);

/**
 *	@private
 *	Internal helper function for getting the hashcode of an as_val.
 */
uint32_t as_boolean_val_hashcode(const as_val * v);

/**
 *	@private
 *	Internal helper function for getting the string representation of an as_val.
 */
char * as_boolean_val_tostring(const as_val * v);

#ifdef __cplusplus
} // end extern "C"
#endif
