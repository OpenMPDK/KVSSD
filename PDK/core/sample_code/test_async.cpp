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

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <mutex>
#include <atomic>
#include <queue>
#include <kvs_api.h>
#include <random>

#ifndef SAMSUNG_API
#define kvs_key_t uint16_t
#endif

#define DEBUG_ON 1
#define ITER_BUFFER_SIZE (32*1024)

#define SUCCESS 0
#define FAILED 1

#define WRITE_OP  1
#define READ_OP   2
#define DELETE_OP 3
#define ITERATOR_OP 4
#define KEY_EXIST_OP 5

std::atomic<int> submitted(0);
std::atomic<int> completed(0);
std::atomic<int> cur_qdepth(0);
pthread_mutex_t lock;

void usage(char *program)
{
  printf("==============\n");
  printf("usage: %s -d device_path [-n num_ios] [-q queue_depth] [-o op_type] [-k klen] [-v vlen]\n", program);
  printf("-d      device_path  :  kvssd device path. e.g. emul: /dev/kvemul; kdd: /dev/nvme0n1; udd: 0000:06:00.0\n");
  printf("-n      num_ios      :  total number of ios (ignore this for iterator)\n");
  printf("-q      queue_depth  :  queue depth (ignore this for iterator)\n");
  printf("-o      op_type      :  1: write; 2: read; 3: delete; 4: iterator;"
                                  "5: check key exist;\n");
  printf("-k      klen         :  key length (ignore this for iterator)\n");
  printf("-v      vlen         :  value length (ignore this for iterator)\n");
  printf("==============\n");
}

kvs_key* _allocate_kvskey(void *key_buff,  kvs_key_t key_len) {
  kvs_key *kvskey = (kvs_key *)malloc(sizeof(kvs_key));
  if(!kvskey) {
    fprintf(stderr, "failed to allocate\n");
    return NULL;
  }
  kvskey->key = key_buff;
  kvskey->length = key_len;
  return kvskey;
}

kvs_value* _allocate_kvsvalue(void *val_buff, uint32_t val_len) {
  kvs_value *kvsvalue = (kvs_value *)malloc(sizeof(kvs_value));
  if(!kvsvalue) {
    fprintf(stderr, "failed to allocate\n");
    return NULL;
  }
  kvsvalue->value = val_buff;
  kvsvalue->length = val_len;
  kvsvalue->actual_value_size = kvsvalue->offset = 0;
  return kvsvalue;
}

bool _allocate_kvs_pair(void *key_buff, kvs_key_t key_len, kvs_key **kvskey,
  void *val_buff, uint32_t val_len, kvs_value **kvsvalue) {
  *kvskey = _allocate_kvskey(key_buff, key_len);
  if(!(*kvskey))
    return false;
  *kvsvalue = _allocate_kvsvalue(val_buff, val_len);
  if(!(*kvsvalue)){
    free(*kvskey);
    return false;
  }
  return true;
}

void _free_str_pool(std::queue<char *> *pool, pthread_mutex_t *pool_lock){
  pthread_mutex_lock(pool_lock);
  while(!pool->empty()) {
      auto p = pool->front();
      pool->pop();
      kvs_free(p);
  }
  pthread_mutex_unlock(pool_lock);
}

// malloc string pool
bool _malloc_str_pool(uint32_t pool_size, std::queue<char *> *pool,
  pthread_mutex_t *pool_lock, uint32_t str_len){
  for (uint32_t i = 0; i < pool_size; i++) {
    char *keymem = (char*)kvs_malloc(str_len, 4096);
    if(!keymem) {
      _free_str_pool(pool, pool_lock);
      return false;
    }
    memset(keymem, 0, str_len);  
    pool->push(keymem);
  }
  return true;
}

// If value pool NULL inputted, will not malloc value pool.
bool _malloc_kv_pool(uint32_t pool_size, std::queue<char *> *key_pool,
  uint32_t key_len, std::queue<char*> *val_pool, uint32_t val_len,
  pthread_mutex_t *pool_lock){
  if( !_malloc_str_pool(pool_size, key_pool, pool_lock, key_len)) {
    return false;
  }

  if(val_pool) {
    if( !_malloc_str_pool(pool_size, val_pool, pool_lock, val_len)) {
      _free_str_pool(key_pool, pool_lock);
      return false;
    }
  }
  return true;
}

void _free_kv_pool(std::queue<char *> *key_pool,
  std::queue<char *> *val_pool, pthread_mutex_t *pool_lock) {
  _free_str_pool(key_pool, pool_lock);
  if(val_pool)
    _free_str_pool(val_pool, pool_lock);
}

void _free_kvs_pair(kvs_key *key, kvs_value *value,
  std::queue<char *> *keypool, std::queue<char *> *valpool,
  pthread_mutex_t *pool_lock) {
  pthread_mutex_lock(pool_lock);
  if(key) {
    if(key->key) {
        memset((char*)key->key, 0 ,key->length);
        keypool->push((char*)key->key);
    }
    free(key);
  }
  if(value) {
    if(value->value) {
        memset((char*)value->value, 0 ,value->length);
        valpool->push((char*)value->value);
    }
    free(value);
  }
  pthread_mutex_unlock(pool_lock);
}

void _free_kv_buff(void *key_buff, void *val_buff,
  std::queue<char *> *keypool, std::queue<char *> *valpool,
  pthread_mutex_t *pool_lock) {
  pthread_mutex_lock(pool_lock);
  if(key_buff)
    keypool->push((char*)key_buff);
  if(val_buff)
    keypool->push((char*)val_buff);
  pthread_mutex_unlock(pool_lock);
}

double _calc_time_span(struct timespec start_time) {
  struct timespec curr_time;
  clock_gettime(CLOCK_REALTIME, &curr_time);
  unsigned long long start, end;
  start = start_time.tv_sec * 1000000000L + start_time.tv_nsec;
  end = curr_time.tv_sec * 1000000000L + curr_time.tv_nsec;
  return (double)(end - start) / 1000000000L;
}

#ifdef SAMSUNG_API

struct iterator_info{
  kvs_iterator_handle iter_handle;
  kvs_iterator_list iter_list;
  kvs_iterator_option g_iter_mode;
};

void print_iterator_keyvals(kvs_iterator_list *iter_list, kvs_iterator_option  g_iter_mode){
  uint8_t *it_buffer = (uint8_t *) iter_list->it_list;
  uint32_t i;

  if(g_iter_mode.iter_type) {
    // key and value iterator (KVS_ITERATOR_KEY_VALUE)
    uint32_t vlen = sizeof(kvs_value_t);
    uint32_t vlen_value = 0;
      
    for(i = 0;i < iter_list->num_entries; i++) {
      fprintf(stdout, "Iterator get %dth key: %s\n", i, it_buffer);
      it_buffer += 16;

      uint8_t *addr = (uint8_t *)&vlen_value;
      for (unsigned int i = 0; i < vlen; i++) {
         *(addr + i) = *(it_buffer + i);
      }

      it_buffer += vlen;
      it_buffer += vlen_value;
    }

  } else {
    // for ETA50K24 firmware with various key length       
    uint32_t key_size = 0;
    char key[256];

    for(i = 0;i < iter_list->num_entries; i++) {
      // get key size
      key_size = *((unsigned int*)it_buffer);
      it_buffer += sizeof(unsigned int);

      // print key
      memcpy(key, it_buffer, key_size);
      key[key_size] = 0;
      fprintf(stdout, "%dth key --> %s, size = %d\n", i, key, key_size);

      it_buffer += key_size;
    }
  }
}

