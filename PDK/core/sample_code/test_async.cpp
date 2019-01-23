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

std::atomic<int> submitted(0);
std::atomic<int> completed(0);
std::atomic<int> cur_qdepth(0);
pthread_mutex_t lock;

struct iterator_info{
  kvs_iterator_handle iter_handle;
  kvs_iterator_list iter_list;
  kvs_iterator_option g_iter_mode;
};
#define iter_buff (32*1024)

void usage(char *program)
{
  printf("==============\n");
  printf("usage: %s -d device_path [-n num_ios] [-q queue_depth] [-o op_type] [-k klen] [-v vlen]\n", program);
  printf("-d      device_path  :  kvssd device path. e.g. emul: /dev/kvemul; kdd: /dev/nvme0n1; udd: 0000:06:00.0\n");
  printf("-n      num_ios      :  total number of ios (ignore this for iterator)\n");
  printf("-q      queue_depth  :  queue depth (ignore this for iterator)\n");
  printf("-o      op_type      :  1: write; 2: read; 3: delete; 4: iterator; 5: check key exist\n");
  printf("-k      klen         :  key length (ignore this for iterator)\n");
  printf("-v      vlen         :  value length (ignore this for iterator)\n");
  printf("==============\n");
}


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


    // for ETA50K24 firmware
    //fprintf(stderr, "error: iterator(KVS_ITERATOR_KEY_VALUE) is not supported.\n");
  } else {
    // key only iterator (KVS_ITERATOR_KEY)

    // For fixed key length
    /*    
    for(i = 0; i < iter_list->num_entries; i++)
      fprintf(stdout, "Iterator get key %s\n",  it_buffer + i * 16);
    */
    
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


void complete(kvs_callback_context* ioctx) {
  if (ioctx->result != 0 && ioctx->result != KVS_ERR_KEY_NOT_EXIST) {
    fprintf(stderr, "io error: op = %d, key = %s, result = 0x%x, err = %s\n", ioctx->opcode, ioctx->key ? (char*)ioctx->key->key:0, ioctx->result, kvs_errstr(ioctx->result));
    exit(1);
  } else {
    //fprintf(stderr, "io_complete: op = %d, key = %s, result = %s\n", ioctx->opcode, ioctx->key? (char*)ioctx->key->key : 0 , kvs_errstr(ioctx->result));
  }

  uint8_t *exist;
  switch(ioctx->opcode) {
  case IOCB_ASYNC_ITER_NEXT_CMD:
    completed++;
    break;
  case IOCB_ASYNC_CHECK_KEY_EXIST_CMD:
    exist = (uint8_t*)ioctx->result_buffer;
    fprintf(stdout, "key %s exist? %s\n", (char*)ioctx->key->key, *exist == 0? "FALSE":"TRUE");
    if(ioctx->result_buffer)
      free(ioctx->result_buffer);
  default:
    //fprintf(stderr, "io_complete: op = %d, key = %s, vlen = %d, actual vlen = %d, result = %s\n", ioctx->opcode, (char*)ioctx->key->key, ioctx->value.length, ioctx->value.actual_value_size, kvs_errstr(ioctx->result));
    std::queue<char*> * keypool = (std::queue<char*> *)ioctx->private1;
    std::queue<char*> * valuepool = (std::queue<char*> *)ioctx->private2;
    pthread_mutex_lock(&lock);
    if(ioctx->key) {
      if(ioctx->key->key)
	keypool->push((char*)ioctx->key->key);
      free(ioctx->key);
    }
    if(ioctx->value) {
      if(ioctx->value->value)
	valuepool->push((char*)ioctx->value->value);
      free(ioctx->value);
    }
    pthread_mutex_unlock(&lock);

    completed++;
    cur_qdepth--; 
    break;
  }
}

#define WRITE_OP  1
#define READ_OP   2
#define DELETE_OP 3
#define ITERATOR_OP 4
#define KEY_EXIST_OP 5

int perform_iterator(kvs_container_handle cont_hd, int iter_kv)
{

  int count = KVS_MAX_ITERATE_HANDLE;
  kvs_iterator_info kvs_iters[count];

  struct iterator_info *iter_info = (struct iterator_info *)malloc(sizeof(struct iterator_info));

  if(iter_kv == 0) {
    iter_info->g_iter_mode.iter_type = KVS_ITERATOR_KEY;
  } else {
    iter_info->g_iter_mode.iter_type = KVS_ITERATOR_KEY_VALUE;
  }

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
    PREFIX_KV |= (prefix_str[i] << i*8);
  }

  iter_ctx_open.bit_pattern = PREFIX_KV;
  iter_ctx_open.private1 = NULL;
  iter_ctx_open.private2 = NULL;
  memset(&iter_ctx_open.option, 0, sizeof(kvs_iterator_option));
  if(iter_kv == 0)
    iter_ctx_open.option.iter_type = KVS_ITERATOR_KEY;
  else
    iter_ctx_open.option.iter_type = KVS_ITERATOR_KEY_VALUE;

  submitted = completed = 0;

  //kvs_close_iterator_all(cont_hd);
  ret = kvs_open_iterator(cont_hd, &iter_ctx_open, &iter_info->iter_handle); 
  if(ret) {
    fprintf(stdout, "open iter failed with err %s\n", kvs_errstr(ret));
    exit(1);
  }
  
  memset(kvs_iters, 0, sizeof(kvs_iters));
  int res = kvs_list_iterators(cont_hd, kvs_iters, count);
  if(res) {
    printf("kv_list_iterators with error: 0x%X\n", res);
    exit(1);
  } else {
    for (int j = 0; j < count; j++){
      if(kvs_iters[j].status == 1) fprintf(stdout, "found handler %d 0x%x 0x%x\n",
					   kvs_iters[j].iter_handle,
					   kvs_iters[j].bit_pattern,
					   kvs_iters[j].bitmask);
    }
  }

  /* Do iteration */

  submitted = completed = 0;
  iter_info->iter_list.size = iter_buff;
  uint8_t *buffer;
  buffer =(uint8_t*) kvs_malloc(iter_buff, 4096);
  memset(buffer, 0, iter_buff);
  iter_info->iter_list.it_list = (uint8_t*) buffer;

  kvs_iterator_context iter_ctx_next;
  iter_ctx_next.private1 = iter_info;
  iter_ctx_next.private2 = NULL;

  iter_info->iter_list.end = 0;
  iter_info->iter_list.num_entries = 0;

  struct timespec t1, t2;
  clock_gettime(CLOCK_REALTIME, &t1);

  while(1) {
    iter_info->iter_list.size = iter_buff;
    memset(iter_info->iter_list.it_list, 0, iter_buff);
    ret = kvs_iterator_next_async(cont_hd, iter_info->iter_handle, &iter_info->iter_list, &iter_ctx_next, complete);
    if(ret != KVS_SUCCESS) {
      fprintf(stderr, "iterator next fails with error 0x%x - %s\n", ret, kvs_errstr(ret));
      free(buffer);
      free(iter_info);
      exit(1);
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
      memset(iter_info->iter_list.it_list, 0,  iter_buff);
    }
  }
  
  clock_gettime(CLOCK_REALTIME, &t2);
  unsigned long long start, end;
  start = t1.tv_sec * 1000000000L + t1.tv_nsec;
  end = t2.tv_sec * 1000000000L + t2.tv_nsec;
  double sec = (double)(end - start) / 1000000000L;
  fprintf(stdout, "Total time %.2f sec; Throughput %.2f keys/sec\n", sec, (double)total_entries /sec );

  /* Close iterator */
  kvs_iterator_context iter_ctx_close;
  iter_ctx_close.private1 = NULL;
  iter_ctx_close.private2 = NULL;

  ret = kvs_close_iterator(cont_hd, iter_info->iter_handle, &iter_ctx_close);
  if(ret != KVS_SUCCESS) {
    fprintf(stderr, "Failed to close iterator\n");
    exit(1);
  }

  if(buffer) kvs_free(buffer);
  if(iter_info) free(iter_info);

  return 0;
}

