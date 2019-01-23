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
#include "kvemul.hpp"

#include <algorithm>
#include <atomic>
#include <tbb/concurrent_queue.h>
#include <list>
#include <kvs_adi.h>

#define MAX_POOLSIZE 10240
#define use_pool

KvEmulator::KvEmulator(kv_device_priv *dev, kvs_callback_function user_io_complete_):
  KvsDriver(dev, user_io_complete_), devH(0),nsH(0), sqH(0), cqH(0), int_handler(0)
{
  queuedepth = 256;
}

// this function will be called after completion of a command
void interrupt_func_emu(void *data, int number) {
  (void) data;
  (void) number;

  //fprintf(stdout, "inside interrupt\n");
}

void on_io_complete(kv_io_context *context){

  if((context->retcode != KV_SUCCESS) && (context->retcode != KV_ERR_KEY_NOT_EXIST)
     && context->retcode != KV_WRN_MORE/* && (context->retcode != KV_ERR_ITERATOR_END)*/) {
    const char *cmd = (context->opcode == KV_OPC_GET)? "GET": ((context->opcode == KV_OPC_STORE)? "PUT": (context->opcode == KV_OPC_DELETE)? "DEL":"OTHER");
    fprintf(stderr, "%s failed with error 0x%x\n", cmd, context->retcode);
  } 

  //const char *cmd = (context->opcode == KV_OPC_GET)? "GET": ((context->opcode == KV_OPC_STORE)? "PUT": (context->opcode == KV_OPC_DELETE)? "DEL":"OTHER");

  KvEmulator::kv_emul_context *ctx = (KvEmulator::kv_emul_context*)context->private_data;
  kvs_callback_context *iocb = &ctx->iocb;
  const auto owner = ctx->owner;
  
  iocb->result = (kvs_result)context->retcode;
  
  if(context->opcode == KV_OPC_GET)
    iocb->value->actual_value_size = context->value->actual_value_size;
  
  if(ctx->syncio) {
    if(context->opcode == KV_OPC_OPEN_ITERATOR) {
      //kvs_iterator_handle iterh = (kvs_iterator_handle)iocb->private1;
      //iterh/*iterh_adi*/ = context->result.hiter;
      kvs_iterator_handle *iterh = (kvs_iterator_handle*)iocb->private1;
      *iterh = context->result.hiter;
    }
    if(owner->int_handler != 0) {
      std::unique_lock<std::mutex> lock_s(ctx->lock_sync);
      ctx->done_sync = 1;
      ctx->done_cond_sync.notify_one();
      lock_s.unlock();
    }
  } else {
    if(context->opcode != KV_OPC_OPEN_ITERATOR && context->opcode != KV_OPC_CLOSE_ITERATOR) {
      if(ctx->on_complete && iocb){
	ctx->on_complete(iocb);
      }
    }

#if defined use_pool
    if(!ctx->syncio) {
      std::unique_lock<std::mutex> lock(owner->lock);
      if(ctx->key) {
	owner->kv_key_pool.push(ctx->key);
      }
      if(ctx->value) {
	owner->kv_value_pool.push(ctx->value);
      }
      
      memset(ctx, 0, sizeof(KvEmulator::kv_emul_context));
      owner->kv_ctx_pool.push(ctx);
      lock.unlock();
    }
#else
    if(!ctx->syncio) {
      free(ctx);
      ctx = NULL;
    }
#endif
  }
}

int KvEmulator::create_queue(int qdepth, uint16_t qtype, kv_queue_handle *handle, int cqid, int is_polling){
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
    // Interrupt mode 
    // set up interrupt handler
    kv_interrupt_handler int_func = (kv_interrupt_handler)malloc(sizeof(_kv_interrupt_handler));
    int_func->handler = interrupt_func_emu;
    int_func->private_data = 0;
    int_func->number = 0;
    this->int_handler = int_func;

    kv_set_interrupt_handler(*handle, int_func);
  }

  return qid;
}

