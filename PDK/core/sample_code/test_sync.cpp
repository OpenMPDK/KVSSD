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
#include <mutex>
#include <atomic>
#include <unistd.h>
#include <queue>
#include <kvs_api.h>

static int use_udd = 0;

struct iterator_info{
  kvs_iterator_handle iter_handle;
  kvs_iterator_list iter_list;
  int has_iter_finish;
  int g_iter_mode;
};
#define iter_buff (32*1024)

void usage(char *program)
{
  printf("==============\n");
  printf("usage: %s -d device_path [-n num_ios] [-o op_type] [-k klen] [-v vlen]\n", program);
  printf("-d      device_path  :  kvssd device path. e.g. emul: /dev/kvemul; kdd: /dev/nvme0n1; udd: 0000:06:00.0\n");
  printf("-n      num_ios      :  total number of ios\n");
  printf("-o      op_type      :  1: write; 2: read; 3: delete; 4: iterator\n");
  printf("-k      klen         :  key length\n");
  printf("-v      vlen         :  value length\n");
  printf("==============\n");
}


void print_iterator_keyvals(kvs_iterator_list *iter_list, int g_iter_mode){
  uint8_t *it_buffer = (uint8_t *) iter_list->it_list;
  uint32_t i;

  if(g_iter_mode == KVS_ITERATOR_OPT_KV) {
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
    for(i = 0; i < iter_list->num_entries; i++)
      fprintf(stdout, "Iterator get key %s\n",  it_buffer + i * 16);
  }
}

#define WRITE_OP  1
#define READ_OP   2
#define DELETE_OP 3
#define ITERATOR_OP 4

