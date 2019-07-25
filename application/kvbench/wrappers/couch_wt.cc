#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <inttypes.h>

#include "wiredtiger.h"
#include "libcouchstore/couch_db.h"
#include "memleak.h"

#define METABUF_MAXLEN (256)
extern int64_t DATABUF_MAXLEN;

struct _db {
    WT_CURSOR *cursor;
    WT_SESSION *session;
    char *filename;
    int sync;
};

static WT_CONNECTION *conn = NULL;
static uint64_t cache_size = 0;
static int indexing_type = 0;
static size_t c_period = 15;
static int compression = 0;
static int split_pct = 0;
static size_t leaf_page_size, int_page_size = 0;

couchstore_error_t couchstore_set_cache(uint64_t size) {
    cache_size = size;
    return COUCHSTORE_SUCCESS;
}
couchstore_error_t couchstore_set_idx_type(int type) {
    indexing_type = type;
    return COUCHSTORE_SUCCESS;
}
couchstore_error_t couchstore_set_chk_period(size_t seconds) {
    c_period = seconds;
    return COUCHSTORE_SUCCESS;
}
couchstore_error_t couchstore_set_compression(int opt) {
    compression = opt;
    return COUCHSTORE_SUCCESS;
}
couchstore_error_t couchstore_set_split_pct(int pct) {
    split_pct = pct;
    return COUCHSTORE_SUCCESS;
}
couchstore_error_t couchstore_set_page_size(size_t leaf_pg_size, size_t int_pg_size) {
    leaf_page_size = leaf_pg_size;
    int_page_size = int_pg_size;
    return COUCHSTORE_SUCCESS;
}

#ifdef PRIu64
    #define _F64 PRIu64
#else
    #define _F64 "llu"
#endif
couchstore_error_t couchstore_open_conn(const char *filename)
{
    int fd;
    int ret;
    char config[256];

    sprintf(config, "create,"
                    "log=(enabled),"
                    "checkpoint=(wait=%d),"
                    "cache_size=%" _F64,
                    (int)c_period,
                    cache_size);
    if (compression) {
        strcat(config, ",extensions=[libwiredtiger_snappy.so]");
    }
    // create directory if not exist
    fd = open(filename, O_RDONLY, 0666);
    if (fd == -1) {
        // create
        char cmd[256];

        sprintf(cmd, "mkdir %s\n", filename);
        ret = system(cmd);
    }

    ret = wiredtiger_open(filename, NULL, config, &conn);
    assert(ret == 0);

    return COUCHSTORE_SUCCESS;
}

couchstore_error_t couchstore_close_conn()
{
    conn->close(conn, NULL);
    return COUCHSTORE_SUCCESS;
}

LIBCOUCHSTORE_API
couchstore_error_t couchstore_open_db(const char *filename,
                                      couchstore_open_flags flags,
                                      Db **pDb)
{
    return couchstore_open_db_ex(filename, flags,
                                 NULL, pDb);
}

LIBCOUCHSTORE_API
couchstore_error_t couchstore_open_db_ex(const char *filename,
                                         couchstore_open_flags flags,
                                         FileOpsInterface *ops,
                                         Db **pDb)
{
    int i, len, ret;
    Db *ppdb;
    char fileonly[256];
    char table_name[256];
    char table_config[256];

    assert(conn);

    *pDb = (Db*)malloc(sizeof(Db));
    ppdb = *pDb;

    ppdb->filename = (char*)malloc(strlen(filename)+1);
    strcpy(ppdb->filename, filename);

    // take filename only (discard directory path)
    len = strlen(filename);
    for (i=len-1; i>=0; --i) {
        if (filename[i] == '/') {
            strcpy(fileonly, filename + (i+1));
            break;
        }
        if (i == 0) { // there is no directory path, filename only
            strcpy(fileonly, filename);
        }
    }

    sprintf(table_name, "table:%s", fileonly);
    sprintf(table_config, " ");
    if (indexing_type == 1) {
        // lsm-tree
        sprintf(table_config,
                "split_pct=%d,leaf_item_max=1KB,"
                "type=lsm,internal_page_max=%zdKB,leaf_page_max=%zdKB,"
                "lsm=(chunk_size=4MB,"
                     "bloom_config=(leaf_page_max=%zdMB))"
                , split_pct, int_page_size, leaf_page_size, leaf_page_size);
    } else {
        sprintf(table_config,
                "split_pct=%d,leaf_item_max=1KB,"
                "internal_page_max=%zdKB,leaf_page_max=%zdKB"
                ,split_pct, int_page_size, leaf_page_size);
    }
    if (compression) {
        strcat(table_config, ",block_compressor=snappy");
    }

    ret = conn->open_session(conn, NULL, NULL, &ppdb->session);
    if (ret != 0) {
        fprintf(stderr, "Error: %s\n",
                        wiredtiger_strerror(ret));
        assert(0);
    }
    ppdb->session->create(ppdb->session, table_name, table_config);
    ppdb->session->open_cursor(ppdb->session, table_name, NULL, NULL, &ppdb->cursor);
    ppdb->sync = (flags & 0x10)?(1):(0);

    return COUCHSTORE_SUCCESS;
}