int32_t KvEmulator::init(const char* devpath, const char* configfile, int queuedepth, int is_polling) {

#ifndef WITH_EMU
  fprintf(stderr, "Emulator is not supported. \nPlease set compilation option properly (-DWITH_EMU=ON)\n");
  exit(1);
#endif
  
  for (int i = 0; i < MAX_POOLSIZE; i++) {
    this->kv_key_pool.push(new kv_key());
    this->kv_value_pool.push(new kv_value());
    this->kv_ctx_pool.push(new kv_emul_context());
  }

  kv_result ret;

  kv_device_init_t dev_init;
  dev_init.devpath = devpath;
  dev_init.need_persistency = FALSE;
  dev_init.configfile = configfile;
  dev_init.is_polling = (is_polling == 1 ? TRUE : FALSE);
  
  ret = kv_initialize_device(&dev_init, &this->devH);
  if (ret != KV_SUCCESS) { fprintf(stderr, "kv_initialize_device failed 0x%x\n", ret); return ret;}

  ret = get_namespace_default(this->devH, &this->nsH);
  if (ret != KV_SUCCESS) { fprintf(stderr, "get_namespace_default failed 0x%x\n", ret); return ret;}
  
  this->queuedepth = queuedepth;
  int cqid = create_queue(this->queuedepth, COMPLETION_Q_TYPE, &this->cqH, 0, is_polling);
  create_queue(this->queuedepth, SUBMISSION_Q_TYPE, &this->sqH, cqid, is_polling);

  return ret;
}

KvEmulator::kv_emul_context* KvEmulator::prep_io_context(int opcode, int contid, const kvs_key *key, const kvs_value *value, void *private1, void *private2, bool syncio, kvs_callback_function cbfn){

#if defined use_pool
  std::unique_lock<std::mutex> lock(this->lock);
  kv_emul_context *ctx = this->kv_ctx_pool.front();
  this->kv_ctx_pool.pop();
  lock.unlock();
#else
  kv_emul_context *ctx = (kv_emul_context *)calloc(1, sizeof(kv_emul_context));
#endif
  
  //kv_emul_context *ctx = (kv_emul_context *)calloc(1, sizeof(kv_emul_context));
  
  ctx->on_complete = cbfn;
  ctx->iocb.opcode = opcode;
  //ctx->iocb.contid = contid;
  if(key) {
    ctx->iocb.key = (kvs_key*)key;
   } else {
    ctx->iocb.key = 0;
  }

  if(value) {
    ctx->iocb.value = (kvs_value*)value;
  } else {
    ctx->iocb.value = 0;
  }

  ctx->iocb.private1 = private1;
  ctx->iocb.private2 = private2;
  ctx->owner = this;

  //ctx->free_ctx = syncio ? 0: 1;
  ctx->syncio = syncio;
  std::unique_lock<std::mutex> lock_s(ctx->lock_sync);
  ctx->done_sync = 0;
  return ctx;
}

