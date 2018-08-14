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
#include <dirent.h>

#include "libcouchstore/couch_db.h"
#include "libcouchstore/file_ops.h"
#include "adv_random.h"
#include "stopwatch.h"
#include "iniparser.h"

#include "arch.h"
#include "zipfian_random.h"
#include "keygen.h"
#include "keyloader.h"
#include "memory.h"

#include "memleak.h"

#if defined __BLOBFS_ROCKS_BENCH
#include "rocksdb/env.h"
#endif

#define COMP_LAT
#define time_check (0)
#define time_check2 (0)
#define SEC_TO_NANO (1000000000L)
/////////////////////////////////
// changho

//#define THREADPOOL

#ifdef THREADPOOL
#include "thpool.h"
#endif
//////////////////////////////


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
std::atomic_uint_fast64_t test_qdepth;

struct bench_info {
    uint8_t initialize;  // flag for initialization
    uint64_t cache_size; // buffer cache size (for fdb, rdb, ldb)
    int auto_compaction; // compaction mode

    //memory allocator
  //void* (*allocate_mem)(int alignment, uint64_t size);
  //  void (*free_mem)(void *ptr);
    int allocatortype;
    mempool_t *keypool;
    mempool_t *valuepool;
    instance_info_t *instances;
    cpu_info *cpuinfo;
  
    //JP: kv bench
    char *device_path;
    int num_devices;
    char *device_name;
    int32_t device_id;
    int store_option;
    int queue_depth;
    int aiothreads_per_device;
    char *core_ids;

    // KV and aerospike
    uint8_t kv_write_mode;
    uint8_t allow_sleep;

    // aerospike
    uint16_t as_port;
    uint32_t loop_capacity;
    char *name_space;
  
    // spdk blobfs
    char *spdk_conf_file;
    char *spdk_bdev;
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
    size_t ndocs;
    float amp_factor;
    char *keyfile;
    char *dbname;
    char *init_filename;
    char *filename;
    char *log_filename;
  //char *timelog_filename;
  //char *syslog_filename;
    size_t nfiles;

    // population
    size_t pop_nthreads;
    size_t pop_batchsize;
    uint8_t pop_commit;
    uint8_t fdb_flush_wal;
    uint8_t seq_fill;
    uint8_t insert_and_update;
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
};

#define MIN(a,b) (((a)<(b))?(a):(b))

static uint32_t rnd_seed;
static int print_term_ms = 100;
static int filesize_chk_term = 4;

FILE *log_fp = NULL;
//FILE *timelog_fp = NULL;
//char *syslog_filename = NULL;