void _iterator_complete_handle(kvs_callback_context* ioctx) {
  if (ioctx->result != KVS_SUCCESS) {
    fprintf(stderr, "ERROR io_complete: iterator result=0x%x, err= %s\n",
       ioctx->result, kvs_errstr(ioctx->result));
    return;
  } 
  struct iterator_info *iter_info;
  iter_info = (struct iterator_info *)ioctx->private1;
#if DEBUG_ON
  fprintf(stdout, "io_complete: op=%d, end=%d, num entries=%d, size=%d, result=%s\n",
    ioctx->opcode, iter_info->iter_list.end,iter_info->iter_list.num_entries,
    iter_info->iter_list.size, kvs_errstr(ioctx->result));
  iter_info->g_iter_mode.iter_type = KVS_ITERATOR_KEY;
  print_iterator_keyvals(&iter_info->iter_list, iter_info->g_iter_mode);
#endif
}

void _key_exist_complete_handle(kvs_callback_context* ioctx) {
  if (ioctx->result != 0 && ioctx->result != KVS_ERR_KEY_NOT_EXIST) {
    fprintf(stderr, "ERROR io_complete: key exist, key = %s, result = 0x%x, err = %s\n",
      ioctx->key ? (char*)ioctx->key->key:0, ioctx->result,
      kvs_errstr(ioctx->result));
    return;
  } 

  uint8_t *exist;
  exist = (uint8_t*)ioctx->result_buffer;
#if DEBUG_ON
  fprintf(stdout, "key %s exist? %s\n", (char*)ioctx->key->key,
          *exist == 0? "FALSE":"TRUE");
#endif
  if(ioctx->result_buffer)
    free(ioctx->result_buffer);

  std::queue<char*> *keypool = (std::queue<char*> *)ioctx->private1;
  _free_kvs_pair(ioctx->key, NULL, keypool, NULL, &lock);
}

void _delete_complete_handle(kvs_callback_context* ioctx) {
  if(ioctx->result != KVS_SUCCESS) {
    fprintf(stdout, "ERROR io_complete: delete . result=%d key=%s err = %s \n",
    ioctx->result, (char*)ioctx->key->key, kvs_errstr(ioctx->result));
  }
  std::queue<char*> *keypool = (std::queue<char*> *)ioctx->private1;
  _free_kvs_pair(ioctx->key, NULL, keypool, NULL, &lock);
}

void _default_complete_handle(kvs_callback_context* ioctx) {
  if(ioctx->result != KVS_SUCCESS) {
    fprintf(stdout, "ERROR io_complete: op=%d. result=%d key=%s err = %s \n",
    ioctx->opcode, ioctx->result, (char*)ioctx->key->key,
    kvs_errstr(ioctx->result));
  }
  std::queue<char*> *keypool = (std::queue<char*> *)ioctx->private1;
  std::queue<char*> *valuepool = (std::queue<char*> *)ioctx->private2;
  _free_kvs_pair(ioctx->key, ioctx->value, keypool, valuepool, &lock);
}

void complete(kvs_callback_context* ioctx) {
  switch(ioctx->opcode) {
    case IOCB_ASYNC_ITER_NEXT_CMD:
      _iterator_complete_handle(ioctx);
      break;
    case IOCB_ASYNC_DEL_CMD:
      _delete_complete_handle(ioctx);
      break;
    case IOCB_ASYNC_CHECK_KEY_EXIST_CMD:
      _key_exist_complete_handle(ioctx);
      break;
    case IOCB_ASYNC_PUT_CMD:
    case IOCB_ASYNC_GET_CMD:
      _default_complete_handle(ioctx);
      break;
    default:
      fprintf(stdout, "ERROR io_complete unknow op = %d, result=%d.\n",
        ioctx->opcode, ioctx->result);
      break;
  }

  completed++;
  cur_qdepth--;
  if(completed.load() % 1000 == 0)
    fprintf(stdout, "completed io count: %d\n", completed.load());
}

// Samsung device support KVS_ITERATOR_KEY iterator type only
int perform_iterator(kvs_container_handle cont_hd,
                      kvs_iterator_type iter_type=KVS_ITERATOR_KEY)
{

  int count = KVS_MAX_ITERATE_HANDLE;
  kvs_iterator_info kvs_iters[count];

  struct iterator_info *iter_info = (struct iterator_info *)malloc(sizeof(struct iterator_info));
  iter_info->g_iter_mode.iter_type = iter_type;

  int ret;
  static int total_entries = 0;

  fprintf(stdout, "\n===========\n");
  fprintf(stdout, "   Do Iterate Operation\n");
  fprintf(stdout, "===========\n"); 
  
  /* Open iterator */
  kvs_iterator_context iter_ctx_open;
 
  iter_ctx_open.bitmask = 0xffff0000;
  char prefix_str[5] = "0000";
  unsigned int PREFIX_KV = 0;
  for (int i = 0; i < 4; i++){
    PREFIX_KV |= (prefix_str[i] << (3-i)*8);
  }

  iter_ctx_open.bit_pattern = PREFIX_KV;
  iter_ctx_open.private1 = NULL;
  iter_ctx_open.private2 = NULL;
  memset(&iter_ctx_open.option, 0, sizeof(kvs_iterator_option));
  iter_ctx_open.option.iter_type = iter_type;

  submitted = completed = 0;

  //kvs_close_iterator_all(cont_hd);
  ret = kvs_open_iterator(cont_hd, &iter_ctx_open, &iter_info->iter_handle); 
  if(ret) {
    fprintf(stdout, "open iter failed with err %s\n", kvs_errstr(ret));
    free(iter_info);
    return FAILED;
  }
  
  memset(kvs_iters, 0, sizeof(kvs_iters));
  int res = kvs_list_iterators(cont_hd, kvs_iters, count);
  if(res) {
    printf("kv_list_iterators with error: 0x%X\n", res);
    free(iter_info);
    return FAILED;
  } else {
    for (int j = 0; j < count; j++){
      if(kvs_iters[j].status == 1)
        fprintf(stdout, "found handler %d 0x%x 0x%x 0x%x\n",
          kvs_iters[j].iter_handle,
          kvs_iters[j].type,
          kvs_iters[j].bit_pattern,
          kvs_iters[j].bitmask);
    }
  }

  /* Do iteration */
  submitted = completed = 0;
  iter_info->iter_list.size = ITER_BUFFER_SIZE;
  uint8_t *buffer;
  buffer =(uint8_t*) kvs_malloc(ITER_BUFFER_SIZE, 4096);
  memset(buffer, 0, ITER_BUFFER_SIZE);
  iter_info->iter_list.it_list = (uint8_t*) buffer;

  kvs_iterator_context iter_ctx_next;
  iter_ctx_next.private1 = iter_info;
  iter_ctx_next.private2 = NULL;

  iter_info->iter_list.end = 0;
  iter_info->iter_list.num_entries = 0;

  struct timespec start_time;
  clock_gettime(CLOCK_REALTIME, &start_time);
  while(1) {
    iter_info->iter_list.size = ITER_BUFFER_SIZE;
    memset(iter_info->iter_list.it_list, 0, ITER_BUFFER_SIZE);
    ret = kvs_iterator_next_async(cont_hd, iter_info->iter_handle, &iter_info->iter_list, &iter_ctx_next, complete);
    if(ret != KVS_SUCCESS) {
      fprintf(stderr, "iterator next fails with error 0x%x - %s\n", ret, kvs_errstr(ret));
      kvs_free(buffer);
      free(iter_info);
      return FAILED;
    }
        
    submitted++;
    while(completed < submitted)
      usleep(1);
      
    total_entries += iter_info->iter_list.num_entries;
    //print_iterator_keyvals(&iter_info->iter_list, iter_info->g_iter_mode);

    if(iter_info->iter_list.end) {
      fprintf(stdout, "Done with all keys. Total: %d\n", total_entries);
      break;
    } else {
      fprintf(stdout, "More keys available, do another iteration\n");
      memset(iter_info->iter_list.it_list, 0,  ITER_BUFFER_SIZE);
    }
  }
  
  double timespan_sec = _calc_time_span(start_time);
  fprintf(stdout, "Total time %.2f sec; Throughput %.2f ops/sec\n",
    timespan_sec, (double)count /timespan_sec );

  /* Close iterator */
  kvs_iterator_context iter_ctx_close;
  iter_ctx_close.private1 = NULL;
  iter_ctx_close.private2 = NULL;

  ret = kvs_close_iterator(cont_hd, iter_info->iter_handle, &iter_ctx_close);
  if(ret != KVS_SUCCESS) {
    fprintf(stderr, "Failed to close iterator\n");
    kvs_free(buffer);
    free(iter_info);
    return FAILED;
  }

  if(buffer) kvs_free(buffer);
  if(iter_info) free(iter_info);

  return SUCCESS;
}

