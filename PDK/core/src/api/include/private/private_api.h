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


#ifndef PRIVATE_API_H
#define PRIVATE_API_H

#include <memory.h>
#include <stdio.h>
#include <pthread.h>
#include <stdint.h>

#include "private_result.h"
#include "kvs_const.h"
#include "private_struct.h"

namespace api_private {
kvs_result kvs_init_env(kvs_init_options* options);
kvs_result kvs_init_env_opts(kvs_init_options* options);
kvs_result kvs_exit_env();
kvs_result kvs_open_device(const char *dev_path, kvs_device_handle *dev_hd);
kvs_result kvs_close_device(kvs_device_handle user_dev);
kvs_result kvs_create_container(kvs_device_handle dev_hd, const char *name, uint64_t size, const kvs_container_context *ctx);
kvs_result kvs_delete_container(kvs_device_handle dev_hd, const char *cont_name);
kvs_result kvs_open_container(kvs_device_handle dev_hd, const char* name, kvs_container_handle *cont_hd);
kvs_result kvs_close_container(kvs_container_handle cont_hd);
kvs_result kvs_list_containers(kvs_device_handle dev_hd, uint32_t index,
  uint32_t buffer_size, kvs_container_name *names, uint32_t *cont_cnt);
kvs_result kvs_get_container_info (kvs_container_handle cont_hd, kvs_container *cont);
int32_t kvs_get_ioevents(kvs_container_handle cont_hd, int maxevents);
kvs_result kvs_get_tuple_info (kvs_container_handle cont_hd, const kvs_key *key, kvs_tuple_info *info);
kvs_result kvs_store_tuple(kvs_container_handle cont_hd, const kvs_key *key, const kvs_value *value, const kvs_store_context *ctx);
kvs_result kvs_store_tuple_async (kvs_container_handle cont_hd, const kvs_key *key, const kvs_value *value, const kvs_store_context *ctx, kvs_callback_function cbfn);
kvs_result kvs_retrieve_tuple(kvs_container_handle cont_hd, const kvs_key *key, kvs_value *value, const kvs_retrieve_context *ctx);
kvs_result kvs_retrieve_tuple_async(kvs_container_handle cont_hd, const kvs_key *key, kvs_value *value, const kvs_retrieve_context *ctx, kvs_callback_function cbfn);
kvs_result kvs_delete_tuple(kvs_container_handle cont_hd, const kvs_key *key, const kvs_delete_context *ctx);
kvs_result kvs_delete_tuple_async(kvs_container_handle cont_hd, const kvs_key* key, const kvs_delete_context* ctx, kvs_callback_function cbfn);
kvs_result kvs_exist_tuples(kvs_container_handle cont_hd, uint32_t key_cnt, const kvs_key *keys, uint32_t buffer_size, uint8_t *result_buffer, const kvs_exist_context *ctx);
kvs_result kvs_exist_tuples_async(kvs_container_handle cont_hd, uint32_t key_cnt, const kvs_key *keys, uint32_t buffer_size, 
                                  uint8_t *result_buffer, const kvs_exist_context *ctx, kvs_callback_function cbfn);
kvs_result kvs_open_iterator(kvs_container_handle cont_hd, const kvs_iterator_context *ctx, kvs_iterator_handle *iter_hd);
kvs_result kvs_close_iterator(kvs_container_handle cont_hd, kvs_iterator_handle hiter, const kvs_iterator_context *ctx);
kvs_result kvs_close_iterator_all(kvs_container_handle cont_hd);
kvs_result kvs_list_iterators(kvs_container_handle cont_hd, kvs_iterator_info *kvs_iters, int count);
kvs_result kvs_iterator_next(kvs_container_handle cont_hd, kvs_iterator_handle hiter, kvs_iterator_list *iter_list, const kvs_iterator_context *ctx);
kvs_result kvs_iterator_next_async(kvs_container_handle cont_hd, kvs_iterator_handle iter_hd, kvs_iterator_list *iter_list, const kvs_iterator_context *ctx, kvs_callback_function cbfn);
kvs_result kvs_get_device_waf(kvs_device_handle dev_hd, float *waf);
kvs_result kvs_get_device_info(kvs_device_handle dev_hd, kvs_device *dev_info);
kvs_result kvs_get_device_utilization(kvs_device_handle dev_hd, int32_t *dev_util);
kvs_result kvs_get_device_capacity(kvs_device_handle dev_hd, int64_t *dev_capa);
kvs_result kvs_get_min_key_length (kvs_device_handle dev_hd, int32_t *min_key_length);
kvs_result kvs_get_max_key_length (kvs_device_handle dev_hd, int32_t *max_key_length);
kvs_result kvs_get_min_value_length (kvs_device_handle dev_hd, int32_t *min_value_length);
kvs_result kvs_get_max_value_length (kvs_device_handle dev_hd, int32_t *max_value_length);
kvs_result kvs_get_optimal_value_length (kvs_device_handle dev_hd, int32_t *opt_value_length);


const char *kvs_errstr(int32_t errorno);
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
}// namespace api_private
#endif
