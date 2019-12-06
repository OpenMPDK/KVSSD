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

#include <ctime>
#include <chrono>
#include <thread>

#include "kvs_utils.h"
#include "kv_config.hpp"

#include "io_cmd.hpp"
#include "kv_device.hpp"
#include "kv_namespace.hpp"

namespace kvadi {

// constructor
// assume key and value has been validated before this.
io_cmd::io_cmd(kv_device_internal *dev, kv_namespace_internal *ns, kv_queue_handle que_hdl) {

    m_thread_id = std::this_thread::get_id();

    m_dev = dev;
    m_ns = ns;
    m_cmd_id = 0;  // TODO:REMOVE THIS

    // submission Q
    ioqueue *que = (ioqueue *)que_hdl->queue;
    m_que = que;

    m_sqid = que->get_qid();
    //m_cqid = que->get_cqid();

    //m_start_time = std::chrono::duration_cast<std::chrono::nanoseconds>(m_cmd_timepoint.time_since_epoch()).count();
}

void io_cmd::set_retcode(kv_result current_result) {
    ioctx.retcode = current_result;
}

const kv_key* io_cmd::get_key() {
    return ioctx.key;
}
const kv_value* io_cmd::get_value() {
    return ioctx.value;
}

uint16_t io_cmd::get_sqid() {
    return m_sqid;
}
/*uint16_t io_cmd::get_cqid() {
    return m_cqid;
}*/

void io_cmd::set_sqid(uint16_t sqid) {
    m_sqid = sqid;
}

/*void io_cmd::set_cqid(uint16_t cqid) {
    m_cqid = cqid;
}*/

ioqueue *io_cmd::get_queue() {
    return m_que;
}

// callback function at host context for a command execution status
// fill in necessary info from command execution results
void io_cmd::call_post_process_func() {
    if (ioctx.post_fn != NULL) {
        ioctx.post_fn((kv_io_context *)&this->ioctx);
    }
}


// run diffrent async cmd including simulating latency based on opcode
// interact directly with underlying KV storage
// this pointer below is for linux kernel based physical kvstore
kv_result io_cmd::execute_cmd() {

    kv_namespace_internal *ns = m_ns;

    switch(ioctx.opcode) {
        case KV_OPC_GET: {
                // set result into value
                op_get_struct_t info = ioctx.command.get_info;
                ioctx.retcode = ns->kv_retrieve(ioctx.ks_id, ioctx.key, info.option, ioctx.value, (void *) this);
                break;
            }

        case KV_OPC_STORE: {
                uint32_t consumed_bytes = 0;
                op_store_struct_t info = ioctx.command.store_info;
                ioctx.retcode = ns->kv_store(ioctx.ks_id, ioctx.key, ioctx.value, info.option, &consumed_bytes, (void *) this);

                // kv_namespace_stat ns_st;
                // ns->kv_get_namespace_stat(&ns_st);
                // fprintf(stderr, "left capacity: %llu\n", ns_st.unallocated_capacity.low);
                // fprintf(stderr, "consumed: %llu\n", consumed_bytes);
                break;
            }

        case KV_OPC_DELETE: {
                uint32_t reclaimed_bytes = 0;
                op_delete_struct_t info = ioctx.command.delete_info;
                ioctx.retcode = ns->kv_delete(ioctx.ks_id, ioctx.key, info.option, &reclaimed_bytes, (void *) this);
                // fprintf(stderr, "reclaimed: %llu\n", reclaimed_bytes);
                break;
            }

        case KV_OPC_DELETE_GROUP: {
                uint64_t reclaimed_bytes = 0;
                op_delete_group_struct_t info = ioctx.command.delete_group_info;
                ioctx.retcode = ns->kv_delete_group(ioctx.ks_id, info.grp_cond, &reclaimed_bytes, (void *) this);
                break;
            }

        case KV_OPC_PURGE: {
                op_purge_struct_t info = ioctx.command.purge_info;
                ioctx.retcode = ns->kv_purge(ioctx.ks_id, info.option, (void *) this);
                break;
            }

        case KV_OPC_CHECK_KEY_EXIST: {
                op_key_exist_struct_t &info  = ioctx.command.key_exist_info;
                ioctx.retcode = ns->kv_exist(ioctx.ks_id, ioctx.key, info.keycount, info.result, info.result_size, (void *) this);
                ioctx.result.buffer_size = info.result_size;
                ioctx.result.buffer_count = info.keycount;
                break;
            }

        case KV_OPC_OPEN_ITERATOR: {
                op_iterator_open_struct_t info = ioctx.command.iterator_open_info;
                ioctx.retcode = ns->kv_open_iterator(ioctx.ks_id, info.it_op, &info.it_cond, &ioctx.result.hiter, (void *) this);
                break;
            }

        case KV_OPC_CLOSE_ITERATOR: {
                op_close_iterator_struct_t info = ioctx.command.iterator_close_info;
                ioctx.retcode = ns->kv_close_iterator(info.iter_hdl, (void *) this);
                ioctx.result.hiter = 0;
                break;
            }

        case KV_OPC_ITERATE_NEXT: {
                op_iterator_next_struct_t info = ioctx.command.iterator_next_info;
                ioctx.retcode = ns->kv_iterator_next_set(info.iter_hdl, info.iter_list, (void *) this);
                break;
            }

        case KV_OPC_ITERATE_NEXT_SINGLE_KV: {
                op_iterator_next_struct_t info = ioctx.command.iterator_next_info;
                ioctx.retcode = ns->kv_iterator_next(info.iter_hdl, info.key, info.value, (void *) this);
                break;
            }

        case KV_OPC_SANITIZE_DEVICE: {
                //op_sanitize_struct_t info = ioctx.command.sanitize_info;
                // XXX can't do much, just call purge function
                ioctx.retcode = ns->kv_purge(ioctx.ks_id, KV_PURGE_OPT_DEFAULT, (void *) this);
                break;
            }

        case KV_OPC_LIST_ITERATOR: {
                op_list_iterator_struct_t info = ioctx.command.iterator_list_info;
                ioctx.retcode = ns->kv_list_iterators(info.kv_iters, info.iter_cnt, (void *) this);
                break;
            }
        default:
            WRITE_WARN("OPCODE %d not recognized", ioctx.opcode);
            ioctx.retcode = KV_ERR_SYS_IO;
    }
/*
    uint32_t latency = m_dev->get_config()->generate_sample_latency(ioctx.opcode);
    // WRITE_INFO("sleeping for opcode %d: %d ns\n", ioctx.opcode, latency);
    if (latency > 0 ) {
        std::this_thread::sleep_for (std::chrono::nanoseconds(latency));
    }
*/

    return ioctx.retcode;
}



} // end of namespace