int perform_read(kvs_container_handle cont_hd, int count, int maxdepth, kvs_key_t klen, uint32_t vlen) {
    int num_submitted = 0;
    std::queue<char *> keypool;
    std::queue<char *> valuepool;
    completed = 0;
    if( !_malloc_kv_pool(maxdepth, &keypool, klen, &valuepool, vlen, &lock)) {
      printf("Allocate kv pool failed.\n");
      return FAILED;
    }

    fprintf(stdout, "\n===========\n");
    fprintf(stdout, "   Do Read Operation\n");
    fprintf(stdout, "===========\n");

    struct timespec start_time;
    clock_gettime(CLOCK_REALTIME, &start_time);
    long int seq = 0;
    int ret = SUCCESS;
    while (num_submitted < count) {
      while (cur_qdepth < maxdepth) {
        if (num_submitted >= count) break;
        pthread_mutex_lock(&lock);
        char *key   = keypool.front(); keypool.pop();
        char *value = valuepool.front(); valuepool.pop();
        pthread_mutex_unlock(&lock);
        if(!key || !value) {
          fprintf(stderr, "ERROR, key value memory pools exhaust.\n");
          ret = FAILED;
          goto exit;
        }
        snprintf(key, klen, "%0*ld", klen - 1, seq++);
        memset(value, 0, vlen);

        kvs_retrieve_option option;
        memset(&option, 0, sizeof(kvs_retrieve_option));
        option.kvs_retrieve_decompress = false;
        option.kvs_retrieve_delete = false;
        const kvs_retrieve_context ret_ctx = {option, &keypool, &valuepool};
        
        kvs_key *kvskey = NULL;
        kvs_value *kvsvalue = NULL;
        if( !_allocate_kvs_pair(key, klen, &kvskey, value, vlen, &kvsvalue)) {
          ret = FAILED;
          _free_kv_buff(key, value, &keypool, &valuepool, &lock);
          goto exit;
        }
        ret = kvs_retrieve_tuple_async(cont_hd, kvskey, kvsvalue, &ret_ctx, complete);
        if (ret != KVS_SUCCESS) {
          fprintf(stderr, "retrieve tuple failed with err %s 0x%x\n",  kvs_errstr(ret), ret);
          _free_kvs_pair(kvskey, kvsvalue, &keypool, &valuepool, &lock);
          goto exit;
        }
        cur_qdepth++;
        num_submitted++;
      }

      if (cur_qdepth == maxdepth) {
        usleep(1);
      }
    }

exit:
    // wait until commands that has succussfully submitted finish
    while(completed < num_submitted) {
      usleep(1);
    }
    double timespan_sec = _calc_time_span(start_time);
    fprintf(stdout, "Total time %.2f sec; Throughput %.2f ops/sec\n",
      timespan_sec, (double)count /timespan_sec );
    _free_kv_pool(&keypool, &valuepool, &lock);
    return ret;
}

int perform_insertion(kvs_container_handle cont_hd, int count, int maxdepth, kvs_key_t klen, uint32_t vlen) {
  int num_submitted = 0;
  std::queue<char *> keypool;
  std::queue<char *> valuepool;
  completed = 0;
  if( !_malloc_kv_pool(maxdepth, &keypool, klen, &valuepool, vlen, &lock)) {
    printf("Allocate kv pool failed.\n");
    return FAILED;
  }

  long int seq = 0;
  fprintf(stdout, "\n===========\n");
  fprintf(stdout, "   Do Write Operation\n");
  fprintf(stdout, "===========\n");

  struct timespec start_time;
  clock_gettime(CLOCK_REALTIME, &start_time);
  int ret = SUCCESS;
  while (num_submitted < count) {
    while (cur_qdepth < maxdepth) {
      if (num_submitted >= count) break;

      pthread_mutex_lock(&lock);
      char *key   = keypool.front(); keypool.pop();
      char *value = valuepool.front(); valuepool.pop();
      pthread_mutex_unlock(&lock);
      if(!key || !value) {
        fprintf(stderr, "ERROR, key value memory pools exhaust.\n");
        ret = FAILED;
        goto exit;
      }
      snprintf(key,  klen, "%0*ld", klen - 1, seq++);
      snprintf(value, vlen, "value%ld", seq);

      kvs_store_option option;
      memset(&option, 0, sizeof(kvs_store_option));
      option.st_type = KVS_STORE_POST;
      option.kvs_store_compress = false;
      const kvs_store_context put_ctx = {option, &keypool, &valuepool };

      kvs_key *kvskey = NULL;
      kvs_value *kvsvalue = NULL;
      if( !_allocate_kvs_pair(key, klen, &kvskey, value, vlen, &kvsvalue)) {
        _free_kv_buff(key, value, &keypool, &valuepool, &lock);
        ret = FAILED;
        goto exit;
      }
      ret = kvs_store_tuple_async(cont_hd, kvskey, kvsvalue, &put_ctx, complete);
      if (ret != KVS_SUCCESS) {
        fprintf(stderr, "store tuple failed with err %s\n", kvs_errstr(ret)); 
        _free_kvs_pair(kvskey, kvsvalue, &keypool, &valuepool, &lock);
        return FAILED;
      }

      cur_qdepth++;
      num_submitted++;
    }

    if (cur_qdepth == maxdepth) {
      usleep(1);
    }
  }

exit: 
  // wait until commands that has succussfully submitted finish
  while(completed < num_submitted) {
    usleep(1);
  }
  double timespan_sec = _calc_time_span(start_time);
  fprintf(stdout, "Total time %.2f sec; Throughput %.2f ops/sec\n",
    timespan_sec, (double)count /timespan_sec );
  _free_kv_pool(&keypool, &valuepool, &lock);
  return SUCCESS;
}

