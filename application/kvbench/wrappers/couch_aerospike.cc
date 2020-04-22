#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>
#include <numa.h>
#include <queue>

#include "stopwatch.h"
#include "arch.h"
#include "libcouchstore/couch_db.h"
#include "memory.h"
#include "workload.h"
#include "aerospike/as_event.h"
#include "aerospike/aerospike_key.h"
#include "aerospike/aerospike.h"
#include "aerospike/as_monitor.h"
#include "aerospike/as_event.h"

#define LATENCY_CHECK
#define MAX_SAMPLES 1000000
int64_t VALUE_MAXLEN = 65536; // 64KB
static int pre_kv_gen = 0;

aerospike as;
char *ns;

#define METABUF_MAXLEN (256)
extern int64_t DATABUF_MAXLEN;

struct _db {
  //  aerospike as;
  bool async;
  char *ns;
  char *hosts;
  char *port;

  //std::queue<IoContext*> iocontexts;
  //std::queue<IoContext*> iodone;
  IoContext *iocontexts;
  IoContext *iodone;
  spin_t lock;
  long outstandingios;
  latency_stat *l_read;
  latency_stat *l_write;
  latency_stat *l_delete;
};

as_monitor monitor;
static int kv_write_mode = 0;

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
    if (db->iocontexts == NULL) { //empty
      db->iocontexts = ctx;
      ctx->next = ctx->prev = NULL;
    } else {
      db->iocontexts->prev = ctx;
      ctx->next = db->iocontexts;
      db->iocontexts = ctx;
      //db->ctx_cnt++;
    }
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
    if(db->iocontexts == NULL) {
	  return NULL;
    }
    ctx = db->iocontexts;
    db->iocontexts = ctx->next;
  }

  ctx->prev = ctx->next = NULL;
  return ctx;
}

bool is_empty(IoContext *ctx) {
  return (ctx == NULL);
}

void pass_lstat_to_db(Db *db, latency_stat *l_read, latency_stat *l_write, latency_stat *l_delete)
{
  db->l_read = l_read;
  db->l_write = l_write;
  db->l_delete = l_delete;
}

void add_event(Db *db, IoContext *ctx) {
  push_ctx(db, ctx, 1);
}

int release_context(Db *db, IoContext **context, int length) {
  for (int i =0 ;i < length ; i++) {
    if (context[i]) {
      push_ctx(db, context[i], 0);
    }
  }
  return 0;
}

static void write_complete(as_error *err, void *udata, as_event_loop *loop)
{
  IoContext *ioctx = (IoContext *)udata;

  if (err) {
    if (err->code == 18) {
      as_error err;
      if (aerospike_key_put_async(&as, &err, NULL, &ioctx->akey, &ioctx->arec, write_complete,ioctx, NULL, NULL) != AEROSPIKE_OK) {
      	fprintf(stderr, "aerospike_key_put (callback): %d, %s\n", err.code , err.message );
      	return ;
      }
      return;
    }

    if(err->code != 2)  // no print for 'not found'
      fprintf(stderr,"Command failed: %d %s, key = %s\n", err->code, err->message, (char*)ioctx->key);
  }

  // as_key_destroy(&ioctx->akey);
  // as_record_destroy(&ioctx->arec);

  ioctx->result = (err)? -1:0;

  add_event((Db*)ioctx->private1,ioctx);

#if defined LATENCY_CHECK
  if(ioctx->monitor == 1){
    struct timespec t11;
    unsigned long long start, end;
    uint64_t cur_sample;
    latency_stat *l_stat = ioctx->l_stat;

    clock_gettime(CLOCK_REALTIME, &t11);
    end = t11.tv_sec * 1000000000L + t11.tv_nsec;
    end /= 1000L;
    start = ioctx->start;
    if (l_stat->cursor >= MAX_SAMPLES) {
      l_stat->cursor = l_stat->cursor % MAX_SAMPLES;
      l_stat->nsamples = MAX_SAMPLES;
    } else {
      l_stat->nsamples = l_stat->cursor + 1;
    }
    cur_sample = l_stat->cursor;
    l_stat->cursor++;
    l_stat->samples[cur_sample] = end - start;

  }
#endif

}

