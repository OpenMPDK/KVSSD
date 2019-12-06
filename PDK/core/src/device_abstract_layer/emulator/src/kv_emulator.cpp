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

#include <string>
#include <algorithm>
#include <bitset>
#include <string>

#include <time.h>
#include "io_cmd.hpp"
#include "kv_emulator.hpp"

uint64_t _kv_emul_queue_latency;

namespace kvadi {

static kv_timer kv_emul_timer;

kv_emulator::kv_emulator(uint64_t capacity, std::vector<double> iops_model_coefficients, bool_t use_iops_model, uint32_t nsid): stat(iops_model_coefficients), m_capacity(capacity),m_available(capacity), m_use_iops_model(use_iops_model), m_nsid(nsid) {
    memset(m_iterator_list, 0, sizeof(m_iterator_list));
}

// delete any remaining keys in memory
kv_emulator::~kv_emulator() {
    std::unique_lock<std::mutex> lock(m_map_mutex);
    emulator_map_t::iterator it_tmp;
    for(uint32_t i = 0 ; i < SAMSUNG_MAX_KEYSPACE_CNT ; i++){	
      auto it = m_map[i].begin();
      while (it != m_map[i].end()) {
          kv_key *key = it->first;
          free(key->key);
          delete key;

          it_tmp = it;
          it++;
          m_map[i].erase(it_tmp);
      }
    }
}

inline kv_key *new_kv_key(const kv_key *key) {
    kv_key *copied_key = new kv_key();
    copied_key->length = key->length;
    copied_key->key    = malloc(key->length);
    memcpy(copied_key->key, key->key, key->length);

    return copied_key;
}

uint64_t counter = 0;
// basic operations

kv_result kv_emulator::kv_store(uint8_t ks_id, const kv_key *key, const kv_value *value, uint8_t option, uint32_t *consumed_bytes, void *ioctx) {
    (void) ioctx;
    // track consumed spaced
    if (m_capacity <= 0 && m_available < (value->length + key->length)) {
        // fprintf(stderr, "No more device space left\n");
        return KV_ERR_DEV_CAPACITY;
    }

    if (option != KV_STORE_OPT_DEFAULT && option != KV_STORE_OPT_IDEMPOTENT) {
        return KV_ERR_OPTION_INVALID;
    }

    const std::string valstr = std::string((char *)value->value, value->length);
    struct timespec begin;
    if (m_use_iops_model) {
        kv_emul_timer.start2(&begin);
    }
    //const uint64_t start_tick = kv_emul_timer.start();
    {
        std::unique_lock<std::mutex> lock(m_map_mutex);


        auto it = m_map[ks_id].find((kv_key *)key);
        if (it != m_map[ks_id].end()) {
            if (option == KV_STORE_OPT_IDEMPOTENT) return KV_ERR_KEY_EXIST;

            // update space
            m_available -= value->length + it->second.length();

            // overwrite
            it->second = valstr;
            
            *consumed_bytes = value->length;
            if (m_use_iops_model) {
                stat.collect(STAT_UPDATE, value->length);
            }
        }
        else {
            kv_key *new_key = new_kv_key(key);
            m_map[ks_id].emplace(std::make_pair(new_key, std::move(valstr)));

            m_available -= key->length + value->length;

            *consumed_bytes = key->length + value->length;

            if (m_use_iops_model) {
                stat.collect(STAT_INSERT, value->length);
            }
        }
        counter ++;
    }

    if (m_use_iops_model) {
        kv_emul_timer.wait_until2(&begin,stat.get_expected_latency_ns() - _kv_emul_queue_latency);
    }
//    kv_emul_timer.wait_until(start_tick, stat.get_expected_latency_ns(), _kv_emul_queue_latency);

    return KV_SUCCESS;
}

kv_result kv_emulator::kv_retrieve(uint8_t ks_id, const kv_key *key, uint8_t option, kv_value *value, void *ioctx) {
    (void) ioctx;

    kv_result ret = KV_ERR_KEY_NOT_EXIST;

    struct timespec begin;
    if (m_use_iops_model) {
        kv_emul_timer.start2(&begin);
    }

    if (option != KV_RETRIEVE_OPT_DEFAULT) {
        return KV_ERR_OPTION_INVALID;
    }

    //const uint64_t start_tick = kv_emul_timer.start();
    {

        std::unique_lock<std::mutex> lock(m_map_mutex);
        auto it = m_map[ks_id].find((kv_key*)key);
        if (it != m_map[ks_id].end()) {
            uint32_t dlen = it->second.length();
            if(value->offset != 0 && (value->offset >= dlen)){
                return KV_ERR_VALUE_OFFSET_INVALID;
            }
            uint32_t copylen = std::min(dlen - value->offset, value->length);

            memcpy(value->value, it->second.data() + value->offset, copylen);

            if (value->length < dlen - value->offset)
              ret = KV_ERR_BUFFER_SMALL;
            else
              ret = KV_SUCCESS;

            value->length = copylen;
            value->actual_value_size = dlen;

            if (m_use_iops_model) {
                stat.collect(STAT_READ, copylen);
            }
        } else {
            return KV_ERR_KEY_NOT_EXIST;
        }
    }
    if (m_use_iops_model) {
        //kv_emul_timer.wait_until(start_tick, stat.get_expected_latency_ns(), _kv_emul_queue_latency);
        kv_emul_timer.wait_until2(&begin,stat.get_expected_latency_ns() - _kv_emul_queue_latency);
    }
    return ret;
}


kv_result kv_emulator::kv_exist(uint8_t ks_id, const kv_key *key, uint32_t keycount, uint8_t *buffers, uint32_t &buffer_size, void *ioctx) {
    (void) ioctx;

    int bitpos = 0;

    if (keycount == 0) {
        return KV_SUCCESS;
    }

    const uint32_t bytes_to_write = ((keycount -1) / 8) + 1;

    if (bytes_to_write > buffer_size) {
        return KV_ERR_BUFFER_SMALL;
    }

    memset (buffers, 0, bytes_to_write );

    std::unique_lock<std::mutex> lock(m_map_mutex);

    for (uint32_t i = 0 ; i < keycount ; i++, bitpos++) {
        const int setidx     = (bitpos / 8);
        const int bitoffset  =  bitpos - setidx * 8;

        auto it = m_map[ks_id].find((kv_key*)&key[i]);
        if (it != m_map[ks_id].end()) {
            buffers[setidx] |= (1 << bitoffset);
        }
    }

    buffer_size = bytes_to_write;

    return KV_SUCCESS;
}

kv_result kv_emulator::kv_purge(uint8_t ks_id, kv_purge_option option, void *ioctx) {
    (void) ioctx;
    emulator_map_t::iterator it_tmp;
    if (option != KV_PURGE_OPT_DEFAULT) {
        WRITE_WARN("only default purge option is supported");
        return KV_ERR_OPTION_INVALID;
    }

    std::unique_lock<std::mutex> lock(m_map_mutex);
    for (auto it = m_map[ks_id].begin(); it != m_map[ks_id].end(); ) {
        kv_key *key = it->first;
        free(key->key);
        delete key;

        it_tmp = it;
        it++;
        m_map[ks_id].erase(it_tmp);
    }

    m_available = m_capacity;
    return KV_SUCCESS;
}

kv_result kv_emulator::kv_delete(uint8_t ks_id, const kv_key *key, uint8_t option, uint32_t *recovered_bytes, void *ioctx) {
    (void) ioctx;

    if (key == NULL || key->key == NULL) {
        return KV_ERR_KEY_INVALID;
    }

    if (option != KV_DELETE_OPT_DEFAULT && option != KV_DELETE_OPT_ERROR) {
        return KV_ERR_OPTION_INVALID;
    }

    std::unique_lock<std::mutex> lock(m_map_mutex);
    auto it = m_map[ks_id].find((kv_key*)key);
    if (it != m_map[ks_id].end()) {
        kv_key *key = it->first;

        uint32_t len = key->length + it->second.length();
        m_available += len;
        if (recovered_bytes != NULL) {
            *recovered_bytes = len;
        }

        m_map[ks_id].erase(it);
        free(key->key);
        delete key;
    } else {
        if (option == KV_DELETE_OPT_ERROR) {
            return KV_ERR_KEY_NOT_EXIST;
        }
    }

    return KV_SUCCESS;
}

// iterator
kv_result kv_emulator::kv_open_iterator(uint8_t ks_id, const kv_iterator_option opt, const kv_group_condition *cond, bool_t keylen_fixed, kv_iterator_handle *iter_hdl, void *ioctx) {
    (void) ioctx;

    if (cond == NULL || iter_hdl == NULL || cond == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    std::unique_lock<std::mutex> lock(m_it_map_mutex);
    uint32_t cur_it_count = m_it_map.size();
    if (cur_it_count >= SAMSUNG_MAX_ITERATORS) {
        *iter_hdl = 0;
        return KV_ERR_TOO_MANY_ITERATORS_OPEN;
    }

    // check if an existing iterator already have the prefix
    for (int i = 0; i < SAMSUNG_MAX_ITERATORS; i++) {
        if (m_iterator_list[i].prefix == cond->bit_pattern 
            && m_iterator_list[i].bitmask == cond->bitmask
            && m_iterator_list[i].status == 1) {
            *iter_hdl = i+1;
            return KV_ERR_ITERATOR_ALREADY_OPEN;
        }
    }

    _kv_iterator_handle *iH = new _kv_iterator_handle();
    iH->it_op = opt;
    iH->ksid = ks_id;
    iH->it_cond.bitmask = cond->bitmask;
    iH->it_cond.bit_pattern = cond->bit_pattern;
    iH->has_fixed_keylen = keylen_fixed;

    uint32_t prefix = iH->it_cond.bit_pattern & iH->it_cond.bitmask;
    memcpy((void*)iH->current_key, (char *)&prefix, 4);
    iH->keylength = 4;
    iH->end = FALSE;
    iH->ksid = ks_id;

    //std::bitset<32> set0 (*(uint32_t*)iH->current_key);
    //std::cerr << "minkey = " << set0 << std::endl;

    // get next available itid, itid start with 1
    kv_iterator_handle itid = 1;
    for (; itid <= SAMSUNG_MAX_ITERATORS; itid++) {
        if (m_it_map.find(itid) == m_it_map.end()) {
            break;
        }
    }

    m_it_map.insert(std::make_pair(itid, iH));

    // update the list
    m_iterator_list[itid - 1].handle_id = itid;
    m_iterator_list[itid - 1].status = 1;
    m_iterator_list[itid - 1].type = opt;
    m_iterator_list[itid - 1].keyspace_id = ks_id;
    m_iterator_list[itid - 1].prefix = cond->bit_pattern;
    m_iterator_list[itid - 1].bitmask = cond->bitmask;
    m_iterator_list[itid - 1].is_eof = 0;

    (*iter_hdl) = itid;
    return KV_SUCCESS;
}

kv_result kv_emulator::kv_iterator_next_set(kv_iterator_handle iter_handle_id, kv_iterator_list *iter_list, void *ioctx) {
    (void) ioctx;

    if (iter_handle_id == 0 || iter_list == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    std::unique_lock<std::mutex> lock(m_map_mutex);
    _kv_iterator_handle *iter_hdl = NULL;
    auto it1 = m_it_map.find(iter_handle_id);
    if (it1 != m_it_map.end()) {
        iter_hdl = it1->second;
    } else {
        return KV_ERR_ITERATOR_NOT_EXIST;
    }

    const bool include_value = iter_hdl->it_op == KV_ITERATOR_OPT_KV || iter_hdl->it_op == KV_ITERATOR_OPT_KV_WITH_DELETE;
    const bool delete_value = iter_hdl->it_op == KV_ITERATOR_OPT_KV_WITH_DELETE;

    kv_key key;
    key.key = iter_hdl->current_key;
    key.length = iter_hdl->keylength;

    // treat bitmask of 0 as iterating all keys
    bool iterate_all = (iter_hdl->it_cond.bitmask == 0);

    bool_t end = TRUE;
    iter_list->end = TRUE;

    iter_list->num_entries = 0;
    const uint32_t buffer_size  = iter_list->size;
    char *buffer = (char *) iter_list->it_list;
    uint32_t buffer_pos = 0;
    int counter = 0;


    uint32_t prefix = 0;
    int8_t ks_id = iter_hdl->ksid;
    auto it = m_map[ks_id].lower_bound(&key);
    while (it != m_map[ks_id].end()) {
        const int klength = it->first->length;
        const int vlength = it->second.length();

        // only to try matching when there is a valid bitmask
        if (!iterate_all) {
            // match leading 4 bytes
            memcpy(&prefix, it->first->key, 4);

            // if no more match, which means we reached the end of matching list
            if ((prefix & iter_hdl->it_cond.bitmask) != 
                (iter_hdl->it_cond.bit_pattern & iter_hdl->it_cond.bitmask)) {
                iter_list->end = TRUE;
                end = TRUE;
                break;
            }
        }

        // found a key
        size_t datasize = klength;
        if (!iter_hdl->has_fixed_keylen) {
            datasize += sizeof(uint32_t);
        }
        datasize += (include_value)? (vlength  + sizeof(uint32_t)):0;

        if ((buffer_pos + datasize) > buffer_size) {
            // save the current key for next iteration
            iter_list->end = FALSE;
            end = FALSE;
            iter_hdl->keylength = klength;
            memcpy(iter_hdl->current_key, it->first->key, klength);
            //std::cerr << "save  key  " << set0 << std::endl;
            // printf("no more buffer space\n");
            break;
        }

        //std::cerr << "found key  " << set0 << std::endl;
        // only output key len when key size is not fixed
        if (!iter_hdl->has_fixed_keylen) {
            memcpy(buffer + buffer_pos, &klength, sizeof(uint32_t));
            buffer_pos += sizeof(uint32_t);
        }
        memcpy(buffer + buffer_pos, it->first->key, klength);
        buffer_pos += klength;

        if (include_value) {
            memcpy(buffer + buffer_pos, &vlength, sizeof(kv_value_t));
            buffer_pos += sizeof(kv_value_t);

            memcpy(buffer + buffer_pos, it->second.data(), vlength);
            buffer_pos += vlength;
        }
        counter++;

        if (delete_value) {
            it = m_map[ks_id].erase(it);
        } else {
            it++;
        }
    }
    //printf("Emulator internal iterator: XXX got entries %d\n", counter);
    iter_list->num_entries = counter;
    iter_list->size = buffer_pos;
    if (end != TRUE) {
        m_iterator_list[iter_handle_id - 1].is_eof = 0;
    } else {
        m_iterator_list[iter_handle_id - 1].is_eof = 1;
    }

    return KV_SUCCESS;
}

// match iterator condition, return max of 1 key
kv_result kv_emulator::kv_iterator_next(kv_iterator_handle iter_handle_id, kv_key *key, kv_value *value, void *ioctx) {
    (void) ioctx;

    if (iter_handle_id == 0 || key == NULL || key->key == NULL || value == NULL || value->value == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    std::unique_lock<std::mutex> lock(m_map_mutex);
    _kv_iterator_handle *iter_hdl = NULL;
    auto it1 = m_it_map.find(iter_handle_id);
    if (it1 != m_it_map.end()) {
        iter_hdl = it1->second;
    } else {
        return KV_ERR_ITERATOR_NOT_EXIST;
    }

    const bool include_value = iter_hdl->it_op == KV_ITERATOR_OPT_KV || iter_hdl->it_op == KV_ITERATOR_OPT_KV_WITH_DELETE;
    const bool delete_value = iter_hdl->it_op == KV_ITERATOR_OPT_KV_WITH_DELETE;

    kv_key key1;
    key1.key = iter_hdl->current_key;
    key1.length = iter_hdl->keylength;

    // check if the end is set from last iteration
    if (iter_hdl->end) {
        iter_hdl->end = TRUE;
        return KV_SUCCESS;
    }

    uint32_t prefix = 0;
    int8_t ks_id = iter_hdl->ksid;
    auto it = m_map[ks_id].lower_bound(&key1);

    // the end
    if (it == m_map[ks_id].end()) {
        iter_hdl->end = TRUE;
        return KV_SUCCESS;
    }

    const uint32_t klength = it->first->length;
    const uint32_t vlength = it->second.length();

    // match leading 4 bytes
    memcpy(&prefix, it->first->key, 4);

    // if no more match, which means we reached the end of matching list
    if ((prefix & iter_hdl->it_cond.bitmask) != iter_hdl->it_cond.bit_pattern) {
        iter_hdl->end = TRUE;
        return KV_SUCCESS;
    }

    // printf("matched 0x%X, current key prefix 0x%X, -- %d\n", to_match, prefix, i);
    // found a key
    // first check key size
    key->length = klength;
    if (klength > key->length) {
        // first save unused key for next iteration 
        iter_hdl->keylength = klength;
        memcpy(iter_hdl->current_key, it->first->key, klength);
        return KV_ERR_BUFFER_SMALL;
    }

    if (iter_hdl->it_op == KV_ITERATOR_OPT_KV || iter_hdl->it_op == KV_ITERATOR_OPT_KV_WITH_DELETE) {
        value->actual_value_size = vlength;
        if (vlength > value->length) {
            value->length= 0;
            value->offset = 0;
            iter_hdl->keylength = klength;
            // first save unused key for next iteration 
            memcpy(iter_hdl->current_key, it->first->key, klength);
            return KV_ERR_BUFFER_SMALL;
        }
    }

    memcpy(key->key, it->first->key, klength);

    if (include_value) {
        memcpy(value->value, it->second.data(), vlength);
        value->length= vlength;
        value->actual_value_size = vlength;
        value->offset = 0;
    }

    // delete the identified key, it points to next element
    if (delete_value) {
        it = m_map[ks_id].erase(it);
    } else {
        it++;
    }
    // save next key for next iteration
    if (it != m_map[ks_id].end()) {
        key->length = klength;
        iter_hdl->keylength = klength;
        memcpy(iter_hdl->current_key, it->first->key, klength);
        m_iterator_list[iter_handle_id - 1].is_eof = 0;
    } else {
        iter_hdl->end = TRUE;
        m_iterator_list[iter_handle_id - 1].is_eof = 1;
    }

    return KV_SUCCESS;
}

kv_result kv_emulator::kv_close_iterator(kv_iterator_handle iter_handle_id, void *ioctx) {
    (void) ioctx;

    if (iter_handle_id <= 0) return KV_ERR_PARAM_INVALID;
    {
        std::unique_lock<std::mutex> lock(m_it_map_mutex);
        auto it = m_it_map.find(iter_handle_id);
        if (it != m_it_map.end()) {
            delete it->second;
            m_it_map.erase(it);
        }

        m_iterator_list[iter_handle_id - 1].status = 0;
    }

    return KV_SUCCESS;
}

// this is sync admin call
kv_result kv_emulator::kv_list_iterators(kv_iterator *iter_list, uint32_t *count, void *ioctx) {

    if (iter_list == NULL || count == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    uint32_t i = 0;
    const uint32_t min = std::min(*count, (uint32_t) SAMSUNG_MAX_ITERATORS);
    std::unique_lock<std::mutex> lock(m_it_map_mutex);

    for (; i < min; i++) {
        iter_list[i].handle_id = m_iterator_list[i].handle_id;
        iter_list[i].status = m_iterator_list[i].status;
        iter_list[i].type = m_iterator_list[i].type;
        iter_list[i].keyspace_id = m_iterator_list[i].keyspace_id;
        iter_list[i].prefix = m_iterator_list[i].prefix;
        iter_list[i].bitmask = m_iterator_list[i].bitmask;
        iter_list[i].is_eof = m_iterator_list[i].is_eof;

        /*The bitpattern of the KV API is of big-endian mode. If the CPU is of little-endian mode,
              the bit pattern and bit mask should be transformed.*/
        iter_list[i].prefix = htobe32(iter_list[i].prefix);
        iter_list[i].bitmask = htobe32(iter_list[i].bitmask);
    }
    *count = min;

    // this is sync admin call
    // call postprocessing after completion of a command
    io_cmd *ioreq = (io_cmd *) ioctx;
    ioreq->call_post_process_func();
    delete ioreq;

    return KV_SUCCESS;
}

kv_result kv_emulator::kv_delete_group(uint8_t ks_id, kv_group_condition *grp_cond, uint64_t *recovered_bytes, void *ioctx) {
    (void) ioctx;

    uint32_t minkey = grp_cond->bitmask & grp_cond->bit_pattern;

    kv_key key;
    key.key = &minkey;
    key.length = 4;

    // 4 leading bytes to match
    uint32_t to_match = grp_cond->bitmask & grp_cond->bit_pattern;
    emulator_map_t::iterator it_tmp;

    std::unique_lock<std::mutex> lock(m_map_mutex);

    auto it = m_map[ks_id].lower_bound(&key);
    while (it != m_map[ks_id].end()) {
        uint32_t prefix = 0;
        memcpy(&prefix, it->first->key, 4);

        // validate, if it no longer match, then we are done
        // as the map is ordered by leading 4 byte as integer
        // in ascending order
        if (((prefix & grp_cond->bitmask) & grp_cond->bit_pattern) != to_match ) {
            return KV_SUCCESS;
        }

        // update reclaimed space first
        kv_key *k = it->first;
        m_available += k->length + it->second.length();

        it_tmp = it;
        it++;
        m_map[ks_id].erase(it_tmp);
    }

    return KV_SUCCESS;
}

kv_result kv_emulator::set_interrupt_handler(const kv_interrupt_handler int_hdl) {
    if (int_hdl == NULL) {
        return KV_ERR_PARAM_INVALID;
    }
    m_interrupt_handler = int_hdl;
    return KV_SUCCESS;
}

kv_interrupt_handler kv_emulator::get_interrupt_handler() {
    return m_interrupt_handler;
}

// shouldn't be called, so just return error.
kv_result kv_emulator::poll_completion(uint32_t timeout_usec, uint32_t *num_events) {
    (void) timeout_usec;
    (void) num_events;
    return KV_ERR_DEV_INIT;
}

uint64_t kv_emulator::get_total_capacity() { return m_capacity;  }
uint64_t kv_emulator::get_available() { return m_available; }

} // end of namespace
