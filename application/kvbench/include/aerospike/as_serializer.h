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
#include <aerospike/as_buffer.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 * TYPES
 *****************************************************************************/

struct as_serializer_hooks_s;

/**
 * Serializer Object
 */
typedef struct as_serializer_s {
	/**
	 *	@private
	 *	If true, then as_serializer_destroy() will free this.
	 */
	bool free;

	/**
	 *	Hooks for the serializer
	 */
	const struct as_serializer_hooks_s *hooks;
} as_serializer;

/**
 * Serializer Function Hooks
 */
typedef struct as_serializer_hooks_s {
	void     (*destroy)(as_serializer *);
	int      (*serialize)(as_serializer *, const as_val *, as_buffer *);
	int32_t  (*serialize_presized)(as_serializer *, const as_val *, uint8_t *);
	int      (*deserialize)(as_serializer *, as_buffer *, as_val **);
	uint32_t (*serialize_getsize)(as_serializer *, const as_val *);
} as_serializer_hooks;

/******************************************************************************
 * FUNCTIONS
 *****************************************************************************/

as_serializer *as_serializer_cons(as_serializer *serializer, bool free, const as_serializer_hooks *hooks);

as_serializer *as_serializer_init(as_serializer *serializer, const as_serializer_hooks *hooks);

as_serializer *as_serializer_new(const as_serializer_hooks *);

void as_serializer_destroy(as_serializer *);

/******************************************************************************
 * INLINE FUNCTIONS
 *****************************************************************************/

static inline int as_serializer_serialize(as_serializer *serializer, as_val *val, as_buffer *buffer)
{
	return as_util_hook(serialize, 1, serializer, val, buffer);
}

/**
 * Pack directly into pre-sized buffer.
 * @return -1 if failed
 * @return length of unpacked buffer
 */
static inline int32_t as_serializer_serialize_presized(as_serializer *serializer, const as_val *val, uint8_t *buf)
{
	return as_util_hook(serialize_presized, 1, serializer, val, buf);
}

static inline int as_serializer_deserialize(as_serializer *serializer, as_buffer *buffer, as_val **val)
{
	return as_util_hook(deserialize, 1, serializer, buffer, val);
}

static inline uint32_t as_serializer_serialize_getsize(as_serializer *serializer, as_val *val)
{
	return as_util_hook(serialize_getsize, 1, serializer, val);
}

#ifdef __cplusplus
} // end extern "C"
#endif