int perform_read(kvs_container_handle cont_hd, int count, int maxdepth, kvs_key_t klen, uint32_t vlen) {
    int num_submitted = 0;
    std::queue<char *> keypool;
    std::queue<char *> valuepool;
    completed = 0;

    for (int i =0 ; i < maxdepth; i++) {
      char *keymem   = (char*)kvs_malloc(klen, 4096);
      char *valuemem = (char*)kvs_malloc(vlen, 4096);
      if(keymem == NULL || valuemem == NULL) {
        fprintf(stderr, "failed to allocate\n");
        exit(1);
      }
      keypool.push(keymem);
      valuepool.push(valuemem);
    }

    fprintf(stdout, "\n===========\n");
    fprintf(stdout, "   Do Read Operation\n");
    fprintf(stdout, "===========\n");

    struct timespec t1, t2;
    clock_gettime(CLOCK_REALTIME, &t1);
        
    long int seq = 0;

    while (num_submitted < count) {

      while (cur_qdepth < maxdepth) {
	if (num_submitted >= count) break;
	pthread_mutex_lock(&lock);
	char *key   = keypool.front(); keypool.pop();
	char *value = valuepool.front(); valuepool.pop();
	pthread_mutex_unlock(&lock);
	memset(value, 0, vlen);
	kvs_retrieve_option option;
	memset(&option, 0, sizeof(kvs_retrieve_option));
	option.kvs_retrieve_decompress = false;
	option.kvs_retrieve_delete = false;
	const kvs_retrieve_context ret_ctx = {option, &keypool, &valuepool};
	kvs_key *kvskey = (kvs_key*)malloc(sizeof(kvs_key));
	kvskey->key = key;
	kvskey->length = klen;
	kvs_value *kvsvalue = (kvs_value*)malloc(sizeof(kvs_value));
	kvsvalue->value = value;
	kvsvalue->length = vlen;
	kvsvalue->actual_value_size = kvsvalue->offset = 0;
	
	sprintf(key, "%0*ld", klen - 1, seq++);
	int ret = kvs_retrieve_tuple_async(cont_hd, kvskey, kvsvalue, &ret_ctx, complete);
	if (ret != KVS_SUCCESS) {
	  fprintf(stderr, "retrieve tuple failed with err %s %x\n",  kvs_errstr(ret), ret);
	  exit(1);
	}

	cur_qdepth++;
	num_submitted++;
	
      }

      if (cur_qdepth == maxdepth) usleep(1);
    }

    // wait until it finishes
    while(completed < num_submitted) {
      usleep(1);
    }
    
    clock_gettime(CLOCK_REALTIME, &t2);
    unsigned long long start, end;
    start = t1.tv_sec * 1000000000L + t1.tv_nsec;
    end = t2.tv_sec * 1000000000L + t2.tv_nsec;
    double sec = (double)(end - start) / 1000000000L;
    fprintf(stdout, "Total time %.2f sec; Throughput %.2f ops/sec\n", sec, (double)count /sec );
        
    while(!keypool.empty()) {
      auto p = keypool.front();
      keypool.pop();
      kvs_free(p);
    }

    while(!valuepool.empty()) {
      auto p =valuepool.front();
      valuepool.pop();
      kvs_free(p);
    }
    
    return 0;
}


