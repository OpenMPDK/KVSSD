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

#include <malloc.h>
#include "kvs_api.h"
#include "private_api.h"

void *_kvs_zalloc(size_t size_bytes, size_t alignment, const char *file) {
  return api_private::_kvs_zalloc(size_bytes, alignment, file);
}

void *_kvs_malloc(size_t size_bytes, size_t alignment, const char *file) {
  return api_private::_kvs_malloc(size_bytes, alignment, file);
}

void  _kvs_free(void * buf, const char *file) {
  api_private::_kvs_free(buf, file);
}

#ifdef SAMSUNG_API

kvs_result kvs_init_env(kvs_init_options* options) {
  api_private::kvs_init_options* pri_options = (api_private::kvs_init_options*)options;
  return (kvs_result)api_private::kvs_init_env(pri_options);
}

kvs_result kvs_init_env_opts(kvs_init_options* options) {
  return (kvs_result)api_private::kvs_init_env_opts((api_private::kvs_init_options*)options);
}

kvs_result kvs_exit_env() {
  return (kvs_result)api_private::kvs_exit_env();
}

kvs_result kvs_open_device(const char *dev_path, kvs_device_handle *dev_hd) {
  return (kvs_result)api_private::kvs_open_device(dev_path, (api_private::kvs_device_handle*)dev_hd);
}

kvs_result kvs_close_device(kvs_device_handle user_dev) {
  return (kvs_result)api_private::kvs_close_device((api_private::kvs_device_handle)user_dev);
}

kvs_result kvs_create_container (kvs_device_handle dev_hd, const char *name, uint64_t size, const kvs_container_context *ctx) {
  return (kvs_result)api_private::kvs_create_container((api_private::kvs_device_handle)dev_hd, name, size, (const api_private::kvs_container_context*)ctx);
}

kvs_result kvs_delete_container (kvs_device_handle dev_hd, const char *cont_name) {
  return (kvs_result)api_private::kvs_delete_container((api_private::kvs_device_handle)dev_hd, cont_name);
}

kvs_result kvs_open_container (kvs_device_handle dev_hd, const char* name, kvs_container_handle *cont_hd) {
  return (kvs_result)api_private::kvs_open_container( (api_private::kvs_device_handle)dev_hd, name, (api_private::kvs_container_handle*)cont_hd);
}

kvs_result kvs_close_container (kvs_container_handle cont_hd) {
  return (kvs_result)api_private::kvs_close_container( (api_private::kvs_container_handle)cont_hd);
}

kvs_result kvs_get_container_info (kvs_container_handle cont_hd, kvs_container *cont) {
  return (kvs_result)api_private::kvs_get_container_info( (api_private::kvs_container_handle)cont_hd, (api_private::kvs_container*)cont);
}

int32_t kvs_get_ioevents(kvs_container_handle cont_hd, int maxevents) {
  return api_private::kvs_get_ioevents( (api_private::kvs_container_handle)cont_hd, maxevents);
}

kvs_result kvs_get_tuple_info (kvs_container_handle cont_hd, const kvs_key *key, kvs_tuple_info *info) {
  return (kvs_result)api_private::kvs_get_tuple_info( (api_private::kvs_container_handle)cont_hd, (const api_private::kvs_key*)key, (api_private::kvs_tuple_info*)info);
}

kvs_result kvs_store_tuple(kvs_container_handle cont_hd, const kvs_key *key, const kvs_value *value, const kvs_store_context *ctx) {
  return (kvs_result)api_private::kvs_store_tuple( (api_private::kvs_container_handle)cont_hd, (const api_private::kvs_key*)key, (const api_private::kvs_value*)value, (const api_private::kvs_store_context*)ctx);
}

kvs_result kvs_store_tuple_async (kvs_container_handle cont_hd, const kvs_key *key, const kvs_value *value, const kvs_store_context *ctx, kvs_callback_function cbfn) {
  return (kvs_result)api_private::kvs_store_tuple_async( (api_private::kvs_container_handle)cont_hd, (const api_private::kvs_key*)key, (const api_private::kvs_value*)value, \
      (const api_private::kvs_store_context*)ctx, (api_private::kvs_callback_function)cbfn);
}

kvs_result kvs_retrieve_tuple(kvs_container_handle cont_hd, const kvs_key *key, kvs_value *value, const kvs_retrieve_context *ctx) {
  return (kvs_result)api_private::kvs_retrieve_tuple( (api_private::kvs_container_handle)cont_hd, (const api_private::kvs_key*)key, (api_private::kvs_value*)value, (const api_private::kvs_retrieve_context*)ctx);
}

kvs_result kvs_retrieve_tuple_async(kvs_container_handle cont_hd, const kvs_key *key, kvs_value *value, const kvs_retrieve_context *ctx, kvs_callback_function cbfn) {
  return (kvs_result)api_private::kvs_retrieve_tuple_async( (api_private::kvs_container_handle)cont_hd, (const api_private::kvs_key*)key, (api_private::kvs_value*)value, \
      (const api_private::kvs_retrieve_context*)ctx, (api_private::kvs_callback_function)cbfn);
}

kvs_result kvs_delete_tuple(kvs_container_handle cont_hd, const kvs_key *key, const kvs_delete_context *ctx) {
  return (kvs_result)api_private::kvs_delete_tuple( (api_private::kvs_container_handle)cont_hd, (const api_private::kvs_key*)key, (const api_private::kvs_delete_context*)ctx);
}

kvs_result kvs_delete_tuple_async(kvs_container_handle cont_hd, const kvs_key* key, const kvs_delete_context* ctx, kvs_callback_function cbfn) {
  return (kvs_result)api_private::kvs_delete_tuple_async( (api_private::kvs_container_handle) cont_hd, (const api_private::kvs_key*) key, (const api_private::kvs_delete_context*) ctx, (api_private::kvs_callback_function)cbfn); 
}

kvs_result kvs_exist_tuples(kvs_container_handle cont_hd, uint32_t key_cnt, const kvs_key *keys, uint32_t buffer_size, uint8_t *result_buffer, const kvs_exist_context *ctx) {
  return (kvs_result)kvs_exist_tuples( (api_private::kvs_container_handle)cont_hd, key_cnt, (const api_private::kvs_key*)keys, buffer_size, result_buffer, (const api_private::kvs_exist_context*)ctx);
}

