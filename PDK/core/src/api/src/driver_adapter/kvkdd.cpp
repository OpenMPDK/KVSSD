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

#include "kvs_utils.h"
#include "kvkdd.hpp"
#include <algorithm>
#include <atomic>
#include <tbb/concurrent_queue.h>
#include <list>
#include <kvs_adi.h>

#define MAX_POOLSIZE 10240
//#define use_pool

KDDriver::KDDriver(kv_device_priv *dev,_on_iocomplete user_io_complete_):
  KvsDriver(dev, user_io_complete_), devH(0),nsH(0), sqH(0), cqH(0), int_handler(0)
{
  queuedepth = 64 * 2;
}


// this function will be called after completion of a command
void interrupt_func(void *data, int number) {
  (void) data;
  (void) number;

  //fprintf(stdout, "inside interrupt\n");
}

void kdd_on_io_complete(kv_io_context *context){

  //const char *cmd = (context->opcode == KV_OPC_GET)? "GET": ((context->opcode == KV_OPC_STORE)? "PUT": (context->opcode == KV_OPC_DELETE)? "DEL":"OTHER");

  if((context->retcode != KV_SUCCESS) && (context->retcode != KV_ERR_KEY_NOT_EXIST) && (context->retcode != KV_ERR_ITERATOR_END)) {
    const char *cmd = (context->opcode == KV_OPC_GET)? "GET": ((context->opcode == KV_OPC_STORE)? "PUT": (context->opcode == KV_OPC_DELETE)? "DEL":"OTHER");
    fprintf(stderr, "%s failed with error 0x%x\n", cmd, context->retcode);
    //exit(1);
  }
  
  KDDriver::kv_kdd_context *ctx = (KDDriver::kv_kdd_context*)context->private_data;

  kv_iocb *iocb = &ctx->iocb;
  const auto owner = ctx->owner;
  
  iocb->result = context->retcode;

  if(context->opcode == KV_OPC_OPEN_ITERATOR) {
    kvs_iterator_handle iterh = (kvs_iterator_handle)iocb->private1;
    iterh->iterh_adi = context->result.hiter;
    if(owner->int_handler != 0) {
      owner->done = 1;
      owner->done_cond.notify_one();
    }
  }

  if(context->opcode == KV_OPC_CLOSE_ITERATOR) {
    if(owner->int_handler != 0) {
      owner->done = 1;
      owner->done_cond.notify_one();
    }
  }

  if(context->opcode != KV_OPC_OPEN_ITERATOR && context->opcode != KV_OPC_CLOSE_ITERATOR) {
    if(ctx->on_complete && iocb)
      ctx->on_complete(iocb);
  }
#if defined use_pool
  //const auto owner = ctx->owner;
    
  std::unique_lock<std::mutex> lock(owner->lock);
  if(ctx->key) {
    owner->kv_key_pool.push(ctx->key);
  }
  if(ctx->value){
    owner->kv_value_pool.push(ctx->value);
  }

  if(ctx->free_ctx) {
    memset(ctx, 0, sizeof(KDDriver::kv_kdd_context));
    owner->kv_ctx_pool.push(ctx);
  }
  lock.unlock();
#else
  if(ctx->free_ctx) {
    free(ctx);
    ctx = NULL;
  }
#endif
}

int KDDriver::create_queue(int qdepth, uint16_t qtype, kv_queue_handle *handle, int cqid, int is_polling){
  
  static int qid = -1;
  kv_queue qinfo;
  qinfo.queue_id = ++qid;
  qinfo.queue_size = qdepth;
  qinfo.completion_queue_id = cqid;
  qinfo.queue_type = qtype;
  qinfo.extended_info = NULL;
  kv_result ret = kv_create_queue(this->devH, &qinfo, handle);
  if (ret != KV_SUCCESS) {fprintf(stderr, "kv_create_queue failed 0x%x\n", ret);}

  if(qtype == COMPLETION_Q_TYPE && is_polling == 0) {
    /* Interrupt mode */
    // set up interrupt handler
    kv_interrupt_handler int_func = (kv_interrupt_handler)malloc(sizeof(_kv_interrupt_handler));
    int_func->handler = interrupt_func;
    int_func->private_data = 0;
    int_func->number = 0;
    this->int_handler = int_func;
    
    kv_set_interrupt_handler(*handle, int_func);

  }
  return qid;
}

