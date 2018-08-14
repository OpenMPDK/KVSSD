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

#include "kv_linux_kernel.hpp"
#include "io_cmd.hpp"
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/eventfd.h>
#include <sys/select.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <math.h>
#include <time.h>
#include "linux_nvme_ioctl.h"
#include <assert.h>
#define ITER_EXT

//
// linux kernel based kvstore
//
namespace kvadi {

    // ideally the number should be from real device queue
    uint32_t kv_linux_kernel::get_cmds_pending_count() {
        return m_req;
    }

    // check for number of ioevents
    kv_result kv_linux_kernel::check_ioevents(unsigned long long eftd_ctx, uint32_t *num_events) {
        kv_result res = KV_SUCCESS;
        *num_events = 0;
        struct nvme_aioevents aioevents;
        unsigned int check_nr = 0;


        while(eftd_ctx) {
            check_nr = eftd_ctx;
            if (check_nr > MAX_AIO_EVENTS) {
                check_nr = MAX_AIO_EVENTS;
            }
            memset(&aioevents, 0, sizeof(aioevents));
            aioevents.nr = check_nr;
            aioevents.ctxid = m_ctxid;
            // get events
            if (ioctl(m_fd, NVME_IOCTL_GET_AIOEVENT, &aioevents) < 0) {
                // fprintf(stderr, "fail to get events..\n");
                res = KV_ERR_SYS_IO;
                break;
            }

            eftd_ctx -= aioevents.nr;
            // process events
            for (int i = 0; i < aioevents.nr; i++) {

                kernel_ioqueue *que = NULL;
                io_cmd *req = (io_cmd *)aioevents.events[i].reqid;

                // get submission queue handle
                que = (kernel_ioqueue *)req->get_queue();
                assert(que);

                // update queue depth asap
                // to allow enqueue to move forward
                que->decrease_qdepth(1);

                int dev_status_code = aioevents.events[i].status;
                /*if(req->ioctx.opcode == KV_OPC_OPEN_ITERATOR || req->ioctx.opcode == KV_OPC_CLOSE_ITERATOR){
                  printf("OPCODE (%d), %d, aioevents.events[i].result:0x%x\n",req->ioctx.opcode,i,aioevents.events[i].result);}*/

                if (dev_status_code) {
                    //aioevents.events[i].status;
                    if (dev_status_code<0){
                        req->ioctx.retcode = KV_ERR_SYS_IO;
                    }

                    if (req->ioctx.opcode == KV_OPC_OPEN_ITERATOR){
                        if (dev_status_code == 0x390){	// Iterator Does Not Exists
                            req->ioctx.retcode = KV_ERR_ITERATOR_NOT_EXIST;}
                        else if(dev_status_code == 0x391){ // All iterators are taken - too many iterators open
                            req->ioctx.retcode = KV_ERR_TOO_MANY_ITERATORS_OPEN;
                        }
                        else if(dev_status_code == 0x392){ // Iterator open
                            req->ioctx.retcode = KV_ERR_ITERATOR_IN_PROGRESS;
                        }
                        else if(dev_status_code == 0x304){
                            req->ioctx.retcode = KV_ERR_VENDOR; // for invalid option
                        }
                        else if (dev_status_code == 0x394){
                            req->ioctx.retcode = KV_ERR_VENDOR; // Failed Iterate Request
                        }
                    }
                    else if (req->ioctx.opcode == KV_OPC_CLOSE_ITERATOR){
                        if(dev_status_code == 0x390){ // Iterator does not exists
                            req->ioctx.retcode = KV_ERR_ITERATOR_NOT_EXIST;
                        }
                        else if(dev_status_code == 0x304){
                            req->ioctx.retcode = KV_ERR_VENDOR; // for invalid option
                        }
                        else if (dev_status_code == 0x394){
                            req->ioctx.retcode = KV_ERR_VENDOR; // Failed Iterate Request
                        }
                    }

                    else if (req->ioctx.opcode == KV_OPC_ITERATE_NEXT || req->ioctx.opcode == KV_OPC_ITERATE_NEXT_SINGLE_KV) {
                        if (dev_status_code== 0x301){
                            req->ioctx.retcode = KV_ERR_BUFFER_SMALL;  // 769 is small buffer size
                        }
                        else if(dev_status_code == 0x390){	// Iterator does not exists
                            req->ioctx.retcode = KV_ERR_ITERATOR_NOT_EXIST;
                        }
                        else if(dev_status_code == 0x308){	// Misaligned Value
                            req->ioctx.retcode = KV_ERR_VALUE_LENGTH_MISALIGNED;
                        }
                        else if (dev_status_code == 0x394){
                            req->ioctx.retcode = KV_ERR_VENDOR; // Failed Iterate Request
                        }
                        else if((dev_status_code == 0x393)){
                            // Scan Finished
                            req->ioctx.command.iterator_next_info.iter_list->end = TRUE;
                            req->ioctx.retcode = KV_ERR_ITERATOR_END;
                        }
                        else {
                            req->ioctx.retcode = KV_ERR_SYS_IO;
                        }
                    }

                    else if(req->ioctx.opcode == KV_OPC_GET){
                        if (dev_status_code == 0x301){
                            req->ioctx.retcode = KV_ERR_VALUE_LENGTH_INVALID;
                        }
                        else if(dev_status_code == 0x302){
                            req->ioctx.retcode = KV_ERR_VALUE_OFFSET_INVALID;
                        }
                        else if(dev_status_code == 0x303){
                            req->ioctx.retcode = KV_ERR_KEY_LENGTH_INVALID;
                        }
                        else if(dev_status_code == 0x308){
                            req->ioctx.retcode = KV_ERR_VALUE_LENGTH_MISALIGNED;
                        }
                        else if(dev_status_code == 0x310){
                            req->ioctx.retcode = KV_ERR_KEY_NOT_EXIST;
                        }
                        else if(dev_status_code == 0x311){
                            req->ioctx.retcode = KV_ERR_UNCORRECTIBLE;
                        }
                        else {
                            req->ioctx.retcode = KV_ERR_SYS_IO;
                        }
                    }

                    else if(req->ioctx.opcode == KV_OPC_STORE){
                        if (dev_status_code == 0x301){
                            req->ioctx.retcode = KV_ERR_VALUE_LENGTH_INVALID;
                        }
                        else if (dev_status_code == 0x303){
                            req->ioctx.retcode = KV_ERR_KEY_LENGTH_INVALID;
                        }
                        else if (dev_status_code == 0x308){
                            req->ioctx.retcode = KV_ERR_VALUE_LENGTH_MISALIGNED;
                        }
                        else if (dev_status_code == 0x311){
                            req->ioctx.retcode = KV_ERR_UNCORRECTIBLE;
                        }
                        else if (dev_status_code == 0x312){
                            req->ioctx.retcode = KV_ERR_DEV_CAPACITY;
                        }
                        else if (dev_status_code == 0x380){
                            req->ioctx.retcode = KV_ERR_KEY_EXIST;
                        }
                        else {
                            req->ioctx.retcode = KV_ERR_SYS_IO;
                        }
                    }
                    else if(req->ioctx.opcode == KV_OPC_DELETE){
                        if(dev_status_code == 0x310){
                            req->ioctx.retcode = KV_ERR_KEY_EXIST;
                        }
                        else {
                            req->ioctx.retcode = KV_ERR_SYS_IO;
                        }
                    }
                    else{
                        req->ioctx.retcode = KV_ERR_SYS_IO;
                    }
                }
                else{
                    req->ioctx.retcode = KV_SUCCESS;
                }

                // actual command execution status by device
                // update value size of get
                if (req->ioctx.retcode == 0 && req->ioctx.opcode == KV_OPC_GET) {
                    if (req->ioctx.value) {
                        ///// temporary
                        // req->ioctx.value->value_size = aioevents.events[i].result;
                    }
                }
                else if (req->ioctx.retcode == 0 && req->ioctx.opcode == KV_OPC_OPEN_ITERATOR) {
                    if (req->ioctx.result.hiter) {
                        req->ioctx.result.hiter->id = (aioevents.events[i].result & 0x000000FF);
                    }

                }
                else if (req->ioctx.opcode == KV_OPC_ITERATE_NEXT) {
                    if (req->ioctx.retcode == 0) {
#ifndef ITER_EXT
                        req->ioctx.result.hiter->id = ((aioevents.events[i].result >> 16) && 0xff);
#endif
                        // transfer size from device in bytes
                        unsigned int xfr_size =  aioevents.events[i].result & 0x0000FFFF;
                        req->ioctx.command.iterator_next_info.iter_list->num_entries = (xfr_size/KVCMD_INLINE_KEY_MAX);
                        printf("NO. of ENTRIES FROM KERNEL:%d\n",req->ioctx.command.iterator_next_info.iter_list->num_entries);
                    }
                }

                // call postprocessing for interrupt mode
                if (!m_dev->is_polling()) {
                    // call post process function.
                    if (m_interrupt_handler) {
                        m_interrupt_handler->handler(m_interrupt_handler->private_data, m_interrupt_handler->number);
                    }
                }

                // call postprocessing after completion of a command
                req->call_post_process_func();
                // done with cmd processing
                delete req;

                // update submission queue cmds count for queue depth control at submit_io()
                // moved to front to allow enqueue to move forward
                // que->decrease_qdepth(1);

                *num_events += 1;
                m_processed++;
                m_req--;
            }
        }

        return res;
    }

