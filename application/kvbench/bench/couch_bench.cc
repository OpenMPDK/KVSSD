#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <assert.h>
#include <sys/types.h>
#include <unistd.h>
#ifndef __APPLE__
#include <malloc.h>
#endif
#include <signal.h>
#include <limits.h>
#include <string>
#include <regex>

#include <dirent.h>
#include <sys/prctl.h>

#include "libcouchstore/couch_db.h"
#include "libcouchstore/file_ops.h"
#include "adv_random.h"
#include "stopwatch.h"
#include "iniparser.h"
#include "workload.h"

#include "arch.h"
#include "zipfian_random.h"
#include "keygen.h"
#include "keyloader.h"
#include "memory.h"
#include "memleak.h"

#if defined __BLOBFS_ROCKS_BENCH
#include "rocksdb/env.h"
//#include "rocksdb/db.h"
#endif

#define COMP_LAT
#define time_check (0)
#define time_check2 (0)
#define SEC_TO_NANO (1000000000L)

//#define THREADPOOL

#ifdef THREADPOOL
#include "thpool.h"
#endif

/* add thread name for bench_thread */
char THREAD_NAME[][16] = {
    "BenchMixer",
    "BenchWriter",
    "BenchReader",
    "BenchIterator",
    "BenchDeleter"
};

bool is_valide_path_name(char* str){
  if(strnlen(str, PATH_MAX+1)>PATH_MAX){
    return false;
  }
  std::ostringstream pattern;
  pattern << "^(/[a-zA-Z0-9_.]{1," << NAME_MAX << "})+/?$";
  return std::regex_match(std::string(str),
                          std::regex(pattern.str()));
}

couchstore_error_t couchstore_close_db(Db *db);

#define alca(type, n) ((type*)alloca(sizeof(type) * (n)))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define MIN(a,b) (((a)<(b))?(a):(b))
#define randomize() srand((unsigned)time(NULL))
#ifdef __DEBUG
#ifndef __DEBUG_COUCHBENCH
    #undef DBG
    #undef DBGCMD
    #undef DBGSW
    #define DBG(args...)
    #define DBGCMD(command...)
    #define DBGSW(n, command...)
#endif
#endif
int64_t DATABUF_MAXLEN = 0;

struct bench_info {
    uint8_t initialize;  // flag for initialization
    uint64_t cache_size; // buffer cache size (for fdb, rdb, ldb)
    int auto_compaction; // compaction mode

    //memory allocator
    void* (*allocate_mem)(int alignment, uint64_t size, int nodeid);
    void (*free_mem)(void *ptr);
    int allocatortype;
    uint64_t kp_unitsize;
    uint64_t vp_unitsize;
    uint64_t kp_numunits;
    uint64_t vp_numunits;
    uint64_t kp_alignment;
    uint64_t vp_alignment;
    instance_info_t *instances;
    cpu_info *cpuinfo;
  
    //kv bench
    char *device_path;
    char *kv_device_path;
    char *kv_emul_configfile;
    char **device_name;
    int32_t device_id;
    int store_option;
    int queue_depth;
    int aiothreads_per_device;
    char *core_ids;
    char *cq_thread_ids;
    int mem_size_mb;
    uint8_t with_iterator;
    uint8_t iterator_mode;
    //uint8_t is_polling;

    // KV and aerospike
    uint8_t kv_write_mode;
    uint8_t allow_sleep;

    // aerospike
    uint16_t as_port;
    uint32_t loop_capacity;
    char *name_space;
  
    // spdk blobfs
    char *spdk_conf_file;
    //char *spdk_bdev;
    uint64_t spdk_cache_size;
  
    int auto_compaction_threads; // ForestDB: Number of auto compaction threads
    uint64_t wbs_init;  // Level/RocksDB: write buffer size for bulk load
    uint64_t wbs_bench; // Level/RocksDB: write buffer size for normal benchmark
    uint64_t bloom_bpk; // Level/RocksDB: bloom filter bits per key
    uint8_t compaction_style; // RocksDB: compaction style
    uint64_t fdb_wal;         // ForestDB: WAL size
    int fdb_type;             // ForestDB: HB+trie or B+tree?
    int wt_type;              // WiredTiger: B+tree or LSM-tree?
    int compression;
    int compressibility;
    int split_pct;      // WiredTiger size of newly split page
    size_t leaf_pg_size, int_pg_size; //WiredTiger page sizes

    uint32_t latency_rate; // sampling rate for latency monitoring
    uint32_t latency_max; // max samples for latency monitoring

    // # docs, # files, DB module name, filename
    //size_t ndocs;
    uint64_t ndocs;
    float amp_factor;
    char *keyfile;
    char *dbname;
    char *init_filename;
    char *filename;
    char *log_filename;
    size_t nfiles;

    // population
    size_t pop_nthreads;
    size_t pop_batchsize;
    uint8_t pop_commit;
    uint8_t fdb_flush_wal;
    uint8_t seq_fill;
    uint8_t pop_first;
    uint8_t pre_gen;
  
    // key generation (prefix)
    size_t nlevel;
    size_t nprefixes;
    struct keygen keygen;
    struct keyloader kl;
    size_t avg_keylen;

    // benchmark threads
    size_t nreaders;
    size_t niterators;
    size_t nwriters;
    size_t ndeleters;
    size_t reader_ops;
    size_t writer_ops;
    int disjoint_write;

    // benchmark details
    struct rndinfo keylen;
    struct rndinfo prefixlen;
    struct rndinfo bodylen;
    int value_size[5];
    int value_size_ratio[5];
    size_t nbatches;
    size_t nops;
    size_t warmup_secs;
    size_t bench_secs;
    struct rndinfo batch_dist;
    struct rndinfo rbatchsize;
    struct rndinfo ibatchsize;
    struct rndinfo wbatchsize;
    struct rndinfo op_dist;
    size_t batchrange;
    uint8_t read_query_byseq;

    // percentage
  //size_t write_prob;
    size_t ratio[4]; //0:read, 1:write, 2: insert 3:delete
    size_t compact_thres;
    size_t compact_period;
    size_t block_reuse_thres;

    // synchronous write
    uint8_t sync_write;
    uint8_t key_existing;
};

#define MIN(a,b) (((a)<(b))?(a):(b))
#define COUCH_MAX_QUEUE_DEPTH 256  // add for queue_depth settings

static uint32_t rnd_seed;
static int print_term_ms = 100;
static int filesize_chk_term = 4;
static std::atomic<std::uint64_t> max_key_id;

FILE *log_fp = NULL;
FILE *insert_latency_fp = NULL;
FILE *insert_ops_fp = NULL;
FILE *run_latency_fp = NULL;
FILE *run_ops_fp = NULL;

#if defined(__KV_BENCH)
extern int couch_kv_min_key_len;
extern int couch_kv_max_key_len;
#endif

#define lprintf(...) {   \
    printf(__VA_ARGS__); \
    if (log_fp) fprintf(log_fp, __VA_ARGS__); } \

int _cmp_docs(const void *a, const void *b)
{
    Doc *aa, *bb;
    aa = (Doc *)a;
    bb = (Doc *)b;

    if (aa->id.size == bb->id.size) return memcmp(aa->id.buf, bb->id.buf, aa->id.size);
    else {
        size_t len = MIN(aa->id.size , bb->id.size);
        int cmp = memcmp(aa->id.buf, bb->id.buf, len);
        if (cmp != 0) return cmp;
        else {
            return (aa->id.size - bb->id.size);
        }
    }
}

static uint8_t metabuf[256];

#define PRINT_TIME(t,str) \
    printf("%d.%01d" str, (int)(t).tv_sec, (int)(t).tv_usec / 100000);
#define LOG_PRINT_TIME(t,str) \
    lprintf("%d.%01d" str, (int)(t).tv_sec, (int)(t).tv_usec / 100000);

uint64_t get_filesize(char *filename)
{
    struct stat filestat;
    stat(filename, &filestat);
    return filestat.st_size;
}

char * print_filesize_approx(uint64_t size, char *output)
{
    if (size < 1024*1024) {
        sprintf(output, "%.2f KB", (double)size / 1024);
    }else if (size >= 1024*1024 && size < 1024*1024*1024) {
        sprintf(output, "%.2f MB", (double)size / (1024*1024));
    }else {
        sprintf(output, "%.2f GB", (double)size / (1024*1024*1024));
    }
    return output;
}

int parse_coreid(struct bench_info *binfo, int* buffer, int buffer_size,
  int *core_count) {
  int core_max = binfo->cpuinfo->num_cores_per_numanodes * binfo->cpuinfo->num_numanodes;
  int str_len = strnlen(binfo->core_ids, 256);
  char *coreid = (char *)malloc(str_len + 1);
  memset(coreid, 0, str_len + 1);
  strncpy(coreid, binfo->core_ids, str_len);
  char* pt = NULL;
  int i = 0;
  if (!coreid)
    return -1;

  pt = strtok ((char*)coreid, ",");
  while (pt != NULL) {
    int id = atoi(pt);
    if (id < core_max) {
      buffer[i++] = id;
      if (i == buffer_size)
        break;
    }
    pt = strtok (NULL, ",");
  }
  *core_count = i;
  free(coreid);
  return i == 0 ? -1 : 0;
}

void print_filesize(char *filename)
{
    char buf[256];
    uint64_t size = get_filesize(filename);

    printf("file size : %lu bytes (%s)\n",
           (unsigned long)size, print_filesize_approx(size, buf));
}

#if defined(__linux) && !defined(__ANDROID__)
    #define __PRINT_IOSTAT
#endif
uint64_t print_proc_io_stat(char *buf, int print)
{
#ifdef __PRINT_IOSTAT
    sprintf(buf, "/proc/%d/io", getpid());
    char str[64];
    int ret; (void)ret;
    unsigned long temp;
    uint64_t val=0;
    FILE *fp = fopen(buf, "r");
    if(fp == NULL) {
      fprintf(stderr, "open %s failed\n", buf);
      return 0;
    }
    while(!feof(fp)) {
        ret = fscanf(fp, "%s %lu", str, &temp);
        if (!strcmp(str, "write_bytes:")) {
            val = temp;
            if (print) {
                lprintf("[proc IO] %lu bytes written (%s)\n",
                        (unsigned long)val, print_filesize_approx(val, str));
            }
        }
    }
    fclose(fp);
    return val;

#else
    return 0;
#endif
}

int empty_callback(Db *db, DocInfo *docinfo, void *ctx)
{
    return 0;
}

//#define MAX_KEYLEN (4096)
#define MAX_KEYLEN (64)
int get_value_size_by_ratio(struct bench_info *binfo, size_t idx) {

  int value_size = 0;
  if(idx % 100 < binfo->value_size_ratio[0]){
    value_size = binfo->value_size[0];
  } else if (idx % 100 <  binfo->value_size_ratio[0] + binfo->value_size_ratio[1] ) {
    value_size  = binfo->value_size[1];
  } else if (idx % 100 <  binfo->value_size_ratio[0] + binfo->value_size_ratio[1] + binfo->value_size_ratio[2]){
    value_size  = binfo->value_size[2];
  } else if (idx % 100 <  binfo->value_size_ratio[0] + binfo->value_size_ratio[1] + binfo->value_size_ratio[2] + binfo->value_size_ratio[3]) {
    value_size  = binfo->value_size[3];
  } else {
    value_size  = binfo->value_size[4];
  }
  
  return value_size; 
}

void _create_doc(struct bench_info *binfo,
                 size_t idx, Doc **pdoc,
                 DocInfo **pinfo, int seq_fill,
		 int socketid, mempool_t *kp, mempool_t *vp, int tid=0)
{
    int r, keylen = 0;
    uint32_t crc;
    Doc *doc = *pdoc;
#if defined __FDB_BENCH || defined __WT_BENCH 
    DocInfo *info = *pinfo;  // FDB need meta info 
#endif
    char keybuf[MAX_KEYLEN];

    if(binfo->bodylen.type != RND_FIXED) { // only affects bodylen (r value)
      crc = keygen_idx2crc(idx, 0);
      BDR_RNG_VARS_SET(crc);
    }

    keylen = (binfo->keylen.type == RND_FIXED)? binfo->keylen.a : 0;
    if (!doc) {
        doc = (Doc *)malloc(sizeof(Doc));
        doc->id.buf = NULL;
        doc->data.buf = NULL;
    }
    doc->tid = tid;
    if(binfo->kv_write_mode == 0 || doc->id.buf == NULL){
      doc->id.buf = (char *)Allocate(kp);
      //doc->id.buf = (char *)malloc(16);
    }

    if(doc->id.buf == NULL){
      fprintf(stderr, "No pool elem available for key - %d\n", kp->num_freeblocks);
      exit(1);
    }
    
    if (binfo->keyfile) {
      doc->id.size = keyloader_get_key(&binfo->kl, idx, doc->id.buf);
    } else {
      if(seq_fill) {
	      doc->id.size = binfo->keylen.a;
	      keygen_seqfill(idx, doc->id.buf, binfo->keylen.a);
      } else {
	      doc->id.size = binfo->keylen.a;
	      keygen_seed2key(&binfo->keygen, idx, doc->id.buf, keylen);
      }
    }

    // as WiredTiger uses C-style string,
    // add NULL character at the end.
    //doc->id.buf[doc->id.size] = 0;

    if(binfo->bodylen.type == RND_FIXED) {
      doc->data.size = binfo->bodylen.a;
      if (doc->data.buf == NULL || binfo->kv_write_mode == 0) {
      	//if (doc->data.buf == NULL){
            // fixed size value buffer, just some random data

      	doc->data.buf = (char *)Allocate(vp);
      	//doc->data.buf = (char *)malloc(4096);
        if(doc->data.buf == NULL) {
          fprintf(stderr,
                  "No pool elem available for value - %d\n", vp->num_freeblocks);
          exit(1);
        }
      	//doc->data.buf = (char*)malloc(4096);
      	//memcpy(doc->data.buf + doc->data.size - 5, (void*)"<end>", 5);
      }
    } else if (binfo->bodylen.type == RND_RATIO) {
      doc->data.size = get_value_size_by_ratio(binfo, idx);
      if (doc->data.buf == NULL || binfo->kv_write_mode == 0) {
	      doc->data.buf = (char *)Allocate(vp);
      }

    } else { // random length
      
      crc = keygen_idx2crc(idx, 0);
      BDR_RNG_VARS_SET(crc); 	
	
      BDR_RNG_NEXTPAIR;
      r = get_random(&binfo->bodylen, rngz, rngz2);
      if (r < 8) r = 8;
      else if(r > DATABUF_MAXLEN) r = DATABUF_MAXLEN;
      
      doc->data.size = r;
      // align to 8 bytes (sizeof(uint64_t))
      doc->data.size = (size_t)((doc->data.size+1) / (sizeof(uint64_t)*1)) *
	      (sizeof(uint64_t)*1);
      if (doc->data.buf == NULL ||binfo->kv_write_mode == 0) {
      	//if (doc->data.buf == NULL) {
      	int max_bodylen, avg_bodylen;
      	int i, abt_array_size, rnd_str_len;
      	char *abt_array = (char*)"abcdefghijklmnopqrstuvwxyz"
      	  "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";
      	abt_array_size = strlen(abt_array);

      	if (binfo->bodylen.type == RND_NORMAL) {
      	  max_bodylen = binfo->bodylen.a + binfo->bodylen.b * 6;
      	  avg_bodylen = binfo->bodylen.a;
      	} else if (binfo->bodylen.type == RND_UNIFORM) {
      	  // uniform
      	  max_bodylen = binfo->bodylen.b + 16;
      	  avg_bodylen = (binfo->bodylen.a + binfo->bodylen.b)/2;
      	}

      	doc->data.buf = (char *)Allocate(vp);
      	if (binfo->compressibility == 0) {
      	  // all random string
                  rnd_str_len = max_bodylen;
      	} else if (binfo->compressibility == 100) {
      	  // repetition of same character
      	  rnd_str_len = 0;
      	} else {
      	  // mixed
      	  rnd_str_len = avg_bodylen * (100 - binfo->compressibility);
      	  rnd_str_len /= 100;
      	}

      	memset(doc->data.buf, 'x', max_bodylen);
      	for (i=0;i<rnd_str_len;++i){
      	  doc->data.buf[i] = abt_array[rand() % abt_array_size];
      	}
      }

      memcpy(doc->data.buf + doc->data.size - 5, (void*)"<end>", 5);
    	/*
    	snprintf(doc->data.buf, doc->data.size,
    		 "idx# %d, body of %.*s, key len %d, body len %d",
    		 (int)idx, (int)doc->id.size, doc->id.buf,
    		 (int)doc->id.size, (int)doc->data.size);
    	*/
    } // end of random value

    
#if defined __FDB_BENCH || defined __WT_BENCH 
    if (!info)
        info = (DocInfo*)malloc(sizeof(DocInfo));

    memset(info, 0, sizeof(DocInfo));
    info->id = doc->id;
    info->rev_meta.buf = (char *)metabuf;
    info->rev_meta.size = 4;
    info->content_meta = (binfo->compression)?(COUCH_DOC_IS_COMPRESSED):(0);
    *pinfo = info;      
#endif
    *pdoc = doc;

}

struct latency_thread_stat {
  uint64_t cursor;
  uint64_t nsamples;
  uint64_t max_samples;
  uint32_t *samples;
};

struct pop_thread_args {
    int tid;
    int n;
    int coreid;
    int socketid;
    int cur_qdepth;
    Db **db;
    struct bench_info *binfo;
    struct pop_thread_args *pop_args;
    spin_t *lock;
    mempool_t *keypool;
    mempool_t *valuepool;
    std::atomic_uint_fast64_t iocount;
    //uint64_t iocount;
    struct stopwatch *sw;
    struct stopwatch *sw_long;
    struct latency_stat *l_write;
};

#define SET_DOC_RANGE(ndocs, nfiles, idx, begin, end) \
    begin = (ndocs) * ((idx)+0) / (nfiles); \
    end = (ndocs) * ((idx)+1) / (nfiles);

#define GET_FILE_NO(ndocs, nfiles, idx) \
    ((idx) / ( ((ndocs) + (nfiles-1)) / (nfiles)))

static void _get_file_range(int t_idx, int num_threads, int num_files,
                            int *begin, int *end)
{
    // return disjoint set of files for each thread

    // example 1): num_thread = 4, num_files = 11
    // => quotient = 2 , remainder = 3
    // t_idx  begin  end  #files
    // 0      0      2    3
    // 1      3      5    3
    // 2      6      8    3
    // 3      9      10   2

    // example 2): num_thread = 4, num_files = 10
    // => quotient = 2 , remainder = 2
    // t_idx  begin  end  #files
    // 0      0      2    3
    // 1      3      5    3
    // 2      6      7    2
    // 3      8      9    2

    int quotient;
    int remainder;

    quotient = num_files / num_threads;
    remainder = num_files - (quotient * num_threads);

    if (remainder) {
        if (t_idx < remainder) {
            *begin = (quotient+1) * t_idx;
            *end = (*begin) + quotient;
        } else {
            *begin = (quotient+1) * remainder + quotient * (t_idx - remainder);
            *end = (*begin) + quotient - 1;
        }
    } else {
        *begin = quotient * t_idx;
        *end = (*begin) + quotient - 1;
    }
}

int getevents(Db *db, int min, int max, IoContext **context, int tid);
int release_context(Db *db, IoContext **contexts, int nr);
void pass_lstat_to_db(Db *db, latency_stat *l_read, latency_stat *l_write, latency_stat *l_delete);
void *pop_thread(void *voidargs){

  //size_t i, k, c, n, db_idx, j;
  uint64_t i, k, c, n, db_idx, j;
  uint64_t counter;
  struct pop_thread_args *args = (struct pop_thread_args *)voidargs;
  struct bench_info *binfo = args->binfo;
  size_t batchsize = args->binfo->pop_batchsize;
  Db *db;
  Doc **docs;
  DocInfo **infos = NULL;

#if defined(__BLOBFS_ROCKS_BENCH)
  // Set up SPDK-specific stuff for this thread
  rocksdb::SpdkInitializeThread();
#endif

  // Insertion latency monitoring
  struct stopwatch sw_monitor, sw_latency;
  struct timeval gap;
  int curfile_no, sampling_ms, monitoring;
  uint64_t cur_sample;
  struct latency_stat *l_stat = args->l_write;

  prctl(PR_SET_NAME, "Population", NULL, NULL, NULL);

  if (binfo->latency_rate) {
    sampling_ms = 1000 / binfo->latency_rate;
  } else {
    sampling_ms = 0;
  }

  stopwatch_init_start(&sw_monitor);
#if defined __AS_BENCH
  db_idx = args->n;
#else  
  db_idx = args->n / binfo->pop_nthreads;
#endif
  db = args->db[db_idx];
  c = (args->n % binfo->pop_nthreads) * batchsize;

#if defined __KV_BENCH || defined __AS_BENCH  
  if(binfo->kv_write_mode == 0)
    pass_lstat_to_db(db, NULL, l_stat, NULL);
#endif
  
  if(binfo->kv_write_mode == 1) { // sync mode

    docs = (Doc**)calloc(batchsize, sizeof(Doc*));
#if defined __FDB_BENCH || defined __WT_BENCH
    infos = (DocInfo**)calloc(batchsize, sizeof(DocInfo*));
#endif
    for (i=0;i<batchsize;++i) {
      docs[i] = NULL;
#if defined __FDB_BENCH || defined __WT_BENCH
      infos[i] = NULL;
      _create_doc(binfo, i, &docs[i],  &infos[i], binfo->seq_fill,
		  args->socketid, args->keypool, args->valuepool);
#else
      _create_doc(binfo, i, &docs[i], NULL, binfo->seq_fill,
		  args->socketid, args->keypool, args->valuepool, args->tid);
#endif
    }
        
    while(c < binfo->ndocs) {
      for(i = c; (i < c + batchsize && i < binfo->ndocs); ++i){
#if defined __FDB_BENCH || defined __WT_BENCH
	_create_doc(binfo, i, &docs[i-c], &infos[i-c], binfo->seq_fill,
		    args->socketid, args->keypool, args->valuepool);
#else
	      _create_doc(binfo, i, &docs[i-c], NULL, binfo->seq_fill,
		      args->socketid, args->keypool, args->valuepool, args->tid);
#endif
      }

      // TODO: add time check here
      if (sampling_ms &&
	      stopwatch_check_ms(&sw_monitor, sampling_ms)) {
      	if (l_stat->cursor >= binfo->latency_max) {
      	  l_stat->cursor = l_stat->cursor % binfo->latency_max;
      	  l_stat->nsamples = binfo->latency_max;
      	} else {
      	  l_stat->nsamples = l_stat->cursor + 1;
      	}
      	cur_sample = l_stat->cursor;
      	stopwatch_init_start(&sw_latency);
      	stopwatch_start(&sw_monitor);
      	monitoring = 1;
      	l_stat->cursor++;
      } else {
	      monitoring = 0;
      }

#if defined __KV_BENCH || defined __AS_BENCH
      couchstore_save_documents(db, docs, NULL, i-c, 0);
#else
      couchstore_save_documents(db, docs, infos, i-c, binfo->compression);
#endif
      
      args->iocount.fetch_add(i-c, std::memory_order_release);
      //args->iocount += i - c;
      if (binfo->pop_commit) {
	      couchstore_commit(db);
      }

      if (monitoring) {
      	gap = stopwatch_get_curtime(&sw_latency);
      	l_stat->samples[cur_sample] = _timeval_to_us(gap);
      }
      
      c += binfo->pop_nthreads * batchsize;
    } // end of while
    
    if(!binfo->pop_commit) {
      couchstore_commit(db);
    }

    // TODO: free batch memory for sync operation
    for (i=0;i<batchsize;++i) {
#if defined __KV_BENCH || defined __AS_BENCH
      // free kv if alloc in KV ways: spdk, ...
      //couchstore_free_document(docs[i]);
#else
      if (docs[i]) {

      }
#if defined __FDB_BENCH || defined __WT_BENCH
      if (infos[i]) free(infos[i]);
#endif
#endif
    }
    free(docs);
    if(infos) free(infos);

  } else { // async mode
#if !defined (__KV_BENCH) && !defined (__AS_BENCH)
    fprintf(stdout, "no async write mode for this DB\n");
    exit(0);
#endif

    docs = (Doc**)calloc(batchsize, sizeof(Doc*));
    IoContext_t *contexts[COUCH_MAX_QUEUE_DEPTH];

    while(c < binfo->ndocs) {
      // async mode: no batch op for kv & as
      while (args->cur_qdepth < binfo->queue_depth) {
      	if(c >= binfo->ndocs) break;
      	_create_doc(binfo, c, &docs[0], NULL, binfo->seq_fill,
      		    args->socketid, args->keypool, args->valuepool, args->tid);

      	if (sampling_ms &&
      	    stopwatch_check_ms(&sw_monitor, sampling_ms)) {
      	  stopwatch_start(&sw_monitor);
      	  monitoring = 1;
      	} else {
      	  monitoring = 0;
      	}

	couchstore_save_documents(db, docs, NULL, 1, monitoring);
	args->cur_qdepth++;

	c += binfo->pop_nthreads * batchsize;
      }

      if (args->cur_qdepth > 0) {
      	int nr = getevents(db, 0, args->cur_qdepth, contexts, args->tid);
      	for(j = 0; j < nr; j++) {
      	  if(contexts[j]) notify(contexts[j], args->keypool, args->valuepool);
      	}

      	release_context(db, contexts, nr);

      	args->cur_qdepth -= nr;
      	args->iocount.fetch_add(nr, std::memory_order_release);
      }

      if(args->cur_qdepth == binfo->queue_depth) {
	      usleep(1);
	    }

    } // end of big while

    // deal with the rest in the Q
    while (args->cur_qdepth > 0) {
      int nr = getevents(db, 0, args->cur_qdepth, contexts, args->tid);

      for (j = 0; j < nr; j++) {
	      if(contexts[j]) notify(contexts[j], args->keypool, args->valuepool);
      }

      release_context(db, contexts, nr);

      args->cur_qdepth -= nr;
      args->iocount.fetch_add(nr, std::memory_order_release);
    }
  } // end of async

  // destoy mempool
  if(args->keypool)
    destroy(binfo->allocatortype, args->keypool);
  if(args->valuepool)
    destroy(binfo->allocatortype, args->valuepool);

}


