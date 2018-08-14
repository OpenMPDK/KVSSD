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
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *	TYPES
 *****************************************************************************/

/**
 *	Types for `as_bytes.type`
 */
typedef enum as_bytes_type_e {

	/** 
	 *	Type is Undefined
	 */
	AS_BYTES_UNDEF		= 0,

	/** 
	 *	String
	 */
	AS_BYTES_INTEGER	= 1,

	/**
	 *	Double
	 */
	AS_BYTES_DOUBLE		= 2,

	/** 
	 *	String
	 */
	AS_BYTES_STRING		= 3,

	/** 
	 *	Generic BLOB
	 */
	AS_BYTES_BLOB		= 4,

	/**
	 *	Serialized Java Object
	 */
	AS_BYTES_JAVA		= 7,

	/**
	 *	Serialized C# Object
	 */
	AS_BYTES_CSHARP		= 8,

	/**
	 *	Pickled Python Object
	 */
	AS_BYTES_PYTHON		= 9,

	/**
	 *	Marshalled Ruby Object
	 */
	AS_BYTES_RUBY		= 10,

	/**
	 *	Serialized PHP Object
	 */
	AS_BYTES_PHP		= 11,

	/**
	 *	Serialized Erlang Data
	 */
	AS_BYTES_ERLANG		= 12,

	/**
	 *	Map
	 */
	AS_BYTES_MAP		= 19,

	/**
	 *	List
	 */
	AS_BYTES_LIST		= 20,

	/**
	 *	LDT  TODO - remove this as soon as client is ready to move forward.
	 */
	AS_BYTES_LDT		= 21,

	/**
	 *	GeoJSON Data
	 */
	AS_BYTES_GEOJSON	= 23,

	/**
	 *	Upper bounds for the enum
	 */
	AS_BYTES_TYPE_MAX	= 24

} as_bytes_type;

/**
 *	Container for byte arrays.
 *
 *	## Initialization
 *	
 *	An as_bytes should be initialized via one of the provided function.
 *	- as_bytes_inita()
 *	- as_bytes_init()
 *	- as_bytes_new()
 *
 *	The as_bytes_inita(), as_bytes_init() and as_bytes_new() are used to 
 *	initialize empty internal buffers of a specified size.
 *
 *	To initialize a stack allocated as_string, use as_bytes_init():
 *
 *	~~~~~~~~~~{.c}
 *	as_bytes b;
 *	as_bytes_init(&b, 20);
 *	~~~~~~~~~~
 *
 *	The above initialized the variable, and allocated 20 bytes to the buffer
 *	using `cf_malloc()`.
 *
 *	To use only stack allocated buffer for as_bytes, ten you should use 
 *	as_bytes_inita():
 *
 *	~~~~~~~~~~{.c}
 *	as_bytes b;
 *	as_bytes_inita(&b, 20);
 *	~~~~~~~~~~
 *
 *	You will see the APIs of the two are very similar. The key difference is
 *	as_bytes_inita() is a macro, which performs stack allocation inline.
 *
 *	If you need a heap allocated as_bytes instance, then you should use
 *	as_bytes_new():
 *
 *	~~~~~~~~~~{.c}
 *	as_bytes * b = as_bytes_new(20);
 *	~~~~~~~~~~
 *
 *	## Wrapping Byte Arrays
 *
 *	If you already have a byte array allocated and want to simply wrap it
 *	in an as_bytes, then use either:
 *	- as_bytes_init_wrap()
 *	- as_bytes_new_wrap()
 *
 *	The as_bytes_init_wrap() function is used to initialize a stack allocated
 *	as_bytes, then set the internal buffer to the byte array provided.
 *
 *	The as_bytes_new_wrap() function is used to create an initialize a new
 *	heap allocated as_bytes, then it will set the internal buffer to the
 *	byte array provided.
 *	
 *
 *	## Destruction
 *
 *	When the as_bytes instance is no longer required, then you should
 *	release the resources associated with it via as_bytes_destroy():
 *
 *	~~~~~~~~~~{.c}
 *	as_bytes_destroy(b);
 *	~~~~~~~~~~
 *	
 *	## Usage
 *
 *	as_bytes has a number of functions for reading and writing data to its 
 *	internal buffer.
 *
 *	For reading at specified index:
 *
 *	| Function | Description |
 *	| -------- | ----------- |
 *	| as_bytes_get() | Copy the bytes in the buffer to another buffer. |
 *	| as_bytes_get_byte() | Read a byte from the buffer |
 *	| as_bytes_get_int16() | Read a 16-bit integer from the buffer |
 *	| as_bytes_get_int32() | Read a 32-bit integer from the buffer |
 *	| as_bytes_get_int64() | Read a 64-bit integer from the buffer |
 *
 *	For writing at specified index:
 *
 *	| Function | Description |
 *	| -------- | ----------- |
 *	| as_bytes_set() | Copy a byte array into the buffer. |
 *	| as_bytes_set_byte() | Write a byte from the buffer |
 *	| as_bytes_set_int16() | Write a 16-bit integer from the buffer |
 *	| as_bytes_set_int32() | Write a 32-bit integer from the buffer |
 *	| as_bytes_set_int64() | Write a 64-bit integer from the buffer |
 *
 *	For writing at to the end of the buffer:
 *
 *	| Function | Description |
 *	| -------- | ----------- |
 *	| as_bytes_append() | Copy a byte array into the buffer. |
 *	| as_bytes_append_byte() | Write a byte from the buffer |
 *	| as_bytes_append_int16() | Write a 16-bit integer from the buffer |
 *	| as_bytes_append_int32() | Write a 32-bit integer from the buffer |
 *	| as_bytes_append_int64() | Write a 64-bit integer from the buffer |
 *
 *
 *	## Conversions
 *
 *	as_bytes is derived from as_val, so it is generally safe to down cast:
 *
 *	~~~~~~~~~~{.c}
 *	as_val val = (as_val) b;
 *	~~~~~~~~~~
 *	
 *	However, upcasting is more error prone. When doing so, you should use 
 *	as_bytes_fromval(). If conversion fails, then the return value is NULL.
 *
 *	~~~~~~~~~~{.c}
 *	as_bytes * i = as_bytes_fromval(val);
 *	~~~~~~~~~~
 *
 *
 *	
 *	@extends as_val
 *	@ingroup aerospike_t
 */