static void read_complete(as_error *err, as_record *record, void *udata, as_event_loop *)
{
  IoContext *ioctx = (IoContext *)udata;
  if (err) {
  } else {

  }

  ioctx->result = (err)? -1:0;
  add_event((Db*)ioctx->private1, ioctx);
  //((Aerospikedb*)ioctx->private1)->add_event(ioctx);
#if defined LATENCY_CHECK
  if(ioctx->monitor == 1){
    struct timespec t11;
    unsigned long long start, end;
    uint64_t cur_sample;
    latency_stat *l_stat = ioctx->l_stat;

    clock_gettime(CLOCK_REALTIME, &t11);
    end = t11.tv_sec * 1000000000L + t11.tv_nsec;
    end /= 1000L;
    start = ioctx->start;
    if (l_stat->cursor >= MAX_SAMPLES) {
      l_stat->cursor = l_stat->cursor % MAX_SAMPLES;
      l_stat->nsamples = MAX_SAMPLES;
    } else {
      l_stat->nsamples = l_stat->cursor + 1;
    }
    cur_sample = l_stat->cursor;
    l_stat->cursor++;
    l_stat->samples[cur_sample] = end - start;

  }
#endif
  //as_key_destroy(&ioctx->akey);
  //as_record_destroy(&ioctx->arec);
}
static void delete_complete(as_error *err, void *udata, as_event_loop *loop)
{
  IoContext *ioctx = (IoContext *)udata;
  if (err) {
  } else {

  }

  ioctx->result = (err)? -1:0;
  add_event((Db*)ioctx->private1, ioctx);

#if defined LATENCY_CHECK
  if(ioctx->monitor == 1){
    struct timespec t11;
    unsigned long long start, end;
    uint64_t cur_sample;
    latency_stat *l_stat = ioctx->l_stat;

    clock_gettime(CLOCK_REALTIME, &t11);
    end = t11.tv_sec * 1000000000L + t11.tv_nsec;
    end /= 1000L;
    start = ioctx->start;
    if (l_stat->cursor >= MAX_SAMPLES) {
      l_stat->cursor = l_stat->cursor % MAX_SAMPLES;
      l_stat->nsamples = MAX_SAMPLES;
    } else {
      l_stat->nsamples = l_stat->cursor + 1;
    }
    cur_sample = l_stat->cursor;
    l_stat->cursor++;
    l_stat->samples[cur_sample] = end - start;

  }
#endif
}

int getevents(Db *db, int min, int max, IoContext **context, int tid)
{
  int i = 0;
  while (!is_empty(db->iodone) && i < max) {

    context[i] = pop_ctx(db, 1);

    db->outstandingios--;
    i++;
  }

  return i;
}

typedef struct fun {
  int a;
  std::queue<IoContext*> testq;
}fun_t;

LIBCOUCHSTORE_API
couchstore_error_t couchstore_open_db(const char *filename,
                                      couchstore_open_flags flags,
                                      Db **pDb)
{
  Db *ppdb;
  *pDb = (Db*)malloc(sizeof(Db));
  ppdb = *pDb;

  IoContext *context = NULL;
  pthread_spin_init(&ppdb->lock, 0);
  ppdb->outstandingios = 0;
  ppdb->iocontexts = ppdb->iodone = NULL;
  for (int i =0; i < 36000; i++) {
    //db.iocontexts.push(new IoContext());
    context = (IoContext *)malloc(sizeof(IoContext));
    if(context == NULL){
      fprintf(stderr, "Can not allocate db context\n");
      exit(0);
    }
    context->prev = context->next = NULL;
    push_ctx(ppdb, context, 0);
  }

  return COUCHSTORE_SUCCESS;
}

