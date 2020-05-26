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


#ifndef KVS_TYPES_H
#define KVS_TYPES_H

#include <memory.h>
#include <stdio.h>
#include <pthread.h>
#include <stdint.h>

#include "kvs_result.h"
#include "kvs_const.h"
#include "kvs_struct.h"

#ifdef __cplusplus
extern "C" {
#endif

// memory
#define _FUNCTIONIZE(a, b)  a(b)
#define _STRINGIZE(a)      #a
#define _INT2STRING(i)     _FUNCTIONIZE(_STRINGIZE, i)
#define _LOCATION       __FILE__ ":" _INT2STRING(__LINE__)

void *_kvs_zalloc(size_t size_bytes, size_t alignment, const char *file);
void *_kvs_malloc(size_t size_bytes, size_t alignment, const char *file);
void  _kvs_free(void * buf, const char *file);

/*! Allocate aligened memory 
 * 
 * It allocates from hugepages when DPDK is enabled, 
 * otherwise allocate from the memory in the current NUMA node.
 *
 * \return a memory pointer if succeeded, 0 otherwise.
 *\ingroup KV_API
 */
#define kvs_malloc(size, alignment) _kvs_malloc(size, alignment, _LOCATION)
/*! Allocate zeroed aligned memory 
 *
 * It allocates from hugepages when DPDK is enabled, 
 * otherwise allocate from the memory in the current NUMA node.
 *
 * \return a memory pointer if succeeded, 0 otherwise.
 *\ingroup KV_API
 */
#define kvs_zalloc(size, alignment) _kvs_zalloc(size, alignment, _LOCATION)

/*! Free memory 
 *\ingroup KV_API
 */
#define kvs_free(buf) _kvs_free(buf, _LOCATION)
#define kvs_memstat() _kvs_report_memstat()

/*
* \defgroup device_interfaces
*/

/*
* \ingroup device_interfaces
*
  This API opens a KVS device. This API internally checks device availability and initializes it. It returns zero if successful. Otherwise, it returns an error code.

  PARAMETERS
  IN URI Universal Resource Identifier of a device
  OUT dev_hd device handle

  RETURNS
  KVS_SUCCESS to indicate that device open is successful or an error code for error

  ERROR CODE
  KVS_ERR_DEV_NOT_EXIST the device does not exist
  KVS_ERR_SYS_IO communication with device failed
  KVS_ERR_PARAM_INVALID URI is NULL
*/
kvs_result kvs_open_device(char *URI, kvs_device_handle *dev_hd);

/*
* \ingroup device_interfaces
*
  This API retrieves the device information (e.g., kvs_device data structure).

  PARAMETERS
  IN dev_hd device handle
  OUT dev_info kvs_device data structure (device information)

  RETURNS
  KVS_SUCCESS for successful completion or an error code for error

  ERROR CODE
  KVS_ERR_DEV_NOT_EXIST no device exists for the device handle
  KVS_ERR_SYS_IO communication with device failed
*/
kvs_result kvs_get_device_info(kvs_device_handle dev_hd, kvs_device *dev_info);

/*
* \ingroup device_interfaces
*
  This API closes a KVS device. dev_hd is associated with an open device.

  PARAMETERS
  IN dev_hd device handle

  RETURNS
  KVS_SUCCESS for successful completion or an error code for error

  ERROR CODE
  KVS_ERR_DEV_NOT_EXIST no device with the dev_hd exists
  KVS_ERR_SYS_IO communication with device failed
*/
kvs_result kvs_close_device(kvs_device_handle dev_hd);

/*
* \ingroup device_interfaces
*
  This API returns device capacity in bytes referenced by the given device handle.

  PARAMETERS
  IN dev_hd device handle
  OUT dev_capacity device capacity

  RETURNS
  KVS_SUCCESS for successful completion or an error code for error

  ERROR CODE
  KVS_ERR_DEV_NOT_EXIST no device exists for the device handle
  KVS_ERR_SYS_IO communication with device failed
*/
kvs_result kvs_get_device_capacity(kvs_device_handle dev_hd, uint64_t *dev_capacity);

/*
* \ingroup device_interfaces
*
  This API returns the device utilization (i.e, used ratio of the device) by the given device handle. The utilization is from 0(0.00% utilized) to 10000(100%).

  PARAMETERS
  IN dev_hd device handle
  OUT dev_utilization device utilization

  RETURNS
  KVS_SUCCESS for successful completion or an error code for error

  ERROR CODE
  KVS_ERR_DEV_NOT_EXIST no device exists for the device handle
  KVS_ERR_SYS_IO communication with device failed
*/
kvs_result kvs_get_device_utilization(kvs_device_handle dev_hd, uint32_t *dev_utilization);

/*
* \ingroup device_interfaces
*
  This API returns the minimum length of key that the device supports.

  PARAMETERS
  IN dev_hd device handle
  OUT min_key_length minimum key length that the device supports

  RETURNS
  KVS_SUCCESS for successful completion or an error code for error

  ERROR CODE
  KVS_ERR_DEV_NOT_EXIST no device exists for the device handle
  KVS_ERR_SYS_IO communication with device failed
*/
kvs_result kvs_get_min_key_length(kvs_device_handle dev_hd, uint32_t *min_key_length);

/*
* \ingroup device_interfaces
*
  This API returns the maximum length of key that the device supports.

  PARAMETERS
  IN dev_hd device handle
  OUT max_key_length maximum key length that the device support

  RETURNS
  KVS_SUCCESS for successful completion or an error code for error

  ERROR CODE
  KVS_ERR_DEV_NOT_EXIST no device exists for the device handle
  KVS_ERR_SYS_IO communication with device failed
*/
kvs_result kvs_get_max_key_length(kvs_device_handle dev_hd, uint32_t *max_key_length);

/*
* \ingroup device_interfaces
*
  This API returns the minimum length of value that the device supports.

  PARAMETERS
  IN dev_hd device handle
  OUT min_value_length minimum value length that the device supports

  RETURNS
  KVS_SUCCESS for successful completion or an error code for error

  ERROR CODE
  KVS_ERR_DEV_NOT_EXIST no device exists for the device handle
  KVS_ERR_SYS_IO communication with device failed
*/
kvs_result kvs_get_min_value_length(kvs_device_handle dev_hd, uint32_t *min_value_length);

/*
* \ingroup device_interfaces
*
  This API returns the maximum length of value that the device supports.

  PARAMETERS
  IN dev_hd device handle
  OUT max_value_length maximum value length that the device supports

  RETURNS
  KVS_SUCCESS for successful completion or an error code for error

  ERROR CODE
  KVS_ERR_DEV_NOT_EXIST no device exists for the device handle
  KVS_ERR_SYS_IO communication with device failed
*/
kvs_result kvs_get_max_value_length(kvs_device_handle dev_hd, uint32_t *max_value_length);

/*
* \ingroup device_interfaces
*
  This API returns the optimal length of value that the device supports. The device will perform best when the value size is the same as the optimal value size.

  PARAMETERS
  IN dev_hd device handle
  OUT opt_value_length optimal value length that the device supports

  RETURNS
  KVS_SUCCESS for successful completion or an error code for error

  ERROR CODE
  KVS_ERR_DEV_NOT_EXIST no device exists for the device handle
  KVS_ERR_SYS_IO communication with device failed
*/
kvs_result kvs_get_optimal_value_length(kvs_device_handle dev_hd, uint32_t *opt_value_length);

/*
* \ingroup device_interfaces
*
  This API creates a new Key Space in a device. An application needs to specify a unique Key Space name, and its capacity.
  The capacity is defined in bytes. A 0 (numeric zero) capacity means no limitation where device capacity limits actual Key Space capacity.
  The device assigns a unique id while an application assigns a unique name.

  PARAMETERS
  IN dev_hd device handle
  IN key_space_name name of Key Space
  IN size capacity of a Key Space with respect to key value pair size (key size + value size) in bytes
  IN opt Key Space option

  RETURNS
  KVS_SUCCESS if a Key Space is created successfully or an error code for error.

  ERROR CODE
  KVS_ERR_DEV_CAPACITY the Key Space size is too big
  KVS_ERR_KS_EXIST Key Space with the same name already exists
  KVS_ERR_KS_NAME Key Space name does not meet the requirement (e.g., too long (see 5.2.2))
  KVS_ERR_DEV_NOT_EXIST no device with the dev_hd exists
  KVS_ERR_SYS_IO communication with device failed
  KVS_ERR_PARAM_INVALID name or opt is NULL
  KVS_ERR_OPTION_INVALID Key Space option is not supported
*/
kvs_result kvs_create_key_space(kvs_device_handle dev_hd, kvs_key_space_name *key_space_name, uint64_t size, kvs_option_key_space opt);

/*
* \ingroup device_interfaces
*
  This API deletes a Key Space identified by the given Key Space name. It deletes all Key Value Pairs within the Key Space as well as the Key Space itself.
  As a side effect of the delete operation, the Key Space is closed for all applications as the Key Space is no longer present in the device.
  It is recommended that all applications accessing a Key Space close the Key Space prior to deleting the Key Space.

  PARAMETERS
  IN dev_hd device handle
  IN key_space_name Key Space name

  RETURNS
  KVS_SUCCESS if a Key Space is deleted successfully or an error code for error

  ERROR CODE
  KVS_ERR_KS_NOT_EXIST Key Space with a given key_space_name does not exist
  KVS_ERR_DEV_NOT_EXIST no device with the dev_hd exists
  KVS_ERR_SYS_IO communication with device failed
*/
kvs_result kvs_delete_key_space(kvs_device_handle dev_hd, kvs_key_space_name *key_space_name);

/*
* \ingroup device_interfaces
*
  This API returns the names of Key Spaces up to the number that fit in the buffer specified in buffer_size.
  A device may define a unique order of Key Space names and index is defined relative to that order. The value of index may change if a Key Space is created or deleted.
  The index specifies a start list entry offset, buffer_size specifies the size of the kvs_key_space_name array, and names is a buffer to store name information.
  The ks_cnt specifies the number of Key Space names to return.

  PARAMETERS
  IN dev_hd device handle
  IN index start index of Key Space as an input
  IN buffer_size buffer size of Key Space names
  OUT names buffer to store Key Space names. This buffer is required to be
  preallocated before calling this routine.
  OUT ks_cnt the number of names stored in the buffer

  RETURNS
  KVS_SUCCESS if the operation is successful or an error code for error.

  ERROR CODE
  KVS_ERR_KS_NOT_EXIST no Key Space exists
  KVS_ERR_DEV_NOT_EXIST no device with the dev_hd exists
  KVS_ERR_SYS_IO communication with device failed
  KVS_ERR_KS_INDEX index is not valid
  KVS_ERR_PARAM_INVALID names or ks_cnt is NULL
  KVS_ERR_KS_NOT_EXIST no Key Space exists
  KVS_ERR_DEV_NOT_EXIST no device with the dev_hd exists
  KVS_ERR_SYS_IO communication with device failed
  KVS_ERR_KS_INDEX index is not valid
  KVS_ERR_PARAM_INVALID names or ks_cnt is NULL
  KVS_ERR_KS_NOT_EXIST no Key Space exists
  KVS_ERR_DEV_NOT_EXIST no device with the dev_hd exists
  KVS_ERR_SYS_IO communication with device failed
  KVS_ERR_KS_INDEX index is not valid
  KVS_ERR_PARAM_INVALID names or ks_cnt is NULL
*/
kvs_result kvs_list_key_spaces(kvs_device_handle dev_hd, uint32_t index, uint32_t buffer_size, kvs_key_space_name *names, uint32_t *ks_cnt);

/*
* \defgroup key_space_interfaces
*/

/*
* \ingroup key_space_interfaces
*
  This API opens a Key Space with a given name. This API communicates with a device to initialize the corresponding Key Space.
  The device is capable of recognizing and initializing the Key Space. If the Key Space is already open, this API returns KVS_ERR_KS_OPEN.

  PARAMETERS
  IN dev_hd Device handle
  IN name Key Space name
  OUT ks_hd Key Space handle

  RETURNS
  KVS_SUCCESS to indicate that device open is successful or an error code for error

  ERROR CODE
  KVS_ERR_KS_NOT_EXIST Key Space with the given name does not exist,
  KVS_ERR_DEV_NOT_EXIST No device with dev_hd exists
  KVS_ERR_SYS_IO Communication with device failed
  KVS_ERR_KS_OPEN Key Space has been opened already
*/
kvs_result kvs_open_key_space(kvs_device_handle dev_hd, char *name, kvs_key_space_handle *ks_hd);

/*
* \ingroup key_space_interfaces
*
  This API closes a Key Space with a given Key Space handle. This API communicates with the device to close the corresponding Key Space.
  This API may clean up any internal Key Space states in the device. If the given Key Space was not open, this returns a KVS_ERR_KS_NOT_OPEN error.

  PARAMETERS
  IN ks_hd Key Space handle

  RETURNS
  KVS_SUCCESS to indicate that closing a Key Space is successful or an error code for an error

  ERROR CODE
  KVS_ERR_KS_NOT_OPEN Key space is not open
  KVS_ERR_KS_NOT_EXIST Key Space with a given ks_hd does not exist
  KVS_ERR_DEV_NOT_EXIST No device with dev_hd exists
  KVS_ERR_SYS_IO Communication with device failed
*/
kvs_result kvs_close_key_space(kvs_key_space_handle ks_hd);

/*
* \ingroup key_space_interfaces
*
  This API retrieves Key Space information.

  PARAMETERS
  IN ks_hd Key Space handle
  OUT ks Key Space information

  RETURNS
  KVS_SUCCESS to indicate that getting Key Space info is successful or an error code for error.

  ERROR CODE
  KVS_ERR_KS_NOT_EXIST Key Space with a given ks_hd does not exist
  KVS_ERR_SYS_IO Communication with device failed
  KVS_ERR_PARAM_INVALID ks is NULL
*/
kvs_result kvs_get_key_space_info(kvs_key_space_handle ks_hd, kvs_key_space *ks);

/*
* \ingroup key_space_interfaces
*
  This API retrieves key value pair properties. Key value pair properties includes a key length, a key byte stream, and a value length.
  Please refer to section 5.4.22 kvs_kvp_info for details. This API is intended to be used when a buffer length for a value is not known.
  The caller should create kvs_kvp_info object before calling this API.

  PARAMETERS
  IN ks_hd Key Space handle
  IN key Key to find for key value properties
  OUT info Key value pair properties

  RETURNS
  KVS_SUCCESS to indicate that retrieving key value pair properties is successful or an error code for error.

  ERROR CODE
  KVS_ERR_KS_NOT_EXIST Key Space with a given ks_hd does not exist
  KVS_ERR_SYS_IO Communication with device failed
  KVS_ERR_KEY_LENGTH_INVALID given key is not supported (e.g., length)
  KVS_ERR_PARAM_INVALID key or info is NULL
  KVS_ERR_KEY_NOT_EXIST key does not exist
*/
kvs_result kvs_get_kvp_info(kvs_key_space_handle ks_hd, kvs_key *key, kvs_kvp_info *info);

/*
* \ingroup key_space_interfaces
*
  This API retrieves a key value pair value with the given key. The value parameter contains output buffer information for the value.
  As an input, value.value contains the buffer to store the key value pair value and value.length contains the buffer size.
  The key value pair value is copied to value.value buffer and value.length is set to the retrieved value size.
  If the offset of value is not zero, the value of key value pair is copied into the buffer, skipping the first offset bytes of the value of key value pair.
  The offset is required to align to KVS_ALIGNMENT_UNIT. If the offset is not aligned, a KVS_ERR_VALUE_OFFSET_MISALIGNED error is returned and no data is transferred.
  If an allocated value buffer is not big enough to hold the value, the device will set actual_value_size to the size of the value,
  return KVS_ERR_BUFFER_SMALL and data is returned to the buffer up to the size specified in value.length.

  PARAMETERS
  IN ks_hd Key Space handle
  IN key Key of the key value pair to get value
  IN opt retrieval option. It may be NULL. In that case, the default retrieval option is used.
  OUT value value to receive the key value pair's value from device

  RETURNS
  KVS_SUCCESS to indicate that retreive is successful or an error code for error.

  ERROR CODE
  KVS_ERR_VALUE_OFFSET_MISALIGNED kvs_value.offset is not aligned to KVS_ALIGNMENT_UNIT
  KVS_ERR_KS_NOT_EXIST Key Space with a given ks_hd does not exist
  KVS_ERR_SYS_IO Communication with device failed
  KVS_ERR_KEY_LENGTH_INVALID given key is not supported (e.g., length)
  KVS_ERR_BUFFER_SMALL Buffer space of value is not allocated or not enough
  KVS_ERR_PARAM_INVALID key or value is NULL
  KVS_ERR_OFFSET_INVALID kvs_value.offset is invalid
  KVS_ERR_OPTION_INVALID the option is not supported
  KVS_ERR_KEY_NOT_EXIST Key does not exist
*/
kvs_result kvs_retrieve_kvp(kvs_key_space_handle ks_hd, kvs_key *key, kvs_option_retrieve *opt, kvs_value *value);

/*
* \ingroup key_space_interfaces
*
  This API asynchronously retrieves a key value pair value with the given key and returns immediately regardless of whether the pair is actually retrieved from a device or not.
  The final execution results are returned to post process function through kvs_postprocess_context. The value parameter contains output buffer information for the value.
  As an input value.value contains the buffer to store the key value pair value and value.length contains the buffer size.
  The key value pair value is copied to value.value buffer and value.length is set to the retrieved value size. If the offset of value is not zero,
  the value of key value pair is copied into the buffer, skipping the first offset bytes of the value of key value pair.
  That is, value.length is equal to the total size of (actual_value_size ¨C offset). The offset is required to align to KVS_ALIGNMENT_UNIT.
  If the offset is not aligned, a KVS_ERR_VALUE_OFFSET_MISALIGNED error is returned. If an allocated value buffer is not big enough to hold the value,
  it will set value.actual_value_size to the actual value length and return KVS_ERR_BUFFER_SMALL.

  PARAMETERS
  IN ks_hd Key Space handle
  IN key Key of the key value pair to get value
  IN opt retrieval option. It may be NULL. In that case, the default retrieval option is used.
  IN private1 Structure passed that may be returned in the kvs_postprocess_context 
    after the async IO is completed
  IN private2 Structure passed that may be returned in the kvs_postprocess_context 
    after the async IO is completed
  OUT value value to receive the key value pair's value from device
  IN post_fn post process function pointer

  RETURNS
  KVS_SUCCESS to indicate that retrieve is successful or an error code for error.

  ERROR CODE
  KVS_ERR_VALUE_OFFSET_MISALIGNED kvs_value.offset is not aligned to KVS_ALIGNMENT_UNIT
  KVS_ERR_KS_NOT_EXIST Key Space with a given ks_hd does not exist
  KVS_ERR_SYS_IO Communication with device failed
  KVS_ERR_KEY_LENGTH_INVALID given key is not supported (e.g., length)
  KVS_ERR_BUFFER_SMALL Buffer space of value is not allocated or not enough
  KVS_ERR_PARAM_INVALID key or value is NULL
  KVS_ERR_OFFSET_INVALID kvs_value.offset is invalid
  KVS_ERR_OPTION_INVALID the option is not supported
  KVS_ERR_KEY_NOT_EXIST Key does not exist
*/
kvs_result kvs_retrieve_kvp_async(kvs_key_space_handle ks_hd, kvs_key *key, 
  kvs_option_retrieve *opt, void *private1, void *private2, kvs_value *value, kvs_postprocess_function post_fn);

/*
* \ingroup key_space_interfaces
*
  This API writes a Key-value key value pair into a Key Space. This API supports the modes defined in section 5.4.9 as specified in opt.
  Store operations execute based on the existence of the key and the kvs_option_store specified. If the Key Space does not have enough space to store a key value pair,
  a KVS_ERR_KS_CAPACITY error message is returned.

  PARAMETERS
  IN ks_hd Key Space handle
  IN key Key of the key value pair to store into Key Space
  IN value Value of the key value pair to store into Key Space
  IN opt Store option. It may be NULL. In that case, the kvs_store_type of KVS_STORE_POST is used.

  RETURNS
  KVS_SUCCESS to indicate that store is successful or an error code for error.

  ERROR CODE
  KVS_ERR_VALUE_OFFSET_MISALIGNED kvs_value.offset is not aligned to KVS_ALIGNMENT_UNIT
  KVS_ERR_KS_NOT_EXIST Key Space with a given ks_hd does not exist
  KVS_ERR_SYS_IO Communication with device failed
  KVS_ERR_KEY_LENGTH_INVALID given key is not supported (e.g., length)
  KVS_ERR_PARAM_INVALID a key or a value is NULL
  KVS_ERR_OFFSET_INVALID kvs_value.offset is invalid
  KVS_ERR_OPTION_INVALID unsupported option
  KVS_ERR_KS_CAPACITY Key Space does not have enough space to store this key value pair
  KVS_ERR_VALUE_UPDATE_NOT_ALLOWED a key exists but overwrite is not permitted
  KVS_ERR_VALUE_LENGTH_INVALID given value is not supported (e.g., length)
*/
kvs_result kvs_store_kvp(kvs_key_space_handle ks_hd, kvs_key *key, kvs_value *value, kvs_option_store *opt);

/*
* \ingroup key_space_interfaces
*
  This API asynchronously writes a Key-value key value pair into a Key Space and returns immediately regardless of whether the pair is actually written to a device or not.
  The final execution results are returned to post process function through kvs_postprocess_context.
  Store operations execute based on the existence of the key and the kvs_option_store specified. If the Key Space does not have enough space to store a key value pair,
  a KVS_ERR_KS_CAPACITY error message is returned.

  PARAMETERS
  IN ks_hd Key Space handle
  IN key Key of the key value pair to store into Key Space
  IN value Value of the key value pair to store into Key Space
  IN opt Store option. It may be NULL. In that case, the kvs_store_type of KVS_STORE_POST is used.
  IN private1 Structure passed that may be returned in the kvs_postprocess_context 
    after the async IO is completed
  IN private2 Structure passed that may be returned in the kvs_postprocess_context 
    after the async IO is completed
  IN post_fn post process function pointer

  RETURNS
  KVS_SUCCESS to indicate that store is successful or an error code for error.

  ERROR CODE
  KVS_ERR_VALUE_OFFSET_MISALIGNED kvs_value.offset is not aligned to KVS_ALIGNMENT_UNIT
  KVS_ERR_KS_NOT_EXIST Key Space with a given ks_hd does not exist
  KVS_ERR_SYS_IO Communication with device failed
  KVS_ERR_KEY_LENGTH_INVALID given key is not supported (e.g., length)
  KVS_ERR_PARAM_INVALID a key or a value is NULL
  KVS_ERR_OFFSET_INVALID kvs_value.offset is invalid
  KVS_ERR_OPTION_INVALID unsupported option
  KVS_ERR_KS_CAPACITY Key Space or device does not have enough space to store this key value pair
  KVS_ERR_VALUE_UPDATE_NOT_ALLOWED a key exists but overwrite is not permitted
  KVS_ERR_VALUE_LENGTH_INVALID given value is not supported (e.g., length)
*/
kvs_result kvs_store_kvp_async(kvs_key_space_handle ks_hd, kvs_key *key, kvs_value *value, 
  kvs_option_store *opt, void *private1, void *private2, kvs_postprocess_function post_fn);

/*
* \ingroup key_space_interfaces
*
  This API deletes key value pair(s) with a given key.

  PARAMETERS
  IN ks_hd Key Space handle
  IN key Key of the key value pair(s) to delete
  IN opt delete option

  RETURNS
  KVS_SUCCESS Indicate that delete is successful or an error code for error.

  ERROR CODE
  KVS_ERR_KS_NOT_EXIST Key Space with a given ks_hd does not exist
  KVS_ERR_PARAM_INVALID key is NULL.
  KVS_ERR_SYS_IO Communication with device failed
  KVS_ERR_KEY_LENGTH_INVALID given key is not supported (e.g., length)
  KVS_ERR_KEY_NOT_EXIST key does not exist
*/
kvs_result kvs_delete_kvp(kvs_key_space_handle ks_hd, kvs_key* key, kvs_option_delete *opt);

/*
* \ingroup key_space_interfaces
*
  This API asynchronously deletes key value pair(s) with a given key and returns immediately regardless of whether the pair is actually deleted from a device or not.
  The final execution results are returned to post process function through kvs_postprocess_context.

  PARAMETERS
  IN ks_hd Key Space handle
  IN key Key of the key value pair(s) to delete
  IN opt delete option
  IN private1 Structure passed that may be returned in the kvs_postprocess_context 
    after the async IO is completed
  IN private2 Structure passed that may be returned in the kvs_postprocess_context 
    after the async IO is completed
  IN post_fn post process function pointer

  RETURNS
  KVS_SUCCESS Indicate that delete is successful or an error code for error.

  ERROR CODE
  KVS_ERR_KS_NOT_EXIST Key Space with a given ks_hd does not exist
  KVS_ERR_PARAM_INVALID key is NULL.
  KVS_ERR_SYS_IO Communication with device failed
  KVS_ERR_KEY_LENGTH_INVALID given key is not supported (e.g., length)
  KVS_ERR_KEY_NOT_EXIST key does not exist
*/
kvs_result kvs_delete_kvp_async(kvs_key_space_handle ks_hd, kvs_key* key, 
  kvs_option_delete *opt, void *private1, void *private2, kvs_postprocess_function post_fn);

/*
* \ingroup key_space_interfaces
*
  This API deletes the key-value pairs in a Key Space that matches with grp_fltr.

  PARAMETERS
  IN ks_hd Key Space handle
  IN grp_fltr Key group filter to delete

  RETURNS
  KV_SUCCESS to indicate that delete key group is successful or an error code for error.

  ERROR CODE
  KVS_ERR_KS_NOT_EXIST Key Space with a given ks_hd does not exist
  KVS_ERR_PARAM_INVALID grp_fltr is NULL.
  KVS_ERR_SYS_IO Communication with device failed
*/
kvs_result kvs_delete_key_group(kvs_key_space_handle ks_hd, kvs_key_group_filter *grp_fltr);

/*
* \ingroup key_space_interfaces
*
  This API deletes the key-value pairs in a Key Space that matches with grp_fltr and returns immediately regardless of whether a key group is actually deleted from a device or not.
  The final execution results are returned to post process function through kvs_postprocess_context.

  PARAMETERS
  IN ks_hd Key Space handle
  IN grp_fltr key group filter to delete
  IN private1 Structure passed that may be returned in the kvs_postprocess_context 
    after the async IO is completed
  IN private2 Structure passed that may be returned in the kvs_postprocess_context 
    after the async IO is completed
  IN post_fn post process function pointer

  RETURNS
  KV_SUCCESS to indicate that delete key group is successful or an error code for error.

  ERROR CODE
  KVS_ERR_KS_NOT_EXIST Key Space with a given ks_hd does not exist
  KVS_ERR_PARAM_INVALID grp_fltr is NULL.
  KVS_ERR_SYS_IO Communication with device failed
*/
kvs_result kvs_delete_key_group_async(kvs_key_space_handle ks_hd, 
  kvs_key_group_filter *grp_fltr, void *private1, void *private2, kvs_postprocess_function post_fn);

/*
* \ingroup key_space_interfaces
*
  This API checks if a set of one or more keys exists and returns a bool type status.
  The existence of a key value pair is determined during an implementation-dependent time window while this API executes. Therefore,
  repeated routine calls may return different outputs in multi-threaded environments. One bit is used for each key.
  Therefore when 32 keys are intended to be checked, a caller should allocate 32 bits (i.e., 4 bytes) of memory buffer and the existence information is filled.
  The LSB (Least Significant Bit) of the list->result_buffer indicates if the first key exist or not.

  PARAMETERS
  IN ks_hd Key Space handle
  IN key_cnt the number of keys to check
  IN keys a set of keys to check
  IN buffer_size list buffer size in bytes
  OUT list a kvs_exist_list indicates whether corresponding key(s) exists or not

  RETURNS
  KVS_SUCCESS to indicate success or an error code for error.

  ERROR CODE
  KVS_ERR_KS_NOT_EXIST Key Space with a given ks_hd does not exist
  KVS_ERR_BUFFER_SMALL the buffer space of list->result_buffer is not big enough
  KVS_ERR_PARAM_INVALID keys or list parameter is NULL
  KVS_ERR_SYS_IO Communication with device failed
*/
kvs_result kvs_exist_kv_pairs(kvs_key_space_handle ks_hd, uint32_t key_cnt, kvs_key *keys, kvs_exist_list *list);

/*
* \ingroup key_space_interfaces
*
  This API asynchronously checks if a set of keys exists and returns a bool type status. It returns immediately regardless of whether keys are checked from a device or not.
  The final execution results are returned to the post process function through kvs_postprocess_context.
  The existence of a key value pair is determined during an implementation-dependent time window while this API executes.
  Therefore, repeated routine calls is able to return different outputs in multi-threaded environments. One bit is used for each key.
  Therefore when 32 keys are intended to be checked, a caller shall allocate 32 bits (i.e., 4 bytes) of memory buffer and the existence information is filled.
  The LSB (Least Significant Bit) of the list->result_buffer indicates if the first key exist or not.

  PARAMETERS
  IN ks_hd Key Space handle
  IN key_cnt the number of keys
  IN keys a set of keys to check
  IN buffer_size list buffer size in bytes
  OUT list a list indicates whether a corresponding key exists or not
  IN private1 Structure passed that may be returned in the kvs_postprocess_context 
    after the async IO is completed
  IN private2 Structure passed that may be returned in the kvs_postprocess_context 
    after the async IO is completed
  IN post_fn post process function pointer

  RETURNS
  KVS_SUCCESS to indicate success or an error code for error.

  ERROR CODE
  KVS_ERR_KS_NOT_EXIST Key Space with a given ks_hd does not exist
  KVS_ERR_BUFFER_SMALL the buffer space of list->result_buffer is not big enough
  KVS_ERR_PARAM_INVALID keys or list parameter is NULL
  KVS_ERR_SYS_IO Communication with device failed
*/
kvs_result kvs_exist_kv_pairs_async(kvs_key_space_handle ks_hd, uint32_t key_cnt, 
  kvs_key *keys, kvs_exist_list *list, void *private1, void *private2, kvs_postprocess_function post_fn);


/*
* \defgroup iterator_interfaces
*/

/*
* \ingroup iterator_interfaces
*
  This API enables applications to set up a Key Group such that the keys in that Key Group may be iterated within a Key Space
  (i.e., kvs_crearte_iterator() enables a device to prepare a Key Group of keys for iteration by matching a given bit pattern (it_fltr.bit_pattern) to all keys in the Key Space
  considering bits indicated by it_fltr.bitmask and the device sets up a Key Group of keys matching that ¡°(bitmask & key) == bit_pattern¡±.)

  PARAMETERS
  IN ks_hd Key Space handle
  IN iter_op iterator option
  IN iter_fltr iterator filter that includes bitmask and bit pattern
  OUT iter_hd iterator handle

  RETURNS
  KVS_SUCCESS to indicate success or an error code for error.

  ERROR CODE
  KVS_ERR_KS_NOT_EXIST Key Space with a given ks_hd does not exist
  KVS_ERR_PARAM_INVALID it_fltr is NULL.
  KVS_ERR_SYS_IO Communication with device failed
  KVS_ERR_ITERATOR_MAX the maximum number of iterators that a device supports is already open. No more iterator are able to be opened.
  KVS_ERR_ITERATOR_OPEN iterator is already opened
  KVS_ERR_OPTION_INVALID the device does not support the specified iterator options
  KVS_ERR_ITERATOR_FILTER_INVALID iterator filter(match bitmask and pattern) is not valid
*/
kvs_result kvs_create_iterator(kvs_key_space_handle ks_hd, kvs_option_iterator *iter_op, kvs_key_group_filter *iter_fltr, kvs_iterator_handle *iter_hd);

/*
* \ingroup iterator_interfaces
*
  This API releases the resources for the iterator Key Group specified by iter_hd in the specified Key Space.

  PARAMETERS
  IN ks_hd Key Space handle
  IN iter_hd iterator handle

  RETURNS
  KVS_SUCCESS to indicate success or an error code for error.

  ERROR CODE
  KVS_ERR_KS_NOT_EXIST Key Space with a given ks_hd does not exist
  KVS_ERR_SYS_IO Communication with device failed
  KVS_ERR_ITERATOR_NOT_EXIST the iterator Key Group does not exist
*/
kvs_result kvs_delete_iterator(kvs_key_space_handle ks_hd, kvs_iterator_handle iter_hd);

/*
* \ingroup iterator_interfaces
*
  This API obtains a subset of key or key-value pairs from an Key Group of iter_hd within a Key Space
  (i.e., kvs_iterator_next() retrieves the next Key Group of keys or key-value pairs in the iterator Key Group (iter_hd) that is created with kvs_create_iterator() command).
  buffer_size is the iterator buffer (iter_list) size in bytes. The retrieved values (iter_list) are either keys or key-value pairs based on the iterator option
  which is specified by kvs_create_iterator().
  Output values (iter_list.it_list) are determined by the iterator option specified by an application.
  KV_ITERATOR_OPT_KEY [MANDATORY]: a subset of keys are returned in iter_list.it_list data structure
  KV_ITERATOR_OPT_KEY_VALUE; a subset of key-value pairs are returned in iter_list.it_list data structure

  PARAMETERS
  IN ks_hd Key Space handle
  IN iter_hd iterator handle
  IN buffer_size iterator buffer (iter_list) size in bytes
  OUT iter_list output buffer for a set of keys or key-value pairs

  RETURNS
  KVS_SUCCESS to indicate success or an error code for error.

  ERROR CODE
  KVS_ERR_KS_NOT_EXIST Key Space with a given ks_hd does not exist
  KVS_ERR_PARAM_INVALID iter_list parameter is NULL
  KVS_ERR_SYS_IO Communication with device failed
  KVS_ERR_ITERATOR_NOT_EXIST the iterator Key Group does not exist
*/
kvs_result kvs_iterate_next(kvs_key_space_handle ks_hd, kvs_iterator_handle iter_hd, kvs_iterator_list *iter_list);

/*
* \ingroup iterator_interfaces
*
  This API obtains a subset of key or key-value pairs from an iterator Key Group of iter_hd within a Key Space
  (i.e., kvs_iterator_next() retrieves a next Key Group of keys or key-value pairs in the iterator key group (iter_hd) that is set with kvs_create_iterator() command).
  Output values (iter_list.it_list) are determined by the iterator option set by an application.
  KV_ITERATOR_OPT_KEY [MANDATORY]: a subset of keys are returned in iter_list.it_list data structure
  KV_ITERATOR_OPT_KEY_VALUE; a subset of key-value pairs are returned in iter_list.it_list data structure

  PARAMETERS
  IN ks_hd Key Space handle
  IN iter_hd iterator handle
  IN buffer_size iterator buffer (iter_list) size in bytes
  OUT iter_list output buffer for a set of keys or key-value pairs
  IN private1 Structure passed that may be returned in the kvs_postprocess_context 
    after the async IO is completed
  IN private2 Structure passed that may be returned in the kvs_postprocess_context 
    after the async IO is completed
  IN post_fn post process function pointer

  RETURNS
  KVS_SUCCESS to indicate success or an error code for error.

  ERROR CODE
  KVS_ERR_KS_NOT_EXIST Key Space with a given ks_hd does not exist
  KVS_ERR_PARAM_INVALID iter_list parameter is NULL
  KVS_ERR_SYS_IO Communication with device failed
  KVS_ERR_ITERATOR_NOT_EXIST the iterator Key Group does not exist
*/
kvs_result kvs_iterate_next_async(kvs_key_space_handle ks_hd, kvs_iterator_handle iter_hd , 
  kvs_iterator_list *iter_list, void *private1, void *private2, kvs_postprocess_function post_fn);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
