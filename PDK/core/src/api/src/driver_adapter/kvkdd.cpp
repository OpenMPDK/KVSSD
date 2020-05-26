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
#include <map>

#include "kvs_utils.h"
#include "kvkdd.hpp"
#include <algorithm>
#include <atomic>
#include <tbb/concurrent_queue.h>
#include <list>
#include <kvs_adi.h>
#include <kvs_adi_internal.h>
#include <kadi.h>

#ifdef KVKDD_DEBUG 
class KvsRWLogger {
public:
  
  FILE *fp;
  std::mutex m;
  
  
  KvsRWLogger() {
    fp = fopen( "/tmp/kvkdd.txt", "w" ); // Open file for writing
  }
  
  ~KvsRWLogger() {
    fclose(fp);
  }
    
  // djb2 hash function
  unsigned int DJBHash(const char* str, unsigned int length)
  {
    unsigned int hash = 5381;
    unsigned int i    = 0;

    for (i = 0; i < length; ++str, ++i)
    {
        hash = ((hash << 5) + hash) + (*str);
    }

    return hash;
  }

  void print_key(void *key, int keylength) {
    const char* in = (const char*)key;
    fprintf(fp, "key (%d bytes)=  '", keylength);
    for (int i =0; i < keylength; i++) {
      fprintf(fp, "%02x", (int)(unsigned char)in[i]);
    }
    fprintf(fp, "'");
  }
  void print_value(void *value, int length, int actualsize = -1) {
    unsigned int h = DJBHash((const char*)value, length);

    fprintf(fp, "hashed value='%u', value length = %d", h, length);
    if (actualsize != -1)
      fprintf(fp, ", actual length = %d", actualsize);
  }
  
  void log_write(void *key, int keylength, void *value, int length, int retcode) {
    std::unique_lock<std::mutex> lock(m);
    fprintf(fp, "write: ");
    print_key(key, keylength);
    print_value(value, length);
    fprintf(fp, "-> %d\n", retcode);
  }

  void log_read(void *key, int keylength, void *value, int length, int actualsize, int retcode) {
    std::unique_lock<std::mutex> lock(m);
    fprintf(fp, "read: ");
    print_key(key, keylength);
    print_value(value,length, actualsize);
    fprintf(fp, "-> %d\n", retcode);
  }
};

KvsRWLogger kvkdd_logger;

#endif
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

inline void free_if_error(int ret, KDDriver::kv_kdd_context *ctx) {
 if (ret != 0 && ctx) {
    delete ctx;
  }
}

KDDriver::KDDriver(kv_device_priv *dev, kvs_postprocess_function user_io_complete_):
  KvsDriver(dev, user_io_complete_), devH(0),nsH(0), sqH(0), cqH(0), int_handler(0)
{
  queuedepth = 256;
}

