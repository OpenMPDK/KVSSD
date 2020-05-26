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

#define SUCCESS 0
#define FAILED 1
#define WRITE_OP  1
#define READ_OP   2
#define DELETE_OP 3
#define ITERATOR_OP 4
#define KEY_EXIST_OP 5

void usage(char *program)
{
  printf("==============\n");
  printf("usage: %s -d device_path [-n num_ios] [-o op_type] [-k klen] [-v vlen] [-t threads]\n", program);
  printf("-d      device_path  :  kvssd device path. e.g. emul: /dev/kvemul; kdd: /dev/nvme0n1; udd: 0000:06:00.0\n");
  printf("-n      num_ios      :  total number of ios (ignore this for iterator)\n");
  printf("-o      op_type      :  1: write; 2: read; 3: delete; 4: iterator;"
                                " 5: key exist check;\n");
  printf("-k      klen         :  key length (ignore this for iterator)\n");
  printf("-v      vlen         :  value length (ignore this for iterator)\n");
  printf("-t      threads      :  number of threads\n");
  printf("==============\n");
}

#define iter_buff (32*1024)

struct iterator_info{
  kvs_iterator_handle iter_handle;
  kvs_iterator_list iter_list;
  int has_iter_finish;
  kvs_option_iterator g_iter_mode;
};

struct thread_args{
  int id;
  uint16_t klen;
  uint32_t vlen;
  int count;
  int op_type;
  kvs_key_space_handle ks_hd;
};

int perform_read(int id, kvs_key_space_handle ks_hd, int count, uint16_t klen, uint32_t vlen) {
  int ret;
  char *key   = (char*)kvs_malloc(klen, 4096);
  char *value = (char*)kvs_malloc(vlen, 4096);
  if(key == NULL || value == NULL) {
    fprintf(stderr, "failed to allocate\n");
    if(key) kvs_free(key);
    if(value) kvs_free(value);
    return FAILED;
  }

  int start_key = id * count;
  for (int i = start_key; i < start_key + count; i++) {
    memset(value, 0, vlen);
    sprintf(key, "%0*d", klen - 1, i);
    kvs_option_retrieve option;
    option.kvs_retrieve_delete = false;

    kvs_key kvskey = {key, klen};
    kvs_value kvsvalue = {value, vlen, 0, 0};

    ret = kvs_retrieve_kvp(ks_hd, &kvskey, &option, &kvsvalue);

    if(ret != KVS_SUCCESS) {
      fprintf(stderr, "retrieve tuple %s failed with error 0x%x\n", key, ret);  
      kvs_free(key);
      kvs_free(value);
      return FAILED;
    }
  }

  if(key) kvs_free(key);
  if(value) kvs_free(value);
  
  return SUCCESS;
}

int perform_insertion(int id, kvs_key_space_handle ks_hd, int count, uint16_t klen, uint32_t vlen) {
  char *key   = (char*)kvs_malloc(klen, 4096);
  char *value = (char*)kvs_malloc(vlen, 4096);
  if(key == NULL || value == NULL) {
    fprintf(stderr, "failed to allocate\n");
    if(key) kvs_free(key);
    if(value) kvs_free(value);
    return FAILED;
  }

  int start_key = id * count;
  for(int i = start_key; i < start_key + count; i++) {
    sprintf(key, "%0*d", klen - 1, i);
    kvs_option_store option;
    option.st_type = KVS_STORE_POST;
    option.assoc = NULL;
        
    kvs_key  kvskey = {key, klen};
    kvs_value kvsvalue = {value, vlen, 0, 0};

    kvs_result ret = kvs_store_kvp(ks_hd, &kvskey, &kvsvalue, &option);
    if(ret != KVS_SUCCESS ) {
      fprintf(stderr, "store tuple failed with error 0x%x\n", ret);
      kvs_free(key);
      kvs_free(value);
      return FAILED;
    } 

    if(i % 100 == 0)
      fprintf(stdout, "%d\r", i);
  }

  if(key) kvs_free(key);
  if(value) kvs_free(value);

  return SUCCESS;
}

