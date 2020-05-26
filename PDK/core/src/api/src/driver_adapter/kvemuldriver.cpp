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
inline void malloc_context(KvEmulator::kv_emul_context **ctx,
                           std::condition_variable* ctx_pool_notfull,
                           std::queue<KvEmulator::kv_emul_context *> &pool, std::mutex& pool_lock) {
#if defined use_pool
  std::unique_lock<std::mutex> lock(pool_lock);
  while (pool.empty()) {
    ctx_pool_notfull->wait(lock);
  }
  *ctx = pool.front();
  pool.pop();
  lock.unlock();
#else
  *ctx = (kv_emul_context *)calloc(1, sizeof(kv_emul_context));
#endif
}

inline void free_context(KvEmulator::kv_emul_context *ctx,
                         std::condition_variable* ctx_pool_notfull,
                         std::queue<KvEmulator::kv_emul_context *> &pool, std::mutex& pool_lock) {
#if defined use_pool
  std::unique_lock<std::mutex> lock(pool_lock);
  memset(ctx, 0, sizeof(KvEmulator::kv_emul_context));
  pool.push(ctx);
  ctx_pool_notfull->notify_one();
  lock.unlock();
#else
  free(ctx);
  ctx = NULL;
#endif
}


KvEmulator::KvEmulator(kv_device_priv *dev,
                       kvs_postprocess_function user_io_complete_):
  KvsDriver(dev, user_io_complete_), devH(0), nsH(0), sqH(0), cqH(0),
  int_handler(0) {
  queuedepth = 256;
}

// this function will be called after completion of a command
void interrupt_func_emu(void *data, int number) {
  (void) data;
  (void) number;

  //fprintf(stdout, "inside interrupt\n");
}

std::map<kv_result, kvs_result> code_map = {
  {KV_SUCCESS, KVS_SUCCESS},
  {KV_WRN_MORE, KVS_ERR_SYS_IO},
  {KV_ERR_DEV_CAPACITY, KVS_ERR_KS_CAPACITY},
  {KV_ERR_DEV_INIT, KVS_ERR_SYS_IO},
  {KV_ERR_DEV_INITIALIZED, KVS_ERR_SYS_IO},
  {KV_ERR_DEV_NOT_EXIST, KVS_ERR_DEV_NOT_EXIST},
  {KV_ERR_DEV_SANITIZE_FAILED, KVS_ERR_SYS_IO},
  {KV_ERR_ITERATOR_NOT_EXIST, KVS_ERR_ITERATOR_NOT_EXIST},
  {KV_ERR_ITERATOR_ALREADY_OPEN, KVS_ERR_ITERATOR_OPEN},
  {KV_ERR_KEY_INVALID, KVS_ERR_SYS_IO},
  {KV_ERR_KEY_LENGTH_INVALID, KVS_ERR_KEY_LENGTH_INVALID},
  {KV_ERR_KEY_NOT_EXIST, KVS_ERR_KEY_NOT_EXIST},
  {KV_ERR_NS_DEFAULT, KVS_ERR_SYS_IO},
  {KV_ERR_NS_INVALID, KVS_ERR_SYS_IO},
  {KV_ERR_OPTION_INVALID, KVS_ERR_OPTION_INVALID},
  {KV_ERR_PARAM_INVALID, KVS_ERR_PARAM_INVALID},
  {KV_ERR_PURGE_IN_PRGRESS, KVS_ERR_SYS_IO},
  {KV_ERR_SYS_IO, KVS_ERR_SYS_IO},
  {KV_ERR_VALUE_LENGTH_INVALID, KVS_ERR_VALUE_LENGTH_INVALID},
  {KV_ERR_VALUE_LENGTH_MISALIGNED, KVS_ERR_VALUE_OFFSET_MISALIGNED},
  {KV_ERR_VALUE_OFFSET_INVALID, KVS_ERR_VALUE_OFFSET_INVALID},
  {KV_ERR_VENDOR, KVS_ERR_SYS_IO},
  {KV_ERR_PERMISSION, KVS_ERR_SYS_IO},
  {KV_ERR_MISALIGNED_VALUE_OFFSET, KVS_ERR_VALUE_OFFSET_MISALIGNED},
  {KV_ERR_BUFFER_SMALL, KVS_ERR_BUFFER_SMALL},
  {KV_ERR_DEV_MAX_NS, KVS_ERR_SYS_IO},
  {KV_ERR_ITERATOR_COND_INVALID, KVS_ERR_ITERATOR_FILTER_INVALID},
  {KV_ERR_KEY_EXIST, KVS_ERR_VALUE_UPDATE_NOT_ALLOWED},
  {KV_ERR_NS_ATTAHED, KVS_ERR_SYS_IO},
  {KV_ERR_NS_CAPACITY, KVS_ERR_KS_CAPACITY},
  {KV_ERR_NS_NOT_ATTACHED, KVS_ERR_SYS_IO},
  {KV_ERR_QUEUE_CQID_INVALID, KVS_ERR_SYS_IO},
  {KV_ERR_QUEUE_SQID_INVALID, KVS_ERR_SYS_IO},
  {KV_ERR_QUEUE_DELETION_INVALID, KVS_ERR_SYS_IO},
  {KV_ERR_QUEUE_MAX_QUEUE, KVS_ERR_SYS_IO},
  {KV_ERR_QUEUE_QID_INVALID, KVS_ERR_SYS_IO},
  {KV_ERR_QUEUE_QSIZE_INVALID, KVS_ERR_SYS_IO},
  {KV_ERR_TIMEOUT, KVS_ERR_SYS_IO},
  {KV_ERR_UNCORRECTIBLE, KVS_ERR_SYS_IO},
  {KV_ERR_QUEUE_IN_SHUTDOWN, KVS_ERR_SYS_IO},
  {KV_ERR_QUEUE_IS_FULL, KVS_ERR_SYS_IO},
  {KV_ERR_COMMAND_SUBMITTED, KVS_ERR_SYS_IO},
  {KV_ERR_TOO_MANY_ITERATORS_OPEN, KVS_ERR_ITERATOR_MAX},
  {KV_ERR_SYS_BUSY, KVS_ERR_SYS_IO},
  {KV_ERR_COMMAND_INITIALIZED, KVS_ERR_SYS_IO},
  {KV_ERR_DD_UNSUPPORTED_CMD, KVS_ERR_SYS_IO},
  {KV_ERR_ITERATE_REQUEST_FAIL, KVS_ERR_SYS_IO},
  {KV_ERR_DD_UNSUPPORTED, KVS_ERR_SYS_IO},
  {KV_ERR_KEYSPACE_INVALID, KVS_ERR_SYS_IO}
};

