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

#include <boost/tokenizer.hpp>
#include "kvs_adi.h"
#include "kvs_adi_internal.h"

#include "kv_namespace.hpp"
#include "kv_emulator.hpp"
#include "kv_device.hpp"

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

    if (dev->get_dev_type() == KV_DEV_TYPE_EMULATOR) {
        // get IOPS model parameters from device config file
        kv_config *devconfig = dev->get_config();
        std::string model_string = devconfig->getkv("iops_model", "parameters");
        bool_t use_iops_model = dev->use_iops_model();

        std::vector<double> iops_model_parameters;

        if (model_string.size() > 0) {
            typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
            boost::char_separator<char> sep(" ,");
            tokenizer tokens(model_string, sep);
            for (tokenizer::iterator tok_iter = tokens.begin(); tok_iter != tokens.end(); ++tok_iter) {
                // std::cout << "IOPS paramters: " << *tok_iter << std::endl;
                iops_model_parameters.push_back(std::stod(*tok_iter));
            }
        }

        // allocate kvstore
        m_emul = new kv_emulator(m_ns_stat.capacity, iops_model_parameters, use_iops_model, nsid);

        m_dummy   = new kv_noop_emulator(m_ns_stat.capacity);
        m_kvstore = m_emul;

    } else {
        fprintf(stderr, "Unsupported device detected, please validate device path and reinitialize.\n");
        abort();
    }

    if (m_kvstore == NULL) {
        m_init_status = KV_ERR_DEV_INIT;
    } else {
        m_init_status = m_kvstore->get_init_status();
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
    *nsinfo = m_ns_info;
    return KV_SUCCESS;
}

kv_result kv_namespace_internal::kv_get_namespace_stat(kv_namespace_stat *ns_st) {
    if (ns_st == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    // update space consumption from kvstore
    // XXX only track 64 bit
    m_ns_stat.unallocated_capacity = m_kvstore->get_available();

    *ns_st = m_ns_stat;
    return KV_SUCCESS;
}

// direct interact with kv storage
// this is processed off submission Q
kv_result kv_namespace_internal::kv_purge(uint8_t ks_id,kv_purge_option option, void *ioctx) {
    set_purge_in_progress(TRUE); 

    if(ks_id < SAMSUNG_MIN_KEYSPACE_ID || ks_id >= SAMSUNG_MAX_KEYSPACE_CNT){
        return KV_ERR_KEYSPACE_INVALID;
    }

    kv_result res = m_kvstore->kv_purge(ks_id, option, ioctx);

    // restore capacity
    if (res == KV_SUCCESS) {
        m_ns_stat.unallocated_capacity = m_ns_stat.capacity;
    }
    set_purge_in_progress(FALSE);

    return res;
}


// directly work with kvstore
kv_result kv_namespace_internal::kv_store(uint8_t ks_id, const kv_key *key, const kv_value *value, uint8_t option, uint32_t *consumed_bytes, void *ioctx) {
    if (key == NULL || value == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    if(ks_id < SAMSUNG_MIN_KEYSPACE_ID || ks_id >= SAMSUNG_MAX_KEYSPACE_CNT){
        return KV_ERR_KEYSPACE_INVALID;
    }

    kv_result res = m_kvstore->kv_store(ks_id, key, value, option, consumed_bytes, ioctx);

    if (res == KV_SUCCESS && consumed_bytes != NULL) {
        // update capacity
        m_ns_stat.unallocated_capacity -= *consumed_bytes;
    }

    return res;
}

kv_result kv_namespace_internal::kv_delete(uint8_t ks_id, const kv_key *key, uint8_t option, uint32_t *recovered_bytes, void *ioctx) {
    if (key == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    if(ks_id <SAMSUNG_MIN_KEYSPACE_ID || ks_id >= SAMSUNG_MAX_KEYSPACE_CNT){
        return KV_ERR_KEYSPACE_INVALID;
    }

    kv_result res = m_kvstore->kv_delete(ks_id, key, option, recovered_bytes, ioctx);

    if (res == KV_SUCCESS && recovered_bytes != NULL) {
        // update capacity
        m_ns_stat.unallocated_capacity += *recovered_bytes;
    }
    return res;
}


kv_result kv_namespace_internal::kv_exist(uint8_t ks_id, const kv_key *key, uint32_t keycount, uint8_t *value, uint32_t &valuesize, void *ioctx) {
    if (key == NULL || value== NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    if(ks_id <SAMSUNG_MIN_KEYSPACE_ID || ks_id >= SAMSUNG_MAX_KEYSPACE_CNT){
        return KV_ERR_KEYSPACE_INVALID;
    }
    return m_kvstore->kv_exist(ks_id, key, keycount, value, valuesize, ioctx);
}

kv_result kv_namespace_internal::kv_retrieve(uint8_t ks_id, const kv_key *key, uint8_t option, kv_value *value, void *ioctx) {
    if (key == NULL || value == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    if(ks_id <SAMSUNG_MIN_KEYSPACE_ID || ks_id >= SAMSUNG_MAX_KEYSPACE_CNT){
        return KV_ERR_KEYSPACE_INVALID;
    }

    return m_kvstore->kv_retrieve(ks_id, key, option, value, ioctx);
}

// at this stage to interact with kvstore, these APIs are all synchronous
kv_result kv_namespace_internal::kv_open_iterator(uint8_t ks_id, const kv_iterator_option it_op, const kv_group_condition *it_cond, kv_iterator_handle *iter_hdl, void *ioctx) {
    if (iter_hdl == 0 || it_cond == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    if(ks_id <SAMSUNG_MIN_KEYSPACE_ID || ks_id >= SAMSUNG_MAX_KEYSPACE_CNT){
        return KV_ERR_KEYSPACE_INVALID;
    }

    return m_kvstore->kv_open_iterator(ks_id, it_op, it_cond, m_dev->is_keylen_fixed(), iter_hdl, ioctx);
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

kv_result kv_namespace_internal::kv_delete_group(uint8_t ks_id, kv_group_condition *grp_cond, uint64_t *recovered_bytes, void *ioctx) {
    if (grp_cond == NULL) {
        return KV_ERR_PARAM_INVALID;
    }
    if(ks_id <SAMSUNG_MIN_KEYSPACE_ID || ks_id >= SAMSUNG_MAX_KEYSPACE_CNT){
        return KV_ERR_KEYSPACE_INVALID;
    }

    return m_kvstore->kv_delete_group(ks_id, grp_cond, recovered_bytes, ioctx);
}

uint64_t kv_namespace_internal::get_total_capacity() {
    return m_kvstore->get_total_capacity();
}

uint64_t kv_namespace_internal::get_available() {
    return m_kvstore->get_available();
}

// only use it for emulator
void kv_namespace_internal::swap_device(bool usedummy) {
    if (m_dev->get_dev_type() == KV_DEV_TYPE_EMULATOR) {
        m_kvstore = (usedummy)? m_dummy:m_emul;
    }
}

uint64_t kv_namespace_internal::get_consumed_space() {
    uint64_t capacity = m_kvstore->get_total_capacity();
    m_ns_stat.capacity = capacity;

    uint64_t available = m_kvstore->get_available();
    m_ns_stat.unallocated_capacity = available;

    return (capacity - available);
}


} // end of namespace
