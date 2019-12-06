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

#ifndef _KV_NAMESPACE_INTERNAL_INCLUDE_H_
#define _KV_NAMESPACE_INTERNAL_INCLUDE_H_

#include "kvs_adi.h"
#include "kvs_adi_internal.h"
#include "kv_device.hpp"

namespace kvadi {

/**
 * this describes internal namespace structure
 * all operations are synchronous once reached here
 */

class kv_device_internal;

class kv_namespace_internal {
public:
    // get namespace id
    uint32_t get_nsid();
    kv_device_internal *get_dev();

    bool_t is_purge_in_progress();
    void set_purge_in_progress(bool_t in_progress);

    // return the device # for this namespace
    // uint32_t get_device_num();

    kv_result kv_get_namespace_info(kv_namespace *ns);
    kv_result kv_get_namespace_stat(kv_namespace_stat *ns_st);

    // all these are sync IO, directly working with kvstore
    kv_result kv_purge( uint8_t ks_id, kv_purge_option option, void *ioctx);
    kv_result kv_delete(uint8_t ks_id, const kv_key *key, uint8_t option, uint32_t *recovered_bytes, void *ioctx);
    kv_result kv_exist(uint8_t ks_id, const kv_key *key, uint32_t keycount, uint8_t *value, uint32_t &valuesize, void *ioctx);
    kv_result kv_retrieve(uint8_t ks_id, const kv_key *key, uint8_t option, kv_value *value, void *ioctx);
    kv_result kv_store(uint8_t ks_id, const kv_key *key, const kv_value *value, uint8_t option, uint32_t *consumed_bytes, void *ioctx);
    kv_result kv_open_iterator(uint8_t ks_id, const kv_iterator_option it_op, const kv_group_condition *it_cond, kv_iterator_handle *iter_hdl, void *ioctx);
    kv_result kv_close_iterator(kv_iterator_handle iter_hdl, void *ioctx);
    kv_result kv_iterator_next(kv_iterator_handle iter_hdl, kv_key *key, kv_value *value, void *ioctx);
    kv_result kv_iterator_next_set(kv_iterator_handle iter_hdl, kv_iterator_list *iter_list, void *ioctx);
    kv_result kv_list_iterators(kv_iterator *kv_iters, uint32_t *iter_cnt, void *ioctx);
    kv_result kv_delete_group( uint8_t ks_id, kv_group_condition *grp_cond, uint64_t *recovered_bytes, void *ioctx);

    kv_result set_interrupt_handler(const kv_interrupt_handler int_hdl);
    kv_interrupt_handler get_interrupt_handler();

    // get consumed bytes
    uint64_t get_consumed_space();

    kv_namespace_internal(kv_device_internal *dev, uint32_t nsid, const kv_namespace *ns);
    ~kv_namespace_internal();

    kv_device_api *get_kvstore();

    void swap_device(bool usedummy);

    uint64_t get_total_capacity();
    uint64_t get_available();

    // get initialization status for any errors
    kv_result get_init_status();

private:
    kv_result m_init_status;

    kv_namespace_internal();

    std::mutex m_mutex;

    // if purging is in progess
    bool_t m_purging_in_progress;
    kv_postprocess_function m_postprocess_func;

    // namespace id
    uint32_t m_nsid;

    // device handle 
    kv_device_internal *m_dev;

    // namespace info
    kv_namespace m_ns_info;

    // namespace stats
    kv_namespace_stat m_ns_stat;

    // kv storage for the namespace
    // iterator mapping
    // std::unordered_map<uint32_t, kv_iterator_internal *> m_iterator_list;
    // maintains a list of iterators
    
    // point to a real kvstore, either a physical one, or an emulator
    // in case of emulator, it's the same object as m_emul
    kv_device_api *m_kvstore;
    kv_device_api *m_emul;

    // only use for testing and latency measurement
    kv_device_api *m_dummy;
};


}
#endif