kvs_result kvs_exist_tuples_async(kvs_container_handle cont_hd, uint32_t key_cnt, const kvs_key *keys, uint32_t buffer_size, uint8_t *result_buffer, const kvs_exist_context *ctx, kvs_callback_function cbfn) {
  return (kvs_result)api_private::kvs_exist_tuples_async((api_private::kvs_container_handle)cont_hd, key_cnt, (const api_private::kvs_key*)keys, buffer_size, result_buffer, (const api_private::kvs_exist_context*)ctx, (api_private::kvs_callback_function)cbfn);
}

kvs_result kvs_open_iterator(kvs_container_handle cont_hd, const kvs_iterator_context *ctx, kvs_iterator_handle *iter_hd) {
  return (kvs_result)api_private::kvs_open_iterator( (api_private::kvs_container_handle)cont_hd, (const api_private::kvs_iterator_context*)ctx, (api_private::kvs_iterator_handle*)iter_hd);
}

kvs_result kvs_close_iterator(kvs_container_handle cont_hd, kvs_iterator_handle hiter, const kvs_iterator_context *ctx) {
  return (kvs_result)api_private::kvs_close_iterator( (api_private::kvs_container_handle)cont_hd, (api_private::kvs_iterator_handle)hiter, (const api_private::kvs_iterator_context*)ctx);
}

kvs_result kvs_close_iterator_all(kvs_container_handle cont_hd) {
  return (kvs_result)api_private::kvs_close_iterator_all( (api_private::kvs_container_handle)cont_hd);
}

kvs_result kvs_list_iterators(kvs_container_handle cont_hd, kvs_iterator_info *kvs_iters, int count) {
  return (kvs_result)api_private::kvs_list_iterators( (api_private::kvs_container_handle)cont_hd, (api_private::kvs_iterator_info*)kvs_iters, count);
}

kvs_result kvs_iterator_next(kvs_container_handle cont_hd, kvs_iterator_handle hiter, kvs_iterator_list *iter_list, const kvs_iterator_context *ctx) {
  return (kvs_result)api_private::kvs_iterator_next( (api_private::kvs_container_handle)cont_hd, (api_private::kvs_iterator_handle)hiter, (api_private::kvs_iterator_list*)iter_list, (const api_private::kvs_iterator_context*)ctx);
}

kvs_result kvs_iterator_next_async(kvs_container_handle cont_hd, kvs_iterator_handle iter_hd, kvs_iterator_list *iter_list, const kvs_iterator_context *ctx, kvs_callback_function cbfn) {
  return (kvs_result)api_private::kvs_iterator_next_async( (api_private::kvs_container_handle)cont_hd, (api_private::kvs_iterator_handle)iter_hd, (api_private::kvs_iterator_list*)iter_list, (const api_private::kvs_iterator_context*)ctx,\
     (api_private::kvs_callback_function)cbfn);
}

kvs_result kvs_get_device_waf(kvs_device_handle dev_hd, float *waf) {
  return (kvs_result)api_private::kvs_get_device_waf( (api_private::kvs_device_handle)dev_hd, waf);
}

kvs_result kvs_get_device_info(kvs_device_handle dev_hd, kvs_device *dev_info) {
  return (kvs_result)api_private::kvs_get_device_info( (api_private::kvs_device_handle)dev_hd, (api_private::kvs_device*)dev_info);
}

kvs_result kvs_get_device_utilization(kvs_device_handle dev_hd, int32_t *dev_util) {
  return (kvs_result)api_private::kvs_get_device_utilization( (api_private::kvs_device_handle)dev_hd, dev_util);
}

kvs_result kvs_get_device_capacity(kvs_device_handle dev_hd, int64_t *dev_capa) {
  return (kvs_result)api_private::kvs_get_device_capacity( (api_private::kvs_device_handle)dev_hd, dev_capa);
}

kvs_result kvs_get_min_key_length (kvs_device_handle dev_hd, int32_t *min_key_length) {
  return (kvs_result)api_private::kvs_get_min_key_length( (api_private::kvs_device_handle)dev_hd, min_key_length);
}

kvs_result kvs_get_max_key_length (kvs_device_handle dev_hd, int32_t *max_key_length) {
  return (kvs_result)api_private::kvs_get_max_key_length( (api_private::kvs_device_handle)dev_hd, max_key_length);
}

kvs_result kvs_get_min_value_length (kvs_device_handle dev_hd, int32_t *min_value_length) {
  return (kvs_result)api_private::kvs_get_min_value_length( (api_private::kvs_device_handle)dev_hd, min_value_length);
}

kvs_result kvs_get_max_value_length (kvs_device_handle dev_hd, int32_t *max_value_length) {
  return (kvs_result)api_private::kvs_get_max_value_length( (api_private::kvs_device_handle)dev_hd, max_value_length);
}

kvs_result kvs_get_optimal_value_length (kvs_device_handle dev_hd, int32_t *opt_value_length) {
  return (kvs_result)api_private::kvs_get_optimal_value_length( (api_private::kvs_device_handle)dev_hd, opt_value_length);
}

const char *kvs_errstr(int32_t errorno) {
  return api_private::kvs_errstr(errorno);
}


#else

#include <unistd.h>
#include <limits.h>
#include "kv_config.hpp"

#define CFG_PATH  "../env_init.conf"
#define convert_res(ret)  (result_mapping[ret])

pthread_mutex_t env_mutex = PTHREAD_MUTEX_INITIALIZER;

struct {
  bool initialized = false;
  uint16_t opened_device_num = 0;
} g_env;

typedef struct {
  kvs_exist_list* list;
  api_private::kvs_key* key;
}exist_data;

typedef struct postprocess_data{
  kvs_postprocess_function post_fn;
  kvs_key* key;

  postprocess_data(kvs_postprocess_function func, kvs_key* k):post_fn(func), key(k) {}
}post_data;

typedef struct {
  api_private::kvs_iterator_list pri_list;
  kvs_iterator_list *iter_list;
}iterator_data;

api_private::kvs_key* _get_private_key(kvs_key* k) {
  api_private::kvs_key* key = new api_private::kvs_key();
  key->key = k->key;
  key->length = (api_private::kvs_key_t)k->length;
  return key;
}

