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

#define MAX_POOLSIZE 1024
#define GB_SIZE (1024 * 1024 * 1024)

KUDDriver::KUDDriver(kv_device_priv *dev,_on_iocomplete user_io_complete_):
  KvsDriver(dev, user_io_complete_), queue_depth(256), num_cq_threads(1), mem_size_mb(1024)
{

  fprintf(stdout, "init udd\n");
}


void udd_iterate_cb(kv_iterate *it, unsigned int result, unsigned int status) {
  KUDDriver::kv_udd_context *ctx = (KUDDriver::kv_udd_context*)it->kv.param.private_data;
  kv_iocb *iocb = &ctx->iocb;
  iocb->result = status;
  if(status != KV_SUCCESS){
    if(status ==  0x93 /*KV_ERR_ITERATE_READ_EOF*/){ //TODO: fix this with SNIA
      //fprintf(stderr, "[%s] EOF result=%d status=%d length=%d\n", __FUNCTION__, result, status, it->kv.value.length);
      iocb->result = KVS_ERR_ITERATOR_END;
      ctx->iter_list->end = TRUE;
    }else {
      fprintf(stderr, "[%s] error. result=%d status=%d\n", __FUNCTION__, result, status);
    }
  } 

  iocb->value = it->kv.value.value;
  iocb->valuesize = it->kv.value.length;
  ctx->iter_list->it_list = it->kv.value.value;
  ctx->iter_list->num_entries = it->kv.value.length / G_ITER_KEY_SIZE_FIXED;
  ctx->iter_list->size = it->kv.value.length;
  if(ctx->on_complete && iocb) ctx->on_complete(iocb);

  if (ctx) {
    free(ctx);
    ctx = NULL;
  }
  if(it) {
    free(it);
    it = NULL;
  }
}


