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


/*! Initialize the library
 * 
 *  This function must be called once to initialize the environment for the library.
 *  \ref kvs_init_options includes all the available options.
 *  \ingroup KV_API
 */
kvs_result kvs_init_env(kvs_init_options* options);

/*! Set default options to \ref kvs_init_options 
 * 
 * \ingroup KV_API
 */
kvs_result kvs_init_env_opts(kvs_init_options* options);

/*! Deinitialize the library
 * 
 * It closes all opened devices and releases any the system resources assigned by the library.
 * 
 * \ingroup KV_API
 */
kvs_result kvs_exit_env();

/*! List KV devices 
 * 
 * It returns the list of KV devices in the system
 * 
 * \see kv_device_info for each information retrieved by this function
 * \ingroup KV_API
 */
//int32_t kvs_list_kvdevices(kv_device_info **devs, int size);

/*! Open a KV device 
 * 
 *  The device path can be either a kernel device path or a SPDK device path that 
 *  can be retrieved from \ref kv_device_info. The corresponding device driver will 
 *  be loaded internally. 
 * 
 *  TO open a KV emulator, please use the pseudo device path of "/dev/kvemul"
 *
 * \return \ref kv_device_handle : a device handle
 * 
 *\ingroup KV_API
 */
kvs_result kvs_open_device(const char *dev_path, kvs_device_handle *dev_hd);

/*! Close a KV device 
 * 
 *  \ingroup KV_API
 */
kvs_result kvs_close_device(kvs_device_handle user_dev);


/*! This API creates a new contrainer in a device. 
 * User needs to specify a unique container name as a null terminated string, and its capacity. 
 * The capacity is defined in 4KB units. 
 * A 0 (numeric zero) capacity of means no limitation where 
 * device capacity limits actual container capacity. 
 * The device assigns a unique id while a user assigns a unique name. 
 *
 * \param dev_hd kvs_device_handle data structure that includes unique device id 
 * \param name name of container
 * \param sz_4kb capacity of a container with respect to tuple size (key size + value size) in 4KB units 
 * \param ctx: group information to create container groups
 */

  /* Container related features are not supported yet 
   * A dummy container will be created after the call
   */
kvs_result kvs_create_container (kvs_device_handle dev_hd, const char *name, uint64_t size, const kvs_container_context *ctx);
  
kvs_result kvs_delete_container (kvs_device_handle dev_hd, const char *cont_name);

kvs_result kvs_open_container (kvs_device_handle dev_hd, const char* name, kvs_container_handle *cont_hd);
  
kvs_result kvs_close_container (kvs_container_handle cont_hd);
    
kvs_result kvs_get_container_info (kvs_container_handle cont_hd, kvs_container *cont);


/*! Check and Process completed asynchronous I/Os
 * 
 *  It checks if there is any completed asynchronous I/Os and
 *  calls the user's callback function specified in \ref kvs_init_options.
 * 
 *  It should be called within the same thread that issued the I/O. 
 *
 *  \param cont_hd container handle  
 *  \param maxevents the maximum number of I/O events to process
 *  \return the number of events processed
 *  \ingroup KV_API
 */
int32_t kvs_get_ioevents(kvs_container_handle cont_hd, int maxevents);

kvs_result kvs_get_tuple_info (kvs_container_handle cont_hd, const kvs_key *key, kvs_tuple_info *info);

  
/*! Store a KV pair 
 *
 * It stores a KV pair to a device. Depending on the type of KV driver, different memory contraints can be applied.
 * 
 * e.g. when using SPDK drivers, the buffers in \ref kvs_key and \ref kvs_value need to be allocated using 
 * \ref kvs_malloc or \ref kvs_zalloc.
 * 
 * Currently no option flags specified in \ref kvs_io_options are supported. \ref KVS_SYNC_IO flag is supported to 
 * enable synchronous I/O while asynchrnous I/O is being used. 
 * 
 * \param cont_hd container handle
 * \param key key to retrieve
 * \param value value to store
 * \param ctx options 
 *\ingroup KV_API
 */
kvs_result kvs_store_tuple(kvs_container_handle cont_hd, const kvs_key *key, const kvs_value *value, const kvs_store_context *ctx);

kvs_result kvs_store_tuple_async (kvs_container_handle cont_hd, const kvs_key *key, const kvs_value *value, const kvs_store_context *ctx, kvs_callback_function cbfn);

  
/*! Retrieve a KV pair 
 *
 * Finds and returns the key-value pair. The value is an input/output parameter. It needs to provide an address of 
 * the value buffer and its size. The retreived data will be copied to the buffer. If the buffer size is not enough 
 * to store the results, it will return \ref KVS_ERR_VALUE.
 * 
 * Some memory constraints can be applied as described in \ref kvs_store_tuple. 
 * 
 * No retrieve options are supported yet except KVS_SYNC_IO. 
 * 
 * \param cont_hd container handle
 * \param key key to retrieve
 * \param value a value buffer where the output will be stored  [in/out]
 * \param ctx options 
 * \ingroup KV_API
 */
kvs_result kvs_retrieve_tuple(kvs_container_handle cont_hd, const kvs_key *key, kvs_value *value, const kvs_retrieve_context *ctx);

