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

#include <iostream>
#include <fstream>
#include <algorithm>
#include <atomic>
#include <tbb/concurrent_queue.h>
#include <list>

#include "kvs_utils.h"
#include "udd.hpp"
#include "uddenv.h"

#include <udd/kvnvme.h>
#include <udd/kv_types.h>

#define MAX_POOLSIZE 10240
#define GB_SIZE (1024 * 1024 * 1024)

KUDDriver::KUDDriver(kv_device_priv *dev, kvs_callback_function user_io_complete_):
  KvsDriver(dev, user_io_complete_), queue_depth(256), num_cq_threads(1), mem_size_mb(1024)
{
  fprintf(stdout, "init udd\n");
}

void udd_iterate_cb(kv_iterate *it, unsigned int result, unsigned int status) {
  KUDDriver::kv_udd_context *ctx = (KUDDriver::kv_udd_context*)it->kv.param.private_data;
  kvs_callback_context *iocb = &ctx->iocb;
  iocb->result = (kvs_result)status;
  if(status != KV_SUCCESS){
    if(status ==  KV_ERR_ITERATE_READ_EOF){ //TODO: fix this with SNIA
      //fprintf(stderr, "[%s] EOF result=%d status=%d length=%d\n", __FUNCTION__, result, status, it->kv.value.length);
      iocb->result = KVS_SUCCESS;
      ctx->iter_list->end = 0x01;//TRUE;
    } else if (status == KV_ERR_ITERATE_FAIL_TO_PROCESS_REQUEST) {
      iocb->result = KVS_ERR_ITERATOR_NOT_EXIST;
    } else if (status == KV_WRN_MORE) {
      iocb->result = KVS_SUCCESS;
    } else if (status == KV_ERR_BUFFER) {
      iocb->result = KVS_ERR_BUFFER_SMALL;
    } else if (status == KV_ERR_INVALID_OPTION){
      iocb->result = KVS_ERR_OPTION_INVALID;
    } else {
      iocb->result = KVS_ERR_SYS_IO;
      fprintf(stderr, "[%s] error. result=0x%x status=0x%x\n", __FUNCTION__, result, status);
    }
  } 

  if(status == KV_SUCCESS || status == KV_ERR_ITERATE_READ_EOF || status == KV_WRN_MORE) {

    // first 4 bytes are for key counts
    uint32_t num_key = *((unsigned int*)it->kv.value.value);
    ctx->iter_list->num_entries = num_key;
  
    char *data_buff = (char *)it->kv.value.value;
    unsigned int buffer_size = it->kv.value.length;
    char *current_ptr = data_buff;
    
    unsigned int key_size = 0;
    int keydata_len_with_padding = 0;
    unsigned int buffdata_len = buffer_size;

    buffdata_len -= KV_IT_READ_BUFFER_META_LEN;
    data_buff += KV_IT_READ_BUFFER_META_LEN;
    for (uint32_t i = 0; i < num_key && buffdata_len > 0; i++) {
      if (buffdata_len < KV_IT_READ_BUFFER_META_LEN) {
	iocb->result = KVS_ERR_SYS_IO;
	break;
      }

      // move 4 byte key len
      memmove(current_ptr, data_buff, KV_IT_READ_BUFFER_META_LEN);
      current_ptr += KV_IT_READ_BUFFER_META_LEN;

      // get key size
      key_size = *((uint32_t *)data_buff);
      buffdata_len -= KV_IT_READ_BUFFER_META_LEN;
      data_buff += KV_IT_READ_BUFFER_META_LEN;

      if (key_size > buffdata_len) {
	iocb->result = KVS_ERR_SYS_IO;
	break;
      }
      if (key_size >= 256) {
	iocb->result = KVS_ERR_SYS_IO;
	break;
      }

      // move key data
      memmove(current_ptr, data_buff, key_size);
      current_ptr += key_size;
      
      // calculate 4 byte aligned current key len including padding bytes
      keydata_len_with_padding = (((key_size + 3) >> 2) << 2);

      // skip to start position of next key
      buffdata_len -= keydata_len_with_padding;
      data_buff += keydata_len_with_padding;
    }
  }
  
  ctx->iter_list->it_list = it->kv.value.value;
  ctx->iter_list->size = it->kv.value.length;
  if(ctx->on_complete && iocb) ctx->on_complete(iocb);    
  
  if (ctx) {
    free(ctx);
    ctx = NULL;
  }
  if(it) {
    kv_free(it);
    it = NULL;
  }
}