kvs_result result_mapping[api_private::KVS_ERR_CONT_OPEN + 1];  // mapping table of kvs_result
void init_result_mapping() {
#define RES_MAP(res1, res2) (result_mapping[api_private::res1] = res2)
  RES_MAP(KVS_SUCCESS, KVS_SUCCESS);
  RES_MAP(KVS_ERR_BUFFER_SMALL, KVS_ERR_BUFFER_SMALL);
  RES_MAP(KVS_ERR_COMMAND_INITIALIZED, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_COMMAND_SUBMITTED, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_DEV_CAPACITY, KVS_ERR_DEV_CAPAPCITY);
  RES_MAP(KVS_ERR_DEV_INIT, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_DEV_INITIALIZED, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_DEV_NOT_EXIST, KVS_ERR_DEV_NOT_EXIST);
  RES_MAP(KVS_ERR_DEV_SANITIZE_FAILED, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_DEV_SANIZE_IN_PROGRESS, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_ITERATOR_COND_INVALID, KVS_ERR_ITERATOR_FILTER_INVALID);
  RES_MAP(KVS_ERR_ITERATOR_MAX, KVS_ERR_ITERATOR_MAX);
  RES_MAP(KVS_ERR_ITERATOR_NOT_EXIST, KVS_ERR_ITERATOR_NOT_EXIST);
  RES_MAP(KVS_ERR_ITERATOR_OPEN, KVS_ERR_ITERATOR_OPEN);
  RES_MAP(KVS_ERR_KEY_EXIST, KVS_ERR_VALUE_UPDATE_NOT_ALLOWED);
  RES_MAP(KVS_ERR_KEY_INVALID, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_KEY_LENGTH_INVALID, KVS_ERR_KEY_LENGTH_INVALID);
  RES_MAP(KVS_ERR_KEY_NOT_EXIST, KVS_ERR_KEY_NOT_EXIST);
  RES_MAP(KVS_ERR_OPTION_INVALID, KVS_ERR_OPTION_INVALID);
  RES_MAP(KVS_ERR_PARAM_INVALID, KVS_ERR_PARAM_INVALID);
  RES_MAP(KVS_ERR_PURGE_IN_PROGRESS, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_QUEUE_CQID_INVALID, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_QUEUE_DELETION_INVALID, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_QUEUE_IN_SUTDOWN, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_QUEUE_IS_FULL, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_QUEUE_MAX_QUEUE, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_QUEUE_QID_INVALID, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_QUEUE_QSIZE_INVALID, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_QUEUE_SQID_INVALID, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_SYS_BUSY, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_SYS_IO, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_TIMEOUT, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_UNCORRECTIBLE, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_VALUE_LENGTH_INVALID, KVS_ERR_VALUE_LENGTH_INVALID);
  RES_MAP(KVS_ERR_VALUE_LENGTH_MISALIGNED, KVS_ERR_PARAM_INVALID);
  RES_MAP(KVS_ERR_VALUE_OFFSET_INVALID, KVS_ERR_VALUE_OFFSET_MISALIGNED);
  RES_MAP(KVS_ERR_VALUE_UPDATE_NOT_ALLOWED, KVS_ERR_VALUE_UPDATE_NOT_ALLOWED);
  RES_MAP(KVS_ERR_VENDOR, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_PERMISSION, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_ENV_NOT_INITIALIZED, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_DEV_NOT_OPENED, KVS_ERR_DEV_NOT_OPENED);
  RES_MAP(KVS_ERR_DEV_ALREADY_OPENED, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_DEV_PATH_TOO_LONG, KVS_ERR_PARAM_INVALID);
  RES_MAP(KVS_ERR_ITERATOR_NUM_OUT_RANGE, KVS_ERR_PARAM_INVALID);
  RES_MAP(KVS_ERR_DD_UNSUPPORTED, KVS_ERR_UNSUPPORTED);
  RES_MAP(KVS_ERR_CACHE_INVALID_PARAM, KVS_ERR_PARAM_INVALID);
  RES_MAP(KVS_ERR_CACHE_NO_CACHED_KEY, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_DD_INVALID_QUEUE_TYPE, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_DD_NO_AVAILABLE_RESOURCE, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_DD_NO_DEVICE, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_DD_UNSUPPORTED_CMD, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_DECOMPRESSION, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_HEAP_ALLOC_FAILURE, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_ITERATE_HANDLE_ALREADY_OPENED, KVS_ERR_ITERATOR_OPEN);
  RES_MAP(KVS_ERR_ITERATE_REQUEST_FAIL, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_MAXIMUM_VALUE_SIZE_LIMIT_EXCEEDED, KVS_ERR_PARAM_INVALID);
  RES_MAP(KVS_ERR_MISALIGNED_KEY_SIZE, KVS_ERR_PARAM_INVALID);
  RES_MAP(KVS_ERR_MISALIGNED_VALUE_OFFSET, KVS_ERR_VALUE_OFFSET_MISALIGNED);
  RES_MAP(KVS_ERR_SDK_CLOSE, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_SDK_OPEN, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_SLAB_ALLOC_FAILURE, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_UNRECOVERED_ERROR, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_NS_ATTACHED, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_NS_CAPACITY, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_NS_DEFAULT, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_NS_INVALID, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_NS_MAX, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_NS_NOT_ATTACHED, KVS_ERR_SYS_IO);
  RES_MAP(KVS_ERR_CONT_CAPACITY, KVS_ERR_KS_CAPACITY);
  RES_MAP(KVS_ERR_CONT_CLOSE, KVS_ERR_KS_NOT_OPEN);
  RES_MAP(KVS_ERR_CONT_EXIST, KVS_ERR_KS_EXIST);
  RES_MAP(KVS_ERR_CONT_GROUP_BY, KVS_ERR_OPTION_INVALID);
  RES_MAP(KVS_ERR_CONT_NAME, KVS_ERR_KS_NAME);
  RES_MAP(KVS_ERR_CONT_NOT_EXIST, KVS_ERR_KS_NOT_EXIST);
  RES_MAP(KVS_ERR_CONT_OPEN, KVS_ERR_KS_OPEN);
  RES_MAP(KVS_ERR_ITERATOR_BUFFER_SIZE, KVS_ERR_BUFFER_SMALL);
}

