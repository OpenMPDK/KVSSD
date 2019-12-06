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

#ifndef _KV_STORE_INCLUDE_H_
#define _KV_STORE_INCLUDE_H_

#include <string>
#include <unordered_map>
#include <map>
#include <set>
#include <list>
#include <bitset>
#include <unordered_map>
#include "kvs_adi_internal.h"
#include "history.hpp"

/**
 * this is for key value store and iteration in memory
 * to emulate KVSSD behavior through ADI
 * each namespace will have one store
 */

namespace kvadi {

struct CmpEmulPrefix {
    bool operator()(const kv_key* a, const kv_key* b) const {

        const char *strA = (const char *)a->key;
        const char *strB = (const char *)b->key;

        // using leading 4 bytes in ascending order for group and iteration
        uint32_t intA = 0;
        memcpy(&intA, strA, 4);
        uint32_t intB = 0;
        memcpy(&intB, strB, 4);

        // first compare first 32 bits
        if (intA == intB) {
            // if equal, compare length
            if (a->length < b->length) {
                return true;
            } else if (a->length > b->length) {
                return false;
            } else {
                // if same length, compare content
                // (a->length == b->length) {
                return (memcmp(strA + 4, strB + 4, b->length - 4) < 0);
            }
        } else {
            return intA < intB;
        }
    }
};

class kv_noop_emulator : public kv_device_api{
public:
    kv_noop_emulator(uint64_t capacity) {}
    virtual ~kv_noop_emulator() {}

    // basic operations
    kv_result kv_store(uint8_t ks_id, const kv_key *key, const kv_value *value, uint8_t option, uint32_t *consumed_bytes, void *ioctx) { return KV_SUCCESS; }
    kv_result kv_retrieve(uint8_t ks_id, const kv_key *key, uint8_t option, kv_value *value, void *ioctx) { return KV_SUCCESS; }
    kv_result kv_exist(uint8_t ks_id, const kv_key *key, uint32_t keycount, uint8_t *value, uint32_t &valuesize, void *ioctx) { return KV_SUCCESS; }
    kv_result kv_purge( uint8_t ks_id, kv_purge_option option, void *ioctx) { return KV_SUCCESS; }
    kv_result kv_delete(uint8_t ks_id, const kv_key *key, uint8_t option, uint32_t *recovered_bytes, void *ioctx) { return KV_SUCCESS; }
    // iterator
    kv_result kv_open_iterator(uint8_t ks_id, const kv_iterator_option opt, const kv_group_condition *cond, bool_t keylen_fixed, kv_iterator_handle *iter_hdl, void *ioctx) { return KV_SUCCESS; }
    kv_result kv_close_iterator(kv_iterator_handle iter_hdl, void *ioctx) { return KV_SUCCESS; }
    kv_result kv_iterator_next(kv_iterator_handle iter_hdl, kv_key *key, kv_value *value, void *ioctx) { return KV_SUCCESS; }
    kv_result kv_iterator_next_set(kv_iterator_handle iter_hdl, kv_iterator_list *iter_list, void *ioctx) { return KV_SUCCESS; }
    kv_result kv_list_iterators(kv_iterator *iter_list, uint32_t *count, void *ioctx) { return KV_SUCCESS; }
    kv_result kv_delete_group( uint8_t ks_id, kv_group_condition *grp_cond, uint64_t *recovered_bytes, void *ioctx) { return KV_SUCCESS; }

    kv_result set_interrupt_handler(const kv_interrupt_handler int_hdl) { return KV_SUCCESS; }
    kv_interrupt_handler get_interrupt_handler() { return NULL; }
    kv_result poll_completion(uint32_t timeout_usec, uint32_t *num_events) { return KV_SUCCESS; }

    uint64_t get_total_capacity() { return -1; }
    uint64_t get_available() { return -1; }
};

class kv_emulator : public kv_device_api{
public:
    kv_emulator(uint64_t capacity, std::vector<double> iops_model_coefficients, bool_t use_iops_model, uint32_t nsid);
    virtual ~kv_emulator();

    // basic operations
    kv_result kv_store(uint8_t ks_id, const kv_key *key, const kv_value *value, uint8_t option, uint32_t *consumed_bytes, void *ioctx);
    kv_result kv_retrieve(uint8_t ks_id, const kv_key *key, uint8_t option, kv_value *value, void *ioctx);
    kv_result kv_exist(uint8_t ks_id, const kv_key *key, uint32_t keycount, uint8_t *value, uint32_t &valuesize, void *ioctx);
    kv_result kv_purge( uint8_t ks_id, kv_purge_option option, void *ioctx);
    kv_result kv_delete(uint8_t ks_id, const kv_key *key, uint8_t option, uint32_t *recovered_bytes, void *ioctx);
    // iterator
    kv_result kv_open_iterator(uint8_t ks_id, const kv_iterator_option opt, const kv_group_condition *cond, bool_t keylen_fixed, kv_iterator_handle *iter_hdl, void *ioctx);
    kv_result kv_close_iterator(kv_iterator_handle iter_hdl, void *ioctx);
    kv_result kv_iterator_next_set(kv_iterator_handle iter_hdl, kv_iterator_list *iter_list, void *ioctx);
    kv_result kv_iterator_next(kv_iterator_handle iter_hdl, kv_key *key, kv_value *value, void *ioctx);
    kv_result kv_list_iterators(kv_iterator *iter_list, uint32_t *count, void *ioctx);
    kv_result kv_delete_group( uint8_t ks_id, kv_group_condition *grp_cond, uint64_t *recovered_bytes, void *ioctx);

    uint64_t get_total_capacity();
    uint64_t get_available();

    // these do nothing, but to conform API, emulator have queue level operations for
    // device behavior simulation.
    kv_result set_interrupt_handler(const kv_interrupt_handler int_hdl);
    kv_interrupt_handler get_interrupt_handler();
    kv_result poll_completion(uint32_t timeout_usec, uint32_t *num_events);

private:

    kv_history stat;
    inline int insert_to_unordered_map(std::unordered_map<kv_key*, std::string> &unordered, kv_key* key,  const kv_value *value, const std::string &valstr, uint8_t option);
    // max capacity
    uint64_t m_capacity;

    // space available
    uint64_t m_available;

    typedef std::map<kv_key*, std::string, CmpEmulPrefix> emulator_map_t;
    //std::map<uint32_t, std::unordered_map<kv_key*, std::string> > m_map;
    std::map<kv_key*, std::string, CmpEmulPrefix> m_map[SAMSUNG_MAX_KEYSPACE_CNT];
    std::mutex m_map_mutex;

    std::map<int32_t, _kv_iterator_handle *> m_it_map;
    kv_iterator m_iterator_list[SAMSUNG_MAX_ITERATORS];
    std::mutex m_it_map_mutex;

    // use IOPS model or not
    bool_t m_use_iops_model;

    uint32_t m_nsid;

    kv_interrupt_handler m_interrupt_handler;
};



} // end of namespace
#endif
