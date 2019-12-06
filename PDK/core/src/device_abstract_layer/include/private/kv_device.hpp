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

#ifndef _KV_DEVICE_INTERNAL_INCLUDE_H_
#define _KV_DEVICE_INTERNAL_INCLUDE_H_

#include <chrono>
#include <queue>
#include <set>
#include <unordered_map>

#include "kvs_adi.h"
#include "kvs_adi_internal.h"

#include "kv_config.hpp"
#include "kv_namespace.hpp"
#include "queue.hpp"
#include "thread_pool.hpp"

namespace kvadi {

// this class keep all info for an opened device
class kv_device_internal {
public:

    // device configuration file
    static const std::string configfile;

    // set shutdown flag for all queues for threads to detect and exit
    void shutdown_all_queues();

    // if device has fixed key length
    bool_t is_keylen_fixed();

    // activate IOPS model or not
    // if false, bypass the model.
    bool_t use_iops_model();

    bool_t insert_namespace(uint32_t nsid, kv_namespace_internal *ns);

    kv_config*& get_config();

    uint64_t get_capacity();
    uint64_t get_available();

    // get current device #
    uint32_t get_devid();

    // remove a queue
    kv_result remove_queue(uint16_t qid);

    // get CQ id from SQ id
    uint16_t get_cqid_from_sqid(uint16_t sqid);

    ioqueue *get_ioqueue(uint16_t qid);

    // get io queue info given a qid
    kv_result get_io_queue_info(uint16_t qid, kv_queue *queue_info);

    // given a CQ id, get all paired SQ ids
    std::set<uint16_t> get_paired_SQ_ids(uint16_t CQ_id);

    // submit IO cmd to SQ, which is priority queue here
    kv_result submit_io(kv_queue_handle que_hdl, io_cmd *cmd);

    // given que info, create a queue and return associated qid
    // kv_queue_handle is uint16_t
    kv_result create_queue(const kv_queue *queinfo, kv_queue_handle *que_hdl);
    kv_device get_devinfo();
    kv_device_stat get_dev_stat();
    ioqueue *get_queue_internal(uint16_t qid);
    kv_namespace_internal *get_namespace_default();
    kv_namespace_internal *get_namespace(const uint32_t nsid);

    // whether the device has been initialized
    bool_t is_initialized();

    /*** device APIs ***/
    // initialize a device
    // internally the device should be make into the s_global_device_list 
    static kv_result kv_initialize_device(kv_device_init_t *options, kv_device_handle *dev_hdl);
    // clean up a device
    static kv_result kv_cleanup_device(kv_device_handle dev_hdl);
    // get initialized device info given device id
    static kv_result kv_get_device_info(const kv_device_handle dev_hdl, kv_device *devinfo);
    // get device stats
    static kv_result kv_get_device_stat(const kv_device_handle dev_hdl, kv_device_stat *devstat);
    // get device waf
    static kv_result kv_get_device_waf(const kv_device_handle dev_hdl, float *waf);
    // sanitize a device
    static kv_result kv_sanitize(kv_queue_handle que_hdl, kv_device_handle dev_hdl, kv_sanitize_option option, kv_sanitize_pattern *pattern, kv_postprocess_function *post_fn);

    // get the initialized device given device id (kv_device_handle is actually
    // an uint32_t *
    // for internal use only
    static kv_device_internal *get_device_instance(kv_device_handle dev_hdl);
    static kv_device_internal *get_device_instance(uint32_t devid);

    // keep dev_hdl to better interface with C wrapper, on C wrapper just
    // becomes a thin passthrough layer, otherwise these are good member
    // functions instead of static ones by removing dev_hdl
    //
    /*** queue APIs ***/
    // create a queue
    static kv_result kv_create_queue(kv_device_handle dev_hdl, const kv_queue *queinfo, kv_queue_handle *que_hdl);
    // delete a queue
    static kv_result kv_delete_queue(kv_device_handle dev_hdl, kv_queue_handle que_hdl);
    // get all queue ids
    static kv_result kv_get_queue_handles(const kv_device_handle dev_hdl, kv_queue_handle *que_hdls, uint16_t *que_cnt);
    static kv_result kv_get_queue_info(const kv_device_handle dev_hdl, const kv_queue_handle que_hdl, kv_queue *queinfo);
    static kv_result kv_get_queue_stat(const kv_device_handle dev_hdl, const kv_queue_handle que_hdl, kv_queue_stat *que_st);

    // non static
    kv_result kv_get_queue_handles(kv_queue_handle *que_hdls, uint16_t *que_cnt);

    /*** namespace APIs ***/
    static kv_result kv_create_namespace(kv_device_handle dev_hdl, const kv_namespace *ns, kv_namespace_handle ns_hdl);
    static kv_result kv_delete_namespace(kv_device_handle dev_hdl, kv_namespace_handle ns_hdl);
    static kv_result kv_attach_namespace(kv_device_handle dev_hdl, kv_namespace_handle ns_hdl);
    static kv_result kv_detach_namespace(kv_device_handle dev_hdl, kv_namespace_handle ns_hdl);
    static kv_result kv_list_namespaces(const kv_device_handle dev_hdl, kv_namespace_handle *ns_hdls, uint32_t *ns_cnt);
    static kv_result kv_get_namespace_info(const kv_device_handle dev_hdl, const kv_namespace_handle ns_hdl, kv_namespace *nsinfo);
    static kv_result kv_get_namespace_stat(const kv_device_handle dev_hdl, const kv_namespace_handle ns_hdl, kv_namespace_stat *ns_stat);
    static kv_result _kv_bypass_namespace(const kv_device_handle dev_hdl, const kv_namespace_handle ns_hdl, bool_t bypass);