int reformat_iterbuffer(kvs_iterator_list *iter_list)
{
  static const int KEY_LEN_BYTES = 4;
  int ret = 0;
  unsigned int key_size = 0;
  int keydata_len_with_padding = 0;
  char *data_buff = (char *)iter_list->it_list;
  unsigned int buffer_size = iter_list->size;
  unsigned int key_count = iter_list->num_entries; 
  // aioevents.events[i].result & 0x0000FFFF;

  // according to firmware command output format, convert them to KVAPI expected format without any padding
  // all data alreay in user provided buffer, but need to remove padding bytes to conform KVAPI format
  char *current_ptr = data_buff;
  unsigned int buffdata_len = buffer_size;

  if (current_ptr == 0) return KV_ERR_PARAM_INVALID;

  if (buffdata_len < KEY_LEN_BYTES) { 
    iter_list->size= 0;
    iter_list->num_entries = 0;
    return KV_ERR_SYS_IO;
  }

  buffdata_len -= KEY_LEN_BYTES;
  data_buff += KEY_LEN_BYTES;
  for (uint32_t i = 0; i < key_count && buffdata_len > 0; i++)
  {
    if (buffdata_len < KEY_LEN_BYTES)
    {
      ret = KV_ERR_SYS_IO;
      break;
    }

    // move 4 byte key len
    memmove(current_ptr, data_buff, KEY_LEN_BYTES);
    current_ptr += KEY_LEN_BYTES;

    // get key size
    key_size = *((uint32_t *)data_buff);
    buffdata_len -= KEY_LEN_BYTES;
    data_buff += KEY_LEN_BYTES;

    if (key_size > buffdata_len)
    {
      ret = KV_ERR_SYS_IO;
      break;
    }
    if (key_size >= 256)
    {
      ret = KV_ERR_SYS_IO;
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
  iter_list->size = current_ptr - (char *)iter_list->it_list;

  return ret;
}

inline kvs_result convert_return_code(kvs_context opcode, int dev_status_code)
{
  if (dev_status_code == 0)
    return KVS_SUCCESS;

  if (dev_status_code < 0)
  {
    return KVS_ERR_SYS_IO;
  }

  if (opcode ==  KVS_CMD_ITER_NEXT)
  {
    if (dev_status_code == 0x301)
    {
      return KVS_ERR_BUFFER_SMALL; // 769 is small buffer size
    }
    else if (dev_status_code == 0x390)
    { // Iterator does not exists
      return KVS_ERR_ITERATOR_NOT_EXIST;
    }
    else if (dev_status_code == 0x308)
    { // Misaligned Value
      return KVS_ERR_PARAM_INVALID;
    }
    else if (dev_status_code == 0x394)
    {
      return KVS_ERR_SYS_IO; // Failed Iterate Request
    }
    else if ((dev_status_code == 0x393))
    {
      // Scan Finished
      return KVS_SUCCESS;
    }
    else
    {
      return convert_return_code(dev_status_code);
    }
  }

  else if (opcode ==  KVS_CMD_RETRIEVE)
  {
    if (dev_status_code == 0x301)
    {
      return KVS_ERR_VALUE_LENGTH_INVALID;
    }
    else if (dev_status_code == 0x302)
    {
      return KVS_ERR_VALUE_OFFSET_INVALID;
    }
    else if (dev_status_code == 0x303)
    {
      return KVS_ERR_KEY_LENGTH_INVALID;
    }
    else if (dev_status_code == 0x304)
    {
      return KVS_ERR_OPTION_INVALID; // for invalid option
    }
    else if (dev_status_code == 0x308)
    {
      return KVS_ERR_PARAM_INVALID;
    }
    else if (dev_status_code == 0x310)
    {
      return KVS_ERR_KEY_NOT_EXIST;
    }
    else
    {
      return convert_return_code(dev_status_code);
    }
  }
  else if (opcode ==  KVS_CMD_STORE)
  {
    if (dev_status_code == 0x301)
    {
      return KVS_ERR_VALUE_LENGTH_INVALID;
    }
    else if (dev_status_code == 0x303)
    {
      return KVS_ERR_KEY_LENGTH_INVALID;
    }
    else if (dev_status_code == 0x304)
    {
      return KVS_ERR_OPTION_INVALID; // for invalid option
    }
    else if (dev_status_code == 0x308)
    {
      return KVS_ERR_PARAM_INVALID;
    }
    else if (dev_status_code == 0x310)
    {
      return KVS_ERR_KEY_NOT_EXIST;
    }
    else if (dev_status_code == 0x311)
    {
      return KVS_ERR_SYS_IO;
    }
    else if (dev_status_code == 0x312)
    {
      return KVS_ERR_DEV_CAPAPCITY;
    }
    else if (dev_status_code == 0x380)
    {
      return KVS_ERR_VALUE_UPDATE_NOT_ALLOWED;
    }
    else
    {
      return convert_return_code(dev_status_code);
    }
  }

  else if (opcode ==  KVS_CMD_DELETE)
  {
    if (dev_status_code == 0x310)
    {
      return KVS_ERR_KEY_NOT_EXIST;
    }
    else if (dev_status_code == 0x304)
    {
      return KVS_ERR_OPTION_INVALID; // for invalid option
    }
    else
    {
      return convert_return_code(dev_status_code);
    }
  }
  else if (opcode ==  KVS_CMD_EXIST)
  {
    if (dev_status_code == 0x301)
    {
      return KVS_ERR_VALUE_LENGTH_INVALID;
    }
    else if (dev_status_code == 0x303)
    {
      return KVS_ERR_KEY_LENGTH_INVALID;
    }
    else if (dev_status_code == 0x305)
    {
      return KVS_ERR_SYS_IO;
    }
    else if (dev_status_code == 0x310)
    {
      // need to indicate key doesn't exist
      // assume the buffer has been cleared
      return KVS_SUCCESS;
    }
    else
    {
      return convert_return_code(dev_status_code);
    }
  }
  return KVS_ERR_SYS_IO;

}
void kdd_on_io_complete(kv_io_context *context){

  #if 0
  if((context->retcode != KV_SUCCESS) && (context->retcode != KV_ERR_KEY_NOT_EXIST) /*&& (context->retcode != KV_ERR_ITERATOR_END)*/) {
    const char *cmd = (context->opcode == KV_OPC_GET)? "GET": ((context->opcode == KV_OPC_STORE)? "PUT": (context->opcode == KV_OPC_DELETE)? "DEL":"OTHER");
    fprintf(stderr, "%s failed with error 0x%x %s\n", cmd, context->retcode, kvs_errstr(context->retcode));
    //exit(1);
  }
  #endif

  KDDriver::kv_kdd_context *ctx = (KDDriver::kv_kdd_context*)context->private_data;

  kvs_postprocess_context *iocb = &ctx->iocb;
  
  if(iocb->context == KVS_CMD_RETRIEVE) {
    iocb->value->actual_value_size = context->value->actual_value_size;
    iocb->value->length = context->value->length;
    //when it is not a partial retrieve and actual length bigger than buffer length user inputted
    if(iocb->value->length < iocb->value->actual_value_size)
      context->retcode = KV_ERR_BUFFER_SMALL;
  }
  else if (iocb->context == KVS_CMD_ITER_NEXT && context->retcode == 0) {

    kvs_iterator_list* list = iocb->result_buffer.iter_list;
    list->end = (context->hiter.end)?TRUE:FALSE;
    
    list->it_list = (uint8_t*)context->hiter.buf;
    list->size =  context->hiter.buflength;
    if (context->hiter.buf && list->size > 0) {
      list->num_entries = *((unsigned int *)context->hiter.buf);
      reformat_iterbuffer(list);
    } else {
      list->num_entries = 0;
    }

  } else if (iocb->context == KVS_CMD_EXIST) {
    *(uint8_t*)iocb->result_buffer.list->result_buffer = (context->retcode == 0x310)? 0:1;
  }

  #ifdef KVKDD_DEBUG 
    if(iocb->context == KVS_CMD_STORE) {
      kvkdd_logger.log_write(context->key->key, context->key->length, context->value->value, context->value->length, context->retcode);
    } else if(iocb->context == KVS_CMD_RETRIEVE) {
      kvkdd_logger.log_read(context->key->key, context->key->length, context->value->value, context->value->length, context->value->actual_value_size, context->retcode);
    }
  #endif

  if(ctx->syncio) {
    /*The conversion of the adi layer return code in the synchronous call is in the main entry method.*/
    iocb->result = (kvs_result)context->retcode;
    std::unique_lock<std::mutex> lock(ctx->lock_sync);
    ctx->done = true;
    ctx->done_cond_sync.notify_one();

  } else { 
    iocb->result = convert_return_code(iocb->context, context->retcode);
    if(ctx->on_complete && iocb) {
      ctx->on_complete(iocb);
    }
    delete ctx;
    ctx = NULL;
  }
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
  if (ret != KV_SUCCESS) {
  	fprintf(stderr, "kv_create_queue failed 0x%x\n", convert_return_code(ret));
   }

  if (!is_polling) {
    KADI *adi = (KADI*)(this->devH->dev);
    adi->start_cbthread();
  }


  return qid;
}

int32_t KDDriver::init(const char* devpath, const char* configfile, int queue_depth, int is_polling) {

#ifndef WITH_KDD
  fprintf(stderr, "Kernel Driver is not supported.\nPlease set compilation option properly (-DWITH_KDD=ON)\n");
  return KVS_ERR_DD_UNSUPPORTED;
#endif
  
  kv_result ret;

  kv_device_init_t dev_init;
  dev_init.devpath = devpath;
  dev_init.need_persistency = FALSE;
  dev_init.is_polling = (is_polling == 1 ? TRUE : FALSE);
  dev_init.configfile = NULL;
  dev_init.queuedepth = queue_depth;

  ret = kv_initialize_device(&dev_init, &this->devH);  
  if (ret != KV_SUCCESS) { 
    fprintf(stderr, "kv_initialize_device failed 0x%x\n", convert_return_code(ret));
    //exit(1);
    return convert_return_code(ret);
  }

  ret = get_namespace_default(this->devH, &this->nsH);
  if (ret != KV_SUCCESS) { 
    fprintf(stderr, "get_namespace_default failed 0x%x\n", convert_return_code(ret));
    //exit(1);
    return convert_return_code(ret);
  }

  this->queuedepth = queue_depth;
  int cqid = create_queue(this->queuedepth, COMPLETION_Q_TYPE, &this->cqH, 0, is_polling);
  create_queue(this->queuedepth, SUBMISSION_Q_TYPE, &this->sqH, cqid, is_polling);

  

  return convert_return_code(ret);
}

/* MAIN ENTRY POINT */

int32_t KDDriver::store_tuple(kvs_key_space_handle ks_hd, const kvs_key *key,
  const kvs_value *value, kvs_option_store option, void *private1, void *private2,
  bool syncio, kvs_postprocess_function cbfn) {
  auto ctx = prep_io_context(KVS_CMD_STORE, ks_hd, key, value, private1,
    private2, syncio, cbfn);
  kv_postprocess_function f = {
    kdd_on_io_complete, (void*)ctx
  };
  kv_store_option option_adi;
  if(trans_store_cmd_opt(option, &option_adi)) {
    delete ctx;
    return KVS_ERR_OPTION_INVALID;
  }

  int ret = kv_store(this->sqH, this->nsH, ks_hd->keyspace_id, (kv_key*)key,
    (kv_value*)value, option_adi, &f);
  while(ret == KV_ERR_QUEUE_IS_FULL) {
    ret = kv_store(this->sqH, this->nsH, ks_hd->keyspace_id, (kv_key*)key,
      (kv_value*)value, option_adi, &f);
  }
  
  if(syncio && ret == 0) {
    wait_for_io(ctx);
    ret = ctx->iocb.result;

    delete ctx; ctx = NULL;
  }

  free_if_error(ret, ctx);

  return convert_return_code(KVS_CMD_STORE, ret);
}

void KDDriver::wait_for_io(kv_kdd_context *ctx) {
    std::unique_lock<std::mutex> lock(ctx->lock_sync);

    while(!ctx->done)
        ctx->done_cond_sync.wait(lock);

}

int32_t KDDriver::retrieve_tuple(kvs_key_space_handle ks_hd, const kvs_key *key,
  kvs_value *value, kvs_option_retrieve option, void *private1, void *private2,
  bool syncio, kvs_postprocess_function cbfn) {
  auto ctx = prep_io_context(KVS_CMD_RETRIEVE, ks_hd, key, value, private1, private2, syncio, cbfn);
  kv_postprocess_function f = {kdd_on_io_complete, (void*)ctx};

  kv_retrieve_option option_adi;
  if(!option.kvs_retrieve_delete){
    option_adi = KV_RETRIEVE_OPT_DEFAULT;
  } else {
    option_adi = KV_RETRIEVE_OPT_DELETE;
  }
  
  int ret = kv_retrieve(this->sqH, this->nsH, ks_hd->keyspace_id,
    (kv_key*)key, option_adi, (kv_value*)value, &f);
  
  while(ret == KV_ERR_QUEUE_IS_FULL) {
    ret = kv_retrieve(this->sqH, this->nsH, ks_hd->keyspace_id,
      (kv_key*)key, option_adi, (kv_value*)value, &f);
  }

  if(syncio && ret == 0) {
     wait_for_io(ctx);  
     ret = ctx->iocb.result;
     delete ctx;
     ctx = NULL;
  }

  free_if_error(ret, ctx);
  return convert_return_code(KVS_CMD_RETRIEVE, ret);
}

int32_t KDDriver::delete_tuple(kvs_key_space_handle ks_hd, const kvs_key *key,
  kvs_option_delete option, void *private1, void *private2, bool syncio, kvs_postprocess_function cbfn) {
  auto ctx = prep_io_context(KVS_CMD_DELETE, ks_hd, key, NULL, private1, private2,
    syncio, cbfn);
  kv_postprocess_function f = {kdd_on_io_complete, (void*)ctx};

  kv_delete_option option_adi;
  if(!option.kvs_delete_error)
    option_adi = KV_DELETE_OPT_DEFAULT;
  else
    option_adi = KV_DELETE_OPT_ERROR;
  
  int ret =  kv_delete(this->sqH, this->nsH, ks_hd->keyspace_id,
    (kv_key*)key, option_adi, &f);
  
  while(ret == KV_ERR_QUEUE_IS_FULL) {
    ret =  kv_delete(this->sqH, this->nsH, ks_hd->keyspace_id,
      (kv_key*)key, option_adi, &f);
  }
  
  if(syncio && ret == 0) {
    wait_for_io(ctx);  
    ret = ctx->iocb.result;
    delete ctx;
    ctx = NULL;
  }    

  free_if_error(ret, ctx);
  return convert_return_code(KVS_CMD_DELETE, ret);
}


int32_t KDDriver::exist_tuple(kvs_key_space_handle ks_hd, uint32_t key_cnt,
  const kvs_key *keys, kvs_exist_list *list, void *private1,
  void *private2, bool syncio, kvs_postprocess_function cbfn) {
  if(key_cnt > 1) {
    fprintf(stderr, "WARN: kernel driver only supports one key check \n");
    return convert_return_code(KV_ERR_PARAM_INVALID);
  }
  auto ctx = prep_io_context(KVS_CMD_EXIST, ks_hd, keys, NULL,
    private1, private2, syncio, cbfn);
  ctx->iocb.result_buffer.list = list;  
  kv_postprocess_function f = {kdd_on_io_complete, (void*)ctx};

  int ret = kv_exist(this->sqH, this->nsH, ks_hd->keyspace_id,
    (kv_key*)keys, key_cnt, list->length, list->result_buffer, &f);
  while(ret == KV_ERR_QUEUE_IS_FULL) {
    ret = kv_exist(this->sqH, this->nsH, ks_hd->keyspace_id,
      (kv_key*)keys, key_cnt, list->length, list->result_buffer, &f);
  }

  if(syncio && ret == 0) {
    wait_for_io(ctx);  
    ret = ctx->iocb.result;
    delete ctx;
    ctx = NULL;
  }

  free_if_error(ret, ctx);

  return convert_return_code(KVS_CMD_EXIST, ret);
}

int KDDriver::check_opened_iterators(uint32_t bitmask, uint32_t bit_pattern,
                                     kvs_iterator_handle *iter_hd) {
  kv_iterator kv_iters[SAMSUNG_MAX_ITERATORS];
  memset(kv_iters, 0, sizeof(kv_iters));
  uint32_t count = SAMSUNG_MAX_ITERATORS;

  kv_result res = kv_list_iterators_sync(sqH, nsH, kv_iters, &count);
    if(res)
      return convert_return_code(res);
  int opened = 0;
  for(uint32_t i = 0; i< count; i++){
    if(kv_iters[i].status == 1) {
      opened++;
      if(kv_iters[i].prefix == bit_pattern && kv_iters[i].bitmask == bitmask) {
        *iter_hd = kv_iters[i].handle_id;
	      fprintf(stdout, "WARN: Iterator with same prefix/bitmask is already opened\n");
	      return KVS_ERR_ITERATOR_OPEN;
      }
    }
  }

  if(opened == SAMSUNG_MAX_ITERATORS)
    return KVS_ERR_ITERATOR_MAX;
  
  return 0;
}

int32_t KDDriver::create_iterator(kvs_key_space_handle ks_hd, kvs_option_iterator option,
  uint32_t bitmask, uint32_t bit_pattern, kvs_iterator_handle *iter_hd) {
  int ret = check_opened_iterators(bitmask, bit_pattern, iter_hd);
  if (ret) {
    return ret;
  }

  kv_group_condition grp_cond = {bitmask, bit_pattern};
  kv_iterator_option option_adi;
  switch(option.iter_type) {
  case KVS_ITERATOR_KEY:
    option_adi = KV_ITERATOR_OPT_KEY;
    break;
  case KVS_ITERATOR_KEY_VALUE:
    option_adi = KV_ITERATOR_OPT_KV;
    break;
  default:
    fprintf(stderr, "WARN: Wrong iterator option\n");
    return KVS_ERR_OPTION_INVALID;
  }
  
  ret = kv_open_iterator_sync(this->sqH, this->nsH, ks_hd->keyspace_id,
    option_adi, &grp_cond, iter_hd);
  return convert_return_code(ret);
}

int32_t KDDriver::delete_iterator(kvs_key_space_handle ks_hd, kvs_iterator_handle hiter) {
  int ret = kv_close_iterator_sync(this->sqH, this->nsH, hiter/*iterh_adi*/);
  return convert_return_code(ret);
}

int32_t KDDriver::delete_iterator_all(kvs_key_space_handle ks_hd) {
  fprintf(stderr, "WARN: this feature is not supported in the kernel driver\n");
  return KVS_ERR_OPTION_INVALID;
}

int32_t KDDriver::iterator_next(kvs_key_space_handle ks_hd, kvs_iterator_handle hiter, kvs_iterator_list *iter_list, void *private1, void *private2, bool syncio, kvs_postprocess_function cbfn) {
  int ret;
  if (syncio) {   
    ret = kv_iterator_next_sync(this->sqH, this->nsH, hiter, (kv_iterator_list *)iter_list);
    reformat_iterbuffer(iter_list);
    return convert_return_code(ret);
  }
  else { /* async */
    auto ctx = prep_io_context(KVS_CMD_ITER_NEXT, ks_hd, 0, 0, private1, private2, syncio, cbfn);
    ctx->iocb.result_buffer.iter_list = iter_list;
    ctx->iocb.iter_hd = hiter;
    kv_postprocess_function f = {
      kdd_on_io_complete, (void*)ctx
    };
    ret = kv_iterator_next(this->sqH, this->nsH, hiter, (kv_iterator_list *)iter_list, &f);
  
    if(ret != KV_SUCCESS) {
      fprintf(stderr, "kv_iterator_next failed with error:  0x%X\n", convert_return_code(ret));
      delete ctx;
      ctx = NULL;
    }
  }
  return convert_return_code(KVS_CMD_ITER_NEXT, ret);
}

int32_t KDDriver::get_device_info(kvs_device *dev_info) {
 
  return 0;
}

int32_t KDDriver::get_used_size(uint32_t *dev_util){
  int ret = 0;
  kv_device_stat *stat = (kv_device_stat*)malloc(sizeof(kv_device_stat));

  ret = kv_get_device_stat(devH, stat);
  if (ret) {
    fprintf(stdout, "The host failed to communicate with the deivce: 0x%x", convert_return_code(ret));
    if(stat) free(stat);
    return convert_return_code(ret);
  }

  if(stat){
    *dev_util = stat->utilization;
    free(stat);
  }
  
  return convert_return_code(ret);
}

int32_t KDDriver::get_total_size(uint64_t *dev_capa) {

  int ret = 0;

  kv_device *devinfo = (kv_device *)malloc(sizeof(kv_device));
  ret = kv_get_device_info(devH, devinfo);
  
  if (ret) {
    fprintf(stdout, "The host failed to communicate with the deivce: 0x%x", convert_return_code(ret));
    if(devinfo) free(devinfo);
    return convert_return_code(ret);
  }

  if(devinfo){
    *dev_capa = devinfo->capacity;
    free(devinfo);
  }
  return convert_return_code(ret);
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

// open/close multiple devices - callback thread will only be destroyed when the last thread is done.
// callback thread should handle multiple devices

KDDriver::~KDDriver() {
  if (kv_delete_queue(this->devH, this->sqH) != KV_SUCCESS) {
    fprintf(stderr, "kv delete submission queue failed\n");
  }

  if (kv_delete_queue(this->devH, this->cqH) != KV_SUCCESS) {
    fprintf(stderr, "kv delete completion queue failed\n");
  }

  kv_delete_namespace(devH, nsH);
  kv_cleanup_device(devH);
}

KDDriver::kv_kdd_context* KDDriver::prep_io_context(kvs_context opcode, kvs_key_space_handle ks_hd,
  const kvs_key *key, const kvs_value *value, void *private1, void *private2,
  bool syncio, kvs_postprocess_function cbfn){
  kv_kdd_context *ctx = new kv_kdd_context();
  ctx->owner = this;
  ctx->iocb.context = opcode;
  ctx->iocb.ks_hd = ks_hd;
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
  ctx->iocb.result_buffer.iter_list = NULL;
  ctx->iocb.result_buffer.list = NULL;
  ctx->on_complete = cbfn;

  ctx->done= false;
  ctx->syncio = syncio;
  
  return ctx;
}

//translate iterator type from device to kvs
int32_t KDDriver::trans_iter_type(uint8_t dev_it_type, uint8_t* kvs_it_type){
  if(kvs_it_type == NULL)
    return KVS_ERR_PARAM_INVALID;
 
  int ret = KVS_SUCCESS;
  switch(dev_it_type){
    case KV_ITERATOR_OPT_KEY:
      *kvs_it_type = KVS_ITERATOR_KEY;
      break;
    case KV_ITERATOR_OPT_KV:
      *kvs_it_type = KVS_ITERATOR_KEY_VALUE;
      break;
    default:
      ret = KVS_ERR_OPTION_INVALID;
      break;
  }
  return convert_return_code(ret);
}

int32_t KDDriver::trans_store_cmd_opt(kvs_option_store kvs_opt,
                                            kv_store_option *kv_opt){
 
    // Default: no compression
    switch(kvs_opt.st_type) {
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


float  KDDriver::get_waf(){

  uint32_t tmp_waf;
  kv_get_device_waf(devH, &tmp_waf);

  return (float) tmp_waf/10.0;
}