/* MAIN ENTRY POINT */
int32_t KvEmulator::store_tuple(int contid, const kvs_key *key, const kvs_value *value, kvs_store_option option, void *private1, void *private2, bool syncio, kvs_callback_function cbfn) {

  auto ctx = prep_io_context(IOCB_ASYNC_PUT_CMD, contid, key, value, private1, private2, syncio, /*this->user_io_complete*/cbfn);
  kv_postprocess_function f = {on_io_complete, (void*)ctx};

  kv_store_option option_adi;
  if(!option.kvs_store_compress) {
    // Default: no compression
    switch(option.st_type) {
    case KVS_STORE_POST:
      option_adi = KV_STORE_OPT_DEFAULT;
      break;
    case KVS_STORE_UPDATE_ONLY:
      option_adi = KV_STORE_OPT_UPDATE_ONLY;
      break;
    case KVS_STORE_NOOVERWRITE:
      option_adi = KV_STORE_OPT_IDEMPOTENT;
      break;
    case KVS_STORE_APPEND:
      option_adi = KV_STORE_OPT_APPEND;
      break;
    default:
      fprintf(stderr, "WARN: Wrong store option\n");
      return KVS_ERR_OPTION_INVALID;
    }
  } else {
    // compression
    switch(option.st_type) {
    case KVS_STORE_POST:
      option_adi = KV_STORE_OPT_POST_WITH_COMPRESS;
      break;
    case KVS_STORE_UPDATE_ONLY:
      option_adi = KV_STORE_OPT_UPDATE_ONLY_COMPRESS;
      break;
    case KVS_STORE_NOOVERWRITE:
      option_adi = KV_STORE_OPT_NOOVERWRITE_COMPRESS;
      break;
    case KVS_STORE_APPEND:
      option_adi = KV_STORE_OPT_APPEND_COMPRESS;
      break;
    default:
      fprintf(stderr, "WARN: Wrong store option\n");
      return KVS_ERR_OPTION_INVALID;
    }
  }
  
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
  key_adi->length = (kv_key_t)key->length;
  value_adi->value = value->value;
  value_adi->length = (kv_value_t)value->length;
  value_adi->offset = 0;
 
  int ret = kv_store(this->sqH, this->nsH, key_adi, value_adi, option_adi, &f);
#else
  ctx->key = (kv_key*)key;
  ctx->value = (kv_value*)value;
  
  int ret = kv_store(this->sqH, this->nsH, (kv_key*)key, (kv_value*)value, option_adi, &f);
#endif
  
  if(syncio && ret == 0) {
    std::unique_lock<std::mutex> lock_s(ctx->lock_sync);
    while(ctx->done_sync == 0)
      ctx->done_cond_sync.wait(lock_s);
    lock_s.unlock();    
    if (syncio )ret = ctx->iocb.result;

#if defined use_pool
    /*
    std::unique_lock<std::mutex> lock(this->lock);
    memset(ctx, 0, sizeof(KvEmulator::kv_emul_context));
    this->kv_ctx_pool.push(ctx);
    lock.unlock();
    */
    std::unique_lock<std::mutex> lock(this->lock);    
    this->kv_key_pool.push(key_adi);
    this->kv_value_pool.push(value_adi);
    memset(ctx, 0, sizeof(KvEmulator::kv_emul_context));
    this->kv_ctx_pool.push(ctx);
    
    //free(ctx);
    //ctx = NULL;
#else
    free(ctx);
    ctx = NULL;
#endif
    
  }
  
  return ret;
}


int32_t KvEmulator::retrieve_tuple(int contid, const kvs_key *key, kvs_value *value, kvs_retrieve_option option/*uint8_t option*/, void *private1, void *private2, bool syncio, kvs_callback_function cbfn) {

  auto ctx = prep_io_context(IOCB_ASYNC_GET_CMD, contid, key, value, private1, private2, syncio, cbfn);
  kv_postprocess_function f = {on_io_complete, (void*)ctx};

  kv_retrieve_option option_adi;
  if(!option.kvs_retrieve_delete) {
    if(!option.kvs_retrieve_decompress)
      option_adi = KV_RETRIEVE_OPT_DEFAULT;
    else
      option_adi = KV_RETRIEVE_OPT_DECOMPRESS;
  } else {
    if(!option.kvs_retrieve_decompress)
      option_adi = KV_RETRIEVE_OPT_DELETE;
    else
      option_adi = KV_RETRIEVE_OPT_DECOMPRESS_DELETE;
  }
  
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
  value_adi->offset = 0;

  int ret = kv_retrieve(this->sqH, this->nsH, key_adi, option_adi, value_adi, &f);
#else
  ctx->key = (kv_key*)key;
  ctx->value = (kv_value*)value;
  int ret = kv_retrieve(this->sqH, this->nsH, (kv_key*)key, option_adi, (kv_value*)value, &f);
#endif
  
  if(syncio && ret == 0) {
    std::unique_lock<std::mutex> lock_s(ctx->lock_sync);
    while(ctx->done_sync == 0)
      ctx->done_cond_sync.wait(lock_s);
    lock_s.unlock();
    if (syncio) {
      ret = ctx->iocb.result;
      value->actual_value_size = ctx->iocb.value->actual_value_size;
    }
    
#if defined use_pool
    /*
    std::unique_lock<std::mutex> lock(this->lock);
    memset(ctx, 0, sizeof(KvEmulator::kv_emul_context));
    this->kv_ctx_pool.push(ctx);
    lock.unlock();
    */

    std::unique_lock<std::mutex> lock(this->lock);
    this->kv_key_pool.push(key_adi);
    this->kv_value_pool.push(value_adi);
    memset(ctx, 0, sizeof(KvEmulator::kv_emul_context));
    this->kv_ctx_pool.push(ctx);
    
    //free(ctx);
    //ctx = NULL;
#else
    free(ctx);
    ctx = NULL;
#endif
  }
  return ret;
}