void on_io_complete(kv_io_context *context) {

  if ((context->retcode != KV_SUCCESS)
      && (context->retcode != KV_ERR_KEY_NOT_EXIST)
      && context->retcode !=
      KV_WRN_MORE) {
    const char *cmd = (context->opcode == KV_OPC_GET) ? "GET" : ((
                        context->opcode == KV_OPC_STORE) ? "PUT" : (context->opcode == KV_OPC_DELETE) ?
                      "DEL" : "OTHER");
    fprintf(stderr, "%s failed with error 0x%x\n", cmd, convert_return_code(context->retcode));
  }


  KvEmulator::kv_emul_context *ctx = (KvEmulator::kv_emul_context*)
                                     context->private_data;
  kvs_postprocess_context *iocb = &ctx->iocb;
  const auto owner = ctx->owner;
  if (context->opcode == KV_OPC_GET)
    iocb->value->actual_value_size = context->value->actual_value_size -
                                     context->value->offset;

  if (ctx->syncio) {  	
    /*The conversion of the adi layer return code in the synchronous call is in the main entry method.*/
    iocb->result = (kvs_result)context->retcode;
    if (context->opcode == KV_OPC_OPEN_ITERATOR) {
      kvs_iterator_handle *iterh = (kvs_iterator_handle*)iocb->private1;
      *iterh = context->result.hiter;
    }
    if (owner->int_handler != 0) {
      std::unique_lock<std::mutex> lock_s(ctx->lock_sync);
      ctx->done_sync = 1;
      ctx->done_cond_sync.notify_one();
      lock_s.unlock();
    }
  } else {
    iocb->result = convert_return_code(context->retcode);
    if (context->opcode != KV_OPC_OPEN_ITERATOR
        && context->opcode != KV_OPC_CLOSE_ITERATOR) {
      if (ctx->on_complete && iocb) {
        ctx->on_complete(iocb);
      }
    }
    free_context(ctx, &owner->ctx_pool_notfull, owner->kv_ctx_pool, owner->lock);
  }
}