void udd_write_cb(kv_pair *kv, unsigned int result, unsigned int status) {

  if(status != KV_SUCCESS && status != KV_ERR_NOT_EXIST_KEY ){
    fprintf(stderr, "[%s] error. key=%s option=%d value.length=%d value.offset=%d status code = 0x%x\n", __FUNCTION__, (char*)kv->key.key, kv->param.io_option.store_option,kv->value.length, kv->value.offset, status);
  }
  
  KUDDriver::kv_udd_context *ctx = (KUDDriver::kv_udd_context*)kv->param.private_data;
  //kv_iocb *iocb = &ctx->iocb;
  kvs_callback_context *iocb = &ctx->iocb;
  if(iocb->opcode == IOCB_ASYNC_CHECK_KEY_EXIST_CMD) {
    iocb->result = (kvs_result)result;
    if(status == KV_ERR_NOT_EXIST_KEY)
      status = 0;//KVS_ERR_KEY_NOT_EXIST;
    else if (status == KV_SUCCESS)
      status = 1;
    *iocb->result_buffer = status;
  } else {
    if(status == KV_SUCCESS) {
      iocb->result = KVS_SUCCESS;
    } else if(status == KV_ERR_INVALID_VALUE_SIZE || status == KV_ERR_MAXIMUM_VALUE_SIZE_LIMIT_EXCEEDED) {
      iocb->result = KVS_ERR_VALUE_LENGTH_INVALID;
    } else if (status == KV_ERR_INVALID_VALUE_OFFSET) {
      iocb->result = KVS_ERR_VALUE_OFFSET_INVALID;
    } else if (status == KV_ERR_INVALID_KEY_SIZE) {
      iocb->result = KVS_ERR_KEY_LENGTH_INVALID;
    } else if (status == KV_ERR_MISALIGNED_VALUE_SIZE) {
      iocb->result = KVS_ERR_VALUE_LENGTH_MISALIGNED;
    } else if (status == KV_ERR_MISALIGNED_VALUE_OFFSET) {
      iocb->result = KVS_ERR_VALUE_OFFSET_INVALID;
    } else if (status == KV_ERR_MISALIGNED_KEY_SIZE) {
      iocb->result = KVS_ERR_KEY_LENGTH_INVALID;
    } else if (status == KV_ERR_NOT_EXIST_KEY) {
      iocb->result = KVS_ERR_KEY_NOT_EXIST;
    } else if (status == KV_ERR_CAPACITY_EXCEEDED) {
      iocb->result = KVS_ERR_CONT_CAPACITY;
    } else if (status == KV_ERR_DD_INVALID_PARAM) {
      iocb->result = KVS_ERR_PARAM_INVALID;
    } else if (status == KV_ERR_DD_UNSUPPORTED_CMD || status == KV_ERR_INVALID_OPTION){
      iocb->result = KVS_ERR_OPTION_INVALID;
    } else if (status == KV_ERR_BUFFER) {
      iocb->result = KVS_ERR_BUFFER_SMALL;
    } else if(status == KV_ERR_IDEMPOTENT_STORE_FAIL) {
      iocb->result = KVS_ERR_KEY_EXIST;
    } else {
      fprintf(stderr, "[%s] error. key=%s option=%d value.length=%d value.offset=%d status code = 0x%x\n", __FUNCTION__, (char*)kv->key.key, kv->param.io_option.store_option,kv->value.length, kv->value.offset, status);
      iocb->result = KVS_ERR_SYS_IO;
    }
  }

  if(iocb->key) {
    iocb->key->key = kv->key.key;
    iocb->key->length = kv->key.length;
  }
  if(iocb->value)
    iocb->value->actual_value_size = kv->value.actual_value_size;
  
  if(ctx->on_complete && iocb) ctx->on_complete(iocb);
 
  const auto owner = ctx->owner;
  if (ctx) {
    free(ctx);
    ctx = NULL;
  }
  if (kv) {
    std::unique_lock<std::mutex> lock(owner->lock);
    owner->kv_pair_pool.push(kv);
    lock.unlock();
  }
}

void print_coremask(uint64_t x)
{
  int z;
  char b[65];
  b[64] = '\0';
  for (z = 0; z < 64; z++) {
    b[63-z] = ((x>>z) & 0x1) + '0';
  }
  printf("coremask = %s\n", b);
}
  