FILE *insert_latency_fp = NULL;
FILE *insert_ops_fp = NULL;
FILE *run_latency_fp = NULL;
FILE *run_ops_fp = NULL;


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
char databuf[4096];
couchstore_error_t couchstore_kvs_zalloc(size_t size_bytes, char **buf);
void _create_doc(struct bench_info *binfo,
                 size_t idx, Doc **pdoc,
                 DocInfo **pinfo, int seq_fill)
{
    int r, keylen = 0;
    uint32_t crc;
    Doc *doc = *pdoc;
#if defined __FDB_BENCH || defined __WT_BENCH 
    DocInfo *info = *pinfo;  // FDB need meta info 
#endif
    char keybuf[MAX_KEYLEN];

    struct timespec t1, t2, t3, t4;
    unsigned long long nanosec1, nanosec2, nanosec3;

    
    //if(seq_fill != 1) {
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
#if 0
    if(!seq_fill) {
      if (binfo->keyfile) {
        // load from file
        doc->id.size = keyloader_get_key(&binfo->kl, idx, keybuf);
      } else {
	// random keygen
	doc->id.size = keygen_seed2key(&binfo->keygen, idx, keybuf, keylen);
	/*
	  if (doc->id.size < 16) {
	  printf("key size unmatch %d \n", (int)doc->id.size);
	  exit(0);
	  }
	  */
      }
    } else {
      // TBD: fixed 16-byte key based on idx
      doc->id.size = binfo->keylen.a;
      keygen_seqfill(idx, keybuf, binfo->keylen.a);
    }
#endif

#if time_check2
    clock_gettime(CLOCK_REALTIME, &t1);
#endif
    //if (!doc->id.buf) {
#if defined (__KV_BENCH)

      doc->id.buf = (char*)Allocate(binfo->keypool);
      //couchstore_kvs_zalloc(MAX_KEYLEN, &(doc->id.buf));
      if(doc->id.buf == NULL) {printf("failed to alloc \n "); exit(1);}
#else
      doc->id.buf = (char *)malloc(MAX_KEYLEN);
      //doc->id.buf = (char*)binfo->allocate_mem(4096, MAX_KEYLEN);
#endif
      //}
#if time_check2
    clock_gettime(CLOCK_REALTIME, &t2);
#endif
    
    if (binfo->keyfile) {
      doc->id.size = keyloader_get_key(&binfo->kl, idx, doc->id.buf);
      //fprintf(stdout, "create key --- %s %d\n", doc->id.buf, (int)doc->id.size);
    } else {
      if(seq_fill) {
	doc->id.size = binfo->keylen.a;
	keygen_seqfill(idx, doc->id.buf, binfo->keylen.a);
      } else	
	doc->id.size = keygen_seed2key(&binfo->keygen, idx, doc->id.buf, keylen);
    }

    //fprintf(stdout, "create key --- %s %d\n", doc->id.buf, (int)doc->id.size);
    //memcpy(doc->id.buf, keybuf, doc->id.size);

    // as WiredTiger uses C-style string,
    // add NULL character at the end.
    doc->id.buf[doc->id.size] = 0;
#if time_check2
    clock_gettime(CLOCK_REALTIME, &t3);
#endif
    
    if(binfo->pre_gen == 1) {
      // TODO: make it various value length for pre-gen
      doc->data.size = 4096;
      doc->data.buf = databuf;    
    } else {
      if(binfo->bodylen.type == RND_FIXED) {
	doc->data.size = binfo->bodylen.a;
	if (!doc->data.buf) {
	  // fixed size value buffer, just some random data
#if defined __KV_BENCH
	  if(binfo->kv_write_mode == 1) // sync
	    couchstore_kvs_zalloc(doc->data.size, &(doc->data.buf));
	  else
	    doc->data.buf = (char *)malloc(doc->data.size);
	    //doc->data.buf = (char *)binfo->allocate_mem(4096, doc->data.size);
#else
	  doc->data.buf = (char *)malloc(doc->data.size);
	  //doc->data.buf = (char *)binfo->allocate_mem(4096, doc->data.size);
#endif
	  memcpy(doc->data.buf + doc->data.size - 5, (void*)"<end>", 5);
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
	if (!doc->data.buf) {
	  //printf("alloc ------ %d\n", keylen);
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
#if defined(__KV_BENCH)
	  couchstore_kvs_zalloc(max_bodylen, &(doc->data.buf));
#else
	  doc->data.buf = (char *)malloc(max_bodylen);
	  //doc->data.buf = (char *)binfo->allocate_mem(4096, max_bodylen);
#endif
	
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
    } // end of in-line gen

#if time_check2
    clock_gettime(CLOCK_REALTIME, &t4);
    nanosec1 = (t2.tv_sec - t1.tv_sec) * SEC_TO_NANO;
    if(t2.tv_nsec >= t1.tv_nsec)
      nanosec1 += (t2.tv_nsec - t1.tv_nsec);
    else
      nanosec1 += nanosec1 - t1.tv_nsec + t2.tv_nsec;

    nanosec2 = (t3.tv_sec - t2.tv_sec) * SEC_TO_NANO;
    if(t3.tv_nsec >= t2.tv_nsec)
      nanosec2 += (t3.tv_nsec - t2.tv_nsec);
    else
      nanosec2 += nanosec2 - t2.tv_nsec + t3.tv_nsec;

    nanosec3 = (t4.tv_sec - t3.tv_sec) * SEC_TO_NANO;
    if(t4.tv_nsec >= t3.tv_nsec)
      nanosec3 += (t4.tv_nsec - t3.tv_nsec);
    else
      nanosec3 += nanosec3 - t3.tv_nsec + t4.tv_nsec;
    
    fprintf(stdout, "%llu %llu %llu\n", nanosec1, nanosec2, nanosec3);
    
#endif
    
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

struct latency_stat {
  uint64_t cursor;
  uint64_t nsamples;
  uint32_t *samples;
  spin_t lock;
};

struct pop_thread_args {
    int n;
    int coreid;
    int socketid;
    int cur_qdepth;
    int max_qdepth;
    Db **db;
    struct bench_info *binfo;
    struct pop_thread_args *pop_args;
    spin_t *lock;
    uint64_t *counter;
    std::atomic_uint_fast64_t iocount;
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
couchstore_error_t  couchstore_kvs_get_aiocompletion(int32_t *count);
couchstore_error_t couchstore_save_documents_test(Db *db, int idx, int kv_write_mode, int monitoring);
std::atomic_uint_fast64_t submitted;
std::atomic_uint_fast64_t completed;
Doc **docs_pre = NULL;
DocInfo** infos_pre = NULL;
void rocksdb_create_doc(struct bench_info *binfo)
{
  int i;
  docs_pre = (Doc**)calloc(binfo->ndocs, sizeof(Doc*));
#if defined __FDB_BENCH || defined __WT_BENCH || defined __LEVEL_BENCH
  infos_pre = (DocInfo**)calloc(binfo->ndocs, sizeof(DocInfo*));
#endif
  for(i = 0; i < binfo->ndocs; i++){
    //_create_doc(binfo, i, &docs[i], &infos[i], binfo->seq_fill);
#if defined __FDB_BENCH || defined __WT_BENCH || defined __LEVEL_BENCH
    _create_doc(binfo, i, &docs_pre[i], &infos_pre[i], binfo->seq_fill);
#else
    _create_doc(binfo, i, &docs_pre[i], NULL, binfo->seq_fill);
#endif
    //fprintf(stdout, "create %d key %s\n", i, docs_pre[i]->id.buf);
  }
}

void * pop_thread(void *voidargs)
{
    size_t i, k, c, n, db_idx;
    uint64_t counter;
    struct pop_thread_args *args = (struct pop_thread_args *)voidargs;
    struct bench_info *binfo = args->binfo;
    size_t batchsize = args->binfo->pop_batchsize;
    Db *db;
    Doc **docs;
    DocInfo **infos = NULL;

    test_qdepth = 0;
#if defined(__BLOBFS_ROCKS_BENCH)
    // Set up SPDK-specific stuff for this thread
    rocksdb::SpdkInitializeThread();
#endif
    
    // Insertion latency monitoring
    struct stopwatch sw_monitor, sw_latency;
    struct timeval gap;
    struct latency_stat *l_stat = args->l_write;
    int curfile_no, sampling_ms, monitoring;
    uint64_t cur_sample;

    struct timespec t1, t2, t3;
    unsigned long long nanosec1, nanosec2;
    
    if (binfo->latency_rate) {
      sampling_ms = 1000 / binfo->latency_rate;
    } else {
      sampling_ms = 0;
    }
    
    stopwatch_init_start(&sw_monitor);

    if (binfo->pre_gen == 1) {
      assert(binfo->nfiles == 1);
      c = args->n * batchsize;
      while(c < binfo->ndocs){
	if (sampling_ms &&
	    stopwatch_check_ms(&sw_monitor, sampling_ms)) {
	  spin_lock(&l_stat->lock);
	  l_stat->cursor++;
	  if (l_stat->cursor >= binfo->latency_max) {
	    l_stat->cursor = l_stat->cursor % binfo->latency_max;
	    l_stat->nsamples = binfo->latency_max;
	  } else {
	    l_stat->nsamples = l_stat->cursor;
	  }
	  cur_sample = l_stat->cursor;
	  spin_unlock(&l_stat->lock);
	  stopwatch_init_start(&sw_latency);
	  stopwatch_start(&sw_monitor);
	  monitoring = 1;
	} else {
	  monitoring = 0;
	}

	// Aerospike will follow the same path as KV
#if defined __KV_BENCH || defined __AS_BENCH
	// TODO: batch size for kvs
	couchstore_save_documents_test(args->db[0], c, binfo->kv_write_mode, monitoring);
	//submitted++;
#elif defined __ROCKS_BENCH || defined __KVROCKS_BENCH || defined __BLOBFS_ROCKS_BENCH || defined __FDB_BENCH || defined __WT_BENCH || defined __LEVEL_BENCH
	if(c + batchsize >= binfo->ndocs)
	  batchsize = binfo->ndocs - c;
	//fprintf(stdout, "t is %d c is %d batch is %d %s\n", (int)args->n, (int)c, (int)batchsize, docs_pre[c]->id.buf);
#if defined __FDB_BENCH || defined __WT_BENCH 
	couchstore_save_documents(args->db[0], &docs_pre[c], &infos_pre[c], batchsize, binfo->compression);
#else
	couchstore_save_documents(args->db[0], &docs_pre[c], NULL, batchsize, binfo->compression);
#endif
	args->iocount.fetch_add(1, std::memory_order_release);
	
	if (binfo->pop_commit) {
	  couchstore_commit(args->db[0]);
	}
	
#endif
	if (monitoring) {
	  gap = stopwatch_get_curtime(&sw_latency);
	  l_stat->samples[cur_sample] = _timeval_to_us(gap);
	}
	c += binfo->pop_nthreads * batchsize;
      }
      if (!binfo->pop_commit) {
	couchstore_commit(args->db[0]);
      }
    } // end of pre kv generation
    else { // generate kv in line

      if(binfo->kv_write_mode == 1) { // sync mode, use common buffer

	docs = (Doc**)calloc(batchsize, sizeof(Doc*));
#if defined __FDB_BENCH || defined __WT_BENCH 
	infos = (DocInfo**)calloc(batchsize, sizeof(DocInfo*));
#endif
	for (i=0;i<batchsize;++i) {
	  docs[i] = NULL;
#if defined __FDB_BENCH || defined __WT_BENCH 
	  infos[i] = NULL;
	  _create_doc(binfo, i, &docs[i],  &infos[i], binfo->seq_fill);
#else
	  _create_doc(binfo, i, &docs[i], NULL, binfo->seq_fill);
#endif
	}

	if (binfo->seq_fill != 1) {
	  //for (k=args->n; k<binfo->nfiles; k+=binfo->pop_nthreads) {
	  db_idx = args->n / binfo->pop_nthreads;
	  //for (k=args->n / binfo->nfiles; k<binfo->pop_nthreads; k++) {
	  k = args->n % binfo->pop_nthreads;
	  db = args->db[db_idx];
	  //SET_DOC_RANGE(binfo->ndocs, binfo->nfiles, k, c, n);
	  SET_DOC_RANGE(binfo->ndocs, binfo->pop_nthreads, k, c, n);
	  while(c < n) {
	    //counter = 0;
#if time_check	      
	      clock_gettime(CLOCK_REALTIME, &t1);
#endif
	      for (i=c; (i<c+batchsize && i<n); ++i){
#if defined __FDB_BENCH || defined __WT_BENCH 
		_create_doc(binfo, i, &docs[i-c], &infos[i-c], binfo->seq_fill);
#else
		_create_doc(binfo, i, &docs[i-c], NULL, binfo->seq_fill);
#endif
		//counter++;
		//printf("thread %d for db %d start %d end %d create doc --- %d\n", args->n, db_idx, c, n, i);
	      }
#if time_check
	      clock_gettime(CLOCK_REALTIME, &t2);
#endif	
	      /*
	      spin_lock(args->lock);
	      *(args->counter) += counter;
	      spin_unlock(args->lock);
	      */
	      if (sampling_ms &&
		  stopwatch_check_ms(&sw_monitor, sampling_ms)) {
		spin_lock(&l_stat->lock);
		l_stat->cursor++;
		if (l_stat->cursor >= binfo->latency_max) {
		  l_stat->cursor = l_stat->cursor % binfo->latency_max;
		  l_stat->nsamples = binfo->latency_max;
		} else {
		  l_stat->nsamples = l_stat->cursor;
		}
		cur_sample = l_stat->cursor;
		spin_unlock(&l_stat->lock);
		stopwatch_init_start(&sw_latency);
		stopwatch_start(&sw_monitor);
		monitoring = 1;
	      } else {
		monitoring = 0;
	      }

#if defined(__KV_BENCH) || defined (__AS_BENCH)
	      couchstore_save_documents(db, docs, NULL, i-c, monitoring);
	      //completed++;
#else
	      couchstore_save_documents(db, docs, infos, i-c, binfo->compression);
	      //fprintf(stdout, "thread %d for db %d store --- %d\n", args->n, db_idx, i);
	      //couchstore_open_document(db, docs[0]->id.buf, docs[0]->id.size, NULL, binfo->compression);
	      //exit(0);
	      //completed++;
#endif
	      args->iocount.fetch_add(i-c, std::memory_order_release);
#if time_check
	      clock_gettime(CLOCK_REALTIME, &t3);
	      nanosec1 = (t2.tv_sec - t1.tv_sec) * SEC_TO_NANO;
	      if(t2.tv_nsec >= t1.tv_nsec)
		nanosec1 += (t2.tv_nsec - t1.tv_nsec);
	      else
		nanosec1 += nanosec1 - t1.tv_nsec + t2.tv_nsec;

	      nanosec2 = (t3.tv_sec - t2.tv_sec) * SEC_TO_NANO;
	      if(t3.tv_nsec >= t2.tv_nsec)
		nanosec2 += (t3.tv_nsec - t2.tv_nsec);
	      else
		nanosec2 += nanosec2 - t2.tv_nsec + t3.tv_nsec;

	      fprintf(stdout, "%llu %llu\n", nanosec1, nanosec2);
#endif
	      if (binfo->pop_commit) {
		couchstore_commit(db);
	      }

	      if (monitoring) {
		gap = stopwatch_get_curtime(&sw_latency);
		l_stat->samples[cur_sample] = _timeval_to_us(gap);
	      }
	      c = i;
	    } // end of while
	    if (!binfo->pop_commit) {
	      couchstore_commit(db);
	    }
	    //} // end of for
	} // end of random fill
	else { // sequential fill
	  db_idx = args->n / binfo->pop_nthreads;
	  db = args->db[db_idx];
	  /*
	  if (binfo->nfiles > 1){
	    printf("Only support single instance for mutli-thread sequential loading\n");
	    exit(-1);
	  }
	  */

	  //c = args->n * batchsize;
	  c = args->n % binfo->pop_nthreads;
	  while(c < binfo->ndocs) {
#if time_check
	    clock_gettime(CLOCK_REALTIME, &t1);
#endif
	    //counter = 0;
	    for(i = c; (i < c + batchsize && i < binfo->ndocs); ++i){
#if defined __FDB_BENCH || defined __WT_BENCH
	      _create_doc(binfo, i, &docs[i-c], &infos[i-c], 1);
#else
	      _create_doc(binfo, i, &docs[i-c], NULL, 1);
#endif
	      //counter++;
	    }
#if time_check
	    clock_gettime(CLOCK_REALTIME, &t2);
#endif
	    /*
	    spin_lock(args->lock);
	    *(args->counter) += counter;
	    spin_unlock(args->lock);
	    */
	    if (sampling_ms &&
		stopwatch_check_ms(&sw_monitor, sampling_ms)) {
	      spin_lock(&l_stat->lock);
	      l_stat->cursor++;
	      if (l_stat->cursor >= binfo->latency_max) {
		l_stat->cursor = l_stat->cursor % binfo->latency_max;
		l_stat->nsamples = binfo->latency_max;
	      } else {
		l_stat->nsamples = l_stat->cursor;
	      }
	      cur_sample = l_stat->cursor;
	      spin_unlock(&l_stat->lock);
	      stopwatch_init_start(&sw_latency);
	      stopwatch_start(&sw_monitor);
	      monitoring = 1;
	    } else {
	      monitoring = 0;
	    }

#if defined __KV_BENCH || defined __AS_BENCH
	    couchstore_save_documents(db, docs, NULL, i-c, monitoring);
#else
	    couchstore_save_documents(db, docs, infos, i-c, binfo->compression);
	    /*
	    counter = args->iocount.load();
	    if(counter % 10000 == 0 )
	      printf("thread %d update counter %ld\n", args->n, counter);
	    */
#endif
	    args->iocount.fetch_add(i-c, std::memory_order_release);
#if time_check
	    clock_gettime(CLOCK_REALTIME, &t3);
	    nanosec1 = (t2.tv_sec - t1.tv_sec) * SEC_TO_NANO;
	    if(t2.tv_nsec >= t1.tv_nsec)
	      nanosec1 += (t2.tv_nsec - t1.tv_nsec);
	    else
	      nanosec1 += nanosec1 - t1.tv_nsec + t2.tv_nsec;

	    nanosec2 = (t3.tv_sec - t2.tv_sec) * SEC_TO_NANO;
	    if(t3.tv_nsec >= t2.tv_nsec)
	      nanosec2 += (t3.tv_nsec - t2.tv_nsec);
	    else
	      nanosec2 += nanosec2 - t2.tv_nsec + t3.tv_nsec;

	    fprintf(stdout, "%llu %llu\n", nanosec1, nanosec2);
#endif
	    //completed++;
	    //printf("t %d for db %d store %d \n", (int)args->n, db_idx, i);
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
	} // end of sequential fill
	
	// TODO: free batch memory for sync operation
	for (i=0;i<batchsize;++i){
#if defined __KV_BENCH || defined __AS_BENCH
	  // free kv if alloc in KV ways: spdk, ...
	  //couchstore_free_document(docs[i]);
#else
	  if (docs[i]) {
	    if (docs[i]->id.buf) free(docs[i]->id.buf);
	    if (docs[i]->data.buf) free(docs[i]->data.buf);
	    free(docs[i]);
	  }
#if defined __FDB_BENCH || defined __WT_BENCH 
	  if (infos[i]) free(infos[i]);
#endif
#endif
	}
	free(docs);
	if(infos) free(infos);

      } else { // KVS - async mode, alloc/free buffer for each op

	//TODO: multi-device
#if !defined (__KV_BENCH) && !defined (__AS_BENCH)
	fprintf(stdout, "no async write mode for this DB\n");
	exit(0);
#endif
	db_idx = args->n / binfo->pop_nthreads;
	db = args->db[db_idx];
	c = (args->n % binfo->pop_nthreads) * batchsize;

	docs = (Doc**)calloc(batchsize, sizeof(Doc*));
	/*
	for(i = c; (i < c + batchsize && i < binfo->ndocs); ++i){
	  _create_doc(binfo, i, &docs[i-c], NULL, binfo->seq_fill);
	}
	*/
	while(c < binfo->ndocs) {
	  printf("------------- %d\n", c);
	  //docs = (Doc**)calloc(batchsize, sizeof(Doc*));
#if time_check
	  clock_gettime(CLOCK_REALTIME, &t1);
#endif
	  while(test_qdepth.load() >= binfo->queue_depth){
	    usleep(1);
	    printf("exceed --- sleep\n");
	  }
	  //test_qdepth++;
	  test_qdepth.fetch_add(1, std::memory_order_release);
	  printf("now qdepth %d\n", test_qdepth.load());
	  	  
	  for(i = c; (i < c + batchsize && i < binfo->ndocs); ++i){
	    _create_doc(binfo, i, &docs[i-c], NULL, binfo->seq_fill);
	    //fprintf(stdout, "t %d create key %d for db %d --- %s\n", (int)args->n, (int)i, db_idx,  docs[i-c]->id.buf);
	  }
#if time_check
	  clock_gettime(CLOCK_REALTIME, &t2);
#endif
	  if (sampling_ms &&
	      stopwatch_check_ms(&sw_monitor, sampling_ms)) {
	    /*
	    spin_lock(&l_stat->lock);
	    l_stat->cursor++;
	    if (l_stat->cursor >= binfo->latency_max) {
	      l_stat->cursor = l_stat->cursor % binfo->latency_max;
	      l_stat->nsamples = binfo->latency_max;
	    } else {
	      l_stat->nsamples = l_stat->cursor;
	    }
	    cur_sample = l_stat->cursor;
	    spin_unlock(&l_stat->lock);
	    */
	    stopwatch_init_start(&sw_latency);
	    stopwatch_start(&sw_monitor);
	    monitoring = 1;
	  } else {
	    monitoring = 0;
	  }
#if defined __KV_BENCH || defined __AS_BENCH

	  couchstore_save_documents(db, docs, NULL, i-c, monitoring);
#else
	  couchstore_save_documents(db, docs, NULL, i-c, binfo->store_option);  
#endif
#if time_check
	  clock_gettime(CLOCK_REALTIME, &t3);

	  nanosec1 = (t2.tv_sec - t1.tv_sec) * SEC_TO_NANO;
	  if(t2.tv_nsec >= t1.tv_nsec)
	    nanosec1 += (t2.tv_nsec - t1.tv_nsec);
	  else
	    nanosec1 += nanosec1 - t1.tv_nsec + t2.tv_nsec;

	  nanosec2 = (t3.tv_sec - t2.tv_sec) * SEC_TO_NANO;
	  if(t3.tv_nsec >= t2.tv_nsec)
	    nanosec2 += (t3.tv_nsec - t2.tv_nsec);
	  else
	    nanosec2 += nanosec2 - t2.tv_nsec + t3.tv_nsec;

	  fprintf(stdout, "%llu %llu\n", nanosec1, nanosec2);
	  	  
#endif
	  if (monitoring) {
	    /*
	    gap = stopwatch_get_curtime(&sw_latency);
	    l_stat->samples[cur_sample] = _timeval_to_us(gap);
	    */
	  }
	  	  
	  c += binfo->pop_nthreads * batchsize;
	  //usleep(100);
	  /*
	  for (i=c; (i < c + batchsize && i < n); ++i) {
	    free(docs[i-c]);
	  }
	  free(docs);
	  */
	} // end of while
	
      }
    } // end of in line kv generation
    /*
    stopwatch_init_start(&sw_monitor);

    if (binfo->seq_fill != 1) { // JP: leave as it is
      //docs = (Doc**)calloc(batchsize, sizeof(Doc*));
      //infos = (DocInfo**)calloc(batchsize, sizeof(DocInfo*));

      for (i=0;i<batchsize;++i) {
        docs[i] = NULL;
        infos[i] = NULL;
        _create_doc(binfo, i, &docs[i], &infos[i], 0);
      }

      for (k=args->n; k<binfo->nfiles; k+=binfo->pop_nthreads) {
        db = args->db[k];
        SET_DOC_RANGE(binfo->ndocs, binfo->nfiles, k, c, n);

        while(c < n) {
            counter = 0;
	    docs = (Doc**)calloc(batchsize, sizeof(Doc*));
	    infos = (DocInfo**)calloc(batchsize, sizeof(DocInfo*));
	    for (i=c; (i<c+batchsize && i<n); ++i){
	      _create_doc(binfo, i, &docs[i-c], &infos[i-c], 0);
                counter++;
            }
            spin_lock(args->lock);
            *(args->counter) += counter;
            spin_unlock(args->lock);

#if defined(__KV_BENCH) || defined(__KVV_BENCH)
	    couchstore_save_documents(db, docs, infos, i-c, binfo->store_option);
#else
            couchstore_save_documents(db, docs, infos, i-c, binfo->compression);
#endif
            if (binfo->pop_commit) {
                couchstore_commit(db);
            }
            c = i;

#if defined (__KV_BENCH)
	    for (i=c; (i < c + batchsize && i < n); ++i){
	      if(binfo->kv_write_mode == 1)  // sync io
	      {
		couchstore_free_document(docs[i-c]);
	      }
	      else
		free(docs[i-c]);
	      if (infos[i-c]) free(infos[i-c]);
	    }
	    //	    if(binfo->kv_write_mode == 1)
            free(docs);
	    free(infos);
#else
	    for (i=c; (i < c + batchsize && i < n); ++i){
	      couchstore_free_document(docs[i-c]);
	      if (infos[i-c]) free(infos[i-c]);
	    }
	    free(docs);
	    free(infos);
#endif
        }
        if (!binfo->pop_commit) {
            couchstore_commit(db);
        }
      }
    } // end of random fill
    else{ 
      //  seq fill: only support single db/container
      //  KVS: no batch insertion, one insertion at a time per thread
       //  others: batch insertion with multi-thread
       //
      db = args->db[0];
      if (binfo->nfiles > 1){
	printf("Only support single instance for mutli-thread sequential loading\n");
	exit(0);
      }

      c = args->n * batchsize;
      while(c < binfo->ndocs) {
	counter = 0;
	docs = (Doc**)calloc(batchsize, sizeof(Doc*));
	infos = (DocInfo**)calloc(batchsize, sizeof(DocInfo*));
	for(i = c; (i < c + batchsize && i < binfo->ndocs); ++i){
	  _create_doc(binfo, i, &docs[i-c], &infos[i-c], 1);
	  counter++;
	}

	spin_lock(args->lock);
	*(args->counter) += counter;
	spin_unlock(args->lock);

	couchstore_save_documents(db, docs, infos, i-c, binfo->compression);

	if (binfo->pop_commit) {
	  couchstore_commit(db);
	}

#if defined (__KV_BENCH)
	// free memory from callback for aio
	for(i = c; (i < c + batchsize && i < binfo->ndocs); ++i) {
	  if(binfo->kv_write_mode == 1)
	    couchstore_free_document(docs[i-c]);
	  else
	    free(docs[i-c]);
	  if (infos[i-c]) free(infos[i-c]);
	}
	//	if(binfo->kv_write_mode == 1)
	  free(docs);
	free(infos);
#else
	for(i = c; (i < c + batchsize && i < binfo->ndocs); ++i) {
	  couchstore_free_document(docs[i-c]);
	  if (infos[i-c]) free(infos[i-c]);
	}
	free(docs);
	free(infos);
#endif
	c += binfo->pop_nthreads * batchsize;
      }

      if (!binfo->pop_commit) {
	couchstore_commit(db);
      }
    } // end of seq fill

*/
    
#ifndef THREADPOOL
    thread_exit(0);
#endif
    return NULL;
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

    //while(completed.load() < binfo->ndocs * binfo->nfiles)
    while(counter < binfo->ndocs * binfo->nfiles)
    {
      //counter = completed.load();
      for(i = 0; i < binfo->pop_nthreads * binfo->nfiles; i++) {
	cur_iocount = args->pop_args[i].iocount.load();
	counter += cur_iocount;
	//printf("thread %d has count %ld -- total %ld\n", i, cur_iocount, counter);
      }
      if (stopwatch_check_ms(args->sw, print_term_ms)){
	tv = stopwatch_get_curtime(args->sw_long);
	tv_i = stopwatch_get_curtime(args->sw);
	stopwatch_start(args->sw);

	if (++c % 10 == 0 && counter) {
	  elapsed_ms = (uint64_t)tv.tv_sec * 1000 +
	    (uint64_t)tv.tv_usec / 1000;
	  remain_sec = (binfo->ndocs * binfo->nfiles - counter);
	  remain_sec = remain_sec / MAX(1, (counter / elapsed_ms));
	  remain_sec = remain_sec / 1000;
	}
	
	//counter = completed.load();
	iops = (double)counter / (tv.tv_sec + tv.tv_usec/1000000.0);
	iops_i = (double)(counter - counter_prev) /
	  (tv_i.tv_sec + tv_i.tv_usec/1000000.0);
	counter_prev = counter;

	
#if defined (__ROCKS_BENCH) || defined (__BLOBFS_ROCKS_BENCH) || defined (__FDB_BENCH) || defined (__WT_BENCH) || defined (__LEVEL_BENCH)
	if (c % filesize_chk_term == 0) {
	  bytes_written = print_proc_io_stat(buf, 0);
	}
#endif
	
	// elapsed time
	printf("\r[%d.%01d s] ", (int)tv.tv_sec, (int)(tv.tv_usec/100000));
	// # inserted documents
	printf("%" _F64 " / %" _F64, counter, (uint64_t)binfo->ndocs * binfo->nfiles);
	// throughput (average, instant)
	printf(" (%.2f ops, %.2f ops, ", iops, iops_i);
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

    /*
    while (counter < binfo->ndocs) {
        spin_lock(args->lock);
        counter = *(args->counter);
        if (stopwatch_check_ms(args->sw, print_term_ms)) {
            tv = stopwatch_get_curtime(args->sw_long);
            tv_i = stopwatch_get_curtime(args->sw);
            stopwatch_start(args->sw);

            if (++c % 10 == 0 && counter) {
                elapsed_ms = (uint64_t)tv.tv_sec * 1000 +
                             (uint64_t)tv.tv_usec / 1000;
                remain_sec = (binfo->ndocs - counter);
                remain_sec = remain_sec / MAX(1, (counter / elapsed_ms));
                remain_sec = remain_sec / 1000;
            }
            iops = (double)counter / (tv.tv_sec + tv.tv_usec/1000000.0);
            iops_i = (double)(counter - counter_prev) /
                     (tv_i.tv_sec + tv_i.tv_usec/1000000.0);
            counter_prev = counter;

#if defined(__KV_BENCH) || defined(__KVROCKS_BENCH) || defined(__KVV_BENCH)
	    // TBD: get bytes written for kvs
#else
            if (c % filesize_chk_term == 0) {
                bytes_written = print_proc_io_stat(buf, 0);
            }
#endif

            // elapsed time
            printf("\r[%d.%01d s] ", (int)tv.tv_sec, (int)(tv.tv_usec/100000));
            // # inserted documents
            printf("%" _F64 " / %" _F64, counter, (uint64_t)binfo->ndocs);
            // throughput (average, instant)
            printf(" (%.2f ops, %.2f ops, ", iops, iops_i);
            // percentage
            printf("%.1f %%, ", (double)(counter * 100.0 / binfo->ndocs));
            // total amount of data written
            printf("%s) ", print_filesize_approx(bytes_written, buf));
            printf(" (-%d s)", (int)remain_sec);
            spin_unlock(args->lock);
            fflush(stdout);

            //if (log_fp) {
	    if(insert_ops_fp){
                fprintf(insert_ops_fp,
                        "%d.%01d,%.2f,%.2f,%" _F64 ",%" _F64 "\n",
                        (int)tv.tv_sec, (int)(tv.tv_usec/100000),
                        iops, iops_i, (uint64_t)counter, bytes_written);
            }
        } else {
            spin_unlock(args->lock);
            usleep(print_term_ms * 1000);
        }
    }
    */
    return NULL;
}

couchstore_error_t couchstore_kvs_pool_malloc(size_t size_bytes, char **buf);
couchstore_error_t couchstore_kvs_pool_free(int numdocs);
couchstore_error_t couchstore_kvs_free(char *buf);
couchstore_error_t couchstore_kvs_create_doc(int numdocs, int seq_fill, char *keybuf);
couchstore_error_t couchstore_kvs_zalloc(size_t size_bytes, char **buf);
couchstore_error_t couchstore_kvs_free_key();
void _wait_leveldb_compaction(struct bench_info *binfo, Db **db);
void _print_percentile(struct bench_info *binfo,
		       struct latency_stat *l_wstat,
		       struct latency_stat *l_rstat,
		       struct latency_stat *l_dstat,
		       const char *unit, int mode);
char *keybuf = NULL;
#if defined __KV_BENCH || defined __AS_BENCH
struct latency_stat kv_read;
struct latency_stat kv_write;
struct latency_stat kv_delete;
#endif
void population(Db **db, struct bench_info *binfo, struct latency_stat *l_write)
{
    size_t i, j;
    //void *ret[binfo->pop_nthreads+1];
    void *ret[binfo->pop_nthreads * binfo->nfiles + 1];
    char buf[1024];
    //uint64_t counter;
    //thread_t tid[binfo->pop_nthreads+1];
    thread_t tid[binfo->pop_nthreads * binfo->nfiles +1];
    //struct pop_thread_args args[binfo->pop_nthreads + 1];
    struct pop_thread_args args[binfo->pop_nthreads * binfo->nfiles + 1]; // each file/device maps to pop_nthreads
    struct stopwatch sw, sw_long;
    struct timeval tv;
    struct timeval t1, t3;
    unsigned long long totalmicrosecs = 0;
    double iops = 0, latency_ms = 0;
    
    int keylen = (binfo->keylen.type == RND_FIXED)? binfo->keylen.a : 0;
    
    if(binfo->pre_gen == 1) {
      if(binfo->nfiles > 1) {
	printf("pre-genenration of KV data only supports single file/device\n");
	exit(-1);
      }
      fprintf(stdout, "pre gen in memory \n");
#if defined __KV_BENCH || defined __AS_BENCH
#if defined __KV_BENCH
      couchstore_kvs_zalloc(64 * binfo->ndocs, &keybuf);
#else
      keybuf = (char*)calloc(binfo->ndocs, 64); // hard code for max 64-byte key
#endif
      if (!keybuf) {
	fprintf(stderr, "insufficeint memory\n");
	exit(-1);
      }
      for(i = 0; i<binfo->ndocs; i++){
	if(binfo->seq_fill == 0) {
	  keygen_seed2key(&binfo->keygen, i, (keybuf + 64*i), keylen);
	} else {
	  keygen_seqfill(i, keybuf + 64*i, binfo->keylen.a);
	}
      }

      couchstore_kvs_create_doc(binfo->ndocs, binfo->seq_fill, keybuf);
#elif defined (__ROCKS_BENCH) || defined (__KVROCKS_BENCH) || defined (__BLOBFS_ROCKS_BENCH) || defined (__FDB_BENCH) || defined (__WT_BENCH) || defined (__LEVEL_BENCH)
      // pregenerate key-value in memory
      rocksdb_create_doc(binfo);
#endif
      fprintf(stdout, "creation done ---- \n");
    } // pre-generate key-value in memory
    
    gettimeofday(&t1, NULL);

    stopwatch_init(&sw);
    stopwatch_start(&sw);
    stopwatch_init(&sw_long);
    stopwatch_start(&sw_long);
       
#ifdef THREADPOOL
    //threadpool thpool = thpool_init(binfo->pop_nthreads);
    threadpool thpool = thpool_init(binfo->pop_nthreads * binfo->nfiles);
#endif

    pthread_attr_t attr;
    cpu_set_t cpus;
    pthread_attr_init(&attr);
    int db_idx, td_idx, nodeid;
    for (i=0;i<=binfo->pop_nthreads * binfo->nfiles;++i){
        args[i].n = i;
        args[i].db = db;
        args[i].binfo = binfo;
	args[i].sw = &sw;
	args[i].sw_long = &sw_long;
	args[i].iocount = 0;
        //args[i].counter = &counter;
	args[i].l_write = l_write;
        if (i < binfo->pop_nthreads * binfo->nfiles) {
	  db_idx = i / binfo->pop_nthreads;
	  td_idx = i%binfo->pop_nthreads;
	  nodeid = binfo->instances[db_idx].nodeid_load;
	  /*
	  printf("thread %d for db %d %d th numaid %d, core %d\n",
		 (int)i, db_idx, td_idx,
		 binfo->instances[db_idx].nodeid_load,
	  	 binfo->instances[db_idx].coreids_pop[td_idx]);
	  */
	  if(binfo->instances[db_idx].coreids_pop[td_idx] != -1) {
	    CPU_ZERO(&cpus);
	    CPU_SET(binfo->instances[db_idx].coreids_pop[td_idx], &cpus);
	    pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus); 	    
	    //printf("thread %d core %d\n", (int)i, binfo->instances[db_idx].coreids_pop[td_idx]);
	  } else if(nodeid != -1){
	    CPU_ZERO(&cpus);
	    for(j = 0; j < binfo->cpuinfo->num_cores_per_numanodes; j++){
	      CPU_SET(binfo->cpuinfo->cpulist[nodeid][j], &cpus);
	      //printf("%d ", binfo->cpuinfo->cpulist[nodeid][j]);
	    }
	    //printf("\n");
	    pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus); 
	  } else {
	    // if no core & socket is set, use round robin to assign numa node
	    CPU_ZERO(&cpus);
	    nodeid = db_idx % binfo->cpuinfo->num_numanodes;
	    //printf("thread %d use node %d\n", (int)i, nodeid);
	    CPU_ZERO(&cpus);
	    for(j = 0; j < binfo->cpuinfo->num_cores_per_numanodes; j++){
	      CPU_SET(binfo->cpuinfo->cpulist[nodeid][j], &cpus);
	      //printf("%d ", binfo->cpuinfo->cpulist[nodeid][j]);
	    }
	    //printf("\n");
	    pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);
	  }
	  args[i].socketid = nodeid;

#ifdef THREADPOOL
	  //fprintf(stderr, "========== theadpool...\n");
	  thpool_add_work(thpool, pop_thread, &args[i]);
#else
	  //fprintf(stderr, "========== no theadpool %d...\n", i);
	  //blobfs_env_test->StartThread(pop_thread, &args[i]);
	  pthread_create(&tid[i], &attr, pop_thread, &args[i]);
          //thread_create(&tid[i], pop_thread, &args[i]);
#endif
        } else {
	  //if(log_fp)
	  if(insert_ops_fp)
	    fprintf(insert_ops_fp, "time,iops,iops_i,counter\n");
	  args[i].pop_args = args;
	  thread_create(&tid[i], pop_print_time, &args[i]);
        }
    }

    //exit(0);
    for (i=0; i<=binfo->pop_nthreads * binfo->nfiles; ++i){
        thread_join(tid[i], &ret[i]);
    }

    // TODO: check sync mode and rocksdb case
  
#if defined (__KV_BENCH) || defined (__AS_BENCH)
    int32_t count;
    //if(binfo->kv_write_mode == 0) {
      /*
      couchstore_kvs_get_aiocompletion(&count);
      while(count != binfo->ndocs) {
	usleep(1);
	//printf(" ------- %ld\n", (long int)count);
	couchstore_kvs_get_aiocompletion(&count);
      }
            */

      //while(completed.load() != binfo->ndocs) {  // wait until all i/Os are done
      while(completed.load() != binfo->ndocs * binfo->nfiles) {
	usleep(1);
	//fprintf(stdout, "wait for IO --- %d\n", completed.load());
      }

      //}
#elif defined (__ROCKS_BENCH) || defined (__KVROCKS_BENCH) || defined (__BLOBFS_ROCKS_BENCH) || defined (__FDB_BENCH) || defined (__WT_BENCH) || defined (__LEVEL_BENCH)
    int ct = 0;
    /*
    while(completed.load() != binfo->ndocs * binfo->nfiles) {
      usleep(1);
    }
    */
    // Linux + (LevelDB or RocksDB): wait for background compaction
#if defined (__ROCKS_BENCH) || defined (__BLOBFS_ROCKS_BENCH) || defined (__LEVEL_BENCH)
    _wait_leveldb_compaction(binfo, db);
#endif  // __ROCKS_BENCH
#endif

    gettimeofday(&t3, NULL);
    totalmicrosecs = (t3.tv_sec - t1.tv_sec) * 1000000;
    totalmicrosecs += (t3.tv_usec - t1.tv_usec);
    latency_ms = (long double)totalmicrosecs / (long double) binfo->ndocs;
    iops = 1000000 / latency_ms;
    lprintf("\niops %lf latency=%lf\n", iops, latency_ms);

    if(binfo->latency_rate){
     lprintf("\ninsertion latency distribution\n");
#if defined __KV_BENCH || defined __AS_BENCH
#if defined COMP_LAT
     if(binfo->kv_write_mode == 0){
       if(kv_write.samples != NULL)
	 _print_percentile(binfo, &kv_write, NULL, NULL, "us", 1);
     }
     else
       _print_percentile(binfo, l_write, NULL, NULL, "us", 1);
#else
     _print_percentile(binfo, l_write, NULL, NULL, "us", 1);
#endif
#else

     _print_percentile(binfo, l_write, NULL, NULL, "us", 1);
#endif     
    }

    lprintf("\n");

#ifdef THREADPOOL
    //thpool_wait(thpool);
    thpool_destroy(thpool);
#endif
    
    printf("population done ----- %d\n", completed.load());
    /*
#if defined (__ROCKS_BENCH) || defined(__KVROCKS_BENCH)
    for(i = 0; i < binfo->ndocs; i++) {
      if (docs[i]->id.buf) free(docs[i]->id.buf);
      if (infos[i]) free(infos[i]);
    }
    free(docs);
    free(infos);
    
#elif defined(__KV_BENCH)
    if(binfo->seq_fill == 0){
      if(keybuf)
	couchstore_kvs_free(keybuf);
    }
    else
      couchstore_kvs_free_key();
#endif

    if(insert_ops_fp)
      fclose(insert_ops_fp);
    if(insert_latency_fp)
      fclose(insert_latency_fp);
    if (log_fp)
      fclose(log_fp);
    
    exit(0);
    */
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
    }else{
        rb = (double)(binfo->rbatchsize.a + binfo->rbatchsize.b) / 2;
        wb = (double)(binfo->wbatchsize.a + binfo->wbatchsize.b) / 2;
    }
    //if (binfo->write_prob < 100) {
    if (binfo->ratio[1] + binfo->ratio[2] < 100) {
        *prob = (p * rb) / ( (1-p)*wb + p*rb );
    }else{
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
    size_t i;

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
    size_t i, keylen;
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
    int id;
    int coreid;
    int socketid;
    int cur_qdepth;
    int max_qdepth;
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
#ifdef __TIME_CHECK
    struct latency_stat *l_prep;
    struct latency_stat *l_real;
#endif
    uint8_t terminate_signal;
    uint8_t op_signal;
#if defined(__BLOBFS_ROCKS_BENCH)
  rocksdb::Env *blobfs_env;
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
  //  couchstore_free_db(db);
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

    if (binfo->filename[filename_len-1] == '/') {
        filename_len--;
    }
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

    dir_info = opendir(dirname);
    if (dir_info != NULL) {
        while((dir_entry = readdir(dir_info))) {
            if (!strncmp(dir_entry->d_name, filename, strlen(filename))) {
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
    uint64_t op_count_read;
    uint64_t op_count_write;
    uint64_t batch_count;
  std::atomic_uint_fast64_t op_read;
  std::atomic_uint_fast64_t op_write;
  std::atomic_uint_fast64_t op_delete;
  std::atomic_uint_fast64_t batch_count_a;
    spin_t lock;
};
/* move up
struct latency_stat {
    uint64_t cursor;
    uint64_t nsamples;
    uint32_t *samples;
    spin_t lock;
};
*/
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
/*
void print_address(uint64_t address, char *buf, int type)
{
  char cmd[256];
  int ret;
  if (type == 0)
    sprintf(cmd, "echo %ld >> /home/jingpei.yang/test.out", address);
  else
    sprintf(cmd, "echo %s >> /home/jingpei.yang/test.out", buf);
  ret = system(cmd);
}
*/
couchstore_error_t couchstore_kvs_free(char *buf);
couchstore_error_t couchstore_open_document_test(Db *db, int idx, int kv_write_mode, int monitoring);
void * bench_thread(void *voidargs)
{
  struct bench_thread_args *args = (struct bench_thread_args *)voidargs;
  size_t i;
  int j;
  int batchsize;
  int write_mode = 0, write_mode_r;
  int write_chance = 0;
  int commit_mask[args->binfo->nfiles]; (void)commit_mask;
  int curfile_no, sampling_ms, monitoring;
  double prob;
  char curfile[256], keybuf[MAX_KEYLEN];
  uint64_t r, crc, op_med;
  uint64_t op_w, op_r, op_d, op_w_cum, op_r_cum, op_d_cum, op_w_turn, op_r_turn, op_d_turn;
  uint64_t expected_us, elapsed_us, elapsed_sec;
  uint64_t cur_sample;
  Db **db;
  Doc *rq_doc = NULL;
  sized_buf rq_id;
  struct rndinfo write_mode_random, op_dist;
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
  
  struct timespec t1, t2, t3, t4, t5, t6, t7;
  unsigned long long nanosec1, nanosec2, nanosec3, nanosec4, nanosec5, nanosec6;

  size_t round = 0;

#if defined(__BLOBFS_ROCKS_BENCH)
  // Set up SPDK-specific stuff for this thread
  rocksdb::SpdkInitializeThread();
  
#endif
  
#if defined(__KV_BENCH) || defined(__AS_BENCH)
  Doc **rq_doc_kv = NULL;
#elif defined (__FDB_BENCH) || defined(__WT_BENCH)
  DocInfo *rq_info = NULL;
#else
  /* change for multi-device 
  int file_doccount[args->binfo->nfiles], c;
  Doc **rq_doc_arr[args->binfo->nfiles];
  batchsize = MAX_BATCHSIZE;
  for (i=0; i<binfo->nfiles;++i){
    rq_doc_arr[i] = (Doc **)malloc(sizeof(Doc*) * batchsize);
    memset(rq_doc_arr[i], 0, sizeof(Doc*) * batchsize);
  }
  */
  //int file_doccount[args->binfo->nfiles], c;
  batchsize = MAX_BATCHSIZE;
  Doc **rq_doc_arr = (Doc **)malloc(sizeof(Doc*) * batchsize);
  memset(rq_doc_arr, 0, sizeof(Doc*) * batchsize); 
#endif

  rq_id.buf = NULL;
  
  db = args->db;

  op_med = op_w = op_r = op_w_cum = op_r_cum = 0;
  elapsed_us = 0;
  write_mode_random.type = RND_UNIFORM;
  write_mode_random.a = 0;
  write_mode_random.b = 256 * 256;

  //comment out for testing capability
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

  // calculate rw_factor and write probability
  _get_rw_factor(binfo, &prob);

  while(!args->terminate_signal) {
    /*
#if time_check
    if(args->id == 0)
      clock_gettime(CLOCK_REALTIME, &t1);
#endif
    */
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
    if (args->op_signal & OP_REOPEN) { // JP: REOPEN is only set for compaction by couchbase or FDB
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
	
	//sprintf(curfile, "%s%d.%d", binfo->filename, (int)i, 
#if !defined(__KV_BENCH) && !defined(__KVV_BENCH) && !defined(__KVROCKS_BENCH) && !defined(__AS_BENCH)
#if defined (__BLOBFS_ROCKS_BENCH)
	couchstore_open_db_blobfs(args->blobfs_env, curfile,
				  0x0, &args->db[i]);
#else
	couchstore_open_db(curfile, 0x0, &args->db[i]);
#endif //  __BLOBFS_ROCKS_BENCH
#endif
      }
      args->op_signal = 0;
    }

    //if(binfo->write_prob > 100 || binfo->write_prob == 0) {
    if(binfo->ratio[1] + binfo->ratio[2] > 100
       || binfo->ratio[1] + binfo->ratio[2] == 0) { // no ratio control
      switch(args->mode) {
      case 0:
	write_mode_r = get_random(&write_mode_random, rngz, rngz2);
	write_mode = ( (prob * 65536.0) > write_mode_r);
	break;
      case 1:
	write_mode = 1;
	break;
      case 2:
      case 3:
	write_mode = 0;
	break;
      case 4:
	write_mode = 2;
	break;
      case 5:
	usleep(100000);
	continue;
      }
    } else { // with ratio control
      gap = stopwatch_get_curtime(&sw);
      elapsed_us = _timeval_to_us(gap);
      if (elapsed_us == 0) elapsed_us = 1;
      elapsed_sec = elapsed_us / 1000000;
      /*
    spin_lock(&args->b_stat->lock);
    op_w = args->b_stat->op_count_write;
    op_r = args->b_stat->op_count_read;
    spin_unlock(&args->b_stat->lock);
      */
      /*
#if time_check
      if(args->id == 0)
	clock_gettime(CLOCK_REALTIME, &t1);
#endif 
      */
      //op_w = args->b_stat->op_write.load();
      //op_r = args->b_stat->op_read.load();

#if time_check
      if(args->id == 0)
	clock_gettime(CLOCK_REALTIME, &t1);
#endif 

      BDR_RNG_NEXTPAIR;
      switch(args->mode) {
      case 0: // reader+writer
	// decide write or read
	/*
	write_mode_r = get_random(&write_mode_random, rngz, rngz2);
	write_mode = ( (prob * 65536.0) > write_mode_r);
	*/
	/* TODO: for read/write/delete workload
	if (write_chance < binfo->write_prob) {
	  write_chance++;
	  write_mode = 1;
	} else {
	  if(write_chance >= binfo->write_prob && write_chance < 100){
	    write_chance++;
	    write_mode = 0;
	  }
	  else {
	    write_chance = 0;
	    write_mode = 1;
	  }
	}
	*/
	break;
	
      case 1: // writer
      write_mode = 1;
      
      //if (binfo->writer_ops > 0 && binfo->write_prob > 100) {
      if (binfo->writer_ops > 0 && binfo->ratio[1] + binfo->ratio[2] > 100) { // with target ops/sec
	// ops mode
	if (op_w_cum < elapsed_sec * binfo->writer_ops) break;
	op_w_turn = op_w_cum - elapsed_sec*binfo->writer_ops;
	if (op_w_turn < binfo->writer_ops) {
	  expected_us = 1000000 * op_w_turn / binfo->writer_ops;
	} else {
	  expected_us = 1000000;
	}
	expected_us += elapsed_sec * 1000000;
	if (expected_us > elapsed_us) {
	  usleep(expected_us - elapsed_us);
	}
      } else {
	if(round % 10000 == 0){ //100000 to avoid frequent stats check
	  op_w = args->b_stat->op_write.load();
	  op_r = args->b_stat->op_read.load();
	  op_d = args->b_stat->op_delete.load();
	  //if (op_w * 100 > (op_w + op_r) * binfo->write_prob &&
	  //binfo->write_prob <= 100) {
	  if (op_w * 100 > (op_w + op_r + op_d) * (binfo->ratio[1] + binfo->ratio[2]) &&
	      (binfo->ratio[1] + binfo->ratio[2]) <= 100) {
	    usleep(1);
	    continue;
	  }
	}
      }
      
      break;
      
      case 2: // reader & iterator
      case 3:
	write_mode = 0;
	//if (binfo->reader_ops > 0 && binfo->write_prob > 100) {
	if (binfo->reader_ops > 0 && (binfo->ratio[1] + binfo->ratio[2]) > 100) {
	  // ops mode
	  if (op_r_cum < elapsed_sec * binfo->reader_ops) break;
	  op_r_turn = op_r_cum - elapsed_sec*binfo->reader_ops;
	  if (op_r_turn < binfo->reader_ops) {
	    expected_us = 1000000 * op_r_turn / binfo->reader_ops;
	  } else {
	    expected_us = 1000000;
 	  }
	  expected_us += elapsed_sec * 1000000;
	  if (expected_us > elapsed_us) {
	    usleep(expected_us - elapsed_us);
	  }
	} else {
	  if(round % 10000 == 0) { //to avoid frequent stats check
	    op_w = args->b_stat->op_write.load();
	    op_r = args->b_stat->op_read.load();
	    op_d = args->b_stat->op_delete.load();
	    //if (op_w * 100 < (op_w + op_r) * binfo->write_prob &&
	    //		binfo->write_prob <= 100) {
	    if (op_w * 100 < (op_w + op_r + op_d) * (binfo->ratio[1] + binfo->ratio[2]) &&
		(binfo->ratio[1] + binfo->ratio[2]) <= 100) {
	      usleep(1);
	      continue;
	    }
	  }
	}
	break;

      case 4: // deleter
	write_mode = 2;
	if(round % 10000 == 0) { //to avoid frequent stats check
	  op_w = args->b_stat->op_write.load();
	  op_r = args->b_stat->op_read.load();
	  op_d = args->b_stat->op_delete.load();
	  if(op_d * 100 > (op_w + op_r + op_d) * (binfo->ratio[1] + binfo->ratio[2]) &&
	     (binfo->ratio[1] + binfo->ratio[2]) <=100) {
	    usleep(1);
	    continue;
	  }
	}
	break;
      case 5: // dummy thread
	// just sleep
	usleep(100000);
	continue;
      }
    }

    // TODO: randomly set batchsize
    /*
    BDR_RNG_NEXTPAIR;
    if (write_mode) {
      batchsize = get_random(&binfo->wbatchsize, rngz, rngz2);
      if (batchsize <= 0) batchsize = 1;
    }else {
      if (args->mode == 2) {
	// reader
	batchsize = get_random(&binfo->rbatchsize, rngz, rngz2);
	if (batchsize <= 0) batchsize = 1;
      }else {
	// iterator
	batchsize = get_random(&binfo->ibatchsize, rngz, rngz2);
	if (batchsize <= 0) batchsize = 1;
      }
    }
    */
    // set batch size - fixed 1
    batchsize = 1;
    

#if time_check
    if (args->id == 0)
      clock_gettime(CLOCK_REALTIME, &t2);
#endif

    // ramdomly set document distribution for batch
    if (binfo->batch_dist.type == RND_UNIFORM) {
      // uniform distribution
      BDR_RNG_NEXTPAIR;
      op_med = get_random(&binfo->batch_dist, rngz, rngz2);
    } else {
      // zipfian distribution
      BDR_RNG_NEXTPAIR;
      op_med = zipf_rnd_get(zipf);
      op_med = op_med * binfo->batch_dist.b +
	(rngz % binfo->batch_dist.b);
    }
    // disable this when amp_factor is larger than 1.0
    //if (op_med >= binfo->ndocs) op_med = binfo->ndocs - 1;

    if(batchsize > 1){
      // distribution of operations in a batch
      if (binfo->op_dist.type == RND_NORMAL){
	op_dist.type = RND_NORMAL;
	op_dist.a = op_med;
	op_dist.b = binfo->batchrange/2;
      } else {
	op_dist.type = RND_UNIFORM;
	op_dist.a = op_med - binfo->batchrange;
	op_dist.b = op_med + binfo->batchrange;
	if (op_dist.a < 0) op_dist.a = 0;
	if (op_dist.b >= (int)binfo->ndocs) op_dist.b = binfo->ndocs;
      }
    }
#if time_check
    if(args->id == 0)
      clock_gettime(CLOCK_REALTIME, &t3);
#endif
    /*
      // moving down to right before calling the IO
    if (sampling_ms &&
	stopwatch_check_ms(&sw_monitor, sampling_ms)) {
      l_stat = (write_mode)?(args->l_write):(args->l_read);
      spin_lock(&l_stat->lock);
      l_stat->cursor++;
      if (l_stat->cursor >= binfo->latency_max) {
	l_stat->cursor = l_stat->cursor % binfo->latency_max;
	l_stat->nsamples = binfo->latency_max;
      } else {
	l_stat->nsamples = l_stat->cursor;
      }
      cur_sample = l_stat->cursor;
      spin_unlock(&l_stat->lock);
      stopwatch_init_start(&sw_latency);
      stopwatch_start(&sw_monitor);
      monitoring = 1;
    } else {
      monitoring = 0;
    }
    */
    
    if(binfo->pre_gen == 1) { // pre-gen kv
      assert(binfo->nfiles == 1);
      if (write_mode == 1) {
	// write (update)
	if(batchsize == 1)
	  r = op_med;
	else {
	  BDR_RNG_NEXTPAIR;
	  r = get_random(&op_dist, rngz, rngz2);
	}
	//if (r >= binfo->ndocs) r = r % binfo->ndocs;

	if (sampling_ms &&
	    stopwatch_check_ms(&sw_monitor, sampling_ms)) {
	  if(write_mode == 0)
	    l_stat = args->l_read;
	  else if(write_mode == 1)
	    l_stat = args->l_write;
	  else
	    l_stat = args->l_delete;
	  //l_stat = (write_mode)?(args->l_write):(args->l_read);
	  spin_lock(&l_stat->lock);
	  l_stat->cursor++;
	  if (l_stat->cursor >= binfo->latency_max) {
	    l_stat->cursor = l_stat->cursor % binfo->latency_max;
	    l_stat->nsamples = binfo->latency_max;
	  } else {
	    l_stat->nsamples = l_stat->cursor;
	  }
	  cur_sample = l_stat->cursor;
	  spin_unlock(&l_stat->lock);
	  stopwatch_init_start(&sw_latency);
	  stopwatch_start(&sw_monitor);
	  monitoring = 1;
	} else {
	  monitoring = 0;
	}
	
#if defined(__KV_BENCH)
	err = couchstore_save_documents_test(db[0], r, binfo->kv_write_mode, monitoring);
#elif defined (__AS_BENCH)
	err = couchstore_save_documents_test(NULL, r, binfo->kv_write_mode, monitoring);
#elif defined (__FDB_BENCH) || defined (__WT_BENCH)
	err = couchstore_save_documents(db[0], &docs_pre[r], &infos_pre[r], 1, binfo->compression);
	// set mask
	commit_mask[0] = 1;
	
	if (binfo->sync_write) {
	  if (commit_mask[0])
	    couchstore_commit(db[0]);
	}
#else
	// each thread is dedicated for one device/db instance
	err = couchstore_save_documents(db[0], &docs_pre[r], NULL, 1, binfo->compression);
#endif
	
	if (err != COUCHSTORE_SUCCESS) {
	  printf("write error: idx %d \n", (int)r);
	}
	if (err == COUCHSTORE_SUCCESS) {
	  op_w_cum += batchsize;
	  submitted++;
	}
      } else if (args->mode == 2) {
	// read
	if(batchsize == 1)
	  r = op_med;
	else {
	  BDR_RNG_NEXTPAIR;
	  r = get_random(&op_dist, rngz, rngz2);
	}
	//if (r >= binfo->ndocs) r = r % binfo->ndocs;

	if (sampling_ms &&
	    stopwatch_check_ms(&sw_monitor, sampling_ms)) {
	  if(write_mode == 0)
	    l_stat = args->l_read;
	  else if(write_mode == 1)
	    l_stat = args->l_write;
	  else
	    l_stat = args->l_delete;
	  //l_stat = (write_mode)?(args->l_write):(args->l_read);
	  spin_lock(&l_stat->lock);
	  l_stat->cursor++;
	  if (l_stat->cursor >= binfo->latency_max) {
	    l_stat->cursor = l_stat->cursor % binfo->latency_max;
	    l_stat->nsamples = binfo->latency_max;
	  } else {
	    l_stat->nsamples = l_stat->cursor;
	  }
	  cur_sample = l_stat->cursor;
	  spin_unlock(&l_stat->lock);
	  stopwatch_init_start(&sw_latency);
	  stopwatch_start(&sw_monitor);
	  monitoring = 1;
	} else {
	  monitoring = 0;
	}
	
#if defined(__KV_BENCH) 
	err = couchstore_open_document_test(db[0], r, binfo->kv_write_mode, monitoring);
#elif defined (__AS_BENCH)
	err = couchstore_open_document_test(NULL, r, binfo->kv_write_mode, monitoring);
#else
	err = couchstore_open_document(db[0], docs_pre[r]->id.buf,
				       docs_pre[r]->id.size, NULL, binfo->compression);
#endif
	//fprintf(stdout, "read doc %d for key %s\n", (int)r, rq_id.buf);
	if (err == COUCHSTORE_SUCCESS) {
	  op_r_cum += batchsize;
	  submitted++;
	}
      } else if (args->mode == 4) {
	// delete operation
	op_d_cum += batchsize;
	submitted++;
      } else {
	fprintf(stderr, "iteration not implemented yet\n");
      }
    } // end of pre gen kv
    else {
      if(binfo->kv_write_mode == 1) { // sync mode, use common buffer
	if (write_mode == 1) {
#if defined __KV_BENCH || defined __AS_BENCH
	  for (j=0;j<batchsize;++j){
	    if(batchsize == 1)
	      r = op_med;
	    else {
	      BDR_RNG_NEXTPAIR;
	      r = get_random(&op_dist, rngz, rngz2);
	    }
	    //if (r >= binfo->ndocs) r = r % binfo->ndocs;
	    
	    if (sampling_ms &&
		stopwatch_check_ms(&sw_monitor, sampling_ms)) {
	      if(write_mode == 0)
		l_stat = args->l_read;
	      else if(write_mode == 1)
		l_stat = args->l_write;
	      else
		l_stat = args->l_delete;
	      //l_stat = (write_mode)?(args->l_write):(args->l_read);
	      spin_lock(&l_stat->lock);
	      l_stat->cursor++;
	      if (l_stat->cursor >= binfo->latency_max) {
		l_stat->cursor = l_stat->cursor % binfo->latency_max;
		l_stat->nsamples = binfo->latency_max;
	      } else {
		l_stat->nsamples = l_stat->cursor;
	      }
	      cur_sample = l_stat->cursor;
	      spin_unlock(&l_stat->lock);
	      stopwatch_init_start(&sw_latency);
	      stopwatch_start(&sw_monitor);
	      monitoring = 1;
	    } else {
	      monitoring = 0;
	    }
	    	    
#if time_check
	    if(args->id == 0)
	       clock_gettime(CLOCK_REALTIME, &t4);
#endif
	    _create_doc(binfo, r, &rq_doc, NULL, binfo->seq_fill);
#if time_check
	    if(args->id == 0)
	      clock_gettime(CLOCK_REALTIME, &t5);
#endif
#if defined __AS_BENCH
	    err = couchstore_save_document(NULL, rq_doc,
					   NULL, monitoring);
#else
	    err = couchstore_save_document(db[0], rq_doc,
					   NULL, monitoring);
#endif
#if time_check
	       if(args->id == 0)
		 clock_gettime(CLOCK_REALTIME, &t6);
#endif
	  }
	  //submitted += batchsize;

#elif defined(__FDB_BENCH) || defined(__WT_BENCH)
	  // initialize
	  memset(commit_mask, 0, sizeof(int) * binfo->nfiles);

	  uint64_t drange_begin, drange_end, dummy, drange_gap;
	  SET_DOC_RANGE(binfo->ndocs, binfo->nfiles, args->frange_begin,
			drange_begin, dummy);
	  SET_DOC_RANGE(binfo->ndocs, binfo->nfiles, args->frange_end,
			dummy, drange_end);
	  drange_gap = drange_end - drange_begin;
	  (void)dummy;
	  
	  for (j=0;j<batchsize;++j){
	    if(batchsize == 1)
	      r = op_med;
	    else {
	      BDR_RNG_NEXTPAIR;
	      r = get_random(&op_dist, rngz, rngz2);
	    }
	    if (binfo->disjoint_write) {
	      if (r >= drange_gap) {
		r = r % drange_gap;
	      }
	      r += drange_begin;
	    }else {
	      //if (r >= binfo->ndocs) r = r % binfo->ndocs;
	    }

	    curfile_no = GET_FILE_NO(binfo->ndocs, binfo->nfiles, r);
	    _bench_result_doc_hit(result, r);
	    _bench_result_file_hit(result, curfile_no);

	    if (binfo->disjoint_write) {
	      assert(args->frange_begin <= curfile_no &&
		     curfile_no <= args->frange_end);
	    }

	    _create_doc(binfo, r, &rq_doc, &rq_info, binfo->seq_fill);
	    err = couchstore_save_document(db[curfile_no], rq_doc,
					   rq_info, binfo->compression);

	    // set mask
	    commit_mask[curfile_no] = 1;
	  }

	  if (binfo->sync_write) {
	    for (j=0; j<(int)binfo->nfiles; ++j) {
	      if (commit_mask[j]) {
		couchstore_commit(db[j]);
	      }
	    }
	  }
	  
#else  // non KV
	  /* disable for multi-device
	  for (i=0; i<binfo->nfiles;++i){
	    file_doccount[i] = 0;
	  }
	  */
	  for(j = 0; j<batchsize; ++j) {
	    if(batchsize == 1)
	      r = op_med;
	    else {
	      BDR_RNG_NEXTPAIR;
	      r = get_random(&op_dist, rngz, rngz2);
	    }
	    //if (r >= binfo->ndocs) r = r % binfo->ndocs;
	    
	    //fprintf(stdout, "%ld\n", r);	    
	    //curfile_no = GET_FILE_NO(binfo->ndocs, binfo->nfiles, r);
	    /* change for multi-device
	    curfile_no = 0;
	    c = file_doccount[curfile_no]++;
	    _create_doc(binfo, r,
			&rq_doc_arr[curfile_no][c],
			NULL, binfo->seq_fill);
	    commit_mask[curfile_no] = 1;
	    */
	    _create_doc(binfo, r, &rq_doc_arr[j], NULL, binfo->seq_fill);
	    commit_mask[0] = 1;
	    //printf("thread %d - batch %d for key %s\n", args->id, j, rq_doc_arr[j]->id.buf);
	  }

	  if (sampling_ms &&
	      stopwatch_check_ms(&sw_monitor, sampling_ms)) {
	    if(write_mode == 0)
	      l_stat = args->l_read;
	    else if(write_mode == 1)
	      l_stat = args->l_write;
	    else
	      l_stat = args->l_delete;
	    //l_stat = (write_mode)?(args->l_write):(args->l_read);
	    spin_lock(&l_stat->lock);
	    l_stat->cursor++;
	    if (l_stat->cursor >= binfo->latency_max) {
	      l_stat->cursor = l_stat->cursor % binfo->latency_max;
	      l_stat->nsamples = binfo->latency_max;
	    } else {
	      l_stat->nsamples = l_stat->cursor;
	    }
	    cur_sample = l_stat->cursor;
	    spin_unlock(&l_stat->lock);
	    stopwatch_init_start(&sw_latency);
	    stopwatch_start(&sw_monitor);
	    monitoring = 1;
	  } else {
	    monitoring = 0;
	  }
	  /*	  	  
	  for (i=0;i<binfo->nfiles;++i) {
	    if (file_doccount[i] > 0) {
	      err = couchstore_save_documents(db[curfile_no],
					      rq_doc_arr[i],
					      NULL, file_doccount[i],
					      binfo->compression);

	      //usleep(1000);
	      if (err != COUCHSTORE_SUCCESS) {
		fprintf(stderr,"write error: file number %" _F64 "\n", i);
	      }
	    }
	  }
	  */
	  err = couchstore_save_documents(db[0], rq_doc_arr,
					  NULL, batchsize,
					  binfo->compression);
	  if (err != COUCHSTORE_SUCCESS) {
	    fprintf(stderr,"write error: thread %d \n", args->id);
	  }
#endif
	  if (err == COUCHSTORE_SUCCESS) {
	    op_w_cum += batchsize;
	    //printf("thread %d write - %ld\n", args->id, op_w_cum);
	  }
	} else if (args->mode == 2) {
	  // read
	  for (j=0;j<batchsize;++j){
	    if(batchsize == 1)
	      r = op_med;
	    else {
	      BDR_RNG_NEXTPAIR;
	      r = get_random(&op_dist, rngz, rngz2);
	    }
	    //if (r >= binfo->ndocs) r = r % binfo->ndocs;

	    /* disable for multi-device
	    curfile_no = GET_FILE_NO(binfo->ndocs, binfo->nfiles, r);
	    */
#if time_check
	    if(args->id == 0)
	      clock_gettime(CLOCK_REALTIME, &t4);
#endif
	    	    
	    curfile_no = 0;

#if defined(__KV_BENCH)
	    //couchstore_kvs_zalloc(rq_id.size + 1, &(rq_id.buf));
	    if(rq_id.buf == NULL) {
	      couchstore_kvs_zalloc(MAX_KEYLEN, &(rq_id.buf));
	    } else
	      memset(rq_id.buf, 0, MAX_KEYLEN);
#else
	    //rq_id.buf = (char *)malloc(rq_id.size + 1);
	    if(rq_id.buf == NULL) {
	      rq_id.buf = (char *)malloc(64);	      // TODO: fixed this issue
	    } else
	      memset(rq_id.buf, 0, MAX_KEYLEN);
#endif    
	    if (binfo->keyfile) {
	      //rq_id.size = keyloader_get_key(&binfo->kl, r, keybuf);
	      rq_id.size = keyloader_get_key(&binfo->kl, r, rq_id.buf);
	    } else {
	      if (binfo->seq_fill){
		//rq_id.size = keygen_seqfill(r, keybuf, binfo->keylen.a);
		rq_id.size = keygen_seqfill(r, rq_id.buf, binfo->keylen.a);
		//fprintf(stdout, "create key %s --- %d\n", rq_id.buf, (int)rq_id.size);
	      }
	      else
		//rq_id.size = keygen_seed2key(&binfo->keygen, r, keybuf, keylen);
		rq_id.size = keygen_seed2key(&binfo->keygen, r, rq_id.buf, keylen);
	    }
	    /*
#if defined(__KV_BENCH)
	    couchstore_kvs_zalloc(rq_id.size + 1, &(rq_id.buf));
#else
	    rq_id.buf = (char *)malloc(rq_id.size + 1);
#endif
	    memcpy(rq_id.buf, keybuf, rq_id.size);
	    */
	    rq_id.buf[rq_id.size] = 0;

#if time_check
	    if(args->id == 0)
	      clock_gettime(CLOCK_REALTIME, &t5);
#endif
	    
	    if (sampling_ms &&
		stopwatch_check_ms(&sw_monitor, sampling_ms)) {
	      if(write_mode == 0)
		l_stat = args->l_read;
	      else if(write_mode == 1)
		l_stat = args->l_write;
	      else
		l_stat = args->l_delete;
	      //l_stat = (write_mode)?(args->l_write):(args->l_read);
	      spin_lock(&l_stat->lock);
	      l_stat->cursor++;
	      if (l_stat->cursor >= binfo->latency_max) {
		l_stat->cursor = l_stat->cursor % binfo->latency_max;
		l_stat->nsamples = binfo->latency_max;
	      } else {
		l_stat->nsamples = l_stat->cursor;
	      }
	      cur_sample = l_stat->cursor;
	      spin_unlock(&l_stat->lock);
	      stopwatch_init_start(&sw_latency);
	      stopwatch_start(&sw_monitor);
	      monitoring = 1;
	    } else {
	      monitoring = 0;
	    }
	    
	    
#if defined __KV_BENCH
	    err = couchstore_open_document(db[curfile_no], rq_id.buf,
					   rq_id.size, &rq_doc, monitoring);
#elif defined __AS_BENCH
	    err = couchstore_open_document(NULL, rq_id.buf,
					   rq_id.size, &rq_doc, monitoring);
#else
	    err = couchstore_open_document(db[curfile_no], rq_id.buf,
					   rq_id.size, &rq_doc, binfo->compression);

#if defined __FDB_BENCH || defined __LEVEL_BENCH//|| defined __WT_BENCH
	    //TODO: need to fix for other DB
	    
	    rq_doc->id.buf = NULL;
	    couchstore_free_document(rq_doc);
	    rq_doc = NULL;
	    
#endif
#endif
	    
#if time_check
	    if(args->id == 0)
	      clock_gettime(CLOCK_REALTIME, &t6);
#endif
	    
	    if (err != COUCHSTORE_SUCCESS) {
	      printf("read error: document number %" _F64 "\n", r);
	    } else {
	      //rq_doc->id.buf = NULL; // comment out for testing capability
#if defined (__KV_BENCH)
	      //couchstore_free_document(rq_doc);
#else
	      //couchstore_free_document(rq_doc); // not returning anything from DB
#endif
	      rq_doc = NULL;
	    }
	    

#if defined (__KV_BENCH)
	    //couchstore_kvs_free(rq_id.buf);
	    //submitted += batchsize;
#else
	  //free(rq_id.buf);
#endif
	  } // end of batch for loop
	  if (err == COUCHSTORE_SUCCESS) {
	    op_r_cum += batchsize;
	    //printf("thread %d read - %ld\n", args->id, op_r_cum);
	  }
	} else if(args->mode == 4) {
	  // delete operation
	  for (j=0;j<batchsize;++j){
	    if(batchsize == 1)
	      r = op_med;
	    else {
	      BDR_RNG_NEXTPAIR;
	      r = get_random(&op_dist, rngz, rngz2);
	    }

#if defined(__KV_BENCH)
	    //couchstore_kvs_zalloc(rq_id.size + 1, &(rq_id.buf));
	    if(rq_id.buf == NULL) {
	      couchstore_kvs_zalloc(MAX_KEYLEN, &(rq_id.buf));
	    } else
	      memset(rq_id.buf, 0, MAX_KEYLEN);
#else
	    //rq_id.buf = (char *)malloc(rq_id.size + 1);
	    if(rq_id.buf == NULL) {
	      rq_id.buf = (char *)malloc(64);         // TODO: fixed this issue
	    } else
	      memset(rq_id.buf, 0, MAX_KEYLEN);
#endif
	    if (binfo->keyfile) {
	      //rq_id.size = keyloader_get_key(&binfo->kl, r, keybuf);
	      rq_id.size = keyloader_get_key(&binfo->kl, r, rq_id.buf);
	    } else {
	      if (binfo->seq_fill){
		//rq_id.size = keygen_seqfill(r, keybuf, binfo->keylen.a);
		rq_id.size = keygen_seqfill(r, rq_id.buf, binfo->keylen.a);
		//fprintf(stdout, "create key %s --- %d\n", rq_id.buf, (int)rq_id.size);
	      }
	      else
		//rq_id.size = keygen_seed2key(&binfo->keygen, r, keybuf, keylen);
		rq_id.size = keygen_seed2key(&binfo->keygen, r, rq_id.buf, keylen);
	    }
	    rq_id.buf[rq_id.size] = 0;
	    
	    if (sampling_ms &&
		stopwatch_check_ms(&sw_monitor, sampling_ms)) {
	      if(write_mode == 0)
		l_stat = args->l_read;
	      else if(write_mode == 1)
		l_stat = args->l_write;
	      else
		l_stat = args->l_delete;
	      //l_stat = (write_mode)?(args->l_write):(args->l_read);
	      spin_lock(&l_stat->lock);
	      l_stat->cursor++;
	      if (l_stat->cursor >= binfo->latency_max) {
		l_stat->cursor = l_stat->cursor % binfo->latency_max;
		l_stat->nsamples = binfo->latency_max;
	      } else {
		l_stat->nsamples = l_stat->cursor;
	      }
	      cur_sample = l_stat->cursor;
	      spin_unlock(&l_stat->lock);
	      stopwatch_init_start(&sw_latency);
	      stopwatch_start(&sw_monitor);
	      monitoring = 1;
	    } else {
	      monitoring = 0;
	    }
	    
	    err = couchstore_delete_document(db[0], rq_id.buf, rq_id.size, monitoring);

	  } // batch for loop

	  //op_d_cum += batchsize;
	  //printf("delete %d %d\n", batchsize, args->b_stat->op_delete.load());

	} else {
	  // TODO
	  printf("%d %d\n", args->mode, write_mode);
	  exit(0);
	  fprintf(stderr, "iteration not implemented yet\n");
	}
      } else {
	// KVS/AS - async mode, alloc/free buffer for each op
#if !defined __KV_BENCH && !defined __AS_BENCH
	fprintf(stdout, "no async mode for this DB\n");
	exit(0);
#endif
	if(write_mode == 1) {
	  for (j=0;j<batchsize;++j){
	    if(batchsize == 1)
	      r = op_med;
	    else {
	      BDR_RNG_NEXTPAIR;
	      r = get_random(&op_dist, rngz, rngz2);
	    }
	    //if (r >= binfo->ndocs) r = r % binfo->ndocs;
	   
	    _create_doc(binfo, r, &rq_doc, NULL, binfo->seq_fill);
	    
	    if (sampling_ms &&
		stopwatch_check_ms(&sw_monitor, sampling_ms)) {
	      if(write_mode == 0)
		l_stat = args->l_read;
	      else if(write_mode == 1)
		l_stat = args->l_write;
	      else
		l_stat = args->l_delete;
	      //l_stat = (write_mode)?(args->l_write):(args->l_read);
	      spin_lock(&l_stat->lock);
	      l_stat->cursor++;
	      if (l_stat->cursor >= binfo->latency_max) {
		l_stat->cursor = l_stat->cursor % binfo->latency_max;
		l_stat->nsamples = binfo->latency_max;
	      } else {
		l_stat->nsamples = l_stat->cursor;
	      }
	      cur_sample = l_stat->cursor;
	      spin_unlock(&l_stat->lock);
	      stopwatch_init_start(&sw_latency);
	      stopwatch_start(&sw_monitor);
	      monitoring = 1;
	    } else {
	      monitoring = 0;
	    }
	    
#if defined __KV_BENCH
	    	    err = couchstore_save_document(db[0], rq_doc,
	    					   NULL, monitoring);
#elif defined __AS_BENCH
		    err = couchstore_save_document(NULL, rq_doc,
						   NULL, monitoring);
#else
	    fprintf(stderr, "no assync support for other DB\n");
	    exit(0);
#endif
	  }
	  if (err == COUCHSTORE_SUCCESS) {
	    op_w_cum += batchsize;
	    submitted += batchsize;
	  }
	} else if (args->mode == 2) {

	  // read
	  for (j=0;j<batchsize;++j){
	    if(batchsize == 1)
	      r = op_med;
	    else {
	      BDR_RNG_NEXTPAIR;
	      r = get_random(&op_dist, rngz, rngz2);
	    }
	    //if (r >= binfo->ndocs) r = r % binfo->ndocs;

#if defined(__KV_BENCH)
	    if(rq_id.buf == NULL)
	      couchstore_kvs_zalloc(MAX_KEYLEN, &(rq_id.buf));
	    /*
	    if (rq_id.buf == NULL)
	      //rq_id.buf = (char*)malloc(MAX_KEYLEN);
	      couchstore_kvs_zalloc(MAX_KEYLEN, &(rq_id.buf));
	    else
	      memset(rq_id.buf, 0, MAX_KEYLEN);
	    */
#else
	    if(rq_id.buf == NULL)
	      rq_id.buf = (char*)malloc(MAX_KEYLEN);
#endif
	    
	    if (binfo->keyfile) {
	      //rq_id.size = keyloader_get_key(&binfo->kl, r, keybuf);
	      rq_id.size = keyloader_get_key(&binfo->kl, r, rq_id.buf);
	    } else {
	      if (binfo->seq_fill){
		rq_id.size = keygen_seqfill(r, rq_id.buf, binfo->keylen.a);
	      }else
		rq_id.size = keygen_seed2key(&binfo->keygen, r, rq_id.buf, keylen);
	    }
	    /*
#if defined(__KV_BENCH)
	    couchstore_kvs_zalloc(rq_id.size + 1, &(rq_id.buf));
#endif
	    memcpy(rq_id.buf, keybuf, rq_id.size);
	    */
	    rq_id.buf[rq_id.size] = 0;

	    if (sampling_ms &&
		stopwatch_check_ms(&sw_monitor, sampling_ms)) {
	      if(write_mode == 0)
		l_stat = args->l_read;
	      else if(write_mode == 1)
		l_stat = args->l_write;
	      else
		l_stat = args->l_delete;
	      //l_stat = (write_mode)?(args->l_write):(args->l_read);
	      spin_lock(&l_stat->lock); 
	      l_stat->cursor++;
	      if (l_stat->cursor >= binfo->latency_max) {
		l_stat->cursor = l_stat->cursor % binfo->latency_max;
		l_stat->nsamples = binfo->latency_max;
	      } else {
		l_stat->nsamples = l_stat->cursor;
	      }
	      cur_sample = l_stat->cursor;
	      spin_unlock(&l_stat->lock);
	      stopwatch_init_start(&sw_latency);
	      stopwatch_start(&sw_monitor);
	      monitoring = 1;
	    } else {
	      monitoring = 0;
	    }

#if defined (__KV_BENCH)
	    err = couchstore_open_document(db[0], rq_id.buf,
					   rq_id.size, &rq_doc, monitoring);
#else
	    err = couchstore_open_document(NULL, rq_id.buf,
					   rq_id.size, &rq_doc, monitoring);
#endif
	    if (err != COUCHSTORE_SUCCESS) {
	      printf("read error: document number %" _F64 "\n", r);
	    } else {
#if defined (__KV_BENCH)
	      //couchstore_free_document(rq_doc); // not returning anything from DB
#endif
	    }
#if defined (__KV_BENCH) || defined(__AS_BENCH)
	    //couchstore_kvs_free(rq_id.buf); // async mode, free at io_completion
	    submitted += batchsize;
#endif
	  }
	  if (err == COUCHSTORE_SUCCESS) {
	    op_r_cum += batchsize;
	  }
	} else if (args->mode == 4) {
	  // delete operation
          for (j=0;j<batchsize;++j){
	    if(batchsize == 1)
	      r = op_med;
	    else {
	      BDR_RNG_NEXTPAIR;
	      r = get_random(&op_dist, rngz, rngz2);
	    }

#if defined(__KV_BENCH)
	    //couchstore_kvs_zalloc(rq_id.size + 1, &(rq_id.buf));
	    if(rq_id.buf == NULL) {
	      couchstore_kvs_zalloc(MAX_KEYLEN, &(rq_id.buf));
	    } else
	      memset(rq_id.buf, 0, MAX_KEYLEN);
#else
            //rq_id.buf = (char *)malloc(rq_id.size + 1);
	    if(rq_id.buf == NULL) {
	      rq_id.buf = (char *)malloc(64);         // TODO: fixed this issue
	    } else
	      memset(rq_id.buf, 0, MAX_KEYLEN);
#endif
            if (binfo->keyfile) {
              //rq_id.size = keyloader_get_key(&binfo->kl, r, keybuf);
              rq_id.size = keyloader_get_key(&binfo->kl, r, rq_id.buf);
            } else {
              if (binfo->seq_fill){
                //rq_id.size = keygen_seqfill(r, keybuf, binfo->keylen.a);
                rq_id.size = keygen_seqfill(r, rq_id.buf, binfo->keylen.a);
                //fprintf(stdout, "create key %s --- %d\n", rq_id.buf, (int)rq_id.size);
              }
              else
                //rq_id.size = keygen_seed2key(&binfo->keygen, r, keybuf, keylen);
                rq_id.size = keygen_seed2key(&binfo->keygen, r, rq_id.buf, keylen);
            }
            rq_id.buf[rq_id.size] = 0;

            if (sampling_ms &&
	      stopwatch_check_ms(&sw_monitor, sampling_ms)) {
	    if(write_mode == 0)
	      l_stat = args->l_read;
	    else if(write_mode == 1)
	      l_stat = args->l_write;
	    else
	      l_stat = args->l_delete;
	    //l_stat = (write_mode)?(args->l_write):(args->l_read);
	    spin_lock(&l_stat->lock);
	    l_stat->cursor++;
	    if (l_stat->cursor >= binfo->latency_max) {
	    l_stat->cursor = l_stat->cursor % binfo->latency_max;
	    l_stat->nsamples = binfo->latency_max;
	  } else {
	    l_stat->nsamples = l_stat->cursor;
	  }
	    cur_sample = l_stat->cursor;
	    spin_unlock(&l_stat->lock);
	    stopwatch_init_start(&sw_latency);
	    stopwatch_start(&sw_monitor);
	    monitoring = 1;
	  } else {
	    monitoring = 0;
	  }
	    usleep(1000);
            err = couchstore_delete_document(db[0], rq_id.buf, rq_id.size, monitoring);

	  } // batch for loop
	  
	  //op_r_cum += batchsize;
	  submitted += batchsize;
	} else {
	  fprintf(stderr, "iteration not implemented yet\n");
	}
      } // end of async mode
    } // end of online kv generate
    
    if (monitoring) {
      gap = stopwatch_get_curtime(&sw_latency);
      l_stat->samples[cur_sample] = _timeval_to_us(gap);
    }

    if (write_mode == 0) {
      args->b_stat->op_read += batchsize;
      args->b_stat->batch_count_a++;
    } else if (write_mode == 1) {
      /*
      spin_lock(&args->b_stat->lock);
      args->b_stat->op_count_write += batchsize;
      args->b_stat->batch_count++;
      spin_unlock(&args->b_stat->lock);
      */
      args->b_stat->op_write += batchsize;
      args->b_stat->batch_count_a++;
      
    } else if(write_mode == 2) {
      /*
      spin_lock(&args->b_stat->lock);
      args->b_stat->op_count_read += batchsize;
      args->b_stat->batch_count++;
      spin_unlock(&args->b_stat->lock);
      */
      args->b_stat->op_delete += batchsize;
      args->b_stat->batch_count_a++;
    }
#if time_check
    if(args->id == 0) {
      clock_gettime(CLOCK_REALTIME, &t7);

      nanosec1 = (t2.tv_sec - t1.tv_sec) * SEC_TO_NANO;
      if(t2.tv_nsec >= t1.tv_nsec)
	nanosec1 += (t2.tv_nsec - t1.tv_nsec);
      else
	nanosec1 += nanosec1 - t1.tv_nsec + t2.tv_nsec;

      nanosec2 = (t3.tv_sec - t2.tv_sec) * SEC_TO_NANO;
      if(t3.tv_nsec >= t2.tv_nsec)
	nanosec2 += (t3.tv_nsec - t2.tv_nsec);
      else
	nanosec2 += nanosec2 - t2.tv_nsec + t3.tv_nsec;

      nanosec3 = (t4.tv_sec - t3.tv_sec) * SEC_TO_NANO;
      if(t4.tv_nsec >= t3.tv_nsec)
	nanosec3 += (t4.tv_nsec - t3.tv_nsec);
      else
	nanosec3 += nanosec3 - t3.tv_nsec + t4.tv_nsec;

      nanosec4 = (t5.tv_sec - t4.tv_sec) * SEC_TO_NANO;
      if(t5.tv_nsec >= t4.tv_nsec)
	nanosec4 += (t5.tv_nsec - t4.tv_nsec);
      else
	nanosec4 += nanosec4 - t4.tv_nsec + t5.tv_nsec;
      
      nanosec5 = (t6.tv_sec - t5.tv_sec) * SEC_TO_NANO;
      if(t6.tv_nsec >= t5.tv_nsec)
	nanosec5 += (t6.tv_nsec - t5.tv_nsec);
      else
	nanosec5 += nanosec5 - t5.tv_nsec + t6.tv_nsec;

      nanosec6 = (t7.tv_sec - t6.tv_sec) * SEC_TO_NANO;
      if(t7.tv_nsec >= t6.tv_nsec)
	nanosec6 += (t7.tv_nsec - t6.tv_nsec);
      else
	nanosec6 += nanosec6 - t6.tv_nsec + t7.tv_nsec;

      fprintf(stdout, "%llu %llu %llu %llu %llu %llu\n",
	      nanosec1, nanosec2, nanosec3, nanosec4, nanosec5, nanosec6);

    }
#endif
    
    round++;
    
  } // JP: end of big while loop


  // TODO: free batch memory if not pre-gen kv
#if defined(__FDB_BENCH) || defined(__WT_BENCH)
  if (rq_doc) {
    free(rq_doc->id.buf);
    free(rq_doc->data.buf);
    free(rq_doc);
  }
  if (rq_info) {
    free(rq_info);
  }
#elif defined __KV_BENCH || defined __AS_BENCH
  
#else
  // TODO
#endif
  return NULL;
}


/*
void * bench_thread(void *voidargs)
{
    struct bench_thread_args *args = (struct bench_thread_args *)voidargs;
    size_t i;
    int j;
    int batchsize;
    int write_mode = 0, write_mode_r;
    int commit_mask[args->binfo->nfiles]; (void)commit_mask;
    int curfile_no, sampling_ms, monitoring, monitoring2;
    double prob;
    char curfile[256], keybuf[MAX_KEYLEN];
    uint64_t r, crc, op_med;
    uint64_t op_w, op_r, op_w_cum, op_r_cum, op_w_turn, op_r_turn;
    uint64_t expected_us, elapsed_us, elapsed_sec;
    uint64_t cur_sample, cur_sample2;
    Db **db;
    Doc *rq_doc = NULL;
#if defined(__KV_BENCH) || defined(__KVV_BENCH)
    Doc **rq_doc_kv = NULL;
#endif
    sized_buf rq_id;
    struct rndinfo write_mode_random, op_dist;
    struct bench_info *binfo = args->binfo;
#ifdef __BENCH_RESULT
    struct bench_result *result = args->result;
#endif
    struct zipf_rnd *zipf = args->zipf;
    struct latency_stat *l_stat, *l_prep, *l_real;
    struct stopwatch sw, sw_monitor, sw_latency, sw_time, sw_time_latency;
    struct timeval gap;
    couchstore_error_t err = COUCHSTORE_SUCCESS;

#if defined(__FDB_BENCH) || defined(__WT_BENCH) || defined(__KV_BENCH) || defined(__KVV_BENCH)
    DocInfo *rq_info = NULL;
#else
    int file_doccount[args->binfo->nfiles], c;
    Doc **rq_doc_arr[args->binfo->nfiles];
    DocInfo **rq_info_arr[args->binfo->nfiles];
    batchsize = MAX_BATCHSIZE;
    for (i=0; i<binfo->nfiles;++i){
        rq_doc_arr[i] = (Doc **)malloc(sizeof(Doc*) * batchsize);
        rq_info_arr[i] = (DocInfo **)
                         malloc(sizeof(DocInfo*) * batchsize);
        memset(rq_doc_arr[i], 0, sizeof(Doc*) * batchsize);
        memset(rq_info_arr[i], 0, sizeof(DocInfo*) * batchsize);
    }
#endif

    db = args->db;

    op_med = op_w = op_r = op_w_cum = op_r_cum = 0;
    elapsed_us = 0;
    write_mode_random.type = RND_UNIFORM;
    write_mode_random.a = 0;
    write_mode_random.b = 256 * 256;

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
#ifdef __TIME_CHECK
    stopwatch_init_start(&sw_time);
    l_prep = args->l_prep;
    l_real = args->l_real;
#endif

    // calculate rw_factor and write probability
    _get_rw_factor(binfo, &prob);

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
        if (args->op_signal & OP_REOPEN) { // JP: REOPEN is only set for compaction by couchbase or FDB
            for (i=0; i<args->binfo->nfiles; ++i) {
                couchstore_close_db(args->db[i]);
                sprintf(curfile, "%s%d.%d", binfo->filename, (int)i,
                        args->compaction_no[i]);
#if !defined(__KV_BENCH) && !defined(__KVV_BENCH)
                couchstore_open_db(curfile, 0x0, &args->db[i]);
#endif
            }
            args->op_signal = 0;
        }

#ifdef __TIME_CHECK
	if (sampling_ms &&
	    stopwatch_check_ms(&sw_time, sampling_ms)){
	  spin_lock(&l_prep->lock);
	  l_prep->cursor++;
	  if (l_prep->cursor >= binfo->latency_max) {
	    l_prep->cursor = l_prep->cursor % binfo->latency_max;
	    l_prep->nsamples = binfo->latency_max;
	  } else {
	    l_prep->nsamples = l_prep->cursor;
	  }
	  cur_sample2 = l_prep->cursor;
	  spin_unlock(&l_prep->lock);
	  spin_lock(&l_real->lock);
	  l_real->cursor = cur_sample2;
	  l_real->nsamples = (l_real->cursor >= binfo->latency_max) ? binfo->latency_max : l_real->cursor;
	  spin_unlock(&l_real->lock);
	  stopwatch_init_start(&sw_time_latency);
	  stopwatch_start(&sw_time);
	  monitoring2 = 1;
	  //printf("1 --- %ld %d %d \n", (long int)(sw_time.start.tv_sec*1000000 + sw_time.start.tv_usec), (int)cur_sample2, (int)l_real->nsamples);
	} else {
	  monitoring2 = 0;
	}
#endif

        gap = stopwatch_get_curtime(&sw);
        elapsed_us = _timeval_to_us(gap);
        if (elapsed_us == 0) elapsed_us = 1;
        elapsed_sec = elapsed_us / 1000000;

        spin_lock(&args->b_stat->lock);
        op_w = args->b_stat->op_count_write;
        op_r = args->b_stat->op_count_read;
        spin_unlock(&args->b_stat->lock);

        BDR_RNG_NEXTPAIR;
        switch(args->mode) {
        case 0: // reader+writer
            // decide write or read
            write_mode_r = get_random(&write_mode_random, rngz, rngz2);
            write_mode = ( (prob * 65536.0) > write_mode_r);
            break;

        case 1: // writer
            write_mode = 1;

            if (binfo->disjoint_write) {
                if (args->frange_begin > args->frange_end) {
                    // disjoint write is on AND
                    // # writers is greater than # files.
                    // this is a surplus writer .. do nothing
                    usleep(100000);
                    continue;
                }
            }

            if (binfo->writer_ops > 0 && binfo->write_prob > 100) {
                // ops mode
                if (op_w_cum < elapsed_sec * binfo->writer_ops) break;
                op_w_turn = op_w_cum - elapsed_sec*binfo->writer_ops;
                if (op_w_turn < binfo->writer_ops) {
                    expected_us = 1000000 * op_w_turn / binfo->writer_ops;
                } else {
                    expected_us = 1000000;
                }
                expected_us += elapsed_sec * 1000000;
                if (expected_us > elapsed_us) {
                    usleep(expected_us - elapsed_us);
                }
            } else {
                if (op_w * 100 > (op_w + op_r) * binfo->write_prob &&
                    binfo->write_prob <= 100) {
                    usleep(1);
                    continue;
                }
            }

            break;

        case 2: // reader & iterator
        case 3:
            write_mode = 0;
            if (binfo->reader_ops > 0 && binfo->write_prob > 100) {
                // ops mode
                if (op_r_cum < elapsed_sec * binfo->reader_ops) break;
                op_r_turn = op_r_cum - elapsed_sec*binfo->reader_ops;
                if (op_r_turn < binfo->reader_ops) {
                    expected_us = 1000000 * op_r_turn / binfo->reader_ops;
                } else {
                    expected_us = 1000000;
                }
                expected_us += elapsed_sec * 1000000;
                if (expected_us > elapsed_us) {
                    usleep(expected_us - elapsed_us);
                }
            } else {
                if (op_w * 100 < (op_w + op_r) * binfo->write_prob &&
                    binfo->write_prob <= 100) {
                    usleep(1);
                    continue;
                }
            }
            break;

        case 4: // dummy thread
            // just sleep
            usleep(100000);
            continue;
        }

        // randomly set batchsize
        BDR_RNG_NEXTPAIR;
        if (write_mode) {
            batchsize = get_random(&binfo->wbatchsize, rngz, rngz2);
            if (batchsize <= 0) batchsize = 1;
        } else {
            if (args->mode == 2) {
                // reader
                batchsize = get_random(&binfo->rbatchsize, rngz, rngz2);
                if (batchsize <= 0) batchsize = 1;
            } else {
                // iterator
                batchsize = get_random(&binfo->ibatchsize, rngz, rngz2);
                if (batchsize <= 0) batchsize = 1;
            }
        }

        // ramdomly set document distribution for batch
        if (binfo->batch_dist.type == RND_UNIFORM) {
            // uniform distribution
            BDR_RNG_NEXTPAIR;
            op_med = get_random(&binfo->batch_dist, rngz, rngz2);
        } else {
            // zipfian distribution
            BDR_RNG_NEXTPAIR;
            op_med = zipf_rnd_get(zipf);
            op_med = op_med * binfo->batch_dist.b +
                     (rngz % binfo->batch_dist.b);
        }
        if (op_med >= binfo->ndocs) op_med = binfo->ndocs - 1;

        // distribution of operations in a batch
        if (binfo->op_dist.type == RND_NORMAL){
            op_dist.type = RND_NORMAL;
            op_dist.a = op_med;
            op_dist.b = binfo->batchrange/2;
        } else {
            op_dist.type = RND_UNIFORM;
            op_dist.a = op_med - binfo->batchrange;
            op_dist.b = op_med + binfo->batchrange;
            if (op_dist.a < 0) op_dist.a = 0;
            if (op_dist.b >= (int)binfo->ndocs) op_dist.b = binfo->ndocs;
        }

#ifdef __TIME_CHECK
	if(monitoring2) {
	  gap = stopwatch_get_curtime(&sw_time_latency);
	  l_prep->samples[cur_sample2] = _timeval_to_us(gap);
	  //gettimeofday(&sw_time_latency.start, NULL);
	  //printf("2 --- %ld %ld \n", (long int)_timeval_to_us(sw_time_latency.start), (long int)_timeval_to_us(gap));
	  stopwatch_start(&sw_time_latency);
	}
#endif


        if (sampling_ms &&
            stopwatch_check_ms(&sw_monitor, sampling_ms)) {
            l_stat = (write_mode)?(args->l_write):(args->l_read);
            spin_lock(&l_stat->lock);
            l_stat->cursor++;
            if (l_stat->cursor >= binfo->latency_max) {
                l_stat->cursor = l_stat->cursor % binfo->latency_max;
                l_stat->nsamples = binfo->latency_max;
            } else {
                l_stat->nsamples = l_stat->cursor;
            }
            cur_sample = l_stat->cursor;
            spin_unlock(&l_stat->lock);
            stopwatch_init_start(&sw_latency);
            stopwatch_start(&sw_monitor);
            monitoring = 1;
        } else {
            monitoring = 0;
        }

	// JP: per operation initialization done, start real read/write operations 
        if (write_mode) {
            // write (update)
            uint64_t drange_begin, drange_end, dummy, drange_gap;
            SET_DOC_RANGE(binfo->ndocs, binfo->nfiles, args->frange_begin,
                          drange_begin, dummy);
            SET_DOC_RANGE(binfo->ndocs, binfo->nfiles, args->frange_end,
                          dummy, drange_end);
            drange_gap = drange_end - drange_begin;
            (void)dummy;

            // JP: KV bench should follow similar FDB or WT routine here
	    // no multi-file concept 
#if defined(__KV_BENCH)
	    // TBD: write to kvs
	    rq_doc_kv = (Doc **)calloc(batchsize, sizeof(Doc*));
	    for (j=0;j<batchsize;++j){
	      BDR_RNG_NEXTPAIR;
	      r = get_random(&op_dist, rngz, rngz2);
	      if (binfo->disjoint_write) {
		if (r >= drange_gap) {
		  r = r % drange_gap;
		}
		r += drange_begin;
	      } else {
		if (r >= binfo->ndocs) r = r % binfo->ndocs;
	      }

	      curfile_no = GET_FILE_NO(binfo->ndocs, binfo->nfiles, r);
	      _bench_result_doc_hit(result, r);
	      _bench_result_file_hit(result, curfile_no);

	      if (binfo->disjoint_write) {
		assert(args->frange_begin <= curfile_no &&
		       curfile_no <= args->frange_end);
	      }

	      _create_doc(binfo, r, &rq_doc_kv[j], &rq_info, binfo->seq_fill);
	      err = couchstore_save_document(db[curfile_no], rq_doc_kv[j],
					     rq_info, binfo->store_option);

	    }
	    for (j = 0; j < batchsize; j++){
	      if(binfo->kv_write_mode == 1){
		//printf("free %s\n", rq_doc_kv[j]->id.buf);
		couchstore_free_document(rq_doc_kv[j]);
	      }
	      else
		free(rq_doc_kv[j]);
	    }
	    //	    	    if(binfo->kv_write_mode == 1)
	      free(rq_doc_kv);
#elif defined(__FDB_BENCH) || defined(__WT_BENCH) || defined(__KVV_BENCH)
            // initialize
            memset(commit_mask, 0, sizeof(int) * binfo->nfiles);

            for (j=0;j<batchsize;++j){

                BDR_RNG_NEXTPAIR;
                r = get_random(&op_dist, rngz, rngz2);
                if (binfo->disjoint_write) {
                    if (r >= drange_gap) {
                        r = r % drange_gap;
                    }
                    r += drange_begin;
                } else {
                    if (r >= binfo->ndocs) r = r % binfo->ndocs;
                }

                curfile_no = GET_FILE_NO(binfo->ndocs, binfo->nfiles, r);
                _bench_result_doc_hit(result, r);
                _bench_result_file_hit(result, curfile_no);

                if (binfo->disjoint_write) {
                    assert(args->frange_begin <= curfile_no &&
                           curfile_no <= args->frange_end);
                }

                _create_doc(binfo, r, &rq_doc, &rq_info, binfo->seq_fill);
#if defined(__KVV_BENCH)
		err = couchstore_save_document(db[curfile_no], rq_doc,
					       rq_info, binfo->store_option);
#else
                err = couchstore_save_document(db[curfile_no], rq_doc,
                                               rq_info, binfo->compression);
#endif

                // set mask
                commit_mask[curfile_no] = 1;
            }

            if (binfo->sync_write) {
                for (j=0; j<(int)binfo->nfiles; ++j) {
                    if (commit_mask[j]) {
                        couchstore_commit(db[j]);
                    }
                }
            }
#else

            for (i=0; i<binfo->nfiles;++i){
                file_doccount[i] = 0;
            }

            for (j=0;j<batchsize;++j){
                BDR_RNG_NEXTPAIR;
                r = get_random(&op_dist, rngz, rngz2);
                if (binfo->disjoint_write) {
                    if (r >= drange_gap) {
                        r = r % drange_gap;
                    }
                    r += drange_begin;
                } else {
                    if (r >= binfo->ndocs) r = r % binfo->ndocs;
                }

                curfile_no = GET_FILE_NO(binfo->ndocs, binfo->nfiles, r);
                _bench_result_doc_hit(result, r);
                _bench_result_file_hit(result, curfile_no);

                if (binfo->disjoint_write) {
                    assert(args->frange_begin <= curfile_no &&
                           curfile_no <= args->frange_end);
                }

		//print_address(r);
                c = file_doccount[curfile_no]++;
                _create_doc(binfo, r,
                            &rq_doc_arr[curfile_no][c],
                            &rq_info_arr[curfile_no][c], binfo->seq_fill);
		//print_address(0, rq_doc_arr[0][0]->id.buf, 1);

                // set mask
                commit_mask[curfile_no] = 1;

            }

            for (i=0;i<binfo->nfiles;++i) {
                if (file_doccount[i] > 0) {
                    err = couchstore_save_documents(db[curfile_no],
                                                    rq_doc_arr[i],
                                                    rq_info_arr[i],
                                                    file_doccount[i],
                                                    binfo->compression);
                    if (err != COUCHSTORE_SUCCESS) {
                        printf("write error: file number %" _F64 "\n", i);
                    }
#if defined(__COUCH_BENCH)
                    if (err == COUCHSTORE_SUCCESS) {
                        err = couchstore_commit(db[curfile_no]);
                        if (err != COUCHSTORE_SUCCESS) {
                            printf("commit error: file number %" _F64 "\n", i);
                        }
                    }
#endif
                }
            }
#endif
            if (err == COUCHSTORE_SUCCESS) {
                op_w_cum += batchsize;
            }

        } else if (args->mode == 2) {
            // read
            for (j=0;j<batchsize;++j){

                BDR_RNG_NEXTPAIR;
                r = get_random(&op_dist, rngz, rngz2);
                if (r >= binfo->ndocs) r = r % binfo->ndocs;
                curfile_no = GET_FILE_NO(binfo->ndocs, binfo->nfiles, r);
                _bench_result_doc_hit(result, r);
                _bench_result_file_hit(result, curfile_no);

                if (binfo->keyfile) {
                    rq_id.size = keyloader_get_key(&binfo->kl, r, keybuf);
                } else {
		  if (binfo->seq_fill){
		    rq_id.size = keygen_seqfill(r, keybuf);
		  }
		  else
                    rq_id.size = keygen_seed2key(&binfo->keygen, r, keybuf);
                }
		//printf("read ---- %s\n", keybuf);
                // as WiredTiger uses C-style string,
                // add NULL character at the end.
#if defined(__KV_BENCH)
		couchstore_kvs_zalloc(rq_id.size + 1, &(rq_id.buf));
#else
                rq_id.buf = (char *)malloc(rq_id.size + 1);
#endif
                memcpy(rq_id.buf, keybuf, rq_id.size);
                rq_id.buf[rq_id.size] = 0;

		// JP: open_doc is a read/get op, should only have 1 db(container) for kvs 
                err = couchstore_open_document(db[curfile_no], rq_id.buf,
                                               rq_id.size, &rq_doc, binfo->compression);
		//printf("free --- %s\n", rq_id.buf);
                if (err != COUCHSTORE_SUCCESS) {
                    printf("read error: document number %" _F64 "\n", r);
                } else {
		  rq_doc->id.buf = NULL;
#if defined (__KV_BENCH)
		  if(binfo->kv_write_mode == 1) { // sync io
		    couchstore_free_document(rq_doc);
		    rq_doc = NULL;
		  }
#else
		  couchstore_free_document(rq_doc);
                  rq_doc = NULL;
#endif
                }
#if defined (__KV_BENCH)
		if(binfo->kv_write_mode == 1){
		  couchstore_kvs_free(rq_id.buf);
		}
#else
                free(rq_id.buf);
#endif
            }
            if (err == COUCHSTORE_SUCCESS) {
                op_r_cum += batchsize;
            }

        } else {
            // iterate
            // JP: different iteration operation for kvs, could be done in phase 2
            struct iterate_args i_args;

            r = op_med;
            curfile_no = GET_FILE_NO(binfo->ndocs, binfo->nfiles, r);

            if (binfo->keyfile) {
                rq_id.size = keyloader_get_key(&binfo->kl, r, keybuf);
            } else {
	      if(binfo->seq_fill)
		rq_id.size = keygen_seqfill(r, keybuf);
	      else
                rq_id.size = keygen_seed2key(&binfo->keygen, r, keybuf);
            }

            // as WiredTiger uses C-style string,
            // add NULL character at the end.
            rq_id.buf = (char *)malloc(rq_id.size + 1);
            memcpy(rq_id.buf, keybuf, rq_id.size);
            rq_id.buf[rq_id.size] = 0;

            i_args.batchsize = batchsize;
            i_args.counter = 0;
            err = couchstore_walk_id_tree(db[curfile_no], &rq_id, 1,
                                          iterate_callback, (void *)&i_args);
            free(rq_id.buf);

            batchsize = i_args.counter;
            op_r_cum += batchsize;
        }

        if (monitoring) {
            gap = stopwatch_get_curtime(&sw_latency);
            l_stat->samples[cur_sample] = _timeval_to_us(gap);
        }

#ifdef __TIME_CHECK
        if(monitoring2) {
          gap = stopwatch_get_curtime(&sw_time_latency);
          l_real->samples[cur_sample2] = _timeval_to_us(gap);
	  //gettimeofday(&sw_time_latency.start, NULL);
	  //printf("3 --- %ld %ld \n", (long int)_timeval_to_us(sw_time_latency.start), (long int)_timeval_to_us(gap));
        }
#endif

        if (write_mode) {
            spin_lock(&args->b_stat->lock);
            args->b_stat->op_count_write += batchsize;
            args->b_stat->batch_count++;
            spin_unlock(&args->b_stat->lock);
        } else {
            spin_lock(&args->b_stat->lock);
            args->b_stat->op_count_read += batchsize;
            args->b_stat->batch_count++;
            spin_unlock(&args->b_stat->lock);
        }
    } // JP: end of big while loop


#if defined(__KV_BENCH)
    if(binfo->kv_write_mode == 1) {
      if (rq_doc) {
	couchstore_kvs_free(rq_doc->id.buf);
	couchstore_kvs_free(rq_doc->data.buf); 
	free(rq_doc); 
      }
    }
    if (rq_info) {
      free(rq_info);
    }
#elif defined(__FDB_BENCH) || defined(__WT_BENCH) || defined (__KVV_BENCH)
    if (rq_doc) {
        free(rq_doc->id.buf);
        free(rq_doc->data.buf);
        free(rq_doc);
    }
    if (rq_info) {
        free(rq_info);
    }
#else
    for (i=0;i<binfo->nfiles;++i) {
        for (j=0;j<MAX_BATCHSIZE;++j){
            if (rq_doc_arr[i][j]) {
                free(rq_doc_arr[i][j]->id.buf);
                free(rq_doc_arr[i][j]->data.buf);
                free(rq_doc_arr[i][j]);
            }
            if (rq_info_arr[i][j]) {
                free(rq_info_arr[i][j]);
            }
        }
        free(rq_doc_arr[i]);
        free(rq_info_arr[i]);
    }
#endif

    return NULL;
}
*/



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
				       int write_mode,
				       int pre_gen);
// non-standard functions for extension for kvs & kvv
couchstore_error_t couchstore_open_device(const char *dev_path,
					  int32_t *dev_id,
					  int write_mode,
					  int num_devices,
					  int pre_gen,
					  mempool_t *pool);
couchstore_error_t couchstore_close_device(int32_t dev_id);
couchstore_error_t couchstore_exit_env(void);
couchstore_error_t couchstore_get_container_count(int32_t dev_id, int32_t *count);
couchstore_error_t couchstore_remove_containers(int32_t dev_id);
couchstore_error_t couchstore_kvs_set_queuedepth(int queue_depth);
couchstore_error_t couchstore_kvs_set_aiothreads(int aio_threads);
couchstore_error_t couchstore_kvs_set_coremask(char *core_ids);
couchstore_error_t couchstore_kvs_get_aiocompletion(int32_t *count);
couchstore_error_t couchstore_kvs_reset_aiocompletion();
couchstore_error_t couchstore_kvs_malloc(size_t size_bytes, void **buf);
couchstore_error_t couchstore_kvs_zalloc(size_t size_bytes, char **buf);
couchstore_error_t couchstore_kvs_free(char *buf);
couchstore_error_t couchstore_kvs_free_key();
couchstore_error_t couchstore_kvs_pool_malloc(size_t size_bytes, char **buf);
//couchstore_error_t couchstore_kvs_pool_free(char *buf);
couchstore_error_t couchstore_kvs_pool_free(int numdocs);
couchstore_error_t couchstore_kvs_create_doc(int numdocs, int seq_fill, char *keybuf);
couchstore_error_t couchstore_save_documents_test(Db *db, int idx, int kv_write_mode, int monitoring);

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

uint64_t get_nand_written(struct bench_info *binfo)
{
  char cmd[256];
  int ret;
  uint64_t temp;

  sprintf(cmd, "nvme smart-log %s | grep nand_data_units_written | awk '{print $4}' | tr -d '()' > tmp.txt", binfo->device_path);
  //sprintf(cmd, "nvme get-log %s -i 0xc1 -l 128|  grep nand_writes | awk '{print $4}'| tr -d '()' > tmp.txt", binfo->device_path);
  ret = system(cmd);
  FILE *fp = fopen("tmp.txt", "r");
  while(!feof(fp)) {
    ret = fscanf(fp, "%lu", &temp);
  }

  fclose(fp);
  return temp;

}

void do_bench(struct bench_info *binfo)
{
  BDR_RNG_VARS;
  int i, j, ret; (void)j; (void)ret;
  int curfile_no, compaction_turn;
  int compaction_no[binfo->nfiles], total_compaction = 0;
  int cur_compaction = -1;
  int bench_threads, singledb_thread_num, first_db_idx;
  uint64_t op_count_read, op_count_write, op_count_delete, display_tick = 0;
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
  Db *db[binfo->nfiles];
#ifdef __FDB_BENCH
  Db *info_handle[binfo->nfiles];
#endif
#if defined(__KV_BENCH) || defined(__KVV_BENCH) || defined(__AS_BENCH)
  int32_t dev_id[binfo->nfiles];
  //  int32_t container_count;
#endif
#if defined(__AS_BENCH)
  // hosts & port
  const char *host;
  uint16_t port;
#endif
#ifdef __BLOBFS_ROCKS_BENCH
  rocksdb::Env *blobfs_env;
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
  struct latency_stat l_read, l_write, l_delete;
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

  // latency stat init
  l_read.cursor = l_write.cursor = l_delete.cursor = 0;
  l_read.nsamples = l_write.nsamples = l_delete.nsamples = 0;
  l_read.samples = (uint32_t*)malloc(sizeof(uint32_t) * binfo->latency_max);
  l_write.samples = (uint32_t*)malloc(sizeof(uint32_t) * binfo->latency_max);
  l_delete.samples = (uint32_t*)malloc(sizeof(uint32_t) * binfo->latency_max);
  spin_init(&l_read.lock);
  spin_init(&l_write.lock);
  spin_init(&l_delete.lock);

#if !defined(__COUCH_BENCH) && !defined(__KV_BENCH) && !defined(__KVROCKS_BENCH) && !defined(__KVV_BENCH) && !defined(__AS_BENCH)
  // all but Couchstore: set buffer cache size
  couchstore_set_cache(binfo->cache_size);
  // set compression option
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
#if defined(__ROCKS_BENCH) || defined(__BLOBFS_ROCKS_BENCH) || defined (__LEVEL_BENCH)
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
#if defined(__KV_BENCH) || defined(__KVV_BENCH)
  if(binfo->kv_write_mode == 0) { // set aio context
    // set queuedepth, aiothreads, coremask
    couchstore_kvs_set_queuedepth(binfo->queue_depth);
    couchstore_kvs_set_aiothreads(binfo->aiothreads_per_device);
    couchstore_kvs_set_coremask(binfo->core_ids);
  }
  // open device only once
  // TODO : multi-device suppor
  couchstore_open_device(binfo->device_path, dev_id, binfo->kv_write_mode, binfo->num_devices, binfo->pre_gen, binfo->keypool);
  printf("device open with ID %d, %d\n", dev_id[0], dev_id[1]);
#endif
#if defined(__AS_BENCH)
  //TODO: other initialization for aerospike
  couchstore_as_setup(binfo->device_path, binfo->as_port, binfo->loop_capacity, binfo->name_space, binfo->kv_write_mode, binfo->pre_gen);
#endif

  
#if defined(__BLOBFS_ROCKS_BENCH)
  NewSpdkEnv(&blobfs_env, binfo->filename, binfo->spdk_conf_file, binfo->spdk_bdev, binfo->spdk_cache_size);
  //blobfs_env_test = blobfs_env;
  if(blobfs_env == NULL) {
    fprintf(stderr, "Could not load SPDK blobfs - check that SPDK mkfs was run "
	    "against block device %s.\n", binfo->spdk_bdev);
    exit(1);
  }
#endif
  
  if (binfo->initialize) {
    // === initialize and populate files ========
#if defined(__KV_BENCH) || defined (__KVV_BENCH) || defined(__KVROCKS_BENCH)
    // do nothing
#elif defined(__AS_BENCH)
    // check whether need to clear data for server?
#else
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
    /*
    str = _get_dirname(binfo->filename, bodybuf);
    printf("str is %s\n", str);
    if (str) {
      if (!_does_file_exist(str)) {
	sprintf(cmd, "mkdir -p %s > errorlog.txt", str);
	ret = system(cmd);
      }
    }
    */
    if (!_does_file_exist(binfo->filename)){
      printf("create %s\n", binfo->filename);
      sprintf(cmd, "mkdir -p %s > errorlog.txt", binfo->filename);
      ret = system(cmd);
    }

#endif

    // for insert_and_update workload, no need for pre-population
    if (!binfo->insert_and_update) {
#if defined(__WT_BENCH)
      // WiredTiger: open connection
      couchstore_set_idx_type(binfo->wt_type);
      couchstore_open_conn((char*)binfo->filename);
#endif
#if defined(__LEVEL_BENCH) || defined(__ROCKS_BENCH) || defined(__BLOBFS_ROCKS_BENCH)
      // LevelDB, RocksDB: set WBS size
      couchstore_set_wbs_size(binfo->wbs_init);
#endif

#if defined(__BLOBFS_ROCKS_BENCH)
      // As of now, BlobFS has no directory support, so it has not been tested
      // with multiple DBs
      if ((int) binfo->nfiles > 1)
	printf("WARN: BlobFS RocksDB has not been tested with nfiles > 1\n");
#endif
	
      for (i=0; i<(int)binfo->nfiles; ++i){
	compaction_no[i] = 0;
	/*
	sprintf(curfile, "%s%d.%d", binfo->init_filename, i,
			compaction_no[i]);
	*/
	sprintf(curfile, "%s/%d", binfo->init_filename, i);
	
	printf("db %d name is %s\n", i, curfile);

	// JP: open/create container here for kvs: filenname = container name
	// for first time population, need to open device first for kvs
#if defined(__KV_BENCH)
	// TODO: multi-device 
	couchstore_open_db_kvs(dev_id[i], curfile, COUCHSTORE_OPEN_FLAG_CREATE, &db[i]);
#elif defined(__KVROCKS_BENCH)
	couchstore_open_db(binfo->device_path, COUCHSTORE_OPEN_FLAG_CREATE, &db[i]);
#elif defined(__KVV_BENCH)
	// do nothing
#elif defined(__BLOBFS_ROCKS_BENCH)
	couchstore_open_db_blobfs(blobfs_env, curfile, COUCHSTORE_OPEN_FLAG_CREATE, &db[i]);	
#elif defined(__AS_BENCH)
	//TODO: open aerospike connection
	couchstore_open_db(curfile, COUCHSTORE_OPEN_FLAG_CREATE, &db[i]);	
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
#if defined(__LEVEL_BENCH) || defined(__ROCKS_BENCH) || defined(__KVROCKS_BENCH) || defined(__BLOBFS_ROCKS_BENCH)
	if (!binfo->pop_commit) {
	  couchstore_set_sync(db[i], 0);
	}
#endif
      }
      //exit(0);
      stopwatch_start(&sw);
#if defined (__KV_BENCH) || defined (__ROCKS_BENCH) || defined (__KVROCKS_BENCH) || defined (__BLOBFS_ROCKS_BENCH) || defined (__FDB_BENCH) || defined (__WT_BENCH) || defined (__LEVEL_BENCH) || defined (COUCH_BENCH) || defined (__AS_BENCH)
      population(db, binfo, &l_write);
      exit(0);
#endif

#if  defined(__PRINT_IOSTAT) && \
  (defined(__LEVEL_BENCH) || defined(__ROCKS_BENCH)  || defined(__BLOBFS_ROCKS_BENCH))
      // Linux + (LevelDB or RocksDB): wait for background compaction
      gap = stopwatch_stop(&sw);
      LOG_PRINT_TIME(gap, " sec elapsed\n");
      //print_proc_io_stat(cmd, 1);
#endif // __PRINT_IOSTAT && (__LEVEL_BENCH || __ROCKS_BENCH)
      
#if !defined(__KV_BENCH) && !defined (__KVROCKS_BENCH) && !defined(__AS_BENCH)
      if (binfo->sync_write) {
	//lprintf("flushing disk buffer.. ");
	fflush(stdout);
	sprintf(cmd, "sync");
	ret = system(cmd);
	//lprintf("done\n");
	fflush(stdout);
      }
#endif

      written_final = written_init = print_proc_io_stat(cmd, 1);
      written_prev = written_final;
      
      // TBD:check whether needs to close db here for kvs
      // TODO: check for aerospike 
#if defined(__ROCKS_BENCH) || defined (__FDB_BENCH) || defined (__BLOBFS_ROCKS_BENCH) || defined (__WT_BENCH) || defined (__LEVEL_BENCH)
      for (i=0; i<(int)binfo->nfiles; ++i){
	couchstore_close_db(db[i]);
      }

#endif
      gap = stopwatch_stop(&sw);
#if defined(__LEVEL_BENCH) || defined(__ROCKS_BENCH) || defined(__BLOBFS_ROCKS_BENCH)
#if defined(__PRINT_IOSTAT)
      gap.tv_sec -= 3; // subtract waiting time
#endif // __PRINT_IOSTAT
#endif // __LEVEL_BENCH || __ROCKS_BENCH
      gap_double = gap.tv_sec + (double)gap.tv_usec / 1000000.0;
      LOG_PRINT_TIME(gap, " sec elapsed ");
      lprintf("(%.2f ops/sec)\n", binfo->ndocs / gap_double);
      //exit(0);
    } else {
      printf("insert and update: no need for population \n");
#if defined(__KV_BENCH)
      //TODO: multi-device 
      couchstore_open_db_kvs(dev_id[0], curfile, COUCHSTORE_OPEN_FLAG_CREATE, &db[i]);
#endif
#if defined(__KVROCKS_BENCH)
      couchstore_open_db(binfo->device_path, COUCHSTORE_OPEN_FLAG_CREATE, &db[i]);
#endif
#if defined (__AS_BENCH)
      //TODO: check whether need to open aerospike connection
      // here or later
#endif
      if(binfo->pre_gen == 1) {
	if(binfo->nfiles > 1){
	  fprintf(stderr, "pre-genenration of KV data only supports single file/device\n");
	  exit(-1);
	}
	int keylen = (binfo->keylen.type == RND_FIXED)? binfo->keylen.a : 0; 
#if defined(__KV_BENCH) || defined(__AS_BENCH)
#if defined(__KV_BENCH)
	couchstore_kvs_zalloc(32 * binfo->ndocs, &keybuf);
#else
	keybuf = (char *)calloc(binfo->ndocs, 32);
#endif
	if (!keybuf) {
	  fprintf(stderr, "insufficeint memory\n");
	  exit(-1);
	}
	for(i = 0; i<binfo->ndocs; i++){
	  if(binfo->seq_fill == 0) {
	    keygen_seed2key(&binfo->keygen, i, (keybuf + 32*i), keylen);
	  }else {
	    keygen_seqfill(i, keybuf + 32*i, binfo->keylen.a);
	  }
	}
	couchstore_kvs_create_doc(binfo->ndocs, binfo->seq_fill, keybuf);
#else
	rocksdb_create_doc(binfo);
#endif
      }
    } // end of insert & update
  }else {
    // === load existing files =========
    stopwatch_start(&sw);
    for (i=0; i<(int)binfo->nfiles; ++i) {
      compaction_no[i] = 0;
    }
    if(binfo->pre_gen == 1) {
#if defined(__ROCKS_BENCH) || defined(__KVROCKS_BENCH) || defined (__BLOBFS_ROCKS_BENCH) || defined (__FDB_BENCH) || defined (__WT_BENCH) || defined(__LEVEL_BENCH) || defined(__AS_BENCH)
      rocksdb_create_doc(binfo);
#endif
    }
    // do nothing for kvs, will open container when starting benchmarking
#if defined(__WT_BENCH)
    // for WiredTiger: open connection
    couchstore_open_conn((char*)binfo->filename);
#elif defined (__ROCKS_BENCH) || defined (__FDB_BENCH) || defined (__LEVEL_BENCH)
    _dir_scan(binfo, compaction_no);
#endif
  } // load existing files

  
  // ==== perform benchmark ====
  lprintf("\nbenchmark\n");
  lprintf("opening DB instance .. \n");

  compaction_turn = 0;

  completed = 0;
  submitted = 0;
  //fprintf(stdout, "reset to %d\n", completed.load());

#if defined(__ROCKS_BENCH) || defined(__BLOBFS_ROCKS_BENCH) || defined (__LEVEL_BENCH)
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
      zipf_rnd_init(&zipf,  (binfo->ndocs + binfo->nops * binfo->ratio[2] * 2) / binfo->batch_dist.b,
		    binfo->batch_dist.a/100.0, 1024*1024);
    } else {
      zipf_rnd_init(&zipf, binfo->ndocs * binfo->amp_factor / binfo->batch_dist.b,
		    binfo->batch_dist.a/100.0, 1024*1024);
    }
  }

  // set signal handler
  old_handler = signal(SIGINT, signal_handler);

  // bench stat init
  b_stat.batch_count = 0;
  b_stat.op_count_read = b_stat.op_count_write = 0;
  b_stat.op_read = b_stat.op_write = b_stat.op_delete = 0; // atomic
  b_stat.batch_count_a = 0; //atomic
  prev_op_count_read = prev_op_count_write = prev_op_count_delete = 0;
  spin_init(&b_stat.lock);


    // latency stat init
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
  
#if defined(__KV_BENCH) || defined(__AS_BENCH)
  // latency check
#if defined COMP_LAT
  if(binfo->kv_write_mode == 0) {
    spin_lock(&kv_read.lock);
    kv_read.cursor = 0;
    kv_read.nsamples = 0;
    spin_unlock(&kv_read.lock);
    spin_lock(&kv_write.lock);
    kv_write.cursor = 0;
    kv_write.nsamples = 0;
    spin_unlock(&kv_write.lock);
    spin_lock(&kv_delete.lock);
    kv_delete.cursor = 0;
    kv_delete.nsamples = 0;
    spin_unlock(&kv_delete.lock);
  }
#endif
#endif
    //l_read.cursor = l_write.cursor = 0;
    //l_read.nsamples = l_write.nsamples = 0;
    //l_read.samples = (uint32_t*)malloc(sizeof(uint32_t) * binfo->latency_max);
    //l_write.samples = (uint32_t*)malloc(sizeof(uint32_t) * binfo->latency_max);
    //spin_init(&l_read.lock);
    //spin_init(&l_write.lock);


  // thread args
  if(binfo->insert_and_update)
  {
    // no readers for update_and_insert mixed workload
    binfo->nreaders = binfo->niterators = 0;
  }
  if (binfo->nreaders + binfo->niterators + binfo->nwriters + binfo->ndeleters == 0){
    // create a dummy thread
    bench_threads = 1;
    b_args = alca(struct bench_thread_args, bench_threads);
    bench_worker = alca(thread_t, bench_threads);
    b_args[0].mode = 5; // dummy thread
  } else {
    singledb_thread_num = binfo->nreaders + binfo->niterators + binfo->nwriters + binfo->ndeleters;
    //bench_threads = binfo->nreaders + binfo->niterators + binfo->nwriters;
    bench_threads = singledb_thread_num * binfo->nfiles;
    b_args = alca(struct bench_thread_args, bench_threads);
    bench_worker = alca(thread_t, bench_threads);
    for (i=0;i<bench_threads;++i){

      // writer, reader, iterator, deleter
      /*
      if(binfo->ndeleters > 0 && binfo->ratio[1] < 100) {
	// if with delete operations & ratio control, each thread will run mixed op
	b_args[i].mode = 0;
	} else */if ((size_t)i % singledb_thread_num < binfo->nwriters) {
	b_args[i].mode = 1; // writer
      } else if ((size_t)i % singledb_thread_num < binfo->nwriters + binfo->nreaders) {
	b_args[i].mode = 2; // reader
      } else if ((size_t)i % singledb_thread_num < binfo->nwriters +
		 binfo->nreaders + binfo->niterators){
	b_args[i].mode = 3; // iterator
      } else {
	b_args[i].mode = 4; // deleters
      }
      //b_args[i].mode = 0;
    }
  }

  bench_worker_ret = alca(void*, bench_threads);
  pthread_attr_t attr;
  cpu_set_t cpus;
  pthread_attr_init(&attr);
  int db_idx, td_idx, nodeid;
 
  for (i=0;i<bench_threads;++i){
    b_args[i].id = i;
    b_args[i].rnd_seed = rnd_seed;
    b_args[i].compaction_no = compaction_no;
    b_args[i].b_stat = &b_stat;
    b_args[i].l_read = &l_read;
    b_args[i].l_write = &l_write;
    b_args[i].l_delete = &l_delete;

    b_args[i].result = &result;
    b_args[i].zipf = &zipf;
    b_args[i].terminate_signal = 0;
    b_args[i].op_signal = 0;
    b_args[i].binfo = binfo;
    if (b_args[i].mode == 1) {
      // writer: 0 ~ nwriters
      // TODO: check how this affect rxdb and others in multi-device case
      _get_file_range(i, binfo->nwriters, binfo->nfiles,
		      &b_args[i].frange_begin, &b_args[i].frange_end);
    }

    // open db instances
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
#elif defined(__ROCKS_BENCH) || defined(__BLOBFS_ROCKS_BENCH) || defined (__LEVEL_BENCH)
    if (i % singledb_thread_num ==0) {
      /*
      // open only once (multiple open is not allowed)
      b_args[i].db = (Db**)malloc(sizeof(Db*) * binfo->nfiles);
      
#if defined(__BLOBFS_ROCKS_BENCH)
      // As of now, BlobFS has no directory support, so it has not been tested
      // with multiple DBs
      if ((int) binfo->nfiles > 1)
	printf("WARN: BlobFS RocksDB has not been tested with nfiles > 1\n");
#endif	
           
	for (j=0; j<(int)binfo->nfiles; ++j){
	  //sprintf(curfile, "%s%d.%d", binfo->filename, j,
	  //compaction_no[j]);
	  sprintf(curfile, "%s/%d", binfo->filename, i);

#if defined(__BLOBFS_ROCKS_BENCH)
	couchstore_open_db_blobfs(blobfs_env, curfile,
				  COUCHSTORE_OPEN_FLAG_CREATE, &b_args[i].db[j]);
#else

	couchstore_open_db(curfile, COUCHSTORE_OPEN_FLAG_CREATE,
			   &b_args[i].db[j]);
#endif
	couchstore_set_sync(b_args[i].db[j], binfo->sync_write);
	}
      */
	// each thread will only work on one file/instance
	b_args[i].db = (Db**)malloc(sizeof(Db*));
	sprintf(curfile, "%s/%d", binfo->filename, i / singledb_thread_num);
#if defined(__BLOBFS_ROCKS_BENCH)
	couchstore_open_db_blobfs(blobfs_env, curfile,
				  COUCHSTORE_OPEN_FLAG_CREATE, &b_args[i].db[0]);
#else
	couchstore_open_db(curfile, COUCHSTORE_OPEN_FLAG_CREATE,
			   &b_args[i].db[0]);
	fprintf(stdout, "thread %d open db %s mode %d\n", i, curfile, b_args[i].mode);
#endif
	couchstore_set_sync(b_args[i].db[0], binfo->sync_write);

    } else {
      //b_args[i].db = b_args[0].db;
      first_db_idx = i / singledb_thread_num * singledb_thread_num;
      b_args[i].db = b_args[first_db_idx].db;
      //fprintf(stdout, "thread %d same as thread %d mode %d\n", i, first_db_idx, b_args[i].mode);
    }
#elif defined (__KVROCKS_BENCH) //|| defined(__BLOBFS_ROCKS_BENCH)
    b_args[i].db = db;
#elif defined(__KV_BENCH) || defined(__KVV_BENCH)
    /*
    // JP: open container for KVS, open only once
    if(i == 0) {
      b_args[i].db = (Db**)malloc(sizeof(Db*) * binfo->nfiles);
      // nfiles should always be 1 for kvs container
      for (j=0; j<(int)binfo->nfiles; ++j){
	sprintf(curfile, "%s%d.%d", binfo->filename, j,
		compaction_no[j]);
#if defined(__KV_BENCH)
	couchstore_open_db_kvs(dev_id[0], curfile, COUCHSTORE_OPEN_FLAG_KVS,
			       &b_args[i].db[j]);
#endif
      }
    } else {
      b_args[i].db = b_args[0].db;
    }
    */
    if(i % singledb_thread_num == 0) {
      b_args[i].db = (Db**)malloc(sizeof(Db*));
#if defined __KV_BENCH
      couchstore_open_db_kvs(dev_id[i / singledb_thread_num],
			     curfile, COUCHSTORE_OPEN_FLAG_KVS, &b_args[i].db[0]);
      fprintf(stdout, "thread %d open db %d mode %d\n", i, dev_id[i/singledb_thread_num], b_args[i].mode);
#endif
    } else {
      first_db_idx = i / singledb_thread_num * singledb_thread_num;
      b_args[i].db = b_args[first_db_idx].db;
      fprintf(stdout, "thread %d same as thread %d mode %d\n", i, first_db_idx, b_args[i].mode);
    }   
#endif // end of kv
#if defined(__AS_BENCH)
    // TODO: open aerospike
#endif
#if defined(__BLOBFS_ROCKS_BENCH)
    b_args[i].blobfs_env = blobfs_env;
#endif

    /* JP: real bench thread here */
    db_idx = i / singledb_thread_num;
    td_idx = i % singledb_thread_num;
    nodeid = binfo->instances[db_idx].nodeid_perf;
    /*
    printf("thread %d for db %d %d th numaid %d, core %d\n",
	   (int)i, db_idx, td_idx,
	   binfo->instances[db_idx].nodeid_perf,
    	   binfo->instances[db_idx].coreids_bench[td_idx]);
    */
    if(binfo->instances[db_idx].coreids_bench[td_idx] != -1) {
      CPU_ZERO(&cpus);
      CPU_SET(binfo->instances[db_idx].coreids_bench[td_idx], &cpus);
      pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);
      //printf("thread %d core %d\n", (int)i, binfo->instances[db_idx].coreids_bench[td_idx]);
    } else if(nodeid != -1){
      CPU_ZERO(&cpus);
      for(j = 0; j < binfo->cpuinfo->num_cores_per_numanodes; j++){
	CPU_SET(binfo->cpuinfo->cpulist[nodeid][j], &cpus);
	//printf("%d ", binfo->cpuinfo->cpulist[nodeid][j]);
      }
      //printf("\n");
      pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);
    }else {
      // if no core & socket is set, use round robin to assign numa node
      CPU_ZERO(&cpus);
      nodeid = db_idx % binfo->cpuinfo->num_numanodes;
      //printf("thread %d use node %d\n", (int)i, nodeid);
      CPU_ZERO(&cpus);
      for(j = 0; j < binfo->cpuinfo->num_cores_per_numanodes; j++){
	CPU_SET(binfo->cpuinfo->cpulist[nodeid][j], &cpus);
	//printf("%d ", binfo->cpuinfo->cpulist[nodeid][j]);
      }
      //printf("\n");
      pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);
    }
    pthread_create(&bench_worker[i], &attr, bench_thread, (void*)&b_args[i]);
    //thread_create(&bench_worker[i], bench_thread, (void*)&b_args[i]);
    //blobfs_env_test->StartThread(bench_thread, (void*)&b_args[i]);
  }

  //exit(0);

  gap = stopwatch_stop(&sw);
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
  
  // JP: background thread for stats collection
  i = 0;
  while (i < (int)binfo->nbatches || binfo->nbatches == 0) {
    /*
    spin_lock(&b_stat.lock);
    op_count_read = b_stat.op_count_read;
    op_count_write = b_stat.op_count_write;
    i = b_stat.batch_count;
    spin_unlock(&b_stat.lock);
    */
    op_count_read = b_stat.op_read.load();
    op_count_write = b_stat.op_write.load();
    op_count_delete = b_stat.op_delete.load();
    i = b_stat.batch_count_a.load();
    
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

      //#if defined( __KV_BENCH) || defined(__KVROCKS_BENCH) || defined(__KVV_BENCH)
		  // TBD: something here to get KVS space stats
      //#else
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
			   (op_count_read+op_count_write + op_count_delete)*100.0 /
			   (binfo->nops-1));
		    gap = sw.elapsed;
		    PRINT_TIME(gap, " s, ");
		  }

		  elapsed_time = gap.tv_sec + (double)gap.tv_usec / 1000000.0;
		  // average throughput
		  printf("%8.2f ops, ",
			 (double)(op_count_read + op_count_write + op_count_delete) / elapsed_time);
		  // instant throughput
		  printf("%8.2f ops)",
			 (double)((op_count_read + op_count_write + op_count_delete) -
				  (prev_op_count_read + prev_op_count_write +
				   prev_op_count_delete)) /
			 (_gap.tv_sec + (double)_gap.tv_usec / 1000000.0));

#if defined (__KV_BENCH) || defined(__KVROCKS_BENCH) || defined(__KVV_BENCH) || defined (__AS_BENCH)
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
		  /* disable this for multi-device case
#if !defined(__KV_BENCH) && !defined(__KVROCKS_BENCH) && !defined(__AS_BENCH) // JP: no compaction in kvs
		  // valid:invalid size check
		  spin_lock(&cur_compaction_lock);
		  if (cur_compaction == -1 && display_tick % filesize_chk_term == 0) {
		    if (!binfo->auto_compaction &&
			                    cur_size > dbinfo->space_used &&
			                    binfo->compact_thres > 0 &&
			((cur_size - dbinfo->space_used) >
			 ((double)binfo->compact_thres/100.0)*
			 (double)cur_size) ) {

		      // compaction
		      cur_compaction = curfile_no;
		      spin_unlock(&cur_compaction_lock);
		      
		      total_compaction++;
		      compaction_no[curfile_no]++;
		      sprintf(curfile, "%s%d.%d",
			      binfo->filename, (int)curfile_no,
			      compaction_no[curfile_no]-1);
		      sprintf(newfile, "%s%d.%d",
			      binfo->filename, (int)curfile_no,
			      compaction_no[curfile_no]);
		      printf("\n[C#%d %s >> %s]",
			     total_compaction, curfile, newfile);
		      // move a line upward
		      printf("%c[1A%c[0C", 27, 27);
		      
		      printf("\r");
		      
		      if (log_fp) {
			fprintf(log_fp, " [C#%d %s >> %s]\n",
				total_compaction, curfile, newfile);
		      }
		      
		      fflush(stdout);
#ifdef __FDB_BENCH
		      c_args.flag = 0;
		      c_args.binfo = binfo;
		      c_args.curfile = (char*)malloc(256);
		      c_args.newfile = (char*)malloc(256);
		      strcpy(c_args.curfile, curfile);
		      strcpy(c_args.newfile, newfile);
		      c_args.sw_compaction = &sw_compaction;
		      c_args.cur_compaction = &cur_compaction;
		      c_args.bench_threads = bench_threads;
		      c_args.b_args = b_args;
		      c_args.lock = &cur_compaction_lock;
		      thread_create(&tid_compactor, compactor, &c_args);
#endif
		      
		    } else {
		      spin_unlock(&cur_compaction_lock);
		    }
		  } else {
		    spin_unlock(&cur_compaction_lock);
		  }
#endif
		  */
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
		      spin_lock(&b_stat.lock);
		      b_stat.op_count_read = 0;
		      b_stat.op_count_write = 0;
		      b_stat.batch_count = 0;
		      spin_unlock(&b_stat.lock);
		      */   
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
	(op_count_read + op_count_write + op_count_delete) >= binfo->nops)
      break;

    if (got_signal) {
      break;
    }
  }

  // terminate all bench_worker threads
  for (i=0;i<bench_threads;++i){
    b_args[i].terminate_signal = 1;
  }
  /*
  lprintf("\n");
  stopwatch_stop(&sw);
  gap = sw.elapsed;
  LOG_PRINT_TIME(gap, " sec elapsed\n");
  gap_double = gap.tv_sec + (double)gap.tv_usec / 1000000.0;
  */
  // JP: waiting for all job threads finish
  for (i=0;i<bench_threads;++i){
    thread_join(bench_worker[i], &bench_worker_ret[i]);
  }


