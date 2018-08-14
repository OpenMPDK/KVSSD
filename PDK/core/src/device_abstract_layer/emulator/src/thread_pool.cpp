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

#include "thread_pool.hpp"
#include "kv_device.hpp"

namespace kvadi {
    
thread_pool::thread_pool() {}
// constructor to init
thread_pool::thread_pool(uint32_t devid, uint32_t thread_count_per_sq, uint32_t thread_count_per_cq) {
    m_devid = devid;
    m_thread_count_per_sq = thread_count_per_sq;
    m_thread_count_per_cq = thread_count_per_cq;
}
/*
void thread_pool::set_thread_count_per_sq(uint32_t count) {
    m_thread_count_per_sq = count;
}
void thread_pool::set_thread_count_per_cq(uint32_t count) {
    m_thread_count_per_cq = count;
}
*/
void thread_pool::set_devid(int32_t devid) {
    m_devid = devid;
}
kv_result thread_pool::create_submit_threads(void (*func)(void*), void* que, int num_threads)
{
    this->m_thread_count_per_sq = num_threads;
    for (unsigned int i = 0; i < m_thread_count_per_sq; i++) {
        m_threads.push_back(std::thread(func, que));
    }
    return KV_SUCCESS;
}

kv_result thread_pool::create_interrupt_threads(void (*func)(void*), void* que,int num_threads)
{
    this->m_thread_count_per_cq = num_threads;
    for (unsigned int i = 0; i < m_thread_count_per_cq; i++) {
        m_threads.push_back(std::thread(func,que));
    }
    return KV_SUCCESS;
}
#if 0
kv_result thread_pool::create_q_threads(uint16_t qid) {
    std::lock_guard<std::mutex> lock(m_mutex);

    kv_device_internal *dev = kv_device_internal::get_device_instance(m_devid);
    if (dev == NULL) {
        WRITE_WARN("Device not found while creating threads for the queue %d\n", qid);
        return KV_ERR_DEV_NOT_EXIST;
    }

    ioqueue *que = dev->get_emul_ioqueue(qid);
    if (que == NULL) {
        return KV_ERR_QUEUE_QID_INVALID;
    }

    // check device post IO processing mode, poll or interrupt
    bool_t is_polling = dev->is_polling();

    // if this is a SQ, only start it for emulator. physical kvssd, no cmds
    // are kept in SQ, cmds are sent to device directly for asyncIO.
    // create threads for SQ, and record thread ids for emulator only
    // this may change as device driver evolves
    if (dev->get_dev_type() == KV_DEV_TYPE_EMULATOR &&
            que->get_qtype() == SUBMISSION_Q_TYPE) {

        for (unsigned int i = 0; i < m_thread_count_per_sq; i++) {
            m_threads.push_back(std::thread(&emul_ioqueue::process_submission_queue, que));
            // WRITE_INFO("SQ threads started for queue %d\n", qid);
        }
    }

    // if this is a CQ
    // if in interrupt mode (not polling), then create threads for interrupt.
    // create threads for CQ, and record thread ids
    // only do this for emulator
    if (dev->get_dev_type() == KV_DEV_TYPE_EMULATOR && 
	!is_polling && que->get_qtype() == COMPLETION_Q_TYPE) {

        for (unsigned int i = 0; i < m_thread_count_per_cq; i++) {
            m_threads.push_back(std::thread(&emul_ioqueue::process_completion_queue, que));
            // WRITE_INFO("CQ threads started for queue %d\n", qid);
        }
    }

    return KV_SUCCESS;
}
#endif
// create an IO event to send to every queue, so those threads working on the
// queue will exit upon seeing the command
void thread_pool::join() {
    for (std::thread& it : m_threads) {
        if (it.joinable()) {
            it.join();
        }
    }
}

// join all threads
thread_pool::~thread_pool() {
    // wait for all threads to exit
    for (std::thread& it : m_threads) {
        if (it.joinable()) {
            it.join();
        }
    }
}

}
