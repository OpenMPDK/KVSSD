#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>
#include <mutex>
#include <queue>

#include "kvs_api.h"
#include "libcouchstore/couch_db.h"
#include "stopwatch.h"
#include "arch.h"
#include "memory.h"
#include "workload.h"

#define workload_check (0)
#define LATENCY_CHECK  // only for async IO completion latency
#define MAX_SAMPLES 1000000
static int use_udd = 0;
static int kdd_is_polling = 1;
#define GB_SIZE (1024*1024*1024)

struct _db {
  int id;
  kvs_device_handle dev;
  kvs_container_handle cont_hd;
  int context_idx;
  IoContext contexts[256];

  std::queue<IoContext*> *iocontexts;
  std::queue<IoContext*> *iodone;
  // only used for async 
  latency_stat *l_read;
  latency_stat *l_write;
  latency_stat *l_delete;
  pthread_mutex_t mutex;

  std::queue<kvs_key*> *kvs_key_pool;
  std::queue<kvs_value*> *kvs_value_pool;
  std::mutex lock_k;
  
  /* For iterator  */
  kvs_iterator_handle iter_handle;
  int has_iter_finish;
  kvs_iterator_list iter_list;

};

static int kv_write_mode = 0;
static int queue_depth = 8;
static int aiothreads_per_device = 2;
static uint64_t coremask = 0;
static int32_t aio_count = 0;
char udd_core_masks[256];
char udd_cq_thread_masks[256];
uint32_t udd_mem_size_mb = 1024;
kvs_iterator_option g_iter_mode;
#define iter_read_size (32 * 1024)

