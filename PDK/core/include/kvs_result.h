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


#ifndef KVS_RESULT_H
#define KVS_RESULT_H

#ifdef __cplusplus
extern "C" {
#endif

// API return value
typedef enum {
  KVS_SUCCESS                     = 0,        // Successful
  KVS_ERR_BUFFER_SMALL            = 0x001,    // buffer space is not enough
  KVS_ERR_DEV_CAPAPCITY           = 0x002,    // device does not have enough space. Key Space size is too big
  KVS_ERR_DEV_NOT_EXIST           = 0x003,    // no device with the dev_hd exists
  KVS_ERR_KS_CAPACITY             = 0x004,    // key space does not have enough space
  KVS_ERR_KS_EXIST                = 0x005,    // key space is already created with the same name
  KVS_ERR_KS_INDEX                = 0x006,    // index is not valid
  KVS_ERR_KS_NAME                 = 0x007,    // key space name is not valid
  KVS_ERR_KS_NOT_EXIST            = 0x008,    // key space does not exist
  KVS_ERR_KS_NOT_OPEN             = 0x009,    // key space does not open
  KVS_ERR_KS_OPEN                 = 0x00A,    // key space is already opened
  KVS_ERR_ITERATOR_FILTER_INVALID = 0x00B,    // iterator filter(match bitmask and pattern) is not valid
  KVS_ERR_ITERATOR_MAX            = 0x00C,    // the maximum number of iterators that a device supports is opened
  KVS_ERR_ITERATOR_NOT_EXIST      = 0x00D,    // the iterator Key Group does not exist
  KVS_ERR_ITERATOR_OPEN           = 0x00E,    // iterator is already opened
  KVS_ERR_KEY_LENGTH_INVALID      = 0x00F,    // key is not valid (e.g., key length is not supported)
  KVS_ERR_KEY_NOT_EXIST           = 0x010,    // key does not exist
  KVS_ERR_OPTION_INVALID          = 0x011,    // an option is not supported in this implementation
  KVS_ERR_PARAM_INVALID           = 0x012,    // null input parameter
  KVS_ERR_SYS_IO                  = 0x013,    // I/O error occurs
  KVS_ERR_VALUE_LENGTH_INVALID    = 0x014,    // value length is out of range
  KVS_ERR_VALUE_OFFSET_INVALID    = 0x015,    // value offset is out of range
  KVS_ERR_VALUE_OFFSET_MISALIGNED = 0x016,    // offset of value is required to be aligned to KVS_ALIGNMENT_UNIT
  KVS_ERR_VALUE_UPDATE_NOT_ALLOWED = 0x017,   // key exists but value update is not allowed
  KVS_ERR_DEV_NOT_OPENED          = 0x018,    // device was not opened yet
} kvs_result;

#ifdef __cplusplus
} // extern "C"
#endif

#endif
