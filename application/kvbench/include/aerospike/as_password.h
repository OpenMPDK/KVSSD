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

#include <string.h>
#include "citrusleaf/cf_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 *	The user name size including null byte.
 */
#define AS_USER_SIZE 64

/**
 *	Size of hash buffer including null byte, padded to 8 byte boundary.
 */
#define AS_PASSWORD_HASH_SIZE 64

/**
 *	Generate random salt value.
 *	Return true if salt was generated.
 */
bool
as_password_gen_salt(char* salt);

/**
 *	Create bcrypt hash of password.
 *	Return true if hash was generated.
 */
bool
as_password_gen_hash(const char* password, const char* salt, char* hash);

/**
 *	Create bcrypt hash of password with constant salt.
 *	Return true if hash was generated.
 */
bool
as_password_gen_constant_hash(const char* password, char* hash);

/**
 *	If the input password is not hashed, convert to bcrypt hashed password.
 *	Return true if hash was successful.
 */
bool
as_password_get_constant_hash(const char* password, char* hash);

/**
 *	Prompt for input password from command line if input password is empty.
 *	If the input password is not hashed, convert to bcrypt hashed password.
 *	Return true if hash was successful.
 */
bool
as_password_prompt_hash(const char* password, char* hash);

/**
 *	Verify password hash. Hash length should always be 60.
 *	Return true if hashes are equal.
 */
static inline bool
as_password_verify(const char* hash1, const char* hash2) {
	return ! memcmp(hash1, hash2, 60);
}

#ifdef __cplusplus
} // end extern "C"
#endif