void * pop_print_time(void *voidargs)
{

    char buf[1024];
    int i;
    double iops, iops_i;
    uint64_t counter = 0, counter_prev = 0;
    uint64_t cur_iocount = 0;
    uint64_t c = 0;
    uint64_t remain_sec = 0;
    uint64_t elapsed_ms = 0;
    uint64_t bytes_written = 0;
    struct pop_thread_args *args = (struct pop_thread_args *)voidargs;
    struct bench_info *binfo = args->binfo;
    struct timeval tv, tv_i;

    while(counter < binfo->ndocs * binfo->nfiles)
    {
      counter = 0;
      for(i = 0; i < binfo->pop_nthreads * binfo->nfiles; i++) {
      	cur_iocount = args->pop_args[i].iocount.load();
      	counter += cur_iocount;
      }

      if (stopwatch_check_ms(args->sw, print_term_ms)) {
      	tv = stopwatch_get_curtime(args->sw_long);
      	tv_i = stopwatch_get_curtime(args->sw);
      	stopwatch_start(args->sw);
      	if (++c % 10 == 0 && counter) {
      	  elapsed_ms = (uint64_t)tv.tv_sec * 1000 +
      	    (uint64_t)tv.tv_usec / 1000;
      	  remain_sec = (binfo->ndocs * binfo->nfiles - counter);
          if(elapsed_ms != 0){
            remain_sec = remain_sec / MAX(1, (counter / elapsed_ms));
            remain_sec = remain_sec / 1000;
          }else{
            remain_sec = 0;
          }
      	}

	iops = (double)counter / (tv.tv_sec + tv.tv_usec/1000000.0);
	iops_i = (double)(counter - counter_prev) /
	  (tv_i.tv_sec + tv_i.tv_usec/1000000.0);
	counter_prev = counter;
	
#if defined (__ROCKS_BENCH) || defined (__BLOBFS_ROCKS_BENCH) || defined (__FDB_BENCH) || defined (__WT_BENCH) || defined (__LEVEL_BENCH) || defined(__KVDB_BENCH)
      	if (c % filesize_chk_term == 0) {
      	  bytes_written = print_proc_io_stat(buf, 0);
      	}
#endif
      	// elapsed time
      	printf("\r[%d.%01d s] ", (int)tv.tv_sec, (int)(tv.tv_usec/100000));
      	// # inserted documents
      	printf("%" _F64 " / %" _F64, counter, (uint64_t)binfo->ndocs * binfo->nfiles);
      	// throughput (average, instant)
      	printf(" (%.2f ops/sec, %.2f ops/sec, ", iops, iops_i);
      	// percentage
      	printf("%.1f %%, ", (double)(counter * 100.0 / (binfo->ndocs * binfo->nfiles)));
      	// total amount of data written
      	printf("%s) ", print_filesize_approx(bytes_written, buf));
      	printf(" (-%d s)", (int)remain_sec);
      	fflush(stdout);

      	if (insert_ops_fp) {
      	  fprintf(insert_ops_fp,
      		  "%d.%01d,%.2f,%.2f,%" _F64 ",%" _F64 "\n",
      		  (int)tv.tv_sec, (int)(tv.tv_usec/100000),
      		  iops, iops_i, (uint64_t)counter, bytes_written);
      	}
      } else {
      	usleep(print_term_ms * 1000);
      	//usleep(100 * 1000);
      }

    }

    return NULL;
}

void _wait_leveldb_compaction(struct bench_info *binfo, Db **db);
void _print_percentile(struct bench_info *binfo,
		       struct latency_stat *l_wstat,
		       struct latency_stat *l_rstat,
		       struct latency_stat *l_dstat,
		       const char *unit, int mode);
void population(Db **db, struct bench_info *binfo)
{
    size_t i, j;
    void *ret[binfo->pop_nthreads * binfo->nfiles + 1];
    thread_t tid[binfo->pop_nthreads * binfo->nfiles +1];
    struct pop_thread_args args[binfo->pop_nthreads * binfo->nfiles + 1]; // each file/device maps to pop_nthreads
    struct stopwatch sw, sw_long;
    struct timeval tv;
    struct timeval t1, t3;
    unsigned long long totalmicrosecs = 0;
    double iops = 0, latency_ms = 0;
    
    int keylen = (binfo->keylen.type == RND_FIXED)? binfo->keylen.a : 0;
    gettimeofday(&t1, NULL);

    stopwatch_init(&sw);
    stopwatch_start(&sw);
    stopwatch_init(&sw_long);
    stopwatch_start(&sw_long);
       
#ifdef THREADPOOL
    threadpool thpool = thpool_init(binfo->pop_nthreads * binfo->nfiles);
#endif

    pthread_attr_t attr[binfo->pop_nthreads * binfo->nfiles];
    cpu_set_t cpus;
    int db_idx, td_idx, nodeid;
    pool_info_t info_key, info_value;
    
    // initialize keypool & value pool
    info_key.allocatortype = info_value.allocatortype = binfo->allocatortype;
    info_key.numunits = binfo->kp_numunits;
    info_key.unitsize = binfo->kp_unitsize;
    info_key.alignment = binfo->kp_alignment;
    info_value.numunits = binfo->vp_numunits;
    info_value.unitsize = binfo->vp_unitsize;
    info_value.alignment = binfo->vp_alignment;

    int coreid[64] = { 0 };
    int core_count = 0;
    int _ret = -1;
#ifdef __KV_BENCH
    if (binfo->kv_device_path[0] != '/') // udd mode
      _ret = parse_coreid(binfo, coreid, 64, &core_count);
#endif
    for (i=0;i<binfo->pop_nthreads * binfo->nfiles;++i){
      pthread_attr_init(&attr[i]);
      db_idx = i / binfo->pop_nthreads;
      td_idx = i%binfo->pop_nthreads;
      nodeid = binfo->instances[db_idx].nodeid_load;
      //nodeid = 0;
      /*
          printf("thread %d for db %d %d th numaid %d, core %d\n",
                 (int)i, db_idx, td_idx,
                 binfo->instances[db_idx].nodeid_load,
                 binfo->instances[db_idx].coreids_pop[td_idx]);
      */
      if(binfo->instances[db_idx].coreids_pop[td_idx] != -1) {
      	CPU_ZERO(&cpus);
      	CPU_SET(binfo->instances[db_idx].coreids_pop[td_idx], &cpus);
      	pthread_attr_setaffinity_np(&attr[i], sizeof(cpu_set_t), &cpus);
      } else if(nodeid != -1) {
	      CPU_ZERO(&cpus);
        if (_ret) {
          for(j = 0; j < binfo->cpuinfo->num_cores_per_numanodes; j++){
            CPU_SET(binfo->cpuinfo->cpulist[nodeid][j], &cpus);
          }
        } else {
          CPU_SET(db_idx < core_count ? coreid[db_idx] : 0, &cpus);
        }
	      pthread_attr_setaffinity_np(&attr[i], sizeof(cpu_set_t), &cpus);
      } else {
      	// if no core & socket is set, use round robin to assign numa node
      	CPU_ZERO(&cpus);
        nodeid = db_idx % binfo->cpuinfo->num_numanodes;
        if (_ret) {
          for(j = 0; j < binfo->cpuinfo->num_cores_per_numanodes; j++){
            CPU_SET(binfo->cpuinfo->cpulist[nodeid][j], &cpus);
          }
        } else {
          CPU_SET(db_idx < core_count ? coreid[db_idx] : 0, &cpus);
        }
      	pthread_attr_setaffinity_np(&attr[i], sizeof(cpu_set_t), &cpus);
      }
      args[i].socketid = nodeid;

      args[i].keypool = (mempool_t *)malloc(sizeof(mempool_t));
      args[i].keypool->base = args[i].keypool->nextfreeblock = NULL;
      pool_setup(&info_key, args[i].keypool, nodeid);

      args[i].valuepool = (mempool_t *)malloc(sizeof(mempool_t));
      args[i].valuepool->base = args[i].valuepool->nextfreeblock = NULL;
      pool_setup(&info_value, args[i].valuepool, nodeid);

      fprintf(stdout, "thread %d initialize key pool - base is %p, next free %p free %d\n", (int)i, args[i].keypool->base, args[i].keypool->nextfreeblock, args[i].keypool->num_freeblocks);
      fprintf(stdout, "thread %d initialize value pool - base is %p, next free %p free %d, unit size %ld\n", (int)i, args[i].valuepool->base, args[i].valuepool->nextfreeblock, args[i].valuepool->num_freeblocks, info_value.unitsize);

      args[i].l_write = (struct latency_stat *)calloc(1, sizeof(struct latency_stat));
      args[i].l_write->samples = (uint32_t*)malloc(sizeof(uint32_t) * binfo->latency_max);

    }

    for (i=0;i<=binfo->pop_nthreads * binfo->nfiles;++i) {
      args[i].n = i;
      args[i].tid = i;
      args[i].db = db;
      args[i].binfo = binfo;
    	args[i].sw = &sw;
    	args[i].sw_long = &sw_long;
    	args[i].iocount = 0;
    	args[i].cur_qdepth = 0;
      if (i < binfo->pop_nthreads * binfo->nfiles) {
#ifdef THREADPOOL
	    thpool_add_work(thpool, pop_thread, &args[i]);
#else
	    pthread_create(&tid[i], &attr[i], pop_thread, &args[i]);
      //thread_create(&tid[i], pop_thread, &args[i]);
#endif
      } else {
    	  if(insert_ops_fp)
    	    fprintf(insert_ops_fp, "time,iops,iops_i,counter\n");
    	  args[i].pop_args = args;
    	  thread_create(&tid[i], pop_print_time, &args[i]);
      }
    }

    for (i=0; i<=binfo->pop_nthreads * binfo->nfiles; ++i) {
        thread_join(tid[i], &ret[i]);
    }

#if defined (__KV_BENCH) || defined (__AS_BENCH)

    // Linux + (LevelDB or RocksDB): wait for background compaction
#elif defined (__ROCKS_BENCH) || defined (__LEVEL_BENCH) //|| defined(__KVDB_BENCH) //|| defined (__BLOBFS_ROCKS_BENCH)
    _wait_leveldb_compaction(binfo, db);
#endif  // __ROCKS_BENCH

    gettimeofday(&t3, NULL);
    totalmicrosecs = (t3.tv_sec - t1.tv_sec) * 1000000;
    totalmicrosecs += (t3.tv_usec - t1.tv_usec);
    latency_ms = (long double)totalmicrosecs / ((long double) binfo->ndocs * (long double) binfo->nfiles);
    iops = 1000000 / latency_ms;
    lprintf("\nThroughput(Insertion) %.2f latency=%lf\n", iops, latency_ms);

    if(binfo->latency_rate){

      struct latency_stat l_stat;
      uint32_t *tmp;
      memset(&l_stat, 0, sizeof(struct latency_stat));
      l_stat.samples = (uint32_t*)malloc(sizeof(uint32_t) * binfo->latency_max * binfo->pop_nthreads * binfo->nfiles);
      tmp = l_stat.samples;
      for(i = 0; i < binfo->pop_nthreads * binfo->nfiles; i++){
        l_stat.nsamples += args[i].l_write->nsamples;
        memcpy(tmp, args[i].l_write->samples, sizeof(uint32_t) * args[i].l_write->nsamples);
        tmp += args[i].l_write->nsamples;
        free(args[i].l_write->samples);
      }

      _print_percentile(binfo, &l_stat, NULL, NULL, "us", 1);
      lprintf("\n");
      free(l_stat.samples);
    }
#ifdef THREADPOOL
    //thpool_wait(thpool);
    thpool_destroy(thpool);
#endif
    max_key_id = binfo->ndocs;
    fprintf(stdout, "population done \n");

}

void _get_rw_factor(struct bench_info *binfo, double *prob)
{
    //double p = binfo->write_prob / 100.0;
    double p = binfo->ratio[1] + binfo->ratio[2];
    double wb, rb;

    // rw_factor = r_batchsize / w_batchsize
    if (binfo->rbatchsize.type == RND_NORMAL) {
        rb = (double)binfo->rbatchsize.a;
        wb = (double)binfo->wbatchsize.a;
    } else {
        rb = (double)(binfo->rbatchsize.a + binfo->rbatchsize.b) / 2;
        wb = (double)(binfo->wbatchsize.a + binfo->wbatchsize.b) / 2;
    }
    //if (binfo->write_prob < 100) {
    if (binfo->ratio[1] + binfo->ratio[2] < 100) {
        *prob = (p * rb) / ( (1-p)*wb + p*rb );
    } else {
        *prob = 65536;
    }
}

struct bench_result_hit{
    uint32_t idx;
    uint64_t hit;
};

struct bench_result{
    struct bench_info *binfo;
    uint64_t ndocs;
    uint64_t nfiles;
    struct bench_result_hit *doc_hit;
    struct bench_result_hit *file_hit;
};

//#define __BENCH_RESULT
#ifdef __BENCH_RESULT
void _bench_result_init(struct bench_result *result, struct bench_info *binfo)
{
  //size_t i;
    uint64_t i;
    result->binfo = binfo;
    result->ndocs = binfo->ndocs;
    result->nfiles = binfo->nfiles;

    result->doc_hit = (struct bench_result_hit*)
                      malloc(sizeof(struct bench_result_hit) * binfo->ndocs);
    result->file_hit = (struct bench_result_hit*)
                       malloc(sizeof(struct bench_result_hit) * binfo->nfiles);
    memset(result->doc_hit, 0,
           sizeof(struct bench_result_hit) * binfo->ndocs);
    memset(result->file_hit, 0,
           sizeof(struct bench_result_hit) * binfo->nfiles);

    for (i=0;i<binfo->ndocs;++i){
        result->doc_hit[i].idx = i;
    }
    for (i=0;i<binfo->nfiles;++i){
        result->file_hit[i].idx = i;
    }
}

void _bench_result_doc_hit(struct bench_result *result, uint64_t idx)
{
    result->doc_hit[idx].hit++;
}

void _bench_result_file_hit(struct bench_result *result, uint64_t idx)
{
    result->file_hit[idx].hit++;
}

int _bench_result_cmp(const void *a, const void *b)
{
    struct bench_result_hit *aa, *bb;
    aa = (struct bench_result_hit*)a;
    bb = (struct bench_result_hit*)b;
    // decending (reversed) order
    if (aa->hit < bb->hit) return 1;
    else if (aa->hit > bb->hit) return -1;
    else return 0;
}

void _bench_result_print(struct bench_result *result)
{
    char buf[1024];
    size_t keylen;
    uint64_t i;
    uint32_t crc;
    uint64_t file_sum, doc_sum, cum;
    FILE *fp;

    file_sum = doc_sum = 0;

    printf("printing bench results.. \n");

    for (i=0;i<result->nfiles;++i) file_sum += result->file_hit[i].hit;
    for (i=0;i<result->ndocs;++i) doc_sum += result->doc_hit[i].hit;

    qsort(result->file_hit, result->nfiles,
         sizeof(struct bench_result_hit), _bench_result_cmp);
    qsort(result->doc_hit, result->ndocs,
         sizeof(struct bench_result_hit), _bench_result_cmp);

    fp = fopen("result.txt", "w");
    if(fp == NULL) {
      fprintf(stderr, "open %s failed\n", "result.txt");
      return;
    }
    fprintf(fp, "== files ==\n");
    cum = 0;
    for (i=0;i<result->nfiles;++i){
        cum += result->file_hit[i].hit;

        fprintf(fp, "%8d%8d%8.1f %%%8.1f %%\n",
                (int)result->file_hit[i].idx,
                (int)result->file_hit[i].hit,
                100.0 * result->file_hit[i].hit/ file_sum,
                100.0 * cum / file_sum);
    }

    fprintf(fp, "== documents ==\n");
    cum = 0;
    for (i=0;i<result->ndocs;++i){
        cum += result->doc_hit[i].hit;
        if (result->doc_hit[i].hit > 0) {
            fprintf(fp, "%8d%8d%8.1f %%%8.1f %%\n",
                    (int)result->doc_hit[i].idx,
                    (int)result->doc_hit[i].hit,
                    100.0 * result->doc_hit[i].hit/ doc_sum,
                    100.0 * cum / doc_sum);
        }
    }
    fclose(fp);
}

void _bench_result_free(struct bench_result *result)
{
    free(result->doc_hit);
    free(result->file_hit);
}

#else
#define _bench_result_init(a,b)
#define _bench_result_doc_hit(a,b)
#define _bench_result_file_hit(a,b)
#define _bench_result_cmp(a,b)
#define _bench_result_print(a)
#define _bench_result_free(a)
#endif

#define OP_CLOSE (0x01)
#define OP_CLOSE_OK (0x02)
#define OP_REOPEN (0x04)
struct bench_thread_args {
    int tid;
    int id;
    int coreid;
    int socketid;
    int cur_qdepth;
    Db **db;
    int mode; // 0:reader+writer, 1:writer, 2:reader
    int frange_begin;
    int frange_end;
    int *compaction_no;
    uint32_t rnd_seed;
    struct bench_info *binfo;
    struct bench_result *result;
    struct zipf_rnd *zipf;
    struct bench_shared_stat *b_stat;
    struct latency_stat *l_read;
    struct latency_stat *l_write;
    struct latency_stat *l_delete;
    std::atomic_uint_fast64_t op_read;
    std::atomic_uint_fast64_t op_write;
    std::atomic_uint_fast64_t op_delete;
    std::atomic_uint_fast64_t op_iter_key;
    mempool_t *keypool;
    mempool_t *valuepool;
#ifdef __TIME_CHECK
    struct latency_stat *l_prep;
    struct latency_stat *l_real;
#endif
    uint8_t terminate_signal;
    uint8_t op_signal;
#if defined(__BLOBFS_ROCKS_BENCH)
  //rocksdb::Env *blobfs_env;
#endif
};

struct compactor_args {
    char *curfile;
    char *newfile;
    struct bench_info *binfo;
    struct stopwatch *sw_compaction;
    struct bench_thread_args *b_args;
    int *cur_compaction;
    int bench_threads;
    int curfile_no;
    uint8_t flag;
    spin_t *lock;
};

#if defined(__COUCH_BENCH)
couchstore_error_t couchstore_close_db(Db *db)
{
    //couchstore_close_file(db);
    //couchstore_free_db(db);
    return COUCHSTORE_SUCCESS;
}
#endif

void (*old_handler)(int);
int got_signal = 0;

#if defined(__FDB_BENCH) || defined(__COUCH_BENCH)

void * compactor(void *voidargs)
{
    struct compactor_args *args = (struct compactor_args*)voidargs;
    struct stopwatch *sw_compaction = args->sw_compaction;
    Db *db;
    char *curfile = args->curfile;
    char *newfile = args->newfile;

    couchstore_open_db(curfile,
                       COUCHSTORE_OPEN_FLAG_CREATE |
                           ((args->binfo->sync_write)?(0x10):(0x0)),
                       &db);
    stopwatch_start(sw_compaction);
    couchstore_compact_db(db, newfile);
    couchstore_close_db(db);
    stopwatch_stop(sw_compaction);

    spin_lock(args->lock);
    *(args->cur_compaction) = -1;
    if (args->flag & 0x1) {
        int ret, i, j; (void)ret;
        int close_ack = 0;
        char cmd[256];

        for (i=0; i<args->bench_threads; ++i) {
            args->b_args[i].op_signal |= OP_REOPEN;
        }

        // wait until all threads reopen the new file
        while (1) {
            close_ack = 0;
            for (j=0; j<args->bench_threads; ++j) {
                if (args->b_args[j].op_signal == 0) {
                    close_ack++;
                }
            }
            if (close_ack < args->bench_threads) {
                usleep(10000);
            } else {
                break;
            }

            if (got_signal) {
                // user wants to break .. don't need to wait here
                break;
            }
        }

        // erase previous db file
        sprintf(cmd, "rm -rf %s 2> errorlog.txt", curfile);
        ret = system(cmd);
    }
    spin_unlock(args->lock);

    free(args->curfile);
    free(args->newfile);

    return NULL;
}

#endif

void signal_handler_confirm(int sig_no)
{
    char *r;
    char cmd[1024];
    printf("\nAre you sure to terminate (y/N)? ");
    r = fgets(cmd, 1024, stdin);
    if (r == cmd && (cmd[0] == 'y' || cmd[0] == 'Y')) {
        printf("Force benchmark program to terminate.. "
               "(data loss may occur)\n");
        exit(0);
    }
}
void signal_handler(int sig_no)
{
    printf("\n ### got Ctrl+C ### \n");
    fflush(stdout);
    got_signal = 1;

    // set signal handler
    signal(SIGINT, signal_handler_confirm);
}

int _dir_scan(struct bench_info *binfo, int *compaction_no)
{
    int filename_len = strlen(binfo->filename);
    int dirname_len = 0;
    int i;
    int db_no, cpt_no;
    int result = 0;
    char dirname[256], *filename;
    DIR *dir_info;
    struct dirent *dir_entry;
    char name[256];

    if (binfo->filename[filename_len-1] == '/') {
        filename_len--;
    }

    strncpy(name, binfo->filename, filename_len);
    strncpy(name + filename_len, "/dummy", 6); // TODO: 
    /*    
    // backward search until find first '/'
    for (i=filename_len-1; i>=0; --i){
        if (binfo->filename[i] == '/') {
            dirname_len = i+1;
            break;
        }
    }
    if (dirname_len > 0) {
        strncpy(dirname, binfo->filename, dirname_len);
        dirname[dirname_len] = 0;
    } else {
        strcpy(dirname, ".");
    }
    filename = binfo->filename + dirname_len;
    */    
    strncpy(dirname, binfo->filename, filename_len);
    dirname[filename_len] = 0;
    filename = name + filename_len +1;
    dir_info = opendir(dirname);
    if (dir_info != NULL) {
      while((dir_entry = readdir(dir_info))) {
        if (!strncmp(dir_entry->d_name, filename, strlen(filename))) {
          fprintf(stdout, "%s %s %s\n", dir_entry->d_name, filename, dirname);
          // file exists
          result = 1;
          if (compaction_no) {
              sscanf(dir_entry->d_name + (filename_len - dirname_len),
                     "%d.%d", &db_no, &cpt_no);
              compaction_no[db_no] = cpt_no;
          }
        }
      }
      closedir(dir_info);
    }

    return result;
}