int32_t KvEmulator::delete_tuple(int contid, const kvs_key *key, kvs_delete_option option/*uint8_t option*/, void *private1, void *private2, bool syncio, kvs_callback_function cbfn) {
  auto ctx = prep_io_context(IOCB_ASYNC_DEL_CMD, contid, key, NULL, private1, private2, syncio, cbfn);
  kv_postprocess_function f = {on_io_complete, (void*)ctx};

  kv_delete_option option_adi;
  if(!option.kvs_delete_error)
    option_adi = KV_DELETE_OPT_DEFAULT;
  else
    option_adi = KV_DELETE_OPT_ERROR;
  
#if defined use_pool
  std::unique_lock<std::mutex> lock(this->lock);
  kv_key *key_adi = this->kv_key_pool.front();
  this->kv_key_pool.pop();
  lock.unlock();
  ctx->key = key_adi;
  ctx->value = NULL;
  
  key_adi->key = key->key;
  key_adi->length = key->length;

  int ret =  kv_delete(this->sqH, this->nsH, key_adi, option_adi, &f);
#else
  ctx->key = (kv_key*)key;
  ctx->value = NULL;
  int ret =  kv_delete(this->sqH, this->nsH, (kv_key*)key, option_adi, &f);
#endif
  
  if(syncio && ret == 0) {
    std::unique_lock<std::mutex> lock_s(ctx->lock_sync);
    while(ctx->done_sync == 0)
      ctx->done_cond_sync.wait(lock_s);
    lock_s.unlock();
    if (syncio )ret = ctx->iocb.result;

#if defined use_pool
    /*
    std::unique_lock<std::mutex> lock(this->lock);
    memset(ctx, 0, sizeof(KvEmulator::kv_emul_context));
    this->kv_ctx_pool.push(ctx);
    lock.unlock();
    */
    std::unique_lock<std::mutex> lock(this->lock);
    this->kv_key_pool.push(key_adi);
    memset(ctx, 0, sizeof(KvEmulator::kv_emul_context));
    this->kv_ctx_pool.push(ctx);
    //free(ctx);
    //ctx = NULL;
#else
    free(ctx);
    ctx = NULL;
#endif
    
  }
  return ret;
}

int32_t KvEmulator::exist_tuple(int contid, uint32_t key_cnt, const kvs_key *keys, uint32_t buffer_size, uint8_t *result_buffer, void *private1, void *private2, bool syncio, kvs_callback_function cbfn) {

  auto ctx = prep_io_context(IOCB_ASYNC_CHECK_KEY_EXIST_CMD, contid, keys, NULL, private1, private2, syncio, cbfn);
  ctx->iocb.key_cnt = key_cnt;
  ctx->iocb.result_buffer = result_buffer;
  kv_postprocess_function f = {on_io_complete, (void*)ctx};

  // no need for key_adi pool
  ctx->key = NULL;
  ctx->value = NULL;
  
  int ret = kv_exist(this->sqH, this->nsH, (kv_key*)keys, key_cnt, buffer_size, result_buffer, &f);

  if(syncio && ret == 0) {
    std::unique_lock<std::mutex> lock_s(ctx->lock_sync);
    while(ctx->done_sync == 0)
      ctx->done_cond_sync.wait(lock_s);
    lock_s.unlock();
    if (syncio )ret = ctx->iocb.result;
    
#if defined use_pool
    std::unique_lock<std::mutex> lock(this->lock);
    memset(ctx, 0, sizeof(KvEmulator::kv_emul_context));
    this->kv_ctx_pool.push(ctx);
#else
    free(ctx);
    ctx = NULL;
#endif
  }
    
  return 0;
}

