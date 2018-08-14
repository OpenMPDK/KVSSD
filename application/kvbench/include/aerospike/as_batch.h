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

#pragma GCC diagnostic ignored "-Waddress"

#include <aerospike/as_bin.h>
#include <aerospike/as_key.h>
#include <aerospike/as_record.h>
#include <aerospike/as_status.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*****************************************************************************
 *	STRUCTURES
 *****************************************************************************/

/**
 *	A collection of keys to be batch processed.
 */
typedef struct as_batch_s {

	/**
	 *	If true, then this structure will be freed when as_batch_destroy() 
	 *	is called.
	 */
	bool _free;
	
	/**
	 *	Sequence of keys in the batch.
	 */
	struct {

		/**
		 *	If true, then this structure will be freed when as_batch_destroy()
		 *	is called.
		 */
		bool _free;

		/**
		 *	The number of keys this structure contains.
		 */
		uint32_t size;

		/**
		 *	The keys contained by this batch.
		 */
		as_key * entries;

	} keys;

} as_batch;

/**
 *	The (key, result, record) for an entry in a batch read.
 *	The result is AEROSPIKE_OK if the record is found,
 *	AEROSPIKE_ERR_RECORD_NOT_FOUND if the transaction succeeds but the record is
 *	not found, or another error code if the transaction fails.
 *	The record is NULL if either the transaction failed or the record does not
 *	exist. For aerospike_batch_exists() calls the record will never contain bins
 *	but will contain metadata (generation and expiration).
 */
typedef struct as_batch_read_s {

	/**
	 *	The key requested.
	 */
	const as_key * key;

	/**
	 *	The result of the transaction to read this key.
	 */
	as_status result;

	/**
	 *	The record for the key requested, NULL if the key was not found.
	 */
	as_record record;

} as_batch_read;


/*********************************************************************************
 *	INSTANCE MACROS
 *********************************************************************************/


/** 
 *	Initializes `as_batch` with specified capacity using alloca().
 *
 *	For heap allocation, use `as_batch_new()`.
 *
 *	~~~~~~~~~~{.c}
 *	as_batch batch;
 *	as_batch_inita(&batch, 2);
 *	as_key_init(as_batch_get(&batch, 0), "ns", "set", "key1");
 *	as_key_init(as_batch_get(&batch, 1), "ns", "set", "key2");
 *	~~~~~~~~~~
 *
 *	When the batch is no longer needed, then use as_batch_destroy() to
 *	release the batch and associated resources.
 *	
 *	@param __batch		The query to initialize.
 *	@param __size		The number of keys to allocate.
 *
 *	@relates as_batch
 *	@ingroup batch_object
 */
#define as_batch_inita(__batch, __size) \
	do { \
		if ( (__batch) != NULL ) {\
			(__batch)->_free = false;\
			(__batch)->keys.entries = (as_key*) alloca(sizeof(as_key) * (__size));\
			if ( (__batch)->keys.entries ) { \
				(__batch)->keys._free = false;\
				(__batch)->keys.size = (__size);\
			}\
	 	} \
	} while(0)

/*********************************************************************************
 *	INSTANCE FUNCTIONS
 *********************************************************************************/

/**
 *	Create and initialize a heap allocated as_batch capable of storing
 *	`capacity` keys.
 *
 *	~~~~~~~~~~{.c}
 *	as_batch * batch = as_batch_new(2);
 *	as_key_init(as_batch_get(batch, 0), "ns", "set", "key1");
 *	as_key_init(as_batch_get(batch, 1), "ns", "set", "key2");
 *	~~~~~~~~~~
 *
 *	When the batch is no longer needed, then use as_batch_destroy() to
 *	release the batch and associated resources.
 *	
 *	@param size			The number of keys to allocate.
 *
 *	@relates as_batch
 *	@ingroup batch_object
 */
as_batch * as_batch_new(uint32_t size);

/**
 *	Initialize a stack allocated as_batch capable of storing `capacity` keys.
 *
 *	~~~~~~~~~~{.c}
 *	as_batch batch;
 *	as_batch_init(&batch, 2);
 *	as_key_init(as_batch_get(&batch, 0), "ns", "set", "key1");
 *	as_key_init(as_batch_get(&batch, 1), "ns", "set", "key2");
 *	~~~~~~~~~~
 *
 *	When the batch is no longer needed, then use as_batch_destroy() to
 *	release the batch and associated resources.
 *	
 *	@param batch		The batch to initialize.
 *	@param size			The number of keys to allocate.
 *	
 *	@relates as_batch
 *	@ingroup batch_object
 */
as_batch * as_batch_init(as_batch * batch, uint32_t size);

/**
 *	Destroy the batch of keys.
 *
 *	~~~~~~~~~~{.c}
 *	as_batch_destroy(batch);
 *	~~~~~~~~~~
 *
 *	@param batch 	The batch to release.
 *
 *	@relates as_batch
 *	@ingroup batch_object
 */
void as_batch_destroy(as_batch * batch);

/**
 *	Get the key at given position of the batch. If the position is not
 *	within the allocated capacity for the batchm then NULL is returned.
 *
 *	@param batch 	The batch to get the key from.
 *	@param i		The position of the key.
 *
 *	@return On success, the key at specified position. If position is invalid, then NULL.
 *
 *	@relates as_batch
 *	@ingroup batch_object
 */
static inline as_key * as_batch_keyat(const as_batch * batch, uint32_t i)
{
	return (batch != NULL && batch->keys.entries != NULL && batch->keys.size > i) ? &batch->keys.entries[i] : NULL;
}

#ifdef __cplusplus
} // end extern "C"
#endif