int perform_delete(int id, kvs_key_space_handle ks_hd, int count, uint16_t klen, uint32_t vlen) {
  char *key  = (char*)kvs_malloc(klen, 4096);
  
  if(key == NULL) {
    fprintf(stderr, "failed to allocate\n");
    return FAILED;
  }

  int start_key = id * count;  
  kvs_option_delete option = {false};
  for(int i = start_key; i < start_key + count; i++) {
    sprintf(key, "%0*d", klen - 1, i);
    kvs_key kvskey = {key, klen};
    kvs_result ret = kvs_delete_kvp(ks_hd, &kvskey, &option);
    if(ret != KVS_SUCCESS) {
      fprintf(stderr, "delete tuple failed with error 0x%x\n", ret);
      kvs_free(key);
      return FAILED;
    } else {
      fprintf(stderr, "delete key %s done \n", key);
    }
  }

  if(key) kvs_free(key);
  return SUCCESS;
}

int perform_key_exist(int id, kvs_key_space_handle ks_hd, int count, uint16_t klen, uint32_t vlen) {
  char *key  = (char*)kvs_malloc(klen, 4096);
  if(key == NULL) {
    fprintf(stderr, "failed to allocate\n");
    return FAILED;
  }
  int start_key = id * count;
  uint8_t exist_status;
  kvs_exist_list list;
  list.result_buffer = &exist_status;
  list.length = 1;
  for(int i = start_key; i < start_key + count; i++) {
    sprintf(key, "%0*d", klen - 1, i);
    kvs_key kvskey = {key, klen};
    kvs_result ret = kvs_exist_kv_pairs(ks_hd, 1, &kvskey, &list);
    if(ret != KVS_SUCCESS)  {
      fprintf(stderr, "exist tuple failed with error 0x%x\n", ret);
      kvs_free(key);
      return FAILED;
    } else {
      fprintf(stderr, "check key %s exist? %s\n", key, exist_status == 0? "FALSE":"TRUE");
    }
  }

  if(key) kvs_free(key);
  return SUCCESS;
}