void udd_write_cb(kv_pair *kv, unsigned int result, unsigned int status) {

  if(status != KV_SUCCESS && status != KV_ERR_KEY_NOT_EXIST /*use adi's err code macro*/ ){
    fprintf(stderr, "[%s] error. key=%s option=%d value.length=%d value.offset=%d status code = 0x%x\n", __FUNCTION__, (char*)kv->key.key, kv->param.io_option.store_option,kv->value.length, kv->value.offset, status);
    //exit(1);
  }

  KUDDriver::kv_udd_context *ctx = (KUDDriver::kv_udd_context*)kv->param.private_data;
  kv_iocb *iocb = &ctx->iocb;
  iocb->result = status;
  iocb->key = kv->key.key;
  iocb->keysize = kv->key.length;

  if(ctx->on_complete && iocb) ctx->on_complete(iocb);
  else
    fprintf(stdout, " no mem %p %p\n", ctx->on_complete, iocb);

  const auto owner = ctx->owner;
  if (ctx) {
    //owner->udd_context_pool.push(ctx);
    free(ctx);
    ctx = NULL;
  }
  if (kv) {
    std::unique_lock<std::mutex> lock(owner->lock);
    owner->kv_pair_pool.push(kv);
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
  
int32_t KUDDriver::init(const char* devpath, bool syncio, uint64_t sq_core, uint64_t cq_core, uint32_t mem_size_mb) {

  int ret;

  kv_nvme_io_options options = {0};
  options.core_mask = (1ULL << sq_core); //sq_core; 
  if (syncio)
    options.sync_mask = 1;     // Use Sync I/O mode
  else
    options.sync_mask = 0;     // Use Async I/O mode
  options.num_cq_threads = 1;  // Use only one CQ Processing Thread
  options.cq_thread_mask = (1ULL << cq_core); //cq_core; 
  options.queue_depth = 256; // MAX QD
  options.mem_size_mb = mem_size_mb; 
  unsigned int ssd_type = KV_TYPE_SSD;

  ret = kv_nvme_init(devpath, &options, ssd_type);  
  if(ret) {
    fprintf(stderr, "Failed to do kv_nvme_init 0x%x\n", ret);
  }

  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(0, &cpuset); // CPU 0
  sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);

  memcpy(trid, devpath, 1024);
  handle = kv_nvme_open(devpath);
  fprintf(stdout, "Open handle %ld with path %s\n", handle, trid);

  
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


KUDDriver::kv_udd_context* KUDDriver::prep_io_context(int opcode, int contid, const kvs_key *key, const kvs_value *value, uint8_t option, void *private1, void *private2, bool syncio) {

  validate_request(key, value);
  kv_udd_context *ctx = (kv_udd_context*)calloc(1, sizeof(kv_udd_context));
  
  ctx->on_complete = this->user_io_complete;
  ctx->iocb.opcode = opcode;
  if(key) {
    ctx->iocb.key = key->key;
    ctx->iocb.keysize = key->length;
  } else {
    ctx->iocb.key = 0;
    ctx->iocb.keysize = 0;
  }
  ctx->iocb.option = option;
  ctx->iocb.result = 0;
  if(value) {
    ctx->iocb.value = value->value;
    ctx->iocb.valuesize = value->length;
    ctx->iocb.value_offset = value->offset;
  } else {
    ctx->iocb.value = 0;
    ctx->iocb.valuesize = 0;
    ctx->iocb.value_offset = 0;
  }

  ctx->iocb.private1 = private1;
  ctx->iocb.private2 = private2;
  ctx->owner = this;
  
  return ctx;
  
}

  
/* MAIN ENTRY POINT */
int32_t KUDDriver::store_tuple(int contid, const kvs_key *key, const kvs_value *value, uint8_t option, void *private1, void *private2, bool syncio) {
  
  int ret = -EINVAL;

  auto ctx = prep_io_context(IOCB_ASYNC_PUT_CMD, contid, key, value, option, private1, private2, syncio);
  
  std::unique_lock<std::mutex> lock(this->lock);
  kv_pair *kv = this->kv_pair_pool.front();
  this->kv_pair_pool.pop();
  if(!kv) {
    fprintf(stderr, "failed to allocate kv pairs\n");
    exit(1);
  }
  
  kv->key.key = key->key;
  kv->key.length = key->length;
  
  kv->value.value = value->value;
  kv->value.length = value->length;
  kv->value.offset = 0;

  kv->param.async_cb = udd_write_cb;
  kv->param.private_data = ctx;
  kv->param.io_option.store_option = 0x00; // TODO: change this option

  if(syncio) {
    ret = kv_nvme_write(handle, kv);
    this->kv_pair_pool.push(kv);
    free(ctx);
    ctx = NULL;
  } else {
    while (ret) {
      ret = kv_nvme_write_async(handle, kv);
      if(ret)
	usleep(1);
      else {
	break;
      }
    }
  }

  return ret;
}

int32_t KUDDriver::retrieve_tuple(int contid, const kvs_key *key, kvs_value *value, uint8_t option, void *private1, void *private2, bool syncio) {

  int ret = -EINVAL;

  auto ctx = prep_io_context(IOCB_ASYNC_GET_CMD, contid, key, value, option, private1, private2, syncio);
  
  std::unique_lock<std::mutex> lock(this->lock);
  kv_pair *kv = this->kv_pair_pool.front();
  this->kv_pair_pool.pop();
  if(!kv) {
    fprintf(stderr, "failed to allocate kv pairs\n");
    exit(1);
  }
  
  kv->key.key = key->key;
  kv->key.length = key->length;
  
  kv->value.value = value->value;
  kv->value.length = value->length;
  kv->value.offset = 0;
  
  kv->param.io_option.retrieve_option = 0x00; // TODO: change this option
  kv->param.async_cb = udd_write_cb;
  kv->param.private_data = ctx;


  if(syncio) {
    ret = kv_nvme_read(handle, kv);
    this->kv_pair_pool.push(kv);
    free(ctx);
    ctx = NULL;
  } else {
    while (ret) {
      ret = kv_nvme_read_async(handle, kv);
      if(ret) {
	usleep(1);
      } else {
	break;
      }
    }
  }
  
  return ret;
}

int32_t KUDDriver::delete_tuple(int contid, const kvs_key *key, uint8_t option, void *private1, void *private2, bool syncio) {

  int ret = -EINVAL;
  auto ctx = prep_io_context(IOCB_ASYNC_DEL_CMD, contid, key, NULL, option, private1, private2, syncio);

  std::unique_lock<std::mutex> lock(this->lock);
  kv_pair *kv = this->kv_pair_pool.front();
  this->kv_pair_pool.pop();
  if(!kv) {
    fprintf(stderr, "failed to allocate kv pairs\n");
    exit(1);
  }

  kv->key.key = key->key;
  kv->key.length = key->length;
  kv->value.value = 0;

  kv->param.io_option.delete_option = 0x00; // TODO: change this option
  kv->param.async_cb = udd_write_cb;
  kv->param.private_data = ctx;

  if(syncio){
    ret = kv_nvme_delete(handle, kv);
    this->kv_pair_pool.push(kv);
    free(ctx);
    ctx = NULL;
  } else {
    while(ret){
      ret = kv_nvme_delete_async(handle, kv);
      if(ret){
	usleep(1);
      } else {
	break;
      }
    }	
  }
  
  return ret;
}

int32_t KUDDriver::open_iterator(int contid,  uint8_t option, uint32_t bitmask,
				 uint32_t bit_pattern, kvs_iterator_handle *iter_hd) {
  
  int ret;
  if(option == KVS_ITERATOR_OPT_KEY) fprintf(stdout, "key only\n");
  else if (option == KVS_ITERATOR_OPT_KV) fprintf(stdout, "key and value\n");
  else fprintf(stdout, "wrong iterator option\n");
 
  int nr_iterate_handle = KV_MAX_ITERATE_HANDLE;
  kv_iterate_handle_info info[KV_MAX_ITERATE_HANDLE];
  ret = kv_nvme_iterate_info(handle, info, nr_iterate_handle);
  if (ret == KV_SUCCESS) {
    for(int i=0;i<nr_iterate_handle;i++){
      if(info[i].status == ITERATE_HANDLE_OPENED){
	//fprintf(stderr, "close iterate_handle : %d\n", info[i].handle_id);
	kv_nvme_iterate_close(handle, info[i].handle_id);
      } else {
	//fprintf(stdout, "iterate %d is closed\n", i);
      }
    }
  }

  uint32_t iterator = KV_INVALID_ITERATE_HANDLE;
  iterator = kv_nvme_iterate_open(handle, bitmask, bit_pattern, (option == KVS_ITERATOR_OPT_KEY ? KV_KEY_ITERATE : KV_KEY_ITERATE_WITH_RETRIEVE));

  if(iterator != KV_INVALID_ITERATE_HANDLE){
    fprintf(stdout, "Iterate_Open Success: iterator id=%d\n", iterator);
    kvs_iterator_handle iterh = (kvs_iterator_handle)malloc(sizeof(struct _kvs_iterator_handle));
    iterh->iterator = iterator;
    *iter_hd = iterh;
  }
  return ret;
}

int32_t KUDDriver::close_iterator(int contid, kvs_iterator_handle hiter) {

  int ret = 0;
  if(hiter->iterator > 0)
    ret = kv_nvme_iterate_close(handle, hiter->iterator);

  if(hiter) free(hiter);
  
  return ret;
}

int32_t KUDDriver::iterator_next(kvs_iterator_handle hiter, kvs_iterator_list *iter_list, void *private1, void *private2, bool syncio) {

  int ret = -EINVAL;

  auto ctx = prep_io_context(IOCB_ASYNC_ITER_NEXT_CMD, 0, 0, 0, 0, private1, private2, syncio);
  ctx->iocb.value = iter_list->it_list;
  ctx->iocb.valuesize = iter_list->size;
  ctx->iocb.value_offset = 0;
  ctx->iter_list = iter_list;
  
  kv_iterate *it = (kv_iterate *)malloc(sizeof(kv_iterate)); 
  if(!it) {
    return -ENOMEM;
  }

  it->iterator = hiter->iterator;
  it->kv.key.length = 0;
  it->kv.key.key = NULL;
  it->kv.value.value = iter_list->it_list;
  it->kv.value.length = iter_list->size;
  it->kv.value.offset = 0;

  it->kv.param.async_cb = udd_iterate_cb;
  it->kv.param.private_data = ctx;
  it->kv.param.io_option.iterate_read_option =  0x00;//KV_ITERATE_READ_DEFAULT;
  if (syncio) {
    ret = kv_nvme_iterate_read(handle, it);
    if(ret == KVS_ERR_ITERATOR_END)
      iter_list->end = TRUE;
    iter_list->num_entries = it->kv.value.length / G_ITER_KEY_SIZE_FIXED;
    iter_list->it_list = it->kv.value.value;
    iter_list->size = it->kv.value.length;
    
    if (it) {
      free(it);
      it = NULL;
    }
    if(ctx) {
      free(ctx);
      ctx = NULL;
    } 
  } else { // async
    while(ret) {
      ret = kv_nvme_iterate_read_async(handle, it);
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

int32_t KUDDriver::get_used_size(){
  
  return kv_nvme_get_used_size(handle);
}

int64_t KUDDriver::get_total_size(){

  return kv_nvme_get_total_size(handle);
}

int32_t KUDDriver::process_completions(int max)
{

  return 0;
}


KUDDriver::~KUDDriver() {
  int ret;
  //fprintf(stdout, "kv close device %s, handle %ld\n", trid, handle);
  ret = kv_nvme_close(handle);
  if(ret){
    fprintf(stderr, "Failed to close nvme, ret %d\n", ret);
  }
  ret = kv_nvme_finalize(trid);
  //fprintf(stdout, "kv finalize done - %s\n", trid);

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