int32_t KUDDriver::init(const char* devpath, bool syncio, uint64_t sq_core, uint64_t cq_core, uint32_t mem_size_mb, int queue_depth) {

  int ret;

  kv_nvme_io_options options = {0};
  options.core_mask = (1ULL << sq_core); //sq_core; 
  if (syncio)
    options.sync_mask = 1;     // Use Sync I/O mode
  else
    options.sync_mask = 0;     // Use Async I/O mode
  options.num_cq_threads = 1;  // Use only one CQ Processing Thread
  options.cq_thread_mask = (1ULL << cq_core); //cq_core; 
  options.queue_depth = queue_depth;
  options.mem_size_mb = mem_size_mb; 
  unsigned int ssd_type = KV_TYPE_SSD;

  //kv_env_init(options.mem_size_mb);
  ret = kv_nvme_init(devpath, &options, ssd_type);  
  if(ret) {
    if (ret == KV_ERR_DD_NO_DEVICE) {
      ret = KVS_ERR_DEV_NOT_EXIST;
    } else if (ret == KV_ERR_DD_INVALID_PARAM) {
      ret = KVS_ERR_PARAM_INVALID;
    } else if (ret == KV_ERR_DD_UNSUPPORTED_CMD) {
      ret = KVS_ERR_OPTION_INVALID;
    } else {
      fprintf(stderr, "Failed to open device: %s 0x%x\n", "KVS_ERR_DEV_INIT", ret);
      ret = KVS_ERR_SYS_IO;
    }
    return ret;
  }

  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(0, &cpuset); // CPU 0
  sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);

  memcpy(trid, devpath, 1024);
  handle = kv_nvme_open(devpath);
  if(handle == 0) {
    fprintf(stderr, "Failed to open device: %s", "KVS_ERR_DEV_INIT");
    exit(1);
  } else {
    fprintf(stdout, "Open handle %ld with path %s\n", handle, trid);
  }
  
  for(int i = 0; i < MAX_POOLSIZE; i++) {
    kv_pair *kv = (kv_pair*)kv_zalloc(sizeof(kv_pair));
    if(!kv) {
      fprintf(stderr, "Failed to allocate kv pair\n");
      exit(1);
    }
    this->kv_pair_pool.push(kv);
  }

  return ret;
}


KUDDriver::kv_udd_context* KUDDriver::prep_io_context(int opcode, int contid, const kvs_key *key, const kvs_value *value, void *private1, void *private2, bool syncio, kvs_callback_function cbfn) {

  kv_udd_context *ctx = (kv_udd_context*)calloc(1, sizeof(kv_udd_context));
  
  ctx->on_complete = cbfn;
  ctx->iocb.opcode = opcode;
  if(key) {
    ctx->iocb.key = (kvs_key*)key;
  } else {
    ctx->iocb.key = 0;    
  }

  ctx->iocb.result = KVS_SUCCESS;
  if(value) {
    ctx->iocb.value = (kvs_value*)value;
  } else {
    ctx->iocb.value = 0;
  }

  ctx->iocb.private1 = private1;
  ctx->iocb.private2 = private2;
  ctx->owner = this;
  
  return ctx;
  
}

/* MAIN ENTRY POINT */
int32_t KUDDriver::store_tuple(int contid, const kvs_key *key, const kvs_value *value, kvs_store_option option, void *private1, void *private2, bool syncio, kvs_callback_function cbfn) {
  
  int ret = -EINVAL;

  auto ctx = prep_io_context(IOCB_ASYNC_PUT_CMD, contid, key, value, private1, private2, syncio, cbfn);
  
  std::unique_lock<std::mutex> lock(this->lock);
  kv_pair *kv = this->kv_pair_pool.front();
  this->kv_pair_pool.pop();
  lock.unlock();
  if(!kv) {
    fprintf(stderr, "failed to allocate kv pairs\n");
    exit(1);
  }

  int option_adi;
  if(!option.kvs_store_compress) {
    // Default: no compression
    switch(option.st_type) {
    case KVS_STORE_POST:
      option_adi = KV_STORE_DEFAULT;
      break;
    case KVS_STORE_NOOVERWRITE:
      option_adi = KV_STORE_IDEMPOTENT;
      break;
    case KVS_STORE_APPEND:
    case KVS_STORE_UPDATE_ONLY:
    default:
      fprintf(stderr, "WARN: Wrong store option\n");
      return KVS_ERR_OPTION_INVALID;
    }
  } else {
    // compression
    switch(option.st_type) {
    case KVS_STORE_POST:
      option_adi = KV_STORE_COMPRESSION;
      break;
    case KVS_STORE_UPDATE_ONLY:
    case KVS_STORE_NOOVERWRITE:
    case KVS_STORE_APPEND:
    default:
      fprintf(stderr, "WARN: Wrong store option\n");
      return KVS_ERR_OPTION_INVALID;
    }
  }
  
  kv->key.key = key->key;
  kv->key.length = key->length;
  
  kv->value.value = value->value;
  kv->value.length = value->length;
  kv->value.offset = value->offset;

  kv->param.async_cb = udd_write_cb;
  kv->param.private_data = ctx;
  kv->param.io_option.store_option = option_adi;//KV_STORE_DEFAULT;

  if(syncio) {
    ret = kv_nvme_write(handle, DEFAULT_IO_QUEUE_ID, kv);
    this->kv_pair_pool.push(kv);
    free(ctx);
    ctx = NULL;
    
    if(ret == KV_SUCCESS) {
      ret = KVS_SUCCESS;
    } else if(ret == KV_ERR_INVALID_VALUE_SIZE || ret == KV_ERR_MAXIMUM_VALUE_SIZE_LIMIT_EXCEEDED) {
      ret = KVS_ERR_VALUE_LENGTH_INVALID;
    } else if (ret == KV_ERR_INVALID_VALUE_OFFSET) {
      ret = KVS_ERR_VALUE_OFFSET_INVALID;
    } else if (ret == KV_ERR_INVALID_KEY_SIZE) {
      ret = KVS_ERR_KEY_LENGTH_INVALID;
    } else if (ret == KV_ERR_MISALIGNED_VALUE_SIZE) {
      ret = KVS_ERR_VALUE_LENGTH_MISALIGNED;
    } else if (ret == KV_ERR_MISALIGNED_VALUE_OFFSET) {
      ret = KVS_ERR_VALUE_OFFSET_INVALID;
    } else if (ret == KV_ERR_MISALIGNED_KEY_SIZE) {
      ret = KVS_ERR_KEY_LENGTH_INVALID;
    } else if (ret == KV_ERR_NOT_EXIST_KEY) {
      ret = KVS_ERR_KEY_NOT_EXIST;
    } else if (ret == KV_ERR_CAPACITY_EXCEEDED) {
      ret = KVS_ERR_CONT_CAPACITY;
    } else if (ret == KV_ERR_DD_INVALID_PARAM) {
      ret = KVS_ERR_PARAM_INVALID;
    } else if (ret == KV_ERR_DD_UNSUPPORTED_CMD || ret == KV_ERR_INVALID_OPTION){
      ret = KVS_ERR_OPTION_INVALID;
    } else if (ret == KV_ERR_BUFFER) {
      ret = KVS_ERR_BUFFER_SMALL;
    } else if (ret == KV_ERR_IDEMPOTENT_STORE_FAIL) {
      ret = KVS_ERR_KEY_EXIST;
    } else {
      fprintf(stderr, "[%s] error. key=%s option=%d value.length=%d value.offset=%d status code = 0x%x\n", __FUNCTION__, (char*)key->key, kv->param.io_option.store_option,kv->value.length, kv->value.offset, ret);
      ret = KVS_ERR_SYS_IO;
    }
  } else {
    while (ret) {
      ret = kv_nvme_write_async(handle, DEFAULT_IO_QUEUE_ID, kv);
      if(ret) {
	usleep(1);
      }
      else {
	break;
      }
    }
  }

  return ret;
}