typedef struct as_bytes_s {

	/**
	 *	@private
	 *	as_boolean is a subtype of as_val.
	 *	You can cast as_boolean to as_val.
	 */
	as_val _;

	/**
	 *	The number of bytes allocated to `as_bytes.value`.
	 */
	uint32_t capacity;

	/**
	 *	The number of bytes used by `as_bytes.value`.
	 */
	uint32_t size;

	/**
	 *	A sequence of bytes.
	 */
	uint8_t * value;

	/**
	 *	If true, then `as_bytes.value` will be freed when as_bytes_destroy()
	 *	is called.
	 */
	bool free;

	/**
	 *	The type of bytes.
	 */
	as_bytes_type type;

} as_bytes;

/******************************************************************************
 *	MACROS
 *****************************************************************************/

/**
 *	Initializes a stack allocated `as_bytes`. Allocates an internal buffer
 *	on the stack of specified capacity using `alloca()`.
 *	
 *	~~~~~~~~~~{.c}
 *	as_bytes bytes;
 *	as_bytes_inita(&bytes, 10);
 *	~~~~~~~~~~
 *
 *	@param __bytes 		The bytes to initialize.
 *	@param __capacity	The number of bytes to allocate on the heap.
 */
#define as_bytes_inita(__bytes, __capacity)\
	as_bytes_init(__bytes, 0);\
	(__bytes)->type = AS_BYTES_BLOB;\
	(__bytes)->free = false;\
	(__bytes)->capacity = (__capacity);\
	(__bytes)->size = 0;\
	(__bytes)->value = (uint8_t*) alloca(sizeof(uint8_t) * (__capacity));


/******************************************************************************
 *	INSTANCE FUNCTIONS
 *****************************************************************************/

/**
 *	Initializes a stack allocated `as_bytes`. Allocates an internal buffer
 *	on the heap of specified capacity using `cf_malloc()`.
 *	
 *	~~~~~~~~~~{.c}
 *	as_bytes bytes;
 *	as_bytes_init_empty(&bytes, 10);
 *	~~~~~~~~~~
 *
 *	@param bytes 	The bytes to initialize.
 *	@param capacity	The number of bytes to allocate on the heap.
 *
 *	@return On success, the initializes bytes. Otherwise NULL.
 *
 *	@relatesalso as_bytes
 */