struct bench_shared_stat {
  //uint64_t batch_count;
  std::atomic_uint_fast64_t op_read;
  std::atomic_uint_fast64_t op_write;
  std::atomic_uint_fast64_t op_delete;
  std::atomic_uint_fast64_t op_iter_key;
  //std::atomic_uint_fast64_t batch_count_a;
  //spin_t lock;
};

struct iterate_args {
    uint64_t batchsize;
    uint64_t counter;
};

int iterate_callback(Db *db,
                     int depth,
                     const DocInfo* doc_info,
                     uint64_t subtree_size,
                     const sized_buf* reduce_value,
                     void *ctx)
{
    struct iterate_args *args = (struct iterate_args *)ctx;
    if (doc_info) {
        args->counter++;
#if defined(__COUCH_BENCH)
        // in Couchstore, we should read the entire doc using doc_info
        Doc *doc = NULL;
        couchstore_open_doc_with_docinfo(db, (DocInfo*)doc_info, &doc, 0x0);
        couchstore_free_document(doc);
#endif
    }
    if (args->counter < args->batchsize) {
        return 0;
    } else {
        return -1; // abort
    }
}

#define MAX_BATCHSIZE (65536)

couchstore_error_t couchstore_open_document_kv (Db *db,
			sized_buf *key,sized_buf *value,
			couchstore_open_options options);
couchstore_error_t couchstore_open_document_kvrocks(Db *db, const void *id,
					    size_t idlen, size_t valuelen,
					    Doc **pDoc, couchstore_open_options options);
couchstore_error_t couchstore_delete_document_kv(Db *db, sized_buf *key,
						 couchstore_open_options options);
couchstore_error_t couchstore_iterator_open(Db *db, int options);
couchstore_error_t couchstore_iterator_close(Db *db);
couchstore_error_t couchstore_iterator_next(Db *db);
bool couchstore_iterator_check_status(Db *db);
int couchstore_iterator_get_numentries(Db *db);
int couchstore_iterator_has_finish(Db *db);
void * bench_thread(void *voidargs)
{
  struct bench_thread_args *args = (struct bench_thread_args *)voidargs;
  size_t i;
  int j;
  int batchsize;
  int write_mode = 0, write_mode_r;
  int commit_mask[args->binfo->nfiles]; (void)commit_mask;
  int curfile_no, sampling_ms, monitoring;
  //double prob;
  uint64_t cur_op_idx = 0;
  char curfile[256], keybuf[MAX_KEYLEN];
  uint64_t r, crc, op_med;
  //uint64_t op_w, op_r, op_d, op_w_cum, op_r_cum, op_d_cum, op_w_turn, op_r_turn, op_d_turn;
  uint64_t expected_us, elapsed_us, elapsed_sec;
  uint64_t cur_sample;
  Db **db;
  int db_idx;
  Doc *rq_doc = NULL;
  sized_buf rq_id, rq_value;
  struct rndinfo op_dist; //write_mode_random, op_dist;
  struct bench_info *binfo = args->binfo;
#ifdef __BENCH_RESULT
  struct bench_result *result = args->result;
#endif
  struct zipf_rnd *zipf = args->zipf;
  struct latency_stat *l_stat;
  struct stopwatch sw, sw_monitor, sw_latency;
  struct timeval gap;
  couchstore_error_t err = COUCHSTORE_SUCCESS;
  int keylen = (binfo->keylen.type == RND_FIXED)? binfo->keylen.a : 0;
  long int total_entries = 0;
  int iterator_send = 0;
  uint64_t max_key_index = 0;
  int singledb_thread_num = binfo->nreaders + binfo->niterators + binfo->nwriters + binfo->ndeleters;
  uint64_t key_offset = 0;

  prctl(PR_SET_NAME, THREAD_NAME[args->mode], NULL, NULL, NULL);

  if (binfo->key_existing) {
    if (args->mode == 0) {
      max_key_index = binfo->ndocs;
      if ((max_key_index % singledb_thread_num) > (args->id % singledb_thread_num)) {
        max_key_index = max_key_index / singledb_thread_num + 1;
      } else {
        max_key_index = max_key_index / singledb_thread_num;
      }
      key_offset = args->id % singledb_thread_num;
    }
  }
#if defined(__BLOBFS_ROCKS_BENCH)
  // Set up SPDK-specific stuff for this thread
  rocksdb::SpdkInitializeThread();
#endif

#if defined (__FDB_BENCH) || defined(__WT_BENCH)
  DocInfo *rq_info = NULL;
#endif  

  rq_id.buf = NULL;
  db = args->db;
#if defined __KV_BENCH
  db_idx = args->id / singledb_thread_num; // match each db to multiple-thread
#elif defined __BLOBFS_ROCKS_BENCH
  db_idx = args->id / singledb_thread_num;
#else
  db_idx = 0; 
#endif
  op_med = 0; //op_w = op_r = op_w_cum = op_r_cum = 0;
  elapsed_us = 0;

#if defined __KV_BENCH || defined __AS_BENCH
  if(binfo->kv_write_mode == 0)
    pass_lstat_to_db(db[db_idx], args->l_read, args->l_write, args->l_delete);
#endif
  
  /*
  write_mode_random.type = RND_UNIFORM;
  write_mode_random.a = 0;
  write_mode_random.b = 256 * 256;
  */
  crc = args->rnd_seed + args->id;
  crc = MurmurHash64A(&crc, sizeof(crc), 0);
  BDR_RNG_VARS_SET(crc);
  BDR_RNG_NEXTPAIR;
  BDR_RNG_NEXTPAIR;
  BDR_RNG_NEXTPAIR;

  stopwatch_init_start(&sw);
  stopwatch_init_start(&sw_monitor);
  if (binfo->latency_rate) {
    sampling_ms = 1000 / binfo->latency_rate;
  } else {
    sampling_ms = 0;
  }
  IoContext_t *contexts[COUCH_MAX_QUEUE_DEPTH];

#if defined __KV_BENCH
  //int use_udd;
  if(binfo->with_iterator > 0 && args->tid == 0) {
    if(binfo->kv_write_mode == 1) {
      fprintf(stdout, "\nWARN: Only support iterator under ASYNC mode\n");
      exit(0);
    }
    couchstore_iterator_open(db[db_idx], binfo->iterator_mode);
    /*
    if(use_udd == 0) {
      int nr = 0;
      while(nr != 1) {
	      nr = getevents(db[db_idx], 0, 1, contexts);
      }
      if(contexts[0]) notify(contexts[0], args->keypool, args->valuepool);
    } else {
      fprintf(stdout, "udd open iter done \n");
      //exit(0);
    }
    */
    fprintf(stdout, "Iterator open done \n");
  }
#endif

  while(!args->terminate_signal) {
    if (args->op_signal & OP_CLOSE) {
      args->op_signal |= OP_CLOSE_OK; // set ack flag
      args->op_signal &= ~(OP_CLOSE); // clear flag
      while (!(args->op_signal & OP_REOPEN)) {
	      usleep(10000); // couchstore cannot write during compaction
      }
      if (args->op_signal & OP_CLOSE) {
      	// compaction again
      	continue;
      }
    }
    if (args->op_signal & OP_REOPEN) {
#if defined(__BLOBFS_ROCKS_BENCH)
      // As of now, BlobFS has no directory support, so it has not been tested
      // with multiple DBs
      if ((int) binfo->nfiles > 1)
	      printf("WARN: BlobFS RocksDB has not been tested with nfiles > 1\n");
#endif
      //TODO: check this for rxdb - multi-device
      for (i=0; i<args->binfo->nfiles; ++i) {
	      couchstore_close_db(args->db[i]);

	      sprintf(curfile, "%s%d.%d", binfo->filename, (int)i,
		      args->compaction_no[i]);
#if !defined(__KV_BENCH) && !defined(__KVV_BENCH) && !defined(__KVROCKS_BENCH) && !defined(__AS_BENCH)
#if defined (__BLOBFS_ROCKS_BENCH)
	      couchstore_open_db(curfile, 0x0, &args->db[i]);
#else
	      couchstore_open_db(curfile, 0x0, &args->db[i]);
#endif //  __BLOBFS_ROCKS_BENCH
#endif
      }
      args->op_signal = 0;
    }
    /*
    if(args->mode > 0){
      write_mode = args->mode;
    } else {
      if(cur_op_idx % 100 < binfo->ratio[1] + binfo->ratio[2]){
	      write_mode = 1; // write: update/insert
      } else if(cur_op_idx % 100 < binfo->ratio[0] + binfo->ratio[1] + binfo->ratio[2]){
	      write_mode = 2; // read
      } else {
	      write_mode = 4; // delete
      }
      cur_op_idx++;
    }
    */
    // ramdomly set document distribution for batch
    if (binfo->batch_dist.type == RND_UNIFORM) {
      // uniform distribution
      BDR_RNG_NEXTPAIR;
      op_med = get_random(&binfo->batch_dist, rngz, rngz2);
    } else {
      // zipfian distribution
      BDR_RNG_NEXTPAIR;
      op_med = zipf_rnd_get(zipf);
      op_med = op_med * binfo->batch_dist.b + (rngz % binfo->batch_dist.b);
    }
    r = op_med;

    if (binfo->kv_write_mode == 1) { // sync mode

      if(args->mode > 0){
	      write_mode = args->mode;
      } else {
      	if(cur_op_idx % 100 < binfo->ratio[1] + binfo->ratio[2]){
      	  write_mode = 1; // write: update/insert
            if (binfo->key_existing && cur_op_idx % 100 < binfo->ratio[1])
              write_mode = 5; // update
      	} else if(cur_op_idx % 100 < binfo->ratio[0] + binfo->ratio[1] + binfo->ratio[2]){
      	  write_mode = 2; // read
      	} else {
      	  write_mode = 4; // delete
      	}
      	cur_op_idx++;
      }
      
      if(write_mode == 1 || write_mode == 5) { // write
        if (binfo->key_existing) {
          if (args->mode == 0) {
            if (write_mode == 5 && max_key_index != 0) { //update
              r = r % max_key_index;
            } else { //insert
              r = max_key_index++;
            }
            r = r * singledb_thread_num + key_offset;
            write_mode = 1;
          } else {
             if (r > max_key_id) {
               r = max_key_id++;
             }
          }
        }
	      _create_doc(binfo, r, &rq_doc, NULL, binfo->seq_fill,
		    args->socketid, args->keypool, args->valuepool, args->tid);

      	if (sampling_ms && stopwatch_check_ms(&sw_monitor, sampling_ms)) {
      	  if(write_mode == 2)
      	    l_stat = args->l_read;
      	  else if(write_mode == 1)
      	    l_stat = args->l_write;
      	  else
      	    l_stat = args->l_delete;

      	  l_stat->cursor++;
      	  if (l_stat->cursor >= binfo->latency_max) {
      	    l_stat->cursor = l_stat->cursor % binfo->latency_max;
      	    l_stat->nsamples = binfo->latency_max;
      	  } else {
      	    l_stat->nsamples = l_stat->cursor;
      	  }
      	  cur_sample = l_stat->cursor;
      	  stopwatch_init_start(&sw_latency);
      	  stopwatch_start(&sw_monitor);
      	  monitoring = 1;
      	} else {
      	  monitoring = 0;
      	}
#if defined __AS_BENCH
      	err = couchstore_save_document(NULL, rq_doc,
      				       NULL, monitoring);
      	DeAllocate(rq_doc->id.buf, args->keypool);
      	rq_doc->id.buf = NULL;
      	DeAllocate(rq_doc->data.buf, args->valuepool);
      	rq_doc->data.buf = NULL;
#elif defined __KV_BENCH
      	err = couchstore_save_document(db[db_idx], rq_doc,
      				       NULL, monitoring);
      	DeAllocate(rq_doc->id.buf, args->keypool);
      	rq_doc->id.buf = NULL;
      	DeAllocate(rq_doc->data.buf, args->valuepool);
      	rq_doc->data.buf = NULL;
#else
      	err = couchstore_save_document(db[db_idx], rq_doc, NULL, binfo->compression);
      	//DeAllocate(rq_doc->id.buf, args->keypool);
#endif

      	if (err != COUCHSTORE_SUCCESS) {
      	  fprintf(stderr,"write error: thread %d \n", args->id);
      	}
      } else if (write_mode == 2) { // read

#if defined __ROCKS_BENCH || defined(__KVDB_BENCH)
    	if(rq_id.buf == NULL) {
        rq_id.buf = (char *)Allocate(args->keypool);
        if(rq_id.buf == NULL){
           fprintf(stderr,
           "Allocate buffer from  keypool failed. "
           "Please set 'key_pool_size' bigger\n");
           exit(1);
        }
      }
    	if(binfo->keyfile) {
    	  rq_id.size = keyloader_get_key(&binfo->kl, r, rq_doc->id.buf);
    	}else{
          if (binfo->key_existing) {
            uint64_t tmp_max_key_id = max_key_id.load();
            if (args->mode == 0 && max_key_index != 0) {
              r = r % max_key_index;
              r = r * singledb_thread_num + key_offset;
            } else if (args->mode > 0 && tmp_max_key_id != 0) {
              r = r % tmp_max_key_id;
            }
          }
    	  if (binfo->seq_fill){
    	    rq_id.size = binfo->keylen.a;
    	    keygen_seqfill(r, rq_id.buf, binfo->keylen.a);
    	  }else {
    	    rq_id.size = binfo->keylen.a;
    	    keygen_seed2key(&binfo->keygen, r, rq_id.buf, keylen);
    	  }
    	}
#else
      if(rq_doc == NULL) {
        rq_doc = (Doc *)malloc(sizeof(Doc));
        rq_doc->id.buf = NULL;
        rq_doc->data.buf = NULL;
      }

      if(rq_doc->id.buf == NULL)
        rq_doc->id.buf = (char *)Allocate(args->keypool);

      if(rq_doc->id.buf == NULL){
         fprintf(stderr,
         "Allocate buffer from keypool failed. "
         "Please set 'key_pool_size' bigger\n");
         exit(1);
      }

      if (binfo->keyfile) {
        rq_doc->id.size = keyloader_get_key(&binfo->kl, r, rq_doc->id.buf);
      } else {
        if (binfo->key_existing) {
          uint64_t tmp_max_key_id = max_key_id.load();
          if (args->mode == 0 && max_key_index != 0) {
            r = r % max_key_index;
            r = r * singledb_thread_num + key_offset;
          } else if (args->mode > 0 && tmp_max_key_id != 0) {
            r = r % tmp_max_key_id;
          }
        }
        if (binfo->seq_fill){
          rq_doc->id.size = binfo->keylen.a;
          keygen_seqfill(r, rq_doc->id.buf, binfo->keylen.a);
        } else {
          rq_doc->id.size = binfo->keylen.a;
          keygen_seed2key(&binfo->keygen, r, rq_doc->id.buf, keylen );
        }
      }

    	if(rq_doc->data.buf == NULL)
    	  rq_doc->data.buf = (char *)Allocate(args->valuepool);

    	if(binfo-> bodylen.type == RND_RATIO)
    	  rq_doc->data.size = get_value_size_by_ratio(binfo, r);
    	else
    	  rq_doc->data.size = binfo->bodylen.a; //binfo.binfo->vp_unitsize;
#endif

    	if (sampling_ms &&
    	    stopwatch_check_ms(&sw_monitor, sampling_ms)) {
    	  if(write_mode == 2)
    	    l_stat = args->l_read;
    	  else if(write_mode == 1)
    	    l_stat = args->l_write;
    	  else
    	    l_stat = args->l_delete;
    	  l_stat->cursor++;
    	  if (l_stat->cursor >= binfo->latency_max) {
    	    l_stat->cursor = l_stat->cursor % binfo->latency_max;
    	    l_stat->nsamples = binfo->latency_max;
    	  } else {
    	    l_stat->nsamples = l_stat->cursor;
    	  }
    	  cur_sample = l_stat->cursor;
    	  stopwatch_init_start(&sw_latency);
    	  stopwatch_start(&sw_monitor);
    	  monitoring = 1;
    	} else {
    	  monitoring = 0;
    	}

#if defined __KV_BENCH
      rq_doc->id.tid = args->tid;
    	err = couchstore_open_document_kv(db[db_idx], &rq_doc->id,
    					  &rq_doc->data, monitoring);
    	DeAllocate(rq_doc->id.buf, args->keypool);
    	rq_doc->id.buf = NULL;
    	DeAllocate(rq_doc->data.buf, args->valuepool);
    	rq_doc->data.buf = NULL;

#elif defined __ROCKS_BENCH || defined(__KVDB_BENCH)
	    err = couchstore_open_document(db[db_idx], rq_id.buf,
				       rq_id.size, NULL, monitoring);
#elif defined __KVROCKS_BENCH
	    err = couchstore_open_document_kvrocks(db[db_idx], rq_doc->id.buf,
				       rq_doc->id.size, rq_doc->data.size, NULL, monitoring);
#else
      rq_doc->id.tid = args->tid;
	    err = couchstore_open_document(db[db_idx], rq_doc->id.buf,
				       rq_doc->id.size, NULL, monitoring);
#if defined __AS_BENCH
      DeAllocate(rq_doc->id.buf, args->keypool);
      rq_doc->id.buf = NULL;
      DeAllocate(rq_doc->data.buf, args->valuepool);
      rq_doc->data.buf = NULL;
#endif

#if defined __FDB_BENCH || defined __LEVEL_BENCH//|| defined __WT_BENCH
    	//TODO: need to fix for other DB
    	rq_doc->id.buf = NULL;
    	couchstore_free_document(rq_doc);
    	rq_doc = NULL;
#endif
#endif

    	if (err != COUCHSTORE_SUCCESS) {
    	  printf("read error: document number %" _F64 "\n", r);
    	} else {
    	  //rq_doc = NULL;
    	}

      } else { // delete
      	if(rq_doc == NULL) {
      	  rq_doc = (Doc *)malloc(sizeof(Doc));
      	  memset(rq_doc, 0, sizeof(Doc));
      	}
      	if(rq_doc->id.buf == NULL)
      	  rq_doc->id.buf = (char *)Allocate(args->keypool);
      	if (binfo->keyfile) {
      	  rq_doc->id.size = keyloader_get_key(&binfo->kl, r, rq_doc->id.buf);
      	} else {
          if (binfo->key_existing) {
            if (args->mode == 0 && max_key_index != 0) {
              r = --max_key_index;
              r = r * singledb_thread_num + key_offset;
            } else if (args->mode > 0 && max_key_id != 0) {
              r = --max_key_id;
            }
          }
      	  if (binfo->seq_fill){
      	    rq_doc->id.size = binfo->keylen.a;
      	    keygen_seqfill(r, rq_doc->id.buf, binfo->keylen.a);
      	  } else {
      	    rq_doc->id.size = binfo->keylen.a;
      	    keygen_seed2key(&binfo->keygen, r, rq_doc->id.buf, keylen);
      	  }
      	}

      	if(rq_doc->data.buf == NULL)
      	  rq_doc->data.buf = (char *)Allocate(args->valuepool);
      	rq_doc->data.size = binfo->bodylen.a; //binfo->vp_unitsize;

      	if (sampling_ms &&
      	  stopwatch_check_ms(&sw_monitor, sampling_ms)) {
      	  if(write_mode == 2)
      	    l_stat = args->l_read;
      	  else if(write_mode == 1)
      	    l_stat = args->l_write;
      	  else
      	    l_stat = args->l_delete;

      	  l_stat->cursor++;
      	  if (l_stat->cursor >= binfo->latency_max) {
      	    l_stat->cursor = l_stat->cursor % binfo->latency_max;
      	    l_stat->nsamples = binfo->latency_max;
      	  } else {
      	    l_stat->nsamples = l_stat->cursor;
      	  }

      	  cur_sample = l_stat->cursor;
      	  stopwatch_init_start(&sw_latency);
      	  stopwatch_start(&sw_monitor);
      	  monitoring = 1;
      	} else {
      	  monitoring = 0;
      	}

      #if defined __KV_BENCH || defined __AS_BENCH
      	couchstore_delete_document_kv(db[db_idx], &rq_doc->id, monitoring);
      #else
      	err = couchstore_delete_document(db[db_idx], rq_doc->id.buf, rq_doc->id.size, monitoring);
      #endif
      	DeAllocate(rq_doc->id.buf, args->keypool);
      	rq_doc->id.buf = NULL;
        DeAllocate(rq_doc->data.buf, args->valuepool);
        rq_doc->data.buf = NULL;
      }

      if (monitoring) {
      	gap = stopwatch_get_curtime(&sw_latency);
      	l_stat->samples[cur_sample] = _timeval_to_us(gap);
      }

      if(write_mode == 1) {
      	args->op_write.fetch_add(1, std::memory_order_release);
      } else if(write_mode == 2){
	      args->op_read.fetch_add(1, std::memory_order_release);
      } else {
	      args->op_delete.fetch_add(1, std::memory_order_release);
      }

    } else { // async mode
      
#if !defined __KV_BENCH && !defined __AS_BENCH
      fprintf(stderr, "Async is not supported in this application\n");
      exit(0);
#else

#if defined __KV_BENCH
      // open iterator
      if(binfo->with_iterator > 0 && args->tid == 0) {
	      int nr = 0;

      	if(binfo->with_iterator == 2) { // iterator alone
      	  // iterator next
      	  struct timespec t1, t2;
      	  clock_gettime(CLOCK_REALTIME, &t1);
      	  nr = 0;

      	  while (1) {
      	    couchstore_iterator_next(db[db_idx]);
      	    while( nr == 0) {
      	      nr = getevents(db[db_idx], 0, 1, contexts, args->tid);
      	    }
      	    for(j = 0; j < nr; j++) {
      	      if(contexts[j]) notify(contexts[j], args->keypool, args->valuepool);
      	    }
      	    release_context(db[db_idx], contexts, nr);
      	    total_entries += couchstore_iterator_get_numentries(db[db_idx]);
      	    nr = 0;
      	    if (couchstore_iterator_check_status(db[db_idx])) break;
      	  }

      	  clock_gettime(CLOCK_REALTIME, &t2);
      	  uint64_t gap = t2.tv_sec * 1000000000L + t2.tv_nsec - (t1.tv_sec * 1000000000L + t1.tv_nsec);
      	  double sec = (double)gap / 1000000000.00;
      	  double tps = (double)total_entries / sec;
      	  fprintf(stdout, "Iteration Done - %ld: total %.2f sec, tps: %.2f\n", total_entries, sec,tps);
      	  got_signal = 1;
      	  break;
      	}
      }
#endif
      if(args->cur_qdepth < binfo->queue_depth) {
	      if(args->terminate_signal) break;
#if defined __KV_BENCH
      	if(binfo->with_iterator == 1  && args->tid == 0 && iterator_send == 0 && args->cur_qdepth < binfo->queue_depth - 1) {
      	  // Do one iterator operation first if (curr qdepth + 1 < max qdepth)
      	  if (!couchstore_iterator_check_status(db[db_idx])) {
      	    if(couchstore_iterator_has_finish(db[db_idx])) {
      	      couchstore_iterator_next(db[db_idx]);
      	      iterator_send = 1;
      	    }
      	  }  /*else {
      	    fprintf(stdout, "Iteration Done \n");
      	    got_signal = 1;
      	    break;
      	  }*/
      	}
#endif

      	if(args->mode > 0){
      	  write_mode = args->mode;
      	} else {
      	  if(cur_op_idx % 100 < binfo->ratio[1] + binfo->ratio[2]){
      	    write_mode = 1; // write: update/insert
      	    if (binfo->key_existing && cur_op_idx % 100 < binfo->ratio[1])
                write_mode = 5;
      	  } else if(cur_op_idx % 100 < binfo->ratio[0] + binfo->ratio[1] + binfo->ratio[2]){
      	    write_mode = 2; // read
      	  } else {
      	    write_mode = 4; // delete
      	  }
      	  cur_op_idx++;
      	}

      	if (write_mode == 1 || write_mode == 5) { // write
          if (binfo->key_existing) {
            if (args->mode == 0) {
              if (write_mode == 5 && max_key_index != 0) {
                r = r % max_key_index;
              } else {
                r = max_key_index++;
              }
              r = r * singledb_thread_num + key_offset;
              write_mode = 1;
            } else if (args->mode > 0) {
              if (r > max_key_id)
                r = max_key_id++;
            }
          }
      	  if(rq_doc == NULL) rq_doc = (Doc *)malloc(sizeof(Doc));
      	  _create_doc(binfo, r, &rq_doc, NULL, binfo->seq_fill,
      		      args->socketid, args->keypool, args->valuepool, args->tid);
#if !defined __KV_BENCH && !defined __AS_BENCH

#else
      	  if (sampling_ms &&
      	      stopwatch_check_ms(&sw_monitor, sampling_ms)) {
      	    stopwatch_start(&sw_monitor);
      	    monitoring = 1;
      	  }else {
      	    monitoring = 0;
      	  }

      	  err = couchstore_save_document(db[db_idx], rq_doc,
      					 NULL, monitoring);

#endif

        } else if(write_mode == 2) { // read

      	  if(rq_doc == NULL) rq_doc = (Doc *)malloc(sizeof(Doc));
      	  rq_doc->id.buf = (char *)Allocate(args->keypool);

      	  if (binfo->keyfile) {
      	    rq_doc->id.size = keyloader_get_key(&binfo->kl, r, rq_doc->id.buf);
      	  } else {
            if (binfo->key_existing) {
              uint64_t tmp_max_key_id = max_key_id.load();
              if (args->mode == 0 && max_key_index != 0) {
                r = r % max_key_index;
                r = r * singledb_thread_num + key_offset;
              } else if (args->mode > 0 && tmp_max_key_id != 0) {
                r = r % tmp_max_key_id;
              }
            }
      	    if (binfo->seq_fill) {
      	      rq_doc->id.size = binfo->keylen.a;
      	      keygen_seqfill(r, rq_doc->id.buf, binfo->keylen.a);
      	    } else{
      	      rq_doc->id.size = binfo->keylen.a;
      	      keygen_seed2key(&binfo->keygen, r, rq_doc->id.buf, keylen);
      	    }
      	  }

#if defined __KV_BENCH
      	  rq_doc->data.buf = (char *)Allocate(args->valuepool); //(char*)kvs_zalloc(1024, 4096);
      	  if (binfo->bodylen.type == RND_RATIO)
      	    rq_doc->data.size = get_value_size_by_ratio(binfo, r);
      	  else
      	    rq_doc->data.size = binfo->bodylen.a; //binfo->vp_unitsize;
#endif
      	  if (sampling_ms &&
      	      stopwatch_check_ms(&sw_monitor, sampling_ms)) {
      	    stopwatch_start(&sw_monitor);
      	    monitoring = 1;
      	  }else {
      	    monitoring = 0;
      	  }

#if defined __KV_BENCH
          rq_doc->id.tid = args->tid;
      	  couchstore_open_document_kv(db[db_idx], &rq_doc->id, &rq_doc->data, monitoring);

      	  //DeAllocate(rq_doc->id.buf, args->keypool);

#else

      	  couchstore_open_document(db[db_idx], rq_doc->id.buf, rq_doc->id.size, NULL, monitoring);
#endif
        } else { // delete
      	  if(rq_doc == NULL) rq_doc = (Doc *)malloc(sizeof(Doc));

      	  rq_doc->id.buf = (char *)Allocate(args->keypool);
      	  if (binfo->keyfile) {
      	    rq_doc->id.size = keyloader_get_key(&binfo->kl, r, rq_doc->id.buf);
      	  } else {
              if (binfo->key_existing) {
                if (args->mode == 0 && max_key_index != 0) {
                  r = --max_key_index;
                  r = r * singledb_thread_num + key_offset;
                } else if (args->mode > 0 && max_key_id != 0) {
                  r = --max_key_id;
                }
              }
      	    if (binfo->seq_fill){
      	      rq_doc->id.size = binfo->keylen.a;
      	      keygen_seqfill(r, rq_doc->id.buf, binfo->keylen.a);
      	    } else {
      	      rq_doc->id.size = binfo->keylen.a;
      	      keygen_seed2key(&binfo->keygen, r, rq_doc->id.buf, keylen);
      	    }
      	  }

      	  if(rq_doc->data.buf == NULL)
      	    rq_doc->data.buf = (char *)Allocate(args->valuepool);
      	  rq_doc->data.size = binfo->bodylen.a;// binfo->vp_unitsize;

      	  if (sampling_ms &&
      	      stopwatch_check_ms(&sw_monitor, sampling_ms)) {
      	    stopwatch_start(&sw_monitor);
      	    monitoring = 1;
      	  }else {
      	    monitoring = 0;
      	  }

#if defined __KV_BENCH || defined __AS_BENCH
          rq_doc->id.tid = args->tid;
      	  couchstore_delete_document_kv(db[db_idx], &rq_doc->id, monitoring);
#else
      	  couchstore_delete_document(db[db_idx], rq_doc->id.buf, rq_doc->id.size, monitoring);
#endif
      	}
      	args->cur_qdepth++;
      	if(write_mode == 1) {
      	  args->op_write.fetch_add(1, std::memory_order_release);
      	} else if(write_mode == 2){
      	  args->op_read.fetch_add(1, std::memory_order_release);
      	} else {
      	  args->op_delete.fetch_add(1, std::memory_order_release);
      	}

      	continue;
      }

      if(args->cur_qdepth > 0) {

      	// deal with finished IO
      	int nr = getevents(db[db_idx], 0, args->cur_qdepth, contexts, args->tid);
      	for(j = 0; j < nr; j++) {
      	  if(contexts[j]) notify(contexts[j], args->keypool, args->valuepool);
      	}

      	release_context(db[db_idx], contexts, nr);
      	args->cur_qdepth -= nr;

#if defined __KV_BENCH
      	if(binfo->with_iterator == 1 && args->tid == 0) {
      	  total_entries += couchstore_iterator_get_numentries(db[db_idx]);
      	  if(couchstore_iterator_has_finish(db[db_idx]))
      	    iterator_send = 0;
      	  /*
      	  if(couchstore_iterator_check_status(db[db_idx])){
      	    got_signal = 1;
      	    break;
      	  }*/
      	}
#endif
      }

      if(args->cur_qdepth == binfo->queue_depth)
	      usleep(1);
#endif
    } // end of async
    
  }   // big while


#if defined __KV_BENCH || defined __AS_BENCH
  int nr = args->cur_qdepth;
  while (args->cur_qdepth > 0 && binfo->kv_write_mode == 0/* && nr > 0*/) {
    // deal with the rest in the Q
    nr = getevents(db[db_idx], 0, args->cur_qdepth, contexts, args->tid);
    for (j = 0; j < nr; j++) {
      if(contexts[j]) notify(contexts[j], args->keypool, args->valuepool);
    }
    release_context(db[db_idx], contexts, nr);
    args->cur_qdepth -= nr;
#if defined __KV_BENCH
    if(binfo->with_iterator == 1 && args->tid == 0) {
      total_entries += couchstore_iterator_get_numentries(db[db_idx]);
    }
#endif
  }

#endif

#if defined __KV_BENCH
  if(binfo->with_iterator > 0 && args->tid == 0) {
    int nr = 0;
    //int use_udd;
    couchstore_iterator_close(db[db_idx]);
    fprintf(stdout, "Iterator close done \n");
  }
    
#endif

  args->b_stat->op_read.fetch_add(args->op_read.load(), std::memory_order_release);
  args->b_stat->op_write.fetch_add(args->op_write.load(), std::memory_order_release);
  args->b_stat->op_delete.fetch_add(args->op_delete.load(), std::memory_order_release);
  args->b_stat->op_iter_key.fetch_add(total_entries, std::memory_order_release);
  
#if defined(__FDB_BENCH) || defined(__WT_BENCH)
  if (rq_doc) {
    free(rq_doc->id.buf);
    free(rq_doc->data.buf);
    free(rq_doc);
  }
  if (rq_info) {
    free(rq_info);
  }
#endif

  if(args->keypool)
    destroy(binfo->allocatortype, args->keypool);
  if(args->valuepool)
    destroy(binfo->allocatortype, args->valuepool);

  return NULL;
}