#if defined (__KV_BENCH) || defined (__AS_BENCH)
  fprintf(stdout, "waiting for IO to be completed\n");
	    if(binfo->kv_write_mode == 0) { // only check async
	      while(completed.load() != submitted) {  // wait until all i/Os are done
		fprintf(stderr, "%ld  ", submitted.load());
		fprintf(stderr, "%ld\n", completed.load());
		usleep(1);
	      }
	    }
	    fprintf(stdout, "all IOs are done\n");
#elif defined (__ROCKS_BENCH) || defined(__BLOBFS_ROCKS_BENCH) || defined (__LEVEL_BENCH)
	    _wait_leveldb_compaction(binfo, db);
#endif

  lprintf("\n");
  stopwatch_stop(&sw);
  gap = sw.elapsed;
  LOG_PRINT_TIME(gap, " sec elapsed\n");
  gap_double = gap.tv_sec + (double)gap.tv_usec / 1000000.0;

  /*
  spin_lock(&b_stat.lock);
  op_count_read = b_stat.op_count_read;
  op_count_write = b_stat.op_count_write;
  spin_unlock(&b_stat.lock);
  */
  op_count_read = b_stat.op_read;
  op_count_write = b_stat.op_write;
  op_count_delete = b_stat.op_delete;

  // TODO: update delete stats
  if (op_count_read) {
    lprintf("%" _F64 " reads (%.2f ops/sec, %.2f us/read)\n",
	    op_count_read,
	    (double)op_count_read / gap_double,
	    gap_double * 1000000 * (binfo->nreaders + binfo->niterators) /
	    op_count_read);
  }
  if (op_count_write) {
    lprintf("%" _F64 " writes (%.2f ops/sec, %.2f us/write)\n",
	    op_count_write,
	    (double)op_count_write / gap_double,
	    gap_double * 1000000 * binfo->nwriters / op_count_write);
  }
  if (op_count_delete) {
    lprintf("%" _F64 " deletes (%.2f ops/sec, %.2f us/write)\n",
	    op_count_delete,
	    (double)op_count_delete / gap_double,
	    gap_double * 1000000 * binfo->ndeleters / op_count_delete);
  }

  lprintf("total %" _F64 " operations (%.2f ops/sec) performed\n",
	  op_count_read + op_count_write + op_count_delete,
	  (double)(op_count_read + op_count_write + op_count_delete) / gap_double);

  lprintf("average latency %f\n", gap_double * 1000000 /
	  ( op_count_read + op_count_write + op_count_delete));