int32_t KvEmulator::open_iterator(int contid,  /*uint8_t option*/kvs_iterator_option option, uint32_t bitmask,
				  uint32_t bit_pattern, kvs_iterator_handle *iter_hd) {
  
  int ret = 0;
  //kvs_iterator_handle iterh = (kvs_iterator_handle)malloc(sizeof(struct _kvs_iterator_handle));
  
  auto ctx = prep_io_context(IOCB_ASYNC_ITER_OPEN_CMD, 0, 0, 0, (void*)iter_hd, 0/*private1, private2*/, TRUE, 0);
  kv_group_condition grp_cond = {bitmask, bit_pattern};
  kv_postprocess_function f = {on_io_complete, (void*)ctx};

  kv_iterator_option option_adi;
  //if(option.kvs_iterator_key) {
  if(option.iter_type == KVS_ITERATOR_KEY) {
    option_adi = KV_ITERATOR_OPT_KEY;
  } else {
    option_adi = KV_ITERATOR_OPT_KV;
  }
  ret = kv_open_iterator(this->sqH, this->nsH, /*option.kvs_iterator_opt_key ? KV_ITERATOR_OPT_KEY : KV_ITERATOR_OPT_KV*/option_adi, &grp_cond, &f);

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
    std::unique_lock<std::mutex> lock_s(ctx->lock_sync);
    while(ctx->done_sync == 0)
      ctx->done_cond_sync.wait(lock_s);
    lock_s.unlock();
  }

  ret = ctx->iocb.result;
  //*iter_hd = iterh;
#if defined use_pool
  
  std::unique_lock<std::mutex> lock(this->lock);
  memset(ctx, 0, sizeof(KvEmulator::kv_emul_context));
  this->kv_ctx_pool.push(ctx);
  lock.unlock();
  
  //free(ctx);
  //ctx = NULL;
#else
  free(ctx);
  ctx = NULL;
#endif
  
  return ret;
}

int32_t KvEmulator::close_iterator(int contid, kvs_iterator_handle hiter) {

  int ret = 0;
  auto ctx = prep_io_context(IOCB_ASYNC_ITER_CLOSE_CMD, 0, 0, 0, 0,0/*private1, private2*/, TRUE, 0);

  kv_postprocess_function f = {on_io_complete, (void*)ctx};
  //this->done = 0;
  ret = kv_close_iterator(this->sqH, this->nsH, hiter/*iterh_adib*/, &f);
  if(ret != KV_SUCCESS) {
    fprintf(stderr, "kv_open_iterator failed with error:  0x%X\n", ret);
    exit(1);
  }

  if(!this->int_handler) { // polling
    uint32_t processed = 0;
    do {
      ret = kv_poll_completion(this->cqH, 0, &processed);
    } while (processed == 0);
  } else { // interrupt
    std::unique_lock<std::mutex> lock_s(ctx->lock_sync);
    while(ctx->done_sync == 0)
      ctx->done_cond_sync.wait(lock_s);
    lock_s.unlock();
  }

  //if(hiter) free(hiter);
  
#if defined use_pool
  
  std::unique_lock<std::mutex> lock(this->lock);
  memset(ctx, 0, sizeof(KvEmulator::kv_emul_context));
  this->kv_ctx_pool.push(ctx);
  lock.unlock();
  
  //free(ctx);
  //ctx = NULL;
#else
  free(ctx);
  ctx = NULL;
#endif
  
  return 0;
}


int32_t KvEmulator::close_iterator_all(int contid) {

  fprintf(stderr, "WARN: this feature is not supported in the emulator\n");
  return KVS_ERR_OPTION_INVALID;
}


