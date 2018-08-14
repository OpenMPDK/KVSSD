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

#include <stdbool.h>

#include <aerospike/as_serializer.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AS_PACKER_BUFFER_SIZE 8192

#define AS_PACKED_MAP_FLAG_NONE				0x00
#define AS_PACKED_MAP_FLAG_K_ORDERED		0x01
#define AS_PACKED_MAP_FLAG_V_ORDERED		0x02 // not allowed on its own
#define AS_PACKED_MAP_FLAG_PRESERVE_ORDER	0x08
#define AS_PACKED_MAP_FLAG_KV_ORDERED	(AS_PACKED_MAP_FLAG_K_ORDERED | AS_PACKED_MAP_FLAG_V_ORDERED)

typedef struct as_packer_buffer {
	struct as_packer_buffer *next;
	unsigned char *buffer;
	int length;
} as_packer_buffer;

typedef struct as_packer {
	struct as_packer_buffer *head;
	struct as_packer_buffer *tail;
	unsigned char *buffer;
	int offset;
	int capacity;
} as_packer;

typedef struct as_unpacker {
	const unsigned char *buffer;
	int offset;
	int length;
} as_unpacker;

typedef struct as_msgpack_ext_s {
	const uint8_t *data;	// pointer to ext contents
	uint32_t size;			// size of ext contents
	uint32_t type_offset;	// offset where the type field is located
	uint8_t type;			// type of ext contents
} as_msgpack_ext;

typedef enum msgpack_compare_e {
	MSGPACK_COMPARE_ERROR	= -2,
	MSGPACK_COMPARE_END		= -1,
	MSGPACK_COMPARE_LESS	= 0,
	MSGPACK_COMPARE_EQUAL	= 1,
	MSGPACK_COMPARE_GREATER = 2,
} msgpack_compare_t;

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/

as_serializer *as_msgpack_new();
as_serializer *as_msgpack_init(as_serializer *);

/**
 * @return 0 on success
 */
int as_pack_val(as_packer *pk, const as_val *val);
/**
 * @return 0 on success
 */
int as_unpack_val(as_unpacker *pk, as_val **val);

/******************************************************************************
 * Pack direct functions
 ******************************************************************************/

static inline uint32_t as_pack_nil_size()
{
	return 1;
}
static inline uint32_t as_pack_bool_size()
{
	return 1;
}
int as_pack_nil(as_packer *pk);
int as_pack_bool(as_packer *pk, bool val);

uint32_t as_pack_uint64_size(uint64_t val);
uint32_t as_pack_int64_size(int64_t val);
int as_pack_uint64(as_packer *pk, uint64_t val);
int as_pack_int64(as_packer *pk, int64_t val);

static inline uint32_t as_pack_float_size()
{
	return 1 + 4;
}
static inline uint32_t as_pack_double_size()
{
	return 1 + 8;
}
/**
 * Pack a float.
 * @return 0 on success
 */
int as_pack_float(as_packer *pk, float val);
int as_pack_double(as_packer *pk, double val);

uint32_t as_pack_str_size(uint32_t str_sz);
uint32_t as_pack_bin_size(uint32_t buf_sz);
/**
 * Pack a str.
 * @return 0 on success
 */
int as_pack_str(as_packer *pk, const uint8_t *buf, uint32_t sz);
int as_pack_bin(as_packer *pk, const uint8_t *buf, uint32_t sz);
/**
 * Pack a list header with ele_count.
 * @return 0 on success
 */
int as_pack_list_header(as_packer *pk, uint32_t ele_count);
/**
 * Get packed header size for list with ele_count.
 * @return header size in bytes
 */
uint32_t as_pack_list_header_get_size(uint32_t ele_count);
/**
 * Pack a map header with ele_count.
 * @return 0 on success
 */
int as_pack_map_header(as_packer *pk, uint32_t ele_count);
/**
 * Get packed header size for map with ele_count.
 * @return header size in bytes
 */
static inline uint32_t as_pack_map_header_get_size(uint32_t ele_count)
{
	return as_pack_list_header_get_size(ele_count);
}
/**
 * Get size of an ext header.
 * @param content_size size in bytes of ext contents
 * @return size of header in bytes
 */
uint32_t as_pack_ext_header_get_size(uint32_t content_size);
/**
 * Pack an ext type.
 * @return 0 on success
 */
int as_pack_ext_header(as_packer *pk, uint32_t content_size, uint8_t type);
int as_pack_buf_ext_header(uint8_t *buf, uint32_t size, uint32_t content_size, uint8_t type);

int as_pack_append(as_packer *pk, const unsigned char *buf, uint32_t sz);

/******************************************************************************
 * Unpack direct functions
 ******************************************************************************/

/**
 * Check next element without consuming any bytes.
 * @return type of next element
 */
as_val_t as_unpack_peek_type(const as_unpacker *pk);
as_val_t as_unpack_buf_peek_type(const uint8_t *buf, uint32_t size);
/**
 * Check next element without consuming any bytes.
 * @return true if ext type
 */
bool as_unpack_peek_is_ext(const as_unpacker *pk);
/**
 * Get size of packed value.
 * @return negative int on error, size on success
 */
int64_t as_unpack_size(as_unpacker *pk);
/**
 * Get size of packed blob.
 * @return negative int on error, size on success
 */
int64_t as_unpack_blob_size(as_unpacker *pk);
/**
 * Unpack integer.
 * @return 0 on success
 */
int as_unpack_int64(as_unpacker *pk, int64_t *i);
int as_unpack_uint64(as_unpacker *pk, uint64_t *i);
/**
 * Unpack double.
 * @return 0 on success
 */
int as_unpack_double(as_unpacker *pk, double *x);
/**
 *  Unpack str (or bin).
 *  @return NULL on failure
 */
const uint8_t *as_unpack_str(as_unpacker *pk, uint32_t *sz_r);
/**
 *  Unpack bin (or str).
 *  @return NULL on failure
 */
const uint8_t *as_unpack_bin(as_unpacker *pk, uint32_t *sz_r);
/**
 * Unpack extension type.
 * @return true on success
 */
int as_unpack_ext(as_unpacker *pk, as_msgpack_ext *ext);
/**
 * Unpack list element count from buffer.
 */
int64_t as_unpack_buf_list_element_count(const uint8_t *buf, uint32_t size);
/**
 * Get element count of packed list.
 * @return negative int on failure, element count on success
 */
int64_t as_unpack_list_header_element_count(as_unpacker *pk);
/**
 * Unpack map element count from buffer.
 */
int64_t as_unpack_buf_map_element_count(const uint8_t *buf, uint32_t size);
/**
 * Get element count of packed map.
 * @return negative int on failure, element count on success
 */
int64_t as_unpack_map_header_element_count(as_unpacker *pk);

/**
 * Compare two msgpack buffers.
 */
msgpack_compare_t as_unpack_buf_compare(const uint8_t *buf1, uint32_t size1, const uint8_t *buf2, uint32_t size2);
msgpack_compare_t as_unpack_compare(as_unpacker *pk1, as_unpacker *pk2);
/**
 * Compare two msgpack buffers.
 * @return true if buf1 < buf2
 */
static inline bool as_unpack_buf_is_less(const uint8_t *buf1, uint32_t size1, const uint8_t *buf2, uint32_t size2)
{
	return as_unpack_buf_compare(buf1, size1, buf2, size2) == MSGPACK_COMPARE_LESS;
}

#ifdef __cplusplus
} // end extern "C"
#endif