int32_t KUDDriver::retrieve_tuple(int contid, const kvs_key *key, kvs_value *value, kvs_retrieve_option option, void *private1, void *private2, bool syncio, kvs_callback_function cbfn) {

  int ret = -EINVAL;

  auto ctx = prep_io_context(IOCB_ASYNC_GET_CMD, contid, key, value, private1, private2, syncio, cbfn);
  
  std::unique_lock<std::mutex> lock(this->lock);
  kv_pair *kv = this->kv_pair_pool.front();
  this->kv_pair_pool.pop();
  lock.unlock();
  if(!kv) {
    fprintf(stderr, "failed to allocate kv pairs\n");
    exit(1);
  }

  int option_adi;
  if(!option.kvs_retrieve_delete) {
    if(!option.kvs_retrieve_decompress)
      option_adi = KV_RETRIEVE_DEFAULT;
    else
      option_adi = KV_RETRIEVE_DECOMPRESSION;
  } else {
    return KVS_ERR_OPTION_INVALID;
  }
  
  kv->key.key = key->key;
  kv->key.length = key->length;
  
  kv->value.value = value->value;
  kv->value.length = value->length;
  kv->value.offset = value->offset;
  
  kv->param.io_option.retrieve_option = option_adi;
  kv->param.async_cb = udd_write_cb;
  kv->param.private_data = ctx;

  if(syncio) {
    ret = kv_nvme_read(handle, DEFAULT_IO_QUEUE_ID, kv);
    value->actual_value_size = kv->value.actual_value_size;
    this->kv_pair_pool.push(kv);
    free(ctx);
    ctx = NULL;

    if(ret == KV_SUCCESS) {
      ret = KVS_SUCCESS;
    } else if(ret == KV_ERR_INVALID_VALUE_SIZE || ret == KV_ERR_MAXIMUM_VALUE_SIZE_LIMIT_EXCEEDED) {
      ret = KVS_ERR_VALUE_LENGTH_INVALID;
    } else if (ret == KV_ERR_INVALID_VALUE_OFFSET) {
      ret = KVS_ERR_VALUE_OFFSET_INVALID;
    } else if (ret == KV_ERR_INVALID_KEY_SIZE) {
      ret = KVS_ERR_KEY_LENGTH_INVALID;
    } else if (ret == KV_ERR_MISALIGNED_VALUE_SIZE) {
      ret = KVS_ERR_VALUE_LENGTH_MISALIGNED;
    } else if (ret == KV_ERR_MISALIGNED_VALUE_OFFSET) {
      ret = KVS_ERR_VALUE_OFFSET_INVALID;
    } else if (ret == KV_ERR_MISALIGNED_KEY_SIZE) {
      ret = KVS_ERR_KEY_LENGTH_INVALID;
    } else if (ret == KV_ERR_NOT_EXIST_KEY) {
      ret = KVS_ERR_KEY_NOT_EXIST;
    } else if (ret == KV_ERR_CAPACITY_EXCEEDED) {
      ret = KVS_ERR_CONT_CAPACITY;
    } else if (ret == KV_ERR_DD_INVALID_PARAM) {
      ret = KVS_ERR_PARAM_INVALID;
    } else if (ret == KV_ERR_DD_UNSUPPORTED_CMD || ret == KV_ERR_INVALID_OPTION){
      ret = KVS_ERR_OPTION_INVALID;
    } else if (ret == KV_ERR_BUFFER) {
      ret = KVS_ERR_BUFFER_SMALL;
    } else {
      fprintf(stderr, "[%s] error. key=%s option=%d value.length=%d value.offset=%d status code = 0x%x\n", __FUNCTION__, (char*)key->key, kv->param.io_option.store_option,kv->value.length, kv->value.offset, ret);
      ret = KVS_ERR_SYS_IO;
    }
    
  } else {
    while (ret) {
      ret = kv_nvme_read_async(handle, DEFAULT_IO_QUEUE_ID, kv);
      if(ret) {
	usleep(1);
      } else {
	break;
      }
    }
  }
  
  return ret;
}

