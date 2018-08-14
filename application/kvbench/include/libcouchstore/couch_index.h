/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#ifndef COUCHSTORE_COUCH_INDEX_H
#define COUCHSTORE_COUCH_INDEX_H

#include <libcouchstore/couch_db.h>

#ifdef __cplusplus
extern "C" {
#endif

    /**
     * Opaque reference to an open index file.
     */
    typedef struct _CouchStoreIndex CouchStoreIndex;

    /*
     * Types of indexes.
     */
    typedef uint64_t couchstore_index_type;
    enum {
        COUCHSTORE_VIEW_PRIMARY_INDEX = 0,  /**< Primary index, maps emitted keys to values */
        COUCHSTORE_VIEW_BACK_INDEX = 1,     /**< Back-index, maps doc IDs to emitted keys */
    };

    /*
     * Available built-in reduce functions to use on the JSON values in primary indexes.
     * These are documented at <http://wiki.apache.org/couchdb/Built-In_Reduce_Functions>
     */
    typedef uint64_t couchstore_json_reducer;
    enum {
        COUCHSTORE_REDUCE_NONE = 0,     /**< No reduction */
        COUCHSTORE_REDUCE_COUNT = 1,    /**< Count rows */
        COUCHSTORE_REDUCE_SUM = 2,      /**< Sum numeric values */
        COUCHSTORE_REDUCE_STATS = 3,    /**< Compute count, min, max, sum, sum of squares */
    };

    
    /**
     * Create a new index file.
     *
     * The file should be closed with couchstore_close_index().
     *
     * @param filename The name of the file containing the index. Any existing file at this path
     *          will be deleted.
     * @param index Pointer to where you want the handle to the index to be
     *           stored.
     * @return COUCHSTORE_SUCCESS for success
     */
    LIBCOUCHSTORE_API
    couchstore_error_t couchstore_create_index(const char *filename,
                                               CouchStoreIndex** index);
    
    /**
     * Close an open index file.
     *
     * @param index Pointer to the index handle to free.
     * @return COUCHSTORE_SUCCESS upon success
     */
    LIBCOUCHSTORE_API
    couchstore_error_t couchstore_close_index(CouchStoreIndex* index);

    /**
     * Read an unsorted key-value file and add its contents to an index file.
     * Each file added will create a new independent index within the file; they are not merged.
     *
     * The key-value file is a sequence of zero or more records, each of which consists of:
     *      key length (16 bits, big-endian)
     *      value length (32 bits, big-endian)
     *      key data
     *      value data
     * The data formats inside the actual keys and values differ with the index types, and are
     * documented in "The Binary (Termless) Format for Views" (view_format.md).
     *
     * @param inputPath The path to the key-value file
     * @param index_type The type of index keys/values in the input file, and the type of index
     *      to generate in the index file. Note: CouchStore can currently add only one back index.
     * @param reduce_function The type of JSON reduce function to apply to the data. Valid only
     *      for primary indexes; ignored in back-indexes.
     * @param index The index file to write to
     * @return COUCHSTORE_SUCCESS on success, else an error code
     */
    LIBCOUCHSTORE_API
    couchstore_error_t couchstore_index_add(const char *inputPath,
                                            couchstore_index_type index_type,
                                            couchstore_json_reducer reduce_function,
                                            CouchStoreIndex* index);

#ifdef __cplusplus
}
#endif
#endif
