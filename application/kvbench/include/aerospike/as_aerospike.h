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
#include <aerospike/as_types.h>
#include <citrusleaf/cf_clock.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 * TYPES
 ******************************************************************************/

typedef struct as_aerospike_s as_aerospike;
typedef struct as_aerospike_hooks_s as_aerospike_hooks;

struct as_aerospike_s {
	bool    is_rcalloc;
	void * source;
	const as_aerospike_hooks * hooks;
};

struct as_aerospike_hooks_s {
	void (* destroy)(as_aerospike *);

	int (* rec_create)(const as_aerospike *, const as_rec *);
	int (* rec_update)(const as_aerospike *, const as_rec *);
	int (* rec_remove)(const as_aerospike *, const as_rec *);
	int (* rec_exists)(const as_aerospike *, const as_rec *);

	int (*log)(const as_aerospike *, const char *, const int, const int, const char *);
	cf_clock (* get_current_time)( const as_aerospike * );
	int (*set_context) ( const as_aerospike *, const as_rec *, const uint32_t context );
	int (*get_config) ( const as_aerospike *, const as_rec *, const char * );
};

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/

as_aerospike * as_aerospike_init(as_aerospike *a, void *source, const as_aerospike_hooks *hooks);

as_aerospike * as_aerospike_new(void *source, const as_aerospike_hooks *hooks);

void as_aerospike_destroy(as_aerospike *);

/******************************************************************************
 * INLINE FUNCTIONS
 ******************************************************************************/

static inline int as_aerospike_rec_create(const as_aerospike * a, const as_rec * r)
{
	return as_util_hook(rec_create, 1, a, r);
}

static inline int as_aerospike_rec_update(const as_aerospike * a, const as_rec * r)
{
	return as_util_hook(rec_update, 1, a, r);
}

static inline int as_aerospike_rec_exists(const as_aerospike * a, const as_rec * r)
{
	return as_util_hook(rec_exists, 1, a, r);
}

static inline int as_aerospike_rec_remove(const as_aerospike * a, const as_rec * r)
{
	return as_util_hook(rec_remove, 1, a, r);
}

static inline int as_aerospike_log(const as_aerospike * a, const char * name, const int line, const int lvl, const char * msg) 
{
	return as_util_hook(log, 1, a, name, line, lvl, msg);
}

static inline cf_clock as_aerospike_get_current_time(const as_aerospike * a )
{
	return as_util_hook(get_current_time, 0, a);
}

static inline int as_aerospike_set_context(const as_aerospike * a, const as_rec *r, const uint32_t context )
{
	return as_util_hook(set_context, 2, a, r, context);
}

static inline int as_aerospike_get_config(const as_aerospike * a, const as_rec *r, const char *name)
{
	return as_util_hook(get_config, 0, a, r, name);
}

#ifdef __cplusplus
} // end extern "C"
#endif
