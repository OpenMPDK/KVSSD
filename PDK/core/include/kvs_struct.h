/**
 *   BSD LICENSE
 *
 *   Copyright (c) 2018 Samsung Electronics Co., Ltd.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Samsung Electronics Co., Ltd. nor the names of
 *       its contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#ifndef KVS_STRUCT_H
#define KVS_STRUCT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

// API library version
typedef struct {
  uint8_t major;      // API library major version number
  uint8_t minor;      // API library minor version number
  uint8_t micro;      // API library micro version number
} kvs_api_version;

// Operation code in API level
typedef enum {
  KVS_CMD_DELETE          =0x01,
  KVS_CMD_DELETE_GROUP    =0x02,
  KVS_CMD_EXIST           =0x03,
  KVS_CMD_ITER_CREATE     =0x04,
  KVS_CMD_ITER_DELETE     =0x05,
  KVS_CMD_ITER_NEXT       =0x06,
  KVS_CMD_RETRIEVE        =0x07,
  KVS_CMD_STORE           =0x08,
} kvs_context;

typedef enum {
  KVS_KEY_ORDER_NONE      = 0,    // [DEFAULT] key ordering is not defined in a Key Space
  KVS_KEY_ORDER_ASCEND    = 1,    // kvp are sorted in ascending key order in a Key Space
  KVS_KEY_ORDER_DESCEND   = 2,    // kvp are sorted in descending key order in a Key Space
} kvs_key_order;

typedef struct {
  kvs_key_order ordering;     // key ordering option in Key Space
} kvs_option_key_space;

typedef struct {
  bool kvs_delete_error;      //[OPTION] return error when the key does not exist
} kvs_option_delete;

typedef enum {
  KVS_ITERATOR_KEY = 0,       // [DEFAULT] iterator command retrieves only key entries without values
  KVS_ITERATOR_KEY_VALUE = 1,  // iterator command retrieves key and value pairs
} kvs_iterator_type;

typedef struct {
  kvs_iterator_type iter_type;    // iterator type
} kvs_option_iterator;

typedef struct {
  bool kvs_retrieve_delete;       // [OPTION] retrieve the value of the key value pair and delete the key value pair
} kvs_option_retrieve;

typedef enum {
  KVS_STORE_POST          =0,     // [DEFAULT] Overwrite value if the key exists, otherwise create the kvp
  KVS_STORE_UPDATE_ONLY   =1,     // Overwrite value if the key exists, otherwise return KVS_KEY_NOT_EXIST error
  KVS_STORE_NOOVERWRITE   =2,     // Return KVS_ERR_VALUE_UPDATE_NOT_ALLOWED if the key exists, otherwise create the kvp
  KVS_STORE_APPEND        =3,     // Append the value if the key exists, otherwise create the kvp
} kvs_store_type;

typedef enum {
  KVS_NOASSOCIATION       =0,     // no association
  KVS_ASSOCIATION_STREAM  =1,     // stream association
} kvs_association_type;

typedef struct {
  void *key;          // a void pointer refers to a key byte string
  uint16_t length;    // key length in bytes
} kvs_key;

typedef struct {
  void *value;                    // start address of buffer for value byte stream
  uint32_t length;                // the length of buffer in bytes for value byte stream
  uint32_t actual_value_size;     // actual value size in bytes that is stored in a device
  uint32_t offset;                // [OPTION] offset to indicate the offset of value stored in device
} kvs_value;

typedef struct {
  kvs_association_type assoc_type;  // association type for a group of associated key value pairs.
  uint16_t assoc_hint;              // association hint (e.g. stream id)
} kvs_association;

typedef struct {
  kvs_store_type st_type;         // store operation type
  kvs_association *assoc;         // association
} kvs_option_store;

struct _kvs_device_handle;
struct _kvs_key_space_handle;
typedef struct _kvs_device_handle* kvs_device_handle;    // type definition of kvs_device_handle
typedef struct _kvs_key_space_handle* kvs_key_space_handle; // type definition of kvs_key_space_handle
typedef uint8_t kvs_iterator_handle;  // type definition of kvs_iterator_handle

typedef struct {
  uint32_t name_len;          // Key Space name length
  char *name;                 // Key Space name specified by the application
} kvs_key_space_name;

typedef struct {
  bool opened;              // is this Key Space opened
  uint64_t capacity;          // Key Space capacity in bytes
  uint64_t free_size;         // available space of Key Space in bytes
  uint64_t count;             // key value pair count that exist in this Key Space
  kvs_key_space_name *name;   // Key Space name
} kvs_key_space;

typedef struct {
  uint64_t capacity;                      // device capacity in bytes
  uint64_t unalloc_capacity;              // device capacity in bytes that has not been allocated to any key space
  uint32_t max_value_len;                 // max length of value in bytes that device is able to support
  uint32_t max_key_len;                   // max length of key in bytes that device is able to support
  uint32_t optimal_value_len;             // optimal value size
  uint32_t optimal_value_granularity;     // optimal value granularity
  void     *extended_info;                // vendor specific extended device information
} kvs_device;

typedef struct {
  uint32_t num_keys;          // the number of key entries in the list
  kvs_key *keys;              // keys checked for existence
  uint32_t length;            // input buffer size(result_buffer) and returned buffer size
  uint8_t *result_buffer;     // exist status info
} kvs_exist_list;

typedef struct {
  uint8_t bitmask[KVS_MAX_KEY_GROUP_BYTES];         // bit mask for bit pattern to use
  uint8_t bit_pattern[KVS_MAX_KEY_GROUP_BYTES];     // bit pattern for filter
} kvs_key_group_filter;

typedef struct {
  uint32_t num_entries;   // the number of iterator entries in the list
  bool end;               // represent if there are more keys to iterate (end =0) or not (end = 1)
  uint32_t size;          // the it_list buffer size as an input and returned data size in the buffer in bytes
  uint8_t *it_list;       // iterator list.
} kvs_iterator_list;

typedef struct {
  kvs_context context;            // operation type
  kvs_key_space_handle ks_hd;    // key space handle
  kvs_key *key;                   // key data structure
  kvs_value *value;               // value data structure
  void *option;                   // operation option
  void *private1;
  void *private2;
  kvs_result result;              // IO result
  kvs_iterator_handle iter_hd;   // iterator handle
  union {
    kvs_iterator_list* iter_list;
    kvs_exist_list* list;
  }result_buffer;
} kvs_postprocess_context;

typedef void(*kvs_postprocess_function)(kvs_postprocess_context *ctx);   // asynchronous notification callback (valid only for async I/O)

typedef struct {
  uint16_t key_len;   // key length in bytes
  uint8_t *key;       // key
  uint32_t value_len; // value length in bytes
} kvs_kvp_info;

#ifdef __cplusplus
} // extern "C"
#endif

#endif