void _wait_leveldb_compaction(struct bench_info *binfo, Db **db)
{
    int n=6;
    int i, ret; (void)ret;
    char buf[256], str[64];
    unsigned long temp;
    uint64_t count=0;
    uint64_t val[n];
    FILE *fp;

    for (i=0;i<n;++i){
        val[i] = i; // set different value
    }

    sprintf(buf, "/proc/%d/io", getpid());
    fflush(stdout);
    lprintf("waiting for background compaction of "
            "LevelDB (RocksDB) log files..");
    fflush(stdout);

    while(1) {
        fp = fopen(buf, "r");
        if(fp == NULL) {
          fprintf(stderr, "open %s failed\n", buf);
          return;
        }
        while(!feof(fp)) {
            ret = fscanf(fp, "%s %lu", str, &temp);
            if (!strcmp(str, "write_bytes:")) {
                val[count] = temp;
                count = (count+1) % n;
                for (i=1;i<n;++i){
                    if (val[i-1] != val[i]) goto wait_next;
                }
                lprintf(" done\n");
                fflush(stdout);
                fclose(fp);
                return;
            }
        }

wait_next:
        fclose(fp);
        usleep(600000);
    }
}

// non-standard functions for extension
couchstore_error_t couchstore_set_flags(uint64_t flags);
couchstore_error_t couchstore_set_cache(uint64_t size);
couchstore_error_t couchstore_set_spdk_cache(uint64_t size);
couchstore_error_t couchstore_set_compaction(int mode,
                                             size_t compact_thres,
                                             size_t block_reuse_thres);
couchstore_error_t couchstore_set_auto_compaction_threads(int num_threads);
couchstore_error_t couchstore_set_chk_period(size_t seconds);
couchstore_error_t couchstore_open_conn(const char *filename);
couchstore_error_t couchstore_close_conn();
couchstore_error_t couchstore_set_wal_size(size_t size);
couchstore_error_t couchstore_set_wbs_size(uint64_t size);
couchstore_error_t couchstore_set_idx_type(int type);
couchstore_error_t couchstore_set_sync(Db *db, int sync);
couchstore_error_t couchstore_set_bloom(int bits_per_key);
couchstore_error_t couchstore_set_compaction_style(int style);
couchstore_error_t couchstore_set_compression(int opt);
couchstore_error_t couchstore_set_split_pct(int pct);
couchstore_error_t couchstore_set_page_size(size_t leaf_pg_size, size_t int_pg_size);
// non-standard functions for aerospike
couchstore_error_t couchstore_as_setup(const char *hosts,
				       uint16_t port,
				       uint32_t loop_capacity,
				       const char *name_space,
				       int write_mode);
// non-standard functions for extension for kvs & kvv
couchstore_error_t couchstore_setup_device(const char *dev_path,
					   char **device_names,
					   char *config_file,
					   int num_devices, int write_mode, int is_polling);
couchstore_error_t couchstore_close_device(int32_t dev_id);
couchstore_error_t couchstore_exit_env(void);
int getevents(Db *db, int min, int max, IoContext **context, int tid);
int release_context(Db *db, IoContext **contexts, int nr);
couchstore_error_t couchstore_kvs_set_aio_option(int queue_depth, char *core_masks, char *cq_thread_masks, uint32_t mem_size_mb);
couchstore_error_t couchstore_kvs_set_aiothreads(int aio_threads);
couchstore_error_t couchstore_kvs_set_coremask(char *core_ids);
couchstore_error_t couchstore_kvs_get_aiocompletion(int32_t *count);
couchstore_error_t couchstore_kvs_set_max_sample(uint32_t sample_num);

static int _does_file_exist(char *filename) {
    struct stat st;
    int result = stat(filename, &st);
    return result == 0;
}

static char *_get_dirname(char *filename, char *dirname_buf)
{
    int i;
    int len = strlen(filename);

    // find first '/' from right
    for (i=len-1; i>=0; --i){
        if (filename[i] == '/' && i>0) {
        	memcpy(dirname_buf, filename, i);
        	dirname_buf[i] = 0;
        	return dirname_buf;
        }
    }
    return NULL;
}

static char *_get_filename_pos(char *filename)
{
    int i;
    int len = strlen(filename);

    // find first '/' from right
    for (i=len-1; i>=0; --i){
        if (filename[i] == '/') {
            return filename + i + 1;
        }
    }
    return NULL;
}

static int _cmp_uint32_t(const void *key1, const void *key2)
{
    uint32_t a, b;
    // must ensure that key1 and key2 are pointers to uint64_t values
    a = *(uint32_t*)key1;
    b = *(uint32_t*)key2;

    if (a < b) {
        return -1;
    } else if (a > b) {
        return 1;
    } else {
        return 0;
    }
}

#ifdef __TIME_CHECK
void _print_time(struct latency_stat *l_prep, struct latency_stat *l_real)
{
  int i;
  if(timelog_fp) {
    for(i = 0; i < l_prep->nsamples; i++) {
      fprintf(timelog_fp, "%d %d\n", (int)l_prep->samples[i], (int)l_real->samples[i]);
    }
  }
}
#endif

void _print_latency(struct bench_info *binfo,
		    struct latency_stat *l_stat,
		    const char *unit)
{

  int percentile[6] = {100, 500, 5000, 9500, 9900, 9999};
  uint64_t i, pos, begin, end;
  double avg = 0;

  if (l_stat->nsamples < 100) {
    // more than 100 samples are necessary
    printf("not enough samples %ld\n", l_stat->nsamples);
    return;
  }

  // sort
  qsort(l_stat->samples, l_stat->nsamples, sizeof(uint32_t), _cmp_uint32_t);

  // average (discard beyond 1% & 99%)
  begin = l_stat->nsamples / 100;
  end = l_stat->nsamples*99 / 100;
  for (i=begin; i<end; ++i){
    avg += l_stat->samples[i];
  }
  avg /= (end - begin);
  lprintf("%d samples (%d Hz), average: %.2f %s\n",
	  (int)l_stat->nsamples, (int)binfo->latency_rate,
	  avg, unit);

  for (i=0; i<6; ++i) { // screen: only 7 percentiles
    pos = l_stat->nsamples * percentile[i] / 10000;
    if( i == 5 && l_stat->nsamples < 10000){
      lprintf("N/A %s (%.2f%%)",
	      unit, (double)percentile[i] / 100);
    }else {
      lprintf("%d %s (%.2f%%)", (int)l_stat->samples[pos],
	      unit, (double)percentile[i] / 100);
    }
    if (i+1 < 6) {
      lprintf(", ");
    } else {
      lprintf("\n");
    }
  }
}

/*
  mode = 1: population
  mode = 2: evaluation  
 */
void _print_percentile(struct bench_info *binfo,
		       struct latency_stat *wl_stat,
		       struct latency_stat *rl_stat,
		       struct latency_stat *dl_stat,
		       const char *unit, int mode)
{
    int percentile[6] = {100, 500, 5000, 9500, 9900, 9999};
    uint64_t i, pos1, pos2;
    FILE *tmp;

    if(wl_stat) {
      lprintf("\nwrite latency distribution\n"); 
      _print_latency(binfo, wl_stat, unit);
    }
    if(rl_stat) {
      lprintf("\nread latency distribution\n");
      _print_latency(binfo, rl_stat, unit);
    }
    if(dl_stat) {
      lprintf("\ndelete latency distribution\n");
      _print_latency(binfo, dl_stat, unit);
    }


    tmp = (mode == 1) ? insert_latency_fp : run_latency_fp; 
    //TODO: add deletion stats
    if (tmp) { // log file: all percentiles
      fprintf(tmp, "pos,write,read,delete\n");
      for (i=1;i<100;++i){
      	/*
      	if (wl_stat && rl_stat) {
      	  pos1 = wl_stat->nsamples * i / 100;
      	  pos2 = rl_stat->nsamples * i / 100;
      	  fprintf(tmp, "%d,%d,%d\n", (int)i, (int)wl_stat->samples[pos1],(int)rl_stat->samples[pos2]);
      	}
      	else if(wl_stat) {
      	  pos1 = wl_stat->nsamples * i / 100;
      	  fprintf(tmp, "%d,%d,0\n", (int)i, (int)wl_stat->samples[pos1]);
      	}
      	else if(rl_stat) {
      	  pos2 = rl_stat->nsamples * i / 100;
      	  fprintf(tmp, "%d,0,%d\n", (int)i, (int)rl_stat->samples[pos2]);
      	}
      } // for loop
      */
      	if (wl_stat){
      	  pos1 = wl_stat->nsamples * i / 100;
      	  fprintf(tmp, "%d,%d,", (int)i, (int)wl_stat->samples[pos1]);
      	} else {
      	  fprintf(tmp, "%d,0,", (int)i);
      	}
      	if(rl_stat) {
      	  pos1 = rl_stat->nsamples * i / 100;
      	  fprintf(tmp, "%d,", (int)rl_stat->samples[pos1]);
      	} else {
      	  fprintf(tmp, "0,");
      	}
      	if(dl_stat) {
      	  pos1 = dl_stat->nsamples * i / 100;
      	  fprintf(tmp, "%d\n", (int)dl_stat->samples[pos1]);
      	} else {
      	  fprintf(tmp, "0\n");
      	}
      }
    }
}

void db_env_setup(struct bench_info *binfo){

#if !defined(__COUCH_BENCH) && !defined(__KV_BENCH) && !defined(__KVROCKS_BENCH) && !defined(__AS_BENCH)
  couchstore_set_cache(binfo->cache_size);
  couchstore_set_compression(binfo->compression);
#endif
  
#if defined(__FDB_BENCH)
  // ForestDB: set compaction mode, threshold, WAL size, index type
  couchstore_set_compaction(binfo->auto_compaction,
			    binfo->compact_thres,
			    binfo->block_reuse_thres);
  couchstore_set_idx_type(binfo->fdb_type);
  couchstore_set_wal_size(binfo->fdb_wal);
  couchstore_set_auto_compaction_threads(binfo->auto_compaction_threads);
#endif
#if defined(__WT_BENCH) || defined(__FDB_BENCH)
  // WiredTiger & ForestDB: set compaction period
  couchstore_set_chk_period(binfo->compact_period);
#endif
  
#if defined(__ROCKS_BENCH) || defined(__BLOBFS_ROCKS_BENCH) || defined (__LEVEL_BENCH) || defined(__KVDB_BENCH)
  // RocksDB: set compaction style
#if !defined (__LEVEL_BENCH)
  if (binfo->compaction_style) {
    couchstore_set_compaction_style(binfo->compaction_style);
  }
#endif
  couchstore_set_bloom(binfo->bloom_bpk);
#endif // __ROCKS_BENCH
#if defined(__WT_BENCH)
  // WiredTiger: set split_pct and page size parameters
  couchstore_set_split_pct(binfo->split_pct);
  couchstore_set_page_size(binfo->leaf_pg_size, binfo->int_pg_size);
#endif
  
#if defined(__KV_BENCH)
  //if(binfo->kv_write_mode == 0) { // set aio context
    // set queuedepth, aiothreads, coremask
    couchstore_kvs_set_aio_option(binfo->queue_depth, binfo->core_ids, binfo->cq_thread_ids, binfo->mem_size_mb);
    couchstore_kvs_set_aiothreads(binfo->aiothreads_per_device);
    couchstore_kvs_set_coremask(binfo->core_ids);
    couchstore_kvs_set_max_sample(binfo->latency_max);
    //}
    couchstore_setup_device(binfo->kv_device_path, NULL, binfo->kv_emul_configfile, binfo->nfiles, binfo->kv_write_mode, 0/*binfo->is_polling*/);
#endif
#if defined(__AS_BENCH)
  //TODO: other initialization for aerospike
  couchstore_as_setup(binfo->device_path, binfo->as_port, binfo->loop_capacity, binfo->name_space, binfo->kv_write_mode);
#endif
#if defined(__BLOBFS_ROCKS_BENCH)
  couchstore_set_spdk_cache(binfo->spdk_cache_size);
  couchstore_setup_device(binfo->filename, binfo->device_name,
			  binfo->spdk_conf_file, binfo->nfiles, 0, 0);
#endif

}