LIBCOUCHSTORE_API
couchstore_error_t couchstore_open_db_ex(const char *filename,
					 couchstore_open_flags flags,
					 FileOpsInterface *ops,
					 Db **pDb)
{

  return COUCHSTORE_SUCCESS;
}

couchstore_error_t couchstore_as_setup(const char *hosts,
				       uint16_t port,
				       uint32_t loop_capacity,
				       const char *name_space,
				       int write_mode)

{

  kv_write_mode = write_mode;

  //TODO: check the capacity setup
  if(kv_write_mode == 0) {
    if (! as_event_create_loops(loop_capacity)) {
      fprintf(stderr, " Failed to create async event loop ");
      exit(1);
    }
  }

  as_config config;
  as_config_init(&config);
  as_config_add_hosts(&config, hosts, port); // for testing

  if(kv_write_mode == 0) {
    as_monitor_init(&monitor);
    as_monitor_begin(&monitor);
  }

  const int read_timeout = 1000000; //10000;
  const int write_timeout = 1000000; //10000;
  const int max_retries  = 10000; //100;
  const auto num_replicas  = AS_POLICY_REPLICA_SEQUENCE;
  as_policies* p = &config.policies;
  config.async_max_conns_per_node = 10000;
  config.max_conns_per_node = 5000;
  config.conn_pools_per_node = 6;

  p->read.base.total_timeout = read_timeout;
  p->read.base.max_retries = max_retries;
  p->read.replica = num_replicas;
  p->read.consistency_level =AS_POLICY_CONSISTENCY_LEVEL_ONE;

  p->write.base.total_timeout = write_timeout;
  p->write.base.max_retries = max_retries;
  p->write.replica = num_replicas;
  p->write.commit_level = AS_POLICY_COMMIT_LEVEL_ALL ;
  p->write.durable_delete = false;

  p->operate.base.total_timeout = write_timeout;
  p->operate.base.max_retries = max_retries;
  p->operate.replica = num_replicas;
  p->operate.commit_level =  AS_POLICY_COMMIT_LEVEL_ALL;
  p->operate.durable_delete = false;

  p->remove.base.total_timeout = write_timeout;
  p->remove.base.max_retries = max_retries;
  p->remove.replica = num_replicas;
  p->remove.commit_level =  AS_POLICY_COMMIT_LEVEL_ALL;
  p->remove.durable_delete = false;
  p->info.timeout = 10000;

  aerospike_init(&as, &config);

  as_error err;
  if (aerospike_connect(&as, &err) != AEROSPIKE_OK) {
    fprintf(stderr, "Aerospike connect failed : %d, %s\n", err.code , err.message );
    as_event_close_loops();
    aerospike_destroy(&as);
    exit(1);
  }

  ns = (char*)malloc(256);
  memcpy(ns, name_space, 256);

  return COUCHSTORE_SUCCESS;
}

LIBCOUCHSTORE_API
couchstore_error_t couchstore_close_db(Db *db)
{
  // TODO: free context, free db, destroy lock
  int i= 0;
  IoContext *tmp;

  tmp = pop_ctx(db, 0);
  while (tmp){
    free(tmp);
    tmp = pop_ctx(db, 0);
  }

  pthread_spin_init(&db->lock, 0);
  free(db);
  return COUCHSTORE_SUCCESS;
}

couchstore_error_t couchstore_close_device(int32_t ndocs)
{
  as_error err;
  int i;

  aerospike_close(&as, &err);
  aerospike_destroy(&as);

  return COUCHSTORE_SUCCESS;

}

LIBCOUCHSTORE_API
couchstore_error_t couchstore_db_info(Db *db, DbInfo* info)
{
  return COUCHSTORE_SUCCESS;
}

