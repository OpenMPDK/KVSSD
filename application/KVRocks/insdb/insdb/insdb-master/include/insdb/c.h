/* Copyright (c) 2011 The LevelDB Authors. All rights reserved.
   Use of this source code is governed by a BSD-style license that can be
   found in the LICENSE file. See the AUTHORS file for names of contributors.

   C bindings for insdb.  May be useful as a stable ABI that can be
   used by programs that keep insdb in a shared library, or for
   a JNI api.

   Does not support:
   . getters for the option types
   . custom comparators that implement key shortening
   . custom iter, db, env, cache implementations using just the C bindings

   Some conventions:

   (1) We expose just opaque struct pointers and functions to clients.
   This allows us to change internal representations without having to
   recompile clients.

   (2) For simplicity, there is no equivalent to the Slice type.  Instead,
   the caller has to pass the pointer and length as separate
   arguments.

   (3) Errors are represented by a null-terminated c string.  NULL
   means no error.  All operations that can raise an error are passed
   a "char** errptr" as the last argument.  One of the following must
   be true on entry:
 *errptr == NULL
 *errptr points to a malloc()ed null-terminated error message
 (On Windows, *errptr must have been malloc()-ed by this library.)
 On success, a insdb routine leaves *errptr unchanged.
 On failure, insdb frees the old value of *errptr and
 set *errptr to a malloc()ed error message.

 (4) Bools have the type unsigned char (0 == false; rest == true)

 (5) All of the pointer arguments must be non-NULL.
 */

