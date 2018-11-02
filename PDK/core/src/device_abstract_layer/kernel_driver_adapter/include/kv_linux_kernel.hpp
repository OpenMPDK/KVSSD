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

#ifndef _KV_STORE_LINUX_KERNEL_H_
#define _KV_STORE_LINUX_KERNEL_H_

#include <string>
#include <thread>
#include "kvs_adi_internal.h"
#include "kv_device.hpp"

/**
 * this is for key value store and iteration in memory
 * to emulate KVSSD behavior through ADI
 * each namespace will have one store
 */

namespace kvadi {

class kv_linux_kernel : public kv_device_api{
public:
    kv_linux_kernel(kv_device_internal *dev);
    virtual ~kv_linux_kernel();
    // basic operations
    kv_result kv_store(const kv_key *key, const kv_value *value, uint8_t option, uint32_t *consumed_bytes, void *ioctx);
    kv_result kv_retrieve(const kv_key *key, uint8_t option, kv_value *value, void *ioctx);
    kv_result kv_exist(const kv_key *key, uint32_t keycount, uint8_t *value, uint32_t &valuesize, void *ioctx);
    kv_result kv_purge(kv_purge_option option, void *ioctx);
    kv_result kv_delete(const kv_key *key, uint8_t option, uint32_t *recovered_bytes, void *ioctx);

    // iterator
    kv_result kv_open_iterator(const kv_iterator_option opt, const kv_group_condition *cond, bool_t keylen_fixed, kv_iterator_handle *iter_hdl, void *ioctx);
    kv_result kv_close_iterator(kv_iterator_handle iter_hdl, void *ioctx);
    kv_result kv_iterator_next_set(kv_iterator_handle iter_hdl, kv_iterator_list *iter_list, void *ioctx);
    kv_result kv_iterator_next(kv_iterator_handle iter_hdl, kv_key *key, kv_value *value, void *ioctx);
    kv_result kv_list_iterators(kv_iterator *iter_list, uint32_t *count, void *ioctx);
    kv_result kv_delete_group(kv_group_condition *grp_cond, uint64_t *recovered_bytes, void *ioctx);

    // synchronous admin call
    uint64_t get_total_capacity();
    uint64_t get_available();
    kv_result get_total_capacity(uint64_t *capacity);
    kv_result get_available(uint64_t *available);

    // get current # of commands pending
    // this is global commands pending, not per queue level
    // ideally we should get per queue level info, or directly queueing control
    // XXX assumes a single pair of queues for now
    // TODO needs modification based on kernel based queuing interface
    uint32_t get_cmds_pending_count();

    // for operating device in polling mode
    kv_result set_interrupt_handler(const kv_interrupt_handler int_hdl);
    kv_interrupt_handler get_interrupt_handler();

    kv_result poll_completion(uint32_t timeout_usec, uint32_t *num_events);

    // for operating devices in interrupt mode
    kv_result start_service(uint32_t timeout_usec, uint32_t *num_events);

    // get initialization status
    kv_result get_init_status();

private:
    kv_result check_ioevents(unsigned long long eftd_ctx, uint32_t *num_events);
    // background service checking IO events for interrupt mode
    void kv_observer();

    std::condition_variable m_cond_reqempty;
    std::mutex m_mutex;

    // record how many commands were received by device
    std::atomic<uint64_t> m_req;
    // record how many commands were executed by device
    std::atomic<uint64_t> m_processed;

    uint64_t m_write;
    uint64_t m_read;
    uint64_t m_delete;
    uint64_t m_iter_open;
    uint64_t m_iter_close;
    uint64_t m_iter_next;
    bool m_init;
    // will be shutdown.
    bool m_shutdown;
    // device is ready to use.
    bool m_ready;
    // real nvme device file handle.
    int m_fd;
    // real nvme device event handle.
    int m_efd;
    // real nvme ctxid;
    uint32_t m_ctxid;
    // real nvme name space id.
    uint32_t m_nsid;
    // max capacity
    uint64_t m_capacity;
    // device internal back pointer.
    kv_device_internal *m_dev;

    // if device is operated in polling mode
    // if this is false (interrupt mode), then a service is started
    // to automatically check command completion status without user polling
    bool_t m_is_polling;

    // interrupt observer.
    // only active when m_is_polling is FALSE
    std::thread m_interrupt_thread;

    // interrupt handler passed down from top layer
    // Current usage is to call this when devices are operated in
    // interrupt mode.
    kv_interrupt_handler m_interrupt_handler;

    // init status 
    kv_result m_init_status;
};



} // end of namespace
#endif // _KV_STORE_LINUX_KERNEL_H_