static void filter2context(kvs_key_group_filter* fltr, api_private::kvs_iterator_context* ctx) {
  ctx->bitmask = (uint32_t)fltr->bitmask[0] | ((uint32_t)fltr->bitmask[1]) << 8 | ((uint32_t)fltr->bitmask[2]) << 16 | ((uint32_t)fltr->bitmask[3]) << 24;
  ctx->bit_pattern = (uint32_t)fltr->bit_pattern[0] | ((uint32_t)fltr->bit_pattern[1]) << 8 | ((uint32_t)fltr->bit_pattern[2]) << 16 | ((uint8_t)fltr->bit_pattern[3]) << 24;
}

static kvs_result _check_key_length(kvs_key* key) {
  if (key->length > UINT8_MAX)
    return KVS_ERR_KEY_LENGTH_INVALID;
  return KVS_SUCCESS;
}

void init_default_option(api_private::kvs_init_options &options) {
  api_private::kvs_init_env_opts(&options);
  options.aio.iocoremask = 0;
  options.aio.queuedepth = 64;
  options.memory.use_dpdk = 0;
  const char* configfile = "../kvssd_emul.conf";
  options.emul_config_file = malloc(PATH_MAX);
  strncpy(options.emul_config_file, configfile, strlen(configfile) + 1);
  char* core;
  core = options.udd.core_mask_str;
  *core = '0';
  core = options.udd.cq_thread_mask;
  *core = '0';
  options.udd.mem_size_mb = 1024;
  options.udd.syncio = 1;
#ifdef WITH_SPDK
  options.memory.use_dpdk = 1;
#endif
}

/*void print_options(api_private::kvs_init_options &options) {
  printf("start to print options:\n");
  printf("iocoremask:%d, queuedepth:%d, config file:%s\n", options.aio.iocoremask, options.aio.queuedepth, options.emul_config_file);
  printf("use_dpdk=%d\n",  options.memory.use_dpdk);
  if (options.memory.use_dpdk) {
    printf("core mask:%s, cq thread mask:%s, mem_size:%d, syncio:%d", options.udd.core_mask_str, options.udd.cq_thread_mask, options.udd.mem_size_mb, options.udd.syncio);
  }
}*/

void init_env_from_cfgfile(api_private::kvs_init_options &options) {
  init_default_option(options);
  std::string file_path(CFG_PATH);
  kvadi::kv_config cfg(file_path);

  int queue_depth = atoi(cfg.getkv("aio", "queue_depth").c_str());;
  options.aio.iocoremask = (uint64_t)atoi(cfg.getkv("aio", "iocoremask").c_str());
  options.aio.queuedepth = queue_depth == 0 ? options.aio.queuedepth : (uint32_t)queue_depth;
  std::string cfg_file_path = cfg.getkv("emu", "cfg_file");
  if (cfg_file_path != "") {
    strncpy(options.emul_config_file, cfg_file_path.c_str(), cfg_file_path.length() + 1);
  }
#ifdef WITH_SPDK
  options.memory.use_dpdk = 1;
  if (cfg.getkv("udd", "core_mask_str").c_str() != "")
    strcpy(options.udd.core_mask_str, cfg.getkv("udd", "core_mask_str").c_str());
  if (cfg.getkv("udd", "cq_thread_mask").c_str() != "")
    strcpy(options.udd.cq_thread_mask, cfg.getkv("udd", "cq_thread_mask").c_str());
  int mem_size = atoi(cfg.getkv("udd", "memory_size").c_str());
  options.udd.mem_size_mb = mem_size == 0 ? options.udd.mem_size_mb : (uint32_t)mem_size;
  options.udd.syncio = cfg.getkv("udd", "syncio") == "0" ? 0 : 1;
#endif

  return;
}

/*void test_init() {
  api_private::kvs_init_options options;
  init_env_from_cfgfile(options);
  print_options(options);
}*/

api_private::kvs_result init_env() {
  api_private::kvs_result ret;
  if (g_env.initialized)
    return api_private::KVS_SUCCESS;

  init_result_mapping();
  api_private::kvs_init_options options;
  init_env_from_cfgfile(options);
  ret = api_private::kvs_init_env(&options);
  if (options.emul_config_file) free(options.emul_config_file);
  if (ret != api_private::KVS_SUCCESS)
    return ret;

  g_env.initialized = true;
  return ret;
}

kvs_result kvs_open_device(char *URI, kvs_device_handle *dev_hd) {
  api_private::kvs_result ret;
  pthread_mutex_lock(&env_mutex);
  ret = init_env();
  if (ret != api_private::KVS_SUCCESS)
    return convert_res(ret);

  ret = api_private::kvs_open_device(URI, (api_private::kvs_device_handle*)dev_hd);
  if (ret == api_private::KVS_SUCCESS)
    ++g_env.opened_device_num;

  pthread_mutex_unlock(&env_mutex);
  return convert_res(ret);
}

kvs_result kvs_close_device(kvs_device_handle dev_hd) {
  pthread_mutex_lock(&env_mutex);
  api_private::kvs_result ret = api_private::kvs_close_device( (api_private::kvs_device_handle)dev_hd);
  if (ret == api_private::KVS_SUCCESS) {
    if (--g_env.opened_device_num == 0) {
      api_private::kvs_exit_env();
      g_env.initialized = false;
    }
  }
  pthread_mutex_unlock(&env_mutex);
  return convert_res(ret);
}

kvs_result kvs_get_device_info(kvs_device_handle dev_hd, kvs_device *dev_info) {
  api_private::kvs_device pri_dev_info;
  api_private::kvs_result ret;

  if (dev_info == NULL)
    return KVS_ERR_PARAM_INVALID;

  ret = api_private::kvs_get_device_info( (api_private::kvs_device_handle)dev_hd, &pri_dev_info);
  dev_info->capacity = (uint64_t)pri_dev_info.capacity;
  dev_info->extended_info = pri_dev_info.extended_info;
  dev_info->max_key_len = pri_dev_info.max_key_len;
  dev_info->max_value_len = pri_dev_info.max_value_len;
  dev_info->optimal_value_granularity = pri_dev_info.optimal_value_granularity;
  dev_info->optimal_value_len = pri_dev_info.optimal_value_len;
  dev_info->unalloc_capacity = (uint64_t)pri_dev_info.unalloc_capacity;

  return convert_res(ret);
}