int32_t KUDDriver::delete_tuple(int contid, const kvs_key *key, kvs_delete_option option, void *private1, void *private2, bool syncio, kvs_callback_function cbfn) {

  int ret = -EINVAL;
  auto ctx = prep_io_context(IOCB_ASYNC_DEL_CMD, contid, key, NULL, private1, private2, syncio, cbfn);

  std::unique_lock<std::mutex> lock(this->lock);
  kv_pair *kv = this->kv_pair_pool.front();
  this->kv_pair_pool.pop();
  lock.unlock();
  if(!kv) {
    fprintf(stderr, "failed to allocate kv pairs\n");
    exit(1);
  }

  int option_adi;
  if(!option.kvs_delete_error)
    option_adi = KV_DELETE_DEFAULT;
  else
    option_adi = KV_DELETE_CHECK_IDEMPOTENT;
  
  kv->key.key = key->key;
  kv->key.length = key->length;
  kv->value.value = 0;

  kv->param.io_option.delete_option = option_adi;
  kv->param.async_cb = udd_write_cb;
  kv->param.private_data = ctx;

  if(syncio){
    ret = kv_nvme_delete(handle, DEFAULT_IO_QUEUE_ID, kv);
    this->kv_pair_pool.push(kv);
    free(ctx);
    ctx = NULL;

    if(ret == KV_SUCCESS) {
      ret = KVS_SUCCESS;
    } else if(ret == KV_ERR_INVALID_VALUE_SIZE || ret == KV_ERR_MAXIMUM_VALUE_SIZE_LIMIT_EXCEEDED) {
      ret = KVS_ERR_VALUE_LENGTH_INVALID;
    } else if (ret == KV_ERR_INVALID_VALUE_OFFSET) {
      ret = KVS_ERR_VALUE_OFFSET_INVALID;
    } else if (ret == KV_ERR_INVALID_KEY_SIZE) {
      ret = KVS_ERR_KEY_LENGTH_INVALID;
    } else if (ret == KV_ERR_MISALIGNED_VALUE_SIZE) {
      ret = KVS_ERR_VALUE_LENGTH_MISALIGNED;
    } else if (ret == KV_ERR_MISALIGNED_VALUE_OFFSET) {
      ret = KVS_ERR_VALUE_OFFSET_INVALID;
    } else if (ret == KV_ERR_MISALIGNED_KEY_SIZE) {
      ret = KVS_ERR_KEY_LENGTH_INVALID;
    } else if (ret == KV_ERR_NOT_EXIST_KEY) {
      ret = KVS_ERR_KEY_NOT_EXIST;
    } else if (ret == KV_ERR_CAPACITY_EXCEEDED) {
      ret = KVS_ERR_CONT_CAPACITY;
    } else if (ret == KV_ERR_DD_INVALID_PARAM) {
      ret = KVS_ERR_PARAM_INVALID;
    } else if (ret == KV_ERR_DD_UNSUPPORTED_CMD || ret == KV_ERR_INVALID_OPTION){
      ret = KVS_ERR_OPTION_INVALID;
    } else if (ret == KV_ERR_BUFFER) {
      ret = KVS_ERR_BUFFER_SMALL;
    } else {
      fprintf(stderr, "[%s] error. key=%s option=%d value.length=%d value.offset=%d status code = 0x%x\n", __FUNCTION__, (char*)key->key, kv->param.io_option.store_option,kv->value.length, kv->value.offset, ret);
      ret = KVS_ERR_SYS_IO;
    }
    
  } else {
    while(ret){
      ret = kv_nvme_delete_async(handle, DEFAULT_IO_QUEUE_ID, kv);
      if(ret){
	usleep(1);
      } else {
	break;
      }
    }	
  }
  
  return ret;
}