int32_t KDDriver::init(const char* devpath, const char* configfile, int queue_depth, int is_polling) {


#ifndef WITH_KDD
  fprintf(stderr, "Kernel Driver is not supported. \nPlease set compilation option properly (-DWITH_KDD=ON)\n");
  exit(1);
#endif

  
  kv_result ret;

  for (int i = 0; i < MAX_POOLSIZE; i++) {
    this->kv_key_pool.push(new kv_key());
    this->kv_value_pool.push(new kv_value());
    this->kv_ctx_pool.push(new kv_kdd_context());
  }


  kv_device_init_t dev_init;
  dev_init.devpath = devpath;
  dev_init.need_persistency = FALSE;
  dev_init.is_polling = (is_polling == 1 ? TRUE : FALSE);
  dev_init.configfile = NULL;
  ret = kv_initialize_device(&dev_init, &this->devH);
  if (ret != KV_SUCCESS) { fprintf(stderr, "kv_initialize_device failed %d\n", ret); return ret;}

  ret = get_namespace_default(this->devH, &this->nsH);
  if (ret != KV_SUCCESS) { fprintf(stderr, "get_namespace_default failed %d\n", ret); return ret;}

  this->queuedepth = queue_depth;
  int cqid = create_queue(this->queuedepth, COMPLETION_Q_TYPE, &this->cqH, 0, is_polling);
  create_queue(this->queuedepth, SUBMISSION_Q_TYPE, &this->sqH, cqid, is_polling);

  return ret;
}

KDDriver::kv_kdd_context* KDDriver::prep_io_context(int opcode, int contid, const kvs_key *key, const kvs_value *value, uint8_t option, void *private1, void *private2, bool syncio){

  validate_request(key, value);
#if defined use_pool  
  std::unique_lock<std::mutex> lock(this->lock);
  kv_kdd_context *ctx = this->kv_ctx_pool.front();
  this->kv_ctx_pool.pop();
  lock.unlock();
#else
  kv_kdd_context *ctx = (kv_kdd_context *)calloc(1, sizeof(kv_kdd_context));
#endif
  ctx->on_complete = this->user_io_complete;
  ctx->iocb.opcode = opcode;
  ctx->iocb.contid = contid;
  if(key) {
    ctx->iocb.key = key->key;
    ctx->iocb.keysize = key->length;
  } else {
    ctx->iocb.key = 0;
    ctx->iocb.keysize = 0;    
  }
  ctx->iocb.option = option;
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
  
  ctx->free_ctx = syncio ? 0: 1;
  return ctx;
}