kvs_result kvs_get_device_capacity(kvs_device_handle dev_hd, uint64_t *dev_capacity) {
  int64_t cap;
  api_private::kvs_result ret;

  if (dev_capacity == NULL)
    return KVS_ERR_PARAM_INVALID;
  
  ret = api_private::kvs_get_device_capacity( (api_private::kvs_device_handle)dev_hd, &cap);
  *dev_capacity = (uint64_t)cap;
  return convert_res(ret);
}

kvs_result kvs_get_device_utilization(kvs_device_handle dev_hd, uint32_t *dev_utilization) {
  int32_t uti;
  api_private::kvs_result ret;

  if (dev_utilization == NULL)
    return KVS_ERR_PARAM_INVALID;

  ret = api_private::kvs_get_device_utilization( (api_private::kvs_device_handle)dev_hd, &uti);
  *dev_utilization = (uint32_t)uti;
  return convert_res(ret);
}

kvs_result kvs_get_min_key_length(kvs_device_handle dev_hd, uint32_t *min_key_length) { int32_t min_len;
  api_private::kvs_result ret;

  if (min_key_length == NULL)
    return KVS_ERR_PARAM_INVALID;

  ret = api_private::kvs_get_min_key_length( (api_private::kvs_device_handle)dev_hd, &min_len);
  *min_key_length = (uint32_t)min_len;
  return convert_res(ret);
}

kvs_result kvs_get_max_key_length(kvs_device_handle dev_hd, uint32_t *max_key_length) {
  int32_t max_len;
  api_private::kvs_result ret;

  if (max_key_length == NULL)
    return KVS_ERR_PARAM_INVALID;

  ret = api_private::kvs_get_max_key_length( (api_private::kvs_device_handle)dev_hd, &max_len);
  *max_key_length = (uint32_t)max_len;
  return convert_res(ret);
}

kvs_result kvs_get_min_value_length(kvs_device_handle dev_hd, uint32_t *min_value_length) {
  int32_t min_len;
  api_private::kvs_result ret;

  if (min_value_length == NULL)
    return KVS_ERR_PARAM_INVALID;

  ret = api_private::kvs_get_min_value_length( (api_private::kvs_device_handle)dev_hd, &min_len);
  *min_value_length = (uint32_t)min_len;
  return convert_res(ret);
}

kvs_result kvs_get_max_value_length(kvs_device_handle dev_hd, uint32_t *max_value_length) {
  int32_t max_len;
  api_private::kvs_result ret;

  if (max_value_length == NULL)
    return KVS_ERR_PARAM_INVALID;

  ret = api_private::kvs_get_max_value_length( (api_private::kvs_device_handle)dev_hd, &max_len);
  *max_value_length = (uint32_t)max_len;
  return convert_res(ret);
}

kvs_result kvs_get_optimal_value_length(kvs_device_handle dev_hd, uint32_t *opt_value_length) {
  int32_t opt_len;
  api_private::kvs_result ret;

  if (opt_value_length == NULL)
    return KVS_ERR_PARAM_INVALID;

  ret = api_private::kvs_get_optimal_value_length( (api_private::kvs_device_handle)dev_hd, &opt_len);
  *opt_value_length = (uint32_t)opt_len;
  return convert_res(ret);
}

kvs_result kvs_create_key_space(kvs_device_handle dev_hd, kvs_key_space_name *key_space_name, uint64_t size, kvs_option_key_space opt) {
  return KVS_SUCCESS;
}

kvs_result kvs_delete_key_space(kvs_device_handle dev_hd, kvs_key_space_name *key_space_name) {
  return KVS_SUCCESS;
}

kvs_result kvs_list_key_spaces(kvs_device_handle dev_hd, uint32_t index, uint32_t buffer_size, kvs_key_space_name *names, uint32_t *ks_cnt) {
  return KVS_ERR_KS_EXIST;
}

kvs_result kvs_open_key_space(kvs_device_handle dev_hd, char *name, kvs_key_space_handle *ks_hd) {
  api_private::kvs_container_handle cont_handle;

  api_private::kvs_device_handle dev_handle = (api_private::kvs_device_handle)dev_hd;
  api_private::kvs_result ret = api_private::kvs_open_container(dev_handle, name, &cont_handle);
  *ks_hd = (kvs_key_space_handle)cont_handle;
  return convert_res(ret);
}

kvs_result kvs_close_key_space(kvs_key_space_handle ks_hd) {
  api_private::kvs_container_handle cont_handle = (api_private::kvs_container_handle)ks_hd;
  return convert_res(api_private::kvs_close_container(cont_handle));
}

kvs_result kvs_get_key_space_info(kvs_key_space_handle ks_hd, kvs_key_space *ks) {
  return KVS_ERR_UNSUPPORTED;
  api_private::kvs_container cont;
  api_private::kvs_container_handle cont_handle = (api_private::kvs_container_handle)ks_hd;

  if (ks == NULL)
    return KVS_ERR_PARAM_INVALID;

  api_private::kvs_result ret = api_private::kvs_get_container_info(cont_handle, &cont);
  ks->capacity = cont.capacity;
  ks->count = cont.count;
  ks->free_size = cont.free_size;
  ks->opened = cont.opened;
  // ks->name = (kvs_key_space_name*)malloc(sizeof(kvs_key_space_name));
  ks->name->name_len = cont.name->name_len;
  ks->name->name = cont.name->name;
  return convert_res(ret);
}

kvs_result kvs_get_kvp_info(kvs_key_space_handle ks_hd, kvs_key *key, kvs_kvp_info *info) {
  api_private::kvs_tuple_info tp_info;
  api_private::kvs_key k;

  if (key == NULL || info == NULL)
    return KVS_ERR_PARAM_INVALID;

  if (_check_key_length(key) != KVS_SUCCESS)
    return KVS_ERR_KEY_LENGTH_INVALID;

  api_private::kvs_container_handle cont_handle = (api_private::kvs_container_handle)ks_hd;
  k.length = (uint8_t)key->length;
  k.key = key->key;
  api_private::kvs_result ret = api_private::kvs_get_tuple_info(cont_handle, &k, &tp_info);
  info->key_len = (uint16_t)tp_info.key_length;
  info->value_len = tp_info.value_length;
  info->key = (uint8_t*)key->key;
  return convert_res(ret);
}