int perform_delete(kvs_container_handle cont_hd, int count, int maxdepth, kvs_key_t klen, uint32_t vlen) {
  int num_submitted = 0;
  std::queue<char *> keypool;
  completed = 0;
  if( !_malloc_kv_pool(maxdepth, &keypool, klen, NULL, 0, &lock)) {
    printf("Allocate kv pool failed.\n");
    return FAILED;
  }

  long int seq = 0;
  fprintf(stdout, "\n===========\n");
  fprintf(stdout, "   Do Delete Operation\n");
  fprintf(stdout, "===========\n"); 

  struct timespec start_time;
  clock_gettime(CLOCK_REALTIME, &start_time);
  int ret = SUCCESS;
  while (num_submitted < count) {
    while (cur_qdepth < maxdepth) {
      if (num_submitted >= count) break;
      pthread_mutex_lock(&lock);
      char *key   = keypool.front(); keypool.pop();
      pthread_mutex_unlock(&lock);
      if(!key) {
        fprintf(stderr, "ERROR, key memory pools exhaust.\n");
        ret = FAILED;
        goto exit;
      }
      snprintf(key, klen, "%0*ld", klen - 1, seq++);
      kvs_key *kvskey = _allocate_kvskey(key, klen);
      if(!kvskey) {
        _free_kv_buff(key, NULL, &keypool, NULL, &lock);
        ret = FAILED;
        goto exit;
      }
      kvs_delete_option option;
      option.kvs_delete_error = false;
      const kvs_delete_context del_ctx = { option, &keypool, NULL};
      ret = kvs_delete_tuple_async(cont_hd, kvskey, &del_ctx, complete);
      if (ret != KVS_SUCCESS) {
        fprintf(stderr, "delete tuple failed with err %s\n", kvs_errstr(ret));
        _free_kvs_pair(kvskey, NULL, &keypool, NULL, &lock);
        goto exit;
      }
      
      cur_qdepth++;
      num_submitted++;
    }

    if (cur_qdepth == maxdepth) {
      usleep(1);
    }
  }

exit:
  // wait until commands that has succussfully submitted finish
  while(completed < num_submitted) {
    usleep(1);
  }

  double timespan_sec = _calc_time_span(start_time);
  fprintf(stdout, "Total time %.2f sec; Throughput %.2f ops/sec\n",
    timespan_sec, (double)count /timespan_sec );
  _free_str_pool(&keypool, &lock);
  return SUCCESS;
}

int perform_key_exist(kvs_container_handle cont_hd, int count, int maxdepth, kvs_key_t klen, uint32_t vlen ) {

  int num_submitted = 0;
  std::queue<char *> keypool;
  completed = 0;
  if( !_malloc_kv_pool(maxdepth, &keypool, klen, NULL, 0, &lock)) {
    printf("Allocate kv pool failed.\n");
    return FAILED;
  }

  long int seq = 0;
  fprintf(stdout, "\n===========\n");
  fprintf(stdout, "   Do key exist check Operation\n");
  fprintf(stdout, "===========\n");

  struct timespec start_time;
  clock_gettime(CLOCK_REALTIME, &start_time);
  int ret = SUCCESS;
  while (num_submitted < count) {
    while (cur_qdepth < maxdepth) {
      if (num_submitted >= count) break;
      pthread_mutex_lock(&lock);
      char *key   = keypool.front(); keypool.pop();
      pthread_mutex_unlock(&lock);
      if(!key) {
        fprintf(stderr, "ERROR, key memory pools exhaust.\n");
        ret = FAILED;
        goto exit;
      }
      snprintf(key,  klen, "%0*ld", klen - 1, seq++);
      kvs_key *kvskey = _allocate_kvskey(key, klen);
      if(!kvskey) {
        _free_kv_buff(key, NULL, &keypool, NULL, &lock);
        ret = FAILED;
        goto exit;
      }

      const kvs_exist_context exist_ctx = {&keypool, NULL};
      uint8_t *status = (uint8_t*)malloc(sizeof(uint8_t));
      ret = kvs_exist_tuples_async(cont_hd, 1, kvskey, 1, status, &exist_ctx, complete);
      if (ret != KVS_SUCCESS) {
        fprintf(stderr, "exit tuple failed with err %s\n", kvs_errstr(ret));
        _free_kvs_pair(kvskey, NULL, &keypool, NULL, &lock);
        goto exit;
      }

      cur_qdepth++;
      num_submitted++;
    }

    if (cur_qdepth == maxdepth) {
      usleep(1);
    }
  }

exit:
  // wait until commands that has succussfully submitted finish
  while(completed < num_submitted) {
    usleep(1);
  }

  double timespan_sec = _calc_time_span(start_time);
  fprintf(stdout, "Total time %.2f sec; Throughput %.2f ops/sec\n",
    timespan_sec, (double)count /timespan_sec );
  _free_str_pool(&keypool, &lock);
  return SUCCESS;
}

int _env_exit(kvs_device_handle dev, const char* cont_name,
  kvs_container_handle cont_handle) {
  int32_t dev_util = 0;
  kvs_get_device_utilization(dev, &dev_util);
  fprintf(stdout, "After: Total used is %d\n", dev_util);
  kvs_close_container(cont_handle);
  kvs_delete_container(dev, cont_name);
  kvs_close_device(dev);
  kvs_result ret = kvs_exit_env();
  return ret;
}

int _env_init(kvs_init_options* options, char* dev_path, kvs_device_handle* dev,
  const char *cont_name, kvs_container_handle* cont_handle) {
  // initialize the environment
  kvs_init_env(options);
  kvs_result ret = kvs_open_device(dev_path, dev);
  if(ret != KVS_SUCCESS) {
    fprintf(stderr, "Device open failed\n");
    return FAILED;
  }

  //container list before create "test"
  uint32_t valid_cnt = 0;
  const uint32_t retrieve_cnt = 2;
  kvs_container_name names[retrieve_cnt];
  for(uint8_t idx = 0; idx < retrieve_cnt; idx++) {
    names[idx].name_len = MAX_CONT_PATH_LEN;
    names[idx].name = (char*)malloc(MAX_CONT_PATH_LEN);
  }
  ret = kvs_list_containers(*dev, 1, retrieve_cnt*sizeof(kvs_container_name),
    names, &valid_cnt);
  if(ret != KVS_SUCCESS) {
    fprintf(stderr, "List current containers failed. error:0x%x.\n", ret);
    kvs_close_device(*dev);
    return FAILED;
  }
  for (uint8_t idx = 0; idx < valid_cnt; idx++) {
    kvs_delete_container(*dev, names[idx].name);
  }

  kvs_container_context ctx;
  // Initialize key order to KVS_KEY_ORDER_NONE
  ctx.option.ordering = KVS_KEY_ORDER_NONE;
  ret = kvs_create_container(*dev, cont_name, 0, &ctx);
  if (ret != KVS_SUCCESS) {
    fprintf(stderr, "Create containers failed. error:0x%x.\n", ret);
    kvs_close_device(*dev);
    return FAILED;
  }

  ret = kvs_open_container(*dev, cont_name, cont_handle);
  if(ret != KVS_SUCCESS) {
    fprintf(stderr, "Open containers %s failed. error:0x%x.\n", cont_name, ret);
    kvs_delete_container(*dev, cont_name);
    kvs_close_device(*dev);
    return FAILED;
  }

  char *name_buff = (char*)malloc(MAX_CONT_PATH_LEN);
  kvs_container_name name = {MAX_CONT_PATH_LEN, name_buff};
  kvs_container cont = {false, 0, 0, 0, 0, &name};
  ret = kvs_get_container_info(*cont_handle, &cont);
  if(ret != KVS_SUCCESS) {
    fprintf(stderr, "Get info of containers %s failed. error:0x%x.\n", name.name, ret);
    _env_exit(*dev, cont_name, *cont_handle);
    return FAILED;
  }

  fprintf(stdout, "Container information get name: %s\n", cont.name->name);
  fprintf(stdout, "open:%d, scale:%d, capacity:%ld, free_size:%ld count:%ld.\n", 
    cont.opened, cont.scale, cont.capacity, cont.free_size, cont.count);
  free(name_buff);

  int32_t dev_util = 0;
  int64_t dev_capa = 0;
  kvs_get_device_utilization(*dev, &dev_util);

  float waf = 0.0;
  kvs_get_device_waf(*dev, &waf);
  kvs_get_device_capacity(*dev, &dev_capa);
  fprintf(stdout, "Before: Total size is %ld bytes, used is %d, waf is %.2f\n", dev_capa, dev_util, waf);
  
  kvs_device *dev_info = (kvs_device*)malloc(sizeof(kvs_device));
  if(dev_info){
    kvs_get_device_info(*dev, dev_info);
    fprintf(stdout, "Total size: %.2f GB\nMax value size: %d\nMax key size: %d\n"
      "Optimal value size: %d\n", (float)dev_info->capacity/1000/1000/1000,
    dev_info->max_value_len, dev_info->max_key_len, dev_info->optimal_value_len);
    free(dev_info);
  }else{
    fprintf(stderr, "dev_info malloc failed\n");
    _env_exit(*dev, cont_name, *cont_handle);
    return FAILED;
  }

  return SUCCESS;
}