/* MAIN ENTRY POINT */
int32_t KDDriver::store_tuple(int contid, const kvs_key *key, const kvs_value *value, uint8_t option, void *private1, void *private2, bool syncio) {

  auto ctx = prep_io_context(IOCB_ASYNC_PUT_CMD, contid, key, value, option, private1, private2, syncio);
  kv_postprocess_function f = {kdd_on_io_complete, (void*)ctx};
#if defined use_pool  
  std::unique_lock<std::mutex> lock(this->lock);
  kv_key *key_adi = this->kv_key_pool.front();
  this->kv_key_pool.pop();
  kv_value *value_adi = this->kv_value_pool.front();
  this->kv_value_pool.pop();
  lock.unlock();

  if(key_adi ==NULL || value_adi == NULL) {
    fprintf(stderr, "NO available kv pair %p %p\n", key_adi, value_adi);
    exit(1);
  }
  ctx->key = key_adi;
  ctx->value = value_adi;
  
  key_adi->key = key->key;
  key_adi->length = (kv_key_t)key->length;
  value_adi->value = value->value;
  value_adi->length = (kv_value_t)value->length;
  //value_adi->value_size = (kv_value_t)value->length;
  value_adi->offset = 0;

  int ret = kv_store(this->sqH, this->nsH, key_adi, value_adi, KV_STORE_OPT_DEFAULT, &f);
#else
  ctx->key = (kv_key*)key;
  ctx->value = (kv_value*)value;
  //ctx->value->value_size = ctx->value->length;

  int ret = kv_store(this->sqH, this->nsH, (kv_key*)key, (kv_value*)value, KV_STORE_OPT_DEFAULT, &f);
#endif
  if(syncio && ret == 0) {
    uint32_t processed = 0;
    do{
      ret = kv_poll_completion(this->cqH, 0, &processed);
    } while (processed == 0);
    if (ret != KV_SUCCESS && ret != KV_WRN_MORE)
      fprintf(stdout, "sync io polling failed\n");
    if (syncio )ret = ctx->iocb.result;

#if defined use_pool
    std::unique_lock<std::mutex> lock(owner->lock);
    memset(ctx, 0, sizeof(KDDriver::kv_kdd_context));
    this->kv_ctx_pool.push(ctx);
    lock.unlock();
#else
    free(ctx);
    ctx = NULL;
#endif
  }
  
  return ret;
}


int32_t KDDriver::retrieve_tuple(int contid, const kvs_key *key, kvs_value *value, uint8_t option, void *private1, void *private2, bool syncio) {

  auto ctx = prep_io_context(IOCB_ASYNC_GET_CMD, contid, key, value, option, private1, private2, syncio);
  kv_postprocess_function f = {kdd_on_io_complete, (void*)ctx};
#if defined use_pool

  std::unique_lock<std::mutex> lock(this->lock);
  kv_key *key_adi = this->kv_key_pool.front();
  this->kv_key_pool.pop();
  kv_value *value_adi = this->kv_value_pool.front();
  this->kv_value_pool.pop();
  lock.unlock();
  ctx->key = key_adi;
  ctx->value = value_adi;
    
  key_adi->key = key->key;
  key_adi->length = key->length;
  value_adi->value = value->value;
  value_adi->length = (kv_value_t)value->length;
  //value_adi->value_size = (kv_value_t)value->length;
  value_adi->offset = 0;

  int ret = kv_retrieve(this->sqH, this->nsH, key_adi, KV_RETRIEVE_OPT_DEFAULT, value_adi, &f);
#else
  ctx->key = (kv_key*)key;
  ctx->value = (kv_value*)value;
  //ctx->value->value_size = ctx->value->length;
  int ret = kv_retrieve(this->sqH, this->nsH, (kv_key*)key, KV_RETRIEVE_OPT_DEFAULT, (kv_value*)value, &f);  
#endif

  if(syncio && ret == 0) {

    uint32_t processed = 0;
    do{
      ret = kv_poll_completion(this->cqH, 0, &processed);
    } while (processed == 0);
    if (ret != KV_SUCCESS && ret != KV_WRN_MORE)
      fprintf(stdout, "sync io polling failed\n");
    if (syncio) ret = ctx->iocb.result;
#if defined use_pool
    std::unique_lock<std::mutex> lock(owner->lock);
    memset(ctx, 0, sizeof(KDDriver::kv_kdd_context));
    this->kv_ctx_pool.push(ctx);
    lock.unlock();
#else
    free(ctx);
    ctx = NULL;
#endif
  }
  return ret;
}

