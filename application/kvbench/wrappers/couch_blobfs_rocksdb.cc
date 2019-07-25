#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <gflags/gflags.h>

#include "stopwatch.h"
#include "rocksdb/cache.h"
#include "rocksdb/db.h"
#include "rocksdb/env.h"
#include "rocksdb/table.h"
#include "rocksdb/filter_policy.h"
#include "rocksdb/table.h"
#include "libcouchstore/couch_db.h"

#define METABUF_MAXLEN (256)
extern int64_t DATABUF_MAXLEN;

rocksdb::Env *blobfs_env;

struct _db {
    rocksdb::DB* db;
    rocksdb::Options *options;
    rocksdb::WriteOptions *write_options;
    rocksdb::ReadOptions *read_options;
    std::string *filename;
    rocksdb::Env *env;
};

static uint64_t cache_size = 0;
static uint64_t spdk_cache_size = 0;
static uint64_t wbs_size = 4*1024*1024;
static int bloom_bits_per_key = 0;
static int compaction_style = 0;
static int compression = 0;
static char spdk_config_file[256];

couchstore_error_t couchstore_set_spdk_cache(uint64_t size)
{
  spdk_cache_size = size;
}

couchstore_error_t couchstore_set_cache(uint64_t size)
{
    cache_size = size;
    return COUCHSTORE_SUCCESS;
}
couchstore_error_t couchstore_set_wbs_size(uint64_t size) {
    wbs_size = size;
    return COUCHSTORE_SUCCESS;
}
couchstore_error_t couchstore_set_bloom(int bits_per_key) {
    bloom_bits_per_key = bits_per_key;
    return COUCHSTORE_SUCCESS;
}
couchstore_error_t couchstore_set_compaction_style(int style) {
    compaction_style = style;
    return COUCHSTORE_SUCCESS;
}
couchstore_error_t couchstore_set_compression(int opt) {
    compression = opt;
    return COUCHSTORE_SUCCESS;
}

couchstore_error_t couchstore_setup_device(const char *dev_path,
					   char **device_names,
					   char *config_file,
					   int num_devices,
					   int write_mode, int is_polling)
{
  int i;
  std::string devname, devnames, dirs, dir;
  strcpy(spdk_config_file, config_file);
  char str[5];
  dir = dev_path;
  for(i = 0; i < num_devices; i++) {
    devname = device_names[i];
    devname.replace(0, 1, "N");
    devnames += devname + ",";
    dirs += dir + "/" + std::to_string(i) + ",";
  }
  devnames.erase(devnames.length()-1, 1);
  dirs.erase(dirs.length()-1, 1);
  printf("Pass devnames %s dirs %s \n", devnames.c_str(), dirs.c_str());

  blobfs_env = rocksdb::NewSpdkEnv(rocksdb::Env::Default(), dirs, config_file, devnames, spdk_cache_size);

  if(blobfs_env == NULL) {
    fprintf(stderr, "Could not load SPDK blobfs - check that SPDK mkfs was run "
	    "against block device %s.\n", devnames.c_str());
    exit(1);
  }

  printf("SPDK initialization done %p\n", blobfs_env);

  return COUCHSTORE_SUCCESS;
}