as_bytes * as_bytes_init(as_bytes * bytes, uint32_t capacity);

/**
 *	Initializes a stack allocated `as_bytes`, wrapping the given buffer.
 *
 *	~~~~~~~~~~{.c}
 *	uint8_t raw[10] = {0};
 *
 *	as_bytes bytes;
 *	as_bytes_init_wrap(&bytes, raw, 10, false);
 *	~~~~~~~~~~
 *	
 *	@param bytes 	The bytes to initialize.
 *	@param value	The initial value.
 *	@param size		The number of bytes of the initial value.
 *	@param free		If true, then `as_bytes_destroy()` will free the value.
 *
 *	@return On success, the initializes bytes. Otherwise NULL.
 *
 *	@relatesalso as_bytes
 */
as_bytes * as_bytes_init_wrap(as_bytes * bytes, uint8_t * value, uint32_t size, bool free);

/**
 *	Create and initialize a new heap allocated `as_bytes`. Allocates an 
 *	internal buffer on the heap of specified capacity using `cf_malloc()`.
 *	
 *	~~~~~~~~~~{.c}
 *	as_bytes * bytes = as_bytes_new(10);
 *	~~~~~~~~~~
 *	
 *	@param capacity	The number of bytes to allocate.
 *
 *	@return On success, the initializes bytes. Otherwise NULL.
 *
 *	@relatesalso as_bytes
 */
as_bytes * as_bytes_new(uint32_t capacity);

/**
 *	Creates a new heap allocated `as_bytes`, wrapping the given buffer.
 *
 *	~~~~~~~~~~{.c}
 *	uint8_t raw[10] = {0};
 *
 *	as_bytes * bytes = as_bytes_new_wrap(raw, 10, false);
 *	~~~~~~~~~~
 *
 *	@param value	The initial value.
 *	@param size		The number of bytes of the initial value.
 *	@param free		If true, then `as_bytes_destroy()` will free the value.
 *
 *	@return On success, the initializes bytes. Otherwise NULL.
 *
 *	@relatesalso as_bytes
 */
as_bytes * as_bytes_new_wrap(uint8_t * value, uint32_t size, bool free);

/**
 *	Destroy the `as_bytes` and release associated resources.
 *
 *	~~~~~~~~~~{.c}
 *	as_bytes_destroy(bytes);
 *	~~~~~~~~~~
 *
 *	@param bytes	The bytes to destroy.
 *
 *	@relatesalso as_bytes
 */
static inline void as_bytes_destroy(as_bytes * bytes) 
{
	as_val_destroy((as_val *) bytes);
}

/******************************************************************************
 *	VALUE FUNCTIONS
 *****************************************************************************/

/**
 *	Get the number of bytes used.
 *
 *	@param bytes The bytes to get the size of.
 *
 *	@return The number of bytes used.
 *
 *	@relatesalso as_bytes
 */
static inline uint32_t as_bytes_size(const as_bytes * bytes)
{
	if ( !bytes ) return 0;
	return bytes->size;
}

/**
 *	Get the number of bytes allocated.
 *
 *	@param bytes The bytes to get the capacity of.
 *
 *	@return The number of bytes allocated.
 *
 *	@relatesalso as_bytes
 */
static inline uint32_t as_bytes_capacity(const as_bytes * bytes)
{
	if ( !bytes ) return 0;
	return bytes->capacity;
}

/**
 *	Get the type of bytes.
 *
 *	@param bytes The bytes to get the type of.
 *
 *	@return The type of bytes.
 *
 *	@relatesalso as_bytes
 */
static inline as_bytes_type as_bytes_get_type(const as_bytes * bytes)
{
	if ( !bytes ) return AS_BYTES_UNDEF;
	return bytes->type;
}

/**
 *	Set the type of bytes.
 *
 *	@param bytes 	The bytes to set the type of.
 *	@param type 	The type for the bytes.
 *
 *	@relatesalso as_bytes
 */
static inline void as_bytes_set_type(as_bytes * bytes, as_bytes_type type)
{
	if ( !bytes ) return;
	bytes->type = type;
}