int32_t KDDriver::delete_tuple(int contid, const kvs_key *key, uint8_t option, void *private1, void *private2, bool syncio) {
  auto ctx = prep_io_context(IOCB_ASYNC_DEL_CMD, contid, key, NULL, option, private1, private2, syncio);
  kv_postprocess_function f = {kdd_on_io_complete, (void*)ctx};
#if defined use_pool
  std::unique_lock<std::mutex> lock(this->lock);
  kv_key *key_adi = this->kv_key_pool.front();
  this->kv_key_pool.pop();
  lock.unlock();  
  ctx->key = key_adi;
  ctx->value = NULL;
  
  key_adi->key = key->key;
  key_adi->length = key->length;

  int ret =  kv_delete(this->sqH, this->nsH, key_adi, KV_DELETE_OPT_DEFAULT, &f);
#else
  ctx->key = (kv_key*)key;
  ctx->value = NULL;
  int ret =  kv_delete(this->sqH, this->nsH, (kv_key*)key, KV_DELETE_OPT_DEFAULT, &f);
#endif
  
  if(syncio && ret == 0) {
    uint32_t processed = 0;
    do{
      ret = kv_poll_completion(this->cqH, 0, &processed);
    }while (processed == 0);
    if (ret != KV_SUCCESS && ret != KV_WRN_MORE)
      fprintf(stdout, "sync io polling failed\n");
    if (syncio )ret = ctx->iocb.result;
#if defined use_pool
    std::unique_lock<std::mutex> lock(owner->lock);
    memset(ctx, 0, sizeof(KDDriver::kv_kdd_context));
    this->kv_ctx_pool.push(ctx);
    lock.unlock();
#else
    free(ctx);
    ctx = NULL;
#endif
  }    
  
  return ret;
}

int32_t KDDriver::open_iterator(int contid,  uint8_t option, uint32_t bitmask,
				uint32_t bit_pattern, kvs_iterator_handle *iter_hd) {
  int ret = 0;
  if(option == KVS_ITERATOR_OPT_KV) {
    fprintf(stderr, "Kernel driver does not support iterator for key-value retrieve\n");
    exit(1);
  }
  kvs_iterator_handle iterh = (kvs_iterator_handle)malloc(sizeof(struct _kvs_iterator_handle));
  auto ctx = prep_io_context(IOCB_ASYNC_ITER_OPEN_CMD, 0, 0, 0, option, iterh, 0/*private1, private2*/, 1); 
  
  kv_group_condition grp_cond = {bitmask, bit_pattern};
  kv_postprocess_function f = {kdd_on_io_complete, (void*)ctx};  

  this->done = 0;  
  ret = kv_open_iterator(this->sqH, this->nsH, /*KV_ITERATOR_OPT_KV*/KV_ITERATOR_OPT_KEY/*(kv_iterator_option)option*/, &grp_cond, &f);
  if(ret != KV_SUCCESS) {
    fprintf(stderr, "kv_open_iterator failed with error:  0x%X\n", ret);
    return ret;
  }

  if(!this->int_handler) { // polling
    uint32_t processed = 0;
    do {
      ret = kv_poll_completion(this->cqH, 0, &processed);
    } while (processed == 0);
  } else { // interrupt
    std::unique_lock<std::mutex> lock(this->lock);
    while(this->done == 0)
      this->done_cond.wait(lock);
  }

  ret = ctx->iocb.result;
  *iter_hd = iterh;
#if defined use_pool
  std::unique_lock<std::mutex> lock(owner->lock);
  memset(ctx, 0, sizeof(KDDriver::kv_kdd_context));
  this->kv_ctx_pool.push(ctx);
  lock.unlock();
#else
  free(ctx);
  ctx = NULL;
#endif
  
  return ret;
}

