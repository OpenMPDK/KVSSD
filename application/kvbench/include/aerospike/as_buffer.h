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

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *	TYPES
 ******************************************************************************/

/**
 *	Byte Buffer.
 */
typedef struct as_buffer_s {

	/**
	 *	Number of bytes allocated to the buffer
	 */
	uint32_t capacity;

	/**
	 *	Number of bytes used
	 */
	uint32_t size;

	/**
	 *	Bytes of the buffer
	 */
	uint8_t * data;

} as_buffer;

/******************************************************************************
 *	INLINE FUNCTION DEFINITIONS
 ******************************************************************************/

/**
 *	Initialize the buffer to default values.
 */
int as_buffer_init(as_buffer * b);

/**
 *	Free the resources allocated to the buffer.
 */
void as_buffer_destroy(as_buffer * b);

#ifdef __cplusplus
} // end extern "C"
#endif