    // async IO APIs are below
    kv_result kv_purge(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, uint8_t ks_id, kv_purge_option option, kv_postprocess_function *post_fn);

    kv_result kv_open_iterator(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, uint8_t ks_id, const kv_iterator_option it_op, const kv_group_condition *it_cond, kv_postprocess_function *post_fn);

    kv_result kv_close_iterator(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, kv_postprocess_function *post_fn, kv_iterator_handle iter_hdl);

    kv_result kv_iterator_next_set(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, kv_iterator_handle iter_hdl, kv_postprocess_function *post_fn, kv_iterator_list *iter_list);

    kv_result kv_iterator_next(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, kv_iterator_handle iter_hdl, kv_postprocess_function *post_fn, kv_key *key, kv_value *value); 

    kv_result kv_list_iterators(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, kv_postprocess_function  *post_fn, kv_iterator *kv_iters, uint32_t *iter_cnt);

    /*** Key value APIs ***/
    kv_result kv_delete(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, uint8_t ks_id, const kv_key *key, kv_delete_option option, kv_postprocess_function *post_fn);
    
    kv_result kv_delete_group(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, uint8_t ks_id, kv_group_condition *grp_cond, kv_postprocess_function *post_fn);

    kv_result kv_exist(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, uint8_t ks_id, const kv_key *key, uint32_t key_cnt, kv_postprocess_function *post_fn, uint32_t buffer_size, uint8_t *buffer);

    kv_result kv_retrieve(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, uint8_t ks_id, const kv_key *key, kv_retrieve_option option, const kv_postprocess_function *post_fn, kv_value *value);
    kv_result kv_store(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, uint8_t ks_id, const kv_key *key, const kv_value *value, kv_store_option option, const kv_postprocess_function *post_fn);
    /*** poll and interrupt handler APIs***/
    // poll will check completion queue, and find corresponding submission
    // queue
    kv_result kv_poll_completion(kv_queue_handle que_hdl, uint32_t timeout_usec, uint32_t *num_completed);

    // set up interrupt handler for host application, will spawn threads to
    // simulate the interrupt effect, using mutex and conditional variable for
    // completion threads to notify at time of completion, signal to the waiting
    // threads with submission queue#
    kv_result kv_set_interrupt_handler(kv_queue_handle que_hdl, const kv_interrupt_handler int_hdl);

    kv_interrupt_handler kv_get_interrupt_handler(kv_queue_handle que_hdl);
    
    bool_t is_polling();
    kv_raw_dev_type get_dev_type();

    // check each namespace consumption, update device level space consumption
    bool_t update_capacity_consumed();

    static uint32_t get_next_devid();

    std::string& get_devpath();

    // get initialization status for any error
    kv_result get_init_status();
    void set_init_status(kv_result result);

    ~kv_device_internal();

private:
    // protect per device object level sychronized access
    std::mutex m_mutex;
 
    // protect device class level sychronized access
    static std::recursive_mutex s_mutex;
    static std::unordered_map<uint32_t, kv_device_internal *> s_global_device_list;
    static uint32_t s_next_devid;

    // private constructor
    kv_device_internal();
    kv_device_internal(kv_device_init_t *options);

    // forbid copy constructors
    kv_device_internal(const kv_device_internal&) = delete;
    kv_device_internal& operator=(const kv_device_internal&) = delete;

    // check if the device has been initialized
    bool_t m_initialized;

    kv_result m_init_status;

    /// control if the namespace data should be saved across library use
    bool_t m_need_persisency;

    // indicate underlying kv storage type
    // such as emulator, or physical kernel based kvssd
    // or SPDK user space based kvssd
    // this is determined by checking device initialization path
    kv_raw_dev_type m_dev_type;

    bool_t m_is_emulator;

    // indicate it's a KV device or a Block device
    kv_device_type m_device_type;

    // for next queue creation
    uint32_t m_next_qid;

    uint32_t m_devid;
    uint32_t m_nsid;

    // this is defined by ADI spec, only contains basic device
    // information
    kv_device m_device_info;
    // maintains queue count
    kv_device_stat m_device_stat;

    // options when device first initialized
    std::string m_devpath;

    // high resolution time when the device first started
    // all other time will be referenced from this point for a unique point in
    // time.
    std::chrono::system_clock::time_point m_start_timepoint;

    // namespace list
    // although samsung only support 1 namespace currently
    // the default namespace with id: 1
    std::unordered_map<int32_t, kv_namespace_internal *> m_ns_list;

    // submission and completion queue info
    std::unordered_map<uint16_t, ioqueue *> m_ioque_list;

    // submission queue to compltion queue pair
    std::unordered_map<uint16_t, uint16_t> m_sq_to_cq_pairs;

    // device configuration from a file
    kv_config *m_config;

    // configuration driven polling or interrupt mode
    bool_t m_is_poll;

    // based configuration, if the device has fixed length or not
    // default is false
    bool_t m_has_fixed_keylen;

    // use IOPS model or not
    bool_t m_use_iops_model;

    // device capacity in byte
    uint64_t m_capacity;

};

} // end of namespace
#endif