/** 
 *	Get the raw value of this instance. If the instance is NULL, then 
 *	return the fallback value.
 *
 *	~~~~~~~~~~{.c}
 *	uint8_t * raw = as_bytes_getorelse(&bytes, NULL);
 *	~~~~~~~~~~
 *
 *	@param bytes	The bytes to get the raw value from.
 *	@param fallback	The value to return if bytes is NULL.
 *
 *	@return The pointer to the raw value if bytes is not NULL. Otherwise 
 *			return the fallback.
 *
 *	@relatesalso as_bytes
 */
static inline uint8_t * as_bytes_getorelse(const as_bytes * bytes, uint8_t * fallback)
{
	return bytes ? bytes->value : fallback;
}

/** 
 *	Get the raw value of this instance.
 *
 *	~~~~~~~~~~{.c}
 *	uint8_t * raw = as_bytes_get(&bytes);
 *	~~~~~~~~~~
 *
 *	@param bytes	The bytes to get the raw value from.
 *
 *	@return The pointer to the raw value.
 *
 *	@relatesalso as_bytes
 */
static inline uint8_t * as_bytes_get(const as_bytes * bytes)
{
	return as_bytes_getorelse(bytes, NULL);
}


/******************************************************************************
 *	GET AT INDEX
 *****************************************************************************/


/** 
 *	Copy into value up to size bytes from the given `as_bytes`, returning
 *	the number of bytes copied.
 *
 *	~~~~~~~~~~{.c}
 *	uint8_t value[3] = {0};
 *	uint32_t sz = as_bytes_copy(&bytes, 0, value, 3);
 *	if ( sz == 0 ) {
 *		// sz == 0, means that an error occurred
 *	}
 *	~~~~~~~~~~
 *
 *	@param bytes	The bytes to read from.
 *	@param index	The positing in bytes to read from.
 *	@param value	The byte buffer to copy into.
 *	@param size		The number of bytes to copy into the buffer.
 *
 *
 *	@return The number of bytes read and stored into value. 0 (zero) indicates
 * 			an error has occurred.
 *
 *	@relatesalso as_bytes
 */
uint32_t as_bytes_copy(const as_bytes * bytes, uint32_t index, uint8_t * value, uint32_t size);

/** 
 *	Read a single byte from the given bytes.
 *
 *	~~~~~~~~~~{.c}
 *	uint8_t value = 0;
 *	uint32_t sz = as_bytes_get_byte(&bytes, 0, &value);
 *	if ( sz == 0 ) {
 *		// sz == 0, means that an error occurred
 *	}
 *	~~~~~~~~~~
 *
 *	@return The number of bytes read and stored into value. 0 (zero) indicates
 * 			an error has occurred.
 *
 *	@relatesalso as_bytes
 */
static inline uint32_t as_bytes_get_byte(const as_bytes * bytes, uint32_t index, uint8_t * value)
{
	return as_bytes_copy(bytes, index, (uint8_t *) value, 1);
}

/** 
 *	Read an int16_t from the given bytes.
 *
 *	~~~~~~~~~~{.c}
 *	int16_t value = 0;
 *	uint32_t sz = as_bytes_get_int16(&bytes, 0, &value);
 *	if ( sz == 0 ) {
 *		// sz == 0, means that an error occurred
 *	}
 *	~~~~~~~~~~
 *
 *	@return The number of bytes read and stored into value. 0 (zero) indicates
 * 			an error has occurred.
 *
 *	@relatesalso as_bytes
 */
static inline uint32_t as_bytes_get_int16(const as_bytes * bytes, uint32_t index, int16_t * value)
{
	return as_bytes_copy(bytes, index, (uint8_t *) value, 2);
}

/** 
 *	Read an int32_t from the given bytes.
 *
 *	~~~~~~~~~~{.c}
 *	int32_t value = 0;
 *	uint32_t sz = as_bytes_get_int32(&bytes, 0, &value);
 *	if ( sz == 0 ) {
 *		// sz == 0, means that an error occurred
 *	}
 *	~~~~~~~~~~
 *
 *	@return The number of bytes read and stored into value. 0 (zero) indicates
 * 			an error has occurred.
 *
 *	@relatesalso as_bytes
 */
