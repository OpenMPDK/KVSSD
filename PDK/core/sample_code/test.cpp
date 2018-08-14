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
#include <kvs_types.h>
#include <kvs_utils.h>
#include <queue>
#include <kvs_adi.h>

static int use_udd = 0;
std::atomic<int> submitted(0);
std::atomic<int> completed(0);
std::atomic<int> cur_qdepth_i(0);
pthread_mutex_t lock;

struct iterator_info{
  kvs_iterator_handle *iter_handle;
  kvs_iterator_list iter_list;
  int has_iter_finish;
  int g_iter_mode;
};
#define iter_buff (32*1024)

void usage(char *program)
{
  printf("==============\n");
  printf("usage: %s -d device_path [-n num_ios] [-q queue_depth] [-o op_type] [-k klen] [-v vlen] [-p is_polling]\n", program);
  printf("-d      device_path  :  kvssd device path. e.g. emul: /dev/kvemul; kdd: /dev/nvme0n1; udd: 0000:06:00.0\n");
  printf("-n      num_ios      :  total number of ios\n");
  printf("-q      queue_depth  :  queue depth\n");
  printf("-o      op_type      :  1: write; 2: read; 3: delete; 4: iterator\n");
  printf("-k      klen         :  key length\n");
  printf("-v      vlen         :  value length\n");
  printf("-p      is_polling   :  polling or interrupt mode (kernel driver only: 0: interrupt; 1: polling)\n");
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


void complete(kv_iocb* ioctx) {

  if (ioctx->result != 0 && ioctx->result != KVS_WRN_MORE && ioctx->result != KVS_ERR_ITERATOR_END && ioctx->result != KVS_ERR_KEY_NOT_EXIST) {
    fprintf(stderr, "io error: op = %d, key = %s, result = 0x%lx\n", ioctx->opcode, (char*)ioctx->key, ioctx->result/*kvs_errstr(ioctx->result)*/);
    exit(1);
  } else {
    //fprintf(stderr, "io_complete: op = %d, key = %s, result = 0x%x\n", ioctx->opcode, (char*)ioctx->key, ioctx->result);
  }

  auto owner = (struct iterator_info*) ioctx->private1;
  switch(ioctx->opcode) {
  case IOCB_ASYNC_ITER_OPEN_CMD:
    fprintf(stdout, "Iterator opened successfully \n");
    owner->iter_handle = ioctx->iter_handle;
    completed++;
    break;
  case IOCB_ASYNC_ITER_NEXT_CMD:
    owner->has_iter_finish = 1;
    completed++;
    break;
  case IOCB_ASYNC_ITER_CLOSE_CMD:
    fprintf(stdout, "Iterator closed successfully \n");
    completed++;
    break;
  default:
    fprintf(stderr, "io_complete: op = %d, key = %s, result = 0x%lx\n", ioctx->opcode, (char*)ioctx->key, ioctx->result);
    std::queue<char*> * keypool = (std::queue<char*> *)ioctx->private1;
    std::queue<char*> * valuepool = (std::queue<char*> *)ioctx->private2;
    pthread_mutex_lock(&lock);
    if(ioctx->key)
      keypool->push((char*)ioctx->key);
    if(ioctx->value)
      valuepool->push((char*)ioctx->value); 
    pthread_mutex_unlock(&lock);
    completed++;
    cur_qdepth_i--; 
    break;
  }
}

#define WRITE_OP  1
#define READ_OP   2
#define DELETE_OP 3
#define ITERATOR_OP 4
int async_mode = 0;

int perform_iterator(kvs_container_handle cont_hd, int is_polling, int iter_kv)
{

  struct iterator_info *iter_info = (struct iterator_info *)malloc(sizeof(struct iterator_info));
  iter_info->g_iter_mode = (iter_kv == 0 ? KVS_ITERATOR_OPT_KEY : KVS_ITERATOR_OPT_KV);
  
  int nr = 0;
  int ret;
  int total_entries = 0;


  fprintf(stdout, "\n===========\n");
  fprintf(stdout, "   Do Iterate Operation %s\n", is_polling ? "Polling" : "Interrupt");
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
  iter_ctx_open.private1 = iter_info;
  iter_ctx_open.private2 = NULL;
  
  iter_ctx_open.option = (iter_kv == 0 ? KVS_ITERATOR_OPT_KEY : KVS_ITERATOR_OPT_KV);

  submitted = completed = 0;
  
  uint32_t iterator = kvs_open_iterator(cont_hd, &iter_ctx_open);
  submitted++;
  
  if(use_udd == 0) {
    if(is_polling) {
      nr = 0;
      while(nr != 1) {
	nr = kvs_get_ioevents(cont_hd, 1);
      }
    } else {
      while(completed < submitted)
	usleep(1);
    }
  } else {
    iter_info->iter_handle = (kvs_iterator_handle*)malloc(sizeof(kvs_iterator_handle));
    iter_info->iter_handle->iterator = iterator;
    fprintf(stdout, "kvbench open iterator %d\n", iter_info->iter_handle->iterator);
    
  }
  
  /* Do iteration */
  submitted = completed = 0;
  iter_info->iter_list.size = iter_buff;
  uint8_t *buffer;
  buffer =(uint8_t*) kvs_malloc(iter_buff, 4096);
  iter_info->iter_list.it_list = (uint8_t*) buffer;

  kvs_iterator_context iter_ctx_next;
  iter_ctx_next.option = KVS_ITER_DEFAULT;
  iter_ctx_next.private1 = iter_info;
  iter_ctx_next.private2 = NULL;

  nr = 0;
  iter_info->iter_list.end = FALSE;
  iter_info->iter_list.num_entries = 0;
  
  while(1) {
    kvs_iterator_next(cont_hd, iter_info->iter_handle, &iter_info->iter_list, &iter_ctx_next);
    submitted++;
    if(is_polling) {
      while(nr == 0) {
	nr = kvs_get_ioevents(cont_hd, 1);
      }
    } else {
      while(completed < submitted)
	usleep(1);
    }

    total_entries += iter_info->iter_list.num_entries;
    //print_iterator_keyvals(&iter_info->iter_list, iter_info->g_iter_mode);
    nr = 0;

    if(iter_info->iter_list.end) {
      fprintf(stdout, "Done with all keys. Total: %d\n", total_entries);
      break;
    } else {
      fprintf(stdout, "More keys available, do another iteration\n");
      memset(iter_info->iter_list.it_list, 0,  iter_buff);
    }
  }

  /* Close iterator */
  submitted = completed = 0;
  kvs_iterator_context iter_ctx_close;
  iter_ctx_close.option = KVS_ITER_DEFAULT;
  iter_ctx_close.private1 = iter_info;
  iter_ctx_close.private2 = NULL;
  submitted++;
  ret = kvs_close_iterator(cont_hd, iter_info->iter_handle, &iter_ctx_close);
  if(ret != KVS_SUCCESS) {
    fprintf(stderr, "Failed to close iterator\n");
    exit(1);
  }

  if(is_polling) {
    nr = 0;
    while(nr == 0) {
      nr = kvs_get_ioevents(cont_hd, 1);
    }
  } else {
    while(completed < submitted) usleep(1);
  }
  
  if(buffer) kvs_free(buffer);
  if(iter_info) free(iter_info);

  return 0;
}

int perform_read(kvs_container_handle cont_hd, int count, int maxdepth, uint16_t klen, uint32_t vlen, int is_polling) {
    int cur_qdepth = 0;
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
    fprintf(stdout, "   Do Read Operation %s\n", is_polling ? "Polling" : "Interrupt");
    fprintf(stdout, "===========\n");
    
    long int seq = 1;
    while (num_submitted < count) {

      while (cur_qdepth < maxdepth) {
	if (num_submitted >= count) break;
	pthread_mutex_lock(&lock);
	char *key   = keypool.front(); keypool.pop();
	char *value = valuepool.front(); valuepool.pop();
	pthread_mutex_unlock(&lock);
	memset(value, 0, vlen);
	const kvs_retrieve_context ret_ctx = { KVS_RETRIEVE_IDEMPOTENT , 0, &keypool, &valuepool};
	const kvs_key  kvskey = { key, klen };
	kvs_value kvsvalue = { value, vlen , 0 /*offset */};
	sprintf(key, "%0*ld", klen - 1, seq++);
	int ret = kvs_retrieve_tuple(cont_hd, &kvskey, &kvsvalue, &ret_ctx);
	if (ret != KVS_SUCCESS) {
	  fprintf(stderr, "retrieve tuple failed\n"); exit(1);
	}

	cur_qdepth++;
	num_submitted++;
      }

      if(is_polling) {
	if (cur_qdepth > 0) {
	  int nr = kvs_get_ioevents(cont_hd, maxdepth);
	  cur_qdepth        -= nr;
	}
      }
      
      if (cur_qdepth == maxdepth) usleep(1);
    }

    // wait until it finishes
    if(is_polling) {
      while (cur_qdepth > 0) {
	int nr = kvs_get_ioevents(cont_hd, maxdepth);
	cur_qdepth        -= nr;
      }
    } else {
      while(completed < num_submitted) {
	usleep(1);
      }
    }

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


int perform_insertion(kvs_container_handle cont_hd, int count, int maxdepth, uint16_t klen, uint32_t vlen, bool is_polling) {
  int cur_qdepth = 0;
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

  long int seq = 1;
  fprintf(stdout, "\n===========\n");
  fprintf(stdout, "   Do Write Operation %s\n", is_polling ? "Polling" : "Interrupt");
  fprintf(stdout, "===========\n");
  
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
      const kvs_store_context put_ctx = { KVS_STORE_POST , 0, &keypool, &valuepool };
      const kvs_key  kvskey = { key, klen};
      const kvs_value kvsvalue = { value, vlen, 0 /*offset */};
      int ret = kvs_store_tuple(cont_hd, &kvskey, &kvsvalue, &put_ctx);
      
      while (ret != KVS_SUCCESS) {
	fprintf(stderr, "store tuple failed %d\n", ret); exit(1);
	ret = kvs_store_tuple(cont_hd, &kvskey, &kvsvalue, &put_ctx);
      }    

      cur_qdepth++;
      num_submitted++;
    }


    if(is_polling) {
      if (cur_qdepth > 0) {
	int nr = kvs_get_ioevents(cont_hd, maxdepth);
	cur_qdepth        -= nr;
      }
    }
    if (cur_qdepth == maxdepth) usleep(1);
    
  }
  
  // wait until it finishes
  if(is_polling) {
    while (cur_qdepth > 0) {
      int nr = kvs_get_ioevents(cont_hd, maxdepth);
      cur_qdepth        -= nr;
    }
  } else {
    while(completed < num_submitted) {
      usleep(1);
    }
  }

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

int perform_delete(kvs_container_handle cont_hd, int count, int maxdepth, uint16_t klen, uint32_t vlen, int is_polling) {
  int cur_qdepth = 0;
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

  long int seq = 1;

  fprintf(stdout, "\n===========\n");
  fprintf(stdout, "   Do Delete Operation %s\n", is_polling ? "Polling" : "Interrupt");
  fprintf(stdout, "===========\n"); 
  
  while (num_submitted < count) {
    while (cur_qdepth < maxdepth) {
      if (num_submitted >= count) break;
      pthread_mutex_lock(&lock);
      char *key   = keypool.front(); keypool.pop();
      pthread_mutex_unlock(&lock);
      sprintf(key, "%0*ld", klen - 1, seq++);
      const kvs_key  kvskey = { key, klen};
      const kvs_delete_context del_ctx = { KVS_DELETE_TUPLE , 0, &keypool, &valuepool};
      int ret = kvs_delete_tuple(cont_hd, &kvskey, &del_ctx);
      if (ret != KVS_SUCCESS) {
	fprintf(stderr, "delete tuple failed\n"); exit(1);
      }
      
      cur_qdepth++;
      num_submitted++;
    }
    
    if(is_polling) {
      if (cur_qdepth > 0) {
	int nr = kvs_get_ioevents(cont_hd, maxdepth);
	cur_qdepth        -= nr;

      }
    }
    
    if (cur_qdepth == maxdepth) usleep(1);
  }

  // wait until it finishes
  if(is_polling) {
    while (cur_qdepth > 0) {
      int nr = kvs_get_ioevents(cont_hd, maxdepth);
      cur_qdepth        -= nr;
    }
  } else {
    while(completed < num_submitted) {
      usleep(1);
    }
  }

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
  uint16_t klen = 16;
  uint32_t vlen = 4096;
  int is_polling = 1;
  int c;
 
  while ((c = getopt(argc, argv, "d:n:q:o:k:v:p:h")) != -1) {
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
    case 'p':
      is_polling = atoi(optarg);
      break;
    case 'h':
      usage(argv[0]);
      exit(1);
      break;
    default:
      usage(argv[0]);
      break;
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
  options.aio.iocomplete_fn = complete;
  options.memory.use_dpdk = 0;
  options.aio.queuedepth = qdepth;
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
    is_polling = 0;
    options.udd.mem_size_mb = 1024;
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset); // CPU 0
    sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);    
    
  } else {
    use_udd = 0;
    options.memory.use_dpdk = 0;
  }
  options.aio.is_polling = is_polling;

  if(is_polling)
    async_mode = 0; // polling
  else
    async_mode = 1; // interrupt
  
  // initialize the environment
  kvs_init_env(&options);

  kvs_device_handle dev;
  dev = kvs_open_device(dev_path);

  kvs_container_context ctx;
  kvs_create_container(dev, "test", 4, &ctx);
  kvs_container_handle cont_handle = kvs_open_container(dev, "test");
  
  switch(op_type) {
  case WRITE_OP:
    perform_insertion(cont_handle, num_ios, qdepth, klen, vlen, is_polling);
    break;
  case READ_OP:
    perform_insertion(cont_handle, num_ios, qdepth, klen, vlen, is_polling);
    perform_read(cont_handle, num_ios, qdepth, klen, vlen, is_polling);
    break;
  case DELETE_OP:
    perform_insertion(cont_handle, num_ios, qdepth, klen, vlen, is_polling);
    perform_delete(cont_handle, num_ios, qdepth, klen, vlen, is_polling);
    break;
  case ITERATOR_OP:
    perform_insertion(cont_handle, num_ios, qdepth, klen, vlen, is_polling);
    perform_iterator(cont_handle, is_polling, 0);
    /* Iterator a key-value pair only works in emulator */
    //perform_iterator(dev, is_polling, 1);
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