    kv_result kv_linux_kernel::poll_completion(uint32_t timeout_usec, uint32_t *num_events) {
        struct timeval timeout;
        fd_set rfds;
        int nr_changed_fds = 0;
        int read_s = 0;
        unsigned long long eftd_ctx = 0;

        FD_ZERO(&rfds);
        FD_SET(m_efd, &rfds);
        memset(&timeout, 0, sizeof(timeout));
        timeout.tv_usec = timeout_usec/1000;

        if (m_shutdown) {
            return KV_ERR_QUEUE_IN_SHUTDOWN;
        }

        nr_changed_fds = select(m_efd + 1, &rfds, NULL, NULL, &timeout);

        if (m_req == 0 && !m_shutdown) {
            *num_events = 0;
            return KV_SUCCESS;
        }

        if (nr_changed_fds == 1 || nr_changed_fds == 0) {
            // get event count
            read_s = read(m_efd, &eftd_ctx, sizeof(unsigned long long));
            if (read_s != sizeof(unsigned long long)) {
                fprintf(stderr, "fail to read from eventfd ..\n");
                return KV_ERR_SYS_IO;
            }
        }

        return check_ioevents(eftd_ctx, num_events);
    }

    kv_result kv_linux_kernel::set_interrupt_handler(const kv_interrupt_handler int_hdl) {
        if (int_hdl == NULL) {
            return KV_ERR_PARAM_NULL;
        }
        m_interrupt_handler = int_hdl;
        return KV_SUCCESS;
    }