static inline uint32_t as_bytes_get_int32(const as_bytes * bytes, uint32_t index, int32_t * value)
{
	return as_bytes_copy(bytes, index, (uint8_t *) value, 4);
}

/**
 *	Read an int64_t from the given bytes.
 *
 *	~~~~~~~~~~{.c}
 *	int64_t value = 0;
 *	uint32_t sz = as_bytes_get_int64(&bytes, 0, &value);
 *	if ( sz == 0 ) {
 *		// sz == 0, means that an error occurred
 *	}
 *	~~~~~~~~~~
 *
 *	@return The number of bytes read and stored into value. 0 (zero) indicates
 * 			an error has occurred.
 *
 *	@relatesalso as_bytes
 */
static inline uint32_t as_bytes_get_int64(const as_bytes * bytes, uint32_t index, int64_t * value)
{
	return as_bytes_copy(bytes, index, (uint8_t *) value, 8);
}

/**
 *	Read a double from the given bytes.
 *
 *	~~~~~~~~~~{.c}
 *	double value = 0;
 *	uint32_t sz = as_bytes_get_double(&bytes, 0, &value);
 *	if ( sz == 0 ) {
 *		// sz == 0, means that an error occurred
 *	}
 *	~~~~~~~~~~
 *
 *	@return The number of bytes read and stored into value. 0 (zero) indicates
 * 			an error has occurred.
 *
 *	@relatesalso as_bytes
 */
static inline uint32_t as_bytes_get_double(const as_bytes * bytes, uint32_t index, double * value)
{
	return as_bytes_copy(bytes, index, (uint8_t *) value, 8);
}

/**
 *	Decode an integer in variable 7-bit format.
 *	The high bit indicates if more bytes are used.
 *
 *	~~~~~~~~~~{.c}
 *	uint32_t value = 0;
 *	uint32_t sz = as_bytes_get_var_int(&bytes, 0, &value);
 *	if ( sz == 0 ) {
 *		// sz == 0, means that an error occurred
 *	}
 *	~~~~~~~~~~
 *
 *	@return The number of bytes copied in to value.
 *
 *	@relatesalso as_bytes
 */
uint32_t as_bytes_get_var_int(const as_bytes * bytes, uint32_t index, uint32_t * value);

/******************************************************************************
 *	SET AT INDEX
 *****************************************************************************/

/**
 *	Copy raw bytes of given size into the given `as_bytes` starting at
 *	specified index.
 *
 *	~~~~~~~~~~{.c}
 *	as_bytes_set(&bytes, 0, (uint8_t[]){'a','b','c'}, 3);
 *	~~~~~~~~~~
 *
 *	@param bytes	The bytes to write to.
 *	@param index	The position to write to.
 *	@param value 	The buffer to read from.
 *	@param size		The number of bytes to read from the value.
 *	
 *	@return On success, true. Otherwise an error occurred.
 *
 *	@relatesalso as_bytes
 */
bool as_bytes_set(as_bytes * bytes, uint32_t index, const uint8_t * value, uint32_t size);

/**
 *	Set a byte at given index.
 *
 *	~~~~~~~~~~{.c}
 *	as_bytes_set_byte(&bytes, 0, 'a');
 *	~~~~~~~~~~
 *
 *	@return On success, true. Otherwise an error occurred.
 *
 *	@relatesalso as_bytes
 */
static inline bool as_bytes_set_byte(as_bytes * bytes, uint32_t index, uint8_t value)
{
	return as_bytes_set(bytes, index, (uint8_t *) &value, 1);
}

/**
 *	Set 16 bit integer at given index.
 *
 *	~~~~~~~~~~{.c}
 *	as_bytes_set_int16(&bytes, 0, 1);
 *	~~~~~~~~~~
 *
 *	@return On success, true. Otherwise an error occurred.
 *
 *	@relatesalso as_bytes
 */
static inline bool as_bytes_set_int16(as_bytes * bytes, uint32_t index, int16_t value)
{
	return as_bytes_set(bytes, index, (uint8_t *) &value, 2);
}

/**
 *	Set 32 bit integer at given index.
 *
 *	~~~~~~~~~~{.c}
 *	as_bytes_set_int32(&bytes, 0, 2);
 *	~~~~~~~~~~
 *
 *	@return On success, true. Otherwise an error occurred.
 *
 *	@relatesalso as_bytes
 */