LIBCOUCHSTORE_API
couchstore_error_t couchstore_open_db(const char *filename,
				      couchstore_open_flags flags,
				      Db **pDb)
{
    Db *ppdb;
     *pDb = (Db*)malloc(sizeof(Db));
    ppdb = *pDb;

    ppdb->filename = new std::string(filename);

    ppdb->options = new rocksdb::Options();
    ppdb->env = blobfs_env;
    ppdb->options->env = ppdb->env;
    ppdb->options->create_if_missing = true;
    if (!compression) {
        ppdb->options->compression = rocksdb::kNoCompression;
    }

    ppdb->options->max_background_compactions = 8;
    ppdb->options->max_background_flushes = 2;
    ppdb->options->max_write_buffer_number = 2;
    ppdb->options->write_buffer_size = wbs_size;
    ppdb->options->compaction_style = rocksdb::CompactionStyle(compaction_style);
    /*
    ppdb->options->min_write_buffer_number_to_merge = 1;
    ppdb->options->max_subcompactions = 1;
    ppdb->options->random_access_max_buffer_size = 1024 * 1024;
    ppdb->options->writable_file_max_buffer_size = 1024 * 1024;
    ppdb->options->use_fsync = false;
    ppdb->options->target_file_size_multiplier = 1;
    ppdb->options->level_compaction_dynamic_level_bytes = false;
    ppdb->options->max_bytes_for_level_multiplier = 10;
    
    ppdb->options->num_levels = 6;
    ppdb->options->hard_rate_limit = 2;
    ppdb->options->level0_file_num_compaction_trigger = 8;
    ppdb->options->target_file_size_base = 134217728; 
    ppdb->options->max_bytes_for_level_base = 1073741824;
    ppdb->options->level0_slowdown_writes_trigger=16;
    ppdb->options->level0_stop_writes_trigger = 24;
    ppdb->options->delete_obsolete_files_period_micros = 314572800;
    ppdb->options->bloom_locality = 1;

    ppdb->options->soft_rate_limit = 0;
    ppdb->options->soft_pending_compaction_bytes_limit = 64ull * 1024 * 1024 * 1024;
    ppdb->options->hard_pending_compaction_bytes_limit = 128ull * 1024 * 1024 * 1024;
    ppdb->options->delayed_write_rate = 8388608u;
    ppdb->options->write_thread_max_yield_usec = 100;
    ppdb->options->write_thread_slow_yield_usec = 3;
    ppdb->options->rate_limit_delay_max_milliseconds = 1000;
    ppdb->options->table_cache_numshardbits = 4;
    */
    //ppdb->options->row_cache = rocksdb::NewLRUCache(cache_size, 6, false, 0.0);

    if (cache_size || bloom_bits_per_key) {
        rocksdb::BlockBasedTableOptions table_options;
	if (cache_size) {
	  table_options.block_cache = rocksdb::NewLRUCache(cache_size, 6, false, 0.0);
	  if(table_options.block_cache == NULL){
	    printf("failed to allocation block cache\n");
	    exit(1);
	  }
        }
        if (bloom_bits_per_key) {
            table_options.filter_policy.reset(
			      rocksdb::NewBloomFilterPolicy(bloom_bits_per_key, false));
        }

        ppdb->options->table_factory.reset(
            rocksdb::NewBlockBasedTableFactory(table_options));
    }

    ppdb->options->max_open_files = 500000;

    fprintf(stdout, "open db %s\n", filename);
    rocksdb::Status status = rocksdb::DB::Open(*ppdb->options, *ppdb->filename, &ppdb->db);
    if (!status.ok()) {
      fprintf(stdout, "Failed to open db %s\n", status.ToString().c_str());
      exit(1);
    }
        
    ppdb->read_options = new rocksdb::ReadOptions(true, true);
    ppdb->write_options = new rocksdb::WriteOptions();
    ppdb->write_options->sync = true;
    ppdb->write_options->disableWAL = true;

    return COUCHSTORE_SUCCESS;
}

couchstore_error_t couchstore_set_sync(Db *db, int sync)
{
    db->write_options->sync = sync ? true : false;
    return COUCHSTORE_SUCCESS;
}

couchstore_error_t couchstore_disable_auto_compaction(Db *db, int cpt)
{
    rocksdb::Status status;
    db->options->disable_auto_compactions = cpt;
    delete db->db;
    //status = rocksdb::DB::Open(*db->options, *db->filename, &db->db);
    return COUCHSTORE_SUCCESS;
}

LIBCOUCHSTORE_API
couchstore_error_t couchstore_close_db(Db *db)
{
    delete db->db;
    delete db->options;
    delete db->write_options;
    delete db->read_options;
    //delete db->env;
    delete db->filename;
    free(db);

    return COUCHSTORE_SUCCESS;
}

couchstore_error_t couchstore_exit_env(void)
{
  delete blobfs_env;
  return COUCHSTORE_SUCCESS;

}

LIBCOUCHSTORE_API
couchstore_error_t couchstore_db_info(Db *db, DbInfo* info)
{
    struct stat filestat;

    info->filename = db->filename->c_str();
    info->doc_count = 0;
    info->deleted_count = 0;
    info->header_position = 0;
    info->last_sequence = 0;

    stat(db->filename->c_str(), &filestat);
    info->space_used = filestat.st_size;

    return COUCHSTORE_SUCCESS;
}

size_t _docinfo_to_buf(DocInfo *docinfo, void *buf)
{
    // [db_seq,] rev_seq, deleted, content_meta, rev_meta (size), rev_meta (buf)
    size_t offset = 0;

    memcpy((uint8_t*)buf + offset, &docinfo->rev_seq, sizeof(docinfo->rev_seq));
    offset += sizeof(docinfo->rev_seq);

    memcpy((uint8_t*)buf + offset, &docinfo->deleted, sizeof(docinfo->deleted));
    offset += sizeof(docinfo->deleted);

    memcpy((uint8_t*)buf + offset, &docinfo->content_meta,
           sizeof(docinfo->content_meta));
    offset += sizeof(docinfo->content_meta);

    memcpy((uint8_t*)buf + offset, &docinfo->rev_meta.size,
           sizeof(docinfo->rev_meta.size));
    offset += sizeof(docinfo->rev_meta.size);

    if (docinfo->rev_meta.size > 0) {
        memcpy((uint8_t*)buf + offset, docinfo->rev_meta.buf, docinfo->rev_meta.size);
        offset += docinfo->rev_meta.size;
    }

    return offset;
}

