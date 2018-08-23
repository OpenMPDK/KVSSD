#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>
#include <mutex>

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
int64_t VALUE_MAXLEN = 65536; // 64KB
#define GB_SIZE (1024*1024*1024)

struct _db {
  int id;
  kvs_device_handle dev;
  kvs_container_handle cont_hd;
  int context_idx;
  IoContext contexts[256];
  IoContext *iocontexts;
  IoContext *iodone;
  spin_t lock;
  spin_t lock_ioctx;
  long outstandingios;

  // only used for async 
  latency_stat *l_read;
  latency_stat *l_write;
  latency_stat *l_delete;
  pthread_mutex_t mutex;

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
int g_iter_mode = KVS_ITERATOR_OPT_KEY;
int g_iter_key_size = 16;
int iter_read_size = 32 * 1024;

std::atomic_uint_fast64_t ctx_count;


void print_iterator_keyvals(kvs_iterator_list *iter_list){
  uint8_t *it_buffer = (uint8_t *) iter_list->it_list;
  uint32_t num_entries = iter_list->num_entries;

  if(g_iter_mode == KVS_ITERATOR_OPT_KV) {
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
    for(int i = 0; i < iter_list->num_entries; i++)
      fprintf(stdout, "Iterator get key %s\n",  it_buffer + i * 16);
  }
}

void push_ctx(Db *db, IoContext *ctx, int is_iodone){
  if(ctx == NULL) return;
  if(is_iodone) {
    pthread_spin_lock(&db->lock);
    if (db->iodone == NULL) { //empty
      db->iodone = ctx;
      ctx->next = ctx->prev = NULL;
    } else{
      db->iodone->prev = ctx;
      ctx->next = db->iodone;
      db->iodone = ctx;
    }
    pthread_spin_unlock(&db->lock);
  } else {
    pthread_spin_lock(&db->lock_ioctx);
    if (db->iocontexts == NULL) { //empty
      db->iocontexts = ctx;
      ctx->next = ctx->prev = NULL;
    } else {
      db->iocontexts->prev = ctx;
      ctx->next = db->iocontexts;
      db->iocontexts = ctx;
    }
    pthread_spin_unlock(&db->lock_ioctx);
    if(db->iocontexts == NULL) {printf("should not happen \n"); exit(0);}
  }

}

IoContext *pop_ctx(Db *db, int is_iodone){
  IoContext *ctx = NULL;

  if(is_iodone){
    pthread_spin_lock(&db->lock);
    if(db->iodone == NULL){
      pthread_spin_unlock(&db->lock);
      return NULL;
    }
    ctx = db->iodone;
    db->iodone = ctx->next;
    pthread_spin_unlock(&db->lock);
  } else {
    pthread_spin_lock(&db->lock_ioctx);
    if(db->iocontexts == NULL)
      { 
	pthread_spin_unlock(&db->lock_ioctx);
	return NULL;
      }
    ctx = db->iocontexts;
    db->iocontexts = ctx->next;
    pthread_spin_unlock(&db->lock_ioctx);

  }

  ctx->prev = ctx->next = NULL;
  return ctx;
}

bool is_empty(IoContext *ctx) {

  return (ctx == NULL);

}

void add_event(Db *db, IoContext *ctx) {

  push_ctx(db, ctx, 1);

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

void on_io_complete(kv_iocb* ioctx) {
  if(ioctx->result != KVS_SUCCESS && ioctx->result != KVS_ERR_KEY_NOT_EXIST && ioctx->result != KVS_WRN_MORE && ioctx->result != KVS_ERR_ITERATOR_END) {
    fprintf(stdout, "io error: op = %d, key = %s, result = 0x%x\n", ioctx->opcode, (char*)ioctx->key, ioctx->result/*kvs_errstr(ioctx->result)*/);
    exit(1);
  }

  auto owner = (struct _db*)ioctx->private1;
  const auto context_idx = owner->context_idx;
  latency_stat *l_stat;

  if(use_udd) {
    IoContext *ctx = pop_ctx(owner, 0);
    if(ctx == NULL) {
      fprintf(stderr, "Not enough context, outstanding %ld\n", ctx_count.load());
      exit(1);
    }
    ctx_count.fetch_sub(1, std::memory_order_release);

    switch(ioctx->opcode) {
    case IOCB_ASYNC_PUT_CMD:
      ctx->op = OP_INSERT;
      ctx->value = ioctx->value;
      ctx->valuelength = ioctx->valuesize;
      ctx->freebuf = 0;
      l_stat = owner->l_write;
      break;
    case IOCB_ASYNC_GET_CMD:
      ctx->op= OP_GET;
      ctx->value = ioctx->value;
      ctx->valuelength = ioctx->valuesize;
      ctx->freebuf = 1;
      l_stat = owner->l_read;
      break;
    case IOCB_ASYNC_DEL_CMD:
      ctx->op= OP_DEL;
      ctx->freebuf = 0;
      l_stat = owner->l_delete;
      break;
    case IOCB_ASYNC_ITER_NEXT_CMD:
      ctx->op = OP_ITER_NEXT;
      //print_iterator_keyvals(&owner->iter_list);
      owner->has_iter_finish = 1;
      break;
    }
  
    ctx->key = ioctx->key;
    ctx->keylength = ioctx->keysize;
   
    add_event(owner, ctx);
  } else {  // kdd
    if(kdd_is_polling) {
      switch(ioctx->opcode) {
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
	/*
      case IOCB_ASYNC_ITER_OPEN_CMD:
	owner->iter_handle = ioctx->iter_handle;
	owner->iter_list.end = 0;
	owner->iter_list.num_entries = 0;
	owner->iter_list.size = 32*1024;
	uint8_t *buffer;
	posix_memalign((void **)&buffer, 4096, 32*1024);
	memset(buffer, 0, 32*1024);
	owner->iter_list.it_list = (uint8_t*) buffer;
	owner->contexts[context_idx].op= OP_ITER_OPEN;
	owner->contexts[context_idx].freebuf = 0;
	owner->contexts[context_idx].key = NULL;
	owner->contexts[context_idx].value = NULL;      
	break;
      case IOCB_ASYNC_ITER_CLOSE_CMD:
	if(owner->iter_list.it_list) free(owner->iter_list.it_list);
	owner->contexts[context_idx].op= OP_ITER_CLOSE;
	owner->contexts[context_idx].freebuf = 0;
	owner->contexts[context_idx].key = NULL;
	owner->contexts[context_idx].value = NULL;
	break;
	*/
      case IOCB_ASYNC_ITER_NEXT_CMD:
	//print_iterator_keyvals(&owner->iter_list);
	owner->contexts[context_idx].op= OP_ITER_NEXT;
	owner->contexts[context_idx].freebuf = 0;
	owner->contexts[context_idx].key = NULL;
	owner->contexts[context_idx].value = NULL;
	owner->has_iter_finish = 1;
	memset(owner->iter_list.it_list, 0, 32*1024);
	break;
      }
      owner->context_idx++;

    } else {  // interrupt mode
      IoContext *ctx = pop_ctx(owner, 0);
      if(ctx == NULL) {
	fprintf(stderr, "Not enough context, outstanding %ld\n", ctx_count.load());
	exit(1);
      }
      ctx_count.fetch_sub(1, std::memory_order_release);
      switch(ioctx->opcode) {
      case IOCB_ASYNC_PUT_CMD:
	ctx->op = OP_INSERT;
	ctx->value = ioctx->value;
	ctx->valuelength = ioctx->valuesize;
	ctx->freebuf = 0;
	ctx->key = ioctx->key;
	ctx->keylength = ioctx->keysize;
	l_stat = owner->l_write;	
	break;
      case IOCB_ASYNC_GET_CMD:
	ctx->op= OP_GET;
	ctx->value = ioctx->value;
	ctx->valuelength = ioctx->valuesize;
	ctx->key = ioctx->key;
	ctx->keylength = ioctx->keysize;
	ctx->freebuf = 1;
	l_stat = owner->l_read;
	break;
      case IOCB_ASYNC_DEL_CMD:
	ctx->op= OP_DEL;
	ctx->freebuf = 1;
	ctx->value = NULL;
	ctx->valuelength = 0;
	ctx->key = ioctx->key;
	ctx->keylength = ioctx->keysize;
	l_stat = owner->l_delete;
	break;
	/*
      case IOCB_ASYNC_ITER_OPEN_CMD:
	owner->iter_handle = ioctx->iter_handle;
	owner->iter_list.end = 0;
	owner->iter_list.num_entries = 0;
	owner->iter_list.size = 32*1024;
	uint8_t *buffer;
	posix_memalign((void **)&buffer, 4096, 32*1024);
	memset(buffer, 0, 32*1024);
	owner->iter_list.it_list = (uint8_t*) buffer;
	ctx->op= OP_ITER_OPEN;
	ctx->freebuf = 1;
	ctx->key = NULL;
	ctx->value = NULL;
	break;
      case IOCB_ASYNC_ITER_CLOSE_CMD:
	if(owner->iter_list.it_list) free(owner->iter_list.it_list);
	ctx->op= OP_ITER_CLOSE;
	ctx->freebuf = 0;
	ctx->key = NULL;
	ctx->value = NULL;
	break;
	*/
      case IOCB_ASYNC_ITER_NEXT_CMD:
	//print_iterator_keyvals(&owner->iter_list);
	ctx->op= OP_ITER_NEXT;
	ctx->freebuf = 0;
	ctx->key = NULL;
	ctx->value = NULL;
	owner->has_iter_finish = 1;
	memset(owner->iter_list.it_list, 0, 32*1024);
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
	//db->iocontexts.push(context[i]);
	push_ctx(db, contexts[i], 0);
      }
    }
    ctx_count.fetch_add(nr, std::memory_order_release);
  }
  
  return 0;
}

int getevents(Db *db, int min, int max, IoContext_t **context)
{
  if(use_udd || kdd_is_polling == 0) {
    int i = 0;
    while (!is_empty(db->iodone) && i < max) {
      context[i] = pop_ctx(db, 1);
      i++;
    }
    return i;
  } else {  // kdd

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
  if(write_mode == 1)  // sync io
    options.aio.iocomplete_fn = NULL;
  else 
    options.aio.iocomplete_fn = on_io_complete;
    
  options.aio.queuedepth = queue_depth;
  options.aio.is_polling = is_polling;
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

  Db *ppdb;
  *pDb = (Db*)malloc(sizeof(Db));
  ppdb = *pDb;

  kvs_open_device(dev_path, &ppdb->dev);
  ppdb->id = id;
  ppdb->context_idx = 0;
  ppdb->iter_handle = NULL;
  ppdb->has_iter_finish = 1;
  memset(ppdb->contexts, 0, sizeof(ppdb->contexts));
  pthread_mutex_init(&(ppdb->mutex), NULL);

  if(use_udd == 1 || kdd_is_polling == 0) {
    IoContext *context = NULL;
    pthread_spin_init(&ppdb->lock, 0);
    pthread_spin_init(&ppdb->lock_ioctx, 0);
    ppdb->outstandingios = 0;
    ppdb->iocontexts = ppdb->iodone = NULL;
    for (int i =0; i < 36000; i++) {
      context = (IoContext *)malloc(sizeof(IoContext));
      if(context == NULL){
	fprintf(stderr, "Can not allocate db context\n");
	exit(0);
      }
      context->prev = context->next = NULL;
      push_ctx(ppdb, context, 0);
    }
    ctx_count = 36000;
    if(use_udd == 1)
      fprintf(stdout, "Total size: %ld GB\nUsed size: %.2f %%\n", kvs_get_device_capacity(ppdb->dev) / GB_SIZE, (float)kvs_get_device_utilization(ppdb->dev) / 100);
  }

  /* Container related op */
  kvs_container_context ctx;
  kvs_create_container(ppdb->dev, "test_container", 4, &ctx);
  kvs_open_container(ppdb->dev, "test", &ppdb->cont_hd);
  
  fprintf(stdout, "device open %s\n", dev_path);

  return COUCHSTORE_SUCCESS;
}

LIBCOUCHSTORE_API
couchstore_error_t couchstore_close_db(Db *db)
{
  if(use_udd || kdd_is_polling == 0) {
    IoContext *tmp; 
    tmp = pop_ctx(db, 0);
    while (tmp){
      free(tmp);
      tmp = pop_ctx(db, 0);
    }
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

  for(i = 0; i < numdocs; i++){
    const kvs_store_context put_ctx = { KVS_STORE_POST | KVS_SYNC_IO, 0, db, NULL};
    const kvs_key kvskey = {docs[i]->id.buf, (uint16_t)docs[i]->id.size};
    const kvs_value kvsvalue = { docs[i]->data.buf, (uint32_t)docs[i]->data.size , 0 };

    ret = kvs_store_tuple(db->cont_hd, &kvskey, &kvsvalue, &put_ctx);

    if(ret != KVS_SUCCESS) {
      fprintf(stderr, "KVBENCH: store tuple sync failed %s\n", (char*)docs[i]->id.buf);
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
  kvs_store_context put_ctx; //{KVS_STORE_POST , 0, db, NULL};
  const kvs_key kvskey = {docs[0]->id.buf, (uint16_t)docs[0]->id.size};
  const kvs_value kvsvalue = { docs[0]->data.buf, (uint32_t)docs[0]->data.size , 0 };

  put_ctx.option = KVS_STORE_POST;
  put_ctx.reserved = 0;
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
    
  ret = kvs_store_tuple(db->cont_hd, &kvskey, &kvsvalue, &put_ctx);
  while (ret) {
    ret = kvs_store_tuple(db->cont_hd, &kvskey, &kvsvalue, &put_ctx);
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
  if(db->iter_handle != NULL) {
    fprintf(stderr, "Device only support one iterator now, please close other iterators\n");
    exit(1);
  }

  iter_ctx.bitmask = 0xffff0000;
  char prefix_str[5] = "0000";
  unsigned int PREFIX_KV = 0;
  for (int i = 0; i < 4; i++){
    PREFIX_KV |= (prefix_str[i] << i*8);
  }
  iter_ctx.bit_pattern = PREFIX_KV;

  g_iter_mode = (iterator_mode == 0 ? KVS_ITERATOR_OPT_KEY : KVS_ITERATOR_OPT_KV);
  iter_ctx.option = (iterator_mode == 0 ? KVS_ITERATOR_OPT_KEY : KVS_ITERATOR_OPT_KV);
  iter_ctx.private1 = db;
  iter_ctx.private2 = NULL;

  kvs_open_iterator(db->cont_hd, &iter_ctx, &db->iter_handle);
  db->iter_list.end = 0;
  db->iter_list.num_entries = 0;
  db->iter_list.size = iter_read_size;
  if(use_udd)
    db->iter_list.it_list = kvs_zalloc(iter_read_size, 4096);
  else {
    uint8_t *buffer;
    posix_memalign((void **)&buffer, 4096, iter_read_size);
    memset(buffer, 0, iter_read_size);
    db->iter_list.it_list = (uint8_t*) buffer;
  }

  
  //uint32_t iterator = kvs_open_iterator(db->cont_hd, &iter_ctx);
  /*
  if(use_udd) {
    db->iter_handle = (kvs_iterator_handle*)malloc(sizeof(kvs_iterator_handle));
    db->iter_handle->iterator = iterator;
    fprintf(stdout, "kvbench open iterator %d\n", db->iter_handle->iterator);

    db->iter_list.num_entries = 0;
    db->iter_list.size = udd_iter_read_size;
    db->iter_list.end = 0;
    db->iter_list.it_list = kvs_zalloc(udd_iter_read_size, 4096);
    if(db->iter_list.it_list == NULL) {
      fprintf(stderr, "Iterator failed to allocate read buffer\n");
      exit(1);
    }
  }
  */
  
  //*use_udd_it = use_udd;
  return COUCHSTORE_SUCCESS; 
}

couchstore_error_t couchstore_iterator_close(Db *db) {
  int ret;
  //*use_udd_it = use_udd;
  if(db->iter_handle) {
    kvs_iterator_context iter_ctx;
    iter_ctx.option = KVS_ITER_DEFAULT;
    iter_ctx.private1 = db;
    iter_ctx.private2 = NULL;
    ret = kvs_close_iterator(db->cont_hd, db->iter_handle, &iter_ctx);
    db->iter_handle = NULL;
    if(db->iter_list.it_list) free(db->iter_list.it_list);
    fprintf(stdout, "Iterator closed \n");
  }
  return COUCHSTORE_SUCCESS;
}

couchstore_error_t couchstore_iterator_next(Db *db) {
  kvs_iterator_context iter_ctx;
  iter_ctx.option = KVS_ITER_DEFAULT;
  iter_ctx.private1 = db;
  iter_ctx.private2 = NULL;

  kvs_iterator_list *iter_list = &db->iter_list;

  if(use_udd){ 
    db->has_iter_finish = 0;
  }
  
  kvs_iterator_next(db->cont_hd, db->iter_handle, iter_list, &iter_ctx);

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
  const kvs_key  kvskey = { key->buf, (uint16_t)key->size };
  kvs_value kvsvalue = { value->buf, (uint32_t)value->size , 0};

  ret_ctx.option = KVS_RETRIEVE_IDEMPOTENT;
  ret_ctx.reserved = 0;
  ret_ctx.private1 = db;
  ret_ctx.private2 = NULL;
    
#if defined LATENCY_CHECK
  //options =1;
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
  
  ret = kvs_retrieve_tuple(db->cont_hd, &kvskey, &kvsvalue, &ret_ctx);
  while(ret) {
    ret = kvs_retrieve_tuple(db->cont_hd, &kvskey, &kvsvalue, &ret_ctx);
  }

  return COUCHSTORE_SUCCESS; 
}

 couchstore_error_t kvs_get_sync(Db *db, sized_buf *key, sized_buf *value,
				 couchstore_open_options options)
{
  int ret;
  const kvs_retrieve_context ret_ctx = { KVS_RETRIEVE_IDEMPOTENT | KVS_SYNC_IO, 0, db, NULL };
  const kvs_key  kvskey = { key->buf, (uint16_t)key->size };
  kvs_value kvsvalue = { value->buf, (uint32_t)value->size , 0 /*offset */};
  
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
  kvs_delete_context del_ctx = { KVS_DELETE_TUPLE | KVS_SYNC_IO, 0, db, 0 };
  const kvs_key  kvskey = { key->buf,(uint16_t) key->size };
  ret = kvs_delete_tuple(db->cont_hd, &kvskey, &del_ctx);

  return COUCHSTORE_SUCCESS;
}

 couchstore_error_t kvs_delete_async(Db *db, sized_buf *key,
				    couchstore_open_options options)
{

  int ret;
  kvs_delete_context del_ctx;// = { KVS_DELETE_TUPLE, 0, db, NULL };
  const kvs_key  kvskey = { key->buf,(uint16_t)key->size };

  del_ctx.option = KVS_DELETE_TUPLE;
  del_ctx.reserved = 0;
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
  
  ret = kvs_delete_tuple(db->cont_hd, &kvskey, &del_ctx);
  while(ret) {
    kvs_delete_tuple(db->cont_hd, &kvskey, &del_ctx);
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