static inline bool as_bytes_set_int32(as_bytes * bytes, uint32_t index, int32_t value)
{
	return as_bytes_set(bytes, index, (uint8_t *) &value, 4);
}

/**
 *	Set 64 bit integer at given index.
 *
 *	~~~~~~~~~~{.c}
 *	as_bytes_set_int64(&bytes, 0, 3);
 *	~~~~~~~~~~
 *
 *	@return On success, true. Otherwise an error occurred.
 *
 *	@relatesalso as_bytes
 */
static inline bool as_bytes_set_int64(as_bytes * bytes, uint32_t index, int64_t value)
{
	return as_bytes_set(bytes, index, (uint8_t *) &value, 8);
}

/**
 *	Set double at given index.
 *
 *	~~~~~~~~~~{.c}
 *	as_bytes_set_double(&bytes, 0, 4.4);
 *	~~~~~~~~~~
 *
 *	@return On success, true. Otherwise an error occurred.
 *
 *	@relatesalso as_bytes
 */
static inline bool as_bytes_set_double(as_bytes * bytes, uint32_t index, double value)
{
	return as_bytes_set(bytes, index, (uint8_t *) &value, 8);
}

/**
 *	Encode an integer in 7-bit format.
 *	The high bit indicates if more bytes are used.
 *
 *	~~~~~~~~~~{.c}
 *	as_bytes_set_var_int(&bytes, 0, 36);
 *	~~~~~~~~~~
 *
 *	The `bytes` must be sufficiently sized for the data being written.
 *	To ensure the `bytes` is allocated sufficiently, you will need to call
 *	`as_bytes_ensure()`.
 *
 *	@return The number of bytes copied into byte array.
 *
 *	@relatesalso as_bytes
 */
uint32_t as_bytes_set_var_int(const as_bytes * bytes, uint32_t index, uint32_t value);

/******************************************************************************
 *	APPEND TO THE END
 *****************************************************************************/

/**
 *	Append raw bytes of given size.
 *
 *	~~~~~~~~~~{.c}
 *	uint8_t value[3] = {'a','b','c'};
 *
 *	as_bytes_append(&bytes, value, 3);
 *	~~~~~~~~~~
 *	
 *	@param bytes	The bytes to append to.
 *	@param value	The buffer to read from.
 *	@param size		The number of bytes to read from the value.
 *
 *	@return On success, true. Otherwise an error occurred.
 *
 *	@relatesalso as_bytes
 */
bool as_bytes_append(as_bytes * bytes, const uint8_t * value, uint32_t size);

/**
 *	Append a uint8_t (byte).
 *
 *	~~~~~~~~~~{.c}
 *	as_bytes_append_byte(&bytes, 'a');
 *	~~~~~~~~~~
 *
 *	@return On success, true. Otherwise an error occurred.
 *
 *	@relatesalso as_bytes
 */
static inline bool as_bytes_append_byte(as_bytes * bytes, uint8_t value)
{
	return as_bytes_append(bytes, (uint8_t *) &value, 1);
}

/**
 *	Append an int16_t value.
 *
 *	~~~~~~~~~~{.c}
 *	as_bytes_append_int16(&bytes, 123);
 *	~~~~~~~~~~
 *
 *	@return On success, true. Otherwise an error occurred.
 *
 *	@relatesalso as_bytes
 */
static inline bool as_bytes_append_int16(as_bytes * bytes, int16_t value)
{
	return as_bytes_append(bytes, (uint8_t *) &value, 2);
}

/**
 *	Append an int32_t value.
 *
 *	~~~~~~~~~~{.c}
 *	as_bytes_append_int32(&bytes, 123);
 *	~~~~~~~~~~
 *
 *	@return On success, true. Otherwise an error occurred.
 *
 *	@relatesalso as_bytes
 */
static inline bool as_bytes_append_int32(as_bytes * bytes, int32_t value)
{
	return as_bytes_append(bytes, (uint8_t *) &value, 4);
}

/**
 *	Append an int64_t value.
 *
 *	~~~~~~~~~~{.c}
 *	as_bytes_append_int64(&bytes, 123);
 *	~~~~~~~~~~
 *
 *	@return On success, true. Otherwise an error occurred.
 *
 *	@relatesalso as_bytes
 */