int main(int argc, char *argv[]) {
  char* dev_path = NULL;
  int num_ios = 10;
  int qdepth = 64;
  int op_type = 1;
  kvs_key_t klen = 16;
  uint32_t vlen = 4096;
  //int is_polling = 0;
  int c;
  int ret;

  while ((c = getopt(argc, argv, "d:n:q:o:k:v:h")) != -1) {
    switch(c) {
    case 'd':
      dev_path = optarg;
      break;
    case 'n':
      num_ios = atoi(optarg);
      break;
    case 'q':
      qdepth = atoi(optarg);
      break;
    case 'o':
      op_type = atoi(optarg);
      break;
    case 'k':
      klen = atoi(optarg);
      break;
    case 'v':
      vlen = atoi(optarg);
      break;
    case 'h':
      usage(argv[0]);
      return FAILED;
    default:
      usage(argv[0]);
      return SUCCESS;
    }
  }

  if(dev_path == NULL) {
    fprintf(stderr, "Please specify KV SSD device path\n");
    usage(argv[0]);
    return SUCCESS;
  }

  kvs_init_options options;
  kvs_init_env_opts(&options);

  char qolon = ':';//character to search
  char *found;
  found = strchr(dev_path, qolon);
  
  options.aio.iocoremask = 0;
  options.memory.use_dpdk = 0;
  options.aio.queuedepth = qdepth;
  const char *configfile = "../kvssd_emul.conf";
  options.emul_config_file =  configfile;
  
  if(found) { // spdk driver
    options.memory.use_dpdk = 1;
    char *core;
    core = options.udd.core_mask_str;
   *core = '0';
    core = options.udd.cq_thread_mask;
    *core = '0';
    options.udd.mem_size_mb = 1024;
    options.udd.syncio = 0; // use async IO mode
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset); // CPU 0
    sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);    
    
  } else {
    options.memory.use_dpdk = 0;
  }

  const char *cont_name = "test";
  kvs_device_handle dev;
  kvs_container_handle cont_handle;
  if(_env_init(&options, dev_path, &dev, cont_name, &cont_handle) != SUCCESS)
    return FAILED;

  switch(op_type) {
  case WRITE_OP:
    perform_insertion(cont_handle, num_ios, qdepth, klen, vlen);
    break;
  case READ_OP:
    //perform_insertion(cont_handle, num_ios, qdepth, klen, vlen);
    perform_read(cont_handle, num_ios, qdepth, klen, vlen);
    break;
  case DELETE_OP:
    //perform_insertion(cont_handle, num_ios, qdepth, klen, vlen);
    perform_delete(cont_handle, num_ios, qdepth, klen, vlen);
    break;
  case ITERATOR_OP:
    //perform_insertion(cont_handle, num_ios, qdepth, klen, vlen);
    perform_iterator(cont_handle);
    break;
  case KEY_EXIST_OP:
    //perform_insertion(cont_handle, num_ios, qdepth,  klen, vlen);
    perform_key_exist(cont_handle, num_ios, qdepth,  klen, vlen);
    break;
  default:
    fprintf(stderr, "Please specify a correct op_type for testing\n");
    ret = FAILED;
  }

  _env_exit(dev, cont_name, cont_handle);

  return ret;
}

#else

std::queue<char*> keypool;
std::queue<char*> valuepool;

void print_iterator_keyvals(kvs_iterator_list *iter_list, kvs_option_iterator g_iter_mode){
  uint8_t *it_buffer = (uint8_t *) iter_list->it_list;
  uint32_t i;

  if(g_iter_mode.iter_type) {
    // key and value iterator (KVS_ITERATOR_KEY_VALUE)
    uint32_t vlen = sizeof(uint32_t);
    uint32_t vlen_value = 0;
      
    for(i = 0;i < iter_list->num_entries; i++) {
      fprintf(stdout, "Iterator get %dth key: %s\n", i, it_buffer);
      it_buffer += 16;

      uint8_t *addr = (uint8_t *)&vlen_value;
      for (unsigned int i = 0; i < vlen; i++) {
         *(addr + i) = *(it_buffer + i);
      }

      it_buffer += vlen;
      it_buffer += vlen_value;
    }

  } else {
    // for ETA50K24 firmware with various key length       
    uint32_t key_size = 0;
    char key[256];

    for(i = 0;i < iter_list->num_entries; i++) {
      // get key size
      key_size = *((unsigned int*)it_buffer);
      it_buffer += sizeof(unsigned int);

      // print key
      memcpy(key, it_buffer, key_size);
      key[key_size] = 0;
      fprintf(stdout, "%dth key --> %s, size = %d\n", i, key, key_size);

      it_buffer += key_size;
    }
  }
}

void _iterator_complete_handle(kvs_postprocess_context* ioctx) {
  if (ioctx->result != KVS_SUCCESS) {
    fprintf(stderr, "ERROR io_complete: iterator result=0x%x\n",
       ioctx->result);
    return;
  }
  kvs_iterator_list* iter_list = ioctx->result_buffer.iter_list;
#if DEBUG_ON
  fprintf(stdout, "io_complete: op=%d, end=%d, num entries=%d, size=%d, result=0x%x\n",
    ioctx->context, iter_list->end,iter_list->num_entries,
    iter_list->size, ioctx->result);
  kvs_option_iterator option = {KVS_ITERATOR_KEY};
  print_iterator_keyvals(iter_list, option);
#endif
}

void _key_exist_complete_handle(kvs_postprocess_context* ioctx) {
  if (ioctx->result != 0) {
    fprintf(stderr, "ERROR io_complete: key exist, key = %s, result = 0x%x\n",
      ioctx->key ? (char*)ioctx->key->key:0, ioctx->result);
    return;
  } 

  uint8_t *exist;
  exist = (uint8_t*)ioctx->result_buffer.list->result_buffer;
#if DEBUG_ON
  fprintf(stdout, "key %s exist? %s\n", (char*)ioctx->key->key,
          *exist == 0? "FALSE":"TRUE");
#endif
  if(ioctx->result_buffer.list->result_buffer)
    free(ioctx->result_buffer.list->result_buffer);
  if(ioctx->result_buffer.list)
    free(ioctx->result_buffer.list);

  _free_kvs_pair(ioctx->key, NULL, &keypool, NULL, &lock);
}