#if defined(__FDB_BENCH) || defined(__COUCH_BENCH)
  if (!binfo->auto_compaction) {
    // manual compaction
    lprintf("compaction : occurred %d time%s, ",
	    total_compaction, (total_compaction>1)?("s"):(""));
    LOG_PRINT_TIME(sw_compaction.elapsed, " sec elapsed\n");
  }
#endif   
#if defined(__KV_BENCH) || defined(__KVV_BENCH) || defined (__KVROCKS_BENCH) || defined (__AS_BENCH)
  // TBD: get kvs bytes written stat
  //couchstore_get_container_info(binfo->device_id);
#else
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
  if(binfo->latency_rate) {
#if defined __KV_BENCH
    if(binfo->kv_write_mode == 0) {
      /*
  #if defind COMP_LAT
      if(kv_write.samples != NULL && binfo->nwriters)
	_print_percentile(binfo, &kv_write, "us", 2);
      if(kv_read.samples != NULL && (binfo->nreaders + binfo->niterators))
	_print_percentile(binfo, &kv_read, "us", 2);
      if(kv_delete.samples != NULL && binfo->ndeleters)
	_print_percentile(binfo, &kv_delete, "us", 2);
  #else
      // submission latency
  #endif
      */
      _print_percentile(binfo, (binfo->nwriters == 0 ? NULL: &kv_write),
			(binfo->nreaders + binfo->niterators == 0? NULL: &kv_read),
			(binfo->ndeleters == 0? NULL : &kv_delete), "us", 2);
    } else { // sync mode
      _print_percentile(binfo, (binfo->nwriters == 0 ? NULL: &l_write),
			(binfo->nreaders + binfo->niterators == 0? NULL: &l_read),
			(binfo->ndeleters == 0? NULL : &l_delete), "us", 2);
    }
#else // other DB
    _print_percentile(binfo, (binfo->nwriters == 0 ? NULL: &l_write),
		      (binfo->nreaders + binfo->niterators == 0? NULL: &l_read),
		      (binfo->ndeleters == 0? NULL : &l_delete), "us", 2); 
    
#endif
  }
  /*
  if (binfo->latency_rate) {
    if(binfo->nwriters && (binfo->nreaders + binfo->niterators)) {
      
#if defined __KV_BENCH || defined __AS_BENCH
#if defined COMP_LAT
      if(binfo->kv_write_mode == 0) {
	if(kv_write.samples != NULL && kv_read.samples != NULL)
	  _print_percentile(binfo, &kv_write, &kv_read, NULL, "us", 2);
      }
      else
	_print_percentile(binfo, &l_write, &l_read, &l_delete, "us", 2);
#else
      _print_percentile(binfo, &l_write, &l_read, &l_delete, "us", 2);
#endif
#else
      
      _print_percentile(binfo, &l_write, &l_read, &l_delete, "us", 2);
#endif	
    }
    else {
      if(binfo->nwriters) {
	// only writes

#if defined __KV_BENCH || defined __AS_BENCH
	_print_percentile(binfo, &l_write, NULL, NULL, "us", 2);
#else

	_print_percentile(binfo, &l_write, NULL, NULL, "us", 2);
#endif
      }
      if(binfo->nreaders + binfo->niterators) {
	// only reads
#if defined __KV_BENCH || defined __AS_BENCH
#if defined COMP_LAT
	if(binfo->kv_write_mode == 0) {
	  if(kv_read.samples != NULL)
	    _print_percentile(binfo, NULL, &kv_read, NULL, "us", 2);
	}
	else
	  _print_percentile(binfo, NULL, &l_read, NULL, "us", 2);
#else
	_print_percentile(binfo, NULL, &l_read, NULL, "us", 2);
#endif
#else
	_print_percentile(binfo, NULL, &l_read, NULL, "us", 2);
#endif
      }
    }
  }
  */
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
#elif defined(__KV_BENCH) || defined(__KVROCKS_BENCH) || defined(__KVV_BENCH) || defined(__BLOBFS_ROCKS_BENCH) || defined (__LEVEL_BENCH)
  //for (j=0;j<(int)binfo->nfiles;++j){
  for (j = 0; j < bench_threads; j+= singledb_thread_num){
    couchstore_close_db(b_args[j].db[0]);
  }
