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

#include "queue.hpp"
#include "kv_device.hpp"

using namespace kvadi;


static void process_submitted_commands(void *que);
static void process_interrupts(void *que);

emul_ioqueue::emul_ioqueue(const kv_queue *queinfo_,  kv_device_internal *dev, emul_ioqueue *out_):
    ioqueue(queinfo_), shutdown(false), out(out_), queue(queinfo_->queue_size)
{
    this->kvstore = dev->get_namespace(KV_NAMESPACE_DEFAULT)->get_kvstore();
    if ( this->queinfo.queue_type ==  SUBMISSION_Q_TYPE) {
        threads.set_devid(dev->get_devid());
        threads.create_submit_threads(process_submitted_commands, this, 1);
    }
}



kv_result emul_ioqueue::init_interrupt_handler(kv_device_internal *dev, const kv_interrupt_handler int_hdl)
{
    if (dev->is_polling()){
        return KV_ERR_OPTION_INVALID;
    }
    this->interrupt_handler = int_hdl;

    threads.set_devid(dev->get_devid());
    threads.create_interrupt_threads(process_interrupts, this, 1);
    return KV_SUCCESS;
}


kv_result emul_ioqueue::enqueue (io_cmd *cmd, bool block ) {
    std::unique_lock<std::mutex> lock(list_mutex);
    if (shutdown) return KV_ERR_QUEUE_IN_SHUTDOWN;
    while (queue.full() && !need_shutdown()) {
        if  (!block) return KV_ERR_QUEUE_IS_FULL;
        cond_notfull.wait_for(lock, std::chrono::microseconds(10));
    }
    queue.push_back(cmd);
    cond_notempty.notify_one();
    return KV_SUCCESS;
}

kv_result  emul_ioqueue::dequeue(io_cmd **cmd, bool block, uint32_t timeout_usec) {
    std::unique_lock<std::mutex> lock(list_mutex);
    while (queue.empty()  && !need_shutdown()) {
        if (!block) {
            *cmd = 0;
            return KV_SUCCESS;
        }

        const uint32_t timeout = (timeout_usec ==  0)? 10:timeout_usec;
        auto status = cond_notempty.wait_for(lock, std::chrono::microseconds(timeout));
        if (status == std::cv_status::timeout && timeout_usec != 0) {
            return KV_ERR_TIMEOUT;
        }
    }

    if (need_shutdown()) return KV_ERR_QUEUE_IN_SHUTDOWN;

    (*cmd) = queue.front(); queue.pop_front();
    cond_notfull.notify_one();
    return KV_SUCCESS;
}

bool emul_ioqueue::empty() {
    std::unique_lock<std::mutex> lock(list_mutex);
    return queue.empty();
}

size_t emul_ioqueue::size() {
    std::unique_lock<std::mutex> lock(list_mutex);
    return queue.size();
}

kv_result emul_ioqueue::poll_completion(uint32_t timeout_usec, uint32_t *num_events)
{
    
    if (this->get_type() != COMPLETION_Q_TYPE) return KV_ERR_QUEUE_CQID_INVALID;
    kv_result res;
    int num_completed = 0;
    const int count = std::max(std::min(*num_events, 128u), 1u);
    for (int i = 0 ; i < count; i++) {
        io_cmd *cmd;
        res = dequeue(&cmd, false, timeout_usec);
        if (res != KV_SUCCESS) {
            fprintf(stderr, "err = %d, timeout = %u\n", res, timeout_usec);
            break;
        }

        if (cmd) {
            cmd->call_post_process_func();

#ifdef ENABLE_LATENCY_TRACING
        cmd->evicted_o = std::chrono::system_clock::now();
        cmd->print_latency();
#endif

            delete cmd;
            num_completed++;
        }
    }
    *num_events = num_completed;
    //fprintf(stderr, "completed %d\n", *num_events);

    if (res != KV_SUCCESS) {  return res;  }

    return (this->size() == 0)?  KV_SUCCESS:KV_WRN_MORE;
}



static void process_submitted_commands(void *que_)
{
    
    emul_ioqueue *que = (emul_ioqueue *)que_;
    while (!que->need_shutdown()) {

        io_cmd *cmd;
        kv_result res = que->dequeue(&cmd, true);
        if (res != KV_SUCCESS || cmd == 0) continue;

#ifdef ENABLE_LATENCY_TRACING
        cmd->evicted_i = std::chrono::system_clock::now();
#endif

        cmd->execute_cmd();

        emul_ioqueue *out = que->get_out_queue();
        if (out)
            out->enqueue(cmd);
    }
}

// to handle interrupt
static void process_interrupts(void *que_) {

    emul_ioqueue *que = (emul_ioqueue *)que_;

    while (!que->need_shutdown()) {

        io_cmd *cmd;
        kv_result res = que->dequeue(&cmd, true);
        if (res != KV_SUCCESS || cmd == 0) continue;

        // XXX something to check how interrupt mode should work with a real device
        // physical devices have real interrupt.
        // only do this for emulator to simulate interrupt, for linux kernel kvssd, skip it
        // as callback has been done at linux kernel based implementation.

        auto ihandler = que->get_interrupt_handler();
        if (ihandler ) {
            ihandler->handler(ihandler->private_data, ihandler->number);
        }

        cmd->call_post_process_func();

        // finally we are done with a command
        delete cmd;
    }
}

kernel_ioqueue::kernel_ioqueue(const kv_queue *queinfo_ ,kv_device_internal *dev): ioqueue(queinfo_), maxdepth(queinfo_->queue_size), curdepth(0) {
    this->kvstore = dev->get_namespace(KV_NAMESPACE_DEFAULT)->get_kvstore();
}

kv_result kernel_ioqueue::poll_completion(uint32_t timeout_usec, uint32_t *num_completed) {
    return this->kvstore->poll_completion(timeout_usec, num_completed);
}