void _delete_complete_handle(kvs_postprocess_context* ioctx) {
  if(ioctx->result != KVS_SUCCESS) {
    fprintf(stdout, "ERROR io_complete: delete . result=%d key=%s\n",
    ioctx->result, (char*)ioctx->key->key);
  }
  _free_kvs_pair(ioctx->key, NULL, &keypool, NULL, &lock);
}

void _default_complete_handle(kvs_postprocess_context* ioctx) {
  if(ioctx->result != KVS_SUCCESS) {
    fprintf(stdout, "ERROR io_complete: op=%d. result=%d key=%s\n",
    ioctx->context, ioctx->result, (char*)ioctx->key->key);
  }
  _free_kvs_pair(ioctx->key, ioctx->value, &keypool, &valuepool, &lock);
}

void complete(kvs_postprocess_context* ioctx) {
  switch(ioctx->context) {
    case KVS_CMD_ITER_NEXT:
      _iterator_complete_handle(ioctx);
      break;
    case KVS_CMD_DELETE:
      _delete_complete_handle(ioctx);
      break;
    case KVS_CMD_EXIST:
      _key_exist_complete_handle(ioctx);
      break;
    case KVS_CMD_STORE:
    case KVS_CMD_RETRIEVE:
      _default_complete_handle(ioctx);
      break;
    default:
      fprintf(stdout, "ERROR io_complete unknow op = %d, result=0x%x.\n",
        ioctx->context, ioctx->result);
      break;
  }

  completed++;
  cur_qdepth--;
  if(completed.load() % 1000 == 0)
    fprintf(stdout, "completed io count: %d\n", completed.load());
}

int perform_iterator(kvs_key_space_handle ks_hd, kvs_option_iterator iter_op={KVS_ITERATOR_KEY})
{
  int count = KVS_MAX_ITERATE_HANDLE;

  int ret;
  static int total_entries = 0;

  fprintf(stdout, "\n===========\n");
  fprintf(stdout, "   Do Iterate Operation\n");
  fprintf(stdout, "===========\n"); 
  
  /* Open iterator */
  kvs_key_group_filter iter_fltr;
  kvs_iterator_handle iter_hd;

  iter_fltr.bitmask[0] = 0xff;
  iter_fltr.bitmask[1] = 0xff;
  iter_fltr.bitmask[2] = 0;
  iter_fltr.bitmask[3] = 0;

  iter_fltr.bit_pattern[0] = '0';
  iter_fltr.bit_pattern[1] = '0';
  iter_fltr.bit_pattern[2] = '0';
  iter_fltr.bit_pattern[3] = '0';


  submitted = completed = 0;

  //kvs_close_iterator_all(cont_hd);
  ret = kvs_create_iterator(ks_hd, &iter_op, &iter_fltr, &iter_hd);
  if(ret) {
    fprintf(stdout, "open iter failed with err 0x%x\n", ret);
    return FAILED;
  }
  
  /* Do iteration */
  kvs_iterator_list* iter_list = (kvs_iterator_list*)malloc(sizeof(kvs_iterator_list));
  if(iter_list == NULL){
    return FAILED;
  }
  iter_list->size = ITER_BUFFER_SIZE;
  uint8_t *buffer;
  buffer =(uint8_t*) kvs_malloc(ITER_BUFFER_SIZE, 4096);
  memset(buffer, 0, ITER_BUFFER_SIZE);
  iter_list->it_list = (uint8_t*) buffer;
  iter_list->end = false;
  iter_list->num_entries = 0;

  struct timespec start_time;
  clock_gettime(CLOCK_REALTIME, &start_time);
  while(1) {
    iter_list->size = ITER_BUFFER_SIZE;
    memset(iter_list->it_list, 0, ITER_BUFFER_SIZE);
    ret = kvs_iterate_next_async(ks_hd, iter_hd, iter_list, complete);
    if(ret != KVS_SUCCESS) {
      fprintf(stderr, "iterator next fails with error 0x%x\n", ret);
      if( (ret = kvs_delete_iterator(ks_hd, iter_hd)) != KVS_SUCCESS)
        fprintf(stderr, "delte iterator after next fails with error 0x%x\n", ret);
      kvs_free(buffer);
      if(iter_list) free(iter_list);
      return FAILED;
    }
        
    submitted++;
    while(completed < submitted)
      usleep(1);
      
    total_entries += iter_list->num_entries;
    //print_iterator_keyvals(&iter_info->iter_list, iter_info->g_iter_mode);

    if(iter_list->end) {
      fprintf(stdout, "Done with all keys. Total: %d\n", total_entries);
      break;
    } else {
      fprintf(stdout, "More keys available, do another iteration\n");
      memset(iter_list->it_list, 0, ITER_BUFFER_SIZE);
    }
  }
  
  double timespan_sec = _calc_time_span(start_time);
  fprintf(stdout, "Total time %.2f sec; Throughput %.2f ops/sec\n",
    timespan_sec, (double)count /timespan_sec );

  /* Close iterator */
  ret = kvs_delete_iterator(ks_hd, iter_hd);
  if(ret != KVS_SUCCESS) {
    fprintf(stderr, "Failed to close iterator\n");
    kvs_free(buffer);
    if(iter_list) free(iter_list);
    return FAILED;
  }

  if(buffer) kvs_free(buffer);
  if(iter_list) free(iter_list);
  return SUCCESS;
}

int perform_read(kvs_key_space_handle ks_hd, int count, int maxdepth, uint16_t klen, uint32_t vlen) {
    int num_submitted = 0;
    completed = 0;
    if( !_malloc_kv_pool(maxdepth, &keypool, klen, &valuepool, vlen, &lock)) {
      printf("Allocate kv pool failed.\n");
      return FAILED;
    }

    fprintf(stdout, "\n===========\n");
    fprintf(stdout, "   Do Read Operation\n");
    fprintf(stdout, "===========\n");

    struct timespec start_time;
    clock_gettime(CLOCK_REALTIME, &start_time);
    long int seq = 0;
    int ret = SUCCESS;
    while (num_submitted < count) {
      while (cur_qdepth < maxdepth) {
        if (num_submitted >= count) break;
        pthread_mutex_lock(&lock);
        char *key   = keypool.front(); keypool.pop();
        char *value = valuepool.front(); valuepool.pop();
        pthread_mutex_unlock(&lock);
        if(!key || !value) {
          fprintf(stderr, "ERROR, key value memory pools exhaust.\n");
          ret = FAILED;
          goto exit;
        }
        snprintf(key,  klen, "%0*ld", klen - 1, seq++);
        memset(value, 0, vlen);

        kvs_option_retrieve option = {false};
        
        kvs_key *kvskey = NULL;
        kvs_value *kvsvalue = NULL;
        if( !_allocate_kvs_pair(key, klen, &kvskey, value, vlen, &kvsvalue)) {
          ret = FAILED;
          _free_kv_buff(key, value, &keypool, &valuepool, &lock);
          goto exit;
        }
        ret = kvs_retrieve_kvp_async(ks_hd, kvskey, &option, kvsvalue, complete);
        if (ret != KVS_SUCCESS) {
          fprintf(stderr, "retrieve tuple failed with err 0x%x\n", ret);
          _free_kvs_pair(kvskey, kvsvalue, &keypool, &valuepool, &lock);
          goto exit;
        }
        cur_qdepth++;
        num_submitted++;
      }

      if (cur_qdepth == maxdepth) {
        usleep(1);
      }
    }

exit:
    // wait until commands that has succussfully submitted finish
    while(completed < num_submitted) {
      usleep(1);
    }
    double timespan_sec = _calc_time_span(start_time);
    fprintf(stdout, "Total time %.2f sec; Throughput %.2f ops/sec\n",
      timespan_sec, (double)count /timespan_sec );
    _free_kv_pool(&keypool, &valuepool, &lock);
    return ret;
}