#if !defined(__KVROCKS_BENCH) && !defined(__BLOBFS_ROCKS_BENCH) && !defined(__AS_BENCH)
  free(b_args[0].db);
#endif
#if defined(__BLOBFS_ROCKS_BENCH)
  delete blobfs_env;
#endif // __BLOBFS_ROCKS_BENCH
#if defined(__AS_BENCH)
  // TODO: close aerospike, destoy whatever
#endif
#elif defined (__ROCKS_BENCH)
  for (j = 0; j < bench_threads; j+= singledb_thread_num){
    couchstore_close_db(b_args[j].db[0]);
    free(b_args[j].db);
    fprintf(stdout, "close & free db in thread %d\n", j);
  }
#endif

#if defined(__KV_BENCH) || defined(__KVV_BENCH)
  for (j = 0; j< binfo->num_devices; j++)
    couchstore_close_device(dev_id[j]);
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

  spin_destroy(&b_stat.lock);
  spin_destroy(&l_read.lock);
  spin_destroy(&l_write.lock);

  free(l_read.samples);
  free(l_write.samples);

  memleak_end();

  if(binfo->pre_gen == 1) {
#if defined (__ROCKS_BENCH) || defined(__KVROCKS_BENCH) || defined (__BLOBFS_ROCKS_BENCH) || defined (__FDB_BENCH) || defined (__WT_BENCH) || defined (__LEVEL_BENCH)
    for(i = 0; i < binfo->ndocs; i++) {
      if (docs_pre[i]->id.buf) free(docs_pre[i]->id.buf);
#if defined (__FDB_BENCH) || defined (__WT_BENCH)
      if (infos_pre[i]) free(infos_pre[i]);
#endif
    }
    free(docs_pre);
    if (infos_pre) free(infos_pre);
    
#elif defined(__KV_BENCH)
  /*
  if(binfo->seq_fill == 0){
    if(keybuf)
      couchstore_kvs_free(keybuf);
  }
  else
    couchstore_kvs_free_key();
  */
    if(keybuf)  couchstore_kvs_free(keybuf);
#elif defined(__AS_BENCH)
    if(keybuf) free(keybuf);
    // TODO: also need to free reqs[] for aerospike & kvs
#endif
  }
}