    void kv_linux_kernel::kv_observer() {
        struct timeval timeout;
        fd_set rfds;
        int nr_changed_fds = 0;
        int read_s = 0;
        unsigned long long eftd_ctx = 0;
        uint32_t num_events = 0;

        FD_ZERO(&rfds);
        FD_SET(m_efd, &rfds);
        memset(&timeout, 0, sizeof(timeout));
        timeout.tv_usec = 1000; //tiemout after 1000 micro_sec

        while(!m_shutdown) {
            while(m_req == 0 && !m_shutdown) {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cond_reqempty.wait(lock);
            }
            if (m_shutdown) {
                return;
            }

            nr_changed_fds = select(m_efd + 1, &rfds, NULL, NULL, &timeout);
            if (nr_changed_fds == 1 || nr_changed_fds == 0) {
                // get event count
                read_s = read(m_efd, &eftd_ctx, sizeof(unsigned long long));
                if (read_s != sizeof(unsigned long long)) {
                    fprintf(stderr, "Internal error: fail to read from eventfd\n");
                    return;
                }
            }

            check_ioevents(eftd_ctx, &num_events);
        }
    }


    kv_linux_kernel::kv_linux_kernel(kv_device_internal *dev) {
        char buff[256];
        struct nvme_aioctx aioctx;
        m_shutdown = false;
        m_dev = dev;
        m_ready = false;
        m_processed = 0;
        m_write = 0;
        m_read = 0;
        m_delete = 0;
        m_iter_open = 0;
        m_iter_close = 0;
        m_iter_next = 0;
        m_req = 0;

        // inherit this from top level device config
        m_is_polling = m_dev->is_polling();

        memset(buff, 0, 256);
        std::string path = m_dev->get_devpath();
        memcpy(buff, path.data(), path.size());
        m_fd = open(buff, O_RDWR);
        if (m_fd < 0) {
            m_ready = false;
            return;
        }
        m_nsid = ioctl(m_fd, NVME_IOCTL_ID);
        if (m_nsid == (unsigned) -1) {
            close(m_fd);
            m_ready = false;
            return;
        }
        m_efd = eventfd(0,0);
        if (m_nsid < 0) {
            close(m_fd);
            m_ready = false;
            return;
        }
        aioctx.eventfd = m_efd;
        aioctx.ctxid = 0;
        if (ioctl(m_fd, NVME_IOCTL_SET_AIOCTX, &aioctx) < 0) {
            close(m_efd);
            close(m_fd);
            m_ready = false;
            return;
        }
        m_ctxid = aioctx.ctxid;
        m_ready = true;

        // only start service for interrupt mode
        if (!m_is_polling) {
            m_interrupt_thread = std::thread(&kv_linux_kernel::kv_observer, this);
        }
        m_init = true;
    }