int KvEmulator::create_queue(int qdepth, uint16_t qtype,
                             kv_queue_handle *handle, int cqid, int is_polling) {
  static int qid = -1;
  kv_queue qinfo;
  qinfo.queue_id = ++qid;
  qinfo.queue_size = qdepth;
  qinfo.completion_queue_id = cqid;
  qinfo.queue_type = qtype;
  qinfo.extended_info = NULL;
  kv_result ret = kv_create_queue(this->devH, &qinfo, handle);
  if (ret != KV_SUCCESS) {fprintf(stderr, "kv_create_queue failed 0x%x\n", convert_return_code(ret));}

  if (qtype == COMPLETION_Q_TYPE && is_polling == 0) {
    // Interrupt mode
    // set up interrupt handler
    kv_interrupt_handler int_func = (kv_interrupt_handler)malloc(sizeof(
                                      _kv_interrupt_handler));
    int_func->handler = interrupt_func_emu;
    int_func->private_data = 0;
    int_func->number = 0;
    this->int_handler = int_func;

    kv_set_interrupt_handler(*handle, int_func);
  }

  return qid;
}

int32_t KvEmulator::init(const char* devpath, const char* configfile,
                         int queuedepth, int is_polling) {

#ifndef WITH_EMU
  fprintf(stderr,
          "Emulator is not supported. \nPlease set compilation option properly (-DWITH_EMU=ON)\n");
  exit(1);
#endif

  for (int i = 0; i < MAX_POOLSIZE; i++) {
    this->kv_ctx_pool.push(new kv_emul_context());
  }

  kv_result ret;

  kv_device_init_t dev_init;
  dev_init.devpath = devpath;
  dev_init.need_persistency = FALSE;
  dev_init.configfile = configfile;
  dev_init.is_polling = (is_polling == 1 ? TRUE : FALSE);

  ret = kv_initialize_device(&dev_init, &this->devH);
  if (ret != KV_SUCCESS) { 
    fprintf(stderr, "kv_initialize_device failed 0x%x\n", convert_return_code(ret)); 
    return convert_return_code(ret);
   }

  ret = get_namespace_default(this->devH, &this->nsH);
  if (ret != KV_SUCCESS) { 
    fprintf(stderr, "get_namespace_default failed 0x%x\n", convert_return_code(ret)); 
    return convert_return_code(ret);
    }

  this->queuedepth = queuedepth;
  int cqid = create_queue(this->queuedepth, COMPLETION_Q_TYPE, &this->cqH, 0,
                          is_polling);
  create_queue(this->queuedepth, SUBMISSION_Q_TYPE, &this->sqH, cqid, is_polling);

  return convert_return_code(ret);
}

KvEmulator::kv_emul_context* KvEmulator::prep_io_context(kvs_context opcode,
    kvs_key_space_handle ks_hd, const kvs_key *key, const kvs_value *value,
    void *private1,
    void *private2, bool syncio, kvs_postprocess_function post_fn) {
  kv_emul_context *ctx = NULL;
  malloc_context(&ctx, &this->ctx_pool_notfull, this->kv_ctx_pool, this->lock);
  ctx->on_complete = post_fn;
  ctx->iocb.context = opcode;
  ctx->iocb.ks_hd = ks_hd;
  if (key) {
    ctx->iocb.key = (kvs_key*)key;
  } else {
    ctx->iocb.key = 0;
  }

  if (value) {
    ctx->iocb.value = (kvs_value*)value;
  } else {
    ctx->iocb.value = 0;
  }

  ctx->iocb.private1 = private1;
  ctx->iocb.private2 = private2;
  ctx->owner = this;

  ctx->syncio = syncio;
  std::unique_lock<std::mutex> lock_s(ctx->lock_sync);
  ctx->done_sync = 0;
  return ctx;
}