int32_t KvEmulator::list_iterators(int contid, kvs_iterator_info *kvs_iters, uint32_t count) {

  //auto ctx = prep_io_context(IOCB_ASYNC_ITER_OPEN_CMD, 0, 0, 0, 0, 0, TRUE, 0);
  //uint32_t count = SAMSUNG_MAX_ITERATORS;
  //kv_postprocess_function f = {on_io_complete, (void*)ctx};

  kv_result res = kv_list_iterators(sqH, nsH, (kv_iterator *)kvs_iters, &count, NULL/*&f*/);

  for(uint32_t i = 0; i< count; i++){
    if(kvs_iters[i].status == 1) fprintf(stdout, "found handler %d %x %x\n",
					 kvs_iters[i].iter_handle, kvs_iters[i].bit_pattern, kvs_iters[i].bitmask);
  }
  
  //free(ctx);
  //ctx = NULL;
  return res;
}

int32_t KvEmulator::iterator_next(kvs_iterator_handle hiter, kvs_iterator_list *iter_list, void *private1, void *private2, bool syncio, kvs_callback_function cbfn) {

  int ret = 0;
  auto ctx = prep_io_context(IOCB_ASYNC_ITER_NEXT_CMD, 0, 0, 0, private1, private2, syncio, cbfn);
  kv_postprocess_function f = {on_io_complete, (void*)ctx};

  ret = kv_iterator_next(this->sqH, this->nsH, hiter/*iterh_adi*/, (kv_iterator_list *)iter_list, &f);
  if(ret != KV_SUCCESS) {
    fprintf(stderr, "kv_iterator_next failed with error:  0x%X\n", ret);
    return ret;
  }

  if(syncio) {
    std::unique_lock<std::mutex> lock_s(ctx->lock_sync);
    while(ctx->done_sync == 0)
      ctx->done_cond_sync.wait(lock_s);
    lock_s.unlock();
    ret = ctx->iocb.result;

#if defined use_pool
    
    std::unique_lock<std::mutex> lock(ctx->lock_sync);
    memset(ctx, 0, sizeof(KvEmulator::kv_emul_context));
    this->kv_ctx_pool.push(ctx);
    lock.unlock();
    
    //free(ctx);
    //ctx = NULL;
#else
    free(ctx);
    ctx = NULL;
#endif
  }
  
  return ret;
}

float KvEmulator::get_waf(){
  WRITE_WARNING("Emulator: get waf is not supported in emulator\n");
  return 0;
}

int32_t KvEmulator::get_device_info(kvs_device *dev_info) {
  return 0;
}

int32_t KvEmulator::get_used_size(int32_t *dev_util){

  int ret = 0;
  kv_device_stat *stat = (kv_device_stat*)malloc(sizeof(kv_device_stat));
  ret = kv_get_device_stat(devH, stat);
  if (ret) {
    fprintf(stdout, "The host failed to communicate with the deivce: 0x%x", ret);
    if(stat) free(stat);
    exit(1);
  }

  *dev_util = stat->utilization;
  if(stat) free(stat);

  return ret;
}

int32_t KvEmulator::get_total_size(int64_t *dev_capa){
  int ret = 0;
  kv_device *devinfo = (kv_device *)malloc(sizeof(kv_device));
  ret = kv_get_device_info(devH, devinfo);

  if (ret) {
    fprintf(stdout, "The host failed to communicate with the deivce: 0x%x", ret);
    if(devinfo) free(devinfo);
    exit(1);
  }

  *dev_capa = devinfo->capacity;
  if(devinfo) free(devinfo);

  return ret;
}

int32_t KvEmulator::process_completions(int max)
{
        int ret;  
        uint32_t processed = 0;

	ret = kv_poll_completion(this->cqH, 0, &processed);
	if (ret != KV_SUCCESS && ret != KV_WRN_MORE)
	  fprintf(stdout, "Polling failed\n");

	return processed;
}


KvEmulator::~KvEmulator() {

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
  /*
  while (get_queued_commands_count(cqH) > 0 || get_queued_commands_count(sqH) > 0){
    usleep(10);
  }
  */

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


