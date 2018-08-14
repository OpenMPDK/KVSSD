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
 *	MACROS
 ******************************************************************************/

#define pair_new(a,b) as_pair_new((as_val *) a, (as_val *) b)

/******************************************************************************
 *	TYPES
 ******************************************************************************/

/**
 *	A Pair of values: (_1,_2)
 *	@ingroup aerospike_t
 */
typedef struct as_pair_s {

	/**
	 *	@private
	 *	as_pair is a subtype of as_val.
	 *	You can cast as_pair to as_val.
	 */
	as_val _;

	/**
	 *	The first value of the pair.
	 */
	as_val * _1;

	/**
	 *	The second value of the pair.
	 */
	as_val * _2;

} as_pair;

/******************************************************************************
 *	INSTANCE FUNCTIONS
 ******************************************************************************/

/**
 *	Create and initializes a new heap allocated `as_pair`.
 *
 *	@param _1	The first value.
 *	@param _2	The second value.
 *
 *	@return On success, the new pair. Otherwise NULL.
 *
 *	@relatesalso as_pair
 */
as_pair * as_pair_new(as_val * _1, as_val * _2);

/**
 *	Initializes a stack allocated `as_pair`.
 *
 *	@param pair	The pair to initialize.
 *	@param _1	The first value.
 *	@param _2	The second value.
 *
 *	@return On success, the new pair. Otherwise NULL.
 *
 *	@relatesalso as_pair
 */
as_pair * as_pair_init(as_pair * pair, as_val * _1, as_val * _2);

/**
 *	Destroy the `as_pair` and release associated resources.
 *
 *	@relatesalso as_pair
 */
static inline void as_pair_destroy(as_pair * pair)
{
	as_val_destroy((as_val *) pair);
}

/******************************************************************************
 *	VALUE FUNCTIONS
 ******************************************************************************/

/**
 *	Get the first value of the pair
 *
 *	@relatesalso as_pair
 */
static inline as_val * as_pair_1(as_pair * pair) 
{
	return pair ? pair->_1 : NULL;
}

/**
 *	Get the second value of the pair
 */
static inline as_val * as_pair_2(as_pair * pair) 
{
	return pair ? pair->_2 : NULL;
}

/******************************************************************************
 *	CONVERSION FUNCTIONS
 *****************************************************************************/

/**
 *	Convert to an as_val.
 *
 *	@relatesalso as_pair
 */
static inline as_val * as_pair_toval(const as_pair * pair) 
{
	return (as_val *) pair;
}

/**
 *	Convert from an as_val.
 *
 *	@relatesalso as_pair
 */
static inline as_pair * as_pair_fromval(const as_val * v) 
{
	return as_util_fromval(v, AS_PAIR, as_pair);
}

/******************************************************************************
 *	as_val FUNCTIONS
 *****************************************************************************/

/**
 *	@private
 *	Internal helper function for destroying an as_val.
 */
void as_pair_val_destroy(as_val *);

/**
 *	@private
 *	Internal helper function for getting the hashcode of an as_val.
 */
uint32_t as_pair_val_hashcode(const as_val *);

/**
 *	@private
 *	Internal helper function for getting the string representation of an as_val.
 */
char * as_pair_val_tostring(const as_val *);

#ifdef __cplusplus
} // end extern "C"
#endif