/* MAIN ENTRY POINT */
int32_t KvEmulator::store_tuple(kvs_key_space_handle ks_hd, const kvs_key *key,
                                const kvs_value *value, kvs_option_store option, void *private1, void *private2,
                                bool syncio, kvs_postprocess_function post_fn) {
  auto ctx = prep_io_context(KVS_CMD_STORE, ks_hd, key, value, private1,
                             private2, syncio, post_fn);
  kv_postprocess_function f = {on_io_complete, (void*)ctx};

  kv_store_option option_adi;

  switch (option.st_type) {
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
      free_context(ctx, &this->ctx_pool_notfull, this->kv_ctx_pool, this->lock);
      return KVS_ERR_OPTION_INVALID;
  }

  ctx->key = (kv_key*)key;
  ctx->value = (kv_value*)value;
  int ret = kv_store(this->sqH, this->nsH, ks_hd->keyspace_id, (kv_key*)key,
                     (kv_value*)value, option_adi, &f);
  if (ret != KV_SUCCESS) {
    fprintf(stderr, "kv_store failed with error:  0x%X\n", ret);
    free_context(ctx, &this->ctx_pool_notfull, this->kv_ctx_pool, this->lock);
    return convert_return_code(ret);
  }

  if (syncio) {
    std::unique_lock<std::mutex> lock_s(ctx->lock_sync);
    while (ctx->done_sync == 0)
      ctx->done_cond_sync.wait(lock_s);
    lock_s.unlock();
    ret = ctx->iocb.result;

    free_context(ctx, &this->ctx_pool_notfull, this->kv_ctx_pool, this->lock);
  }
  
  return convert_return_code(ret);
}


int32_t KvEmulator::retrieve_tuple(kvs_key_space_handle ks_hd, const kvs_key *key,
  kvs_value *value, kvs_option_retrieve option, void *private1, void *private2,
  bool syncio, kvs_postprocess_function cbfn) {
  auto ctx = prep_io_context(KVS_CMD_RETRIEVE, ks_hd, key, value, private1, 
    private2, syncio, cbfn);
  kv_postprocess_function f = {on_io_complete, (void*)ctx};

  kv_retrieve_option option_adi;
  if(!option.kvs_retrieve_delete){
    option_adi = KV_RETRIEVE_OPT_DEFAULT;
  } else {
    option_adi = KV_RETRIEVE_OPT_DELETE;
  }

  ctx->key = (kv_key*)key;
  ctx->value = (kv_value*)value;
  int ret = kv_retrieve(this->sqH, this->nsH, ks_hd->keyspace_id, 
    (kv_key*)key, option_adi, (kv_value*)value, &f);
  if(ret != KV_SUCCESS) {
    fprintf(stderr, "kv_retrieve failed with error:  0x%X\n", ret);
    free_context(ctx, &this->ctx_pool_notfull, this->kv_ctx_pool, this->lock);
    return convert_return_code(ret);
  }
  
  if(syncio) {
    std::unique_lock<std::mutex> lock_s(ctx->lock_sync);
    while(ctx->done_sync == 0)
      ctx->done_cond_sync.wait(lock_s);
    lock_s.unlock();
    ret = ctx->iocb.result;
    value->actual_value_size = ctx->iocb.value->actual_value_size;
    
    free_context(ctx, &this->ctx_pool_notfull, this->kv_ctx_pool, this->lock);
  }
  
  return convert_return_code(ret);
}


int32_t KvEmulator::delete_tuple(kvs_key_space_handle ks_hd, const kvs_key *key,
                                 kvs_option_delete option, void *private1, void *private2, bool syncio,
                                 kvs_postprocess_function post_fn) {
  auto ctx = prep_io_context(KVS_CMD_DELETE, ks_hd, key, NULL, private1, private2,
                             syncio, post_fn);
  kv_postprocess_function f = {on_io_complete, (void*)ctx};

  kv_delete_option option_adi;
  if (!option.kvs_delete_error)
    option_adi = KV_DELETE_OPT_DEFAULT;
  else
    option_adi = KV_DELETE_OPT_ERROR;

  ctx->key = (kv_key*)key;
  ctx->value = NULL;
  int ret =  kv_delete(this->sqH, this->nsH, ks_hd->keyspace_id, (kv_key*)key,
                       option_adi, &f);
  if (ret != KV_SUCCESS) {
    fprintf(stderr, "kv_delete failed with error:  0x%X\n", ret);
    free_context(ctx, &this->ctx_pool_notfull, this->kv_ctx_pool, this->lock);
    return convert_return_code(ret);
  }

  if (syncio) {
    std::unique_lock<std::mutex> lock_s(ctx->lock_sync);
    while (ctx->done_sync == 0)
      ctx->done_cond_sync.wait(lock_s);
    lock_s.unlock();
    if (syncio )ret = ctx->iocb.result;

    free_context(ctx, &this->ctx_pool_notfull, this->kv_ctx_pool, this->lock);
  }
  
  return convert_return_code(ret);
}

