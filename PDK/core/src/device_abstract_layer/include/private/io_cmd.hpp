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

#ifndef _IO_CMD_INCLUDE_H_
#define _IO_CMD_INCLUDE_H_

#include <thread>
#include "kvs_adi.h"
#include "kvs_adi_internal.h"


//#define ENABLE_LATENCY_TRACING

namespace kvadi {

class kv_device_internal;
class kv_namespace_internal;
class ioqueue;

class io_cmd {
public:
    // make it public for easier access
    io_ctx_t ioctx;
    
    // should assign a unique cmd id, which is monotically increasing
    // in a thread context
    // for KV_OPC_STORE, no overwrite for KV_STORE_OPT_IDEMPOTENT
    // return key already exists error
    io_cmd(kv_device_internal *, kv_namespace_internal *ns, kv_queue_handle que_hdl);

    // set current return code
    void set_retcode(kv_result current_result);

    const kv_key* get_key();
    const kv_value* get_value();

    // run cmd including simulating latency based on opcode
    kv_result execute_cmd();

    uint16_t get_sqid();
    //uint16_t get_cqid();

    void set_sqid(uint16_t sqid);
    //void set_cqid(uint16_t cqid);

    ioqueue *get_queue();

    // to be called by a worker thread to execute the task associated with the
    // command.
    void call_post_process_func();

    // return nanoseconds since epoch for the cmd
    //uint64_t get_cmd_time_nanoseconds();

#ifdef ENABLE_LATENCY_TRACING
    std::chrono::time_point<std::chrono::system_clock> inserted_i;
    std::chrono::time_point<std::chrono::system_clock> evicted_i;
    std::chrono::time_point<std::chrono::system_clock> inserted_o;
    std::chrono::time_point<std::chrono::system_clock> evicted_o;

    void print_latency() {
        fprintf(stderr, "submission queue insert - submission queue evict: %ld ns \n", std::chrono::duration_cast<std::chrono::nanoseconds>(evicted_i - inserted_i).count() );
        fprintf(stderr, "processing time : %ld ns \n", std::chrono::duration_cast<std::chrono::nanoseconds>(inserted_o - evicted_i ).count() );
        fprintf(stderr, "completion queue insert - evict : %ld ns \n", std::chrono::duration_cast<std::chrono::nanoseconds>(evicted_o - evicted_i ).count() );
    }

#endif
private:
    // command context info, hold all info for command execution and return
    // opcode, key, value, option, timeout etc.
    // right now, everything is base on value copy on runtime stack for
    // io context, so be careful on this fact
    // io_ctx_t m_ioctx;

    // user context caller thread id that submitted this io_cmd
    std::thread::id m_thread_id;

    // command generation time info
    //std::chrono::system_clock::time_point m_cmd_timepoint;
    // in nanoseconds when the command was first submitted 
   // uint64_t m_start_time;

    // assigned unique id, if 0, then not assigned yet.
    // uint16_t m_cmd_id;

    // device id
    kv_device_internal *m_dev;

    // namespace identifier
    uint32_t m_nsid;
    kv_namespace_internal *m_ns;

    // command id, assigned by caller
    uint16_t m_cmd_id;

    // command submission qid
    uint16_t m_sqid;

    // command completion qid
    uint16_t m_cqid;

    // submission Q
    ioqueue *m_que;

    // results from IO operation
    //kv_result m_res;
};


} // end of namespace
#endif // end of include
