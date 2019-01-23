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

#include "kvs_adi.h"
#include "kvs_adi_internal.h"

#include "kv_namespace.hpp"
#include "kv_device.hpp"
#include "kv_linux_kernel.hpp"

namespace kvadi {

kv_device_api *kv_namespace_internal::get_kvstore() {
    return m_kvstore;
}

// everything is synchronous for directly interacting with kvstore
//
uint32_t kv_namespace_internal::get_nsid() {
    return m_nsid;
}

kv_device_internal *kv_namespace_internal::get_dev() {
    return m_dev;
}

kv_result kv_namespace_internal::get_init_status() { 
    return m_init_status; 
}

kv_namespace_internal::kv_namespace_internal(kv_device_internal *dev, uint32_t nsid, const kv_namespace *ns) {
    m_nsid = nsid;
    m_dev = dev;

    if (ns != NULL) {
        m_ns_info = *ns;
    }

    m_ns_stat.nsid = nsid;
    m_ns_stat.attached = TRUE;
    m_ns_stat.capacity = m_ns_info.capacity;
    m_ns_stat.unallocated_capacity = m_ns_stat.capacity;

    m_kvstore = new kv_linux_kernel(dev);
    m_dummy   = NULL;

    if (m_kvstore != NULL ) {
        m_init_status = m_kvstore->get_init_status();
    } else {
        m_init_status = KV_ERR_DEV_INIT;
    }
}

kv_namespace_internal::~kv_namespace_internal() {
    if (m_kvstore) delete m_kvstore;
    if (m_dummy) delete m_dummy;
}

bool_t kv_namespace_internal::is_purge_in_progress() {
    return m_purging_in_progress;
}

void kv_namespace_internal::set_purge_in_progress(bool_t in_progress) {
    m_purging_in_progress = in_progress;
}


kv_result kv_namespace_internal::kv_get_namespace_info(kv_namespace *nsinfo) {
    if (nsinfo == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    m_ns_info.capacity = m_kvstore->get_total_capacity();
    m_ns_info.nsid = m_nsid;

    *nsinfo = m_ns_info;
    return KV_SUCCESS;
}

kv_result kv_namespace_internal::kv_get_namespace_stat(kv_namespace_stat *ns_st) {
    if (ns_st == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    // update space consumption from kvstore
    m_ns_stat.unallocated_capacity = m_kvstore->get_available();
    m_ns_stat.nsid = m_nsid;

    *ns_st = m_ns_stat;
    return KV_SUCCESS;
}

// direct interact with kv storage
// this is processed off submission Q
kv_result kv_namespace_internal::kv_purge(kv_purge_option option, void *ioctx) {
    set_purge_in_progress(TRUE); 
    kv_result res = m_kvstore->kv_purge(option, ioctx);

    set_purge_in_progress(FALSE);

    return res;
}


// directly work with kvstore
kv_result kv_namespace_internal::kv_store(const kv_key *key, const kv_value *value, uint8_t option, uint32_t *consumed_bytes, void *ioctx) {
    (void) consumed_bytes;
    if (key == NULL || value == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    kv_result res = m_kvstore->kv_store(key, value, option, consumed_bytes, ioctx);

    return res;
}

kv_result kv_namespace_internal::kv_delete(const kv_key *key, uint8_t option, uint32_t *recovered_bytes, void *ioctx) {
    (void) recovered_bytes;
    if (key == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    kv_result res = m_kvstore->kv_delete(key, option, recovered_bytes, ioctx);

    return res;
}


kv_result kv_namespace_internal::kv_exist(const kv_key *key, uint32_t keycount, uint8_t *value, uint32_t &valuesize, void *ioctx) {
    if (key == NULL || value== NULL) {
        return KV_ERR_PARAM_INVALID;
    }
    return m_kvstore->kv_exist(key, keycount, value, valuesize, ioctx);
}

kv_result kv_namespace_internal::kv_retrieve(const kv_key *key, uint8_t option, kv_value *value, void *ioctx) {
    if (key == NULL || value == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    return m_kvstore->kv_retrieve(key, option, value, ioctx);
}


// at this stage to interact with kvstore, these APIs are all synchronous
kv_result kv_namespace_internal::kv_open_iterator(const kv_iterator_option it_op, const kv_group_condition *it_cond, kv_iterator_handle *iter_hdl, void *ioctx) {
    if (iter_hdl == 0 || it_cond == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    return m_kvstore->kv_open_iterator(it_op, it_cond, m_dev->is_keylen_fixed(), iter_hdl, ioctx);
}

// return after done, no IO command
kv_result kv_namespace_internal::kv_close_iterator(kv_iterator_handle iter_hdl, void *ioctx) {

    return (m_kvstore->kv_close_iterator(iter_hdl, ioctx));
}

kv_result kv_namespace_internal::kv_iterator_next_set(kv_iterator_handle iter_hdl, kv_iterator_list *iter_list, void *ioctx) {
    if (iter_hdl == 0 || iter_list == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    return m_kvstore->kv_iterator_next_set(iter_hdl, iter_list, ioctx);
}


kv_result kv_namespace_internal::kv_iterator_next(kv_iterator_handle iter_hdl, kv_key *key, kv_value *value, void *ioctx) {
    if (iter_hdl == 0 || key == NULL || value == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    return m_kvstore->kv_iterator_next(iter_hdl, key, value, ioctx);
}

kv_result kv_namespace_internal::kv_list_iterators(kv_iterator *kv_iters, uint32_t *iter_cnt, void *ioctx) {
    if (kv_iters == NULL || iter_cnt == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    return m_kvstore->kv_list_iterators(kv_iters, iter_cnt, ioctx);
}

kv_result kv_namespace_internal::kv_delete_group(kv_group_condition *grp_cond, uint64_t *recovered_bytes, void *ioctx) {
    if (grp_cond == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    return m_kvstore->kv_delete_group(grp_cond, recovered_bytes, ioctx);
}

kv_result kv_namespace_internal::set_interrupt_handler(const kv_interrupt_handler int_hdl) {
    return m_kvstore->set_interrupt_handler(int_hdl);
}

kv_interrupt_handler kv_namespace_internal::get_interrupt_handler() {
    return m_kvstore->get_interrupt_handler();
}

// only use it for emulator
void kv_namespace_internal::swap_device(bool usedummy) { }

uint64_t kv_namespace_internal::get_consumed_space() {
    uint64_t capacity = m_kvstore->get_total_capacity();
    uint64_t available = m_kvstore->get_available();

    return (capacity - available);
}

uint64_t kv_namespace_internal::get_total_capacity() {
    return m_kvstore->get_total_capacity();
}    

uint64_t kv_namespace_internal::get_available() {
    return m_kvstore->get_available();
}

} // end of namespace