int32_t KvEmulator::exist_tuple(kvs_key_space_handle ks_hd, uint32_t key_cnt,
                                const kvs_key *keys,kvs_exist_list *list, void *private1,
                                void *private2, bool syncio, kvs_postprocess_function post_fn) {
  auto ctx = prep_io_context(KVS_CMD_EXIST, ks_hd, keys, NULL,
                             private1, private2, syncio, post_fn);
  
  ctx->iocb.result_buffer.list = list;
  kv_postprocess_function f = {on_io_complete, (void*)ctx};

  // no need for key_adi pool
  ctx->key = NULL;
  ctx->value = NULL;

  int ret = kv_exist(this->sqH, this->nsH, ks_hd->keyspace_id, (kv_key*)keys,
                     key_cnt, list->length, list->result_buffer, &f);
  if (ret != KV_SUCCESS) {
    fprintf(stderr, "kv_exist failed with error:  0x%X\n", ret);
    free_context(ctx, &this->ctx_pool_notfull, this->kv_ctx_pool, this->lock);
    return convert_return_code(ret);
  }

  if (syncio) {
    std::unique_lock<std::mutex> lock_s(ctx->lock_sync);
    while (ctx->done_sync == 0)
      ctx->done_cond_sync.wait(lock_s);
    lock_s.unlock();
    if (syncio )ret = ctx->iocb.result;

    free_context(ctx, &this->ctx_pool_notfull, this->kv_ctx_pool, this->lock);
  }
  
 return convert_return_code(ret);
}

int32_t KvEmulator::trans_store_cmd_opt(kvs_option_store kvs_opt,
                                        kv_store_option *kv_opt) {

  // Default: no compression
  switch (kvs_opt.st_type) {
    case KVS_STORE_POST:
      *kv_opt = KV_STORE_OPT_DEFAULT;
      break;
    case KVS_STORE_UPDATE_ONLY:
      *kv_opt = KV_STORE_OPT_UPDATE_ONLY;
      break;
    case KVS_STORE_NOOVERWRITE:
      *kv_opt = KV_STORE_OPT_IDEMPOTENT;
      break;
    case KVS_STORE_APPEND:
      *kv_opt = KV_STORE_OPT_APPEND;
      break;
    default:
      fprintf(stderr, "WARN: Wrong store option\n");
      return KVS_ERR_OPTION_INVALID;
  }
  return KVS_SUCCESS;
}