void do_bench(struct bench_info *binfo)
{
  BDR_RNG_VARS;
  int i, j, ret; (void)j; (void)ret;
  int curfile_no, compaction_turn;
  int compaction_no[binfo->nfiles], total_compaction = 0;
  int cur_compaction = -1;
  int bench_threads, singledb_thread_num, first_db_idx;
  uint64_t op_count_read, op_count_write, op_count_delete, op_count_iter_key, display_tick = 0;
  uint64_t prev_op_count_read, prev_op_count_write, prev_op_count_delete;
  uint64_t written_init, written_final, written_prev;
  uint64_t nand_written_init, nand_written_final;
  uint64_t avg_docsize;
  char curfile[256], newfile[256], bodybuf[1024], cmd[256];
  char fsize1[128], fsize2[128], *str;
  char spaces[128];
  void *compactor_ret;
  void **bench_worker_ret;
  double gap_double;
  bool warmingup = false;
  bool startlog = false;
#if defined __AS_BENCH
  Db *db[binfo->nfiles * binfo->pop_nthreads];
#else
  Db *db[binfo->nfiles];
#endif
  
#ifdef __FDB_BENCH
  Db *info_handle[binfo->nfiles];
#endif
#if defined(__KV_BENCH) || defined(__AS_BENCH) || defined(__KVROCKS_BENCH)
  //int32_t dev_id[binfo->nfiles];
  char *dev_path[binfo->nfiles]; 
#endif
#if defined(__AS_BENCH)
  // hosts & port
  const char *host;
  uint16_t port;
#endif
#ifdef __BLOBFS_ROCKS_BENCH
  //rocksdb::Env *blobfs_env;
#endif
  DbInfo *dbinfo;
  thread_t tid_compactor;
  thread_t *bench_worker;
  spin_t cur_compaction_lock;
  struct stopwatch sw, sw_compaction, progress;
  struct timeval gap, _gap;
  struct zipf_rnd zipf;
  struct bench_result result;
  struct bench_shared_stat b_stat;
  struct bench_thread_args *b_args;
  //struct latency_stat l_read, l_write, l_delete;
#if defined(__FDB_BENCH) || defined(__COUCH_BENCH)
  struct compactor_args c_args;
#endif
  FILE *tmp;

  memleak_start();

  spin_init(&cur_compaction_lock);

  dbinfo = (DbInfo *)malloc(sizeof(DbInfo));
  stopwatch_init(&sw);
  stopwatch_init(&sw_compaction);

  _bench_result_init(&result, binfo);

  written_init = written_final = written_prev = 0;
  memset(&tid_compactor, 0x0, sizeof(tid_compactor));

  if (binfo->bodylen.type == RND_NORMAL || binfo->bodylen.type == RND_FIXED) {
    avg_docsize = binfo->bodylen.a;
  } else {
    avg_docsize = (binfo->bodylen.a + binfo->bodylen.b)/2;
  }
  
  if (binfo->keylen.type == RND_NORMAL || binfo->keylen.type == RND_FIXED) {
    avg_docsize += binfo->keylen.a;
  } else {
    avg_docsize += ((binfo->keylen.a + binfo->keylen.b)/2);
  }

  strcpy(fsize1, print_filesize_approx(0, cmd));
  strcpy(fsize2, print_filesize_approx(0, cmd));
  memset(spaces, ' ', 80);
  spaces[80] = 0;

  db_env_setup(binfo);
  
#if defined(__KV_BENCH) || defined (__KVROCKS_BENCH) 
  // open device only once
  //couchstore_setup_device(binfo->kv_device_path, NULL, NULL, binfo->nfiles, binfo->kv_write_mode);
  char *pt;
  i = 0;
  pt = strtok (binfo->kv_device_path, ",");
  while (pt != NULL) {
    dev_path[i++] = pt;
    pt = strtok (NULL, ",");
  }
#endif

  if (binfo->initialize) {
    // === initialize and populate files ========
#if !defined (__KV_BENCH) && !defined(__KVROCKS_BENCH) && !defined(__AS_BENCH)
    // check if previous file exists
    if (_dir_scan(binfo, NULL)) {
      // ask user
      char answer[64], *ret;
      memset(answer, 0x0, sizeof(answer));
      lprintf("\nPrevious DB file already exists. "
	      "Are you sure to remove it (y/N)? ");
      ret = fgets(answer, sizeof(answer), stdin);
      if (ret && !(answer[0] == 'Y' || answer[0] == 'y')) {
      	lprintf("Terminate benchmark ..\n\n");

      	free(dbinfo);
      	_bench_result_free(&result);
      	memleak_end();
      	return;
      }
    }

    // erase previous db file
    lprintf("\ninitialize\n");
    sprintf(cmd, "rm -rf %s*/* 2> errorlog.txt", binfo->filename);
    ret = system(cmd);

    // create directory if doesn't exist
    if (!_does_file_exist(binfo->filename)){
      printf("create %s\n", binfo->filename);
      sprintf(cmd, "mkdir -p %s > errorlog.txt", binfo->filename);
      ret = system(cmd);
    }
#endif

    if (binfo->pop_first) {
#if defined(__WT_BENCH)
      // WiredTiger: open connection
      couchstore_set_idx_type(binfo->wt_type);
      couchstore_open_conn((char*)binfo->filename);
#endif
#if defined(__LEVEL_BENCH) || defined(__ROCKS_BENCH) || defined(__BLOBFS_ROCKS_BENCH) || defined(__KVDB_BENCH)
      // LevelDB, RocksDB: set WBS size
      couchstore_set_wbs_size(binfo->wbs_init);
#endif


#if defined __AS_BENCH
      for (i = 0; i < (int)binfo->nfiles * binfo->pop_nthreads; ++i) {
	      couchstore_open_db(curfile, COUCHSTORE_OPEN_FLAG_CREATE, &db[i]);
      }
#else
      for (i=0; i<(int)binfo->nfiles; ++i) {
	      compaction_no[i] = 0;
#if !defined(__KV_BENCH)
      	sprintf(curfile, "%s/%d", binfo->init_filename, i);
      	printf("db %d name is %s\n", i, curfile);
#endif
#if defined(__KV_BENCH)
      	couchstore_open_db_kvs(dev_path[i], &db[i], i);

#elif defined(__KVROCKS_BENCH)
      	fprintf(stdout, "=== opening dev %s\n", dev_path[i]);
      	couchstore_open_db(dev_path[i], COUCHSTORE_OPEN_FLAG_CREATE, &db[i]);
#else
#if defined(__FDB_BENCH)
      	if (!binfo->pop_commit) {
      	  // set wal_flush_before_commit flag (0x1)
      	  // clear auto_commit (0x10)
      	  couchstore_set_flags(0x1);
      	}
#endif
        couchstore_open_db(curfile, COUCHSTORE_OPEN_FLAG_CREATE, &db[i]);
#endif
#if defined(__LEVEL_BENCH) || defined(__ROCKS_BENCH) || defined(__KVROCKS_BENCH) || defined(__BLOBFS_ROCKS_BENCH) || defined(__KVDB_BENCH)
      	if (!binfo->pop_commit) {
      	  couchstore_set_sync(db[i], 0);
      	}
#endif
      }
#endif

      stopwatch_start(&sw);
      if(binfo->pop_nthreads != 0 && binfo->nfiles != 0 && binfo->ndocs != 0)
        population(db, binfo);

#if  defined(__PRINT_IOSTAT) && \
  (defined(__LEVEL_BENCH) || defined(__ROCKS_BENCH)  || defined(__BLOBFS_ROCKS_BENCH) || defined(__KVDB_BENCH))
      // Linux + (LevelDB or RocksDB): wait for background compaction
      gap = stopwatch_stop(&sw);
      LOG_PRINT_TIME(gap, " sec elapsed\n");
      //print_proc_io_stat(cmd, 1);
#endif // __PRINT_IOSTAT && (__LEVEL_BENCH || __ROCKS_BENCH)

#if !defined(__KV_BENCH) && !defined (__KVROCKS_BENCH) && !defined(__AS_BENCH)
      if (binfo->sync_write) {
      	fflush(stdout);
      	sprintf(cmd, "sync");
      	ret = system(cmd);
      	fflush(stdout);
      }
#endif

#if !defined __KV_BENCH && !defined __AS_BENCH
      written_final = written_init = print_proc_io_stat(cmd, 1);
      written_prev = written_final;
      double waf_a;
      uint64_t w_per_doc = (double)written_final / binfo->ndocs;
      waf_a = (double)w_per_doc / avg_docsize;
      lprintf("%s written per doc update (%.1f x write amplification)\n",
	      print_filesize_approx(w_per_doc, bodybuf), waf_a);
#endif

#if !defined __KVROCKS_BENCH && !defined __KV_BENCH && !defined __BLOBFS_ROCKS_BENCH
#if defined __AS_BENCH
      for (i=0; i<(int)binfo->nfiles * binfo->pop_nthreads; ++i){
      	printf("close db %d\n", i);
      	couchstore_close_db(db[i]);
      }
#else
      for (i=0; i<(int)binfo->nfiles; ++i){
      	printf("close db %d\n", i);
      	couchstore_close_db(db[i]);
      }
#endif
#endif
      gap = stopwatch_stop(&sw);
#if defined(__LEVEL_BENCH) || defined(__ROCKS_BENCH) || defined(__BLOBFS_ROCKS_BENCH) || defined(__KVDB_BENCH)
#if defined(__PRINT_IOSTAT)
      gap.tv_sec -= 3; // subtract waiting time
#endif // __PRINT_IOSTAT
#endif // __LEVEL_BENCH || __ROCKS_BENCH
      gap_double = gap.tv_sec + (double)gap.tv_usec / 1000000.0;
      LOG_PRINT_TIME(gap, " sec elapsed ");
      lprintf("(%.2f ops/sec)\n", gap_double ? binfo->ndocs * binfo->nfiles / gap_double : 0);
    } else {
      fprintf(stdout, "Start benchmark without population\n");
#if defined(__KV_BENCH)
      for (i=0; i<(int)binfo->nfiles; ++i){
  	    couchstore_open_db_kvs(dev_path[i], &db[i], i);
      }

#elif defined(__KVROCKS_BENCH)
      for(i=0; i<(int)binfo->nfiles; ++i){
        couchstore_open_db(dev_path[i], COUCHSTORE_OPEN_FLAG_CREATE, &db[i]);
      }
#elif defined (__AS_BENCH)
      //TODO: check whether need to open aerospike connection
     
#endif
    } // end of pop_first = false
  } else {
    // === load existing files =========
    stopwatch_start(&sw);
    for (i=0; i<(int)binfo->nfiles; ++i) {
      compaction_no[i] = 0;
    }
#if defined(__WT_BENCH)
    // for WiredTiger: open connection
    couchstore_open_conn((char*)binfo->filename);
#elif defined (__ROCKS_BENCH) || defined (__FDB_BENCH) || defined (__LEVEL_BENCH) || defined(__KVDB_BENCH)
    _dir_scan(binfo, compaction_no);
#elif defined (__KV_BENCH)
    for (i=0; i<(int)binfo->nfiles; ++i){
      couchstore_open_db_kvs(dev_path[i], &db[i], i);
    }
#endif
  } // load existing files

    
  // ==== perform benchmark ====
  lprintf("\nbenchmark\n");
  lprintf("opening DB instance .. \n");

  compaction_turn = 0;

#if defined(__ROCKS_BENCH) || defined (__LEVEL_BENCH) || defined(__KVDB_BENCH) // || defined(__BLOBFS_ROCKS_BENCH) 
  couchstore_set_wbs_size(binfo->wbs_bench);
#endif
#if defined(__FDB_BENCH)
  // ForestDB:
  // clear wal_flush_before_commit flag (0x1)
  // set auto_commit (0x10) if async mode
  if (binfo->sync_write) {
    couchstore_set_flags(0x0);
  } else {
    couchstore_set_flags(0x10);
  }
#endif

  if (binfo->batch_dist.type == RND_ZIPFIAN) {
    // zipfian distribution .. initialize zipf_rnd
    if(binfo->nops > 0) {
      zipf_rnd_init(&zipf, (binfo->ndocs + binfo->nops * binfo->ratio[2] * 2) / binfo->batch_dist.b,
		    binfo->batch_dist.a/100.0, 1024*1024);
    } else {
      zipf_rnd_init(&zipf, binfo->ndocs * binfo->amp_factor / binfo->batch_dist.b,
		    binfo->batch_dist.a/100.0, 1024*1024);
    }
  }

  // set signal handler
  old_handler = signal(SIGINT, signal_handler);
  b_stat.op_read = b_stat.op_write= b_stat.op_delete = b_stat.op_iter_key = 0;
  
  // thread args
  /*
  if (binfo->nreaders + binfo->niterators + binfo->nwriters + binfo->ndeleters == 0){
    // create a dummy thread
    bench_threads = 1;
    b_args = alca(struct bench_thread_args, bench_threads);
    bench_worker = alca(thread_t, bench_threads);
    b_args[0].mode = 5; // dummy thread
    } */
  singledb_thread_num = binfo->nreaders + binfo->niterators + binfo->nwriters + binfo->ndeleters;
  bench_threads = singledb_thread_num * binfo->nfiles;
#if defined __KV_BENCH
  // only support one thread per device for kv
  //bench_threads = binfo->nfiles;
#endif
  
  b_args = alca(struct bench_thread_args, bench_threads);
  bench_worker = alca(thread_t, bench_threads);
  
  //pthread_attr_t attr;
  pthread_attr_t attr[bench_threads];
  cpu_set_t cpus;
  //pthread_attr_init(&attr);
  int db_idx, td_idx, nodeid;
  int total_ratio = binfo->ratio[0] + binfo->ratio[1] + binfo->ratio[2] + binfo->ratio[3];
  pool_info_t info_key, info_value;
  // initialize keypool & value pool
  info_key.allocatortype = info_value.allocatortype = binfo->allocatortype;
  info_key.numunits = binfo->kp_numunits;
  info_key.unitsize = binfo->kp_unitsize;
  info_key.alignment = binfo->kp_alignment;
  info_value.numunits = binfo->vp_numunits;
  info_value.unitsize = binfo->vp_unitsize;
  info_value.alignment = binfo->vp_alignment;
  
  for (i=0;i<bench_threads;++i){
    if(total_ratio > 0 && total_ratio <= 100) {   
      b_args[i].mode = 0; // mixed workload in one thread
    } else { // no ratio control, dedicated thread for each operation
      if ((size_t)i % singledb_thread_num < binfo->nwriters) {
	      b_args[i].mode = 1; // writer
      } else if ((size_t)i % singledb_thread_num < binfo->nwriters + binfo->nreaders) {
	      b_args[i].mode = 2; // reader
      } else if ((size_t)i % singledb_thread_num < binfo->nwriters +
		    binfo->nreaders + binfo->niterators){
	      b_args[i].mode = 3; // iterator
      } else {
	      b_args[i].mode = 4; // deleters
      }
    }

    b_args[i].id = i;
    b_args[i].rnd_seed = rnd_seed;
    b_args[i].compaction_no = compaction_no;
    b_args[i].b_stat = &b_stat;
    b_args[i].l_read = (struct latency_stat *)calloc(1, sizeof(struct latency_stat));
    b_args[i].l_write = (struct latency_stat *)calloc(1, sizeof(struct latency_stat));
    b_args[i].l_delete = (struct latency_stat *)calloc(1, sizeof(struct latency_stat));
    b_args[i].l_read->samples = (uint32_t*)malloc(sizeof(uint32_t) * binfo->latency_max);
    b_args[i].l_write->samples = (uint32_t*)malloc(sizeof(uint32_t) * binfo->latency_max);
    b_args[i].l_delete->samples = (uint32_t*)malloc(sizeof(uint32_t) * binfo->latency_max);
    b_args[i].op_read = b_args[i].op_write = b_args[i].op_delete = b_args[i].op_iter_key = 0;
    b_args[i].cur_qdepth = 0;
    
    //b_args[i].result = &result;
    b_args[i].zipf = &zipf;
    b_args[i].terminate_signal = 0;
    b_args[i].op_signal = 0;
    b_args[i].binfo = binfo;

    b_args[i].keypool = (mempool_t *)malloc(sizeof(mempool_t));
    b_args[i].keypool->base = b_args[i].keypool->nextfreeblock = NULL;

    b_args[i].valuepool = (mempool_t *)malloc(sizeof(mempool_t));
    b_args[i].valuepool->base = b_args[i].valuepool->nextfreeblock = NULL;
    
    //open db instances
#if defined(__FDB_BENCH) || defined(__COUCH_BENCH) || defined(__WT_BENCH)
    b_args[i].db = (Db**)malloc(sizeof(Db*) * binfo->nfiles);
    for (j=0; j<(int)binfo->nfiles; ++j){
      sprintf(curfile, "%s%d.%d", binfo->filename, j, compaction_no[j]);
      couchstore_open_db(curfile,
			 COUCHSTORE_OPEN_FLAG_CREATE |
			 ((binfo->sync_write)?(0x10):(0x0)),
			 &b_args[i].db[j]);
#if defined(__FDB_BENCH)
      // ForestDB: open another handle to get DB info
      if (i==0) {
	      couchstore_open_db(curfile,
			   COUCHSTORE_OPEN_FLAG_CREATE |
			   ((binfo->sync_write)?(0x10):(0x0)),
			   &info_handle[j]);
      }
#endif
    }
#elif defined(__ROCKS_BENCH) || defined (__LEVEL_BENCH) || defined(__KVDB_BENCH) //|| defined(__BLOBFS_ROCKS_BENCH) 
    if (i % singledb_thread_num ==0) {
      b_args[i].db = (Db**)malloc(sizeof(Db*));
      sprintf(curfile, "%s/%d", binfo->filename, i / singledb_thread_num);
      couchstore_open_db(curfile,
			 COUCHSTORE_OPEN_FLAG_CREATE, &b_args[i].db[0]);
      couchstore_set_sync(b_args[i].db[0], binfo->sync_write);
    } else {
      first_db_idx = i / singledb_thread_num * singledb_thread_num;
      b_args[i].db = b_args[first_db_idx].db;

    }
#elif defined (__KVROCKS_BENCH) || defined(__BLOBFS_ROCKS_BENCH)
    b_args[i].db = db;
#elif defined(__KV_BENCH)
    /*
    if(i % singledb_thread_num == 0) {
      b_args[i].db = (Db**)malloc(sizeof(Db*));
      couchstore_open_db_kvs(dev_path[i / singledb_thread_num], &b_args[i].db[0], i / singledb_thread_num);
    } else {
      first_db_idx = i / singledb_thread_num * singledb_thread_num;
      b_args[i].db = b_args[first_db_idx].db;
      }
    */
    b_args[i].db = db;
#endif // end of kv
#if defined(__AS_BENCH)
    // TODO: open aerospike
    b_args[i].db = (Db**)malloc(sizeof(Db*));
    couchstore_open_db(curfile, COUCHSTORE_OPEN_FLAG_CREATE, &b_args[i].db[0]);
#endif
#if defined(__BLOBFS_ROCKS_BENCH)
    //b_args[i].blobfs_env = blobfs_env;
#endif

    db_idx = i / singledb_thread_num;
    td_idx = i % singledb_thread_num;
    nodeid = binfo->instances[db_idx].nodeid_perf;
    //nodeid = 0;
    pthread_attr_init(&attr[i]);
    b_args[i].socketid = nodeid;
    // initialize key/value pool when nodeid is valid value
    pool_setup(&info_key, b_args[i].keypool, nodeid);
    pool_setup(&info_value, b_args[i].valuepool, nodeid);

    int coreid[64] = { 0 };
    int core_count = 0;
    int ret = -1;
#ifdef __KV_BENCH
    if (binfo->kv_device_path[0] != '/') // udd mode
      ret = parse_coreid(binfo, coreid, 64, &core_count);
#endif
    if(binfo->instances[db_idx].coreids_bench[td_idx] != -1) {
      CPU_ZERO(&cpus);
      CPU_SET(binfo->instances[db_idx].coreids_bench[td_idx], &cpus);
      pthread_attr_setaffinity_np(&attr[i], sizeof(cpu_set_t), &cpus);
    } else if(nodeid != -1){
      CPU_ZERO(&cpus);
      if (ret) {
        for(j = 0; j < binfo->cpuinfo->num_cores_per_numanodes; j++){
          CPU_SET(binfo->cpuinfo->cpulist[nodeid][j], &cpus);
        }
      } else {
        CPU_SET(db_idx < core_count ? coreid[db_idx] : 0, &cpus);
      }
      pthread_attr_setaffinity_np(&attr[i], sizeof(cpu_set_t), &cpus);
    }else {
      // if no core & socket is set, use round robin to assign numa node
      CPU_ZERO(&cpus);
      nodeid = db_idx % binfo->cpuinfo->num_numanodes;
      if (ret) {
        for(j = 0; j < binfo->cpuinfo->num_cores_per_numanodes; j++){
          CPU_SET(binfo->cpuinfo->cpulist[nodeid][j], &cpus);
        }
      } else {
        CPU_SET(db_idx < core_count ? coreid[db_idx] : 0, &cpus);
      }

      pthread_attr_setaffinity_np(&attr[i], sizeof(cpu_set_t), &cpus);
    }

    // TODO: initialize keypool & valuepool per thread
  }

  bench_worker_ret = alca(void*, bench_threads);

  for(i = 0; i < bench_threads; ++i){
    b_args[i].tid = i;
    pthread_create(&bench_worker[i], &attr[i], bench_thread, (void*)&b_args[i]);
  }

  prev_op_count_read = prev_op_count_write = prev_op_count_delete = 0;
  gap = stopwatch_stop(&sw);
  if(binfo->pop_first)
    LOG_PRINT_TIME(gap, " sec elapsed\n");
  
  // timer for total elapsed time
  stopwatch_init(&sw);
  stopwatch_start(&sw);

  // timer for periodic stdout print
  stopwatch_init(&progress);
  stopwatch_start(&progress);

  if (binfo->warmup_secs) {
    warmingup = true;
    lprintf("\nwarming up\n");
    lprintf("time,ops_avg,ops_i,read_cnt,write_cnt,bytes_written\n");
  }

  if(run_ops_fp)
    fprintf(run_ops_fp, "time,ops_avg,ops_i,read_cnt,write_cnt,bytes_written\n");

  i = 0;
  while (i < (int)binfo->nbatches || binfo->nbatches == 0) {
    op_count_read = op_count_write = op_count_delete = 0;
    for(j = 0; j< bench_threads; j++){
      op_count_read += b_args[j].op_read.load();
      op_count_write += b_args[j].op_write.load();
      op_count_delete += b_args[j].op_delete.load();
    }
      
    //i = b_stat.batch_count_a.load();
    
    if (stopwatch_check_ms(&progress, print_term_ms)) {
      // for every 0.1 sec, print current status
      uint64_t cur_size = 0;
      int cpt_no;
      double elapsed_time;
      Db *temp_db;

      display_tick++;

      // reset stopwatch for the next period
      _gap = stopwatch_get_curtime(&progress);
      stopwatch_init(&progress);
      /*
      BDR_RNG_NEXTPAIR;
      spin_lock(&cur_compaction_lock);
      curfile_no = compaction_turn;
      if (display_tick % filesize_chk_term == 0) {
	      compaction_turn = (compaction_turn + 1) % binfo->nfiles;
      }
#ifdef __FDB_BENCH
      temp_db = info_handle[curfile_no];
#else
      temp_db = b_args[0].db[curfile_no];
#endif
      cpt_no = compaction_no[curfile_no] -
	      ((curfile_no == cur_compaction)?(1):(0));
      spin_unlock(&cur_compaction_lock);

#if defined __FDB_BENCH || defined __WT_BENCH
		  if (binfo->auto_compaction) {
		    // auto compaction
		    if (display_tick % filesize_chk_term == 0) {
		      couchstore_db_info(temp_db, dbinfo);
		      strcpy(curfile, dbinfo->filename);
		    }
		  } else {
		    // manual compaction
		    couchstore_db_info(temp_db, dbinfo);
		    sprintf(curfile, "%s%d.%d",
			    binfo->filename, (int)curfile_no, cpt_no);
		  }
#endif
      */
		  stopwatch_stop(&sw);
		  // overwrite spaces
		  printf("\r%s", spaces);
		  // move a line upward
		  printf("%c[1A%c[0C", 27, 27);
		  printf("\r%s\r", spaces);
		  
		  if (!warmingup && binfo->nbatches > 0) {
		    // batch count
		    printf("%5.1f %% (", i*100.0 / (binfo->nbatches-1));
		    gap = sw.elapsed;
		    PRINT_TIME(gap, " s, ");
		  } else if (warmingup || binfo->bench_secs > 0) {
		    // seconds
		    printf("(");
		    gap = sw.elapsed;
		    PRINT_TIME(gap, " s / ");
		    if (warmingup) {
		      printf("%d s, ", (int)binfo->warmup_secs);
		    } else {
		      printf("%d s, ", (int)binfo->bench_secs);
		    }
		  } else {
		    // # operations
		    printf("%5.1f %% (",
			   binfo->nops * binfo->nfiles - 1 == 0 ?
			   (op_count_read+op_count_write + op_count_delete)*100.0 :
			   (op_count_read+op_count_write + op_count_delete)*100.0 /
			   (binfo->nops * binfo->nfiles -1));
		    gap = sw.elapsed;
		    PRINT_TIME(gap, " s, ");
		  }

		  elapsed_time = gap.tv_sec + (double)gap.tv_usec / 1000000.0;
		  // average throughput
		  printf("%8.2f ops/s, ",
			 (double)(op_count_read + op_count_write + op_count_delete) / elapsed_time);
		  // instant throughput
		  printf("%8.2f ops/s)",
			 (double)((op_count_read + op_count_write + op_count_delete) -
				  (prev_op_count_read + prev_op_count_write +
				   prev_op_count_delete)) /
			 (_gap.tv_sec + (double)_gap.tv_usec / 1000000.0));

#if defined (__KV_BENCH) || defined(__KVROCKS_BENCH) || defined (__AS_BENCH)
		  // TBD: get KVS bytes written
#else
		  if (display_tick % filesize_chk_term == 0) {
		    written_prev = written_final;
		    written_final = print_proc_io_stat(cmd, 0);
		  }
#endif

		  tmp = (warmingup == true) ? log_fp : run_ops_fp;
		  //if (log_fp) {
		  if(tmp){
		    // 1. elapsed time
		    // 2. average throughput
		    // 3. instant throughput
		    // 4. # reads
		    // 5. # writes
		    // 6. # bytes written by host
		    fprintf(tmp,
			                            "%d.%01d,%.2f,%.2f,"
			    "%" _F64",%" _F64 ",%" _F64 "\n",
			    (int)gap.tv_sec, (int)gap.tv_usec / 100000,
			    (double)(op_count_read + op_count_write + op_count_delete) /
			    (elapsed_time),
			    (double)((op_count_read + op_count_write + op_count_delete) -
				     (prev_op_count_read + prev_op_count_write +
				      prev_op_count_delete)) /
			    (_gap.tv_sec + (double)_gap.tv_usec / 1000000.0),
			    op_count_read, op_count_write,
			    written_final - written_init);
		  }

		  printf("\n");
#if defined(__FDB_BENCH) || defined(__COUCH_BENCH)
		  if (display_tick % filesize_chk_term == 0) {
		    cur_size = dbinfo->file_size;
		    if (binfo->auto_compaction) { // auto
		      print_filesize_approx(cur_size, fsize1);
		    } else { // manual
		      char curfile_temp[256];
		      uint64_t cur_size_temp;
		      sprintf(curfile_temp, "%s%d.%d",
			      binfo->filename, (int)curfile_no,
			      compaction_no[curfile_no]);
		      cur_size_temp = get_filesize(curfile_temp);
		      print_filesize_approx(cur_size_temp, fsize1);
		    }
		    print_filesize_approx(dbinfo->space_used, fsize2);
		  }

		  // actual file size / live data size
		  printf("(%s / %s) ", fsize1, fsize2);
#endif
		  
#ifdef __PRINT_IOSTAT // only for linux
		  uint64_t w_per_doc;
		  uint64_t inst_written_doc;

		  // data written
		  printf("(%s, ", print_filesize_approx(written_final - written_init, cmd));
		  // avg write throughput
		  printf("%s/s ", print_filesize_approx((written_final - written_init) /
							elapsed_time, cmd));
		  // avg write amplification
		  if (written_final - written_init > 0) {
		    w_per_doc = (double)(written_final - written_init) / op_count_write;
		  } else {
		    w_per_doc = 0;
		  }
		  printf("%.1f x, ", (double)w_per_doc / avg_docsize);

		  // instant write throughput
		  printf("%s/s ", print_filesize_approx(
							(written_final - written_prev) /
							(print_term_ms * filesize_chk_term / 1000.0), cmd));
		  // instant write amplification
		  inst_written_doc = (op_count_write - prev_op_count_write) *
		    filesize_chk_term;
		  if (inst_written_doc) {
		    w_per_doc = (double)(written_final - written_prev) / inst_written_doc;
		  } else {
		    w_per_doc = 0;
		  }
		  printf("%.1f x)", (double)w_per_doc / avg_docsize);
#endif
		  fflush(stdout);

		  prev_op_count_read = op_count_read;
		  prev_op_count_write = op_count_write;
		  prev_op_count_delete = op_count_delete;

		  stopwatch_start(&sw);

		  if (binfo->bench_secs && !warmingup &&
		      (size_t)sw.elapsed.tv_sec >= binfo->bench_secs)
		    break;

		  if ((size_t)sw.elapsed.tv_sec >= binfo->warmup_secs){
		    if (warmingup) {
		      // end of warming up .. initialize stats
		      stopwatch_init_start(&sw);
		      prev_op_count_read = op_count_read = 0;
		      prev_op_count_write = op_count_write = 0;
		      prev_op_count_delete = op_count_delete = 0;
		      written_init = written_final;
		      /*
		      // atomic 
		      b_stat.op_read = 0;
		      b_stat.op_write = 0;
		      b_stat.op_delete = 0;
		      b_stat.batch_count_a = 0;
		      
		      spin_lock(&l_read.lock);
		      l_read.cursor = 0;
		      l_read.nsamples = 0;
		      spin_unlock(&l_read.lock);
		      spin_lock(&l_write.lock);
		      l_write.cursor = 0;
		      l_write.nsamples = 0;
		      spin_unlock(&l_write.lock);
		      spin_lock(&l_delete.lock);
		      l_delete.cursor = 0;
		      l_delete.nsamples = 0;
		      spin_unlock(&l_delete.lock);
		      */
		      for (j=0; j<bench_threads; j++) {
      			b_args[j].op_read = b_args[j].op_write = b_args[j].op_delete = b_args[j].op_iter_key = 0;
      			b_args[j].l_read->nsamples = b_args[j].l_write->nsamples
      			  = b_args[j].l_delete->nsamples = 0;
      			b_args[j].l_read->cursor = b_args[j].l_write->cursor
      			  = b_args[j].l_delete->cursor = 0;
		      }
		      warmingup = false;
		      lprintf("\nevaluation\n");
		      lprintf("time,ops_avg,ops_i,read_cnt,write_cnt,bytes_written\n");
		      //if(run_ops_fp)
		      //fprintf(run_ops_fp, "time,ops_avg,ops_i,read_cnt,write_cnt,bytes_written\n");
		    }
		    
		    if(!startlog) {
		      startlog = true;
		    }
		  }

		  stopwatch_start(&progress);
    } else {
      // sleep 0.1 sec
      stopwatch_start(&progress);
      usleep(print_term_ms * 1000);
    }

    if (binfo->nops && !warmingup &&
	    (op_count_read + op_count_write + op_count_delete) >= binfo->nops * binfo->nfiles)
      break;

    if (got_signal) {
      break;
    }
    /*
    if (binfo->with_iterator == 2) { // iterator alone
      break;
    }
    */
  }

  // terminate all bench_worker threads
  for (i=0;i<bench_threads;++i){
    b_args[i].terminate_signal = 1;
  }

  for (i=0;i<bench_threads;++i){
    thread_join(bench_worker[i], &bench_worker_ret[i]);
  }
  
#if defined (__KV_BENCH) || defined (__AS_BENCH)

  // each thread will check outstanding IOs individually
#elif defined (__ROCKS_BENCH) || defined (__LEVEL_BENCH) //|| defined(__KVDB_BENCH) //|| defined(__BLOBFS_ROCKS_BENCH)
  _wait_leveldb_compaction(binfo, db);
#endif

  lprintf("\n");
  stopwatch_stop(&sw);
  gap = sw.elapsed;
  LOG_PRINT_TIME(gap, " sec elapsed\n");
  gap_double = gap.tv_sec + (double)gap.tv_usec / 1000000.0;

  op_count_read = b_stat.op_read.load();
  op_count_write = b_stat.op_write.load();
  op_count_delete = b_stat.op_delete.load();
  op_count_iter_key = b_stat.op_iter_key.load();
  if (op_count_read) {
    lprintf("%" _F64 " reads (%.2f ops/sec, %.2f us/read)\n",
	    op_count_read,
	    (double)op_count_read / gap_double,
	    gap_double * 1000000 / op_count_read);
  }
  if (op_count_write) {
    lprintf("%" _F64 " writes (%.2f ops/sec, %.2f us/write)\n",
	    op_count_write,
	    (double)op_count_write / gap_double,
	    gap_double * 1000000 / op_count_write);
  }
  if (op_count_delete) {
    lprintf("%" _F64 " deletes (%.2f ops/sec, %.2f us/delete)\n",
	    op_count_delete,
	    (double)op_count_delete / gap_double,
	    gap_double * 1000000 / op_count_delete);
  }
  if (binfo->with_iterator != 2) {
    lprintf("total %" _F64 " operations performed\n",
	  op_count_read + op_count_write + op_count_delete);

    lprintf("Throughput(Benchmark) %.2f ops/sec\n", (double)(op_count_read + op_count_write + op_count_delete) / gap_double);
  }
  if(op_count_iter_key > 0) {
    lprintf("Throughput(Iterator)  %.2f keys/sec; total %ld keys\n", (double)(op_count_iter_key) / gap_double, op_count_iter_key);
  }
  
  if(op_count_read + op_count_write + op_count_delete > 0) {
    lprintf("average latency %f\n", gap_double * 1000000 /
	    ( op_count_read + op_count_write + op_count_delete));
  }
#if defined(__FDB_BENCH) || defined(__COUCH_BENCH)
  if (!binfo->auto_compaction) {
    // manual compaction
    lprintf("compaction : occurred %d time%s, ",
	    total_compaction, (total_compaction>1)?("s"):(""));
    LOG_PRINT_TIME(sw_compaction.elapsed, " sec elapsed\n");
  }
#endif   

#if !defined __KV_BENCH && !defined __AS_BENCH
  written_final = print_proc_io_stat(cmd, 1);
#endif

#if defined(__PRINT_IOSTAT)
  {
    uint64_t written = written_final - written_init;
    uint64_t w_per_doc = (double)written / op_count_write;
    double waf_d, waf_a;

    if (op_count_write) {
      lprintf("total %" _F64 " bytes (%s) written during benchmark\n",
	      written,
	      print_filesize_approx((written_final - written_init),
				    bodybuf));
      lprintf("average disk write throughput: %.2f MB/s\n",
	      (double)written / (gap.tv_sec*1000000 + gap.tv_usec) *
	      1000000 / (1024*1024));
      waf_a = (double)w_per_doc / avg_docsize;
      lprintf("%s written per doc update (%.1f x write amplification)\n",
	      print_filesize_approx(w_per_doc, bodybuf),
	      waf_a);
    }
  }
#endif

  // TODO: update latency rate
  if(binfo->latency_rate && binfo->with_iterator != 2) {
    struct latency_stat w_stat;
    struct latency_stat r_stat;
    struct latency_stat d_stat;
    uint32_t *t1, *t2, *t3;
    memset(&w_stat, 0, sizeof(struct latency_stat));
    memset(&r_stat, 0, sizeof(struct latency_stat));
    memset(&d_stat, 0, sizeof(struct latency_stat));

    w_stat.samples = (uint32_t*)malloc(sizeof(uint32_t) *
				       binfo->latency_max * bench_threads);
    r_stat.samples = (uint32_t*)malloc(sizeof(uint32_t) *
				       binfo->latency_max * bench_threads);
    d_stat.samples = (uint32_t*)malloc(sizeof(uint32_t) *
				       binfo->latency_max * bench_threads);
    t1 = w_stat.samples;
    t2 = r_stat.samples;
    t3 = d_stat.samples;

    for(i = 0; i < bench_threads; i++){
      w_stat.nsamples += b_args[i].l_write->nsamples;
      r_stat.nsamples += b_args[i].l_read->nsamples;
      d_stat.nsamples += b_args[i].l_delete->nsamples;

      memcpy(t1, b_args[i].l_write->samples, sizeof(uint32_t) *
	     b_args[i].l_write->nsamples);
      memcpy(t2, b_args[i].l_read->samples, sizeof(uint32_t) *
	     b_args[i].l_read->nsamples);
      memcpy(t3, b_args[i].l_delete->samples, sizeof(uint32_t) *
	     b_args[i].l_delete->nsamples);

      t1 += b_args[i].l_write->nsamples;
      t2 += b_args[i].l_read->nsamples;
      t3 += b_args[i].l_delete->nsamples;
      free(b_args[i].l_write->samples);
      free(b_args[i].l_read->samples);
      free(b_args[i].l_delete->samples);
    }

    _print_percentile(binfo, (w_stat.nsamples == 0? NULL: &w_stat),
		      (r_stat.nsamples == 0? NULL : &r_stat),
		      (d_stat.nsamples == 0? NULL : &d_stat), "us", 2);
    free(w_stat.samples);
    free(r_stat.samples);
    free(d_stat.samples);    
  }

  lprintf("\n");
  
  keygen_free(&binfo->keygen);
  if (binfo->keyfile) {
    keyloader_free(&binfo->kl);
  }
  if (binfo->batch_dist.type == RND_ZIPFIAN) {
    zipf_rnd_free(&zipf);
  }

#ifdef __FDB_BENCH
  // print ForestDB's own block cache info (internal function call)
  //bcache_print_items();
#endif

  printf("waiting for termination of DB module..\n");
#if defined(__FDB_BENCH) || defined(__COUCH_BENCH) || defined(__WT_BENCH)
  for (i=0;i<bench_threads;++i){
    for (j=0;j<(int)binfo->nfiles;++j){
      couchstore_close_db(b_args[i].db[j]);
#ifdef __FDB_BENCH
      if (i==0) {
	      couchstore_close_db(info_handle[j]);
      }
#endif
    }
    free(b_args[i].db);
  }
#elif defined(__KV_BENCH) || defined(__KVROCKS_BENCH) || defined __BLOBFS_ROCKS_BENCH
  for (j=0;j<(int)binfo->nfiles;++j){
    couchstore_close_db(db[j]);
  }
#elif defined (__LEVEL_BENCH) //|| defined __BLOBFS_ROCKS_BENCH
  //for (j=0;j<(int)binfo->nfiles;++j){
  for (j = 0; j < bench_threads; j+= singledb_thread_num){
    couchstore_close_db(b_args[j].db[0]);
  }
#if !defined(__BLOBFS_ROCKS_BENCH)
  free(b_args[0].db);
#endif

#elif defined (__ROCKS_BENCH) || defined(__KVDB_BENCH)
  for (j = 0; j < bench_threads; j+= singledb_thread_num){
    couchstore_close_db(b_args[j].db[0]);
    free(b_args[j].db);
    fprintf(stdout, "close & free db in thread %d\n", j);
  }
#elif defined (__AS_BENCH)
  for( j = 0; j < bench_threads; j++){
    printf("close db %d\n", j);
    couchstore_close_db(b_args[j].db[0]);
  }
#endif

#if defined(__KV_BENCH) ||  defined(__BLOBFS_ROCKS_BENCH)
  //for (j = 0; j< binfo->nfiles; j++)
  //couchstore_close_device(dev_id[j]);
  couchstore_exit_env();
#endif
#if defined (__AS_BENCH)
  couchstore_close_device(binfo->ndocs);
#endif
#if defined(__WT_BENCH) || defined(__FDB_BENCH)
  couchstore_close_conn();
#endif
  
  lprintf("\n");
  stopwatch_stop(&sw);
  gap = sw.elapsed;
  LOG_PRINT_TIME(gap, " sec elapsed\n");

  free(dbinfo);

  _bench_result_print(&result);
  _bench_result_free(&result);
  /*
  spin_destroy(&b_stat.lock);
  spin_destroy(&l_read.lock);
  spin_destroy(&l_write.lock);

  free(l_read.samples);
  free(l_write.samples);
  */
  memleak_end();
}