int32_t KUDDriver::exist_tuple(int contid, uint32_t key_cnt, const kvs_key *keys, uint32_t buffer_size, uint8_t *result_buffer, void *private1, void *private2, bool syncio, kvs_callback_function cbfn ) {

  int ret;
  auto ctx = prep_io_context(IOCB_ASYNC_CHECK_KEY_EXIST_CMD, contid, keys, NULL, private1, private2, syncio, cbfn);
  ctx->iocb.result_buffer = result_buffer;
  
  std::unique_lock<std::mutex> lock(this->lock);
  kv_pair *kv = this->kv_pair_pool.front();
  this->kv_pair_pool.pop();
  lock.unlock();
  if(!kv) {
    fprintf(stderr, "failed to allocate kv pairs\n");
    exit(1);
  }
											       
  kv->key.key = keys->key;
  kv->key.length = keys->length;

  kv->param.io_option.exist_option = KV_EXIST_DEFAULT;
  kv->param.async_cb = udd_write_cb;
  kv->param.private_data = ctx;

  if(syncio) {
    ret = kv_nvme_exist(handle, DEFAULT_IO_QUEUE_ID, kv);
    if(ret == KV_SUCCESS) {
      *result_buffer = 1;//ret;
      ret = KVS_SUCCESS;
    } else if(ret == KV_ERR_INVALID_VALUE_SIZE || ret == KV_ERR_MAXIMUM_VALUE_SIZE_LIMIT_EXCEEDED) {
       *result_buffer = ret = KVS_ERR_VALUE_LENGTH_INVALID;
    } else if (ret == KV_ERR_INVALID_VALUE_OFFSET) {
       *result_buffer = ret = KVS_ERR_VALUE_OFFSET_INVALID;
    } else if (ret == KV_ERR_INVALID_KEY_SIZE) {
       *result_buffer = ret = KVS_ERR_KEY_LENGTH_INVALID;
    } else if (ret == KV_ERR_MISALIGNED_VALUE_SIZE) {
       *result_buffer = ret = KVS_ERR_VALUE_LENGTH_MISALIGNED;
    } else if (ret == KV_ERR_MISALIGNED_VALUE_OFFSET) {
       *result_buffer = ret = KVS_ERR_VALUE_OFFSET_INVALID;
    } else if (ret == KV_ERR_MISALIGNED_KEY_SIZE) {
       *result_buffer = ret = KVS_ERR_KEY_LENGTH_INVALID;
    } else if (ret == KV_ERR_NOT_EXIST_KEY) {
      *result_buffer = 0;//KVS_ERR_KEY_NOT_EXIST;
      ret = KVS_SUCCESS;
    } else if (ret == KV_ERR_CAPACITY_EXCEEDED) {
       *result_buffer = ret = KVS_ERR_CONT_CAPACITY;
    } else if (ret == KV_ERR_DD_INVALID_PARAM) {
       *result_buffer = ret = KVS_ERR_PARAM_INVALID;
    } else if (ret == KV_ERR_DD_UNSUPPORTED_CMD || ret == KV_ERR_INVALID_OPTION){
       *result_buffer = KVS_ERR_OPTION_INVALID;
    } else if (ret == KV_ERR_BUFFER) {
       *result_buffer = ret = KVS_ERR_BUFFER_SMALL;
    } else {
      fprintf(stderr, "[%s] error. key=%s option=%d value.length=%d value.offset=%d status code = 0x%x\n", __FUNCTION__, (char*)keys->key, kv->param.io_option.store_option,kv->value.length, kv->value.offset, ret);
       *result_buffer = ret = KVS_ERR_SYS_IO;
    }
    
    this->kv_pair_pool.push(kv);
    free(ctx);
    ctx = NULL;    
  } else {
    ret = kv_nvme_exist_async(handle, DEFAULT_IO_QUEUE_ID, kv);
    if(ret == KV_ERR_NOT_EXIST_KEY) {
      *result_buffer = KVS_ERR_KEY_NOT_EXIST;
      ret = KVS_SUCCESS;
    }
    else
      *result_buffer = ret;
  }

  return ret;
}