int32_t KvEmulator::create_iterator(kvs_key_space_handle ks_hd, kvs_option_iterator option,
  uint32_t bitmask, uint32_t bit_pattern, kvs_iterator_handle *iter_hd) {
  int ret = 0;
  
  auto ctx = prep_io_context(KVS_CMD_ITER_CREATE, ks_hd, 0, 0,
    (void*)iter_hd, 0, TRUE, 0);
  kv_group_condition grp_cond = {bitmask, bit_pattern};
  kv_postprocess_function f = {on_io_complete, (void*)ctx};

  kv_iterator_option option_adi;
  if(option.iter_type == KVS_ITERATOR_KEY) {
    option_adi = KV_ITERATOR_OPT_KEY;
  } else {
    option_adi = KV_ITERATOR_OPT_KV;
  }
  ret = kv_open_iterator(this->sqH, this->nsH, ks_hd->keyspace_id, option_adi, &grp_cond, &f);
  if(ret != KV_SUCCESS) {
    fprintf(stderr, "kv_open_iterator failed with error:  0x%X\n", ret);
    free_context(ctx, &this->ctx_pool_notfull, this->kv_ctx_pool, this->lock);
    return convert_return_code(ret);
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
  free_context(ctx, &this->ctx_pool_notfull, this->kv_ctx_pool, this->lock);
  
 return convert_return_code(ret);
}

int32_t KvEmulator::delete_iterator(kvs_key_space_handle ks_hd, kvs_iterator_handle hiter) {
  int ret = 0;
  auto ctx = prep_io_context(KVS_CMD_ITER_DELETE, 0, 0, 0, 0,0, TRUE, 0);

  kv_postprocess_function f = {on_io_complete, (void*)ctx};
  //this->done = 0;
  ret = kv_close_iterator(this->sqH, this->nsH, hiter, &f);
  if(ret != KV_SUCCESS) {
    fprintf(stderr, "kv_open_iterator failed with error:  0x%X\n", ret);
    free_context(ctx, &this->ctx_pool_notfull, this->kv_ctx_pool, this->lock);
    return convert_return_code(ret);
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

  free_context(ctx, &this->ctx_pool_notfull, this->kv_ctx_pool, this->lock);

  return 0;
}


int32_t KvEmulator::delete_iterator_all(kvs_key_space_handle ks_hd) {

  fprintf(stderr, "WARN: this feature is not supported in the emulator\n");
  return KVS_ERR_OPTION_INVALID;
}

int32_t KvEmulator::iterator_next(kvs_key_space_handle ks_hd, kvs_iterator_handle hiter, kvs_iterator_list *iter_list, void *private1, void *private2, bool syncio, kvs_postprocess_function cbfn) {

  int ret = 0;
  kv_emul_context* ctx = prep_io_context(KVS_CMD_ITER_NEXT, ks_hd, 0, 0, private1, private2, syncio, cbfn);
  kv_postprocess_function f = {on_io_complete, (void*)ctx};
  ctx->iocb.iter_hd = hiter;
  ctx->iocb.result_buffer.iter_list = iter_list;

  ret = kv_iterator_next(this->sqH, this->nsH, hiter, (kv_iterator_list *)iter_list, &f);
  if(ret != KV_SUCCESS) {
    fprintf(stderr, "kv_iterator_next failed with error:  0x%X\n", convert_return_code(ret));
    free_context(ctx, &this->ctx_pool_notfull, this->kv_ctx_pool, this->lock);
    return convert_return_code(ret);
  }

  if(syncio) {
    std::unique_lock<std::mutex> lock_s(ctx->lock_sync);
    while(ctx->done_sync == 0)
      ctx->done_cond_sync.wait(lock_s);
    lock_s.unlock();
    ret = ctx->iocb.result;

    free_context(ctx, &this->ctx_pool_notfull, this->kv_ctx_pool, this->lock);
  }
  
 return convert_return_code(ret);
}

float KvEmulator::get_waf(){
  WRITE_WARNING("Emulator: get waf is not supported in emulator\n");
  return 0;
}

int32_t KvEmulator::get_device_info(kvs_device *dev_info) {
  return 0;
}

int32_t KvEmulator::get_used_size(uint32_t *dev_util){
  int ret = 0;
  kv_device_stat *stat = (kv_device_stat*)malloc(sizeof(kv_device_stat));
  ret = kv_get_device_stat(devH, stat);
  if (ret) {
    fprintf(stdout, "The host failed to communicate with the deivce: 0x%x", convert_return_code(ret));
    if (stat) free(stat);
    exit(1);
  }

  *dev_util = stat->utilization;
  if (stat) free(stat);

  return convert_return_code(ret);
}

int32_t KvEmulator::get_total_size(uint64_t *dev_capa){
  int ret = 0;
  kv_device *devinfo = (kv_device *)malloc(sizeof(kv_device));
  ret = kv_get_device_info(devH, devinfo);

  if (ret) {
    fprintf(stdout, "The host failed to communicate with the deivce: 0x%x", convert_return_code(ret));
    if (devinfo) free(devinfo);
    exit(1);
  }

  *dev_capa = devinfo->capacity;
  if (devinfo) free(devinfo);

  return convert_return_code(ret);
}

int32_t KvEmulator::process_completions(int max) {
  int ret;
  uint32_t processed = 0;

  ret = kv_poll_completion(this->cqH, 0, &processed);
  if (ret != KV_SUCCESS && ret != KV_WRN_MORE)
    fprintf(stdout, "Polling failed\n");

  return processed;
}

void KvEmulator::wait_for_io(kv_emul_context *ctx) {
  std::unique_lock<std::mutex> lock(ctx->lock_sync);

  while (!ctx->done_sync)
    ctx->done_cond_sync.wait(lock);
}

KvEmulator::~KvEmulator() {
  std::unique_lock<std::mutex> lock(this->lock);
  while (!this->kv_ctx_pool.empty()) {
    auto p = this->kv_ctx_pool.front();
    this->kv_ctx_pool.pop();
    delete p;
  }
  lock.unlock();

  // shutdown device
  if (this->int_handler) {
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