void _print_benchinfo(struct bench_info *binfo)
{
    char tempstr[256];

    lprintf("\n === benchmark configuration ===\n");
    lprintf("DB module: %s\n", binfo->dbname);

    lprintf("random seed: %d\n", (int)rnd_seed);
#if !defined(__KV_BENCH)
    if (strcmp(binfo->init_filename, binfo->filename)) {
        lprintf("initial filename: %s#\n", binfo->init_filename);
    }
    lprintf("filename: %s#", binfo->filename);
#else
    lprintf("filename: %s# ", binfo->kv_device_path)
#endif
    if (binfo->initialize) {
        lprintf(" (initialize)\n");
    } else {
        lprintf(" (use existing DB file)\n");
    }

    lprintf("# documents (i.e. working set size): %ld\n", (long int)binfo->ndocs);

    if (binfo->nfiles > 1) {
        lprintf("# files: %d\n", (int)binfo->nfiles);
    }

    lprintf("# threads: ");
    lprintf("reader %d", (int)binfo->nreaders);
    /*lprintf("reader %d, iterator %d", (int)binfo->nreaders,
                                      (int)binfo->niterators);*/
    //if (binfo->write_prob > 100) {
    if (binfo->ratio[1] > 100) {
        if (binfo->reader_ops) {
            lprintf(" (%d ops/sec), ", (int)binfo->reader_ops);
        } else {
            lprintf(" (max), ");
        }
    } else {
        lprintf(", ");
    }
    lprintf("writer %d, deleter %d", (int)binfo->nwriters, (int)binfo->ndeleters);
    //if (binfo->write_prob > 100) {
    if (binfo->ratio[1] > 100) {
        if (binfo->writer_ops) {
            lprintf(" (%d ops/sec)\n", (int)binfo->writer_ops);
        } else {
            lprintf(" (max)\n");
        }
    } else {
        lprintf("\n");
    }
    if (binfo->disjoint_write) {
        lprintf("enabled disjoint write among %d writers over %d files\n",
                (int)binfo->nwriters, (int)binfo->nfiles);
    }

    lprintf("# auto-compaction threads: %d\n", binfo->auto_compaction_threads);

    lprintf("block cache size: %s\n",
            print_filesize_approx(binfo->cache_size, tempstr));
#if defined(__LEVEL_BENCH) || defined(__ROCKS_BENCH) || defined(__BLOBFS_ROCKS_BENCH) || defined(__KVDB_BENCH)
    lprintf("WBS size: %s (init), ",
        print_filesize_approx(binfo->wbs_init, tempstr));
    lprintf("%s (bench)\n",
        print_filesize_approx(binfo->wbs_bench, tempstr));
    if (binfo->bloom_bpk) {
        lprintf("bloom filter enabled (%d bits per key)\n", (int)binfo->bloom_bpk);
    }
#endif // __LEVEL_BENCH || __ROCKS_BENCH

#if defined(__ROCKS_BENCH) || defined(__BLOBFS_ROCKS_BENCH) || defined(__KVDB_BENCH)
    lprintf("compaction style: ");
    switch(binfo->compaction_style) {
    case 0:
        lprintf("level (default)\n");
        break;
    case 1:
        lprintf("universal\n");
        break;
    case 2:
        lprintf("FIFO\n");
        break;
    }
#endif

#if defined(__FDB_BENCH)
    lprintf("WAL size: %" _F64"\n", binfo->fdb_wal);
    lprintf("indexing: %s\n", (binfo->fdb_type==0)?"hb+trie":"b-tree");
#endif // __FDB_BENCH

#if defined(__WT_BENCH)
    lprintf("indexing: %s\n", (binfo->wt_type==0)?"b-tree":"lsm-tree");
#endif // __WT_BENCH

    if (binfo->compression) {
        lprintf("compression is enabled (compressibility %d %%)\n",
                binfo->compressibility);
    }

    if (binfo->keyfile) {
        // load key from file
        lprintf("key data: %s (avg length: %d)\n",
                binfo->keyfile, (int)binfo->avg_keylen);
    } else {
      if (binfo->keylen.type == RND_FIXED) {
	      lprintf("key length: %s(%d) / ",
		      "Fixed size",
		      (int)binfo->keylen.a);
      }else{
        // random keygen
        lprintf("key length: %s(%d,%d) / ",
                (binfo->keylen.type == RND_NORMAL)?"Norm":"Uniform",
                (int)binfo->keylen.a, (int)binfo->keylen.b);
      }
    }
    if(binfo->bodylen.type == RND_FIXED){
      lprintf("body length: %s(%d)\n",
	      "Fixed size",
	      (int)binfo->bodylen.a);
    } else if (binfo->bodylen.type == RND_RATIO) {
      int ratio_count = 0;
      lprintf("body length: Ratio(");
      int i = 0;
      while (i < 5) {
        lprintf("%d[%d%%]", binfo->value_size[i], binfo->value_size_ratio[i]);
        ratio_count += binfo->value_size_ratio[i];
        if (ratio_count == 100)
          break;
        if (i != 4)
          lprintf(",");
        i++;
      }
      lprintf(")\n");
    } else {
      lprintf("body length: %s(%d,%d)\n",
	      (binfo->bodylen.type == RND_NORMAL)?"Norm":"Uniform",
	      (int)binfo->bodylen.a, (int)binfo->bodylen.b);
    }
    lprintf("batch distribution: ");
    if (binfo->batch_dist.type == RND_UNIFORM) {
        lprintf("Uniform\n");
    }else{
        lprintf("Zipfian (s=%.2f, group: %d documents)\n",
                (double)binfo->batch_dist.a/100.0, (int)binfo->batch_dist.b);
    }

    if (binfo->nbatches > 0) {
        lprintf("# batches for benchmark: %lu\n",
            (unsigned long)binfo->nbatches);
    }
    if (binfo->nops > 0){
        lprintf("# operations for benchmark: %lu\n",
                (unsigned long)binfo->nops);
    }
    if (binfo->bench_secs > 0){
        lprintf("benchmark duration: %lu seconds\n",
                (unsigned long)binfo->bench_secs);
    }

    lprintf("read batch size: point %s(%d,%d), range %s(%d,%d)\n",
            (binfo->rbatchsize.type == RND_NORMAL)?"Norm":"Uniform",
            (int)binfo->rbatchsize.a, (int)binfo->rbatchsize.b,
            (binfo->ibatchsize.type == RND_NORMAL)?"Norm":"Uniform",
            (int)binfo->ibatchsize.a, (int)binfo->ibatchsize.b);
    lprintf("write batch size: %s(%d,%d)\n",
            (binfo->wbatchsize.type == RND_NORMAL)?"Norm":"Uniform",
            (int)binfo->wbatchsize.a, (int)binfo->wbatchsize.b);
    lprintf("inside batch distribution: %s",
            (binfo->op_dist.type == RND_NORMAL)?"Norm":"Uniform");
    lprintf(" (-%d ~ +%d, total %d)\n",
            (int)binfo->batchrange , (int)binfo->batchrange,
            (int)binfo->batchrange*2);
    //if (binfo->write_prob <= 100) {
    if (binfo->ratio[1] + binfo->ratio[2] <= 100) {
        lprintf("write ratio: %d %%", (int)binfo->ratio[1]);
    } else {
        lprintf("write ratio: max capacity");
    }
    lprintf(" (%s)\n", ((binfo->sync_write)?("synchronous"):("asynchronous")));
    lprintf("insertion order: %s\n", ((binfo->seq_fill)?("sequential fill"):("random fill")));

#if defined(__FDB_BENCH)
    lprintf("compaction threshold: %d %% "
            "(period: %d seconds, %s)\n",
            (int)binfo->compact_thres, (int)binfo->compact_period,
            ((binfo->auto_compaction)?("auto"):("manual")));
    if (binfo->block_reuse_thres) {
        lprintf("block reuse threshold: %d %%\n", (int)binfo->block_reuse_thres);
    }
#endif
#if defined(__COUCH_BENCH)
    lprintf("compaction threshold: %d %%\n", (int)binfo->compact_thres);
#endif
#if defined(__WT_BENCH)
    lprintf("checkpoint period: %d seconds\n", (int)binfo->compact_period);
#endif
}

void _set_keygen(struct bench_info *binfo)
{
    size_t i, level = binfo->nlevel+1;
    int avg_keylen, avg_prefixlen;
    struct rndinfo rnd_len[level], rnd_dist[level];
    struct keygen_option opt;

    if (binfo->keylen.type == RND_NORMAL || binfo->keylen.type == RND_FIXED) {
        avg_keylen = binfo->keylen.a;
    } else {
        avg_keylen = (binfo->keylen.a + binfo->keylen.b) / 2;
    }
    if (binfo->prefixlen.type == RND_NORMAL) {
        avg_prefixlen = binfo->prefixlen.a;
    } else {
        avg_prefixlen = (binfo->prefixlen.a + binfo->prefixlen.b) / 2;
    }

    for (i=0;i<binfo->nlevel+1; ++i) {
        if (i<binfo->nlevel) {
            rnd_len[i] = binfo->prefixlen;
            rnd_dist[i].type = RND_UNIFORM;
            rnd_dist[i].a = 0;
            rnd_dist[i].b = binfo->nprefixes;
        } else {
            // right most (last) prefix
	  if(binfo->keylen.type == RND_NORMAL) {
	    rnd_len[i].type = RND_NORMAL;
            rnd_len[i].a = avg_keylen - avg_prefixlen * binfo->nlevel;
            rnd_len[i].b = binfo->keylen.b;
            rnd_dist[i].type = RND_UNIFORM;
            rnd_dist[i].a = 0;
            rnd_dist[i].b = 0xfffffffffffffff;
	  }else {
	    rnd_len[i].type = RND_UNIFORM;
	    rnd_len[i].a = binfo->keylen.a - avg_prefixlen * binfo->nlevel;
	    rnd_len[i].b = binfo->keylen.b;
	    rnd_dist[i].type = RND_UNIFORM;
	    rnd_dist[i].a = binfo->keylen.a - avg_prefixlen * binfo->nlevel;
	    rnd_dist[i].b = binfo->keylen.b;
	  }
        }
    }
    opt.abt_only = 1;
    opt.delimiter = 1;
    keygen_init(&binfo->keygen, level, rnd_len, rnd_dist, &opt);
}

void _set_keyloader(struct bench_info *binfo)
{
    int ret;
    struct keyloader_option option;

    option.max_nkeys = binfo->ndocs;
    ret = keyloader_init(&binfo->kl, binfo->keyfile, &option);
    if (ret < 0) {
        printf("error occured during loading file %s\n", binfo->keyfile);
        exit(0);
    }
    binfo->ndocs = keyloader_get_nkeys(&binfo->kl);
    binfo->avg_keylen = keyloader_get_avg_keylen(&binfo->kl);
}


