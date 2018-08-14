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
 * FUNCTIONS
 ******************************************************************************/

// Our base-64 encoding always pads with '=' so encoded length will always be a
// multiple of 4 bytes. Note that the length returned here does NOT include an
// extra byte for making a null-terminated string.
static inline uint32_t
cf_b64_encoded_len(uint32_t in_size)
{
	return ((in_size + 2) / 3) << 2;
}

void cf_b64_encode(const uint8_t* in, uint32_t in_size, char* out);

// The size returned here is the minimum required for an 'out' buffer passed in
// a decode method. Caller must ensure 'in_len' is a multiple of 4 bytes.
static inline uint32_t
cf_b64_decoded_buf_size(uint32_t in_len)
{
	return (in_len * 3) >> 2;
}

void cf_b64_decode(const char* in, uint32_t in_len, uint8_t* out, uint32_t* out_size);
void cf_b64_decode_in_place(uint8_t* in_out, uint32_t in_len, uint32_t* out_size);
bool cf_b64_validate_and_decode(const char* in, uint32_t in_len, uint8_t* out, uint32_t* out_size);
bool cf_b64_validate_and_decode_in_place(uint8_t* in_out, uint32_t in_len, uint32_t* out_size);

/******************************************************************************/

#ifdef __cplusplus
} // end extern "C"
#endif
