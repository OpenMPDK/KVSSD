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

/**
 *	as_stringmap provides a convenience interface for populating a map with
 *	string keys.
 *
 *	@addtogroup stringmap_t StringMap
 *	@{
 */

#pragma once

#include <aerospike/as_util.h>
#include <aerospike/as_val.h>
#include <aerospike/as_integer.h>
#include <aerospike/as_string.h>
#include <aerospike/as_bytes.h>
#include <aerospike/as_list.h>
#include <aerospike/as_map.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *	SETTER FUNCTIONS
 *****************************************************************************/

/**
 *	Set the specified key's value to an as_val.
 */
static inline int as_stringmap_set(as_map * m, const char * k, as_val * v) 
{
	return as_util_hook(set, 1, m, (as_val *) as_string_new_strdup(k), v);
}

/**
 *	Set the specified key's value to an int64_t.
 */
static inline int as_stringmap_set_int64(as_map * m, const char * k, int64_t v) 
{
	return as_util_hook(set, 1, m, (as_val *) as_string_new_strdup(k), (as_val *) as_integer_new(v));
}

/**
 *	Set the specified key's value to a double.
 */
static inline int as_stringmap_set_double(as_map * m, const char * k, double v)
{
	return as_util_hook(set, 1, m, (as_val *) as_string_new_strdup(k), (as_val *) as_double_new(v));
}

/**
 *	Set the specified key's value to a NULL terminated string.
 */
static inline int as_stringmap_set_str(as_map * m, const char * k, const char * v) 
{
	return as_util_hook(set, 1, m, (as_val *) as_string_new_strdup(k), (as_val *) as_string_new_strdup(v));
}

/**
 *	Set the specified key's value to an as_integer.
 */
static inline int as_stringmap_set_integer(as_map * m, const char * k, as_integer * v) 
{
	return as_util_hook(set, 1, m, (as_val *) as_string_new_strdup(k), (as_val *) v);
}

/**
 *	Set the specified key's value to an as_integer.
 */
static inline int as_stringmap_set_as_double(as_map * m, const char * k, as_double * v)
{
	return as_util_hook(set, 1, m, (as_val *) as_string_new_strdup(k), (as_val *) v);
}
	
/**
 *	Set the specified key's value to an as_string.
 */
static inline int as_stringmap_set_string(as_map * m, const char * k, as_string * v) 
{
	return as_util_hook(set, 1, m, (as_val *) as_string_new_strdup(k), (as_val *) v);
}

/**
 *	Set the specified key's value to an as_bytes.
 */
static inline int as_stringmap_set_bytes(as_map * m, const char * k, as_bytes * v) 
{
	return as_util_hook(set, 1, m, (as_val *) as_string_new_strdup(k), (as_val *) v);
}

/**
 *	Set the specified key's value to an as_list.
 */
static inline int as_stringmap_set_list(as_map * m, const char * k, as_list * v) 
{
	return as_util_hook(set, 1, m, (as_val *) as_string_new_strdup(k), (as_val *) v);
}

/**
 *	Set the specified key's value to an as_map.
 */
static inline int as_stringmap_set_map(as_map * m, const char * k, as_map * v) 
{
	return as_util_hook(set, 1, m, (as_val *) as_string_new_strdup(k), (as_val *) v);
}

/******************************************************************************
 *	GETTER FUNCTIONS
 *****************************************************************************/

/**
 *	Get the specified key's value as an as_val.
 */
static inline as_val * as_stringmap_get(as_map * m, const char * k) 
{
	as_string key;
	as_val * v = as_util_hook(get, NULL, m, (as_val *) as_string_init(&key, (char *) k, false));
	return v;
}

/**
 *	Get the specified key's value as an int64_t.
 */
static inline int64_t as_stringmap_get_int64(as_map * m, const char * k) 
{
	as_string key;
	as_val * v = as_util_hook(get, NULL, m, (as_val *) as_string_init(&key, (char *) k, false));
	as_integer * i = as_integer_fromval(v);
	return i ? as_integer_toint(i) : 0;
}

/**
 *	Get the specified key's value as a double.
 */
static inline double as_stringmap_get_double(as_map * m, const char * k)
{
	as_string key;
	as_val * v = as_util_hook(get, NULL, m, (as_val *) as_string_init(&key, (char *) k, false));
	as_double * ptr = as_double_fromval(v);
	return ptr ? ptr->value : 0.0;
}

/**
 *	Get the specified key's value as a NULL terminated string.
 */
static inline char * as_stringmap_get_str(as_map * m, const char * k) 
{
	as_string key;
	as_val * v = as_util_hook(get, NULL, m, (as_val *) as_string_init(&key, (char *) k, false));
	as_string * s = as_string_fromval(v);
	return s ? as_string_tostring(s) : NULL;
}

/**
 *	Get the specified key's value as an as_integer.
 */
static inline as_integer * as_stringmap_get_integer(as_map * m, const char * k) 
{
	as_string key;
	as_val * v = as_util_hook(get, NULL, m, (as_val *) as_string_init(&key, (char *) k, false));
	return as_integer_fromval(v);
}

/**
 *	Get the specified key's value as an as_double.
 */
static inline as_double * as_stringmap_get_as_double(as_map * m, const char * k)
{
	as_string key;
	as_val * v = as_util_hook(get, NULL, m, (as_val *) as_string_init(&key, (char *) k, false));
	return as_double_fromval(v);
}

/**
 *	Get the specified key's value as an as_string.
 */
static inline as_string * as_stringmap_get_string(as_map * m, const char * k) 
{
	as_string key;
	as_val * v = as_util_hook(get, NULL, m, (as_val *) as_string_init(&key, (char *) k, false));
	return as_string_fromval(v);
}

/**
 *	Get the specified key's value as an as_bytes.
 */
static inline as_bytes * as_stringmap_get_bytes(as_map * m, const char * k) 
{
	as_string key;
	as_val * v = as_util_hook(get, NULL, m, (as_val *) as_string_init(&key, (char *) k, false));
	return as_bytes_fromval(v);
}

/**
 *	Get the specified key's value as an as_list.
 */
static inline as_list * as_stringmap_get_list(as_map * m, const char * k) 
{
	as_string key;
	as_val * v = as_util_hook(get, NULL, m, (as_val *) as_string_init(&key, (char *) k, false));
	return as_list_fromval(v);
}

/**
 *	Get the specified key's value as an as_map.
 */
static inline as_map * as_stringmap_get_map(as_map * m, const char * k) 
{
	as_string key;
	as_val * v = as_util_hook(get, NULL, m, (as_val *) as_string_init(&key, (char *) k, false));
	return as_map_fromval(v);
}

/**
 *	@}
 */

#ifdef __cplusplus
} // end extern "C"
#endif