/*
void do_bench(struct bench_info *binfo)
{
    BDR_RNG_VARS;
    int i, j, ret; (void)j; (void)ret;
    int curfile_no, compaction_turn;
    int compaction_no[binfo->nfiles], total_compaction = 0;
    int cur_compaction = -1;
    int bench_threads;
    uint64_t op_count_read, op_count_write, display_tick = 0;
    uint64_t prev_op_count_read, prev_op_count_write;
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
    Db *db[binfo->nfiles];
#ifdef __FDB_BENCH
    Db *info_handle[binfo->nfiles];
#endif
#if defined(__KV_BENCH) || defined(__KVV_BENCH)
    int32_t dev_id;
    int32_t container_count;
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
    struct latency_stat l_read, l_write;
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

    if (binfo->bodylen.type == RND_NORMAL) {
        avg_docsize = binfo->bodylen.a;
    } else {
        avg_docsize = (binfo->bodylen.a + binfo->bodylen.b)/2;
    }
    if (binfo->keylen.type == RND_NORMAL) {
        avg_docsize += binfo->keylen.a;
    } else {
        avg_docsize += ((binfo->keylen.a + binfo->keylen.b)/2);
    }

    strcpy(fsize1, print_filesize_approx(0, cmd));
    strcpy(fsize2, print_filesize_approx(0, cmd));
    memset(spaces, ' ', 80);
    spaces[80] = 0;

    // latency stat init
    l_read.cursor = l_write.cursor = 0;
    l_read.nsamples = l_write.nsamples = 0;
    l_read.samples = (uint32_t*)malloc(sizeof(uint32_t) * binfo->latency_max);
    l_write.samples = (uint32_t*)malloc(sizeof(uint32_t) * binfo->latency_max);
    spin_init(&l_read.lock);
    spin_init(&l_write.lock);

#ifdef __TIME_CHECK
    l_prep.cursor = l_real.cursor = 0;
    l_prep.nsamples = l_real.nsamples = 0;
    l_prep.samples = (uint32_t*)malloc(sizeof(uint32_t) * binfo->latency_max);
    l_real.samples = (uint32_t*)malloc(sizeof(uint32_t) * binfo->latency_max);
    spin_init(&l_prep.lock);
    spin_init(&l_real.lock);
#endif

#if !defined(__COUCH_BENCH) && !defined(__KV_BENCH) && !defined(__KVROCKS_BENCH) && !defined(__KVV_BENCH) 
    //#if defined(__FDB_BENCH) || defined(__LEVEL_BENCH) || defined(__WT_BENCH) || defined(__ROCKS_BENCH)
    // all but Couchstore: set buffer cache size
    couchstore_set_cache(binfo->cache_size);
    // set compression option
    couchstore_set_compression(binfo->compression);
#endif

#if defined(__LEVEL_BENCH) || defined(__ROCKS_BENCH)
    // LevelDB, RocksDB: set bloom filter bits per key
    couchstore_set_bloom(binfo->bloom_bpk);
#endif // __LEVEL_BENCH || __ROCKS_BENCH
#if defined(__ROCKS_BENCH)
    // RocksDB: set compaction style
    if (binfo->compaction_style) {
        couchstore_set_compaction_style(binfo->compaction_style);
    }
#endif // __ROCKS_BENCH
#if defined(__KV_BENCH) || defined(__KVV_BENCH)
    if(binfo->kv_write_mode == 0) { // set aio context
      // set queuedepth, aiothreads, coremask
      couchstore_kvs_set_queuedepth(binfo->queue_depth);
      couchstore_kvs_set_aiothreads(binfo->aiothreads_per_device);
      couchstore_kvs_set_coremask(binfo->core_ids);
    }
    // open device only once
    couchstore_open_device(binfo->device_path, &dev_id, binfo->kv_write_mode, binfo->num_devices);
#endif

    if (binfo->initialize) {
        // === initialize and populate files ========
#if defined(__KV_BENCH)
        // TBD: need something here for kvs check whether previous KV exists

        couchstore_get_container_count(dev_id, &container_count);
        if(container_count > 0){
	  // container already exists, ask user
	  printf("there are %d containers ---\n", container_count);
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
	  }else {
	    // delete previous container
	    lprintf("\ninitialize\n");	    
	    couchstore_remove_containers(dev_id);
	  }
	}
	else{
	  printf("there is no previous container -----\n");
	}

#elif defined(__KVV_BENCH)
	printf("this is kvv --- \n");
	// assume no persistency in kvv
	// no previous kvs exists, do nothing
#elif defined(__KVROCKS_BENCH)
	printf("this is kvrocks --- \n");
#else
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
        sprintf(cmd, "rm -rf %s* 2> errorlog.txt", binfo->filename);
        ret = system(cmd);

        // create directory if doesn't exist
        str = _get_dirname(binfo->filename, bodybuf);
        if (str) {
            if (!_does_file_exist(str)) {
                sprintf(cmd, "mkdir -p %s > errorlog.txt", str);
                ret = system(cmd);
            }
        }
#endif

	// for insert_and_update workload, no need for pre-population
	if (!binfo->insert_and_update) {
#if defined(__LEVEL_BENCH) || defined(__ROCKS_BENCH)
	  // LevelDB, RocksDB: set WBS size
	  couchstore_set_wbs_size(binfo->wbs_init);
#endif

	  for (i=0; i<(int)binfo->nfiles; ++i){
            compaction_no[i] = 0;
            sprintf(curfile, "%s%d.%d", binfo->init_filename, i,
                                        compaction_no[i]);
	    // JP: open/create container here for kvs: filenname = container name
	    // for first time population, need to open device first for kvs
#if defined(__KV_BENCH)
	    couchstore_open_db_kvs(dev_id, curfile, COUCHSTORE_OPEN_FLAG_CREATE, &db[i]);
#elif defined(__KVROCKS_BENCH)
	    couchstore_open_db(binfo->device_path, COUCHSTORE_OPEN_FLAG_CREATE, &db[i]);
#elif defined(__KVV_BENCH)
	    // do nothing
#else
            couchstore_open_db(curfile, COUCHSTORE_OPEN_FLAG_CREATE, &db[i]);
#endif
#if defined(__LEVEL_BENCH) || defined(__ROCKS_BENCH) || defined(__KVROCKS_BENCH)
	    if (!binfo->pop_commit) {
	      couchstore_set_sync(db[i], 0);
            }
#endif
	  }

	  stopwatch_start(&sw);
#if defined (__KV_BENCH) || defined (__ROCKS_BENCH) || defined (__KVROCKS_BENCH)
	  population(db, binfo, &l_write);
	  //population_test(db, binfo, &l_write);
#endif

#if  defined(__PRINT_IOSTAT) && \
    (defined(__LEVEL_BENCH) || defined(__ROCKS_BENCH))
	  // Linux + (LevelDB or RocksDB): wait for background compaction
	  gap = stopwatch_stop(&sw);
	  LOG_PRINT_TIME(gap, " sec elapsed\n");
	  print_proc_io_stat(cmd, 1);
	  _wait_leveldb_compaction(binfo, db);
#endif // __PRINT_IOSTAT && (__LEVEL_BENCH || __ROCKS_BENCH)

#if !defined(__KV_BENCH) && !defined (__KVROCKS_BENCH)
	  if (binfo->sync_write) {
            lprintf("flushing disk buffer.. "); fflush(stdout);
            sprintf(cmd, "sync");
            ret = system(cmd);
            lprintf("done\n"); fflush(stdout);
	  }
#endif
	  

	  //#if defined(__KV_BENCH) || defined(__KVV_BENCH)
	// TBD: get bytes written for kvs
	//written_final = written_init = print_proc_io_stat(cmd, 1);
	  //if(binfo->kv_write_mode == 0)
	  //couchstore_kvs_reset_aiocompletion();
	  //#else
	  //written_final = written_init = print_proc_io_stat(cmd, 1);
	  //#endif
	  
	  //written_prev = written_final;

	// TBD:check whether needs to close db here for kvs
#if defined(__ROCKS_BENCH)
	  for (i=0; i<(int)binfo->nfiles; ++i){
            couchstore_close_db(db[i]);
	  }
#endif
	  gap = stopwatch_stop(&sw);
#if defined(__LEVEL_BENCH) || defined(__ROCKS_BENCH)
#if defined(__PRINT_IOSTAT)
	  gap.tv_sec -= 3; // subtract waiting time
#endif // __PRINT_IOSTAT
#endif // __LEVEL_BENCH || __ROCKS_BENCH
	  gap_double = gap.tv_sec + (double)gap.tv_usec / 1000000.0;
	  LOG_PRINT_TIME(gap, " sec elapsed ");
	  lprintf("(%.2f ops/sec)\n", binfo->ndocs / gap_double);
	} else {
	  printf("insert and update: no need for population \n");
	}
    }else {
        // === load existing files =========
        stopwatch_start(&sw);
        for (i=0; i<(int)binfo->nfiles; ++i) {
            compaction_no[i] = 0;
        }
// do nothing for kvs, will open container when starting benchmarking 
#if !defined(__KV_BENCH) && !defined(__KVV_BENCH) && !defined(__KVROCKS_BENCH)
        _dir_scan(binfo, compaction_no);
#endif
	} // load existing files

    // ==== perform benchmark ====
    lprintf("\nbenchmark\n");
    lprintf("opening DB instance .. ");

    compaction_turn = 0;

#if defined(__KV_BENCH) || defined (__ROCKS_BENCH) || defined (__KVROCKS_BENCH)
    completed = 0;
    //fprintf(stdout, "reset to %d\n", completed.load());
#endif

#if defined(__LEVEL_BENCH) || defined(__ROCKS_BENCH)
    // LevelDB, RocksDB: reset write buffer size
    couchstore_set_wbs_size(binfo->wbs_bench);
#endif // __LEVEL_BENCH || __ROCKS_BENCH

    if (binfo->batch_dist.type == RND_ZIPFIAN) {
        // zipfian distribution .. initialize zipf_rnd
        zipf_rnd_init(&zipf, binfo->ndocs / binfo->batch_dist.b,
                      binfo->batch_dist.a/100.0, 1024*1024);
    }

    // set signal handler
    old_handler = signal(SIGINT, signal_handler);

    // bench stat init
    b_stat.batch_count = 0;
    b_stat.op_count_read = b_stat.op_count_write = 0;
    prev_op_count_read = prev_op_count_write = 0;
    spin_init(&b_stat.lock);

// 
    // latency stat init
// spin_lock(&l_read.lock);
//   l_read.cursor = 0;
//   l_read.nsamples = 0;
//   spin_unlock(&l_read.lock);
//   spin_lock(&l_write.lock);
//   l_write.cursor = 0;
//   l_write.nsamples = 0;
//   spin_unlock(&l_write.lock);
    //l_read.cursor = l_write.cursor = 0;
    //l_read.nsamples = l_write.nsamples = 0;
    //l_read.samples = (uint32_t*)malloc(sizeof(uint32_t) * binfo->latency_max);
    //l_write.samples = (uint32_t*)malloc(sizeof(uint32_t) * binfo->latency_max);
    //spin_init(&l_read.lock);
    //spin_init(&l_write.lock);
  
    
    // thread args
    if(binfo->insert_and_update)
    {
      // no readers for update_and_insert mixed workload
      binfo->nreaders = binfo->niterators = 0;
    }
    if (binfo->nreaders + binfo->niterators + binfo->nwriters == 0){
        // create a dummy thread
        bench_threads = 1;
        b_args = alca(struct bench_thread_args, bench_threads);
        bench_worker = alca(thread_t, bench_threads);
        b_args[0].mode = 4; // dummy thread
    } else {
        bench_threads = binfo->nreaders + binfo->niterators + binfo->nwriters;
        b_args = alca(struct bench_thread_args, bench_threads);
        bench_worker = alca(thread_t, bench_threads);
        for (i=0;i<bench_threads;++i){
            // writer, reader, iterator
            if ((size_t)i < binfo->nwriters) {
                b_args[i].mode = 1; // writer
            } else if ((size_t)i < binfo->nwriters + binfo->nreaders) {
                b_args[i].mode = 2; // reader
            } else {
                b_args[i].mode = 3; // iterator
            }
        }
    }

    bench_worker_ret = alca(void*, bench_threads);
    for (i=0;i<bench_threads;++i){
        b_args[i].id = i;
        b_args[i].rnd_seed = rnd_seed;
        b_args[i].compaction_no = compaction_no;
        b_args[i].b_stat = &b_stat;
        //b_args[i].l_read = &l_read;
	//b_args[i].l_write = &l_write;

        b_args[i].result = &result;
        b_args[i].zipf = &zipf;
        b_args[i].terminate_signal = 0;
        b_args[i].op_signal = 0;
        b_args[i].binfo = binfo;
        if (b_args[i].mode == 1) {
            // writer: 0 ~ nwriters
	  // TBD: need to eliminate file related operations for KVS, or maybe just leave as it is
            _get_file_range(i, binfo->nwriters, binfo->nfiles,
                            &b_args[i].frange_begin, &b_args[i].frange_end);
        }

        // open db instances
#if defined(__LEVEL_BENCH) || defined(__ROCKS_BENCH)
        if (i==0) {
            // open only once (multiple open is not allowed)
            b_args[i].db = (Db**)malloc(sizeof(Db*) * binfo->nfiles);
            for (j=0; j<(int)binfo->nfiles; ++j){
                sprintf(curfile, "%s%d.%d", binfo->filename, j,
                                            compaction_no[j]);
                couchstore_open_db(curfile, COUCHSTORE_OPEN_FLAG_CREATE,
                                   &b_args[i].db[j]);
                couchstore_set_sync(b_args[i].db[j], binfo->sync_write);
            }
        } else {
            b_args[i].db = b_args[0].db;
        }
#elif defined(__KV_BENCH) || defined(__KVV_BENCH)
	// JP: open container for KVS, open only once
	if(i == 0) {
	  b_args[i].db = (Db**)malloc(sizeof(Db*) * binfo->nfiles);
	  // nfiles should always be 1 for kvs container
	  for (j=0; j<(int)binfo->nfiles; ++j){
	    sprintf(curfile, "%s%d.%d", binfo->filename, j,
		                         compaction_no[j]);
#if defined(__KV_BENCH)
	    couchstore_open_db_kvs(dev_id, curfile, COUCHSTORE_OPEN_FLAG_KVS,
				   &b_args[i].db[j]);
#endif
	  }
	} else {
	  b_args[i].db = b_args[0].db;
	}
#endif
	// JP: real bench thread here 
        thread_create(&bench_worker[i], bench_thread, (void*)&b_args[i]);
    }

    gap = stopwatch_stop(&sw);
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

    // JP: background thread for stats collection
    i = 0;
    while (i < (int)binfo->nbatches || binfo->nbatches == 0) {
        spin_lock(&b_stat.lock);
        op_count_read = b_stat.op_count_read;
        op_count_write = b_stat.op_count_write;
        i = b_stat.batch_count;
        spin_unlock(&b_stat.lock);

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

            BDR_RNG_NEXTPAIR;
            spin_lock(&cur_compaction_lock);
            curfile_no = compaction_turn;
            if (display_tick % filesize_chk_term == 0) {
                compaction_turn = (compaction_turn + 1) % binfo->nfiles;
            }

            temp_db = b_args[0].db[curfile_no];
            cpt_no = compaction_no[curfile_no] -
                     ((curfile_no == cur_compaction)?(1):(0));
            spin_unlock(&cur_compaction_lock);

#if defined( __KV_BENCH) || defined(__KVROCKS_BENCH) || defined(__KVV_BENCH) 
	    // TBD: something here to get KVS space stats
#else
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
                       (op_count_read+op_count_write)*100.0 /
                           (binfo->nops-1));
                gap = sw.elapsed;
                PRINT_TIME(gap, " s, ");
            }

            elapsed_time = gap.tv_sec + (double)gap.tv_usec / 1000000.0;
            // average throughput
            printf("%8.2f ops, ",
                (double)(op_count_read + op_count_write) / elapsed_time);
            // instant throughput
            printf("%8.2f ops)",
                (double)((op_count_read + op_count_write) -
                    (prev_op_count_read + prev_op_count_write)) /
                (_gap.tv_sec + (double)_gap.tv_usec / 1000000.0));

#if defined (__KV_BENCH) || defined(__KVROCKS_BENCH) || defined(__KVV_BENCH)
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
                        (double)(op_count_read + op_count_write) /
                                (elapsed_time),
                        (double)((op_count_read + op_count_write) -
                                (prev_op_count_read + prev_op_count_write)) /
                                (_gap.tv_sec + (double)_gap.tv_usec / 1000000.0),
                        op_count_read, op_count_write,
                        written_final - written_init);
            }

            printf("\n");

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

            stopwatch_start(&sw);

#if !defined(__KV_BENCH) && !defined(__KVV_BENCH) // JP: no compaction in kvs
            // valid:invalid size check
            spin_lock(&cur_compaction_lock);
            if (cur_compaction == -1 && display_tick % filesize_chk_term == 0) {
                if (!binfo->auto_compaction &&
                    cur_size > dbinfo->space_used &&
                    binfo->compact_thres > 0 &&
                    ((cur_size - dbinfo->space_used) >
                         ((double)binfo->compact_thres/100.0)*
                         (double)cur_size) ) {

                    // compaction
                    cur_compaction = curfile_no;
                    spin_unlock(&cur_compaction_lock);

                    total_compaction++;
                    compaction_no[curfile_no]++;

                    sprintf(curfile, "%s%d.%d",
                            binfo->filename, (int)curfile_no,
                            compaction_no[curfile_no]-1);
                    sprintf(newfile, "%s%d.%d",
                            binfo->filename, (int)curfile_no,
                            compaction_no[curfile_no]);
                    printf("\n[C#%d %s >> %s]",
                           total_compaction, curfile, newfile);
                    // move a line upward
                    printf("%c[1A%c[0C", 27, 27);
                    printf("\r");
                    if (log_fp) {
                        fprintf(log_fp, " [C#%d %s >> %s]\n",
                                total_compaction, curfile, newfile);
                    }
                    fflush(stdout);
                } else {
                    spin_unlock(&cur_compaction_lock);
                }
            } else {
                spin_unlock(&cur_compaction_lock);
            }
#endif

            if (binfo->bench_secs && !warmingup &&
                (size_t)sw.elapsed.tv_sec >= binfo->bench_secs)
                break;

	    if ((size_t)sw.elapsed.tv_sec >= binfo->warmup_secs){
	      if (warmingup) {
                // end of warming up .. initialize stats
                stopwatch_init_start(&sw);
                prev_op_count_read = op_count_read = 0;
                prev_op_count_write = op_count_write = 0;
                written_init = written_final;
                spin_lock(&b_stat.lock);
                b_stat.op_count_read = 0;
                b_stat.op_count_write = 0;
                b_stat.batch_count = 0;
                spin_unlock(&b_stat.lock);
                spin_lock(&l_read.lock);
                l_read.cursor = 0;
                l_read.nsamples = 0;
                spin_unlock(&l_read.lock);
                spin_lock(&l_write.lock);
                l_write.cursor = 0;
                l_write.nsamples = 0;
                spin_unlock(&l_write.lock);
#if defined(__KV_BENCH)

		//if(binfo->kv_write_mode == 0)
		//  couchstore_kvs_reset_aiocompletion();

#endif
                warmingup = false;
                lprintf("\nevaluation\n");
		lprintf("time,ops_avg,ops_i,read_cnt,write_cnt,bytes_written\n");
		if(run_ops_fp)
		  fprintf(run_ops_fp, "time,ops_avg,ops_i,read_cnt,write_cnt,bytes_written\n");
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
            (op_count_read + op_count_write) >= binfo->nops)
            break;

        if (got_signal) {
            break;
        }
    }

    // terminate all bench_worker threads
    for (i=0;i<bench_threads;++i){
        b_args[i].terminate_signal = 1;
    }

    lprintf("\n");
    stopwatch_stop(&sw);
    gap = sw.elapsed;
    LOG_PRINT_TIME(gap, " sec elapsed\n");
    gap_double = gap.tv_sec + (double)gap.tv_usec / 1000000.0;

    // JP: waiting for all job threads finish
    for (i=0;i<bench_threads;++i){
        thread_join(bench_worker[i], &bench_worker_ret[i]);
    }

#if defined (__KV_BENCH)
    int32_t count;
    if(binfo->kv_write_mode == 0) {
      couchstore_kvs_get_aiocompletion(&count);
      while(count < (op_count_read + op_count_write)) {
	//printf(" ------- %ld - %ld\n", (long int)count, op_count_read + op_count_write);
	couchstore_kvs_get_aiocompletion(&count);
      }
      //printf(" async ---------- done %ld %ld\n", (long int)count, op_count_read + op_count_write);
    }
#endif

    spin_lock(&b_stat.lock);
    op_count_read = b_stat.op_count_read;
    op_count_write = b_stat.op_count_write;
    spin_unlock(&b_stat.lock);

    // waiting for unterminated compactor & bench workers
#if defined(__FDB_BENCH) || defined(__COUCH_BENCH)
    if (cur_compaction != -1) {
        lprintf("waiting for termination of remaining compaction..\n");
        fflush(stdout);
        thread_join(tid_compactor, &compactor_ret);
    }
#endif

    //printf("killing dstat --- \n");
    //sprintf(cmd, "pkill -f dstat");
    ret = system(cmd);

    if (op_count_read) {
        lprintf("%" _F64 " reads (%.2f ops/sec, %.2f us/read)\n",
                op_count_read,
                (double)op_count_read / gap_double,
                gap_double * 1000000 * (binfo->nreaders + binfo->niterators) /
                    op_count_read);
    }
    if (op_count_write) {
        lprintf("%" _F64 " writes (%.2f ops/sec, %.2f us/write)\n",
                op_count_write,
                (double)op_count_write / gap_double,
                gap_double * 1000000 * binfo->nwriters / op_count_write);
    }

    lprintf("total %" _F64 " operations (%.2f ops/sec) performed\n",
            op_count_read + op_count_write,
             (double)(op_count_read + op_count_write) / gap_double);

#if defined(__FDB_BENCH) || defined(__COUCH_BENCH)
    if (!binfo->auto_compaction) {
        // manual compaction
        lprintf("compaction : occurred %d time%s, ",
                total_compaction, (total_compaction>1)?("s"):(""));
        LOG_PRINT_TIME(sw_compaction.elapsed, " sec elapsed\n");
    }
#endif

#if defined(__KV_BENCH) || defined(__KVV_BENCH)
    // TBD: get kvs bytes written stat
    //couchstore_get_container_info(binfo->device_id);
#else
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
	    lprintf("WAF,app,dev,e2e\n");
#if !defined(__KV_BENCH) && !defined(__KVV_BENCH)
	    nand_written_final = get_nand_written(binfo);
	    waf_d = (double)(nand_written_final - nand_written_init) * 512 * 1000 / (written_final - written_init);
	    //lprintf("Deivce WAF %.2f, ETE WAF %.2f\n", 
	    //    waf_d, waf_d * waf_a);
	    lprintf("WAF,%.1f,%.2f,%.2f\n",waf_a, waf_d, waf_d * waf_a);
#else
	    lprintf("WAF,1,1,1\n");
#endif
        }
    }
#endif

    if (binfo->latency_rate) {
      
      //  if (binfo->nwriters) {
      //     lprintf("\nwrite latency distribution\n");
      //     _print_percentile(binfo, &l_write, "us");
      // }
      // if (binfo->nreaders + binfo->niterators) {
      //      lprintf("\nread latency distribution\n");
      //     _print_percentile(binfo, &l_read, "us");
      //	    }
      if(binfo->nwriters && (binfo->nreaders + binfo->niterators)) {
	_print_percentile(binfo, &l_write, &l_read, "us", 2);
      }
      else {
	if(binfo->nwriters) {
	  // only writes
	  _print_percentile(binfo, &l_write, NULL, "us", 2);
	}
	if(binfo->nreaders + binfo->niterators) {
	  // only reads
	  _print_percentile(binfo, NULL, &l_read, "us", 2);
	}
      }

#ifdef __TIME_CHECK
	if(binfo->latency_rate) {
	  _print_time(&l_prep, &l_real);
	}
#endif
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
#elif defined(__LEVEL_BENCH) || defined(__ROCKS_BENCH) || defined(__KV_BENCH) || defined(__KVROCKS_BENCH) || defined(__KVV_BENCH)
    for (j=0;j<(int)binfo->nfiles;++j){
        couchstore_close_db(b_args[0].db[j]);
    }
    free(b_args[0].db);
#endif

#if defined(__WT_BENCH) || defined(__FDB_BENCH)
    couchstore_close_conn();
#endif

#if defined(__KV_BENCH) || defined(__KVV_BENCH)
    couchstore_close_device(dev_id);
#endif

    lprintf("\n");
    stopwatch_stop(&sw);
    gap = sw.elapsed;
    LOG_PRINT_TIME(gap, " sec elapsed\n");

    free(dbinfo);

    _bench_result_print(&result);
    _bench_result_free(&result);

    spin_destroy(&b_stat.lock);
    spin_destroy(&l_read.lock);
    spin_destroy(&l_write.lock);
#ifdef __TIME_CHECK
    spin_destroy(&l_prep.lock);
    spin_destroy(&l_real.lock);
    free(l_prep.samples);
    free(l_real.samples);
#endif
    free(l_read.samples);
    free(l_write.samples);

    memleak_end();
}

*/