LIBCOUCHSTORE_API
couchstore_error_t couchstore_save_documents(Db *db, Doc* const docs[], DocInfo *infos[],
        unsigned numdocs, couchstore_save_options options)
{

    unsigned i;
    uint8_t *buf;
    rocksdb::Status status;
    rocksdb::WriteBatch wb;
    
    buf = (uint8_t*)malloc(DATABUF_MAXLEN);
    
    for (i=0;i<numdocs;++i){
      memcpy(buf, docs[i]->data.buf, docs[i]->data.size);
      wb.Put(rocksdb::Slice(docs[i]->id.buf, docs[i]->id.size),
	     rocksdb::Slice((char*)buf, docs[i]->data.size));
    }
    free(buf);

    status = db->db->Write(*db->write_options, &wb);
    if (!status.ok()) {
      printf("ERR %s \n", status.ToString().c_str());
    }
    assert(status.ok());

    return COUCHSTORE_SUCCESS;
}

LIBCOUCHSTORE_API
couchstore_error_t couchstore_save_document(Db *db, const Doc *doc, DocInfo *info,
        couchstore_save_options options)
{
    return couchstore_save_documents(db, (Doc**)&doc, (DocInfo**)&info, 1, options);
}

void _buf_to_docinfo(void *buf, size_t size, DocInfo *docinfo)
{
    size_t offset = 0;

    memcpy(&docinfo->rev_seq, (uint8_t*)buf + offset, sizeof(docinfo->rev_seq));
    offset += sizeof(docinfo->rev_seq);

    memcpy(&docinfo->deleted, (uint8_t*)buf + offset, sizeof(docinfo->deleted));
    offset += sizeof(docinfo->deleted);

    memcpy(&docinfo->content_meta, (uint8_t*)buf + offset,
           sizeof(docinfo->content_meta));
    offset += sizeof(docinfo->content_meta);

    memcpy(&docinfo->rev_meta.size, (uint8_t*)buf + offset,
           sizeof(docinfo->rev_meta.size));
    offset += sizeof(docinfo->rev_meta.size);

    if (docinfo->rev_meta.size > 0) {
        //docinfo->rev_meta.buf = (char *)malloc(docinfo->rev_meta.size);
        docinfo->rev_meta.buf = ((char *)docinfo) + sizeof(DocInfo);
        memcpy(docinfo->rev_meta.buf, (uint8_t*)buf + offset, docinfo->rev_meta.size);
        offset += docinfo->rev_meta.size;
    }else{
        docinfo->rev_meta.buf = NULL;
    }
}

LIBCOUCHSTORE_API
couchstore_error_t couchstore_docinfo_by_id(Db *db, const void *id, size_t idlen, DocInfo **pInfo)
{
    rocksdb::Status status;
    std::string *value = NULL;
    size_t valuelen= 0;
    size_t rev_meta_size;
    size_t meta_offset;

    status = db->db->Get(*db->read_options, rocksdb::Slice((char*)id, idlen),
                         value);
    meta_offset = sizeof(uint64_t)*1 + sizeof(int) +
                  sizeof(couchstore_content_meta_flags);
    memcpy(&rev_meta_size, (uint8_t*)value + sizeof(uint16_t) + meta_offset,
           sizeof(size_t));

    *pInfo = (DocInfo *)malloc(sizeof(DocInfo) + rev_meta_size);
    (*pInfo)->id.buf = (char *)id;
    (*pInfo)->id.size = idlen;
    (*pInfo)->size = idlen + valuelen;
    (*pInfo)->bp = 0;
    (*pInfo)->db_seq = 0;
    _buf_to_docinfo((uint8_t*)value + sizeof(uint16_t), valuelen, (*pInfo));

    free(value);

    return COUCHSTORE_SUCCESS;
}