int32_t KDDriver::close_iterator(int contid, kvs_iterator_handle hiter) {
  int ret = 0;
  auto ctx = prep_io_context(IOCB_ASYNC_ITER_CLOSE_CMD, 0, 0, 0, 0, 0,0/*private1, private2*/, 1);
  
  kv_postprocess_function f = {kdd_on_io_complete, (void*)ctx};
  this->done = 0;
  ret = kv_close_iterator(this->sqH, this->nsH, hiter->iterh_adi, &f);

  if(ret != KV_SUCCESS) {
    fprintf(stderr, "kv_open_iterator failed with error:  0x%X\n", ret);
    exit(1);
  }

  if(!this->int_handler) { // polling
    uint32_t processed = 0;
    do{
      ret = kv_poll_completion(this->cqH, 0, &processed);
    } while (processed == 0);
  } else { // interrupt
    std::unique_lock<std::mutex> lock(this->lock);
    while(this->done == 0)
      this->done_cond.wait(lock);
  }

  if(hiter) free(hiter);

#if defined use_pool
  std::unique_lock<std::mutex> lock(owner->lock);
  memset(ctx, 0, sizeof(KDDriver::kv_kdd_context));
  this->kv_ctx_pool.push(ctx);
  lock.unlock();
#else
  free(ctx);
  ctx = NULL;
#endif
  
  return 0;
}

int32_t KDDriver::iterator_next(kvs_iterator_handle hiter, kvs_iterator_list *iter_list, void *private1, void *private2, bool syncio) {
  
  int ret = 0;
  auto ctx = prep_io_context(IOCB_ASYNC_ITER_NEXT_CMD, 0, 0, 0, 0, private1, private2, syncio);
  kv_postprocess_function f = {kdd_on_io_complete, (void*)ctx};
  
  ret = kv_iterator_next(this->sqH, this->nsH, hiter->iterh_adi, (kv_iterator_list *)iter_list, &f);
  if(ret != KV_SUCCESS) {
    fprintf(stderr, "kv_iterator_next failed with error:  0x%X\n", ret);
    return ret;
  }

  if(syncio) {
    uint32_t processed = 0;
    do{
      ret = kv_poll_completion(this->cqH, 0, &processed);
    } while (processed == 0);
    ret = ctx->iocb.result;

#if defined use_pool
    std::unique_lock<std::mutex> lock(owner->lock);
    memset(ctx, 0, sizeof(KDDriver::kv_kdd_context));
    this->kv_ctx_pool.push(ctx);
    lock.unlock();
#else
    free(ctx);
    ctx = NULL;
#endif
  }

  return ret;
}

float KDDriver::get_waf(){
  WRITE_WARNING("KDD: get waf is not supported in kernel driver\n");
  return 0;
}

int32_t KDDriver::get_used_size(){
  WRITE_WARNING("KDD: get used size is not supported in kernel driver\n");
  return 0;
}

int64_t KDDriver::get_total_size() {
  WRITE_WARNING("KDD: get total size is not supported in kernel driver\n");
  return 0;
}

int32_t KDDriver::process_completions(int max)
{
        int ret;  
        uint32_t processed = 0;

	ret = kv_poll_completion(this->cqH, 0, &processed);
	if (ret != KV_SUCCESS && ret != KV_WRN_MORE)
	  fprintf(stdout, "Polling failed\n");

	return processed;
}


KDDriver::~KDDriver() {

  while(!this->kv_key_pool.empty()) {
    auto p = this->kv_key_pool.front();
    this->kv_key_pool.pop();
    delete p;
  }

  while(!this->kv_value_pool.empty()) {
    auto p = this->kv_value_pool.front();
    this->kv_value_pool.pop();
    delete p;
  }

  while(!this->kv_ctx_pool.empty()) {
    auto p = this->kv_ctx_pool.front();
    this->kv_ctx_pool.pop();
    delete p;
  }

  // shutdown device
  if(this->int_handler) {
    free(int_handler);
  }
  
  while (get_queued_commands_count(cqH) > 0 || get_queued_commands_count(sqH) > 0){
    usleep(10);
  }

  if (kv_delete_queue(this->devH, this->sqH) != KV_SUCCESS) {
    fprintf(stderr, "kv delete submission queue failed\n");
    exit(1);
  }

  if (kv_delete_queue(this->devH, this->cqH) != KV_SUCCESS) {
    fprintf(stderr, "kv delete completion queue failed\n");
    exit(1);
  }

  kv_delete_namespace(devH, nsH);
  kv_cleanup_device(devH);
}