// Samsung device support KVS_ITERATOR_KEY iterator type only
int perform_iterator(kvs_key_space_handle ks_hd,
                      kvs_iterator_type iter_type=KVS_ITERATOR_KEY)
{
  struct iterator_info *iter_info = (struct iterator_info *)malloc(sizeof(struct iterator_info));
  iter_info->g_iter_mode.iter_type = iter_type;

  kvs_result ret;
  static int total_entries = 0;

  /* Open iterator */
  kvs_key_group_filter iter_fltr;

  iter_fltr.bitmask[0] = 0xff;
  iter_fltr.bitmask[1] = 0xff;
  iter_fltr.bitmask[2] = 0;
  iter_fltr.bitmask[3] = 0;

  iter_fltr.bit_pattern[0] = '0';
  iter_fltr.bit_pattern[1] = '0';
  iter_fltr.bit_pattern[2] = '0';
  iter_fltr.bit_pattern[3] = '0';

  ret = kvs_create_iterator(ks_hd, &(iter_info->g_iter_mode), &iter_fltr, &(iter_info->iter_handle));
  if(ret != KVS_SUCCESS) {
    fprintf(stderr, "iterator open fails with error 0x%x\n", ret);
    free(iter_info);
    return FAILED;
  }
    
  /* Do iteration */
  iter_info->iter_list.size = iter_buff;
  uint8_t *buffer;
  buffer =(uint8_t*)kvs_malloc(iter_buff, 4096);
  iter_info->iter_list.it_list = (uint8_t*) buffer;

  iter_info->iter_list.end = 0;
  iter_info->iter_list.num_entries = 0;

  struct timespec t1, t2;
  clock_gettime(CLOCK_REALTIME, &t1);

  while(1) {
    iter_info->iter_list.size = iter_buff;
    memset(iter_info->iter_list.it_list, 0, iter_buff);
    ret = kvs_iterate_next(ks_hd, iter_info->iter_handle, &(iter_info->iter_list));
    if(ret != KVS_SUCCESS) {
      fprintf(stderr, "iterator next fails with error 0x%x\n", ret);
      free(iter_info);
      kvs_free(buffer);
      return FAILED;
    }
        
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
  ret = kvs_delete_iterator(ks_hd, iter_info->iter_handle);
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

void do_io(int id, kvs_key_space_handle ks_hd, int count, uint16_t klen, uint32_t vlen, int op_type) {

  switch(op_type) {
  case WRITE_OP:
    perform_insertion(id, ks_hd, count, klen, vlen);
    break;
  case READ_OP:
    //perform_insertion(id, ks_hd, count, klen, vlen);
    perform_read(id, ks_hd, count, klen, vlen);
    break;
  case DELETE_OP:
    //perform_insertion(id, ks_hd, count, klen, vlen);
    perform_delete(id, ks_hd, count, klen, vlen);
    break;
  case ITERATOR_OP:
    perform_iterator(ks_hd);
    //Iterator a key-value pair only works in emulator
    //perform_iterator(cont_handle, 1);
    break;
  case KEY_EXIST_OP:
    //perform_insertion(id, ks_hd, count, klen, vlen);
    perform_key_exist(id, ks_hd, count, klen, vlen);
    break;
  default:
    fprintf(stderr, "Please specify a correct op_type for testing\n");
    return;

  }
}

void *iothread(void *args)
{
  thread_args *targs = (thread_args *)args;
  do_io(targs->id, targs->ks_hd, targs->count, targs->klen, targs->vlen, targs->op_type);
  return NULL;
}

int _env_exit(kvs_device_handle dev, char* keyspace_name,
  kvs_key_space_handle ks_hd) {
  uint32_t dev_util = 0;
  kvs_get_device_utilization(dev, &dev_util);
  fprintf(stdout, "After: Total used is %d\n", dev_util);  
  kvs_close_key_space(ks_hd);
  kvs_key_space_name ks_name;
  ks_name.name_len = strlen(keyspace_name);
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
  int op_type = 1;
  uint16_t klen = 16;
  uint32_t vlen = 4096;
  int ret, c, t = 1;

  while ((c = getopt(argc, argv, "d:n:o:k:v:t:h")) != -1) {
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
    case 't':
      t = atoi(optarg);
      break;
    case 'h':
      usage(argv[0]);
      return SUCCESS;
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

  if(op_type == ITERATOR_OP && t > 1) {
    fprintf(stdout, "Iterator only supports single thread \n");
    return FAILED;
  }
    
  char ks_name[MAX_KEYSPACE_NAME_LEN];
  snprintf(ks_name, MAX_KEYSPACE_NAME_LEN, "%s", "keyspace_test");
  kvs_device_handle dev;
  kvs_key_space_handle ks_hd;
  if(_env_init(dev_path, &dev, ks_name, &ks_hd) != SUCCESS)
    return FAILED;

  thread_args args[t];
  pthread_t tid[t];

  struct timespec t1, t2;
  clock_gettime(CLOCK_REALTIME, &t1);

  for(int i = 0; i < t; i++){
    args[i].id = i;
    args[i].klen = klen;
    args[i].vlen = vlen;
    args[i].count = num_ios;
    args[i].ks_hd = ks_hd;
    args[i].op_type = op_type;
    pthread_attr_t *attr = (pthread_attr_t *)malloc(sizeof(pthread_attr_t));
    cpu_set_t cpus;
    pthread_attr_init(attr);
    CPU_ZERO(&cpus);
    CPU_SET(0, &cpus); // CPU 0
    pthread_attr_setaffinity_np(attr, sizeof(cpu_set_t), &cpus);

    ret = pthread_create(&tid[i], attr, iothread, &args[i]);
    if (ret != 0) { 
      fprintf(stderr, "thread exit\n");
      free(attr);
      return FAILED;
    }
    pthread_attr_destroy(attr);
    free(attr);
  }

  for(int i = 0; i < t; i++) {
    pthread_join(tid[i], 0);
  }

  if(op_type != ITERATOR_OP) {
    clock_gettime(CLOCK_REALTIME, &t2);
    unsigned long long start, end;
    start = t1.tv_sec * 1000000000L + t1.tv_nsec;
    end = t2.tv_sec * 1000000000L + t2.tv_nsec;
    double sec = (double)(end - start) / 1000000000L;
    fprintf(stdout, "Total time %.2f sec; Throughput %.2f ops/sec\n", sec, (double) num_ios * t /sec );
  }

  ret = _env_exit(dev, ks_name, ks_hd);
  return ret;
}