LIBCOUCHSTORE_API
couchstore_error_t couchstore_docinfos_by_id(Db *db, const sized_buf ids[], unsigned numDocs,
        couchstore_docinfos_options options, couchstore_changes_callback_fn callback, void *ctx)
{
    size_t i;
    DocInfo *docinfo;
    rocksdb::Status status;
    std::string *value = NULL;
    size_t valuelen = 0;
    size_t rev_meta_size, max_meta_size = 256;
    size_t meta_offset;

    meta_offset = sizeof(uint64_t)*1 + sizeof(int) + sizeof(couchstore_content_meta_flags);
    docinfo = (DocInfo*)malloc(sizeof(DocInfo) + max_meta_size);

    for (i=0;i<numDocs;++i){
        status = db->db->Get(*db->read_options,
                             rocksdb::Slice(ids[i].buf, ids[i].size), value);

        memcpy(&rev_meta_size, (uint8_t*)value + sizeof(uint16_t) + meta_offset,
               sizeof(size_t));
        if (rev_meta_size > max_meta_size) {
            max_meta_size = rev_meta_size;
            docinfo = (DocInfo*)realloc(docinfo, sizeof(DocInfo) + max_meta_size);
        }

        memset(docinfo, 0, sizeof(DocInfo));
        docinfo->id.buf = ids[i].buf;
        docinfo->id.size = ids[i].size;
        docinfo->size = ids[i].size + valuelen;
        docinfo->bp = 0;
        docinfo->db_seq = 0;
        _buf_to_docinfo((uint8_t*)value + sizeof(uint16_t), valuelen, docinfo);
        free(value);

        callback(db, docinfo, ctx);
    }

    free(docinfo);

    return COUCHSTORE_SUCCESS;
}

LIBCOUCHSTORE_API
couchstore_error_t couchstore_docinfos_by_sequence(Db *db,
                                                   const uint64_t sequence[],
                                                   unsigned numDocs,
                                                   couchstore_docinfos_options options,
                                                   couchstore_changes_callback_fn callback,
                                                   void *ctx)
{
    // do nothing

    return COUCHSTORE_SUCCESS;
}

LIBCOUCHSTORE_API
couchstore_error_t couchstore_open_document(Db *db,
                                            const void *id,
                                            size_t idlen,
                                            Doc **pDoc,
                                            couchstore_open_options options)
{
    rocksdb::Status status;
    std::string tmp;
    char* value = NULL;
    size_t valuelen;

    status = db->db->Get(*db->read_options, rocksdb::Slice((char*)id, idlen),
    			 &tmp);
    //status = db->db->Get(*db->read_options, key, &tmp);

    if (status.ok()) {
      /*
        valuelen = tmp.size();
        value = strdup(tmp.c_str());
      */
    } else {
        valuelen = 0;
        if (!status.IsNotFound()) {
	  //printf("ERR %s\n", status.ToString().c_str());
        }
    }
    assert(status.ok());
    /*
    *pDoc = (Doc *)malloc(sizeof(Doc));
    (*pDoc)->id.buf = (char*)id;
    (*pDoc)->id.size = idlen;
    (*pDoc)->data.buf = (char*)value;
    (*pDoc)->data.size = valuelen;
    */
    return COUCHSTORE_SUCCESS;
}

LIBCOUCHSTORE_API
couchstore_error_t couchstore_delete_document(Db *db,
					      const void *id,
					      size_t idlen,
					      couchstore_open_options options)
{

  rocksdb::Status status;
  status = db->db->Delete(*db->write_options, rocksdb::Slice((char*)id, idlen));
  if(!status.ok()) {
    // not found??

  }

  return COUCHSTORE_SUCCESS;
}

LIBCOUCHSTORE_API
couchstore_error_t couchstore_walk_id_tree(Db *db,
                                           const sized_buf* startDocID,
                                           couchstore_docinfos_options options,
                                           couchstore_walk_tree_callback_fn callback,
                                           void *ctx)
{
    int c_ret = 0;
    rocksdb::Iterator* rit = db->db->NewIterator(rocksdb::ReadOptions());
    rocksdb::Slice startkey = rocksdb::Slice(startDocID->buf, startDocID->size);
    rocksdb::Slice keyptr, valueptr;
    DocInfo doc_info;
    Doc doc;

    rit->Seek(startkey);

    while (rit->Valid()) {
        keyptr = rit->key();
        valueptr = rit->value();

        doc_info.id.buf = (char *)malloc(keyptr.size());
        memcpy(doc_info.id.buf, keyptr.data(), keyptr.size());
        doc.data.buf = (char *)malloc(valueptr.size());
        memcpy(doc.data.buf, valueptr.data(), valueptr.size());

        c_ret = callback(db, 0, &doc_info, 0, NULL, ctx);

        free(doc_info.id.buf);
        free(doc.data.buf);

        if (c_ret != 0) {
            break;
        }

        rit->Next();
    }

    delete rit;

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

LIBCOUCHSTORE_API
couchstore_error_t couchstore_compact_db_ex(Db* source, const char* target_filename,
        uint64_t flags, FileOpsInterface *ops)
{
    return COUCHSTORE_SUCCESS;
}

LIBCOUCHSTORE_API
couchstore_error_t couchstore_compact_db(Db* source, const char* target_filename)
{
    return couchstore_compact_db_ex(source, target_filename, 0x0, NULL);
}