int32_t KUDDriver::open_iterator(int contid,  /*uint8_t option*/kvs_iterator_option option, uint32_t bitmask,
				 uint32_t bit_pattern, kvs_iterator_handle *iter_hd) {
  
  int ret = 0;

  uint8_t option_udd;

  if(option.iter_type == KVS_ITERATOR_KEY)
    option_udd = KV_KEY_ITERATE;
  else
    option_udd = KV_KEY_ITERATE_WITH_RETRIEVE;
  
  int nr_iterate_handle = KV_MAX_ITERATE_HANDLE;
  int opened = 0;
  kv_iterate_handle_info info[KV_MAX_ITERATE_HANDLE];
  ret = kv_nvme_iterate_info(handle, info, nr_iterate_handle);
  if (ret == KV_SUCCESS) {
    for(int i=0;i<nr_iterate_handle;i++){
      if(info[i].status == ITERATE_HANDLE_OPENED){
	opened++;
	if(info[i].bitmask == bitmask && info[i].prefix == bit_pattern) {	
	  //kv_nvme_iterate_close(handle, info[i].handle_id);
	  fprintf(stdout, "WARN: Iterator with same prefix/bitmask is already opened\n");
	  return KVS_ERR_ITERATOR_OPEN;
	}
      } else {
	//fprintf(stdout, "iterate %d is closed\n", i);
      }
    }
  }

  if(opened == KV_MAX_ITERATE_HANDLE)
    return KVS_ERR_ITERATOR_MAX;
  
  uint32_t iterator = KV_INVALID_ITERATE_HANDLE;
  iterator = kv_nvme_iterate_open(handle, KV_KEYSPACE_IODATA, bitmask, bit_pattern, /*(option == KVS_ITERATOR_OPT_KEY ? KV_KEY_ITERATE : KV_KEY_ITERATE_WITH_RETRIEVE)*/option_udd);

  if(iterator != KV_INVALID_ITERATE_HANDLE){
    if(iterator == KV_ERR_ITERATE_HANDLE_ALREADY_OPENED)
      ret = KVS_ERR_ITERATOR_OPEN;
    else if (iterator == KV_ERR_ITERATE_NO_AVAILABLE_HANDLE)
      ret = KVS_ERR_ITERATOR_MAX;
    else {
      fprintf(stdout, "Iterate_Open Success: iterator id=0x%x\n", iterator);
      ret = 0;
    }
    //kvs_iterator_handle iterh = (kvs_iterator_handle)malloc(sizeof(struct _kvs_iterator_handle));
    //iterh->iterator = iterator;
    //*iter_hd = iterh;
    *iter_hd = iterator;
  } else
    ret = KVS_ERR_ITERATE_REQUEST_FAIL;
  
  return ret;
}

int32_t KUDDriver::close_iterator(int contid, kvs_iterator_handle hiter) {

  int ret = 0;
  //if(hiter->iterator > 0)
  if(hiter > 0)
    ret = kv_nvme_iterate_close(handle, hiter/*->iterator*/);

  if(ret != KV_SUCCESS) {
    if(ret == KV_ERR_ITERATE_FAIL_TO_PROCESS_REQUEST) {
      ret = KVS_ERR_ITERATOR_NOT_EXIST;
    } else
      ret = KVS_ERR_SYS_IO;
  }
  //if(hiter) free(hiter);
  
  return ret;
}


int32_t KUDDriver::close_iterator_all(int contid) {
  int ret;
  int nr_iterate_handle = KV_MAX_ITERATE_HANDLE;
  kv_iterate_handle_info info[KV_MAX_ITERATE_HANDLE];
  ret = kv_nvme_iterate_info(handle, info, nr_iterate_handle);
  if (ret == KV_SUCCESS) {
    for(int i=0;i<nr_iterate_handle;i++){
      if(info[i].status == ITERATE_HANDLE_OPENED){
	kv_nvme_iterate_close(handle, info[i].handle_id);
	fprintf(stdout, "Close itertor %d\n", info[i].handle_id);
      }
    }
  }
  return KVS_SUCCESS;
}

int32_t KUDDriver::list_iterators(int contid, kvs_iterator_info *kvs_iters, uint32_t count) {

  //int nr_iterate_handle = KV_MAX_ITERATE_HANDLE;
  //kv_iterate_handle_info info[KV_MAX_ITERATE_HANDLE];
  
  int ret = kv_nvme_iterate_info(handle, (kv_iterate_handle_info*)kvs_iters, count);

  if(ret == KV_ERR_DD_INVALID_PARAM)
    ret = KVS_ERR_PARAM_INVALID;
  
  return ret;
}