LIBCOUCHSTORE_API
couchstore_error_t couchstore_save_documents(Db *db, Doc* const docs[], DocInfo *infos[],
        unsigned numdocs, couchstore_save_options options)
{

  int i;
  as_error err;

  for(i = 0; i < numdocs; i++) {
    if(kv_write_mode == 0) { // async
      IoContext *ctx = pop_ctx(db, 0);
      if(ctx == NULL) {
      	fprintf(stderr, "Not enough context, outstanding %ld\n", db->outstandingios);
      	exit(1);
      }
      //as_key akey;
      if (as_key_init_rawp(&ctx->akey, ns, "set", (const uint8_t *)docs[i]->id.buf, docs[i]->id.size, false) == 0) {
      	fprintf(stderr, "failed to initialize the key\n");
      	exit(-1);
      }

      //as_record rec;
      as_record_init(&ctx->arec, 1);
      if (as_record_set_rawp(&ctx->arec, "bin", (const uint8_t*)docs[i]->data.buf, docs[i]->data.size, false) == 0) {
      	fprintf(stderr, "failed to initialize the value\n");
      	exit(-1);
      }

      ctx->key = docs[i]->id.buf;
      ctx->keylength = docs[i]->id.size;
      ctx->value = docs[i]->data.buf;
      ctx->valuelength = docs[i]->data.size;
      //ctx->op = OP_INSERT;
      ctx->private1 = db;
      ctx->l_stat = db->l_write;

#if defined LATENCY_CHECK
      if(options == 1) {
      	struct timespec t11;
      	unsigned long long nanosec;
      	clock_gettime(CLOCK_REALTIME, &t11);
      	nanosec = t11.tv_sec * 1000000000L + t11.tv_nsec;
      	nanosec /= 1000L;
      	ctx->start = nanosec;
      	ctx->monitor = options;
      }
#endif

      if (aerospike_key_put_async(&as, &err, NULL, &ctx->akey, &ctx->arec, write_complete, ctx, NULL, NULL) != AEROSPIKE_OK) {
      	fprintf(stderr, "aerospike_key_put failed: %d, %s\n", err.code, err.message);
      	exit(-1);
      }
      db->outstandingios++;
    }else { // sync
      as_key akey;
      if (as_key_init_rawp(&akey, ns, "set", (const uint8_t *)docs[i]->id.buf, docs[i]->id.size, false) == 0){
      	fprintf(stderr, "failed to initialize the key\n");
      	exit(-1);
      }

      as_record rec;
      as_record_init(&rec, 1);
      if (as_record_set_rawp(&rec, "bin", (const uint8_t*)docs[i]->data.buf, docs[i]->data.size, false) == 0) {
      	fprintf(stderr, "failed to initialize the value\n");
      	exit(-1);
      }

      if (aerospike_key_put(&as, &err, NULL, &akey, &rec) != AEROSPIKE_OK) {
      	fprintf(stderr, "aerospike_key_put failed: %d, %s\n",err.code, err.message);
      	exit(-1);
      }

      as_key_destroy(&akey);
      as_record_destroy(&rec);

    }
  }

  return COUCHSTORE_SUCCESS;
}

LIBCOUCHSTORE_API
couchstore_error_t couchstore_save_document(Db *db, const Doc *doc, DocInfo *info,
        couchstore_save_options options)
{
    return couchstore_save_documents(db, (Doc**)&doc, (DocInfo**)&info, 1, options);
}

