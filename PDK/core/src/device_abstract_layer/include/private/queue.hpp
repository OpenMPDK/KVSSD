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

#ifndef _NEW_QUEUE_INCLUDE_H_
#define _NEW_QUEUE_INCLUDE_H_

#include <condition_variable>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <mutex>
#include "kvs_adi.h"
#include "kvs_adi_internal.h"

#include "io_cmd.hpp"
#include "thread_pool.hpp"
#include <boost/circular_buffer.hpp>
#include <list>

namespace kvadi {

class kv_device_internal;

class ioqueue {
protected:
    kv_queue queinfo;
    kv_queue_stat questat;
    kv_interrupt_handler interrupt_handler;
public:
    ioqueue(const kv_queue *queinfo_) {
        this->queinfo.queue_id            = queinfo_->queue_id;
        this->queinfo.queue_size          = queinfo_->queue_size;
        this->queinfo.completion_queue_id = queinfo_->completion_queue_id;
        this->queinfo.queue_type          = queinfo_->queue_type;
        this->queinfo.extended_info       = queinfo_->extended_info;
    }

    virtual ~ioqueue() {}
    int get_qid()  { return queinfo.queue_id;   }
    int get_type() { return queinfo.queue_type; }

    virtual kv_result init_interrupt_handler(kv_device_internal *dev,const kv_interrupt_handler int_hdl) = 0;
    kv_interrupt_handler get_interrupt_handler() { return this->interrupt_handler; }

    kv_result kv_get_queue_info(kv_queue *queinfo) {
        queinfo->queue_id            = this->queinfo.queue_id;
        queinfo->queue_size          = this->queinfo.queue_size;
        queinfo->completion_queue_id = this->queinfo.completion_queue_id;
        queinfo->queue_type          = this->queinfo.queue_type;
        queinfo->extended_info       = this->queinfo.extended_info;
        return KV_SUCCESS;
    }

    kv_result kv_get_queue_stat(kv_queue_stat *que_stat) {
        que_stat->queue_id = get_qid();
        que_stat->extended_info = 0;
        return KV_SUCCESS;
    }

    virtual size_t size() = 0;
    virtual kv_result poll_completion(uint32_t timeout_usec, uint32_t *num_completed) { return KV_SUCCESS; }
    virtual void terminate() {}

};

class emul_ioqueue: public ioqueue {

    std::mutex list_mutex;
    std::condition_variable cond_notempty;
    std::condition_variable cond_notfull;

    bool shutdown;
    emul_ioqueue *out;
    kv_device_api *kvstore;
    boost::circular_buffer<io_cmd*> queue;
    thread_pool threads;
public:

    emul_ioqueue(const kv_queue *queinfo_, kv_device_internal *dev, emul_ioqueue *out_ = 0);
    virtual ~emul_ioqueue() { terminate();    }

    kv_result enqueue (io_cmd *cmd, bool block = true);
    kv_result dequeue(io_cmd **cmd, bool block = true, uint32_t timeout_usec = 0);
    bool empty();
    size_t size() override;

    int get_cqid() {
        if (out == 0) return 0;
        return out->get_qid();
    }

    void terminate() {
        
        {
            std::unique_lock<std::mutex> lock(list_mutex);
            shutdown = true;
            cond_notempty.notify_all();
            cond_notfull.notify_all();
        }
        threads.join();

        while (!queue.empty()){
            delete this->queue.front();
            this->queue.pop_front();
        }
    }

    inline bool need_shutdown() { return shutdown; }
    emul_ioqueue *get_out_queue() { return out; }
    kv_result poll_completion(uint32_t timeout_usec, uint32_t *num_completed) override;
    kv_result init_interrupt_handler(kv_device_internal *dev,const kv_interrupt_handler int_hdl) override;
};

class kernel_ioqueue: public ioqueue
{
    int maxdepth;
    std::atomic<int> curdepth;
    kv_device_api *kvstore;
public:
    kernel_ioqueue(const kv_queue *queinfo_ ,kv_device_internal *dev);
    virtual ~kernel_ioqueue() {}

    void increase_qdepth() {
        curdepth++;
    }

    void decrease_qdepth(int n) {
        curdepth -= n;
    }

    bool full() {
        return (curdepth.load() >= maxdepth);
    }
    size_t size() override { return curdepth.load(); }

    kv_result init_interrupt_handler(kv_device_internal *dev, const kv_interrupt_handler int_hdl)
    {
        this->interrupt_handler = int_hdl;
        return KV_SUCCESS;
    }

    kv_result poll_completion(uint32_t timeout_usec, uint32_t *num_completed);

};

}
#endif
