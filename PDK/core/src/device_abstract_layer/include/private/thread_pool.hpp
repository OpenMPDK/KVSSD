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

#ifndef _THREAD_POOL_INCLUDE_H_
#define _THREAD_POOL_INCLUDE_H_

#include <thread>
#include <mutex>
#include <vector>
#include <condition_variable>

#include "io_cmd.hpp"

namespace kvadi {


class thread_pool {
public:
    thread_pool();
    // create main monitor thread and submission q threads and completion
    // q threads
    thread_pool(uint32_t devid, uint32_t thread_count_per_sq, uint32_t thread_count_per_cq);
    //void set_thread_count_per_sq(uint32_t count);
    //void set_thread_count_per_cq(uint32_t count);
    void set_devid(int32_t devid);

    // start threads to working on the queue
    //kv_result create_q_threads(uint16_t qid);

    kv_result create_submit_threads(void (*func)(void*), void *que, int num_threads);
    kv_result create_interrupt_threads(void (*func)(void*), void *que, int num_threads);

    // shut down all threads working on a queue
    // this will send a command to queue for threads to process and exit
    // need to wait and join those threads
    // void shutdown_threads(uint16_t qid);

    // stop all threads by shutdown every threads waiting on queues
    // which should call shutdown_threads(qid) for every queue
    void join();

    ~thread_pool();

private:
    uint32_t m_devid;

    // keep all threads for a given queue
    std::vector<std::thread> m_threads;

    // number of threads to start per queue
    uint32_t m_thread_count_per_sq;
    uint32_t m_thread_count_per_cq;

    std::mutex m_mutex;
    // std::condition_variable m_monitor_cond;
    // bool_t stop;
};


}
#endif