int perform_insertion(kvs_container_handle cont_hd, int count, int maxdepth, kvs_key_t klen, uint32_t vlen) {
  int num_submitted = 0;
  std::queue<char *> keypool;
  std::queue<char *> valuepool;
  completed = 0;
  for (int i =0 ; i < maxdepth; i++) {
    char *keymem   = (char*)kvs_malloc(klen, 4096);
    char *valuemem = (char*)kvs_malloc(vlen, 4096);
    if(keymem == NULL || valuemem == NULL) {
      fprintf(stderr, "failed to allocate\n");
      exit(1);
    }
    keypool.push(keymem);
    valuepool.push(valuemem);
  }

  long int seq = 0;
  fprintf(stdout, "\n===========\n");
  fprintf(stdout, "   Do Write Operation\n");
  fprintf(stdout, "===========\n");

  struct timespec t1, t2;
  clock_gettime(CLOCK_REALTIME, &t1);

  while (num_submitted < count) {
    while (cur_qdepth < maxdepth) {
      if (num_submitted >= count) break;

      pthread_mutex_lock(&lock);
      char *key   = keypool.front(); keypool.pop();
      char *value = valuepool.front(); valuepool.pop();
      pthread_mutex_unlock(&lock);
      if(!key || !value) {
	fprintf(stderr, "no mem is allocated\n");
	exit(1);
      }
      
      sprintf(key, "%0*ld", klen - 1, seq++);
      sprintf(value, "value%ld", seq);
      kvs_store_option option;
      memset(&option, 0, sizeof(kvs_store_option));
      option.st_type = KVS_STORE_POST;
      option.kvs_store_compress = false;
      
      const kvs_store_context put_ctx = {option, &keypool, &valuepool };
      kvs_key *kvskey = (kvs_key*)malloc(sizeof(kvs_key));
      kvskey->key = key;
      kvskey->length = klen;
      kvs_value *kvsvalue = (kvs_value*)malloc(sizeof(kvs_value));
      kvsvalue->value = value;
      kvsvalue->length = vlen;
      kvsvalue->actual_value_size = kvsvalue->offset = 0;
      int ret = kvs_store_tuple_async(cont_hd, kvskey, kvsvalue, &put_ctx, complete);
           
      if (ret != KVS_SUCCESS) {
	fprintf(stderr, "store tuple failed with err %s\n", kvs_errstr(ret)); 
	//ret = kvs_store_tuple_async(cont_hd, kvskey, kvsvalue, &put_ctx, complete);
	exit(1);
      }

      cur_qdepth++;
      num_submitted++;
    }

    if (cur_qdepth == maxdepth) usleep(1);
    
  }
  
  // wait until it finishes
  while(completed < num_submitted) {
    usleep(1);
  }
  
  clock_gettime(CLOCK_REALTIME, &t2);
  unsigned long long start, end;
  start = t1.tv_sec * 1000000000L + t1.tv_nsec;
  end = t2.tv_sec * 1000000000L + t2.tv_nsec;
  double sec = (double)(end - start) / 1000000000L;
  fprintf(stdout, "Total time %.2f sec; Throughput %.2f ops/sec\n", sec, (double)count /sec );
  
  while(!keypool.empty()) {
    auto p = keypool.front();
    keypool.pop();
    kvs_free(p);
  }

  while(!valuepool.empty()) {
    auto p =valuepool.front();
    valuepool.pop();
    kvs_free(p);
  }
  
  return 0;
}

