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

#if defined(__linux__)

#include <netinet/in.h>
#include <asm/byteorder.h>

#define cf_swap_to_be16(_n) __cpu_to_be16(_n)
#define cf_swap_to_le16(_n) __cpu_to_le16(_n)
#define cf_swap_from_be16(_n) __be16_to_cpu(_n)
#define cf_swap_from_le16(_n) __le16_to_cpu(_n)

#define cf_swap_to_be32(_n) __cpu_to_be32(_n)
#define cf_swap_to_le32(_n) __cpu_to_le32(_n)
#define cf_swap_from_be32(_n) __be32_to_cpu(_n)
#define cf_swap_from_le32(_n) __le32_to_cpu(_n)

#define cf_swap_to_be64(_n) __cpu_to_be64(_n)
#define cf_swap_to_le64(_n) __cpu_to_le64(_n)
#define cf_swap_from_be64(_n) __be64_to_cpu(_n)
#define cf_swap_from_le64(_n) __le64_to_cpu(_n)

#endif // __linux__

#if defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#include <arpa/inet.h>

#define cf_swap_to_be16(_n) OSSwapHostToBigInt16(_n)
#define cf_swap_to_le16(_n) OSSwapHostToLittleInt16(_n)
#define cf_swap_from_be16(_n) OSSwapBigToHostInt16(_n)
#define cf_swap_from_le16(_n) OSSwapLittleToHostInt16(_n)

#define cf_swap_to_be32(_n) OSSwapHostToBigInt32(_n)
#define cf_swap_to_le32(_n) OSSwapHostToLittleInt32(_n)
#define cf_swap_from_be32(_n) OSSwapBigToHostInt32(_n)
#define cf_swap_from_le32(_n) OSSwapLittleToHostInt32(_n)

#define cf_swap_to_be64(_n) OSSwapHostToBigInt64(_n)
#define cf_swap_to_le64(_n) OSSwapHostToLittleInt64(_n)
#define cf_swap_from_be64(_n) OSSwapBigToHostInt64(_n)
#define cf_swap_from_le64(_n) OSSwapLittleToHostInt64(_n)

#endif // __APPLE__

#if defined(CF_WINDOWS)
#include <stdint.h>
#include <stdlib.h>
#include <WinSock2.h>

#define cf_swap_to_be16(_n) _byteswap_uint16(_n)
#define cf_swap_to_le16(_n) (_n)
#define cf_swap_from_be16(_n) _byteswap_uint16(_n)
#define cf_swap_from_le16(_n) (_n)

#define cf_swap_to_be32(_n) _byteswap_uint32(_n)
#define cf_swap_to_le32(_n) (_n)
#define cf_swap_from_be32(_n) _byteswap_uint32(_n)
#define cf_swap_from_le32(_n) (_n)

#define cf_swap_to_be64(_n) _byteswap_uint64(_n)
#define cf_swap_to_le64(_n) (_n)
#define cf_swap_from_be64(_n) _byteswap_uint64(_n)
#define cf_swap_from_le64(_n) (_n)
#endif // CF_WINDOWS

static inline double
cf_swap_to_big_float64(double d)
{
	uint64_t i = cf_swap_to_be64(*(uint64_t*)&d);
	return *(double*)&i;
}

static inline double
cf_swap_to_little_float64(double d)
{
	uint64_t i = cf_swap_to_le64(*(uint64_t*)&d);
	return *(double*)&i;
}

static inline double
cf_swap_from_big_float64(double d)
{
	uint64_t i = cf_swap_from_be64(*(uint64_t*)&d);
	return *(double*)&i;
}

static inline double
cf_swap_from_little_float64(double d)
{
	uint64_t i = cf_swap_from_le64(*(uint64_t*)&d);
	return *(double*)&i;
}