int32_t KUDDriver::iterator_next(kvs_iterator_handle hiter, kvs_iterator_list *iter_list, void *private1, void *private2, bool syncio, kvs_callback_function cbfn) {

  int ret = -EINVAL;

  auto ctx = prep_io_context(IOCB_ASYNC_ITER_NEXT_CMD, 0, 0, 0, private1, private2, syncio, cbfn);

  ctx->iter_list = iter_list;

  kv_iterate *it = (kv_iterate *)kv_zalloc(sizeof(kv_iterate)); 
  if(!it) {
    return -ENOMEM;
  }

  it->iterator = hiter;//hiter->iterator;
  it->kv.key.length = 0;
  it->kv.key.key = NULL;
  it->kv.value.value = iter_list->it_list;
  it->kv.value.length = iter_list->size;
  it->kv.value.offset = 0;

  it->kv.param.async_cb = udd_iterate_cb;
  it->kv.param.private_data = ctx;
  it->kv.param.io_option.iterate_read_option = KV_ITERATE_READ_DEFAULT;
  if (syncio) {
    ret = kv_nvme_iterate_read(handle, DEFAULT_IO_QUEUE_ID, it);

    if(ret != KV_SUCCESS) {
      if(ret == KV_ERR_ITERATE_READ_EOF  /*KVS_ERR_ITERATOR_END*/) {
	iter_list->end = 0x01;//TRUE;
	ret = 0;
      } else if (ret == KV_ERR_ITERATE_FAIL_TO_PROCESS_REQUEST) {
	ret = KVS_ERR_ITERATOR_NOT_EXIST;
      } else if (ret == KV_ERR_BUFFER) {
	ret = KVS_ERR_BUFFER_SMALL;
      } else if (ret == KV_ERR_INVALID_OPTION) {
	ret = KVS_ERR_OPTION_INVALID;
      } else 
	ret = KVS_ERR_SYS_IO;
    }


    if(ret == KV_SUCCESS) {

      // first 4 bytes are for key counts
      uint32_t num_key = *((unsigned int*)it->kv.value.value);
      iter_list->num_entries = num_key;

      char *data_buff = (char *)it->kv.value.value;
      unsigned int buffer_size = it->kv.value.length;
      char *current_ptr = data_buff;

      unsigned int key_size = 0;
      int keydata_len_with_padding = 0;
      unsigned int buffdata_len = buffer_size;

      buffdata_len -= KV_IT_READ_BUFFER_META_LEN;
      data_buff += KV_IT_READ_BUFFER_META_LEN;
      for (uint32_t i = 0; i < num_key && buffdata_len > 0; i++) {
	if (buffdata_len < KV_IT_READ_BUFFER_META_LEN) {
	  ret = KVS_ERR_SYS_IO;
	  break;
	}

	// move 4 byte key len
	memmove(current_ptr, data_buff, KV_IT_READ_BUFFER_META_LEN);
	current_ptr += KV_IT_READ_BUFFER_META_LEN;

	// get key size
	key_size = *((uint32_t *)data_buff);
	buffdata_len -= KV_IT_READ_BUFFER_META_LEN;
	data_buff += KV_IT_READ_BUFFER_META_LEN;

	if (key_size > buffdata_len) {
	  ret = KVS_ERR_SYS_IO;
	  break;
	}
	if (key_size >= 256) {
	  ret = KVS_ERR_SYS_IO;
	  break;
	}

	// move key data
	memmove(current_ptr, data_buff, key_size);
	current_ptr += key_size;

	// calculate 4 byte aligned current key len including padding bytes
	keydata_len_with_padding = (((key_size + 3) >> 2) << 2);

	// skip to start position of next key
	buffdata_len -= keydata_len_with_padding;
	data_buff += keydata_len_with_padding;
      }
    }
    
    /*

    
    uint32_t num_key = *((unsigned int*)it->kv.value.value);
    iter_list->num_entries = num_key;

    // move forward 4B for numkey field
    memcpy((char*)it->kv.value.value, (char*)it->kv.value.value + 4,  it->kv.value.length - 4);
    */
    iter_list->it_list = it->kv.value.value;
    iter_list->size = it->kv.value.length;
    
    if (it) {
      kv_free(it);
      it = NULL;
    }
    if(ctx) {
      free(ctx);
      ctx = NULL;
    } 
  } else { // async
    while(ret) {
      ret = kv_nvme_iterate_read_async(handle, DEFAULT_IO_QUEUE_ID, it);
      if(ret) {
	usleep(1);
      } else {
	break;
      }
    }
  }
  
  return ret;
}  

float KUDDriver::get_waf(){

  return (float)kv_nvme_get_waf(handle) / 10;
}

int32_t KUDDriver::get_device_info(kvs_device *dev_info) {
  return 0;
}

int32_t KUDDriver::get_used_size(int32_t *dev_util){
  
  *dev_util = kv_nvme_get_used_size(handle);
  return 0;
}

int32_t KUDDriver::get_total_size(int64_t *dev_capa){

  *dev_capa = kv_nvme_get_total_size(handle);
  return 0;
}

int32_t KUDDriver::process_completions(int max)
{

  return 0;
}


KUDDriver::~KUDDriver() {
  int ret;

  ret = kv_nvme_close(handle);
  if(ret){
    fprintf(stderr, "Failed to close nvme, ret %d\n", ret);
  }
  ret = kv_nvme_finalize(trid);

  while(!this->kv_pair_pool.empty()) {
    auto p = this->kv_pair_pool.front();
    this->kv_pair_pool.pop();
    kv_free(p);
  }

  /*
  while(!this->udd_context_pool.empty()){
    auto p = this->udd_context_pool.front();
    this->udd_context_pool.pop();
    delete p;
  }
  */
}

