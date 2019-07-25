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


// kvs_result messages;	
typedef enum {	
  // generic command status	
  KVS_SUCCESS=0	,                     // success
	
  // errors	
  KVS_ERR_BUFFER_SMALL=0x001	,     // provided buffer size too small
  KVS_ERR_COMMAND_INITIALIZED=0x002 , // initialized by caller before submission
  KVS_ERR_COMMAND_SUBMITTED=0x003  ,  // the beginning state after being accepted into a submission queue
  KVS_ERR_DEV_CAPACITY=0x004	,     // device does not have enough space
  KVS_ERR_DEV_INIT=0x005	,     // device initialization failed
  KVS_ERR_DEV_INITIALIZED=0x006	,     // device was already initialized
  KVS_ERR_DEV_NOT_EXIST=0x007	,     // no device exists 
  KVS_ERR_DEV_SANITIZE_FAILED=0x008 , // the previous sanitize operation failed
  KVS_ERR_DEV_SANIZE_IN_PROGRESS=0x009,// the sanitization operation is in progress
  KVS_ERR_ITERATOR_COND_INVALID=0x00A,// iterator condition is not valid
  KVS_ERR_ITERATOR_MAX=0x00B	,     // Exceeded max number of opened iterators
  KVS_ERR_ITERATOR_NOT_EXIST=0x00C ,  // no iterator exists
  KVS_ERR_ITERATOR_OPEN=0x00D	,     // iterator is already open
  KVS_ERR_KEY_EXIST=0x00E	,    // given key already exists (with KVS_STORE_IDEMPOTENT option)
  KVS_ERR_KEY_INVALID=0x00F	,    // key format is invalid
  KVS_ERR_KEY_LENGTH_INVALID=0x010	, // key length is out of range (unsupported key length)
  KVS_ERR_KEY_NOT_EXIST=0x011	, // given key doesn’t exist
  KVS_ERR_OPTION_INVALID=0x012	, // device does not support the specified options
  KVS_ERR_PARAM_INVALID=0x013	, // no input pointer can be NULL
  KVS_ERR_PURGE_IN_PROGRESS=0x014  	, // purge operation is in progress
  KVS_ERR_QUEUE_CQID_INVALID=0x015	, // completion queue identifier is invalid
  KVS_ERR_QUEUE_DELETION_INVALID=0x016	, // cannot delete completion queue since submission queue has not been fully deleted
  KVS_ERR_QUEUE_IN_SUTDOWN=0x017	, // queue in shutdown mode
  KVS_ERR_QUEUE_IS_FULL=0x018	, // queue is full, unable to accept mor IO
  KVS_ERR_QUEUE_MAX_QUEUE=0x019	, // maximum number of queues are already created
  KVS_ERR_QUEUE_QID_INVALID=0x01A	, // queue identifier is invalid
  KVS_ERR_QUEUE_QSIZE_INVALID=0x01B, // queue size is invalid
  KVS_ERR_QUEUE_SQID_INVALID=0x01C	, // submission queue identifier is invalid
  KVS_ERR_SYS_BUSY=0x01D  	, //iterator next call that can return empty results, retry is recommended
  KVS_ERR_SYS_IO=0x01E	, // host failed to communicate with the device
  KVS_ERR_TIMEOUT=0x01F	, // timer expired and no operation is completed yet.
  KVS_ERR_UNCORRECTIBLE=0x020	, // uncorrectable error occurs
  KVS_ERR_VALUE_LENGTH_INVALID=0x021	, // value length is out of range
  KVS_ERR_VALUE_LENGTH_MISALIGNED=0x022	, // value length is misaligned. Value length shall be multiples of 4 bytes.
  KVS_ERR_VALUE_OFFSET_INVALID=0x023    , // value offset is invalid meaning that offset is out of bound. 
  KVS_ERR_VALUE_UPDATE_NOT_ALLOWED=0x024	, // key exists but value update is not allowed
  KVS_ERR_VENDOR=0x025	, // vendor-specific error is returned, check the system log for more details
  KVS_ERR_PERMISSION = 0x26,     // unable to open device due to permission
  KVS_ERR_ENV_NOT_INITIALIZED= 0x27	,     // environment was not initialized
  KVS_ERR_DEV_NOT_OPENED= 0x28	,     // device was not opened
  KVS_ERR_DEV_ALREADY_OPENED= 0x29,     // device is already opened
  KVS_ERR_DEV_PATH_TOO_LONG=0x02A,     // the length of dev path more than 255
  KVS_ERR_ITERATOR_NUM_OUT_RANGE=0x02B, //the number of iterator required out of range(equal 0, or bigger than 16)
  KVS_ERR_DD_UNSUPPORTED=0x02C,   //device driver does not support.
  KVS_ERR_ITERATOR_BUFFER_SIZE=0x02D, // buffer (iter_list->it_list) size in kvs_iterator_list error

  // From user driver	
  KVS_ERR_CACHE_INVALID_PARAM=0x200	, // (kv cache) invalid parameters
  KVS_ERR_CACHE_NO_CACHED_KEY=0x201	, // (kv cache) cache miss
  KVS_ERR_DD_INVALID_QUEUE_TYPE=0x202	, // queue type is invalid
  KVS_ERR_DD_NO_AVAILABLE_RESOURCE=0x203	, // no more resource is available
  KVS_ERR_DD_NO_DEVICE=0x204	, // no device exist
  KVS_ERR_DD_UNSUPPORTED_CMD=0x205	, // invalid command (no support)
  KVS_ERR_DECOMPRESSION=0x206	, // retrieveing uncompressed value with  KVS_RETRIEVE_DECOMPRESSION option
  KVS_ERR_HEAP_ALLOC_FAILURE=0x207	, // heap allocation fail for sdk operations
  KVS_ERR_ITERATE_HANDLE_ALREADY_OPENED=0x208	, // fail to open iterator with given prefix/bitmask as it is already opened
  KVS_ERR_ITERATE_REQUEST_FAIL=0x209	, // fail to process the iterate request due to FW internal status
  KVS_ERR_MAXIMUM_VALUE_SIZE_LIMIT_EXCEEDED=0x20A	, // value of given key is already full(KVS_MAX_TOTAL_VALUE_LEN)
  KVS_ERR_MISALIGNED_KEY_SIZE=0x20B	, // misaligned key length(size)
  KVS_ERR_MISALIGNED_VALUE_OFFSET=0x20C	, // misaligned value offset
  KVS_ERR_SDK_CLOSE=0x20D	, // device close failed
  KVS_ERR_SDK_INVALID_PARAM=0x20E	, // invalid parameters for sdk operations
  KVS_ERR_SDK_OPEN=0x20F	, // device open failed
  KVS_ERR_SLAB_ALLOC_FAILURE=0x210	, // slab allocation fail for sdk operations
  KVS_ERR_UNRECOVERED_ERROR=0x211	, // internal I/O error
  
  // from emulator and Kernel driver	
  KVS_ERR_NS_ATTACHED=0x300	, // namespace is already attached
  KVS_ERR_NS_CAPACITY=0x301	, // namespace does not have enough space
  KVS_ERR_NS_DEFAULT=0x302	, // default namespace can not be modified, deleted, attached or detached
  KVS_ERR_NS_INVALID=0x303	, // namespace does not exist
  KVS_ERR_NS_MAX=0x304	, // maximum number of namespaces were created
  KVS_ERR_NS_NOT_ATTACHED=0x305	, // device cannot detach a namespace since it has not been fully deleted
  
  // Container	
  KVS_ERR_CONT_CAPACITY=0x400	, // conatainer does not have enough space
  KVS_ERR_CONT_CLOSE=0x401	, // container is closed
  KVS_ERR_CONT_EXIST=0x402	, // container is already created with the same name
  KVS_ERR_CONT_GROUP_BY=0x403	, // group_by option is invalid
  KVS_ERR_CONT_INDEX=0x404	, // index is not valid
  KVS_ERR_CONT_NAME=0x405	, // container name is invalid
  KVS_ERR_CONT_NOT_EXIST=0x406	, // container does not existi
  KVS_ERR_CONT_OPEN=0x407	, // container is already opened
} kvs_result;	


#ifdef __cplusplus
} // extern "C"
#endif

#endif