kvs_result kvs_retrieve_tuple_async(kvs_container_handle cont_hd, const kvs_key *key, kvs_value *value, const kvs_retrieve_context *ctx, kvs_callback_function cbfn);

  
/*! Delete a KV pair 
 * 
 * Deletes a key-value pair.
 * 
 * No delete options are supported yet except KVS_SYNC_IO. 
 * 
 * \param cont_hd container handle
 * \param key key to delete
 * \param ctx options 
 * \ingroup KV_API
 */
kvs_result kvs_delete_tuple(kvs_container_handle cont_hd, const kvs_key *key, const kvs_delete_context *ctx);

kvs_result kvs_delete_tuple_async(kvs_container_handle cont_hd, const kvs_key* key, const kvs_delete_context* ctx, kvs_callback_function cbfn);


kvs_result kvs_exist_tuples(kvs_container_handle cont_hd, uint32_t key_cnt, const kvs_key *keys, uint32_t buffer_size, uint8_t *result_buffer, const kvs_exist_context *ctx);
  
kvs_result kvs_exist_tuples_async(kvs_container_handle cont_hd, uint32_t key_cnt, const kvs_key *keys, uint32_t buffer_size, uint8_t *result_buffer, const kvs_exist_context *ctx, kvs_callback_function cbfn);
  
  
/*! Open an iterator
 *
 * \param cont_hd container handle
 * \param ctx options
 * \param iter_hd : a pointer to iterator handler
 * \ingroup KV_API
 */
kvs_result kvs_open_iterator(kvs_container_handle cont_hd, const kvs_iterator_context *ctx, kvs_iterator_handle *iter_hd);
  
/*! close an iterator
 * 
 * \param cont_hd container handle
 * \param hiter the iterator handler
 * \param ctx options
 * \ingroup KV_API
 */
kvs_result kvs_close_iterator(kvs_container_handle cont_hd, kvs_iterator_handle hiter, const kvs_iterator_context *ctx);

/*! close all opened iterators
 *
 * \param cont_hd container handle
 * \ingroup KV_API
 */
kvs_result kvs_close_iterator_all(kvs_container_handle cont_hd);

/*! retrieves a list of iterators in this device
 * \param cont_hd container handle
 * \param kvs_iters an array of kvs_iterator_info
 * \param count the number of iterators to retrieve
 * \ingroup KV_API
 */
kvs_result kvs_list_iterators(kvs_container_handle cont_hd, kvs_iterator_info *kvs_iters, int count);

  
/*! iterator next
 *
 * retrieve a next group of keys or key-value pairs in the iterator group 
 *
 * \param cont_hd container handle
 * \param hiter the iterator handler
 * \param iter_list output buffer for a set of keys or key-value pairs
 * \param ctx options
 * \ingroup KV_API
 */
kvs_result kvs_iterator_next(kvs_container_handle cont_hd, kvs_iterator_handle hiter, kvs_iterator_list *iter_list, const kvs_iterator_context *ctx);

kvs_result kvs_iterator_next_async(kvs_container_handle cont_hd, kvs_iterator_handle iter_hd, kvs_iterator_list *iter_list, const kvs_iterator_context *ctx, kvs_callback_function cbfn);
  
/*! Get WAF (Write Amplificaton Factor) in a KV NMVe Device 
 * \param dev device handle
 * \return \ref float : WAF 
 * \ingroup KV_API 
 */
//float kvs_get_waf(struct kv_device_api *dev);

/* ! Get device info
 * \param: dev_hd device handle
 * return: kvs_device: device info
 */
kvs_result kvs_get_device_info(kvs_device_handle dev_hd, kvs_device *dev_info);
  
/*! Get device used size in percentage in a KV NMVe Device
 * \param dev_hd device handle
 * \return int32_t : device space utilization in an integer form of 0(0.00%) to 10,000(100.00%).
 * \ingroup KV_API
 */
kvs_result kvs_get_device_utilization(kvs_device_handle dev_hd, int32_t *dev_util);

/*! Get device total size in a KV NMVe Device
 * \param dev_hd device handle
 * \return int64_t : total device size in bytes
 */
kvs_result kvs_get_device_capacity(kvs_device_handle dev_hd, int64_t *dev_capa);

kvs_result kvs_get_min_key_length (kvs_device_handle dev_hd, int32_t *min_key_length);

kvs_result kvs_get_max_key_length (kvs_device_handle dev_hd, int32_t *max_key_length);

kvs_result kvs_get_min_value_length (kvs_device_handle dev_hd, int32_t *min_value_length);

kvs_result kvs_get_max_value_length (kvs_device_handle dev_hd, int32_t *max_value_length);

kvs_result kvs_get_optimal_value_length (kvs_device_handle dev_hd, int32_t *opt_value_length);

  
/*! Returns an error string
 *
 *  It interpretes the return value of the functions listed here.
 *
 *\ingroup KV_API
 */
const char *kvs_errstr(int32_t errorno);
  
// memory
#define _FUNCTIONIZE(a,b)  a(b)
#define _STRINGIZE(a)      #a
#define _INT2STRING(i)     _FUNCTIONIZE(_STRINGIZE,i)
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

#ifdef __cplusplus
} // extern "C"
#endif

#endif