LIBCOUCHSTORE_API
couchstore_error_t couchstore_close_db(Db *db)
{
    db->cursor->close(db->cursor);
    db->session->close(db->session, NULL);
    free(db->filename);
    free(db);

    return COUCHSTORE_SUCCESS;
}

LIBCOUCHSTORE_API
couchstore_error_t couchstore_db_info(Db *db, DbInfo* info)
{
    struct stat filestat;

    info->filename = db->filename;
    info->doc_count = 0;
    info->deleted_count = 0;
    info->header_position = 0;
    info->last_sequence = 0;

    stat(db->filename, &filestat);
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
  //return COUCHSTORE_SUCCESS;
    int ret;
    unsigned i;
    uint16_t metalen;
    uint8_t *buf;
    WT_ITEM item;
    //WT_ITEM test;

    if (db->sync) {
        ret = db->session->begin_transaction(db->session, "sync");
        assert(ret==0);
    }

    buf = (uint8_t*)malloc(sizeof(metalen) + METABUF_MAXLEN + DATABUF_MAXLEN);

    for (i=0;i<numdocs;++i){
        item.data = docs[i]->id.buf;
        item.size = docs[i]->id.size;
        db->cursor->set_key(db->cursor, &item);

        metalen = _docinfo_to_buf(infos[i], buf + sizeof(metalen));
        memcpy(buf, &metalen, sizeof(metalen));
        memcpy(buf + sizeof(metalen) + metalen, docs[i]->data.buf, docs[i]->data.size);

        item.data = buf;
        item.size = sizeof(metalen) + metalen + docs[i]->data.size;
        db->cursor->set_value(db->cursor, &item);

        ret = db->cursor->insert(db->cursor);
        if (ret != 0) {
            printf("ERR %d\n", ret);
        }

        infos[i]->db_seq = 0;
    }
    free(buf);

    if (db->sync) {
        ret = db->session->commit_transaction(db->session, NULL);
        assert(ret==0);
    }

    completed += numdocs;

    /*
    printf("read --- %s\n", docs[0]->id.buf);
    test.data = docs[0]->id.buf;
    test.size = docs[0]->id.size;
    db->cursor->set_key(db->cursor, &test);
    ret = db->cursor->search(db->cursor);
    assert(ret == 0);
    db->cursor->get_value(db->cursor, &test);
    printf("read value done ---- \n");
    exit(0);
    */
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
couchstore_error_t couchstore_open_document(Db *db,
                                            const void *id,
                                            size_t idlen,
                                            Doc **pDoc,
                                            couchstore_open_options options)
{
  //return COUCHSTORE_SUCCESS;
    int ret;
    WT_ITEM item;

    item.data = id;
    item.size = idlen;
    db->cursor->set_key(db->cursor, &item);
    ret = db->cursor->search(db->cursor);
    assert(ret == 0);

    db->cursor->get_value(db->cursor, &item);
    /*  

    *pDoc = (Doc *)malloc(sizeof(Doc));
    (*pDoc)->id.buf = (char*)id;
    (*pDoc)->id.size = idlen;
    (*pDoc)->data.buf = (char*)malloc(item.size);
    memcpy((*pDoc)->data.buf, item.data, item.size);
    (*pDoc)->data.size = item.size;
    */
    return COUCHSTORE_SUCCESS;
}

LIBCOUCHSTORE_API
couchstore_error_t couchstore_walk_id_tree(Db *db,
                                           const sized_buf* startDocID,
                                           couchstore_docinfos_options options,
                                           couchstore_walk_tree_callback_fn callback,
                                           void *ctx)
{
    int ret;
    int c_ret = 0;
    DocInfo doc_info;
    Doc doc;
    WT_ITEM item;

    item.data = startDocID->buf;
    item.size = startDocID->size;
    db->cursor->set_key(db->cursor, &item);
    ret = db->cursor->search(db->cursor);
    assert(ret == 0);

    do {
        db->cursor->get_key(db->cursor, &item);
        doc_info.id.buf = (char *)malloc(item.size);
        memcpy(doc_info.id.buf, item.data, item.size);

        db->cursor->get_value(db->cursor, &item);
        doc.data.buf = (char *)malloc(item.size);
        memcpy(doc.data.buf, item.data, item.size);

        c_ret = callback(db, 0, &doc_info, 0, NULL, ctx);

        free(doc_info.id.buf);
        free(doc.data.buf);

        if (c_ret != 0) {
            break;
        }

    } while ((ret = db->cursor->next(db->cursor)) == 0);

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
    free(docinfo);
}

LIBCOUCHSTORE_API
couchstore_error_t couchstore_commit(Db *db)
{
    // do nothing
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

