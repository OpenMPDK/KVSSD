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

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *	TYPES
 *****************************************************************************/

#if defined(__APPLE__) || defined(CF_WINDOWS)

#pragma pack(push, 1) // packing is now 1
typedef struct as_proto_s {
	uint64_t	version	:8;
	uint64_t	type	:8;
	uint64_t	sz		:48;
} as_proto;
#pragma pack(pop) // packing is back to what it was

#pragma pack(push, 1) // packing is now 1
typedef struct as_compressed_proto_s {
	as_proto	proto;
	uint64_t	uncompressed_sz;
} as_compressed_proto;
#pragma pack(pop) // packing is back to what it was

#pragma pack(push, 1) // packing is now 1
typedef struct as_msg_s {
/*00*/	uint8_t		header_sz;			// number of uint8_ts in this header
/*01*/	uint8_t		info1;				// bitfield about this request
/*02*/	uint8_t		info2;
/*03*/	uint8_t		info3;
/*04*/	uint8_t		unused;
/*05*/	uint8_t		result_code;
/*06*/	uint32_t	generation;
/*10*/	uint32_t	record_ttl;
/*14*/	uint32_t	transaction_ttl;
/*18*/	uint16_t	n_fields;			// size in uint8_ts
/*20*/	uint16_t	n_ops;				// number of operations
/*22*/	uint8_t		data[0];			// data contains first the fields, then the ops
} as_msg;
#pragma pack(pop) // packing is back to what it was

#pragma pack(push, 1) // packing is now 1
typedef struct as_proto_msg_s {
	as_proto  	proto;
	as_msg		m;
} as_proto_msg;
#pragma pack(pop) // packing is back to what it was

#else

typedef struct as_proto_s {
	uint8_t		version;
	uint8_t		type;
	uint64_t	sz:48;
	uint8_t		data[];
} __attribute__ ((__packed__)) as_proto;

typedef struct as_compressed_proto_s {
	as_proto	proto;
	uint64_t	uncompressed_sz;
	uint8_t		data[];					// compressed bytes
} __attribute__((__packed__)) as_compressed_proto;

typedef struct as_msg_s {
/*00*/	uint8_t		header_sz;			// number of uint8_ts in this header
/*01*/	uint8_t		info1;				// bitfield about this request
/*02*/	uint8_t		info2;
/*03*/	uint8_t		info3;
/*04*/	uint8_t		unused;
/*05*/	uint8_t		result_code;
/*06*/	uint32_t	generation;
/*10*/	uint32_t	record_ttl;
/*14*/	uint32_t	transaction_ttl;
/*18*/	uint16_t	n_fields;			// size in uint8_ts
/*20*/	uint16_t	n_ops;				// number of operations
/*22*/	uint8_t		data[];				// data contains first the fields, then the ops
} __attribute__((__packed__)) as_msg;

typedef struct as_proto_msg_s {
	as_proto  	proto;
	as_msg		m;
} __attribute__((__packed__)) as_proto_msg;

#endif

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/

void as_proto_swap_to_be(as_proto *m);
void as_proto_swap_from_be(as_proto *m);
void as_msg_swap_header_from_be(as_msg *m);

#ifdef __cplusplus
} // end extern "C"
#endif