kvs_result kvs_retrieve_kvp(kvs_key_space_handle ks_hd, kvs_key *key, kvs_option_retrieve *opt, kvs_value *value) {
  api_private::kvs_key k;
  api_private::kvs_retrieve_context ctx;
  api_private::kvs_container_handle cont_hd = (api_private::kvs_container_handle)ks_hd;

  if (key == NULL || opt == NULL)
    return KVS_ERR_PARAM_INVALID;

  if (_check_key_length(key) != KVS_SUCCESS)
    return KVS_ERR_KEY_LENGTH_INVALID;

  ctx.option.kvs_retrieve_delete = opt->kvs_retrieve_delete;
  ctx.option.kvs_retrieve_decompress = false;
  k.length = (uint8_t)key->length;
  k.key = key->key;

  return convert_res(api_private::kvs_retrieve_tuple(cont_hd, &k, (api_private::kvs_value*)value, &ctx));
}

static kvs_context op2context(uint8_t op) {
  static kvs_context op_code[KVS_CMD_STORE + 1] = {0, KVS_CMD_STORE, KVS_CMD_RETRIEVE, KVS_CMD_DELETE, KVS_CMD_EXIST, KVS_CMD_ITER_CREATE, KVS_CMD_ITER_DELETE, KVS_CMD_ITER_NEXT};
  return op_code[op];
}

void private_callback_func(api_private::kvs_callback_context* ioctx) {
  kvs_postprocess_context postctx;

  postctx.context = op2context(ioctx->opcode);
  postctx.iter_hd = (kvs_iterator_handle*)ioctx->iter_hd;
  postctx.ks_hd = (kvs_key_space_handle*)ioctx->cont_hd;
  postctx.value = (kvs_value*)ioctx->value;
  // key is in private1
  postprocess_data* pd = (postprocess_data*)ioctx->private1;
  postctx.key = pd->key;
  if (ioctx->key)
    delete ioctx->key;
  if (postctx.context == KVS_CMD_EXIST) {
    exist_data* ed = (exist_data*)(ioctx->private2);
    postctx.result_buffer.list = ed->list;
    //free(ed->key); already freed
    free(ed);
  }
  else if (postctx.context == KVS_CMD_ITER_NEXT) {
    iterator_data* iter_data = (iterator_data*)(ioctx->private2);
    iter_data->iter_list->num_entries = iter_data->pri_list.num_entries;
    iter_data->iter_list->size = iter_data->pri_list.size;
    if (iter_data->pri_list.end)
      iter_data->iter_list->end = true;
    else
      iter_data->iter_list->end = false;
    postctx.result_buffer.iter_list = iter_data->iter_list;
    free(iter_data);
  }
  else
    postctx.option = ioctx->private2;
  postctx.result = convert_res(ioctx->result);

  pd->post_fn(&postctx);
  delete pd;
}

kvs_result kvs_retrieve_kvp_async(kvs_key_space_handle ks_hd, kvs_key *key, kvs_option_retrieve *opt, kvs_value *value, kvs_postprocess_function post_fn) {
  api_private::kvs_retrieve_context ctx;

  if (key == NULL || opt == NULL || post_fn == NULL)
    return KVS_ERR_PARAM_INVALID;

  if (_check_key_length(key) != KVS_SUCCESS)
    return KVS_ERR_KEY_LENGTH_INVALID;

  api_private::kvs_key* k = _get_private_key(key);
  api_private::kvs_container_handle cont_hd = (api_private::kvs_container_handle)ks_hd;

  ctx.option.kvs_retrieve_decompress = false;
  ctx.option.kvs_retrieve_delete = opt->kvs_retrieve_delete;
  ctx.private1 = (void*)(new postprocess_data(post_fn, key));
  ctx.private2 = (void*)opt;
  
  return convert_res(api_private::kvs_retrieve_tuple_async(cont_hd, k, (api_private::kvs_value*)value, &ctx, private_callback_func));
}

kvs_result kvs_store_kvp(kvs_key_space_handle ks_hd, kvs_key *key, kvs_value *value, kvs_option_store *opt) {
  api_private::kvs_container_handle cont_hd = (api_private::kvs_container_handle)ks_hd;

  if (key == NULL || opt == NULL)
    return KVS_ERR_PARAM_INVALID;

  if (_check_key_length(key) != KVS_SUCCESS)
    return KVS_ERR_KEY_LENGTH_INVALID;

  api_private::kvs_key k = {key->key, (api_private::kvs_key_t)key->length};
  api_private::kvs_store_context ctx;
  ctx.option.st_type = (api_private::kvs_store_type)opt->st_type;
  ctx.option.kvs_store_compress = false;

  return convert_res(api_private::kvs_store_tuple(cont_hd, &k, (api_private::kvs_value*)value, &ctx));
}

kvs_result kvs_store_kvp_async(kvs_key_space_handle ks_hd, kvs_key *key, kvs_value *value, kvs_option_store *opt, kvs_postprocess_function post_fn) {
  api_private::kvs_container_handle cont_hd = (api_private::kvs_container_handle)ks_hd;

  if (key == NULL || opt == NULL || post_fn == NULL)
    return KVS_ERR_PARAM_INVALID;

  if (_check_key_length(key) != KVS_SUCCESS)
    return KVS_ERR_KEY_LENGTH_INVALID;

  api_private::kvs_key* k = _get_private_key(key);
  api_private::kvs_store_context ctx;

  ctx.option.st_type = (api_private::kvs_store_type)opt->st_type;
  ctx.option.kvs_store_compress = false;
  ctx.private1 = (void*)(new postprocess_data(post_fn, key));
  ctx.private2 = (void*)opt;
  
  return convert_res(api_private::kvs_store_tuple_async(cont_hd, k, (api_private::kvs_value*)value, &ctx, private_callback_func));
}