void print_iterator_keyvals(kvs_iterator_list *iter_list){
  uint8_t *it_buffer = (uint8_t *) iter_list->it_list;
  uint32_t num_entries = iter_list->num_entries;

  if(g_iter_mode.iter_type) {
    uint32_t klen  = 16; // Only support fixed key length for iterator
    uint32_t vlen = sizeof(kvs_value_t);
    uint32_t vlen_value = 0;

    for(int i = 0;i < iter_list->num_entries; i++) {
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
    // For fixed key length
    /*
    for(int i = 0; i < iter_list->num_entries; i++)
      fprintf(stdout, "Iterator get key %s\n",  it_buffer + i * 16);
    */

    // for ETA50K24 firmware with various key length
    uint32_t key_size = 0;
    char key[256];

    for(int i = 0;i < iter_list->num_entries; i++) {
      // get key size
      key_size = *((unsigned int*)it_buffer);
      it_buffer += sizeof(unsigned int);
      // print key
      memcpy(key, it_buffer, key_size);
      key[key_size] = 0;
      fprintf(stdout, "%dth key --> %s\n", i, key);
      it_buffer += key_size;
    }
    
  }
}

void release_kvskeyvalue(Db *db, kvs_key *key, kvs_value *value){
  //memset(key, 0, sizeof(kvs_key));
  std::unique_lock<std::mutex> lock(db->lock_k);
  if(key)
    db->kvs_key_pool->push(key);
  if(value)
    db->kvs_value_pool->push(value);
}

void add_event(Db *db, IoContext *ctx) {
  std::unique_lock<std::mutex> lock(db->lock_k);
  db->iodone->push(ctx);

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

uint64_t coreid_to_mask(char *core_ids)
{
    int i, tmp = 0, found = 0;
    uint64_t coremask = 0;

    for (i = 0; i < strlen(core_ids); i++) {
      if((core_ids[i] == ',' || core_ids[i] == '}') && found == 1) {
	const int coreid = tmp;
	coremask |= (1ULL << coreid);
	tmp = 0;
	found = 0;
      }
      else if (core_ids[i] <= '9' && core_ids[i] >= '0'){
	tmp = tmp * 10 + core_ids[i] - '0';
	found = 1;
      }
    }
    return coremask;
}

void free_doc(Doc *doc)
{
  if (doc->id.buf) free(doc->id.buf); 
  if (doc->data.buf) free(doc->data.buf);
}

void on_io_complete(kvs_callback_context* ioctx) {
  if(ioctx->result != KVS_SUCCESS && ioctx->result != KVS_ERR_KEY_NOT_EXIST) {
    fprintf(stdout, "io error: op = %d, key = %s, result = %s\n", ioctx->opcode, ioctx->key? (char*)ioctx->key->key:0, kvs_errstr(ioctx->result));
    exit(1);
  }

  auto owner = (struct _db*)ioctx->private1;
  const auto context_idx = owner->context_idx;
  latency_stat *l_stat;

  if(use_udd) {
    std::unique_lock<std::mutex> lock(owner->lock_k);
    IoContext *ctx = owner->iocontexts->front();
    owner->iocontexts->pop();
    lock.unlock();
    if(ctx == NULL) {
      fprintf(stderr, "Not enough context, outstanding %d\n", owner->iodone->size());
      exit(1);
    }

    switch(ioctx->opcode) {
    case IOCB_ASYNC_PUT_CMD:
      //ctx->op = OP_INSERT;
      ctx->value = ioctx->value->value;
      ctx->key = ioctx->key->key;
      l_stat = owner->l_write;
      release_kvskeyvalue(owner, ioctx->key, ioctx->value); 
      break;
    case IOCB_ASYNC_GET_CMD:
      //ctx->op= OP_GET;
      ctx->value = ioctx->value->value;
      ctx->key = ioctx->key->key;
      l_stat = owner->l_read;
      release_kvskeyvalue(owner, ioctx->key, ioctx->value); 
      break;
    case IOCB_ASYNC_DEL_CMD:
      //ctx->op= OP_DEL;
      ctx->key = ioctx->key->key;
      ctx->value = NULL;
      l_stat = owner->l_delete;
      release_kvskeyvalue(owner, ioctx->key, 0); 
      break;
    case IOCB_ASYNC_ITER_NEXT_CMD:
      //ctx->op = OP_ITER_NEXT;
      //print_iterator_keyvals(&owner->iter_list);
      ctx->key = ctx->value = NULL;
      owner->has_iter_finish = 1;
      break;
    }
  
    //ctx->key = ioctx->key->key;
    add_event(owner, ctx);
  } else {  // kdd
    if(kdd_is_polling) {
      switch(ioctx->opcode) {
	/*
      case IOCB_ASYNC_PUT_CMD:
	owner->contexts[context_idx].op= OP_INSERT;
	owner->contexts[context_idx].value = ioctx->value;
	owner->contexts[context_idx].valuelength = ioctx->valuesize;
	owner->contexts[context_idx].freebuf = 0;
	owner->contexts[context_idx].key = ioctx->key;
	owner->contexts[context_idx].keylength = ioctx->keysize;
	l_stat = owner->l_write;
	//fprintf(stdout, "finish write %s - %d \n", ioctx->key, ioctx->valuesize);
	break;
      case IOCB_ASYNC_GET_CMD:
	owner->contexts[context_idx].op= OP_GET;
	owner->contexts[context_idx].value = ioctx->value;
	owner->contexts[context_idx].valuelength = ioctx->valuesize;
	owner->contexts[context_idx].freebuf = 1;
	owner->contexts[context_idx].key = ioctx->key;
	owner->contexts[context_idx].keylength = ioctx->keysize;
	l_stat = owner->l_read;
	break;
      case IOCB_ASYNC_DEL_CMD:
	owner->contexts[context_idx].op= OP_DEL;
	owner->contexts[context_idx].freebuf = 0;
	owner->contexts[context_idx].key = ioctx->key;
	owner->contexts[context_idx].keylength = ioctx->keysize;
	owner->contexts[context_idx].value = NULL;
	owner->contexts[context_idx].valuelength = 0;
	l_stat = owner->l_delete;
	break;
      case IOCB_ASYNC_ITER_NEXT_CMD:
	//print_iterator_keyvals(&owner->iter_list);
	owner->contexts[context_idx].op= OP_ITER_NEXT;
	owner->contexts[context_idx].freebuf = 0;
	owner->contexts[context_idx].key = NULL;
	owner->contexts[context_idx].value = NULL;
	owner->has_iter_finish = 1;
	memset(owner->iter_list.it_list, 0, 32*1024);
	break;
	*/
      }
      owner->context_idx++;

    } else {  // interrupt mode
      //IoContext *ctx = pop_ctx(owner, 0);
      std::unique_lock<std::mutex> lock(owner->lock_k);
      IoContext *ctx = owner->iocontexts->front();
      owner->iocontexts->pop();
      lock.unlock();
      if(ctx == NULL) {
	fprintf(stderr, "Not enough context, outstanding %ld\n", owner->iodone->size());
	exit(1);
      }

      switch(ioctx->opcode) {
      case IOCB_ASYNC_PUT_CMD:
	//ctx->op = OP_INSERT;
	ctx->value = ioctx->value->value;
	ctx->key = ioctx->key->key;
	l_stat = owner->l_write;
	release_kvskeyvalue(owner, ioctx->key, ioctx->value);
	break;
      case IOCB_ASYNC_GET_CMD:
	//ctx->op= OP_GET;
	ctx->value = ioctx->value->value;
	ctx->key = ioctx->key->key;
	l_stat = owner->l_read;
	release_kvskeyvalue(owner, ioctx->key, ioctx->value);
	break;
      case IOCB_ASYNC_DEL_CMD:
	//ctx->op= OP_DEL;
	ctx->value = NULL;
	ctx->key = ioctx->key->key;
	l_stat = owner->l_delete;
	release_kvskeyvalue(owner, ioctx->key, 0);
	break;
      case IOCB_ASYNC_ITER_NEXT_CMD:
	//print_iterator_keyvals(&owner->iter_list);
	//ctx->op= OP_ITER_NEXT;
	ctx->key = NULL;
	ctx->value = NULL;
	std::unique_lock<std::mutex> lock(owner->lock_k);
	owner->has_iter_finish = 1;
	lock.unlock();
	break;
      }

      add_event(owner, ctx);
    } // end of interrupt mode

  }
  
#if defined LATENCY_CHECK
  if(ioctx->private2){
    struct timespec t11;
    unsigned long long start, end;
    uint64_t cur_sample;

    clock_gettime(CLOCK_REALTIME, &t11);
    end = t11.tv_sec * 1000000000L + t11.tv_nsec;
    end /= 1000L;
    start = *((unsigned long long*)ioctx->private2);
    if (l_stat->cursor >= MAX_SAMPLES) {
      l_stat->cursor = l_stat->cursor % MAX_SAMPLES;
      l_stat->nsamples = MAX_SAMPLES;
    } else {
      l_stat->nsamples = l_stat->cursor + 1; 
    }
    cur_sample = l_stat->cursor;
    l_stat->cursor++;
    l_stat->samples[cur_sample] = end - start;
    free(ioctx->private2);
  }
#endif
  
}

void on_iothread_ready(kvs_thread_t id) {}

void pass_lstat_to_db(Db *db, latency_stat *l_read, latency_stat *l_write, latency_stat *l_delete)
{
  db->l_read = l_read;
  db->l_write = l_write;
  db->l_delete = l_delete;
}

int release_context(Db *db, IoContext **contexts, int nr){

  if(use_udd || kdd_is_polling == 0) {
    for (int i =0 ;i < nr ; i++) {
      if (contexts[i]) {
	std::unique_lock<std::mutex> lock(db->lock_k);
	db->iocontexts->push(contexts[i]);
	//push_ctx(db, contexts[i], 0);
      }
    }
  }
  
  return 0;
}

int getevents(Db *db, int min, int max, IoContext_t **context)
{
  if(use_udd || kdd_is_polling == 0) {
    int i = 0;
    std::unique_lock<std::mutex> lock(db->lock_k);
    while(!db->iodone->empty() && i < max) {
      context[i] = db->iodone->front();
      db->iodone->pop();
      i++;
    }
    lock.unlock();
    return i;
  } else {  // kdd polling

    db->has_iter_finish = 0;
    //static std::mutex mu;
    //std::unique_lock<std::mutex> lock(mu);
    int ret = kvs_get_ioevents(db->cont_hd, max);

    for (int i= 0; i < ret; i ++) context[i] = &(db->contexts[i]);
    db->context_idx = 0;

    return ret;
  }
  
}

couchstore_error_t couchstore_kvs_set_aio_option(int kvs_queue_depth, char *core_masks, char *cq_thread_masks, uint32_t mem_size_mb)
{
  queue_depth = kvs_queue_depth;
  strcpy(udd_core_masks, core_masks);
  strcpy(udd_cq_thread_masks, cq_thread_masks);
  udd_mem_size_mb = mem_size_mb;
  return COUCHSTORE_SUCCESS;
}

couchstore_error_t couchstore_kvs_set_aiothreads(int kvs_aio_threads)
{
  aiothreads_per_device = kvs_aio_threads;
  return COUCHSTORE_SUCCESS;
}

couchstore_error_t couchstore_kvs_set_coremask(char *kvs_core_ids)
{
  coremask = coreid_to_mask(kvs_core_ids);
  return COUCHSTORE_SUCCESS;
}

couchstore_error_t couchstore_kvs_get_aiocompletion(int32_t *count)
{
  *count = aio_count;
  return COUCHSTORE_SUCCESS;
}

couchstore_error_t couchstore_kvs_reset_aiocompletion(){
  aio_count = 0;
  return COUCHSTORE_SUCCESS;
}

couchstore_error_t couchstore_setup_device(const char *dev_path,
					   char **dev_names,
					   char *config_file,
					   int num_devices, int write_mode,
					   int is_polling)

{  
  kvs_init_options options;
  kvs_init_env_opts(&options);

  int socket, core;
  get_curcpu(&socket, &core);
  uint64_t cpumask = 0;
  //TODO: add core mask
  cpumask |= (1ULL << core);

  fprintf(stderr, "master core = %d, mask = %lx\n ", core, cpumask);
  options.aio.iocoremask = cpumask;

  options.aio.queuedepth = queue_depth;
  //options.aio.is_polling = is_polling;
  options.emul_config_file = config_file;
  kdd_is_polling = is_polling;
  
  if(dev_path[1] == 'd'){ // /dev/kvemul or /dev/nvme kernel driver
    options.memory.use_dpdk = 0;
    use_udd = 0;
  } else {
    options.memory.use_dpdk = 1;
    use_udd = 1;
  }
  
  /* udd options */
  strcpy(options.udd.core_mask_str, udd_core_masks);
  strcpy(options.udd.cq_thread_mask, udd_cq_thread_masks);

  options.udd.mem_size_mb = udd_mem_size_mb;
  options.udd.syncio = write_mode;
  
  kvs_init_env(&options);
  kv_write_mode = write_mode;
  
  fprintf(stdout, "device init done\n");

  return COUCHSTORE_SUCCESS;
}

couchstore_error_t couchstore_close_device(int32_t dev_id)
{

  // kvs_close_device(dev_id);
}

couchstore_error_t couchstore_exit_env(){
  
  kvs_exit_env();

  return COUCHSTORE_SUCCESS; 
}

LIBCOUCHSTORE_API
couchstore_error_t couchstore_open_db_kvs(const char *dev_path,
					  Db **pDb, int id)
{

  int ret;
  Db *ppdb;
  *pDb = (Db*)malloc(sizeof(Db));
  ppdb = *pDb;

  ret = kvs_open_device(dev_path, &ppdb->dev);
  if(ret != KVS_SUCCESS) {
    fprintf(stderr, "Device open failed %s\n", kvs_errstr(ret));
    exit(1);
  }
  
  ppdb->id = id;
  ppdb->context_idx = 0;
  //ppdb->iter_handle = NULL;
  ppdb->has_iter_finish = 1;
  memset(ppdb->contexts, 0, sizeof(ppdb->contexts));
  pthread_mutex_init(&(ppdb->mutex), NULL);

  if(use_udd == 1 || kdd_is_polling == 0) {
    IoContext *context = NULL;
    ppdb->iocontexts = new std::queue<IoContext*>;
    ppdb->iodone = new std::queue<IoContext*>;
    
    ppdb->kvs_key_pool = new std::queue<kvs_key*>;
    ppdb->kvs_value_pool = new std::queue<kvs_value*>;
    
    kvs_key *key = NULL;
    kvs_value *value = NULL;
    for (int i =0; i < 36000; i++) {
      context = (IoContext *)malloc(sizeof(IoContext));
      if(context == NULL){
	fprintf(stderr, "Can not allocate db context\n");
	exit(0);
      }

      ppdb->iocontexts->push(context);
      
      key = (kvs_key*)malloc(sizeof(kvs_key));
      ppdb->kvs_key_pool->push(key);

      value = (kvs_value*)malloc(sizeof(kvs_value));
      ppdb->kvs_value_pool->push(value);
      
    }
  }
    
  /* Container related op */
  kvs_container_context ctx;
  kvs_create_container(ppdb->dev, "test", 4, &ctx);
  kvs_open_container(ppdb->dev, "test", &ppdb->cont_hd);
  
  fprintf(stdout, "device open %s\n", dev_path);

  return COUCHSTORE_SUCCESS;
}

LIBCOUCHSTORE_API
couchstore_error_t couchstore_close_db(Db *db)
{
  if(use_udd || kdd_is_polling == 0) {
    IoContext *tmp;

    std::unique_lock<std::mutex> lock(db->lock_k);

    while(!db->iocontexts->empty()) {
      tmp = db->iocontexts->front();
      db->iocontexts->pop();
      free(tmp);
    }
    delete db->iocontexts;
    delete db->iodone;

    kvs_key *key;
    kvs_value *value;
    while(!db->kvs_key_pool->empty()) {
      key = db->kvs_key_pool->front();
      db->kvs_key_pool->pop();
      free(key);
    }
    delete db->kvs_key_pool;
    
    while(!db->kvs_value_pool->empty()) {
      value = db->kvs_value_pool->front();
      db->kvs_value_pool->pop();
      free(value);
    }
    delete db->kvs_value_pool;
  }

  kvs_close_container(db->cont_hd);
  free(db);
    
  return COUCHSTORE_SUCCESS;
}

LIBCOUCHSTORE_API
couchstore_error_t couchstore_db_info(Db *db, DbInfo* info)
{
    // TBD: need to get container information here, device usage
    return COUCHSTORE_SUCCESS;
}

couchstore_error_t kvs_store_sync(Db *db, Doc* const docs[],
				   unsigned numdocs, couchstore_save_options options)
{
  int i, ret;
  kvs_store_option option; 
  for(i = 0; i < numdocs; i++){
    const kvs_store_context put_ctx = {option, db, NULL};
    const kvs_key kvskey = {docs[i]->id.buf, (kvs_key_t)docs[i]->id.size};
    const kvs_value kvsvalue = { docs[i]->data.buf, (uint32_t)docs[i]->data.size , 0, 0 };

    ret = kvs_store_tuple(db->cont_hd, &kvskey, &kvsvalue, &put_ctx);

    if(ret != KVS_SUCCESS) {
      fprintf(stderr, "KVBENCH: store tuple sync failed %s 0x%x\n", (char*)docs[i]->id.buf, ret);
      exit(1);
    }
  }

  return COUCHSTORE_SUCCESS;
}

couchstore_error_t kvs_store_async(Db *db, Doc* const docs[],
				   unsigned numdocs, couchstore_save_options options)
{

  int ret;

  assert(numdocs == 1);
  kvs_store_option option; 
  kvs_store_context put_ctx; //{KVS_STORE_POST , 0, db, NULL};

  std::unique_lock<std::mutex> lock(db->lock_k);
  kvs_key *kvskey = db->kvs_key_pool->front();
  db->kvs_key_pool->pop();
  kvs_value *kvsvalue = db->kvs_value_pool->front();
  db->kvs_value_pool->pop();
  lock.unlock();
  if(kvskey == NULL || kvsvalue == NULL) {fprintf(stdout, "No elem in the kvs key/value_pool\n"); exit(0);}

  kvskey->key = docs[0]->id.buf;
  kvskey->length=  (kvs_key_t)docs[0]->id.size;

  kvsvalue->value = docs[0]->data.buf;
  kvsvalue->length = (uint32_t)docs[0]->data.size;
  kvsvalue->actual_value_size = kvsvalue->offset = 0;
  
  put_ctx.option = option;
  put_ctx.private1 = db;
  put_ctx.private2 = NULL;
  
#if defined LATENCY_CHECK
  if(options == 1){
    struct timespec t11;
    unsigned long long nanosec;
    clock_gettime(CLOCK_REALTIME, &t11);
    unsigned long long *p1 = (unsigned long long *)malloc(sizeof(unsigned long long));
    nanosec = t11.tv_sec * 1000000000L + t11.tv_nsec;
    nanosec /= 1000L;
    put_ctx.private2 = p1;
    *p1 = nanosec;
  }
#endif

  ret = kvs_store_tuple_async(db->cont_hd, kvskey, kvsvalue, &put_ctx, on_io_complete);
  while (ret) {
    ret = kvs_store_tuple_async(db->cont_hd, kvskey, kvsvalue, &put_ctx, on_io_complete);
  }

  return COUCHSTORE_SUCCESS;
  
}
 
LIBCOUCHSTORE_API
couchstore_error_t couchstore_save_documents(Db *db, Doc* const docs[], DocInfo *infos[],
		   unsigned numdocs, couchstore_save_options options)
{
  if(kv_write_mode == 1)
    return kvs_store_sync(db, docs, numdocs, options);
  else
    return kvs_store_async(db, docs, numdocs, options);
}
 
 
LIBCOUCHSTORE_API
couchstore_error_t couchstore_save_document(Db *db, const Doc *doc, DocInfo *info,
        couchstore_save_options options)
{

  return couchstore_save_documents(db, (Doc**)&doc, (DocInfo**)&info, 1, options);;
}


couchstore_error_t couchstore_iterator_open(Db *db, int iterator_mode) {

  kvs_iterator_context iter_ctx;
  /*
  if(db->iter_handle != NULL) {
    fprintf(stderr, "Device only support one iterator now, please close other iterators\n");
    exit(1);
  }
  */
  iter_ctx.bitmask = 0xffff0000;
  char prefix_str[5] = "0000";
  unsigned int PREFIX_KV = 0;
  for (int i = 0; i < 4; i++){
    PREFIX_KV |= (prefix_str[i] << i*8);
  }
  iter_ctx.bit_pattern = PREFIX_KV;

  kvs_iterator_option option;
  memset(&option, 0, sizeof(kvs_iterator_option));
  if(iterator_mode == 0) {
    g_iter_mode.iter_type = KVS_ITERATOR_KEY;
    option.iter_type = KVS_ITERATOR_KEY;
  } else {
    g_iter_mode.iter_type = KVS_ITERATOR_KEY_VALUE;
    option.iter_type = KVS_ITERATOR_KEY_VALUE;
  }
  iter_ctx.option = option;
  iter_ctx.private1 = db;
  iter_ctx.private2 = NULL;

  //kvs_close_iterator_all(db->cont_hd);
  int ret = kvs_open_iterator(db->cont_hd, &iter_ctx, &db->iter_handle);
  if(ret) {
    fprintf(stdout, "open iter failed with err %s\n", kvs_errstr(ret));
    exit(1);
  }
  db->iter_list.end = 0;
  db->iter_list.num_entries = 0;
  db->iter_list.size = iter_read_size;

  uint8_t *buffer;
  buffer =(uint8_t*) kvs_malloc(iter_read_size, 4096);
  db->iter_list.it_list = (uint8_t*)buffer;
  
  return COUCHSTORE_SUCCESS; 
}

couchstore_error_t couchstore_iterator_close(Db *db) {
  int ret;

  if(db->iter_handle) {
    kvs_iterator_context iter_ctx;
    iter_ctx.private1 = db;
    iter_ctx.private2 = NULL;
    ret = kvs_close_iterator(db->cont_hd, db->iter_handle, &iter_ctx);
    //db->iter_handle = NULL;
    if(db->iter_list.it_list) kvs_free(db->iter_list.it_list);
    fprintf(stdout, "Iterator closed \n");
  }
  return COUCHSTORE_SUCCESS;
}

couchstore_error_t couchstore_iterator_next(Db *db) {
  kvs_iterator_context iter_ctx;
  kvs_iterator_option option;
  iter_ctx.option = option;//KVS_ITER_DEFAULT;
  iter_ctx.private1 = db;
  iter_ctx.private2 = NULL;

  kvs_iterator_list *iter_list = &db->iter_list;
  iter_list->size = iter_read_size;
  
  //  if(use_udd){
  std::unique_lock<std::mutex> lock(db->lock_k);
  db->has_iter_finish = 0;
  lock.unlock();
    //}

  memset(iter_list->it_list, 0, iter_read_size);
  
  kvs_iterator_next_async(db->cont_hd, db->iter_handle, iter_list, &iter_ctx, on_io_complete);

  return COUCHSTORE_SUCCESS;
}

bool couchstore_iterator_check_status(Db *db) {

  return db->iter_list.end;
}

int couchstore_iterator_get_numentries(Db *db) {

  return db->has_iter_finish ? db->iter_list.num_entries : 0;
}

int couchstore_iterator_has_finish(Db *db) {
  return db->has_iter_finish;
}

couchstore_error_t kvs_get_async(Db *db, sized_buf *key, sized_buf *value,
				 couchstore_open_options options)

{
  int ret;
  kvs_retrieve_context ret_ctx;// = { KVS_RETRIEVE_IDEMPOTENT, 0, db, NULL};

  std::unique_lock<std::mutex> lock(db->lock_k);
  kvs_key *kvskey = db->kvs_key_pool->front();
  db->kvs_key_pool->pop();
  kvs_value *kvsvalue = db->kvs_value_pool->front();
  db->kvs_value_pool->pop();
  lock.unlock();
  
  if(kvskey == NULL || kvsvalue == NULL) {fprintf(stdout, "No elem in the kvs key/value_pool\n"); exit(0);}
  
  kvskey->key = key->buf;
  kvskey->length = (kvs_key_t)key->size;

  kvsvalue->value = value->buf;
  kvsvalue->length = (uint32_t)value->size;
  kvsvalue->actual_value_size = kvsvalue->offset = 0;
  
  kvs_retrieve_option option;
  ret_ctx.option = option; 
  ret_ctx.private1 = db;
  ret_ctx.private2 = NULL;
    
#if defined LATENCY_CHECK
  if(options == 1){
    struct timespec t11;
    unsigned long long nanosec;
    clock_gettime(CLOCK_REALTIME, &t11);
    unsigned long long *p1 = (unsigned long long *)malloc(sizeof(unsigned long long));
    nanosec = t11.tv_sec * 1000000000L + t11.tv_nsec;
    nanosec /= 1000L;
    ret_ctx.private2 = p1;
    *p1 = nanosec;
  }
#endif
  
  ret = kvs_retrieve_tuple_async(db->cont_hd, kvskey, kvsvalue, &ret_ctx, on_io_complete);
  while(ret) {
    ret = kvs_retrieve_tuple_async(db->cont_hd, kvskey, kvsvalue, &ret_ctx, on_io_complete);
  }

  return COUCHSTORE_SUCCESS; 
}

 couchstore_error_t kvs_get_sync(Db *db, sized_buf *key, sized_buf *value,
				 couchstore_open_options options)
{
  int ret;
  kvs_retrieve_option option;
  const kvs_retrieve_context ret_ctx = { option,db, NULL };
  const kvs_key  kvskey = { key->buf, (kvs_key_t)key->size };
  kvs_value kvsvalue = { value->buf, (uint32_t)value->size , 0, 0 /*offset */};
  
  ret = kvs_retrieve_tuple(db->cont_hd, &kvskey, &kvsvalue, &ret_ctx);

  if(ret != KVS_SUCCESS) {
    fprintf(stderr, "KVBENCH: retrieve tuple sync failed for %s, err 0x%x\n", (char*)key->buf, ret);
    //exit(1);
  } 
    
  return COUCHSTORE_SUCCESS; 
}
 
couchstore_error_t couchstore_open_document_kv (Db *db,
						sized_buf *key,
						sized_buf *value,
						couchstore_open_options options)
{
   if(kv_write_mode == 1)
     return kvs_get_sync(db, key, value, options);
   else
     return kvs_get_async(db, key, value, options);

   return COUCHSTORE_SUCCESS;

}
 
LIBCOUCHSTORE_API
couchstore_error_t couchstore_open_document(Db *db,
                                            const void *id,
                                            size_t idlen,
                                            Doc **pDoc,
                                            couchstore_open_options options)
{
  
    return COUCHSTORE_SUCCESS;
}

couchstore_error_t kvs_delete_sync(Db *db,
				   sized_buf *key,
				   couchstore_open_options options)
{
  int ret;
  kvs_delete_option option;
  kvs_delete_context del_ctx = { option, db, 0 };

  const kvs_key  kvskey = { key->buf,(kvs_key_t) key->size };
  ret = kvs_delete_tuple(db->cont_hd, &kvskey, &del_ctx);

  return COUCHSTORE_SUCCESS;
}

 couchstore_error_t kvs_delete_async(Db *db, sized_buf *key,
				    couchstore_open_options options)
{

  int ret;
  kvs_delete_option option;
  kvs_delete_context del_ctx;// = { KVS_DELETE_TUPLE, 0, db, NULL };

  std::unique_lock<std::mutex> lock(db->lock_k);  
  kvs_key *kvskey = db->kvs_key_pool->front();
  db->kvs_key_pool->pop();
  lock.unlock();
  if(kvskey == NULL) {fprintf(stdout, "No elem in the kvs key/value_pool\n"); exit(0);}
  kvskey->key = key->buf;
  kvskey->length= (kvs_key_t)key->size;
  
  del_ctx.option = option;//KVS_DELETE_TUPLE;
  del_ctx.private1 = db;
  del_ctx.private2 = NULL;

#if defined LATENCY_CHECK
  if(options == 1){
    struct timespec t11;
    unsigned long long nanosec;
    clock_gettime(CLOCK_REALTIME, &t11);
    unsigned long long *p1 = (unsigned long long *)malloc(sizeof(unsigned long long));
    nanosec = t11.tv_sec * 1000000000L + t11.tv_nsec;
    nanosec /= 1000L;
    del_ctx.private2 = p1;
    *p1 = nanosec;
  }
#endif
  
  ret = kvs_delete_tuple_async(db->cont_hd, kvskey, &del_ctx, on_io_complete);
  while(ret) {
    kvs_delete_tuple_async(db->cont_hd, kvskey, &del_ctx, on_io_complete);
  }
  
  return COUCHSTORE_SUCCESS;
 
}

couchstore_error_t couchstore_delete_document_kv(Db *db,
						 sized_buf *key,
						 couchstore_open_options options)
{
  if(kv_write_mode == 1)
    return kvs_delete_sync(db, key, options);
  else
    return kvs_delete_async(db, key, options);
}
 
LIBCOUCHSTORE_API
couchstore_error_t couchstore_delete_document(Db *db, 
					      const void *id,
					      size_t idlen,
					      couchstore_open_options options)
{
  return COUCHSTORE_SUCCESS; 
}

LIBCOUCHSTORE_API
couchstore_error_t couchstore_walk_id_tree(Db *db,
                                           const sized_buf* startDocID,
                                           couchstore_docinfos_options options,
                                           couchstore_walk_tree_callback_fn callback,
                                           void *ctx)
{
  // TBD: iterator - phase 2
  return COUCHSTORE_SUCCESS;
}

couchstore_error_t couchstore_kvs_malloc(size_t size_bytes, void **buf){
  *buf = kvs_malloc(size_bytes, 4096);
  return COUCHSTORE_SUCCESS;
}

LIBCOUCHSTORE_API
void couchstore_free_document(Doc *doc)
{
  if (doc->id.buf) kvs_free(doc->id.buf); 
  if (doc->data.buf) kvs_free(doc->data.buf);
  free(doc);
}


LIBCOUCHSTORE_API
void couchstore_free_docinfo(DocInfo *docinfo)
{

  free(docinfo);
}

LIBCOUCHSTORE_API
couchstore_error_t couchstore_commit(Db *db)
{
    // do nothing for KVS
    return COUCHSTORE_SUCCESS;
}