void init_cpu_aff(struct bench_info *binfo){
  int i, j, line_num = 0, core_count = 0;
  int nodeid = 0, coreid = 0, insid_load = 0,insid_perf = 0;
  int cur_load = 0, cur_bench = 0, prev_load_insid = -1, prev_bench_insid = -1;
  static dictionary *cfg;
  FILE *fp;
  char line[128];

  // initialize population phase cpu info
  fp = fopen("cpu.txt", "r");
  if(fp == NULL) {
    fprintf(stderr, "please genrerate cpu core info file with \"-c\" option\n");
    exit(0);
  }

  int total_bench_threads = binfo->nreaders + binfo->niterators + binfo->nwriters + binfo->ndeleters;
  binfo->instances = (instance_info_t *)malloc(sizeof(instance_info_t) * binfo->nfiles);
  for(i = 0; i < binfo->nfiles; i++) {
    binfo->instances[i].coreids_pop = (int*)malloc(sizeof(int) * binfo->pop_nthreads);
    for (j = 0; j<binfo->pop_nthreads; j++)
      binfo->instances[i].coreids_pop[j] = -1;

    binfo->instances[i].coreids_bench = (int*)malloc(sizeof(int) * total_bench_threads);
    for(j = 0; j< total_bench_threads; j++)
      binfo->instances[i].coreids_bench[j] = -1;
    binfo->instances[i].nodeid_load = -1;
    binfo->instances[i].nodeid_perf = -1;
  }

  binfo->cpuinfo->cpulist = (int **)calloc(1, sizeof(int *) * binfo->cpuinfo->num_numanodes);
  for(i = 0; i < binfo->cpuinfo->num_numanodes; i++) {
    binfo->cpuinfo->cpulist[i] = (int*)calloc(1, sizeof(int) * binfo->cpuinfo->num_cores_per_numanodes);
  }
  
  while(fgets(line, sizeof line, fp)!= NULL ){
    if(line_num == 0) {line_num++; continue;}
    sscanf(line, "%d,%d,%d,%d", &nodeid, &coreid, &insid_load, &insid_perf);
    if (nodeid >= binfo->cpuinfo->num_numanodes ||
        (coreid / binfo->cpuinfo->num_numanodes) >= binfo->cpuinfo->num_cores_per_numanodes) {
        fprintf(stderr, "numa node id or core id is invalid, please confirm cpu.txt\n");
        for (i = 0; i < binfo->cpuinfo->num_numanodes; i++)
            free(binfo->cpuinfo->cpulist[i]);
        free(binfo->cpuinfo->cpulist);
        if (fp) fclose(fp);
        exit(1);
    }
    binfo->cpuinfo->cpulist[nodeid][core_count++ % binfo->cpuinfo->num_cores_per_numanodes] = coreid;
    if(insid_load >=0 && insid_load < binfo->nfiles) {
      if(prev_load_insid != insid_load) {
      	if(prev_load_insid != -1) {
      	  while(cur_load < binfo->pop_nthreads)
      	    binfo->instances[prev_load_insid].coreids_pop[cur_load++] = -1;
      	}
      	cur_load = 0;
      	prev_load_insid = insid_load;
      	binfo->instances[insid_load].nodeid_load = nodeid;
      }
      if(cur_load < binfo->pop_nthreads)
	      binfo->instances[insid_load].coreids_pop[cur_load++] = coreid;
      }

    if(insid_perf >=0 && insid_perf < binfo->nfiles) {
      if(prev_bench_insid != insid_perf) {
      	if(prev_bench_insid != -1) {
      	  while(cur_bench < total_bench_threads)
      	    binfo->instances[prev_bench_insid].coreids_bench[cur_bench++] = -1;
      	}
      	cur_bench = 0;
      	prev_bench_insid = insid_perf;
      	binfo->instances[insid_perf].nodeid_perf = nodeid;
      }
      if(cur_bench < total_bench_threads)
	      binfo->instances[insid_perf].coreids_bench[cur_bench++] = coreid;
    }
    line_num++;
  }

  for(i=0;i<binfo->cpuinfo->num_numanodes;i++){
    printf("node %d: ", i);
    for(j=0;j< binfo->cpuinfo->num_cores_per_numanodes; j++)
      printf("%d ", binfo->cpuinfo->cpulist[i][j]);
    printf("\n");
  }

  for(i = 0; i < binfo->nfiles; i++) {
    get_nvme_numa_node(binfo->device_name[i], &binfo->instances[i]);
    while (cur_load < binfo->pop_nthreads){
      // the rest pop thread will use all cores
      binfo->instances[i].coreids_pop[cur_load++] = -1;
    }
    while(cur_bench < total_bench_threads) {
      // the rest bench thread will use all cores
      binfo->instances[i].coreids_bench[cur_bench++] = -1;
    }
  }

  for(i = 0;i < binfo->nfiles; i++){
    printf("pop -- dev %s, numaid %d, core: ", binfo->device_name[i], binfo->instances[i].nodeid_load);
    for(j = 0; j < binfo->pop_nthreads; j++)
      printf("%d ", binfo->instances[i].coreids_pop[j]);
    printf("\n");
  }
  for(i = 0;i < binfo->nfiles; i++){
    printf("bench --- dev %s, numaid %d, core: ", binfo->device_name[i], binfo->instances[i].nodeid_perf);
    for(j = 0; j < total_bench_threads; j++)
      printf("%d ", binfo->instances[i].coreids_bench[j]);
    printf("\n");
  }
  
  if(fp) fclose(fp);
}

struct bench_info get_benchinfo(char* bench_config_filename, int config_only)
{
    int i, j;
    static dictionary *cfg;
    char *str;
    struct bench_info binfo;
    char *pt;
    memset(&binfo, 0x0, sizeof(binfo));
    cfg = iniparser_new(bench_config_filename);

    if(config_only){
      cpu_info *cpuinfo = (cpu_info *)malloc(sizeof(cpu_info));
      memset(cpuinfo, 0, sizeof(cpu_info));
      get_cpuinfo(cpuinfo, 1);
      printf("total %d nodes, %d cores per node\n", cpuinfo->num_numanodes, cpuinfo->num_cores_per_numanodes);
      free(cpuinfo);
      iniparser_free(cfg);
      exit(0);
    } else {
      binfo.cpuinfo = (cpu_info *)malloc(sizeof(cpu_info));
      memset(binfo.cpuinfo, 0, sizeof(cpu_info));
      get_cpuinfo(binfo.cpuinfo, 0);

    }
    
    char *dbname = (char*)malloc(64);
    char *filename = (char*)malloc(256);
    char *init_filename = (char*)malloc(256);
    char *log_filename = (char*)malloc(256);
    char *device_path = (char*)malloc(256);
    char *kv_device_path = (char*)malloc(1024);
    char *kv_emul_configfile = (char*)malloc(1024);
    char *name_space = (char*)malloc(256);
    char *spdk_conf_file = (char*)malloc(256);
    //char *spdk_bdev = (char*)malloc(256);
    char *core_ids = (char*)malloc(256);
    char *cq_thread_ids = (char*)malloc(256);
    char dbname_postfix[64];
    size_t ncores;
#if defined(WIN32) || defined(_WIN32)
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    ncores = (size_t)sysinfo.dwNumberOfProcessors;
#else
    ncores = (size_t)sysconf(_SC_NPROCESSORS_ONLN);
#endif

#ifdef __FDB_BENCH
    sprintf(dbname, "ForestDB");
    sprintf(dbname_postfix, "fdb");
#elif __COUCH_BENCH
    sprintf(dbname, "Couchstore");
    sprintf(dbname_postfix, "couch");
#elif __LEVEL_BENCH
    sprintf(dbname, "LevelDB");
    sprintf(dbname_postfix, "level");
#elif __ROCKS_BENCH
    sprintf(dbname, "RocksDB");
    sprintf(dbname_postfix, "rocks");
#elif __BLOBFS_ROCKS_BENCH
    sprintf(dbname, "BlobFSRocksDB");
    sprintf(dbname_postfix, "blobfs_rocks");
#elif __WT_BENCH
    sprintf(dbname, "WiredTiger");
    sprintf(dbname_postfix, "wt");
#elif __KV_BENCH
    sprintf(dbname, "KVS");
    sprintf(dbname_postfix, "kvs");
#elif __KVDB_BENCH
    sprintf(dbname, "KVDB");
    sprintf(dbname_postfix, "kvdb");
#elif __KVROCKS_BENCH
    sprintf(dbname, "KVROCKS");
    sprintf(dbname_postfix, "kvrocks");
#elif __AS_BENCH
    sprintf(dbname, "Aerospike");
    sprintf(dbname_postfix, "as");
#else
    sprintf(dbname, "unknown");
    sprintf(dbname_postfix, "unknown");
#endif
    
    //memset(&binfo, 0x0, sizeof(binfo));
    binfo.dbname = dbname;
    binfo.filename = filename;
    binfo.init_filename = init_filename;
    binfo.log_filename = log_filename;
    binfo.device_path = device_path;
    binfo.kv_device_path = kv_device_path;
    binfo.kv_emul_configfile = kv_emul_configfile;
    binfo.core_ids = core_ids;
    binfo.cq_thread_ids = cq_thread_ids;
    binfo.spdk_conf_file = spdk_conf_file;
    //binfo.spdk_bdev = spdk_bdev;
    binfo.name_space = name_space;
    
    binfo.ndocs = iniparser_getint(cfg, (char*)"document:ndocs", 10000);

    str = iniparser_getstring(cfg, (char*)"document:key_file", (char*)"");
    if (strcmp(str, "")) {
        binfo.keyfile = (char*)malloc(256);
        strcpy(binfo.keyfile, str);
        _set_keyloader(&binfo);
    } else {
        binfo.keyfile = NULL;
    }
    binfo.amp_factor = iniparser_getdouble(cfg, (char*)"document:amp_factor", 1);
    
    pool_info_t info;
    str = iniparser_getstring(cfg, (char*)"system:allocator", (char*)"default");
    //#if !defined __KV_BENCH
    if (str[0] == 'N' || str[0] == 'n') {
      binfo.allocate_mem = &allocate_mem_numa;
      binfo.free_mem = &free_mem_numa;
      binfo.allocatortype = info.allocatortype = NUMA_ALLOCATOR;
    } else if (str[0] == 'p' || str[0] == 'P'){
      binfo.allocate_mem = &allocate_mem_posix;
      binfo.free_mem = &free_mem_posix;
      binfo.allocatortype = info.allocatortype = POSIX_ALLOCATOR;
    } else if (str[0] == 's' || str[0] == 'S') {
#if defined __KV_BENCH
      binfo.allocate_mem = &allocate_mem_spdk;
      binfo.free_mem = &free_mem_spdk;
      binfo.allocatortype = info.allocatortype = SPDK_ALLOCATOR;
#else
      fprintf(stdout, "No SPDK allocator available, please use Posix or Numa\n");
      iniparser_free(cfg);
      exit(0);
#endif
    } else {
      // default: regular malloc, no pool
      binfo.allocatortype = info.allocatortype = NO_POOL;
    }

    binfo.kp_numunits = iniparser_getint(cfg, (char*)"system:key_pool_size", 128);
    binfo.kp_unitsize = iniparser_getint(cfg, (char*)"system:key_pool_unit", 16);
    binfo.kp_alignment = iniparser_getint(cfg, (char*)"system:key_pool_alignment", 4096);
    if(binfo.kp_alignment <= 0) {
      fprintf(stderr,
      "ERROR: system:key_pool_alignment must be greater than 0\n");
      exit(1);
    }

    binfo.vp_numunits = iniparser_getint(cfg, (char*)"system:value_pool_size", 128);
    binfo.vp_unitsize = iniparser_getint(cfg, (char*)"system:value_pool_unit", 4096);
    binfo.vp_alignment = iniparser_getint(cfg, (char*)"system:value_pool_alignment", 4096);
    if(binfo.vp_alignment <= 0) {
      fprintf(stderr,
      "ERROR: system:value_pool_alignment must be greater than 0\n");
      exit(1);
    }
    str = iniparser_getstring(cfg, (char*)"kvs:write_mode", (char*)"sync");
    binfo.kv_write_mode = (str[0]=='s')?(1):(0);

    str = iniparser_getstring(cfg, (char*)"kvs:allow_sleep", (char*)"true");
    binfo.allow_sleep = (str[0]=='t')?(1):(0);

    char *devname_ret;
    str = iniparser_getstring(cfg, (char*)"system:device_path", (char*)"");
    strcpy(binfo.device_path, str);

    binfo.device_name = (char **)malloc(sizeof(char*) * 256);
#ifndef __KV_BENCH
    i = 0;
    pt = strtok (str, ",");
    while(pt != NULL){// && i < binfo.nfiles) {
      binfo.device_name[i] = (char *)malloc(256);
      devname_ret = _get_filename_pos(pt);
      //strcpy(binfo.device_name[i++], devname_ret ? devname_ret : pt);
      snprintf(binfo.device_name[i++], 256, "%s", devname_ret ? devname_ret : pt);
      pt = strtok(NULL, ",");
    }
    binfo.nfiles = i;
#endif

#if defined(__KV_BENCH) || defined (__AS_BENCH) || defined (__KVROCKS_BENCH)
    str = iniparser_getstring(cfg, (char*)"kvs:store_option", (char*)"post");
    if (str[0] == 'i' || str[0] == 'I') binfo.store_option = 0;    // KVS_STORE_IDEMPOTENT
    else if (str[0] == 'p' || str[0] == 'P') binfo.store_option = 1;  // KVS_STORE_POST
    else binfo.store_option = 2; // KVS_STORE_APPEND

    binfo.queue_depth = iniparser_getint(cfg, (char*)"kvs:queue_depth", 8);
    if(binfo.kv_write_mode == 0) { // aio
      if (binfo.queue_depth <= 0) {
      	fprintf(stderr, "WARN: Please set queue depth greater than 0\n");
      	iniparser_free(cfg);
      	exit(1);
      } else if (binfo.queue_depth > binfo.kp_numunits || binfo.queue_depth > binfo.vp_numunits) {
      	fprintf(stderr, "WARN: Please set key/value pool size equal to or greater than the queue_depth: %d\n", binfo.queue_depth);
      	iniparser_free(cfg);
      	exit(1);
      } else if (binfo.queue_depth > COUCH_MAX_QUEUE_DEPTH) {
        fprintf(stderr, "WARN: Please set the queue_depth equal or less than"
          " %d: Now,the queue_depth is %d\n",
          COUCH_MAX_QUEUE_DEPTH, binfo.queue_depth);
        iniparser_free(cfg);
        exit(1);
      }
    }

    binfo.aiothreads_per_device = iniparser_getint(cfg, (char*)"kvs:aiothreads_per_device", 2);
    
    str = iniparser_getstring(cfg, (char*)"kvs:core_ids", (char*)"1");
    strcpy(binfo.core_ids, str);
    
    str = iniparser_getstring(cfg, (char*)"kvs:cq_thread_ids", (char*)"2");
    strcpy(binfo.cq_thread_ids, str);
    
    binfo.mem_size_mb = iniparser_getint(cfg, (char*)"kvs:mem_size_mb", 1024);
    str = iniparser_getstring(cfg, (char*)"kvs:device_path", (char*)"");
    strcpy(binfo.kv_device_path, str);
#ifdef __KV_BENCH
    i = 0;
    pt = strtok (str, ",");
    while(pt != NULL){
      binfo.device_name[i] = (char *)malloc(256);
      devname_ret = _get_filename_pos(pt);
      //strcpy(binfo.device_name[i++], devname_ret ? devname_ret : pt);
      snprintf(binfo.device_name[i++], 256, "%s", devname_ret ? devname_ret : pt);
      pt = strtok(NULL, ",");
    }
    binfo.nfiles = i;
#endif
    str = iniparser_getstring(cfg, (char*)"kvs:emul_configfile", (char*)"/tmp/kvemul.conf");
    strcpy(binfo.kv_emul_configfile, str);
    
    str = iniparser_getstring(cfg, (char*)"kvs:with_iterator", (char*)"false");
    if (str[0] == 't' || str[0] == 'T') {
      /* Run iterator with read/write mixed workload */
      binfo.with_iterator = 1;
    } else if (str[0] == 'a' || str[0] == 'A') {
      /* Run iterator alone without read/write mixed */
      binfo.with_iterator = 2;
    } else {
      /* Default mode: no iterator */
      binfo.with_iterator = 0;
    }

    str = iniparser_getstring(cfg, (char*)"kvs:iterator_mode", (char*)"key");
    if (str[0] == 'v') {
      /* iterator retrieve key & value */
      binfo.iterator_mode = 1;
    } else {
      /* Default: iterator retrieve key only */
      binfo.iterator_mode = 0;
    }
    //fprintf(stdout, "with iterator %d - mode %d\n", binfo.with_iterator, binfo.iterator_mode);
    /*
    str = iniparser_getstring(cfg, (char*)"kvs:is_polling", (char*)"true");
    if (str[0] == 't' || str[0] == 'T') {
      binfo.is_polling = 1;
    } else {
      binfo.is_polling = 0;
    }
    */
#endif

#if defined(__AS_BENCH)
    str = iniparser_getstring(cfg, (char*)"aerospike:hosts", (char*)"127.0.0.1");
    strcpy(binfo.device_path, str);

    binfo.as_port = iniparser_getint(cfg, (char*)"aerospike:port", 3000);
    binfo.loop_capacity = iniparser_getint(cfg, (char*)"aerospike:loop_capacity", 10);

    str = iniparser_getstring(cfg, (char*)"aerospike:namespace", (char*)"test");
    strcpy(binfo.name_space, str);
#endif
    
#if defined(__BLOBFS_ROCKS_BENCH)
    str = iniparser_getstring(cfg, (char*)"blobfs:spdk_conf_file", (char*)"");
    strcpy(binfo.spdk_conf_file, str);
    //str = iniparser_getstring(cfg, (char*)"blobfs:spdk_bdev", (char*)"");
    //strcpy(binfo.spdk_bdev, str);
    binfo.spdk_cache_size = iniparser_getint(cfg, (char*)"blobfs:spdk_cache_size", 4096);
    fprintf(stderr, "SPDK Conf file: %s, cache size: %lu.\n", binfo.spdk_conf_file, binfo.spdk_cache_size);
#endif
    
    str = iniparser_getstring(cfg, (char*)"log:filename", (char*)"");
    strcpy(binfo.log_filename, str);

    binfo.cache_size =
        iniparser_getint(cfg, (char*)"db_config:cache_size_MB", 128);
    binfo.cache_size *= (1024*1024);

    str = iniparser_getstring(cfg, (char*)"db_config:compaction_mode",
                                   (char*)"auto");
    if (str[0] == 'a' || str[0] == 'A') binfo.auto_compaction = 1;
    else binfo.auto_compaction = 0;
#if defined(__LEVEL_BENCH) || defined(__ROCKS_BENCH) || defined(__WT_BENCH) || defined(__BLOBFS_ROCKS_BENCH) || defined(__KVDB_BENCH)
    binfo.auto_compaction = 1;
#elif defined(__COUCH_BENCH)
    // couchstore: manual compaction only
    binfo.auto_compaction = 0;
#endif

    // Number of auto-compaction threads for ForestDB
    binfo.auto_compaction_threads =
        iniparser_getint(cfg, (char*)"db_config:auto_compaction_threads", 4);

    // write buffer size for LevelDB & RocksDB
    binfo.wbs_init =
        iniparser_getint(cfg, (char*)"db_config:wbs_init_MB", 4);
    binfo.wbs_init *= (1024*1024);
    binfo.wbs_bench =
        iniparser_getint(cfg, (char*)"db_config:wbs_bench_MB", 4);
    binfo.wbs_bench *= (1024*1024);
    // bloom filter bit for LevelDB & RocksDB
    binfo.bloom_bpk =
        iniparser_getint(cfg, (char*)"db_config:bloom_bits_per_key", 0);
    // compaction style for RocksDB
    str = iniparser_getstring(cfg, (char*)"db_config:compaction_style",
                                   (char*)"level");
    if (str[0] == 'F' || str[0] == 'f') {
        // FIFO style
        binfo.compaction_style = 2;
    } else if (str[0] == 'U' || str[0] == 'u') {
        // universal style
        binfo.compaction_style = 1;
    } else {
      // level style (default)
        binfo.compaction_style = 0;
    }
    // WAL size for ForestDB
    binfo.fdb_wal = iniparser_getint(cfg, (char*)"db_config:fdb_wal", 4096);
    // indexing type for ForestDB
    str = iniparser_getstring(cfg, (char*)"db_config:fdb_type", (char*)"hb+trie");
    if (str[0] == 'h' || str[0] == 'H') {
        binfo.fdb_type = 0; /* hb+trie */
    } else {
        binfo.fdb_type = 1; /* b-tree */
    }
    // indexing type for WiredTiger
    str = iniparser_getstring(cfg, (char*)"db_config:wt_type", (char*)"btree");
    if (str[0] == 'b' || str[0] == 'B') {
        binfo.wt_type = 0; /* b-tree */
    } else {
        binfo.wt_type = 1; /* lsm-tree */
    }

    // compression
    str = iniparser_getstring(cfg, (char*)"db_config:compression", (char*)"false");
    if (str[0] == 't' || str[0] == 'T' || str[0] == 'e' || str[0] == 'E') {
        // enabled
        binfo.compression = 1;
    } else {
        binfo.compression = 0;
    }
    //split_pct and page size paramaters for WiredTiger
    binfo.split_pct =
        iniparser_getint(cfg, (char*)"db_config:split_pct", 100);
    binfo.leaf_pg_size =
        iniparser_getint(cfg, (char*)"db_config:leaf_pg_size_KB", 4);
    binfo.int_pg_size =
        iniparser_getint(cfg, (char*)"db_config:int_pg_size_KB", 4);

    str = iniparser_getstring(cfg, (char*)"db_file:filename",
                                   (char*)"./dummy");

#if defined(__ROCKS_BENCH)
    if(!is_valide_path_name(str)){
      fprintf(stderr,
        "ERROR: filename as below is invalid.\n[db_file]\nfilename = %s\n", str);
      exit(1);
    }
    if(access(str, F_OK)){
      fprintf(stderr,
        "ERROR: file does not exist.\n[db_file]\nfilename = %s\n", str);
      exit(1);
    }

    struct stat st;
    stat(str,&st);
    if (!S_ISDIR(st.st_mode)){
      fprintf(stderr,
        "ERROR: filename as below must be a directory for rocksdb_bench."
        "\n[db_file]\nfilename = %s\n", str);
      exit(1);
    }
#endif

    char dirname[256], *dirname_ret, *filename_ret;
    dirname_ret = _get_dirname(str, dirname);
    filename_ret = _get_filename_pos(str);
    if (dirname_ret) {
      /*
      sprintf(binfo.filename, "%s/%s/%s_%s",
	      dirname_ret, dbname_postfix, filename_ret, dbname_postfix);
      */
      sprintf(binfo.filename, "%s/%s", dirname_ret, filename_ret);
    } else {
        sprintf(binfo.filename, "%s/%s_%s",
            dbname_postfix, str, dbname_postfix);
    }
#if !defined(__KV_BENCH)
    printf("db name %s\n", binfo.filename);
#endif
    str = iniparser_getstring(cfg, (char*)"db_file:init_filename",
                                   binfo.filename);
    strcpy(binfo.init_filename, str);

    str = iniparser_getstring(cfg, (char*)"population:pre_gen",
			      (char*)"false");
    binfo.pre_gen = (str[0]=='t')?(1):(0);
    
    str = iniparser_getstring(cfg, (char*)"population:seq_fill",
			      (char*)"false");
    binfo.seq_fill = (str[0]=='t')?(1):(0);

    str = iniparser_getstring(cfg, (char*)"population:pop_first",
			      (char*)"true");
    binfo.pop_first = (str[0]=='t')?(1):(0);

    long int tmp_pop_nthreads =
      iniparser_getint(cfg, (char*)"population:nthreads", ncores*2);
    binfo.pop_nthreads = (tmp_pop_nthreads<1) ? (ncores * 2) : tmp_pop_nthreads;
#if defined __KVROCKS_BENCH
    if(binfo.pop_nthreads > 1) {
      fprintf(stderr, "WARN: KV SSD only support one thread per device now\n");
    }
    binfo.pop_nthreads = 1;
#endif

    binfo.pop_batchsize = iniparser_getint(cfg, (char*)"population:batchsize",
                                                1);

    str = iniparser_getstring(cfg, (char*)"population:periodic_commit",
                                   (char*)"no");
    if (str[0] == 'n' /*|| binfo.nthreads == 1*/) binfo.pop_commit = 0;
    else binfo.pop_commit = 1;