int perform_insertion(kvs_key_space_handle ks_hd, int count, int maxdepth, uint16_t klen, uint32_t vlen) {
  int num_submitted = 0;
  completed = 0;
  if( !_malloc_kv_pool(maxdepth, &keypool, klen, &valuepool, vlen, &lock)) {
    printf("Allocate kv pool failed.\n");
    return FAILED;
  }

  long int seq = 0;
  fprintf(stdout, "\n===========\n");
  fprintf(stdout, "   Do Write Operation\n");
  fprintf(stdout, "===========\n");

  struct timespec start_time;
  clock_gettime(CLOCK_REALTIME, &start_time);
  int ret = SUCCESS;
  while (num_submitted < count) {
    while (cur_qdepth < maxdepth) {
      if (num_submitted >= count) break;

      pthread_mutex_lock(&lock);
      char *key   = keypool.front(); keypool.pop();
      char *value = valuepool.front(); valuepool.pop();
      pthread_mutex_unlock(&lock);
      if(!key || !value) {
        fprintf(stderr, "ERROR, key value memory pools exhaust.\n");
        ret = FAILED;
        goto exit;
      }
      snprintf(key,  klen, "%0*ld", klen - 1, seq++);
      snprintf(value, vlen, "value%ld", seq);

      kvs_option_store option = {KVS_STORE_POST, NULL};

      kvs_key *kvskey = NULL;
      kvs_value *kvsvalue = NULL;
      if( !_allocate_kvs_pair(key, klen, &kvskey, value, vlen, &kvsvalue)) {
        _free_kv_buff(key, value, &keypool, &valuepool, &lock);
        ret = FAILED;
        goto exit;
      }
      ret = kvs_store_kvp_async(ks_hd, kvskey, kvsvalue, &option, complete);
      if (ret != KVS_SUCCESS) {
        fprintf(stderr, "store tuple failed with err 0x%x\n", ret); 
        _free_kvs_pair(kvskey, kvsvalue, &keypool, &valuepool, &lock);
        return FAILED;
      }

      cur_qdepth++;
      num_submitted++;
    }

    if (cur_qdepth == maxdepth) {
      usleep(1);
    }
  }

exit:
  // wait until commands that has succussfully submitted finish
  while(completed < num_submitted) {
    usleep(1);
  }
  double timespan_sec = _calc_time_span(start_time);
  fprintf(stdout, "Total time %.2f sec; Throughput %.2f ops/sec\n",
    timespan_sec, (double)count /timespan_sec );
  _free_kv_pool(&keypool, &valuepool, &lock);
  return SUCCESS;
}

int perform_delete(kvs_key_space_handle ks_hd, int count, int maxdepth, uint16_t klen, uint32_t vlen) {
  int num_submitted = 0;
  completed = 0;
  if( !_malloc_kv_pool(maxdepth, &keypool, klen, NULL, 0, &lock)) {
    printf("Allocate kv pool failed.\n");
    return FAILED;
  }

  long int seq = 0;
  fprintf(stdout, "\n===========\n");
  fprintf(stdout, "   Do Delete Operation\n");
  fprintf(stdout, "===========\n"); 

  struct timespec start_time;
  clock_gettime(CLOCK_REALTIME, &start_time);
  int ret = SUCCESS;
  while (num_submitted < count) {
    while (cur_qdepth < maxdepth) {
      if (num_submitted >= count) break;
      pthread_mutex_lock(&lock);
      char *key   = keypool.front(); keypool.pop();
      pthread_mutex_unlock(&lock);
      if(!key) {
        fprintf(stderr, "ERROR, key memory pools exhaust.\n");
        ret = FAILED;
        goto exit;
      }
      snprintf(key, klen, "%0*ld", klen - 1, seq++);
      kvs_key *kvskey = _allocate_kvskey(key, klen);
      if(!kvskey) {
        _free_kv_buff(key, NULL, &keypool, NULL, &lock);
        ret = FAILED;
        goto exit;
      }
      kvs_option_delete option = {false};
      ret = kvs_delete_kvp_async(ks_hd, kvskey, &option, complete);
      if (ret != KVS_SUCCESS) {
        fprintf(stderr, "delete tuple failed with err 0x%x\n", ret);
        _free_kvs_pair(kvskey, NULL, &keypool, NULL, &lock);
        goto exit;
      }
      
      cur_qdepth++;
      num_submitted++;
    }

    if (cur_qdepth == maxdepth) {
      usleep(1);
    }
  }

exit:
  // wait until commands that has succussfully submitted finish
  while(completed < num_submitted) {
    usleep(1);
  }

  double timespan_sec = _calc_time_span(start_time);
  fprintf(stdout, "Total time %.2f sec; Throughput %.2f ops/sec\n",
    timespan_sec, (double)count /timespan_sec );
  _free_str_pool(&keypool, &lock);
  return SUCCESS;
}

int perform_key_exist(kvs_key_space_handle ks_hd, int count, int maxdepth, uint16_t klen, uint32_t vlen ) {
  int num_submitted = 0;
  completed = 0;
  if( !_malloc_kv_pool(maxdepth, &keypool, klen, NULL, 0, &lock)) {
    printf("Allocate kv pool failed.\n");
    return FAILED;
  }

  long int seq = 0;
  fprintf(stdout, "\n===========\n");
  fprintf(stdout, "   Do key exist check Operation\n");
  fprintf(stdout, "===========\n");

  struct timespec start_time;
  clock_gettime(CLOCK_REALTIME, &start_time);
  int ret = SUCCESS;
  while (num_submitted < count) {
    while (cur_qdepth < maxdepth) {
      if (num_submitted >= count) break;
      pthread_mutex_lock(&lock);
      char *key   = keypool.front(); keypool.pop();
      pthread_mutex_unlock(&lock);
      if(!key) {
        fprintf(stderr, "ERROR, key memory pools exhaust.\n");
        ret = FAILED;
        goto exit;
      }
      snprintf(key, klen, "%0*ld", klen - 1, seq++);
      kvs_key *kvskey = _allocate_kvskey(key, klen);
      if(!kvskey) {
        _free_kv_buff(key, NULL, &keypool, NULL, &lock);
        ret = FAILED;
        goto exit;
      }

      uint8_t *status = (uint8_t*)malloc(sizeof(uint8_t));
      kvs_exist_list *list = (kvs_exist_list*)malloc(sizeof(kvs_exist_list));
      if(list){
        list->keys = kvskey;
        list->length = 1;
        list->num_keys = 1;
        list->result_buffer = status;
      }
      ret = kvs_exist_kv_pairs_async(ks_hd, 1, kvskey, list, complete);
      if (ret != KVS_SUCCESS) {
        fprintf(stderr, "exit tuple failed with err 0x%x\n", ret);
        if(status) free(status);
        if(list) free(list);
        _free_kvs_pair(kvskey, NULL, &keypool, NULL, &lock);
        goto exit;
      }

      cur_qdepth++;
      num_submitted++;
    }

    if (cur_qdepth == maxdepth) {
      usleep(1);
    }
  }

exit:
  // wait until commands that has succussfully submitted finish
  while(completed < num_submitted) {
    usleep(1);
  }

  double timespan_sec = _calc_time_span(start_time);
  fprintf(stdout, "Total time %.2f sec; Throughput %.2f ops/sec\n",
    timespan_sec, (double)count /timespan_sec );
  _free_str_pool(&keypool, &lock);
  return SUCCESS;
}

