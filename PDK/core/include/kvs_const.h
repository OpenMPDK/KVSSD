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


#ifndef KVS_CONST_H
#define KVS_CONST_H

#ifdef __cplusplus
extern "C" {
#endif

#define KVS_MIN_KEY_LENGTH 4
#define KVS_MAX_KEY_LENGTH 255
#define KVS_MIN_VALUE_LENGTH 0
#define KVS_MAX_VALUE_LENGTH (2*1024*1024)
#define KVS_OPTIMAL_VALUE_LENGTH 4096
#define KVS_ALIGNMENT_UNIT 512 /*value of KVS_ALIGNMENT_UNIT must be a power of 2 currently */
#define KVS_VALUE_LENGTH_ALIGNMENT_UNIT 4 /*value of KV_VALUE_LENGTH_ALIGNMENT_UNIT must be a power of 2 currently */
#define KVS_MAX_ITERATE_HANDLE 16
#define G_ITER_KEY_SIZE_FIXED 16
#define KVS_MAX_KEY_GROUP_BYTES 4
#define KVS_ITERATOR_BUFFER_SIZE (32*1024)
#define MAX_CONT_PATH_LEN 255
#define MAX_KEYSPACE_NAME_LEN MAX_CONT_PATH_LEN


#ifdef __cplusplus
} // extern "C"
#endif
#endif
