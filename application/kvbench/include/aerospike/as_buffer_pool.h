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

#include <citrusleaf/cf_queue.h>

#ifdef __cplusplus
extern "C" {
#endif
	
	/******************************************************************************
	 *	TYPES
	 *****************************************************************************/
	
	/**
	 *	@private
	 *	Buffer.
	 */
	typedef struct as_buffer_result_s {
		void* data;
		uint32_t capacity;
	} as_buffer_result;
	
	/**
	 *	@private
	 *	Buffer pool.
	 */
	typedef struct as_buffer_pool_s {
		cf_queue* queue;
		uint32_t header_size;
		uint32_t buffer_size;
	} as_buffer_pool;
	
	/******************************************************************************
	 *	FUNCTIONS
	 *****************************************************************************/
	
	/**
	 *	@private
	 *	Initialize empty buffer pool.  Each buffer in the pool will be a fixed size.
	 *
	 *	@param pool			Buffer pool.
	 *	@param header_size 	Size of buffer header.
	 *	@param buffer_size 	Fixed buffer size.
	 *
	 *	Returns:
	 *	0  : Success
	 *	-1 : Failed to create queue.
	 */
	int
	as_buffer_pool_init(as_buffer_pool* pool, uint32_t header_size, uint32_t buffer_size);
	
	/**
	 *	@private
	 *	If requested buffer size is less than/equal the pool's buffer size, pop buffer from pool.
	 *	Otherwise allocate memory on heap.  If the pool is empty, also create buffer on heap.
	 *
	 *	@param pool			Buffer pool.
	 *	@param size			Requested size of buffer.
	 *	@param buffer		Buffer to be populated.
	 *
	 *	Returns:
	 *	0  : Found in pool.
	 *	1  : Pool empty. Allocated new buffer.
	 *	2  : Size greater than capacity. Allocated new large buffer.
	 *	-1 : Memory allocation error.
	 *	-2 : Queue failure.
	 */
	int
	as_buffer_pool_pop(as_buffer_pool* pool, uint32_t size, as_buffer_result* buffer);
	
	/**
	 *	@private
	 *	If buffer capacity less than/equal the pool's buffer size, push buffer back into pool.
	 *	Otherwise, free memory and do not put back into pool.
	 *
	 *	@param pool			Buffer pool.
	 *	@param buffer		Buffer.
	 *	@param capacity		Capacity of buffer.
	 *
	 *	Returns:
	 *	0  : Success
	 *	-1 : Queue failure.
	 *	-2 : Detected large buffer of different size. Buffer was destroyed.
	 */
	int
	as_buffer_pool_push(as_buffer_pool* pool, void* buffer, uint32_t capacity);
	
	/**
	 *	@private
	 *	If buffer capacity less than/equal the pool's buffer size and the number of unused buffers 
	 *	is less than/equal than max_buffers, push buffer back into pool.
	 *	Otherwise, free memory and do not put back into pool.
	 *
	 *	@param pool			Buffer pool.
	 *	@param buffer		Buffer.
	 *	@param capacity		Capacity of buffer.
	 *	@param max_buffers 	Maximum number of unused buffers allowed in pool.
	 *
	 *	Returns:
	 *	0  : Success
	 *	-1 : Too many buffers exist in pool. Buffer was destroyed.
	 *	-2 : Detected large buffer of different size. Buffer was destroyed.
	 */
	int
	as_buffer_pool_push_limit(as_buffer_pool* pool, void* buffer, uint32_t capacity, uint32_t max_buffers);

	/**
	 *	@private
	 *	Delete buffer_count buffers from the buffer pool. This is useful when a large number of
	 *	buffers are created due to a burst of concurrent buffer usage and pruning is desired.
	 *
	 *	@param pool			Buffer pool.
	 *	@param buffer_count	Number of buffers to delete.
	 *
	 *	Returns number of buffers deleted.
	 */
	int
	as_buffer_pool_drop_buffers(as_buffer_pool* pool, int buffer_count);
	
	/**
	 *	@private
	 *	Empty buffer pool and destroy.
	 *
	 *	@param pool			Buffer pool.
	 */
	void
	as_buffer_pool_destroy(as_buffer_pool* pool);
	
#ifdef __cplusplus
} // end extern "C"
#endif