LIBCOUCHSTORE_API
couchstore_error_t couchstore_open_document(Db *db,
                                            const void *id,
                                            size_t idlen,
                                            Doc **pDoc,
                                            couchstore_open_options options)
{
  as_error err;
  as_key akey;
  as_record *rec = 0;
  uint64_t cur_sample = 0;
  struct timespec t11;
  unsigned long long nanosec;

  if(as_key_init_rawp(&akey, ns, "set", (const uint8_t*)id, idlen, false) == 0) {
    fprintf(stderr, "failed to initialize the key\n");
    exit(-1);
  }

  if(kv_write_mode == 0) {// async
    IoContext *ctx = pop_ctx(db, 0);
    ctx->key = (void *)id;
    ctx->keylength = idlen;
    ctx->value = 0;
    ctx->valuelength = 0;
    //ctx->op = OP_GET;
    ctx->private1 = db;
    ctx->l_stat = db->l_read;

#if defined LATENCY_CHECK
    if(options == 1) {
      struct timespec t11;
      unsigned long long nanosec;
      clock_gettime(CLOCK_REALTIME, &t11);
      nanosec = t11.tv_sec * 1000000000L + t11.tv_nsec;
      nanosec /= 1000L;
      ctx->start = nanosec;
      ctx->monitor = options;
    }
#endif

    if (aerospike_key_get_async(&as, &err, NULL, &akey, read_complete, ctx, NULL, NULL) != AEROSPIKE_OK) {
      fprintf(stderr, "aerospike_key_get failed: %d, %s\n", err.code, err.message);
      exit(-1);
    }

  } else { // sync
    as_status ret = aerospike_key_get(&as, &err, NULL, &akey, &rec);
    if(ret != AEROSPIKE_OK && ret != AEROSPIKE_ERR_RECORD_NOT_FOUND) {
      fprintf(stderr, "aerospike_key_get failed: %d, %s\n", err.code, err.message);
      exit(-1);
    }

    as_key_destroy(&akey);
    as_record_destroy(rec);
  }

  return COUCHSTORE_SUCCESS;
}

couchstore_error_t couchstore_delete_document_kv(Db *db,sized_buf *key,
						 couchstore_open_options options)
{

  as_error err;
  as_key akey;

  if(as_key_init_rawp(&akey, ns, "set", (const uint8_t*)key->buf, key->size, false) == 0) {
    fprintf(stderr, "failed to initialize the key\n");
    exit(-1);
  }

  if(kv_write_mode == 0) {// async
    IoContext *ctx = pop_ctx(db, 0);
    ctx->key = key->buf;
    ctx->keylength = key->size;
    ctx->value = 0;
    ctx->valuelength = 0;
    //ctx->op = OP_DEL;
    ctx->private1 = db;
    ctx->l_stat = db->l_delete;

#if defined LATENCY_CHECK
    if(options == 1) {
      struct timespec t11;
      unsigned long long nanosec;
      clock_gettime(CLOCK_REALTIME, &t11);
      nanosec = t11.tv_sec * 1000000000L + t11.tv_nsec;
      nanosec /= 1000L;
      ctx->start = nanosec;
      ctx->monitor = options;
    }
#endif

    if (aerospike_key_remove_async(&as, &err, NULL, &akey, delete_complete, ctx, NULL, NULL) != AEROSPIKE_OK) {
      fprintf(stderr, "aerospike_key_remove failed: %d, %s\n", err.code, err.message);
      exit(-1);
    }
  } else { // sync
    as_status ret = aerospike_key_remove(&as, &err, NULL, &akey);
    if(ret != AEROSPIKE_OK && ret != AEROSPIKE_ERR_RECORD_NOT_FOUND) {
      fprintf(stderr, "aerospike_key_remove failed: %d, %s\n", err.code, err.message);
      exit(-1);
    }
  }

  return COUCHSTORE_SUCCESS;
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
void couchstore_free_document(Doc *doc)
{
    if (doc->id.buf) free(doc->id.buf);
    if (doc->data.buf) free(doc->data.buf);
    free(doc);
}


LIBCOUCHSTORE_API
void couchstore_free_docinfo(DocInfo *docinfo)
{
    //free(docinfo->rev_meta.buf);
    free(docinfo);
}

LIBCOUCHSTORE_API
couchstore_error_t couchstore_commit(Db *db)
{
    // do nothing (automatically performed at the end of each write batch)

    return COUCHSTORE_SUCCESS;
}