    str = iniparser_getstring(cfg, (char*)"population:fdb_flush_wal",
                                   (char*)"no");
    if (str[0] == 'n' /*|| binfo.nthreads == 1*/) binfo.fdb_flush_wal = 0;
    else binfo.fdb_flush_wal = 1;

    // key length
    int max_key_len;
    str = iniparser_getstring(cfg, (char*)"key_length:distribution",
                                   (char*)"normal");
    if (str[0] == 'n') {
        binfo.keylen.type = RND_NORMAL;
        binfo.keylen.a = iniparser_getint(cfg, (char*)"key_length:median", 64);
        binfo.keylen.b =
            iniparser_getint(cfg, (char*)"key_length:standard_deviation", 8);
	      max_key_len = binfo.keylen.a + binfo.keylen.b * 6;
    }else if (str[0] == 'u'){
        binfo.keylen.type = RND_UNIFORM;
        binfo.keylen.a =
            iniparser_getint(cfg, (char*)"key_length:lower_bound", 32);
        binfo.keylen.b =
            iniparser_getint(cfg, (char*)"key_length:upper_bound", 96);
        if (binfo.keylen.a > binfo.keylen.b) {
          fprintf(stderr, "WARN: Please set the upper bound of key length to be equal to or larger than the lower bound\n");
          iniparser_free(cfg);
          exit(1);
        }
	      max_key_len = binfo.keylen.b + 16;
    } else {
      binfo.keylen.type = RND_FIXED;
      binfo.keylen.a = iniparser_getint(cfg, (char*)"key_length:fixed_size", 16);
      max_key_len = binfo.keylen.a;
    }

#if defined(__KV_BENCH)
    if (binfo.keylen.a < couch_kv_min_key_len || binfo.keylen.a > couch_kv_max_key_len || binfo.keylen.b > couch_kv_max_key_len) {
      fprintf(stderr, "WARN: The length of key should be between %d and %d\n", couch_kv_min_key_len, couch_kv_max_key_len);
      iniparser_free(cfg);
      exit(1);
    }
#endif

    if(binfo.kp_unitsize < max_key_len) {
      fprintf(stderr, "WARN: Please set 'key_pool_unit' to be equal to or larger than the largest possible key size in your config: %d\n", max_key_len);
      iniparser_free(cfg);
      exit(1);
    }

    // prefix composition
    str = iniparser_getstring(cfg, (char*)"prefix:distribution",
                                   (char*)"uniform");
    if (str[0] == 'n') {
        binfo.prefixlen.type = RND_NORMAL;
        binfo.prefixlen.a = iniparser_getint(cfg, (char*)"prefix:median", 8);
        binfo.prefixlen.b =
            iniparser_getint(cfg, (char*)"prefix:standard_deviation", 1);
    }else{
        binfo.prefixlen.type = RND_UNIFORM;
        binfo.prefixlen.a =
            iniparser_getint(cfg, (char*)"prefix:lower_bound", 4);
        binfo.prefixlen.b =
            iniparser_getint(cfg, (char*)"prefix:upper_bound", 12);
    }
    binfo.nlevel = iniparser_getint(cfg, (char*)"prefix:level", 0);
    binfo.nprefixes = iniparser_getint(cfg, (char*)"prefix:nprefixes", 100);

    // thread information
    long int tmp_nreaders, tmp_niterators, tmp_nwriters, tmp_ndeleters;
    long int tmp_sum_threads;
    tmp_nreaders = iniparser_getint(cfg, (char*)"threads:readers", 0);
    tmp_niterators = iniparser_getint(cfg, (char*)"threads:iterators", 0);
    tmp_nwriters = iniparser_getint(cfg, (char*)"threads:writers", 0);
    tmp_ndeleters = iniparser_getint(cfg, (char*)"threads:deleters", 0);
    tmp_sum_threads = tmp_nreaders + tmp_niterators +
      tmp_nwriters + tmp_ndeleters;
    if (tmp_nreaders < 0 || tmp_niterators < 0 ||
        tmp_nwriters < 0 || tmp_ndeleters < 0 || tmp_sum_threads <= 0) {
      fprintf(stderr, "WARN: Please set the thread number to be equal to or"
        " larger than 0, and the total thread number to be larger than 0\n");
      iniparser_free(cfg);
      exit(1);
    }
    binfo.nreaders = tmp_nreaders;
    binfo.niterators = tmp_niterators;
    binfo.nwriters = tmp_nwriters;
    binfo.ndeleters = tmp_ndeleters;

    binfo.reader_ops = iniparser_getint(cfg, (char*)"threads:reader_ops", 0);
    binfo.writer_ops = iniparser_getint(cfg, (char*)"threads:writer_ops", 0);
    str = iniparser_getstring(cfg, (char*)"threads:disjoint_write", (char*)"false");
    if (str[0] == 't' || str[0] == 'T' || str[0] == 'e' || str[0] == 'E') {
        // enabled
        binfo.disjoint_write = 1;
    } else {
        binfo.disjoint_write = 0;
    }
#if defined __KVROCKS_BENCH
    // only support one thread per device for kv
    if (binfo.nreaders + binfo.ndeleters + binfo.nwriters + binfo.niterators > 1) {
      fprintf(stderr, "WARN: KV SSD only support one thread per device now\n");
    }
    binfo.nreaders = binfo.ndeleters = binfo.niterators = 0;
    binfo.nwriters = 1;
#endif
    init_cpu_aff(&binfo);
    
    // create keygen structure
    _set_keygen(&binfo);

    // body length
    int max_body_buf;
    str = iniparser_getstring(cfg, (char*)"body_length:distribution",
                                   (char*)"normal");
    if (str[0] == 'n') {
        binfo.bodylen.type = RND_NORMAL;
        binfo.bodylen.a =
            iniparser_getint(cfg, (char*)"body_length:median", 512);
        binfo.bodylen.b =
            iniparser_getint(cfg, (char*)"body_length:standard_deviation", 32);
        DATABUF_MAXLEN = binfo.bodylen.a + 5*binfo.bodylen.b;
	      max_body_buf = binfo.bodylen.a + binfo.bodylen.b * 6;
    }else if (str[0] == 'u'){
        binfo.bodylen.type = RND_UNIFORM;
        binfo.bodylen.a =
            iniparser_getint(cfg, (char*)"body_length:lower_bound", 448);
        binfo.bodylen.b =
            iniparser_getint(cfg, (char*)"body_length:upper_bound", 576);
        if (binfo.bodylen.a > binfo.bodylen.b) {
          fprintf(stderr, "WARN: Please set the upper bound of value body length to be equal to or larger than the lower bound\n");
          iniparser_free(cfg);
          exit(1);
        }
        DATABUF_MAXLEN = binfo.bodylen.b;
	      max_body_buf = binfo.bodylen.b + 16;
    } else if (str[0] == 'f'){
      binfo.bodylen.type = RND_FIXED;
      binfo.bodylen.a =
	      iniparser_getint(cfg, (char*)"body_length:fixed_size", 512);
      DATABUF_MAXLEN = binfo.bodylen.a;
      max_body_buf = binfo.bodylen.a;
    } else if (str[0] == 'r') {
      char buf[256];
      char *pt;
      int size_cnt = 0;
      i = 0;
      binfo.bodylen.type = RND_RATIO;
      str = iniparser_getstring(cfg, (char*)"body_length:value_size",(char*)"4096");
      memset(binfo.value_size, 0, sizeof(binfo.value_size));
      strcpy(buf, str);
      pt = strtok ((char*)buf, ",");
      while(pt != NULL && i < 5) {
      	binfo.value_size[i++] = atoi(pt);
      	DATABUF_MAXLEN = MAX(DATABUF_MAXLEN, atoi(pt));
      	pt = strtok(NULL, ",");
      }
      max_body_buf = DATABUF_MAXLEN;
      str = iniparser_getstring(cfg, (char*)"body_length:value_size_ratio",(char*)"100:0");
      size_cnt = i;
      i = 0;
      int total_ratio = 0;;
      memset(binfo.value_size_ratio, 0, sizeof(binfo.value_size_ratio));
      strcpy(buf, str);
      pt = strtok((char*)buf, ":");
      while(pt!= NULL && i < 5) {
      	binfo.value_size_ratio[i++] = atoi(pt);
      	total_ratio += atoi(pt);
      	pt = strtok(NULL, ":");
      }

      if(i != size_cnt) {
        fprintf(stdout, "WARN: The number of value_size and value_size_ratio are mismatch\n");
        iniparser_free(cfg);
        exit(1);
      }

      if(total_ratio != 100) {
      	fprintf(stdout, "WARN: Total value size ratio should equal to 100\n");
      	iniparser_free(cfg);
      	exit(1);
      }

      //binfo.vp_unitsize = DATABUF_MAXLEN;

    } else {
      fprintf(stdout, "Please set value size (body_length) distribution properly:\n");
      fprintf(stdout, "fixed,ratio,uniform,normal\n");
      iniparser_free(cfg);
      exit(1);
    }
    
    if(binfo.vp_unitsize < max_body_buf) {
      fprintf(stderr, "WARN: Please set 'value_pool_unit' to be equal to or larger than the largest possible value size in your config: %d\n", max_body_buf);
      iniparser_free(cfg);
      exit(1);
    }
    if(binfo.vp_unitsize == 0 || binfo.kp_unitsize == 0){
      fprintf(stderr, "WARN: Invalid memory pool size, should be greater than 0\n");
      iniparser_free(cfg);
      exit(1);
    }

    binfo.compressibility =
        iniparser_getint(cfg, (char*)"body_length:compressibility", 100);
    if (binfo.compressibility > 100) {
        binfo.compressibility = 100;
    }
    if (binfo.compressibility < 0) {
        binfo.compressibility = 0;
    }

    binfo.nbatches = iniparser_getint(cfg, (char*)"operation:nbatches", 0);
    binfo.nops = iniparser_getint(cfg, (char*)"operation:nops", 0);
    binfo.warmup_secs = iniparser_getint(cfg, (char*)"operation:warmingup", 0);
    binfo.bench_secs = iniparser_getint(cfg, (char*)"operation:duration", 0);
    if (binfo.nbatches == 0 && binfo.nops == 0 && binfo.bench_secs == 0) {
        binfo.bench_secs = 60;
    }

    size_t avg_write_batchsize;
    str = iniparser_getstring(cfg, (char*)"operation:batchsize_distribution",
                              (char*)"normal");
    if (str[0] == 'n') {
        binfo.rbatchsize.type = RND_NORMAL;
        binfo.rbatchsize.a =
            iniparser_getint(cfg, (char*)"operation:read_batchsize_median", 3);
        binfo.rbatchsize.b =
            iniparser_getint(cfg, (char*)"operation:read_batchsize_"
                                         "standard_deviation", 1);
        binfo.ibatchsize.type = RND_NORMAL;
        binfo.ibatchsize.a =
            iniparser_getint(cfg, (char*)"operation:iterate_batchsize_median", 1000);
        binfo.ibatchsize.b =
            iniparser_getint(cfg, (char*)"operation:iterate_batchsize_"
                                         "standard_deviation", 100);
        binfo.wbatchsize.type = RND_NORMAL;
        binfo.wbatchsize.a =
            iniparser_getint(cfg, (char*)"operation:write_batchsize_"
                                         "median", 10);
        binfo.wbatchsize.b =
            iniparser_getint(cfg, (char*)"operation:write_batchsize_"
                                         "standard_deviation", 1);
        avg_write_batchsize = binfo.wbatchsize.a;
    }else{
        binfo.rbatchsize.type = RND_UNIFORM;
        binfo.rbatchsize.a =
            iniparser_getint(cfg, (char*)"operation:read_batchsize_"
                                         "lower_bound", 1);
        binfo.rbatchsize.b =
            iniparser_getint(cfg, (char*)"operation:read_batchsize_"
                                         "upper_bound", 5);
        binfo.ibatchsize.type = RND_UNIFORM;
        binfo.ibatchsize.a =
            iniparser_getint(cfg, (char*)"operation:iterate_batchsize_"
                                         "lower_bound", 500);
        binfo.ibatchsize.b =
            iniparser_getint(cfg, (char*)"operation:iterate_batchsize_"
                                         "upper_bound", 1500);
        binfo.wbatchsize.type = RND_UNIFORM;
        binfo.wbatchsize.a =
            iniparser_getint(cfg, (char*)"operation:write_batchsize_"
                                         "lower_bound", 5);
        binfo.wbatchsize.b =
            iniparser_getint(cfg, (char*)"operation:write_batchsize_"
                                         "upper_bound", 15);
        avg_write_batchsize = (binfo.wbatchsize.a + binfo.wbatchsize.b)/2;
    }

    str = iniparser_getstring(cfg, (char*)"operation:read_query",
                                   (char*)"key");
    if (str[0] == 'k' || str[0] == 'i') {
        binfo.read_query_byseq = 0;
    }else {
        // by_seq is not supported now..
        //binfo.read_query_byseq = 1;
        binfo.read_query_byseq = 0;
    }
    /*
    str = iniparser_getstring(cfg,
                              (char*)"operation:batch_distribution",
                              (char*)"uniform");
    if (str[0] == 'u') {
        binfo.batch_dist.type = RND_UNIFORM;
        binfo.batch_dist.a = 0;
        //binfo.batch_dist.b = binfo.ndocs;
      	if(binfo.nops > 0){
      	  // if set total # of op counts, refer to YCSB for
      	  // insertion key space extension
      	  binfo.batch_dist.b = binfo.ndocs + binfo.nops * binfo.ratio[2] * 2;
      	  printf("higher bound is %d\n", binfo.batch_dist.b);
      	  iniparser_free(cfg);
      	  exit(0);
      	} else {
      	  binfo.batch_dist.b = binfo.ndocs * binfo.amp_factor;
      	}
    }else{
        double s = iniparser_getdouble(cfg, (char*)"operation:"
                                                   "batch_parameter1", 1);
        binfo.batch_dist.type = RND_ZIPFIAN;
        binfo.batch_dist.a = (int64_t)(s * 100);
        binfo.batch_dist.b =
            iniparser_getint(cfg, (char*)"operation:batch_parameter2", 64);
    }
    */
    str = iniparser_getstring(cfg,
                              (char*)"operation:operation_distribution",
                              (char*)"uniform");
    if (str[0] == 'n') {
        binfo.op_dist.type = RND_NORMAL;
    }else{
        binfo.op_dist.type = RND_UNIFORM;
    }

    binfo.batchrange = iniparser_getint(cfg, (char*)"operation:batch_range",
                                        avg_write_batchsize);
    /*
    binfo.write_prob = iniparser_getint(cfg,
                                        (char*)"operation:write_ratio_percent",
                                        20);
    if (binfo.write_prob == 0) {
        binfo.nwriters = 0;
    } else if (binfo.write_prob == 100) {
        binfo.nreaders = 0;
    }
    */
    str = iniparser_getstring(cfg, (char*)"operation:read_write_insert_delete",
			      (char*)"0:0:0:0");
    printf("ratio is %s\n", str);
    //char *pt;
    memset(binfo.ratio, 0, sizeof(binfo.ratio));
    char buff[256];
    int op_total_ratio = 0;
    i = 0;
    strcpy(buff, str);
    pt = strtok ((char*)buff, ":");
    while(pt != NULL){
      binfo.ratio[i++] = atoi(pt);
      pt = strtok(NULL, ":");
      op_total_ratio += binfo.ratio[i - 1];
    }
    if (op_total_ratio != 0 && op_total_ratio != 100) {
  	  fprintf(stdout, "WARN: Total operation ratio should equal to 100 or 0\n");
  	  iniparser_free(cfg);
  	  exit(1);
    }

    str = iniparser_getstring(cfg,
			      (char*)"operation:batch_distribution",
			      (char*)"uniform");
    if (str[0] == 'u') {
      binfo.batch_dist.type = RND_UNIFORM;
      binfo.batch_dist.a = 0;
      //binfo.batch_dist.b = binfo.ndocs;
      if(binfo.nops > 0){
      	// if set total # of op counts, refer to YCSB for
      	// insertion key space extension
      	binfo.batch_dist.b = binfo.ndocs + binfo.nops * binfo.ratio[2] * 2;
      	printf("Using insertion ratio control, key range 0~%ld\n", binfo.batch_dist.b);
      } else {
      	binfo.batch_dist.b = binfo.ndocs * binfo.amp_factor;
      }
    }else{
      double s = iniparser_getdouble(cfg, (char*)"operation:"
				     "batch_parameter1", 1);
      binfo.batch_dist.type = RND_ZIPFIAN;
      binfo.batch_dist.a = (int64_t)(s * 100);
      binfo.batch_dist.b =
		    iniparser_getint(cfg, (char*)"operation:batch_parameter2", 64);
    }

    str = iniparser_getstring(cfg, (char*)"operation:write_type",
                                   (char*)"sync");
    binfo.sync_write = (str[0]=='s')?(1):(0);
    binfo.key_existing = iniparser_getboolean(cfg, (char*)"operation:key_existing", false);

    binfo.compact_thres =
        iniparser_getint(cfg, (char*)"compaction:threshold", 30);
    binfo.compact_period =
        iniparser_getint(cfg, (char*)"compaction:period", 60);
    binfo.block_reuse_thres =
        iniparser_getint(cfg, (char*)"compaction:block_reuse", 70);
    if (binfo.block_reuse_thres) {
        if (binfo.block_reuse_thres > 100) {
            binfo.block_reuse_thres = 100;
        }

#ifdef __FDB_BENCH
        // if block reuse is turned on, re-adjust compaction threshold
        // (only for forestdb)
        int live_ratio = 100 - binfo.block_reuse_thres;
        if (binfo.compact_thres > 0 && binfo.compact_thres < 100) {
            binfo.compact_thres = 100 - live_ratio/2;
        }
#endif
    }

    // latency monitoring
    binfo.latency_rate =
        iniparser_getint(cfg, (char*)"latency_monitor:rate", 100);
    if (binfo.latency_rate > 1000) {
        binfo.latency_rate = 1000;
    }
    binfo.latency_max =
        iniparser_getint(cfg, (char*)"latency_monitor:max_samples", 1000000);
    if (!binfo.latency_max) {
      printf("WARN: max_sample cannot be 0\n");
      iniparser_free(cfg);
      exit(0);
    }
    print_term_ms =
        iniparser_getint(cfg, (char*)"latency_monitor:print_term_ms", 100);
    if (!print_term_ms) {
      printf("WARN: print_term_ms cannot be 0\n");
      iniparser_free(cfg);
      exit(0);
    }
    if (binfo.bench_secs != 0 && print_term_ms > binfo.bench_secs * 1000)
      print_term_ms = binfo.bench_secs * 1000;
    iniparser_free(cfg);
    return binfo;
}

int main(int argc, char **argv){
    int opt, i;
    int initialize = 1;
    int config_only = 0;
    char config_filename[256];
    const char *short_opt = "hecf:";
    char filename[256], timelog_filename[256];
    char insert_latency_filename[256], insert_ops_filename[256];
    char run_latency_filename[256], run_ops_filename[256];
    struct bench_info binfo;
    struct timeval gap;

    randomize();
    rnd_seed = rand();

    strcpy(config_filename,"bench_config.ini");

    struct option   long_opt[] =
    {
        {"database",      no_argument,       NULL, 'e'},
	    {"config",        no_argument,       NULL, 'c'},
        {"help",          no_argument,       NULL, 'h'},
        {"file",          optional_argument, NULL, 'f'},
        {NULL,            0,                 NULL, 0  }
    };

    while( (opt = getopt_long(argc, argv, short_opt, long_opt, NULL) ) != -1) {
        switch(opt)
        {
            case -1:       // no more arguments
            case 0:        // toggle long options
                break;

            case 'f':
                printf("Using \"%s\" config file\n", optarg);
                strcpy(config_filename, optarg);
                break;

            case 'e':
                printf("Using existing DB file\n");
                initialize = 0;
                break;

      	    case 'c':
      	        printf("Generating CPU file only\n");
      		    config_only = 1;
		        break;

            case 'h':
                printf("Usage: %s [OPTIONS]\n", argv[0]);
                printf("  -f file                   file\n");
                printf("  -e, --database            use existing database file\n");
		printf("  -c, --config only         generate CPU config file\n");
                printf("  -h, --help                print this help and exit\n");
                printf("\n");
                return(0);

            case ':':
            case '?':
                fprintf(stderr, "Try `%s --help' for more information.\n", argv[0]);
                return(-2);

            default:
                fprintf(stderr, "%s: invalid option -- %c\n", argv[0], opt);
                fprintf(stderr, "Try `%s --help' for more information.\n", argv[0]);
                return(-2);
        };
    };

    binfo = get_benchinfo(config_filename, config_only);

    if (strcmp(binfo.log_filename, "")){
        int ret;
        char temp[256], cmd[256], *str, *filename_ret;

        // create directory if doesn't exist
        str = _get_dirname(binfo.log_filename, temp);
        if (str) {
            if (!_does_file_exist(str)) {
                sprintf(cmd, "mkdir -p %s > errorlog.txt", str);
                ret = system(cmd);
                (void)ret;
            }
        }

        // openops log file
        gettimeofday(&gap, NULL);

      	filename_ret =  _get_filename_pos(binfo.log_filename);
      	sprintf(filename, "%s/%s-%s.txt", str, binfo.dbname, filename_ret);
        log_fp = fopen(filename, "w");

      	/* Separate each stat to different output files in csv format  */
      	//insert_latency_filename[256], insert_ops_filename[256];
      	//run_latency_filename[256], run_ops_filename[256];
      	sprintf(insert_latency_filename, "%s/%s-insert.latency.csv",
      		str, binfo.dbname);
      	sprintf(insert_ops_filename, "%s/%s-insert.ops.csv",
      		str, binfo.dbname);
      	sprintf(run_latency_filename, "%s/%s-run.latency.csv",
      		str, binfo.dbname);
      	sprintf(run_ops_filename, "%s/%s-run.ops.csv",
      		str, binfo.dbname);

      	insert_latency_fp = fopen(insert_latency_filename, "w");
      	insert_ops_fp = fopen(insert_ops_filename, "w");
      	if(binfo.with_iterator != 2) {
      	  run_latency_fp = fopen(run_latency_filename, "w");
      	  run_ops_fp = fopen(run_ops_filename, "w");
      	}
    }

    binfo.initialize = initialize;

    _print_benchinfo(&binfo);

    do_bench(&binfo);

    if (log_fp) {
        fclose(log_fp);
    }
    if(insert_latency_fp)
      fclose(insert_latency_fp);
    if(insert_ops_fp)
      fclose(insert_ops_fp);
    if(run_latency_fp)
      fclose(run_latency_fp);
    if(run_ops_fp)
      fclose(run_ops_fp);
    if(binfo.cpuinfo){
      if(binfo.cpuinfo->cpulist)
	      free(binfo.cpuinfo->cpulist);
      free(binfo.cpuinfo);
    }
    if(binfo.instances){
      if(binfo.instances->coreids_pop)
	      free(binfo.instances->coreids_pop);
      if(binfo.instances->coreids_bench)
	      free(binfo.instances->coreids_bench);
      free(binfo.instances);
    }

    if(binfo.device_name) {
      for(i = 0; i < binfo.nfiles; i++)
	      free(binfo.device_name[i]);
      free(binfo.device_name);
    }
    if(binfo.dbname)
      free(binfo.dbname);
    if(binfo.filename)
      free(binfo.filename);
    if(binfo.init_filename)
      free(binfo.init_filename);
    if(binfo.log_filename)
      free(binfo.log_filename);
    if(binfo.device_path)
      free(binfo.device_path);
    if(binfo.kv_device_path)
      free(binfo.kv_device_path);
    if(binfo.kv_emul_configfile)
      free(binfo.kv_emul_configfile);
    if(binfo.spdk_conf_file)
      free(binfo.spdk_conf_file);
    //if(binfo.spdk_bdev)
    // free(binfo.spdk_bdev);
    if(binfo.core_ids)
      free(binfo.core_ids);
    if(binfo.cq_thread_ids)
      free(binfo.cq_thread_ids);
    if(binfo.name_space)
      free(binfo.name_space);
    return 0;
}