int perform_delete(kvs_container_handle cont_hd, int count, int maxdepth, kvs_key_t klen, uint32_t vlen) {
  int num_submitted = 0;
  std::queue<char *> keypool;
  std::queue<char *> valuepool;
  completed = 0;
  
  for (int i =0 ; i < maxdepth; i++) {
    char *keymem   = (char*)kvs_malloc(klen, 4096);
    char *valuemem = (char*)kvs_malloc(vlen, 4096);
    if(keymem == NULL || valuemem == NULL) {
      fprintf(stderr, "failed to allocate\n");
      exit(1);
    }
    keypool.push(keymem);
    valuepool.push(valuemem);
  }

  long int seq = 0;

  fprintf(stdout, "\n===========\n");
  fprintf(stdout, "   Do Delete Operation\n");
  fprintf(stdout, "===========\n"); 

  struct timespec t1, t2;
  clock_gettime(CLOCK_REALTIME, &t1);
    
  while (num_submitted < count) {
    while (cur_qdepth < maxdepth) {
      if (num_submitted >= count) break;
      pthread_mutex_lock(&lock);
      char *key   = keypool.front(); keypool.pop();
      pthread_mutex_unlock(&lock);
      sprintf(key, "%0*ld", klen - 1, seq++);
      kvs_key *kvskey = (kvs_key*)malloc(sizeof(kvs_key));
      kvskey->key = key;
      kvskey->length = klen;
      kvs_delete_option option;
      option.kvs_delete_error = false;
      const kvs_delete_context del_ctx = { option, &keypool, &valuepool};
      int ret = kvs_delete_tuple_async(cont_hd, kvskey, &del_ctx, complete);
      if (ret != KVS_SUCCESS) {
	fprintf(stderr, "delete tuple failed with err %s\n", kvs_errstr(ret));
	exit(1);
      }
      
      cur_qdepth++;
      num_submitted++;
    }

    if (cur_qdepth == maxdepth) usleep(1);
  }

  // wait until it finishes
  while(completed < num_submitted) {
    usleep(1);
  }


  clock_gettime(CLOCK_REALTIME, &t2);
  unsigned long long start, end;
  start = t1.tv_sec * 1000000000L + t1.tv_nsec;
  end = t2.tv_sec * 1000000000L + t2.tv_nsec;
  double sec = (double)(end - start) / 1000000000L;
  fprintf(stdout, "Total time %.2f sec; Throughput %.2f ops/sec\n", sec, (double)count /sec );
    
  while(!keypool.empty()) {
    auto p = keypool.front();
    keypool.pop();
    kvs_free(p);
  }

  while(!valuepool.empty()) {
    auto p =valuepool.front();
    valuepool.pop();
    kvs_free(p);
  }
  
  return 0;
}