    kv_linux_kernel::~kv_linux_kernel() {

        m_shutdown = true;
        // notify observer thread to shutdown
        m_cond_reqempty.notify_one();

        if (m_init) {
            if (!m_is_polling && m_interrupt_thread.joinable()) {
                m_interrupt_thread.join();
            }

            if (m_efd) {
                struct nvme_aioctx aioctx;
                aioctx.eventfd = m_efd;
                aioctx.ctxid = m_ctxid;
                ioctl(m_fd, NVME_IOCTL_DEL_AIOCTX, &aioctx);
                close(m_efd);
            }
            if (m_fd) {
                close(m_fd);
            }
        }
        m_init = false;
        m_ready = false;
    }

    // basic operations to submit a command to device
    kv_result kv_linux_kernel::kv_store(const kv_key *key, const kv_value *value, uint8_t option, uint32_t *consumed_bytes, void *ioctx) {
        struct nvme_passthru_kv_cmd cmd;
        uint8_t dev_option = 0;
        if (!m_ready) return KV_ERR_SYS_IO;
        switch(option) {
            case KV_STORE_OPT_COMPRESS:
                dev_option = 1;
                break;
            case KV_STORE_OPT_IDEMPOTENT:
                dev_option = 2;
                break;
            default:
                dev_option = 0;
                break;
        }
        memset(&cmd, 0, sizeof(struct nvme_passthru_kv_cmd));
        cmd.opcode = nvme_cmd_kv_store;
        cmd.nsid = m_nsid;
        cmd.cdw4 = dev_option;
        cmd.cdw5 = value->offset;
        cmd.data_addr = (__u64)value->value;
        cmd.data_length = value->length;
        cmd.key_length = key->length;
        if (key->length > KVCMD_INLINE_KEY_MAX) {
            cmd.key_addr = (__u64)(key->key);
        } else {
            memcpy(cmd.key, key->key, key->length);
        }
        cmd.cdw11 = key->length - 1;
        cmd.cdw10 = (value->length >> 2);
        cmd.reqid = (__u64)ioctx;
        cmd.ctxid = m_ctxid;
        if (ioctl(m_fd, NVME_IOCTL_AIO_CMD, &cmd) < 0) {
            return KV_ERR_SYS_IO;
        }
        m_write++;
        m_req++;
        m_cond_reqempty.notify_one();
        return KV_SUCCESS;
    }

    kv_result kv_linux_kernel::kv_retrieve(const kv_key *key, uint8_t option, kv_value *value, void *ioctx) {
        struct nvme_passthru_kv_cmd cmd;
        uint8_t dev_option = 0;
        if (!m_ready) return KV_ERR_SYS_IO;

        switch(option) {
            case KV_RETRIEVE_OPT_DECOMPRESS:
                dev_option = 1;
                break;
            default:
                dev_option = 0;
                break;
        }
        memset(&cmd, 0, sizeof(struct nvme_passthru_kv_cmd));
        cmd.opcode = nvme_cmd_kv_retrieve;
        cmd.nsid = m_nsid;
        cmd.cdw4 = dev_option;
        cmd.cdw5 = value->offset;
        cmd.data_addr =(__u64)value->value;
        //Actual buffer size, was value->value_size is being  returned after the call.
        cmd.data_length = value->length;
        cmd.key_length = key->length;
        if (key->length > KVCMD_INLINE_KEY_MAX) {
            cmd.key_addr = (__u64)(key->key);
        } else {
            memcpy(cmd.key, key->key, key->length);
        }
        cmd.cdw11 = key->length - 1;
        cmd.cdw10 = (value->length >> 2);
        cmd.reqid = (__u64)ioctx;
        cmd.ctxid = m_ctxid;
        if (ioctl(m_fd, NVME_IOCTL_AIO_CMD, &cmd) < 0) {
            return KV_ERR_SYS_IO;
        }
        m_read++;
        m_req++;
        m_cond_reqempty.notify_one();
        return KV_SUCCESS;
    }