int perform_iterator(kvs_container_handle cont_hd, int iter_kv)
{
  struct iterator_info *iter_info = (struct iterator_info *)malloc(sizeof(struct iterator_info));
  iter_info->g_iter_mode = (iter_kv == 0 ? KVS_ITERATOR_OPT_KEY : KVS_ITERATOR_OPT_KV);

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
  iter_ctx_open.option = (iter_kv == 0 ? KVS_ITERATOR_OPT_KEY : KVS_ITERATOR_OPT_KV);

  kvs_open_iterator(cont_hd, &iter_ctx_open, &iter_info->iter_handle);

  /* Do iteration */
  iter_info->iter_list.size = iter_buff;
  uint8_t *buffer;
  buffer =(uint8_t*) kvs_malloc(iter_buff, 4096);
  iter_info->iter_list.it_list = (uint8_t*) buffer;

  kvs_iterator_context iter_ctx_next;
  iter_ctx_next.option = KVS_ITER_DEFAULT;
  iter_ctx_next.private1 = iter_info;
  iter_ctx_next.private2 = NULL;

  iter_info->iter_list.end = 0;
  iter_info->iter_list.num_entries = 0;

  struct timespec t1, t2;
  clock_gettime(CLOCK_REALTIME, &t1);

  while(1) {
    iter_info->iter_list.size = iter_buff;
    kvs_iterator_next(cont_hd, iter_info->iter_handle, &iter_info->iter_list, &iter_ctx_next);

    total_entries += iter_info->iter_list.num_entries;
    print_iterator_keyvals(&iter_info->iter_list, iter_info->g_iter_mode);
        
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
  iter_ctx_close.option = KVS_ITER_DEFAULT;
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

int perform_read(kvs_container_handle cont_hd, int count, uint16_t klen, uint32_t vlen) {

  char *key   = (char*)kvs_malloc(klen, 4096);
  char *value = (char*)kvs_malloc(vlen, 4096);
  if(key == NULL || value == NULL) {
    fprintf(stderr, "failed to allocate\n");
    exit(1);
  }

  fprintf(stdout, "\n===========\n");
  fprintf(stdout, "   Do Read Operation \n");
  fprintf(stdout, "===========\n");

  struct timespec t1, t2;
  clock_gettime(CLOCK_REALTIME, &t1);
    
  for (int i = 0; i < count; i++) {
    memset(value, 0, vlen);
    sprintf(key, "%0*d", klen - 1, i);    
    const kvs_retrieve_context ret_ctx = { KVS_RETRIEVE_IDEMPOTENT | KVS_SYNC_IO, 0, 0, 0};
    const kvs_key  kvskey = { key, klen };
    kvs_value kvsvalue = { value, vlen , 0 /*offset */};

    int ret = kvs_retrieve_tuple(cont_hd, &kvskey, &kvsvalue, &ret_ctx);
    if(ret != KVS_SUCCESS) {
      fprintf(stderr, "retrieve tuple %s failed with error 0x%x - %s\n", key, ret, kvs_errstr(ret));
      //exit(1);
    } else {
      fprintf(stderr, "retrieve tuple %s with value %s done \n", key, value);
    }
  }

  clock_gettime(CLOCK_REALTIME, &t2);
  unsigned long long start, end;
  start = t1.tv_sec * 1000000000L + t1.tv_nsec;
  end = t2.tv_sec * 1000000000L + t2.tv_nsec;
  double sec = (double)(end - start) / 1000000000L;
  fprintf(stdout, "Total time %.2f sec; Throughput %.2f ops/sec\n", sec, (double)count /sec );
  
  if(key) kvs_free(key);
  if(value) kvs_free(value);
  
  return 0;
}


int perform_insertion(kvs_container_handle cont_hd, int count, uint16_t klen, uint32_t vlen) {

  char *key   = (char*)kvs_malloc(klen, 4096);
  char *value = (char*)kvs_malloc(vlen, 4096);
  if(key == NULL || value == NULL) {
    fprintf(stderr, "failed to allocate\n");
    exit(1);
  }

  fprintf(stdout, "\n===========\n");
  fprintf(stdout, "   Do Write Operation \n");
  fprintf(stdout, "===========\n");

  struct timespec t1, t2;
  clock_gettime(CLOCK_REALTIME, &t1);
  
  for(int i = 0; i < count; i++) {
    sprintf(key, "%0*d", klen - 1, i);
    sprintf(value, "%0*d", klen - 1, i + 10); 
    
    const kvs_store_context put_ctx = { KVS_STORE_POST | KVS_SYNC_IO, 0, 0, 0};
    const kvs_key  kvskey = { key, klen};
    const kvs_value kvsvalue = { value, vlen, 0};

    int ret = kvs_store_tuple(cont_hd, &kvskey, &kvsvalue, &put_ctx);    
    if(ret != KVS_SUCCESS) {
      fprintf(stderr, "store tuple failed with error 0x%x - %s\n", ret, kvs_errstr(ret));
      exit(1);
    } else {
      //fprintf(stderr, "store key %s with value %s done \n", key, value);
    }
  }

  clock_gettime(CLOCK_REALTIME, &t2);
  unsigned long long start, end;
  start = t1.tv_sec * 1000000000L + t1.tv_nsec;
  end = t2.tv_sec * 1000000000L + t2.tv_nsec;
  double sec = (double)(end - start) / 1000000000L;
  fprintf(stdout, "Total time %.2f sec; Throughput %.2f ops/sec\n", sec, (double)count /sec );
  
  if(key) kvs_free(key);
  if(value) kvs_free(value);

  return 0;
}

int perform_delete(kvs_container_handle cont_hd, int count, uint16_t klen, uint32_t vlen) {

  char *key  = (char*)kvs_malloc(klen, 4096);
  
  if(key == NULL) {
    fprintf(stderr, "failed to allocate\n");
    exit(1);
  }

  fprintf(stdout, "\n===========\n");
  fprintf(stdout, "   Do Delete Operation\n");
  fprintf(stdout, "===========\n"); 

  struct timespec t1, t2;
  clock_gettime(CLOCK_REALTIME, &t1);
  
  for(int i = 0; i < count; i++) {
    sprintf(key, "%0*d", klen - 1, i);
    const kvs_key  kvskey = { key, klen};
    const kvs_delete_context del_ctx = { KVS_DELETE_TUPLE , 0, 0, 0};
    int ret = kvs_delete_tuple(cont_hd, &kvskey, &del_ctx);
    if(ret != KVS_SUCCESS) {
      fprintf(stderr, "delete tuple failed with error 0x%x - %s\n", ret, kvs_errstr(ret));
      exit(1);
    } else {
      //fprintf(stderr, "delete key %s done \n", key);
    }
  }

  clock_gettime(CLOCK_REALTIME, &t2);
  unsigned long long start, end;
  start = t1.tv_sec * 1000000000L + t1.tv_nsec;
  end = t2.tv_sec * 1000000000L + t2.tv_nsec;
  double sec = (double)(end - start) / 1000000000L;
  fprintf(stdout, "Total time %.2f sec; Throughput %.2f ops/sec\n", sec, (double)count /sec );
  
  if(key) kvs_free(key);
  return 0;
}


int main(int argc, char *argv[]) {
  char* dev_path = NULL;
  int num_ios = 10;
  int op_type = 1;
  uint16_t klen = 16;
  uint32_t vlen = 4096;
  int c;
 
  while ((c = getopt(argc, argv, "d:n:o:k:v:h")) != -1) {
    switch(c) {
    case 'd':
      dev_path = optarg;
      break;
    case 'n':
      num_ios = atoi(optarg);
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
  
  options.memory.use_dpdk = 0;
  options.aio.iocomplete_fn = NULL;
  
  const char *configfile = "../kvssd_emul.conf";
  options.emul_config_file =  configfile;
  
  if(found) { // spdk driver
    use_udd = 1;
    options.memory.use_dpdk = 1;
    char *core;
    core = options.udd.core_mask_str;
   *core = '0';
    core = options.udd.cq_thread_mask;
    *core = '0';
    options.udd.mem_size_mb = 1024;
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset); // CPU 0
    sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);    
    
  } else {
    use_udd = 0;
    options.memory.use_dpdk = 0;
  }

  // initialize the environment
  kvs_init_env(&options);

  kvs_device_handle dev;
  kvs_open_device(dev_path, &dev);

  kvs_container_context ctx;
  kvs_create_container(dev, "test", 4, &ctx);

  kvs_container_handle cont_handle;
  kvs_open_container(dev, "test", &cont_handle);
    
  switch(op_type) {
  case WRITE_OP:
    perform_insertion(cont_handle, num_ios, klen, vlen);
    break;
  case READ_OP:
    perform_insertion(cont_handle, num_ios, klen, vlen);
    perform_read(cont_handle, num_ios, klen, vlen);
    break;
  case DELETE_OP:
    perform_insertion(cont_handle, num_ios, klen, vlen);
    perform_delete(cont_handle, num_ios, klen, vlen);
    break;
  case ITERATOR_OP:
    //perform_insertion(cont_handle, num_ios, klen, vlen);
    perform_iterator(cont_handle, 0);
    /* Iterator a key-value pair only works in emulator */
    //perform_iterator(cont_handle, 1);
    break;
  default:
    fprintf(stderr, "Please specify a correct op_type for testing\n");
    exit(1);

  }
  
  kvs_close_container(cont_handle);
  kvs_delete_container(dev, "test");
  kvs_exit_env();
  
  return 0;
}
