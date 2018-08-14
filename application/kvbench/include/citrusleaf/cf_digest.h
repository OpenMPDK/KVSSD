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

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <openssl/ripemd.h>

#ifdef __APPLE__
// Openssl is deprecated on mac, but the library is still included.
// Since RIPEMD160 is not supported by the new mac common cryto system library,
// openssl is still used.  Save old settings and disable deprecated warnings.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 * CONSTANTS & TYPES
 *****************************************************************************/

#define CF_DIGEST_KEY_SZ RIPEMD160_DIGEST_LENGTH

typedef struct cf_digest_s {
	uint8_t digest[CF_DIGEST_KEY_SZ];
} cf_digest;

extern const cf_digest cf_digest_zero;

/******************************************************************************
 * INLINE FUNCTIONS
 *****************************************************************************/

static inline void
cf_digest_compute(const void *data, size_t len, cf_digest *d)
{
	RIPEMD160((const unsigned char *)data, len, (unsigned char *)d->digest);
}

static inline void
cf_digest_compute2(const void *data1, size_t len1, const void *data2, size_t len2, cf_digest *d)
{
	if (len1 == 0) {
		RIPEMD160((const unsigned char *)data2, len2, (unsigned char *)d->digest);
	}
	else {
		RIPEMD160_CTX c;
		RIPEMD160_Init(&c);
		RIPEMD160_Update(&c, data1, len1);
		RIPEMD160_Update(&c, data2, len2);
		RIPEMD160_Final((unsigned char *)(d->digest), &c);
	}
}

static inline int
cf_digest_compare(const cf_digest *d1, const cf_digest *d2)
{
	return memcmp(d1->digest, d2->digest, CF_DIGEST_KEY_SZ);
}

static inline void
cf_digest_string(const cf_digest *d, char* output)
{
	char* p = output;

	*p++ = '0';
	*p++ = 'x';

	for (int i = 0; i < CF_DIGEST_KEY_SZ; i++) {
		sprintf(p, "%02x", d->digest[i]);
		p += 2;
	}
}

/*****************************************************************************/

#ifdef __cplusplus
} // end extern "C"
#endif

#ifdef __APPLE__
// Restore old settings.
#pragma GCC diagnostic pop
#endif
