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

#include <citrusleaf/cf_atomic.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *	TYPES
 *****************************************************************************/

/**
 *	as_val types
 */
typedef enum as_val_t {
	AS_UNDEF		= 0,
    AS_UNKNOWN      = 0,	//<! @deprecated
    AS_NIL          = 1,
    AS_BOOLEAN      = 2,
    AS_INTEGER      = 3,
    AS_STRING       = 4,
    AS_LIST         = 5,
    AS_MAP          = 6,
    AS_REC          = 7,
    AS_PAIR         = 8,
    AS_BYTES        = 9,
	AS_DOUBLE       = 10,
    AS_GEOJSON      = 11,
    AS_VAL_T_MAX
} __attribute__((packed)) as_val_t;

/**
 *	Represents a value
 *	@ingroup aerospike_t
 */
typedef struct as_val_s {

	/**
	 *	Value type
	 */
    enum as_val_t type;

    /**
     *	Value can be freed.
     *	Should be false for stack allocated values.
     */
    bool free;

    /**
     *	Reference count
     *	Values are ref counted.
     *	To increment the count, use `as_val_reserve()`
     */
    cf_atomic32 count;

} as_val;

/******************************************************************************
 *	MACROS
 *****************************************************************************/
 
/**
 *	Returns the `as_val.type` of a value.
 *
 *	@param __v 	The `as_val` to get the type of
 *
 *	@return An as_val_t value. If the type is unknown, then it will 
 *	be AS_UNKNOWN.
 */
#define as_val_type(__v) (__v ? ((as_val *)__v)->type : AS_UNDEF)

/**
 *	Increment the `as_val.count` of a value.
 *	
 *	@param __v	The `as_val` to be incremented.
 *
 *	@return	The value, with it's refcount incremented.
 */
#define as_val_reserve(__v) ( as_val_val_reserve((as_val *)__v) )

/**
 *	Decrement the `as_val.count` of a value. If `as_val.count` reaches 0 (zero) and
 *	`as_val.free` is true, then free the `as_val` instance.
 *
 *	@param __v 	The `as_val` to be decremented.
 *
 *	@return The value, if its `as_val.count` > 0. Otherwise NULL.
 */
#define as_val_destroy(__v) ( as_val_val_destroy((as_val *)__v) )

/**
 *	Get the hashcode value for the value.
 *
 *	@param __v 	The `as_val` to get the hashcode value for.
 *
 *	@return The hashcode value.
 */
#define as_val_hashcode(__v) ( as_val_val_hashcode((as_val *)__v) )

/**
 *	Get the string representation of the value.
 *
 *	@param __v 	The `as_val` to get the string value for.
 *
 *	@return The string representation on success. Otherwise NULL.
 */
#define as_val_tostring(__v) ( as_val_val_tostring((as_val *)__v) )

/******************************************************************************
 *	FUNCTIONS
 *****************************************************************************/

/**
 *	@private
 *	Helper function for incrementing the count of a value.
 */
as_val * as_val_val_reserve(as_val *);

/**
 *	@private
 *	Helper function for decrementing the count of a value,
 *	and if count==0 and free==true, then free the value.
 */
as_val * as_val_val_destroy(as_val *);

/**
 *	@private
 *	Helper function for calculating the hash value.
 */
uint32_t as_val_val_hashcode(const as_val *);

/**
 *	@private
 *	Helper function for generating the string representation.
 */
char * as_val_val_tostring(const as_val *);

/******************************************************************************
 *	INSTANCE FUNCTIONS
 *****************************************************************************/

/**
 *	@private
 *	Initialize an as_val. 
 *	Should only be used by subtypes.
 *	@deprecated Use as_val_cons() instead.
 */
static inline void as_val_init(as_val * v, as_val_t type, bool free) 
{
    v->type = type; 
    v->free = free; 
    v->count = 1;
}


/**
 *	@private
 *	Initialize an as_val. 
 *	Should only be used by subtypes.
 */
static inline as_val * as_val_cons(as_val * val, as_val_t type, bool free) 
{
	if ( !val ) return val;

    val->type = type; 
    val->free = free; 
    val->count = 1;
    return val;
}

#ifdef __cplusplus
} // end extern "C"
#endif
