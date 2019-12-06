/**
 *   BSD LICENSE
 *
 *   Copyright (c) 2018 Samsung Electronics Co., Ltd.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Samsung Electronics Co., Ltd. nor the names of
 *       its contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _KVS_ADI_INTERNAL_INCLUDE_H_
#define _KVS_ADI_INTERNAL_INCLUDE_H_

#include <stdint.h>
#include <thread>
#include <memory.h>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include "kvs_adi.h"
#include "kvs_utils.h"

#define MAX_LOG_PAGE_SIZE 512
class kv_device_api {
public:
    virtual ~kv_device_api() {}
    //basic operations
    virtual kv_result kv_store(uint8_t ks_id, const kv_key *key, const kv_value *value, uint8_t option, uint32_t *consumed_bytes, void *ioctx) =0;
    virtual kv_result kv_retrieve(uint8_t ks_id, const kv_key *key, uint8_t option, kv_value *value, void *ioctx) =0;
    virtual kv_result kv_exist(uint8_t ks_id, const kv_key *key, uint32_t keycount, uint8_t *value, uint32_t &valuesize, void *ioctx) =0;
    virtual kv_result kv_purge(uint8_t ks_id, kv_purge_option option, void *ioctx) =0;
    virtual kv_result kv_delete(uint8_t ks_id, const kv_key *key, uint8_t option, uint32_t *recovered_bytes, void *ioctx) =0;

    // iterator
    virtual kv_result kv_open_iterator(uint8_t ks_id, const kv_iterator_option opt, const kv_group_condition *cond, bool_t keylen_fixed, kv_iterator_handle *iter_hdl, void *ioctx) =0;
    virtual kv_result kv_close_iterator(kv_iterator_handle iter_hdl, void *ioctx) =0;
    virtual kv_result kv_iterator_next_set(kv_iterator_handle iter_hdl, kv_iterator_list *iter_list, void *ioctx) =0;
    virtual kv_result kv_iterator_next(kv_iterator_handle iter_hdl, kv_key *key, kv_value *value, void *ioctx) =0;
    virtual kv_result kv_list_iterators(kv_iterator *iter_list, uint32_t *count, void *ioctx) =0;
    virtual kv_result kv_delete_group(uint8_t ks_id, kv_group_condition *grp_cond, uint64_t *recovered_bytes, void *ioctx) =0;

    // device setup related
    // Only good for physical devices.
    // for operating in interrtupt mode, which should have a service running checking
    // IO completion automatically.
    virtual kv_result set_interrupt_handler(const kv_interrupt_handler int_hdl) = 0;
    virtual kv_interrupt_handler get_interrupt_handler() = 0;

    // for operating in polling mode, need to call poll API repeatedly to check IO 
    // completion status.
    virtual kv_result poll_completion(uint32_t timeout_usec, uint32_t *num_events) = 0;

    // capacity
    virtual uint64_t get_total_capacity() =0;
    virtual uint64_t get_available() =0;

    // get initialization status
    virtual kv_result get_init_status() { return KV_SUCCESS; }
};

#ifdef __cplusplus
extern "C" {
#endif

/** internal C structure for C++ wrapper */
struct _kv_device_handle {
    uint32_t devid;
    // device object pointer
    void *dev;
};

struct _kv_queue_handle{
    uint16_t qid;
    // device object pointer
    void *dev;
    // queue object
    void *queue;
    // _kv_device_handle devid;
} ;

struct _kv_namespace_handle{
    uint32_t nsid;
    // device object pointer
    void *dev;
    // namespace object pointer
    void *ns;
    // _kv_device_handle devid;
} ;

#define ITERATOR_BUFFER_LEN 32 * 1024
struct _kv_iterator_handle {
    int id;
    char current_key[SAMSUNG_KV_MAX_KEY_LEN];
    int keylength;

    uint32_t nsid;
    int8_t ksid;

    kv_iterator_option it_op;
    kv_group_condition it_cond;

    // indicate if the device has fixed key size
    // default should be true
    bool_t has_fixed_keylen;

    // buffer 32k for single key/value iteration
    uint8_t buffer[ITERATOR_BUFFER_LEN];
    // For determining the end of iteration
    bool_t end;
};

typedef enum {
    KV_DEV_TYPE_NONE = 0,
    KV_DEV_TYPE_EMULATOR = 1,
    KV_DEV_TYPE_LINUX_KERNEL = 2
} kv_raw_dev_type;

#ifdef __cplusplus
} // extern "C"
#endif

#endif