kvs_result kvs_delete_kvp(kvs_key_space_handle ks_hd, kvs_key* key, kvs_option_delete *opt) {
  api_private::kvs_container_handle cont_hd = (api_private::kvs_container_handle)ks_hd;

  if (key == NULL || opt == NULL)
    return KVS_ERR_PARAM_INVALID;

  if (_check_key_length(key) != KVS_SUCCESS)
    return KVS_ERR_KEY_LENGTH_INVALID;

  api_private::kvs_key k = {key->key, (api_private::kvs_key_t)key->length};
  api_private::kvs_delete_context ctx;

  ctx.option = {opt->kvs_delete_error};

  return convert_res(api_private::kvs_delete_tuple(cont_hd, &k, &ctx));
}

kvs_result kvs_delete_kvp_async(kvs_key_space_handle ks_hd, kvs_key* key, kvs_option_delete *opt, kvs_postprocess_function post_fn) {
  api_private::kvs_container_handle cont_hd = (api_private::kvs_container_handle)ks_hd;

  if (key == NULL || opt == NULL || post_fn == NULL)
    return KVS_ERR_PARAM_INVALID;

  if (_check_key_length(key) != KVS_SUCCESS)
    return KVS_ERR_KEY_LENGTH_INVALID;

  api_private::kvs_key* k = _get_private_key(key);
  api_private::kvs_delete_context ctx;

  ctx.option = {opt->kvs_delete_error};
  ctx.private1 = (void*)(new postprocess_data(post_fn, key));
  ctx.private2 = (void*)opt;

  return convert_res(api_private::kvs_delete_tuple_async(cont_hd, k, &ctx, private_callback_func));
}

kvs_result kvs_create_iterator(kvs_key_space_handle ks_hd, kvs_option_iterator *iter_op, kvs_key_group_filter *iter_fltr, kvs_iterator_handle *iter_hd) {
  api_private::kvs_container_handle cont_hd = (api_private::kvs_container_handle)ks_hd;
  api_private::kvs_iterator_context ctx;

  if (iter_op == NULL || iter_fltr == NULL)
    return KVS_ERR_PARAM_INVALID;

  ctx.option.iter_type = (api_private::kvs_iterator_type)iter_op->iter_type;
  filter2context(iter_fltr, &ctx);

  return convert_res(api_private::kvs_open_iterator(cont_hd, &ctx, (api_private::kvs_iterator_handle*)iter_hd));
}

kvs_result kvs_delete_iterator(kvs_key_space_handle ks_hd, kvs_iterator_handle iter_hd) {
  api_private::kvs_iterator_context ctx;
  api_private::kvs_container_handle cont_hd = (api_private::kvs_container_handle)ks_hd;
  api_private::kvs_iterator_handle it_hd = (api_private::kvs_iterator_handle)iter_hd;

  return convert_res(api_private::kvs_close_iterator(cont_hd, it_hd, &ctx));
}

kvs_result kvs_iterate_next(kvs_key_space_handle ks_hd, kvs_iterator_handle iter_hd, kvs_iterator_list *iter_list) {
  api_private::kvs_container_handle cont_hd = (api_private::kvs_container_handle)ks_hd;
  api_private::kvs_iterator_handle it_hd = (api_private::kvs_iterator_handle)iter_hd;
  api_private::kvs_iterator_context ctx;
  api_private::kvs_iterator_list list;

  if (iter_list == NULL)
    return KVS_ERR_PARAM_INVALID;

  list.it_list = iter_list->it_list;
  list.size = iter_list->size;

  api_private::kvs_result ret = api_private::kvs_iterator_next(cont_hd, it_hd, &list, &ctx);
  if (ret == api_private::KVS_SUCCESS) {
    iter_list->num_entries = list.num_entries;
    iter_list->size = list.size;
    if (list.end)
      iter_list->end = true;
    else
      iter_list->end = false;
  }
  return convert_res(ret);
}

kvs_result kvs_iterate_next_async(kvs_key_space_handle ks_hd, kvs_iterator_handle iter_hd , kvs_iterator_list *iter_list, kvs_postprocess_function post_fn) {
  api_private::kvs_container_handle cont_hd = (api_private::kvs_container_handle)ks_hd;
  api_private::kvs_iterator_handle it_hd = (api_private::kvs_iterator_handle)iter_hd;
  api_private::kvs_iterator_context ctx;
  iterator_data *iter_data = (iterator_data*)malloc(sizeof(iterator_data));

  if (iter_list == NULL || post_fn == NULL)
    return KVS_ERR_PARAM_INVALID;

  iter_data->iter_list = iter_list;
  iter_data->pri_list.it_list = iter_list->it_list;
  iter_data->pri_list.size = iter_list->size;
  ctx.private1 = (void*)(new postprocess_data(post_fn, NULL));
  ctx.private2 = (void*)iter_data;

  return convert_res(api_private::kvs_iterator_next_async(cont_hd, it_hd, &(iter_data->pri_list), &ctx, private_callback_func));
}

kvs_result kvs_exist_kv_pairs(kvs_key_space_handle ks_hd, uint32_t key_cnt, kvs_key *keys, kvs_exist_list *list) {
  if (keys == NULL || list == NULL)
    return KVS_ERR_PARAM_INVALID;

  api_private::kvs_container_handle cont_hd = (api_private::kvs_container_handle)ks_hd;
  api_private::kvs_exist_context ctx;
  api_private::kvs_key* key_list = (api_private::kvs_key*)malloc(key_cnt * sizeof(api_private::kvs_key));
  for (unsigned int i = 0; i != key_cnt; ++i) {
    if (_check_key_length(keys + i) != KVS_SUCCESS) {
      free(key_list);
      return KVS_ERR_KEY_LENGTH_INVALID;
    }
    key_list[i].length = (api_private::kvs_key_t)keys[i].length;
    key_list[i].key = keys[i].key;
  }
  list->keys = keys;
  list->num_keys = key_cnt;
  kvs_result ret = convert_res(api_private::kvs_exist_tuples(cont_hd, key_cnt, key_list, list->length, list->result_buffer, &ctx));
  free(key_list);
  return ret;
}