static inline bool as_bytes_append_int64(as_bytes * bytes, int64_t value)
{
	return as_bytes_append(bytes, (uint8_t *) &value, 8);
}

/**
 *	Append a double value.
 *
 *	~~~~~~~~~~{.c}
 *	as_bytes_append_double(&bytes, 123.456);
 *	~~~~~~~~~~
 *
 *	@return On success, true. Otherwise an error occurred.
 *
 *	@relatesalso as_bytes
 */
static inline bool as_bytes_append_double(as_bytes * bytes, double value)
{
	return as_bytes_append(bytes, (uint8_t *) &value, 8);
}

/******************************************************************************
 *	MODIFIES BUFFER
 *****************************************************************************/

/**
 *	Truncate the bytes' buffer. The size specifies the number of bytes to 
 *	remove from the end of the buffer. 
 *
 *	This means, if the buffer has size of 100, and we truncate 10, then
 *	the remaining size is 90. 

 *	Truncation does not modify the capacity of the buffer.
 *	
 *	~~~~~~~~~~{.c}
 *	as_bytes_truncate(&bytes, 10);
 *	~~~~~~~~~~
 *
 *	@param bytes	The bytes to truncate.
 *	@param n		The number of bytes to remove from the end.
 *	
 *	@return On success, true. Otherwise an error occurred.
 *
 *	@relatesalso as_bytes
 */
bool as_bytes_truncate(as_bytes * bytes, uint32_t n);

/**
 *	Ensure the bytes buffer can handle `capacity` bytes.
 *		
 *	If `resize` is true and `capacity` exceeds the capacity of the bytes's 
 *	buffer, then resize the capacity of the buffer to `capacity` bytes. If the 
 *	buffer was heap allocated, then `cf_realloc()` will be used to resize. If the 
 *	buffer was stack allocated, it will be converted to a heap allocated buffer 
 *	using cf_malloc() and then its contents will be copied into the new heap 
 *	allocated  buffer.
 *
 *	If `resize` is false, and if the capacity is not sufficient, then return
 *	false.
 *	
 *	~~~~~~~~~~{.c}
 *	as_bytes_ensure(&bytes, 100, true);
 *	~~~~~~~~~~
 *	
 *	@param bytes	The bytes to ensure the capacity of.
 *	@param capacity	The total number of bytes to ensure bytes can handle.
 *	@param resize	If true and capacity is not sufficient, then resize the buffer.
 *
 *	@return On success, true. Otherwise an error occurred.
 *
 *	@relatesalso as_bytes
 */
bool as_bytes_ensure(as_bytes * bytes, uint32_t capacity, bool resize);


/**
 *	Get the bytes value.
 *
 *	@deprecated Use as_bytes_get() instead.
 *
 *	@relatesalso as_bytes
 */
static inline uint8_t * as_bytes_tobytes(const as_bytes * bytes, uint32_t * size) 
{
	if ( !bytes ) return NULL;

	if ( size ) {
		*size = bytes->size;
	}

	return bytes->value;
}

/******************************************************************************
 *	CONVERSION FUNCTIONS
 *****************************************************************************/

/**
 *	Convert to an as_val.
 *
 *	@relatesalso as_bytes
 */
static inline as_val * as_bytes_toval(const as_bytes * b) 
{
	return (as_val *) b;
}

/**
 *	Convert from an as_val.
 *
 *	@relatesalso as_bytes
 */
static inline as_bytes * as_bytes_fromval(const as_val * v) 
{
	return as_util_fromval(v, AS_BYTES, as_bytes);
}

/******************************************************************************
 *	as_val FUNCTIONS
 *****************************************************************************/

/**
 *	@private
 *	Internal helper function for destroying an as_val.
 */
void as_bytes_val_destroy(as_val * v);

/**
 *	@private
 *	Internal helper function for getting the hashcode of an as_val.
 */
uint32_t as_bytes_val_hashcode(const as_val * v);

/**
 *	@private
 *	Internal helper function for getting the string representation of an as_val.
 */
char * as_bytes_val_tostring(const as_val * v);

#ifdef __cplusplus
} // end extern "C"
#endif
