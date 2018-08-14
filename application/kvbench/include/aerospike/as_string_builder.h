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

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *	TYPES
 ******************************************************************************/

/**
 *	Fast, non thread safe string builder implementation.
 */
typedef struct as_string_builder_s {
	/**
	 *	String Buffer
	 */
	char* data;

	/**
	 *	Number of bytes allocated to the buffer
	 */
	uint32_t capacity;

	/**
	 *	String length of buffer.
	 */
	uint32_t length;
	
	/**
	 *	Allow resize.
	 */
	bool resize;
	
	/**
	 *	Should buffer be freed on destroy.
	 */
	bool free;
} as_string_builder;

/******************************************************************************
 *	MACROS
 ******************************************************************************/
	
/**
 *	Initialize string builder with a stack allocated buffer.
 */
#define as_string_builder_inita(__sb, __capacity, __resize)\
(__sb)->data = alloca(__capacity);\
(__sb)->data[0] = 0;\
(__sb)->capacity = (__capacity);\
(__sb)->length = 0;\
(__sb)->resize = (__resize);\
(__sb)->free = false;
	
/******************************************************************************
 *	INSTANCE FUNCTIONS
 ******************************************************************************/
	
/**
 *	Initialize string builder with a heap allocated buffer.
 */
void
as_string_builder_init(as_string_builder* sb, uint32_t capacity, bool resize);

/**
 *	Free the resources allocated to the buffer.
 */
void
as_string_builder_destroy(as_string_builder* sb);

/**
 *	Initialize to empty string from current state.
 *	Capacity remains unchanged.
 */
static inline void
as_string_builder_reset(as_string_builder* sb)
{
	sb->data[0] = 0;
	sb->length = 0;
}

/**
 *	Append null terminated string value to string buffer.
 *	Returns if successful or not.
 */
bool
as_string_builder_append(as_string_builder* sb, const char* value);

/**
 *	Append a single character to string buffer.
 *	Returns if successful or not.
 */
bool
as_string_builder_append_char(as_string_builder* sb, char value);

#ifdef __cplusplus
} // end extern "C"
#endif
