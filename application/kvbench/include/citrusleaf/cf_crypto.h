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
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <openssl/sha.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 * CONSTANTS
 ******************************************************************************/

#define CF_SHA_HEX_BUFF_LEN (SHA_DIGEST_LENGTH*2)

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/

bool cf_convert_sha1_to_hex(unsigned char * hash, unsigned char * sha1_hex_buff);

/******************************************************************************/

#ifdef __cplusplus
} // end extern "C"
#endif