int perform_key_exist(kvs_container_handle cont_hd, int count, int maxdepth, kvs_key_t klen, uint32_t vlen ) {

  int num_submitted = 0;
  std::queue<char *> keypool;
  std::queue<char *> valuepool;
  completed = 0;

  for (int i =0 ; i < maxdepth; i++) {
    char *keymem   = (char*)kvs_malloc(klen, 4096);
    char *valuemem = (char*)kvs_malloc(vlen, 4096);
    if(keymem == NULL || valuemem == NULL) {
      fprintf(stderr, "failed to allocate\n");
      exit(1);
    }

    keypool.push(keymem);
    valuepool.push(valuemem);
  }

  long int seq = 0;
  fprintf(stdout, "\n===========\n");
  fprintf(stdout, "   Do key exist check Operation\n");
  fprintf(stdout, "===========\n");

  struct timespec t1, t2;
  clock_gettime(CLOCK_REALTIME, &t1);
  while (num_submitted < count) {
    while (cur_qdepth < maxdepth) {
      if (num_submitted >= count) break;
      pthread_mutex_lock(&lock);
      char *key   = keypool.front(); keypool.pop();
      pthread_mutex_unlock(&lock);
      sprintf(key, "%0*ld", klen - 1, seq++);
      kvs_key *kvskey = (kvs_key*)malloc(sizeof(kvs_key));
      kvskey->key = key;
      kvskey->length = klen;
      const kvs_exist_context exist_ctx = {&keypool, &valuepool};

      uint8_t *status = (uint8_t*)malloc(sizeof(uint8_t));
      int ret = kvs_exist_tuples_async(cont_hd, 1, kvskey, 1, status, &exist_ctx, complete);
      if (ret != KVS_SUCCESS) {
	fprintf(stderr, "exit tuple failed with err %s\n", kvs_errstr(ret));
	exit(1);
      }

      cur_qdepth++;
      num_submitted++;
    }

    if (cur_qdepth == maxdepth) usleep(1);
  }
  // wait until it finishes
  while(completed < num_submitted) {
    usleep(1);
  }

  clock_gettime(CLOCK_REALTIME, &t2);
  unsigned long long start, end;
  start = t1.tv_sec * 1000000000L + t1.tv_nsec;
  end = t2.tv_sec * 1000000000L + t2.tv_nsec;
  double sec = (double)(end - start) / 1000000000L;
  fprintf(stdout, "Total time %.2f sec; Throughput %.2f ops/sec\n", sec, (double)count /sec );

  while(!keypool.empty()) {
    auto p = keypool.front();
    keypool.pop();
    kvs_free(p);
  }
  
  while(!valuepool.empty()) {
    auto p =valuepool.front();
    valuepool.pop();
    kvs_free(p);
  }
  
  return 0;
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
      exit(1);
      break;
    default:
      usage(argv[0]);
      exit(0);
    }
  }

  
  if(dev_path == NULL) {
    fprintf(stderr, "Please specify KV SSD device path\n");
    usage(argv[0]);
    exit(0);
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

  // initialize the environment
  kvs_init_env(&options);

  kvs_device_handle dev;
  ret = kvs_open_device(dev_path, &dev);
  if(ret != KVS_SUCCESS) {
    fprintf(stderr, "Device open failed %s\n", kvs_errstr(ret));
    exit(1);
  }
  
  kvs_container_context ctx;
  kvs_create_container(dev, "test", 4, &ctx);
  
  kvs_container_handle cont_handle;
  kvs_open_container(dev, "test", &cont_handle);

  int32_t dev_util = 0;
  int64_t dev_capa = 0;
  kvs_device *dev_info = (kvs_device*)malloc(sizeof(kvs_device));

  kvs_get_device_utilization(dev, &dev_util);

  kvs_get_device_capacity(dev, &dev_capa);
  fprintf(stdout, "Before: Total size is %ld bytes, used is %d\n", dev_capa, dev_util);

  kvs_get_device_info(dev, dev_info);
  fprintf(stdout, "Total size: %.2f GB\nMax value size: %d\nMax key size: %d\nOptimal value size: %d\n",
	  (float)dev_info->capacity/1024/1024/1024, dev_info->max_value_len,
	  dev_info->max_key_len, dev_info->optimal_value_len);
  
  if(dev_info)
    free(dev_info);
  
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
    perform_iterator(cont_handle, 0);
    break;
  case KEY_EXIST_OP:
    //perform_insertion(cont_handle, num_ios, qdepth,  klen, vlen);
    perform_key_exist(cont_handle, num_ios, qdepth,  klen, vlen);
    break;
  default:
    fprintf(stderr, "Please specify a correct op_type for testing\n");
    exit(1);

  }

  kvs_get_device_utilization(dev, &dev_util);
  fprintf(stdout, "After: Total size is %ld bytes, used is %d\n", dev_capa, dev_util);  
  
  kvs_close_container(cont_handle);
  kvs_delete_container(dev, "test");
  kvs_exit_env();

  return 0;
}