    kv_result kv_linux_kernel::kv_exist(const kv_key *key, uint32_t &keycount, uint8_t *buffers, uint32_t &buffer_size, void *ioctx) {
        return KV_ERR_SYS_IO;
    }

    kv_result kv_linux_kernel::kv_purge(kv_purge_option option, void *ioctx) {
        return KV_ERR_SYS_IO;
    }

    kv_result kv_linux_kernel::kv_delete(const kv_key *key, uint8_t option, uint32_t *recovered_bytes, void *ioctx) {
        struct nvme_passthru_kv_cmd cmd;
        if (!m_ready) return KV_ERR_SYS_IO;

        memset(&cmd, 0, sizeof(struct nvme_passthru_kv_cmd));
        cmd.opcode = nvme_cmd_kv_delete;
        cmd.nsid = m_nsid;
        cmd.key_length = key->length;
        if (key->length > KVCMD_INLINE_KEY_MAX) {
            cmd.key_addr = (__u64)(key->key);
        } else {
            memcpy(cmd.key, key->key, key->length);
        }
        cmd.cdw11 = key->length - 1;
        cmd.reqid = (__u64)ioctx;
        cmd.ctxid = m_ctxid;
        if (ioctl(m_fd, NVME_IOCTL_AIO_CMD, &cmd) < 0) {
            return KV_ERR_SYS_IO;
        }
        m_delete++;
        m_req++;
        m_cond_reqempty.notify_one();
        return KV_SUCCESS;
    }

    // iterator
    kv_result kv_linux_kernel::kv_open_iterator(const kv_iterator_option opt, const kv_group_condition *cond, bool_t keylen_fixed, kv_iterator_handle *iter_hdl, void *ioctx) {

        if (cond == NULL || iter_hdl == NULL || cond == NULL) {
            return KV_ERR_PARAM_NULL;
        }

        (*iter_hdl) = new _kv_iterator_handle();

        struct nvme_passthru_kv_cmd cmd;
        if (!m_ready) return KV_ERR_SYS_IO;
        memset(&cmd, 0, sizeof(struct nvme_passthru_kv_cmd));
        uint8_t dev_option =  KV_ITERATOR_OPT_KEY; // 0x00 < [DEFAULT] iterator command gets only key entri
        switch(opt){
            case KV_ITERATOR_OPT_KV:
                return KV_ERR_SYS_IO; // Not supported yet- returns Key-Values both.
                break;
            default:
                ///< [DEFAULT] iterator command gets only key entries without values
                dev_option = KV_ITERATOR_OPT_KEY;
        }

        cmd.opcode = nvme_cmd_kv_iter_req;
        cmd.nsid = m_nsid;
#ifdef ITER_EXT
        cmd.cdw4 = (0x01|0x04); // For open option ITER_OPTION_OPEN
#else
        cmd.cdw4 = 0x01; // For open option ITER_OPTION_OPEN
#endif
        cmd.cdw12 = cond->bit_pattern;
        cmd.cdw13 = cond->bitmask;
        cmd.reqid = (__u64)ioctx;
        cmd.ctxid = m_ctxid;
        int ret = 0;
        ret = ioctl(m_fd, NVME_IOCTL_AIO_CMD, &cmd);
        if(ret<0){
            return KV_ERR_SYS_IO;
        } // rest of all are Vendor specific error (SCT x3)

        m_iter_open++;
        m_req++;
        m_cond_reqempty.notify_one();
        return KV_SUCCESS;
    }