#ifndef STORAGE_INSDB_INCLUDE_C_H_
#define STORAGE_INSDB_INCLUDE_C_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include "insdb/export.h"

    /* Exported types */

    typedef struct insdb_t               insdb_t;
    typedef struct insdb_cache_t         insdb_cache_t;
    typedef struct insdb_comparator_t    insdb_comparator_t;
    typedef struct insdb_env_t           insdb_env_t;
    typedef struct insdb_filelock_t      insdb_filelock_t;
    typedef struct insdb_filterpolicy_t  insdb_filterpolicy_t;
    typedef struct insdb_iterator_t      insdb_iterator_t;
    typedef struct insdb_logger_t        insdb_logger_t;
    typedef struct insdb_options_t       insdb_options_t;
    typedef struct insdb_randomfile_t    insdb_randomfile_t;
    typedef struct insdb_readoptions_t   insdb_readoptions_t;
    typedef struct insdb_seqfile_t       insdb_seqfile_t;
    typedef struct insdb_snapshot_t      insdb_snapshot_t;
    typedef struct insdb_writablefile_t  insdb_writablefile_t;
    typedef struct insdb_writebatch_t    insdb_writebatch_t;
    typedef struct insdb_writeoptions_t  insdb_writeoptions_t;

    /* DB operations */

    INSDB_EXPORT insdb_t* insdb_open(const insdb_options_t* options,
            const char* name, char** errptr);

    INSDB_EXPORT void insdb_close(insdb_t* db);

    INSDB_EXPORT void insdb_put(insdb_t* db,
            const insdb_writeoptions_t* options,
            const char* key, size_t keylen, const char* val,
            size_t vallen, char** errptr);

    INSDB_EXPORT void insdb_delete(insdb_t* db,
            const insdb_writeoptions_t* options,
            const char* key, size_t keylen,
            char** errptr);

    INSDB_EXPORT void insdb_write(insdb_t* db,
            const insdb_writeoptions_t* options,
            insdb_writebatch_t* batch, char** errptr);

    /* Returns NULL if not found.  A malloc()ed array otherwise.
       Stores the length of the array in *vallen. */
    INSDB_EXPORT char* insdb_get(insdb_t* db,
            const insdb_readoptions_t* options,
            const char* key, size_t keylen, size_t* vallen,
            char** errptr);

    INSDB_EXPORT insdb_iterator_t* insdb_create_iterator(
            insdb_t* db, const insdb_readoptions_t* options);

    INSDB_EXPORT const insdb_snapshot_t* insdb_create_snapshot(insdb_t* db);

    INSDB_EXPORT void insdb_release_snapshot(
            insdb_t* db, const insdb_snapshot_t* snapshot);

    /* Returns NULL if property name is unknown.
       Else returns a pointer to a malloc()-ed null-terminated value. */
    INSDB_EXPORT char* insdb_property_value(insdb_t* db,
            const char* propname);

    INSDB_EXPORT void insdb_approximate_sizes(
            insdb_t* db, int num_ranges, const char* const* range_start_key,
            const size_t* range_start_key_len, const char* const* range_limit_key,
            const size_t* range_limit_key_len, uint64_t* sizes);

    INSDB_EXPORT void insdb_compact_range(insdb_t* db, const char* start_key,
            size_t start_key_len,
            const char* limit_key,
            size_t limit_key_len);

    /* Management operations */

    INSDB_EXPORT void insdb_destroy_db(const insdb_options_t* options,
            const char* name, char** errptr);

    INSDB_EXPORT void insdb_repair_db(const insdb_options_t* options,
            const char* name, char** errptr);

    /* Iterator */

    INSDB_EXPORT void insdb_iter_destroy(insdb_iterator_t*);
    INSDB_EXPORT unsigned char insdb_iter_valid(const insdb_iterator_t*);
    INSDB_EXPORT void insdb_iter_seek_to_first(insdb_iterator_t*);
    INSDB_EXPORT void insdb_iter_seek_to_last(insdb_iterator_t*);
    INSDB_EXPORT void insdb_iter_seek(insdb_iterator_t*, const char* k,
            size_t klen);
    INSDB_EXPORT void insdb_iter_next(insdb_iterator_t*);
    INSDB_EXPORT void insdb_iter_prev(insdb_iterator_t*);
    INSDB_EXPORT const char* insdb_iter_key(const insdb_iterator_t*,
            size_t* klen);
    INSDB_EXPORT const char* insdb_iter_value(const insdb_iterator_t*,
            size_t* vlen);
    INSDB_EXPORT void insdb_iter_get_error(const insdb_iterator_t*,
            char** errptr);

    /* Write batch */

    INSDB_EXPORT insdb_writebatch_t* insdb_writebatch_create();
    INSDB_EXPORT void insdb_writebatch_destroy(insdb_writebatch_t*);
    INSDB_EXPORT void insdb_writebatch_clear(insdb_writebatch_t*);
    INSDB_EXPORT void insdb_writebatch_put(insdb_writebatch_t*,
            const char* key, size_t klen,
            const char* val, size_t vlen);
    INSDB_EXPORT void insdb_writebatch_delete(insdb_writebatch_t*,
            const char* key, size_t klen);
    INSDB_EXPORT void insdb_writebatch_iterate(
            insdb_writebatch_t*, void* state,
            void (*put)(void*, const char* k, size_t klen, const char* v, size_t vlen),
	        void (*merge)(void*, const char* k, size_t klen, const char* v, size_t vlen),
            void (*deleted)(void*, const char* k, size_t klen));

    /* Options */

    INSDB_EXPORT insdb_options_t* insdb_options_create();
    INSDB_EXPORT void insdb_options_destroy(insdb_options_t*);
    INSDB_EXPORT void insdb_options_set_comparator(insdb_options_t*,
            insdb_comparator_t*);
    INSDB_EXPORT void insdb_options_set_filter_policy(insdb_options_t*,
            insdb_filterpolicy_t*);
    INSDB_EXPORT void insdb_options_set_create_if_missing(insdb_options_t*,
            unsigned char);
    INSDB_EXPORT void insdb_options_set_error_if_exists(insdb_options_t*,
            unsigned char);
    INSDB_EXPORT void insdb_options_set_paranoid_checks(insdb_options_t*,
            unsigned char);
    INSDB_EXPORT void insdb_options_set_env(insdb_options_t*, insdb_env_t*);
    INSDB_EXPORT void insdb_options_set_info_log(insdb_options_t*,
            insdb_logger_t*);
    INSDB_EXPORT void insdb_options_set_write_buffer_size(insdb_options_t*,
            size_t);
    INSDB_EXPORT void insdb_options_set_max_open_files(insdb_options_t*, int);
    INSDB_EXPORT void insdb_options_set_cache(insdb_options_t*,
            insdb_cache_t*);
    INSDB_EXPORT void insdb_options_set_block_size(insdb_options_t*, size_t);
    INSDB_EXPORT void insdb_options_set_block_restart_interval(
            insdb_options_t*, int);
    INSDB_EXPORT void insdb_options_set_max_file_size(insdb_options_t*,
            size_t);

    enum {
        insdb_no_compression = 0,
        insdb_snappy_compression = 1
    };
    INSDB_EXPORT void insdb_options_set_compression(insdb_options_t*, int);

    /* Comparator */

    INSDB_EXPORT insdb_comparator_t* insdb_comparator_create(
            void* state, void (*destructor)(void*),
            int (*compare)(void*, const char* a, size_t alen, const char* b,
                size_t blen),
            const char* (*name)(void*));
    INSDB_EXPORT void insdb_comparator_destroy(insdb_comparator_t*);

    /* Filter policy */

    INSDB_EXPORT insdb_filterpolicy_t* insdb_filterpolicy_create(
            void* state, void (*destructor)(void*),
            char* (*create_filter)(void*, const char* const* key_array,
                const size_t* key_length_array, int num_keys,
                size_t* filter_length),
            unsigned char (*key_may_match)(void*, const char* key, size_t length,
                const char* filter, size_t filter_length),
            const char* (*name)(void*));
    INSDB_EXPORT void insdb_filterpolicy_destroy(insdb_filterpolicy_t*);

    INSDB_EXPORT insdb_filterpolicy_t* insdb_filterpolicy_create_bloom(
            int bits_per_key);

    /* Read options */

    INSDB_EXPORT insdb_readoptions_t* insdb_readoptions_create();
    INSDB_EXPORT void insdb_readoptions_destroy(insdb_readoptions_t*);
    INSDB_EXPORT void insdb_readoptions_set_verify_checksums(
            insdb_readoptions_t*, unsigned char);
    INSDB_EXPORT void insdb_readoptions_set_fill_cache(insdb_readoptions_t*,
            unsigned char);
    INSDB_EXPORT void insdb_readoptions_set_snapshot(insdb_readoptions_t*,
            const insdb_snapshot_t*);

    /* Write options */

    INSDB_EXPORT insdb_writeoptions_t* insdb_writeoptions_create();
    INSDB_EXPORT void insdb_writeoptions_destroy(insdb_writeoptions_t*);
    INSDB_EXPORT void insdb_writeoptions_set_sync(insdb_writeoptions_t*,
            unsigned char);

    /* Cache */

    INSDB_EXPORT insdb_cache_t* insdb_cache_create_lru(size_t capacity);
    INSDB_EXPORT void insdb_cache_destroy(insdb_cache_t* cache);

    /* Env */

    INSDB_EXPORT insdb_env_t* insdb_create_default_env();
    INSDB_EXPORT void insdb_env_destroy(insdb_env_t*);

    /* Utility */

    /* Calls free(ptr).
REQUIRES: ptr was malloc()-ed and returned by one of the routines
in this file.  Note that in certain cases (typically on Windows), you
may need to call this routine instead of free(ptr) to dispose of
malloc()-ed memory returned by this library. */
    INSDB_EXPORT void insdb_free(void* ptr);

    /* Return the major version number for this release. */
    INSDB_EXPORT int insdb_major_version();

    /* Return the minor version number for this release. */
    INSDB_EXPORT int insdb_minor_version();

#ifdef __cplusplus
}  /* end extern "C" */
#endif

#endif  /* STORAGE_INSDB_INCLUDE_C_H_ */
