/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2016 Couchbase, Inc
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

#ifndef LIBCOUCHSTORE_FILE_OPS_H
#define LIBCOUCHSTORE_FILE_OPS_H

#include <stdint.h>
#include <sys/types.h>
#include "couch_common.h"

    /**
     * Abstract file handle. Implementations can use it for anything
     * they want, whether a pointer to an allocated data structure, or
     * an integer such as a Unix file descriptor.
     */
    typedef struct couch_file_handle_opaque* couch_file_handle;

    typedef struct {
#ifdef WIN32
        DWORD error;
#else
        int error;
#endif
    } couchstore_error_info_t;



#ifdef __cplusplus
/**
 * An abstract base class that defines the interface of the file
 * I/O primitives used by CouchStore. Passed to couchstore_open_db_ex().
 */
class FileOpsInterface {
public:
    /**
     * Virtual destructor used for optional cleanup
     */
    virtual ~FileOpsInterface() {}

    /**
     * Initialize state (e.g. allocate memory) for a file handle
     * before opening a file.  This method is optional and
     * doesn't need to do anything at all; it can just return NULL
     * if there isn't anything to do.
     *
     * Note: No error checking is done on the result of this call
     * so any failure should be handled accordingly (e.g. error
     * when calling the `open` method).
     */
    virtual couch_file_handle constructor(couchstore_error_info_t* errinfo) = 0;

    /**
     * Open a file.
     *
     * @param on input, a pointer to the file handle that was
     *        returned by the constructor function. The function
     *        can change this value if it wants to; the value
     *        stored here on return is the one that will be passed
     *        to the other functions.
     * @param path the name of the file
     * @param flags flags as specified by UNIX open(2) system call
     * @return COUCHSTORE_SUCCESS upon success.
     */
    virtual couchstore_error_t open(couchstore_error_info_t* errinfo,
                                    couch_file_handle* handle, const char* path,
                                    int oflag) = 0;

    /**
     * Close file associated with this handle.
     *
     * @param handle file handle to close
     * @return COUCHSTORE_SUCCESS upon success, COUCHSTORE_ERROR_FILE_CLOSE if
     *         there was an error.
     */
    virtual couchstore_error_t close(couchstore_error_info_t* errinfo,
                                     couch_file_handle handle) = 0;

    /**
     * Read a chunk of data from a given offset in the file.
     *
     * @param handle file handle to read from
     * @param buf where to store data
     * @param nbyte number of bytes to read
     * @param offset where to read from
     * @return number of bytes read (which may be less than nbytes),
     *         or a value <= 0 if an error occurred
     */
    virtual ssize_t pread(couchstore_error_info_t* errinfo,
                          couch_file_handle handle, void* buf, size_t nbytes,
                          cs_off_t offset) = 0;

    /**
     * Write a chunk of data to a given offset in the file.
     *
     * @param handle file handle to write to
     * @param buf where to read data
     * @param nbyte number of bytes to write
     * @param offset where to write to
     * @return number of bytes written (which may be less than nbytes),
     *         or a value <= 0 if an error occurred
     */
    virtual ssize_t pwrite(couchstore_error_info_t* errinfo,
                           couch_file_handle handle, const void* buf,
                           size_t nbytes, cs_off_t offset) = 0;

    /**
     * Find the end of the file.
     *
     * @param handle file handle to find the offset for
     * @return the offset (from beginning of the file), or -1 if
     *         the operation failed
     */
    virtual cs_off_t goto_eof(couchstore_error_info_t* errinfo,
                              couch_file_handle handle) = 0;

    /**
     * Flush the buffers to disk
     *
     * @param handle file handle to flush
     * @return COUCHSTORE_SUCCESS upon success
     */
    virtual couchstore_error_t sync(couchstore_error_info_t* errinfo,
                                    couch_file_handle handle) = 0;

    /**
     * Give filesystem caching advice.
     * @param handle file handle to give advice on
     * @param offset offset to start at
     * @param len length of range to advise on
     * @param advice the advice type, see couchstore_file_advice_t
     *        in couch_common.h
     */
    virtual couchstore_error_t advise(couchstore_error_info_t* errinfo,
                                      couch_file_handle handle, cs_off_t offset,
                                      cs_off_t len,
                                      couchstore_file_advice_t advice) = 0;

    /**
     * Called as part of shutting down the db instance this instance was
     * passed to. A hook to for releasing allocated resources
     *
     * @param handle file handle to be released
     */
    virtual void destructor(couch_file_handle handle) = 0;
};

#else
    /**
     * Opaque reference to a FileOpsInterface instance
     */
    typedef struct couch_file_ops_opaque* FileOpsInterface;

#endif

#endif