kvs_result kvs_exist_kv_pairs_async(kvs_key_space_handle ks_hd, uint32_t key_cnt, kvs_key *keys, kvs_exist_list *list, kvs_postprocess_function post_fn) {
  if (keys == NULL || list == NULL || post_fn == NULL)
    return KVS_ERR_PARAM_INVALID;

  api_private::kvs_container_handle cont_hd = (api_private::kvs_container_handle)ks_hd;
  api_private::kvs_exist_context ctx;
  api_private::kvs_key* key_list = (api_private::kvs_key*)malloc(key_cnt * sizeof(api_private::kvs_key));
  for (unsigned int i = 0; i != key_cnt; ++i) {
    if (_check_key_length(keys + i) != KVS_SUCCESS) {
      free(key_list);
      return KVS_ERR_KEY_LENGTH_INVALID;
    }
    key_list[i].length = (api_private::kvs_key_t)keys[i].length;
    key_list[i].key = keys[i].key;
  }
  list->keys = keys;
  list->num_keys = key_cnt;

  exist_data* ed = (exist_data*)malloc(sizeof(exist_data));
  ed->list = list;
  ed->key = key_list;
  ctx.private1 = (void*)(new postprocess_data(post_fn, keys));
  ctx.private2 = (void*)ed;

  return convert_res(api_private::kvs_exist_tuples_async(cont_hd, key_cnt, key_list, list->length, list->result_buffer, &ctx, private_callback_func));
}

kvs_result kvs_delete_key_group(kvs_key_space_handle ks_hd, kvs_key_group_filter *grp_fltr) {
  return KVS_ERR_UNSUPPORTED;             // does not support delete key group for now
  api_private::kvs_container_handle cont_hd = (api_private::kvs_container_handle)ks_hd;
  api_private::kvs_iterator_handle iter_hd;
  api_private::kvs_iterator_context ctx;
  api_private::kvs_iterator_info kvs_iters[KVS_MAX_ITERATE_HANDLE];

  if (grp_fltr == NULL)
    return KVS_ERR_PARAM_INVALID;

  ctx.option = {api_private::KVS_ITERATOR_WITH_DELETE};
  filter2context(grp_fltr, &ctx);
  api_private::kvs_result ret = api_private::kvs_open_iterator(cont_hd, &ctx, &iter_hd);
  if (ret != api_private::KVS_SUCCESS)
    return convert_res(ret);

  while (true) {
    ret = api_private::kvs_list_iterators(cont_hd, kvs_iters, KVS_MAX_ITERATE_HANDLE);
    if (ret != api_private::KVS_SUCCESS) {
      api_private::kvs_close_iterator(cont_hd, iter_hd, &ctx);      // context is needed but useless in kvs_close_iterator
      return convert_res(ret);
    }
    for (int i = 0; i != KVS_MAX_ITERATE_HANDLE; ++i) {
      if (kvs_iters[i].iter_handle == iter_hd && kvs_iters[i].is_eof) {
        api_private::kvs_close_iterator(cont_hd, iter_hd, &ctx);
        return KVS_SUCCESS;
      }
    }
    usleep(100000);
  }
}

typedef struct {
  kvs_postprocess_function post_fn;
  api_private::kvs_container_handle cont_hd;
  api_private::kvs_iterator_handle it_hd;
}delete_key_group_context;

void* delete_key_group_checker(void* arg) {
  kvs_postprocess_context post_ctx;
  delete_key_group_context* dkg_ctx = (delete_key_group_context*)arg;
  api_private::kvs_iterator_info kvs_iters[KVS_MAX_ITERATE_HANDLE];
  api_private::kvs_iterator_context ctx;

  post_ctx.context = KVS_CMD_DELETE_GROUP;
  post_ctx.ks_hd = (kvs_key_space_handle*)&(dkg_ctx->cont_hd);
  post_ctx.iter_hd = &(dkg_ctx->it_hd);
  while(true) {
    api_private::kvs_result ret = api_private::kvs_list_iterators(dkg_ctx->cont_hd, kvs_iters, KVS_MAX_ITERATE_HANDLE);
    if (ret != api_private::KVS_SUCCESS) {
      post_ctx.result = convert_res(ret);
      dkg_ctx->post_fn(&post_ctx);
      api_private::kvs_close_iterator(dkg_ctx->cont_hd, dkg_ctx->it_hd, &ctx);      // context is needed but useless in kvs_close_iterator
      free(arg);
      return NULL;
    }
    for (int i = 0; i != KVS_MAX_ITERATE_HANDLE; ++i) {
      if (kvs_iters[i].iter_handle == dkg_ctx->it_hd && kvs_iters[i].is_eof) {
        post_ctx.result = KVS_SUCCESS;
        dkg_ctx->post_fn(&post_ctx);
        api_private::kvs_close_iterator(dkg_ctx->cont_hd, dkg_ctx->it_hd, &ctx);
        free(arg);
        return NULL;
      }
    }
    usleep(100000);
  }
}

kvs_result kvs_delete_key_group_async(kvs_key_space_handle ks_hd, kvs_key_group_filter *grp_fltr, kvs_postprocess_function post_fn) {
  return KVS_ERR_UNSUPPORTED;             // does not support delete key group for now
  pthread_t pid;
  api_private::kvs_container_handle cont_hd = (api_private::kvs_container_handle)ks_hd;
  api_private::kvs_iterator_handle iter_hd;
  api_private::kvs_iterator_context ctx;

  if (grp_fltr == NULL || post_fn == NULL)
    return KVS_ERR_PARAM_INVALID;

  ctx.option = {api_private::KVS_ITERATOR_WITH_DELETE};
  filter2context(grp_fltr, &ctx);
  api_private::kvs_result ret = api_private::kvs_open_iterator(cont_hd, &ctx, &iter_hd);
  if (ret != api_private::KVS_SUCCESS)
    return convert_res(ret);

  delete_key_group_context* dkg_ctx = (delete_key_group_context*)malloc(sizeof(delete_key_group_context));
  dkg_ctx->cont_hd = cont_hd;
  dkg_ctx->it_hd = iter_hd;
  dkg_ctx->post_fn = post_fn;
  if (pthread_create(&pid, NULL,  delete_key_group_checker, dkg_ctx) == -1) {
    free(dkg_ctx);
    return KVS_ERR_SYS_IO;
  }

  return KVS_SUCCESS;
}
#endif