int _env_exit(kvs_device_handle dev, char* keyspace_name,
  kvs_key_space_handle ks_hd) {
  uint32_t dev_util = 0;
  kvs_get_device_utilization(dev, &dev_util);
  fprintf(stdout, "After: Total used is %d\n", dev_util);  
  kvs_close_key_space(ks_hd);
  kvs_key_space_name ks_name;
  ks_name.name_len = strlen(keyspace_name) + 1;
  ks_name.name = keyspace_name;
  kvs_delete_key_space(dev, &ks_name);
  kvs_result ret = kvs_close_device(dev);
  return ret;
}

int _env_init(char* dev_path, kvs_device_handle* dev, char *keyspace_name,
  kvs_key_space_handle* ks_hd) {
  kvs_result ret = kvs_open_device(dev_path, dev);
  if(ret != KVS_SUCCESS) {
    fprintf(stderr, "Device open failed 0x%x\n", ret);
    return FAILED;
  }

  //keyspace list after create "test"
  const uint32_t retrieve_cnt = 2;
  kvs_key_space_name names[retrieve_cnt];
  for(uint8_t idx = 0; idx < retrieve_cnt; idx++) {
    names[idx].name_len = MAX_KEYSPACE_NAME_LEN;
    names[idx].name = (char*)malloc(MAX_KEYSPACE_NAME_LEN);
  }

  uint32_t valid_cnt = 0;
  ret = kvs_list_key_spaces(*dev, 1, retrieve_cnt*sizeof(kvs_key_space_name),
    names, &valid_cnt);
  if(ret != KVS_SUCCESS) {
    fprintf(stderr, "List current keyspace failed. error:0x%x.\n", ret);
    kvs_close_device(*dev);
    return FAILED;
  }
  for (uint8_t idx = 0; idx < valid_cnt; idx++) {
    kvs_delete_key_space(*dev, &names[idx]);
  }

  //create key spaces
  kvs_key_space_name ks_name;
  kvs_option_key_space option = { KVS_KEY_ORDER_NONE };
  ks_name.name = keyspace_name;
  ks_name.name_len = strlen(keyspace_name);
  //currently size of keyspace is not support specify
  ret = kvs_create_key_space(*dev, &ks_name, 0, option);
  if (ret != KVS_SUCCESS) {
    kvs_close_device(*dev);
    fprintf(stderr, "Create keyspace failed. error:0x%x.\n", ret);
    return FAILED;
  }

  ret = kvs_open_key_space(*dev, keyspace_name, ks_hd);
  if(ret != KVS_SUCCESS) {
    fprintf(stderr, "Open keyspace %s failed. error:0x%x.\n", keyspace_name, ret);
    kvs_delete_key_space(*dev, &ks_name);
    kvs_close_device(*dev);
    return FAILED;
  }

  kvs_key_space ks_info;
  ks_info.name = (kvs_key_space_name *)malloc(sizeof(kvs_key_space_name));
  if(!ks_info.name) {
    fprintf(stderr, "Malloc resource failed.\n");
    _env_exit(*dev, keyspace_name, *ks_hd);
    return FAILED;
  }
  ks_info.name->name = (char*)malloc(MAX_CONT_PATH_LEN);
  if(!ks_info.name->name) {
    fprintf(stderr, "Malloc resource failed.\n");
    free(ks_info.name);
    _env_exit(*dev, keyspace_name, *ks_hd);
    return FAILED;
  }
  ks_info.name->name_len = MAX_CONT_PATH_LEN;
  ret = kvs_get_key_space_info(*ks_hd, &ks_info);
  if(ret != KVS_SUCCESS) {
    fprintf(stderr, "Get info of keyspace failed. error:0x%x.\n", ret);
    free(ks_info.name->name);
    free(ks_info.name);
    return FAILED;
  }
  fprintf(stdout, "Keyspace information get name: %s\n", ks_info.name->name);
  fprintf(stdout, "open:%d, count:%ld, capacity:%ld, free_size:%ld.\n", 
    ks_info.opened, ks_info.count, ks_info.capacity, ks_info.free_size);
  free(ks_info.name->name);
  free(ks_info.name);

  uint32_t dev_util = 0;
  uint64_t dev_capa = 0;
  kvs_get_device_utilization(*dev, &dev_util);
  kvs_get_device_capacity(*dev, &dev_capa);
  fprintf(stdout, "Before: Total size is %ld bytes, used is %d\n", dev_capa, dev_util);
  kvs_device *dev_info = (kvs_device*)malloc(sizeof(kvs_device));
  if(dev_info) {
    kvs_get_device_info(*dev, dev_info);
    fprintf(stdout, "Total size: %.2f GB\nMax value size: %d\nMax key size: %d\nOptimal value size: %d\n",
    (float)dev_info->capacity/1000/1000/1000, dev_info->max_value_len,
    dev_info->max_key_len, dev_info->optimal_value_len);
    free(dev_info);
  }

  return SUCCESS;
}


int main(int argc, char *argv[]) {
  char* dev_path = NULL;
  int num_ios = 10;
  int qdepth = 64;
  int op_type = 1;
  uint16_t klen = 16;
  uint32_t vlen = 4096;
  //int is_polling = 0;
  int c;
  int ret = SUCCESS;

  while ((c = getopt(argc, argv, "d:n:q:o:k:v:h")) != -1) {
    switch(c) {
    case 'd':
      dev_path = optarg;
      break;
    case 'n':
      num_ios = atoi(optarg);
      break;
    case 'q':
      qdepth = atoi(optarg);
      break;
    case 'o':
      op_type = atoi(optarg);
      break;
    case 'k':
      klen = atoi(optarg);
      break;
    case 'v':
      vlen = atoi(optarg);
      break;
    case 'h':
      usage(argv[0]);
      return FAILED;
    default:
      usage(argv[0]);
      return SUCCESS;
    }
  }

  if(dev_path == NULL) {
    fprintf(stderr, "Please specify KV SSD device path\n");
    usage(argv[0]);
    return SUCCESS;
  }
  
  char ks_name[MAX_KEYSPACE_NAME_LEN];
  snprintf(ks_name, MAX_KEYSPACE_NAME_LEN, "%s", "keyspace_test");
  kvs_device_handle dev;
  kvs_key_space_handle ks_hd;
  if(_env_init(dev_path, &dev, ks_name, &ks_hd) != SUCCESS)
    return FAILED;

  switch(op_type) {
  case WRITE_OP:
    perform_insertion(ks_hd, num_ios, qdepth, klen, vlen);
    break;
  case READ_OP:
    //perform_insertion(cont_handle, num_ios, qdepth, klen, vlen);
    perform_read(ks_hd, num_ios, qdepth, klen, vlen);
    break;
  case DELETE_OP:
    //perform_insertion(cont_handle, num_ios, qdepth, klen, vlen);
    perform_delete(ks_hd, num_ios, qdepth, klen, vlen);
    break;
  case ITERATOR_OP:
    //perform_insertion(cont_handle, num_ios, qdepth, klen, vlen);
    perform_iterator(ks_hd);
    break;
  case KEY_EXIST_OP:
    //perform_insertion(cont_handle, num_ios, qdepth,  klen, vlen);
    perform_key_exist(ks_hd, num_ios, qdepth,  klen, vlen);
    break;
  default:
    fprintf(stderr, "Please specify a correct op_type for testing\n");
    ret = FAILED;
  }

  _env_exit(dev, ks_name, ks_hd);
  return ret;
}
#endif
