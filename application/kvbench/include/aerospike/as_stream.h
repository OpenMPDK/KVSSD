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

#include <stdlib.h>

#include <aerospike/as_val.h>
#include <aerospike/as_util.h>

#include <citrusleaf/alloc.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *	MACROS
 *****************************************************************************/

#define AS_STREAM_END ((void *) 0)

/******************************************************************************
 *	TYPES
 *****************************************************************************/

struct as_stream_hooks_s;

/**
 *	Stream Status Codes
 */
typedef enum as_stream_status_e {
	AS_STREAM_OK	= 0,
	AS_STREAM_ERR	= 1
} as_stream_status;

/**
 *	Stream Interface
 *
 *	To use the stream interface, you will need to create an instance 
 *	via one of the implementations.
 *
 *	@ingroup aerospike_t
 */
typedef struct as_stream_s {

	/**
	 *	Specifies whether the cf_free() can be used
	 *	on this stream.
	 */
	bool free;

	/**
	 *	Context data for the stream.
	 */
	void * data;

	/**
	 *	Hooks for the stream
	 */
	const struct as_stream_hooks_s * hooks;

} as_stream;

/**
 *	Stream Hooks
 *
 *	An implementation of `as_rec` should provide implementations for each
 *	of the hooks.
 */
typedef struct as_stream_hooks_s {

	/**
	 *	Destroy the stream.
	 */
	int (* destroy)(as_stream * stream);

	/**
	 *	Read the next value from the stream.
	 */
	as_val * (* read)(const as_stream * stream);

	/**
	 *	Write a value to the stream.
	 */
	as_stream_status (* write)(const as_stream * stream, as_val * value);
	
} as_stream_hooks;

/**
 *  Wrapper functions to ensure each CF allocation-related function call has a unique line.
 */
void *as_stream_malloc(size_t size);
void as_stream_free(void *ptr);

/******************************************************************************
 *	INSTANCE FUNCTIONS
 *****************************************************************************/

/**
 *	Initializes a stack allocated as_stream for a given source and hooks.
 *
 *	@param stream	The stream to initialize.
 *	@param data		The source feeding the stream
 *	@param hooks	The hooks that interface with the source
 *
 *	@return On success, the initialized stream. Otherwise NULL.
 *
 *	@relatesalso as_stream
 */
static inline as_stream * as_stream_init(as_stream * stream, void * data, const as_stream_hooks * hooks) 
{
	if ( !stream ) return stream;

	stream->free = false;
	stream->data = data;
	stream->hooks = hooks;
	return stream;
}

/**
 *	Creates a new heap allocated as_stream for a given source and hooks.
 *
 *	@param data		The source feeding the stream
 *	@param hooks	The hooks that interface with the source
 *
 *	@return On success, a new stream. Otherwise NULL.
 *
 *	@relatesalso as_stream
 */
static inline as_stream * as_stream_new(void * data, const as_stream_hooks * hooks)
{
	as_stream * stream = (as_stream *) as_stream_malloc(sizeof(as_stream));
	if ( !stream ) return stream;

	stream->free = true;
	stream->data = data;
	stream->hooks = hooks;
	return stream;
}

/**
 *	Destroy the as_stream and associated resources.
 *
 *	@param stream 	The stream to destroy.
 *
 *	@return 0 on success, otherwise 1.
 *
 *	@relatesalso as_stream
 */
static inline void as_stream_destroy(as_stream * stream)
{
	as_util_hook(destroy, 1, stream);
	if ( stream && stream->free ) {
		as_stream_free(stream);
	}
}

/******************************************************************************
 *	VALUE FUNCTIONS
 *****************************************************************************/

/**
 *	Get the source for the stream
 *
 *	@param stream 	The stream to get the source from
 *
 *	@return pointer to the source of the stream
 *
 *	@relatesalso as_stream
 */
static inline void * as_stream_source(const as_stream * stream)
{
	return (stream ? stream->data : NULL);
}

/**
 *	Reads a value from the stream
 *
 *	@param stream 	The stream to be read.
 *
 *	@return the element read from the stream or STREAM_END
 *
 *	@relatesalso as_stream
 */
static inline as_val * as_stream_read(const as_stream * stream)
{
	return as_util_hook(read, NULL, stream);
}

/**
 *	Is the stream readable? Tests whether the stream has a read function.
 *
 *	@param stream 	The stream to test.
 *
 *	@return true if the stream can be read from
 *
 *	@relatesalso as_stream
 */
static inline bool as_stream_readable(const as_stream * stream)
{
	return stream != NULL && stream->hooks != NULL && stream->hooks->read;
}

/**
 *	Write a value to the stream
 *
 *	@param stream	The stream to write to.
 *	@param value	The element to write to the stream.
 *
 *	@return AS_STREAM_OK on success, otherwise is failure.
 *
 *	@relatesalso as_stream
 */
static inline as_stream_status as_stream_write(const as_stream * stream, as_val * value)
{
	return as_util_hook(write, AS_STREAM_ERR, stream, value);
}


/**
 *	Is the stream writable? Tests whether the stream has a write function.
 *
 *	@param stream 	The stream to test.
 *
 *	@return true if the stream can be written to.
 *
 *	@relatesalso as_stream
 */
static inline bool as_stream_writable(const as_stream * stream)
{
	return stream != NULL && stream->hooks != NULL && stream->hooks->write;
}

#ifdef __cplusplus
} // end extern "C"
#endif