    kv_result kv_linux_kernel::kv_close_iterator(kv_iterator_handle iter_hdl, void *ioctx) {

        struct nvme_passthru_kv_cmd cmd;
        if (!m_ready) return KV_ERR_SYS_IO;
        memset(&cmd, 0, sizeof(struct nvme_passthru_kv_cmd));
        cmd.opcode = nvme_cmd_kv_iter_req;
        cmd.nsid = m_nsid;
        cmd.cdw4 = 0x02; // For close option ITER_OPTION_CLOSE
        cmd.cdw5 = iter_hdl->id; // Iterator handle
        cmd.reqid = (__u64)ioctx;
        cmd.ctxid = m_ctxid;

        if (iter_hdl) {
            delete iter_hdl;
        }

        int ret = 0;
        ret = ioctl(m_fd, NVME_IOCTL_AIO_CMD, &cmd);
        if(ret<0){
            return KV_ERR_SYS_IO;
        } // rest of all are Vendor specific error (SCT x3)
        m_iter_close++;
        m_req++;
        m_cond_reqempty.notify_one();
        return KV_SUCCESS;
    }

    // TODO XXX
    kv_result kv_linux_kernel::kv_iterator_next_set(kv_iterator_handle iter_hdl, kv_iterator_list *iter_list, void *ioctx) {
        struct nvme_passthru_kv_cmd cmd;
        if (!m_ready) return KV_ERR_SYS_IO;
        memset(&cmd, 0, sizeof(struct nvme_passthru_kv_cmd));

        cmd.opcode = nvme_cmd_kv_iter_read;
        cmd.nsid = m_nsid;
        cmd.cdw4 = 0; // fixed key length
        cmd.cdw5 = iter_hdl->id; // Iterator handle
        cmd.data_addr = (__u64)iter_list->it_list;
        cmd.data_length = iter_list->size;
        cmd.cdw10 = ((iter_list->size) >> 2);
        cmd.reqid = (__u64)ioctx;
        cmd.ctxid = m_ctxid;
        int ret = 0;
        ret = ioctl(m_fd, NVME_IOCTL_AIO_CMD, &cmd);
        if(ret<0){
            return KV_ERR_SYS_IO;
        } // rest of all are Vendor specific error (SCT x03)
        m_iter_next++;
        m_req++;
        m_cond_reqempty.notify_one();
        return KV_SUCCESS;
    }

    // TODO XXX
    kv_result kv_linux_kernel::kv_iterator_next(kv_iterator_handle iter_hdl, kv_key *key, kv_value *value, void *ioctx) {
        struct nvme_passthru_kv_cmd cmd;
        if (!m_ready) return KV_ERR_SYS_IO;
        memset(&cmd, 0, sizeof(struct nvme_passthru_kv_cmd));

        cmd.opcode = nvme_cmd_kv_iter_read;
        cmd.nsid = m_nsid;
        cmd.cdw4 = 0; // fixed key length
        cmd.cdw5 = iter_hdl->id; // Iterator handle
        cmd.data_addr = (__u64)iter_hdl->buffer;
        cmd.data_length = ITERATOR_BUFFER_LEN;
        cmd.cdw10 = ((ITERATOR_BUFFER_LEN) >> 2);
        cmd.reqid = (__u64)ioctx;
        cmd.ctxid = m_ctxid;
        int ret = 0;
        ret = ioctl(m_fd, NVME_IOCTL_AIO_CMD, &cmd);
        if(ret<0){
            return KV_ERR_SYS_IO;
        } // rest of all are Vendor specific error (SCT x03)
        m_iter_next++;
        m_req++;
        m_cond_reqempty.notify_one();
        return KV_SUCCESS;
    }

    kv_result kv_linux_kernel::kv_list_iterators(kv_iterator *iter_list, uint32_t *count, void *ioctx) {
        return KV_ERR_SYS_IO;
    }

    kv_result kv_linux_kernel::kv_delete_group(kv_group_condition *grp_cond, uint64_t *recovered_bytes, void *ioctx) {
        return KV_ERR_SYS_IO;
    }

    // needs update how to get physical space information from real device
    uint64_t kv_linux_kernel::get_total_capacity() { return 1024*1024*1024;  }
    uint64_t kv_linux_kernel::get_available() { return 1024*1024*1024; }


} // end of namespace