void _print_benchinfo(struct bench_info *binfo)
{
    char tempstr[256];

    lprintf("\n === benchmark configuration ===\n");
    lprintf("DB module: %s\n", binfo->dbname);

    lprintf("random seed: %d\n", (int)rnd_seed);

    if (strcmp(binfo->init_filename, binfo->filename)) {
        lprintf("initial filename: %s#\n", binfo->init_filename);
    }
    lprintf("filename: %s#", binfo->filename);
    if (binfo->initialize) {
        lprintf(" (initialize)\n");
    } else {
        lprintf(" (use existing DB file)\n");
    }

    lprintf("# documents (i.e. working set size): %d\n", (int)binfo->ndocs);
    if (binfo->nfiles > 1) {
        lprintf("# files: %d\n", (int)binfo->nfiles);
    }

    lprintf("# threads: ");
    lprintf("reader %d, iterator %d", (int)binfo->nreaders,
                                      (int)binfo->niterators);
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
#if defined(__LEVEL_BENCH) || defined(__ROCKS_BENCH) || defined(__BLOBFS_ROCKS_BENCH)
    lprintf("WBS size: %s (init), ",
        print_filesize_approx(binfo->wbs_init, tempstr));
    lprintf("%s (bench)\n",
        print_filesize_approx(binfo->wbs_bench, tempstr));
    if (binfo->bloom_bpk) {
        lprintf("bloom filter enabled (%d bits per key)\n", (int)binfo->bloom_bpk);
    }
#endif // __LEVEL_BENCH || __ROCKS_BENCH

#if defined(__ROCKS_BENCH) || defined(__BLOBFS_ROCKS_BENCH)
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
    }else {
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
    fprintf(stderr, "please genrerate cpu core info file\n");
    exit(0);
  }

  int total_bench_threads = binfo->nreaders + binfo->niterators + binfo->nwriters + binfo->ndeleters;
  binfo->instances = (instance_info_t *)malloc(sizeof(instance_info_t) * binfo->nfiles);
  for(i = 0; i < binfo->nfiles; i++) {
    binfo->instances[i].coreids_pop = (int*)malloc(sizeof(int) * binfo->pop_nthreads);
    //memset(binfo->instances[i].coreids_pop, -1, sizeof(binfo->instances[i].coreids_pop));
    for (j = 0; j<binfo->pop_nthreads; j++)
      binfo->instances[i].coreids_pop[j] = -1;
    binfo->instances[i].coreids_bench = (int*)malloc(sizeof(int) * total_bench_threads);
    //memset(binfo->instances[i].coreids_bench, -1, sizeof(sizeof(int) * total_bench_threads));
    for(j = 0; j< total_bench_threads; j++)
      binfo->instances[i].coreids_bench[j] = -1;
    binfo->instances[i].nodeid_load = -1;
    binfo->instances[i].nodeid_perf = -1;
  }

  binfo->cpuinfo->cpulist = (int **)malloc(sizeof(int *) * binfo->cpuinfo->num_numanodes);
  for(i = 0; i < binfo->cpuinfo->num_numanodes; i++) {
    binfo->cpuinfo->cpulist[i] = (int*)malloc(sizeof(int) * binfo->cpuinfo->num_cores_per_numanodes);
  }
  
  while(fgets(line, sizeof line, fp)!= NULL ){
    if(line_num == 0) {line_num++; continue;}
    sscanf(line, "%d,%d,%d,%d", &nodeid, &coreid, &insid_load, &insid_perf);
    //printf("%d %d %d %d\n", nodeid, coreid,insid_load,insid_perf);
    binfo->cpuinfo->cpulist[nodeid][core_count++ % binfo->cpuinfo->num_cores_per_numanodes] = coreid;
    if(insid_load >=0 && insid_load < binfo->nfiles) {
      if(prev_load_insid != insid_load) {
	if(prev_load_insid != -1){
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
    
    if(insid_perf >=0 && insid_perf < binfo->nfiles){
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
    printf("pop -- dev %d, numaid %d, core: ", i, binfo->instances[i].nodeid_load);
    for(j = 0; j < binfo->pop_nthreads; j++)
      printf("%d ", binfo->instances[i].coreids_pop[j]);
    printf("\n");
  }
  for(i = 0;i < binfo->nfiles; i++){
    printf("bench --- dev %d, numaid %d, core: ", i, binfo->instances[i].nodeid_perf);
    for(j = 0; j < total_bench_threads; j++)
      printf("%d ", binfo->instances[i].coreids_bench[j]);
    printf("\n");
  }
  
  if(fp) fclose(fp);
}

struct bench_info get_benchinfo(char* bench_config_filename)
{
    int i, j;
    static dictionary *cfg;
    char *str;
    struct bench_info binfo;
    memset(&binfo, 0x0, sizeof(binfo));    
    cfg = iniparser_new(bench_config_filename);

    str = iniparser_getstring(cfg, (char*)"system:config_only",
			      (char*)"false");
    if(str[0] == 't' || str[0] == 'T') {
      cpu_info *cpuinfo = (cpu_info *)malloc(sizeof(cpu_info));
      memset(cpuinfo, 0, sizeof(cpu_info));
      get_cpuinfo(cpuinfo, 1);
      printf("total %d nodes cores %d\n", cpuinfo->num_numanodes, cpuinfo->num_cores_per_numanodes);
      exit(0);
    } else {
      binfo.cpuinfo = (cpu_info *)malloc(sizeof(cpu_info));
      memset(binfo.cpuinfo, 0, sizeof(cpu_info));
      get_cpuinfo(binfo.cpuinfo, 0);
      //printf("node %d -- core %d\n", binfo.cpuinfo->num_numanodes, binfo.cpuinfo->num_cores_per_numanodes);
      /*
      for(i = 0; i < binfo.cpuinfo->num_numanodes; i++){
	printf("node %d: ", i);
	for(j = 0; j< binfo.cpuinfo->num_cores_per_numanodes; j++)
	  printf("%d ", binfo.cpuinfo->cpulist[i][j]);
	printf("\n");
      }
      */
    }
    
    char *dbname = (char*)malloc(64);
    char *filename = (char*)malloc(256);
    char *init_filename = (char*)malloc(256);
    char *log_filename = (char*)malloc(256);
    //char *syslog_filename = (char*)malloc(256);
    //char *timelog_filename = (char*)malloc(256);
    char *device_path = (char*)malloc(256);
    char *name_space = (char*)malloc(256);
    char *device_name = (char*)malloc(256);
    char *spdk_conf_file = (char*)malloc(256);
    char *spdk_bdev = (char*)malloc(256);
    char *core_ids = (char*)malloc(256);
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
#elif __KVROCKS_BENCH
    sprintf(dbname, "KVROCKS");
    sprintf(dbname_postfix, "kvrocks");
#elif __KVV_BENCH 
    sprintf(dbname, "KVV");
    sprintf(dbname_postfix, "kvv");
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
    //binfo.syslog_filename = syslog_filename;
    //binfo.timelog_filename = timelog_filename;
    binfo.device_path = device_path;
    binfo.device_name = device_name;
    binfo.core_ids = core_ids;
    binfo.spdk_conf_file = spdk_conf_file;
    binfo.spdk_bdev = spdk_bdev;
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
    
    char *devname_ret;
    str = iniparser_getstring(cfg, (char*)"kvs:device_path", (char*)"");
    strcpy(binfo.device_path, str);
    devname_ret = _get_filename_pos(str);
    if(devname_ret)
      strcpy(binfo.device_name, devname_ret);

    pool_info_t info;
    str = iniparser_getstring(cfg, (char*)"system:allocator", (char*)"default");
    if (str[0] == 'N' || str[0] == 'n') {
      //binfo.allocate_mem = &allocate_mem_numa;
      //binfo.free_mem = &free_mem_numa;
      binfo.allocatortype = info.allocatortype = NUMA_ALLOCATOR;
    } else if (str[0] == 'p' || str[0] == 'P'){
      //binfo.allocate_mem = &allocate_mem_posix;
      //binfo.free_mem = &free_mem_posix;
      binfo.allocatortype = info.allocatortype = POSIX_ALLOCATOR;
    } else {
      // default: regular malloc, no pool
      binfo.allocatortype = info.allocatortype = NO_POOL;
    }

    info.numunits = iniparser_getint(cfg, (char*)"system:key_pool_size", 128);
    info.unitsize = iniparser_getint(cfg, (char*)"system:key_pool_unit", 16);
    info.alignment = info.unitsize;
    binfo.keypool = (mempool_t *)malloc(sizeof(mempool_t));
    binfo.keypool->base = binfo.keypool->nextfreeblock = NULL;
    pool_setup(&info, binfo.keypool);
    fprintf(stdout, "key pool initialized - base is %p, next free %p total %d free %d\n", binfo.keypool->base, binfo.keypool->nextfreeblock, binfo.keypool->num_blocks, binfo.keypool->num_freeblocks);
    
    info.numunits = iniparser_getint(cfg, (char*)"system:value_pool_size", 128);
    info.unitsize = iniparser_getint(cfg, (char*)"system:value_pool_unit", 4096);
    info.alignment = info.unitsize;
    binfo.valuepool = (mempool_t *)malloc(sizeof(mempool_t));
    binfo.valuepool->base = binfo.valuepool->nextfreeblock = NULL;
    pool_setup(&info, binfo.valuepool);
    fprintf(stdout, "value pool initialized - base is %p\n", binfo.valuepool->base);
    
    binfo.num_devices = iniparser_getint(cfg, (char*)"kvs:num_devices", 1);

    str = iniparser_getstring(cfg, (char*)"kvs:write_mode", (char*)"sync");
    binfo.kv_write_mode = (str[0]=='s')?(1):(0);

    str = iniparser_getstring(cfg, (char*)"kvs:allow_sleep", (char*)"true");
    binfo.allow_sleep = (str[0]=='t')?(1):(0);
    
#if defined(__KV_BENCH) || defined(__KVV_BENCH) || defined (__AS_BENCH)
    str = iniparser_getstring(cfg, (char*)"kvs:store_option", (char*)"post");
    if (str[0] == 'i' || str[0] == 'I') binfo.store_option = 0;    // KVS_STORE_IDEMPOTENT
    else if (str[0] == 'p' || str[0] == 'P') binfo.store_option = 1;  // KVS_STORE_POST
    else binfo.store_option = 2; // KVS_STORE_APPEND

    //str = iniparser_getstring(cfg, (char*)"kvs:write_mode", (char*)"sync");
    //binfo.kv_write_mode = (str[0]=='s')?(1):(0);

    if(binfo.kv_write_mode == 0) { // aio
      binfo.queue_depth = iniparser_getint(cfg, (char*)"kvs:queue_depth", 8);
      binfo.aiothreads_per_device = iniparser_getint(cfg, (char*)"kvs:aiothreads_per_device", 2);

      str = iniparser_getstring(cfg, (char*)"kvs:core_ids", (char*)"{}");
      strcpy(binfo.core_ids, str);
    }
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
    str = iniparser_getstring(cfg, (char*)"blobfs:spdk_bdev", (char*)"");
    strcpy(binfo.spdk_bdev, str);
    binfo.spdk_cache_size = iniparser_getint(cfg, (char*)"blobfs:spdk_cache_size", 4096);
    fprintf(stderr, "SPDK Conf file: %s, bdev: %s, cache size: %lu.\n", binfo.spdk_conf_file, binfo.spdk_bdev, binfo.spdk_cache_size);
#endif
    
    str = iniparser_getstring(cfg, (char*)"log:filename", (char*)"");
    strcpy(binfo.log_filename, str);
    /*
    str = iniparser_getstring(cfg, (char*)"log:syslog_filename", (char*)"syslog");
    strcpy(binfo.syslog_filename, str);
#ifdef __TIME_CHECK
    str = iniparser_getstring(cfg, (char*)"log:timelog_filename", (char*)"syslog");
    strcpy(binfo.timelog_filename, str);
#endif
    */
    binfo.cache_size =
        iniparser_getint(cfg, (char*)"db_config:cache_size_MB", 128);
    binfo.cache_size *= (1024*1024);

    str = iniparser_getstring(cfg, (char*)"db_config:compaction_mode",
                                   (char*)"auto");
    if (str[0] == 'a' || str[0] == 'A') binfo.auto_compaction = 1;
    else binfo.auto_compaction = 0;
#if defined(__LEVEL_BENCH) || defined(__ROCKS_BENCH) || defined(__WT_BENCH) || defined(__BLOBFS_ROCKS_BENCH)
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

    printf("db name %s\n", binfo.filename);

    str = iniparser_getstring(cfg, (char*)"db_file:init_filename",
                                   binfo.filename);
    strcpy(binfo.init_filename, str);

    binfo.nfiles = iniparser_getint(cfg, (char*)"db_file:nfiles", 1);

    str = iniparser_getstring(cfg, (char*)"population:pre_gen",
			      (char*)"false");
    binfo.pre_gen = (str[0]=='t')?(1):(0);
    
    str = iniparser_getstring(cfg, (char*)"population:seq_fill",
			      (char*)"false");
    binfo.seq_fill = (str[0]=='t')?(1):(0);

    str = iniparser_getstring(cfg, (char*)"population:insert_and_update",
			      (char*)"false");
    binfo.insert_and_update = (str[0]=='t')?(1):(0);

    binfo.pop_nthreads = iniparser_getint(cfg, (char*)"population:nthreads",
                                               ncores*2);
    if (binfo.pop_nthreads < 1) binfo.pop_nthreads = ncores*2;
    //if (binfo.pop_nthreads > binfo.nfiles && binfo.seq_fill != 1) binfo.pop_nthreads = binfo.nfiles; // JP: non sequential fill only supports single thread insertion

    //init_cpu_aff(&binfo);

    binfo.pop_batchsize = iniparser_getint(cfg, (char*)"population:batchsize",
                                                4096);

    str = iniparser_getstring(cfg, (char*)"population:periodic_commit",
                                   (char*)"no");
    if (str[0] == 'n' /*|| binfo.nthreads == 1*/) binfo.pop_commit = 0;
    else binfo.pop_commit = 1;

    str = iniparser_getstring(cfg, (char*)"population:fdb_flush_wal",
                                   (char*)"no");
    if (str[0] == 'n' /*|| binfo.nthreads == 1*/) binfo.fdb_flush_wal = 0;
    else binfo.fdb_flush_wal = 1;

    // key length
    str = iniparser_getstring(cfg, (char*)"key_length:distribution",
                                   (char*)"normal");
    if (str[0] == 'n') {
        binfo.keylen.type = RND_NORMAL;
        binfo.keylen.a = iniparser_getint(cfg, (char*)"key_length:median", 64);
        binfo.keylen.b =
            iniparser_getint(cfg, (char*)"key_length:standard_deviation", 8);
    }else if (str[0] == 'u'){
        binfo.keylen.type = RND_UNIFORM;
        binfo.keylen.a =
            iniparser_getint(cfg, (char*)"key_length:lower_bound", 32);
        binfo.keylen.b =
            iniparser_getint(cfg, (char*)"key_length:upper_bound", 96);
    } else {
      binfo.keylen.type = RND_FIXED;
      binfo.keylen.a = iniparser_getint(cfg, (char*)"key_length:fixed_size", 16);
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
    binfo.nreaders = iniparser_getint(cfg, (char*)"threads:readers", 0);
    binfo.niterators = iniparser_getint(cfg, (char*)"threads:iterators", 0);
    binfo.nwriters = iniparser_getint(cfg, (char*)"threads:writers", 0);
    binfo.ndeleters = iniparser_getint(cfg, (char*)"threads:deleters", 0);
    binfo.reader_ops = iniparser_getint(cfg, (char*)"threads:reader_ops", 0);
    binfo.writer_ops = iniparser_getint(cfg, (char*)"threads:writer_ops", 0);
    str = iniparser_getstring(cfg, (char*)"threads:disjoint_write", (char*)"false");
    if (str[0] == 't' || str[0] == 'T' || str[0] == 'e' || str[0] == 'E') {
        // enabled
        binfo.disjoint_write = 1;
    } else {
        binfo.disjoint_write = 0;
    }
#if defined __KV_BENCH
    binfo.nfiles = binfo.num_devices;
#endif
    init_cpu_aff(&binfo);
    
    // create keygen structure
    _set_keygen(&binfo);

    // body length
    str = iniparser_getstring(cfg, (char*)"body_length:distribution",
                                   (char*)"normal");
    if (str[0] == 'n') {
        binfo.bodylen.type = RND_NORMAL;
        binfo.bodylen.a =
            iniparser_getint(cfg, (char*)"body_length:median", 512);
        binfo.bodylen.b =
            iniparser_getint(cfg, (char*)"body_length:standard_deviation", 32);
        DATABUF_MAXLEN = binfo.bodylen.a + 5*binfo.bodylen.b;
    }else if (str[0] == 'u'){
        binfo.bodylen.type = RND_UNIFORM;
        binfo.bodylen.a =
            iniparser_getint(cfg, (char*)"body_length:lower_bound", 448);
        binfo.bodylen.b =
            iniparser_getint(cfg, (char*)"body_length:upper_bound", 576);
        DATABUF_MAXLEN = binfo.bodylen.b;
    } else {
      binfo.bodylen.type = RND_FIXED;
      binfo.bodylen.a =
	iniparser_getint(cfg, (char*)"body_length:fixed_size", 512);
      DATABUF_MAXLEN = binfo.bodylen.a;
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
    char *pt;
    memset(binfo.ratio, 0, sizeof(binfo.ratio));
    char buff[256];
    i = 0;
    strcpy(buff, str);
    pt = strtok ((char*)buff, ":");
    while(pt != NULL){
      binfo.ratio[i++] = atoi(pt);
      pt = strtok(NULL, ":");
    }
    //if(ratio == 100) printf("Yes for ratio control\n");
    //binfo.write_prob = binfo.ratio[1];
    if (binfo.ratio[1] + binfo.ratio[2] == 0) {
      binfo.nwriters = 0;
    } else if (binfo.ratio[1] + binfo.ratio[2] == 100) {
      binfo.nreaders = 0;
      binfo.ndeleters = 0;
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

    print_term_ms =
        iniparser_getint(cfg, (char*)"latency_monitor:print_term_ms", 100);

    iniparser_free(cfg);
    return binfo;
}

int main(int argc, char **argv){
    int opt;
    int initialize = 1;
    char config_filename[256];
    const char *short_opt = "hef:";
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

            case 'h':
                printf("Usage: %s [OPTIONS]\n", argv[0]);
                printf("  -f file                   file\n");
                printf("  -e, --existing            use existing database file\n");
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
    /*
    cpu_info *cpuinfo = (cpu_info *)malloc(sizeof(cpu_info));
    memset(cpuinfo, 0, sizeof(cpu_info));
    get_cpuinfo(cpuinfo);
    printf("total %d nodes cores %d\n", cpuinfo->num_numanodes, cpuinfo->num_cores_per_numanodes);
    exit(0);
    */
    binfo = get_benchinfo(config_filename);

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

        // open ops log file
        gettimeofday(&gap, NULL);
	/*
        sprintf(filename, "%s_%d_%s.txt",
                binfo.log_filename, (int)gap.tv_sec,
                binfo.dbname);
	*/
	filename_ret =  _get_filename_pos(binfo.log_filename);
	sprintf(filename, "%s/%s-%s.txt",
		str, binfo.dbname, filename_ret);
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
	run_latency_fp = fopen(run_latency_filename, "w");
	run_ops_fp = fopen(run_ops_filename, "w");
	/*
	syslog_filename = (char *)malloc(256);
	filename_ret =  _get_filename_pos(binfo.syslog_filename);
	sprintf(syslog_filename, "%s/%s-run.%s.csv",
		str, binfo.dbname, filename_ret);

#ifdef __TIME_CHECK
	sprintf(timelog_filename, "%s_%d_%s.txt",
		binfo.timelog_filename, (int)gap.tv_sec,
		binfo.dbname);
	timelog_fp = fopen(timelog_filename, "w");
#endif
	*/
    }

    binfo.initialize = initialize;

    _print_benchinfo(&binfo);
    //exit(0);
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
    /*
    if(syslog_filename){
      free(syslog_filename);
    }
    if(timelog_fp) {
      fclose(timelog_fp);
    }
    */
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
    if(binfo.device_name)
      free(binfo.device_name);
    if(binfo.spdk_conf_file)
      free(binfo.spdk_conf_file);
    if(binfo.spdk_bdev)
      free(binfo.spdk_bdev);
    if(binfo.core_ids)
      free(binfo.core_ids);
    if(binfo.name_space)
      free(binfo.name_space);
    return 0;
}
