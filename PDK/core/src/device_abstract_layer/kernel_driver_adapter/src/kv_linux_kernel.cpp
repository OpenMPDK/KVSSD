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
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "kv_linux_kernel.hpp"
#include "io_cmd.hpp"
#include <sys/ioctl.h>
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

    //////////////////////////////////////////////////////
    // for capacity from device
    struct nvme_lbaf {
            __le16                  ms;
            __u8                    ds;
            __u8                    rp;
    };
    
    struct nvme_admin_cmd {
            __u8    opcode;
            __u8    flags;
            __u16   rsvd1;
            __u32   nsid;
            __u32   cdw2;
            __u32   cdw3;
            __u64   metadata;
            __u64   addr;
            __u32   metadata_len;
            __u32   data_len;
            __u32   cdw10;
            __u32   cdw11;
            __u32   cdw12;
            __u32   cdw13;
            __u32   cdw14;
            __u32   cdw15;
            __u32   timeout_ms;
            __u32   result;
    };
    
    struct nvme_id_ns {
            __le64                  nsze;
            __le64                  ncap;
            __le64                  nuse;
            __u8                    nsfeat;
            __u8                    nlbaf;
            __u8                    flbas;
            __u8                    mc;
            __u8                    dpc;
            __u8                    dps;
            __u8                    nmic;
            __u8                    rescap;
            __u8                    fpi;
            __u8                    dlfeat;
            __le16                  nawun;
            __le16                  nawupf;
            __le16                  nacwu;
            __le16                  nabsn;
            __le16                  nabo;
            __le16                  nabspf;
            __le16                  noiob;
            __u8                    nvmcap[16];
            __u8                    rsvd64[28];
            __le32                  anagrpid;
            __u8                    rsvd96[3];
            __u8                    nsattr;
            __le16                  nvmsetid;
            __le16                  endgid;
            __u8                    nguid[16];
            __u8                    eui64[8];
            struct nvme_lbaf        lbaf[16];
            __u8                    rsvd192[192];
            __u8                    vs[3712];
    };
    

    struct nvme_id_power_state {
    	__le16			max_power;	/* centiwatts */
    	__u8			rsvd2;
    	__u8			flags;
    	__le32			entry_lat;	/* microseconds */
    	__le32			exit_lat;	/* microseconds */
    	__u8			read_tput;
    	__u8			read_lat;
    	__u8			write_tput;
    	__u8			write_lat;
    	__le16			idle_power;
    	__u8			idle_scale;
    	__u8			rsvd19;
    	__le16			active_power;
    	__u8			active_work_scale;
    	__u8			rsvd23[9];
    };

    struct nvme_id_ctrl {
            __le16                  vid;
            __le16                  ssvid;
            char                    sn[20];
            char                    mn[40];
            char                    fr[8];
            __u8                    rab;
            __u8                    ieee[3];
            __u8                    cmic;
            __u8                    mdts;
            __le16                  cntlid;
            __le32                  ver;
            __le32                  rtd3r;
            __le32                  rtd3e;
            __le32                  oaes;
            __le32                  ctratt;
            __u8                    rsvd100[156];
            __le16                  oacs;
            __u8                    acl;
            __u8                    aerl;
            __u8                    frmw;
            __u8                    lpa;
            __u8                    elpe;
            __u8                    npss;
            __u8                    avscc;
            __u8                    apsta;
            __le16                  wctemp;
            __le16                  cctemp;
            __le16                  mtfa;
            __le32                  hmpre;
            __le32                  hmmin;
            __u8                    tnvmcap[16];
            __u8                    unvmcap[16];
            __le32                  rpmbs;
            __u8                    rsvd316[4];
            __le16                  kas;
            __u8                    rsvd322[190];
            __u8                    sqes;
            __u8                    cqes;
            __le16                  maxcmd;
            __le32                  nn;
            __le16                  oncs;
            __le16                  fuses;
            __u8                    fna;
            __u8                    vwc;
            __le16                  awun;
            __le16                  awupf;
            __u8                    nvscc;
            __u8                    rsvd531;
            __le32                  sgls;
            __le16                  acwu;
            __u8                    rsvd534[2];
            __u8                    rsvd540[228];
            char                    subnqn[256];
            __u8                    rsvd1024[768];
            __le32                  ioccsz;
            __le32                  iorcsz;
            __le16                  icdoff;
            __u8                    ctrattr;
            __u8                    msdbd;
            __u8                    rsvd1804[244];
            struct nvme_id_power_state      psd[32];
            __u8                    vs[1024];
    };


    struct nvme_smart_log {
            __u8                    critical_warning;
            __u8                    temperature[2];
            __u8                    avail_spare;
            __u8                    spare_thresh;
            __u8                    percent_used;
            __u8                    rsvd6[26];
            __u8                    data_units_read[16];
            __u8                    data_units_written[16];
            __u8                    host_reads[16];
            __u8                    host_writes[16];
            __u8                    ctrl_busy_time[16];
            __u8                    power_cycles[16];
            __u8                    power_on_hours[16];
            __u8                    unsafe_shutdowns[16];
            __u8                    media_errors[16];
            __u8                    num_err_log_entries[16];
            __le32                  warning_temp_time;
            __le32                  critical_comp_time;
            __le16                  temp_sensor[8];
            __le32                  thm_temp1_trans_count;
            __le32                  thm_temp2_trans_count;
            __le32                  thm_temp1_total_time;
            __le32                  thm_temp2_total_time;
            __u8                    rsvd232[280];
    };
    
    #define NVME_IOCTL_ID           _IO('N', 0x40)
    #define NVME_IOCTL_ADMIN_CMD    _IOWR('N', 0x41, struct nvme_admin_cmd)
    #define NVME_IOCTL_SUBMIT_IO    _IOW('N', 0x42, struct nvme_user_io)
    //////////////////////////////////////////////////////


    static int nvme_submit_admin_passthru(int fd, struct nvme_passthru_cmd *cmd)
    {
    	return ioctl(fd, NVME_IOCTL_ADMIN_CMD, cmd);
    }

    #define NVME_IDENTIFY_DATA_SIZE 4096
    int nvme_identify13(int fd, __u32 nsid, __u32 cdw10, __u32 cdw11, void *data)
    {
    	struct nvme_admin_cmd cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.opcode = 0x06; //nvme_admin_identify;
        cmd.addr = (uint64_t) data;
        cmd.nsid = nsid;
        cmd.cdw10 = cdw10;
        cmd.cdw11 = cdw11;
        // cmd.data_len = NVME_IDENTIFY_DATA_SIZE;
        cmd.data_len = sizeof(cmd);

    	return nvme_submit_admin_passthru(fd, &cmd);
    }
    
    int nvme_identify(int fd, __u32 nsid, __u32 cdw10, void *data)
    {
    	return nvme_identify13(fd, nsid, cdw10, 0, data);
    }
    
    int nvme_identify_ctrl(int fd, void *data)
    {
    	return nvme_identify(fd, 0, 1, data);
    }


    bool is_kvssd(int fd) {
        struct nvme_id_ctrl ctrl;
        memset(&ctrl, 0, sizeof (struct nvme_id_ctrl));
        int err = nvme_identify_ctrl(fd, &ctrl);
        if (err) {
            // fprintf(stderr, "ERROR : nvme_identify_ctrl() failed 0x%x\n", err);
            return false;
        }

        // this may change based on firmware revision change
        // check firmware version
        // sample FW Revision ETA50K24, letter K at index 5 is the key
        if (strlen((char *) ctrl.fr) >= 6 && *((char *)ctrl.fr + 5) == 'K') {
            // fprintf(stderr, "found a kv device FW: %s\n", ctrl.fr);
            return true;
        }

        // fprintf(stderr, "not a kv device\n");
        return false;
    }

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

                // handle a failed command
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
                        else if(dev_status_code == 0x304){
                            req->ioctx.retcode = KV_ERR_OPTION_INVALID; // for invalid option
                        }
                        else if (dev_status_code == 0x394){
                            req->ioctx.retcode = KV_ERR_ITERATE_REQUEST_FAIL; // Failed Iterate Request
                        }
                    }
                    else if (req->ioctx.opcode == KV_OPC_CLOSE_ITERATOR){
                        // fprintf(stderr, "close iterator device returned status: 0x%x\n", dev_status_code);
                        if(dev_status_code == 0x390){ // Iterator does not exists
                            req->ioctx.retcode = KV_ERR_ITERATOR_NOT_EXIST;
                        }
                        else if(dev_status_code == 0x304){
                            req->ioctx.retcode = KV_ERR_OPTION_INVALID; // for invalid option
                        }
                        else if (dev_status_code == 0x394){
                            req->ioctx.retcode = KV_ERR_ITERATE_REQUEST_FAIL; // Failed Iterate Request
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
                            req->ioctx.retcode = KV_ERR_ITERATE_REQUEST_FAIL; // Failed Iterate Request
                        }
                        else if((dev_status_code == 0x393)){
                            // Scan Finished
                            req->ioctx.command.iterator_next_info.iter_list->end = TRUE;
                            req->ioctx.retcode = KV_SUCCESS;
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
                        else if(dev_status_code == 0x304){
                            req->ioctx.retcode = KV_ERR_OPTION_INVALID; // for invalid option
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
                        else if(dev_status_code == 0x304){
                            req->ioctx.retcode = KV_ERR_OPTION_INVALID; // for invalid option
                        }
                        else if (dev_status_code == 0x308){
                            req->ioctx.retcode = KV_ERR_VALUE_LENGTH_MISALIGNED;
                        }
                        else if(dev_status_code == 0x310){
                            req->ioctx.retcode = KV_ERR_KEY_NOT_EXIST;
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
                            req->ioctx.retcode = KV_ERR_KEY_NOT_EXIST;
                        }
                        else if(dev_status_code == 0x304){
                            req->ioctx.retcode = KV_ERR_OPTION_INVALID; // for invalid option
                        }
                        else {
                            req->ioctx.retcode = KV_ERR_SYS_IO;
                        }
                    }

                    else if(req->ioctx.opcode == KV_OPC_CHECK_KEY_EXIST){
                        if (dev_status_code == 0x301){
                            req->ioctx.retcode = KV_ERR_VALUE_LENGTH_INVALID;
                        }
                        else if (dev_status_code == 0x303){
                            req->ioctx.retcode = KV_ERR_KEY_LENGTH_INVALID;
                        }
                        else if (dev_status_code == 0x305){
                            req->ioctx.retcode = KV_ERR_NS_INVALID;
                        }
                        else if (dev_status_code == 0x310){
                            // need to indicate key doesn't exist
                            // assume the buffer has been cleared
                            req->ioctx.retcode = KV_SUCCESS;
                        }
                        else {
                            req->ioctx.retcode = KV_ERR_SYS_IO;
                        }
                    }

                    else{
                        // fprintf(stderr, "WARNING: unrecognized opcode 0x%X not handled\n", req->ioctx.opcode);
                        req->ioctx.retcode = KV_ERR_SYS_IO;
                    }
                }
                else{
                    req->ioctx.retcode = KV_SUCCESS;
                }

                // actual command execution status by device
                // update value size of get
                // handle successfully completed command
                if (req->ioctx.retcode == 0 && req->ioctx.opcode == KV_OPC_GET) {
                    if (req->ioctx.value) {
                        req->ioctx.value->actual_value_size = aioevents.events[i].result;

                        // device only return full value size, so if there is no error
                        // the returned data len must be the minimum of original buffer len
                        // and the full value size
                        req->ioctx.value->length = std::min(aioevents.events[i].result, req->ioctx.value->length);
                    }
                }
                else if (req->ioctx.retcode == 0 && req->ioctx.opcode == KV_OPC_OPEN_ITERATOR) {
                    req->ioctx.result.hiter = (aioevents.events[i].result & 0x000000FF);
                    // fprintf(stderr, "open iterator got id: %d\n", req->ioctx.result.hiter);

                    // open iterator actually failed
                    /*
                     * no interpretation
                    if (req->ioctx.result.hiter == 0) {
                        req->ioctx.retcode = KV_ERR_ITERATE_REQUEST_FAIL;
                    }
                    */
                }
                else if (req->ioctx.retcode == 0 && req->ioctx.opcode == KV_OPC_ITERATE_NEXT) {
#ifndef ITER_EXT
                        req->ioctx.result.hiter = ((aioevents.events[i].result >> 16) && 0xff);
                        // fprintf(stderr, "iterator next got id: %d\n", req->ioctx.result.hiter);
#endif

// for iteration behavior before and including EHA50K0K
// use a buffer to hold iteration output
#define KEY_LEN_BYTES 4
#ifdef ITERATOR_BEFORE_EHA50K0K
                        // transfer size from device in bytes
                        unsigned int xfr_size =  aioevents.events[i].result & 0x0000FFFF;
                        req->ioctx.command.iterator_next_info.iter_list->num_entries = (xfr_size/KVCMD_INLINE_KEY_MAX);
                        // printf("NO. of ENTRIES FROM KERNEL:%d\n",req->ioctx.command.iterator_next_info.iter_list->num_entries);


// for iteration behavior after EEA50K22_20180921
// use a buffer to hold iteration output
#else
                        char *data_buff = (char *) req->ioctx.command.iterator_next_info.iter_list->it_list;
                        unsigned int buffer_size =  aioevents.events[i].result & 0x0000FFFF;

			// according to firmware command output format, convert them to KVAPI expected format without any padding
                        // all data alreay in user provided buffer, but need to remove padding bytes to conform KVAPI format
                        char *current_ptr = data_buff;

                        unsigned int key_size = 0;
                        int keydata_len_with_padding = 0;
                        unsigned int buffdata_len = buffer_size;
                        // printf("buffer size %d\n", buffdata_len);
                        if (buffdata_len < KEY_LEN_BYTES) {
                            req->ioctx.retcode = KV_ERR_SYS_IO;
                        }

                        // first 4 bytes are for key counts
                        unsigned int key_count = *((uint32_t *)data_buff);
                        req->ioctx.command.iterator_next_info.iter_list->num_entries = key_count;

                        buffdata_len -= KEY_LEN_BYTES;
                        data_buff += KEY_LEN_BYTES;
                        for (uint32_t i = 0; i < key_count && buffdata_len > 0; i++) {
                            if (buffdata_len < KEY_LEN_BYTES) {
                                req->ioctx.retcode = KV_ERR_SYS_IO;
                                break;
                            }

                            // move 4 byte key len
                            memmove(current_ptr, data_buff, KEY_LEN_BYTES);
                            current_ptr += KEY_LEN_BYTES;

                            // get key size
                            key_size = *((uint32_t *)data_buff);
                            buffdata_len -= KEY_LEN_BYTES;
                            data_buff += KEY_LEN_BYTES;

                            if (key_size > buffdata_len) {
                                req->ioctx.retcode = KV_ERR_SYS_IO;
                                break;
                            }
                            if (key_size >= 256) {
                                req->ioctx.retcode = KV_ERR_SYS_IO;
                                break;
                            }

                            // move key data
                            memmove(current_ptr, data_buff, key_size);
                            current_ptr += key_size;

                            // calculate 4 byte aligned current key len including padding bytes
                            keydata_len_with_padding = (((key_size + 3) >> 2) << 2);

                            // skip to start position of next key
                            buffdata_len -= keydata_len_with_padding;
                            data_buff += keydata_len_with_padding;
                        }

#endif // end of ITERATOR_BEFORE_EHA50K0K
                }

                else if(req->ioctx.retcode == 0 && req->ioctx.opcode == KV_OPC_CHECK_KEY_EXIST){
                    uint32_t keycount = req->ioctx.command.key_exist_info.keycount;

                    uint8_t *buffers = req->ioctx.command.key_exist_info.result;
                    uint32_t buffer_size = req->ioctx.command.key_exist_info.result_size;
                    // original input keys, but we only support 1 currently
                    // const kv_key keys = req->ioctx.key
                    int bitpos = 0;
                    const uint32_t bytes_to_write = ((keycount -1) / 8) + 1;
                    
                    if (bytes_to_write > buffer_size) {
                        req->ioctx.retcode = KV_ERR_BUFFER_SMALL;
                    } else {
                    
                        memset(buffers, 0, bytes_to_write);
                    
                        // XXX check how device should handle a vector of keys in the future
                        // currently keycount = 1 only
                        for (uint32_t i = 0 ; i < keycount ; i++, bitpos++) {
                            const int setidx     = (bitpos / 8);
                            const int bitoffset  =  bitpos - setidx * 8;
                    
                            // key is found if the status is set to 0
                            // dev_status_code <== aioevents.events[i].status
                            bool key_found = !dev_status_code;
                 
                            if (key_found) {
                                buffers[setidx] |= (1 << bitoffset);
                            }
                        }
                        buffer_size = bytes_to_write;
                    }
                }

                // call postprocessing for interrupt mode
                if (!m_dev->is_polling()) {
                    // call interrupt handler function.
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
                // fprintf(stderr, "fail to read from eventfd ..\n");
                return KV_ERR_SYS_IO;
            }
        }

        return check_ioevents(eftd_ctx, num_events);
    }

    kv_result kv_linux_kernel::set_interrupt_handler(const kv_interrupt_handler int_hdl) {
        if (int_hdl == NULL) {
            return KV_ERR_PARAM_INVALID;
        }
        m_interrupt_handler = int_hdl;
        return KV_SUCCESS;
    }

    kv_interrupt_handler kv_linux_kernel::get_interrupt_handler() {
        return m_interrupt_handler;
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
                // std::unique_lock<std::mutex> lock(m_mutex);
                // m_cond_reqempty.wait(lock);
                // if there is still no request, then give up CPU
                if (m_req == 0) {
                    sched_yield();
                }
            }
            if (m_shutdown) {
                return;
            }

            nr_changed_fds = select(m_efd + 1, &rfds, NULL, NULL, &timeout);
            if (nr_changed_fds == 1 || nr_changed_fds == 0) {
                // get event count
                // fprintf(stderr, "m_req %llu m_processed %llu\r\n", m_req.load(), m_processed.load());
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
        m_init_status = KV_SUCCESS;

        // inherit this from top level device config
        m_is_polling = m_dev->is_polling();

        memset(buff, 0, 256);
        std::string path = m_dev->get_devpath();
        memcpy(buff, path.data(), path.size());
        m_fd = open(buff, O_RDWR);

        if (m_fd < 0) {
            m_ready = false;
            if (errno == ENOENT) {
                m_init_status = KV_ERR_DEV_NOT_EXIST;
            } else if (errno == EACCES) {
                m_init_status = KV_ERR_PERMISSION;
            }
            return;
        }

        if (!is_kvssd(m_fd)) {
            m_init_status = KV_ERR_SYS_IO;
            close(m_fd);
            m_fd = -1;
            m_ready = false;
            return;
        }

        m_nsid = ioctl(m_fd, NVME_IOCTL_ID);
        if (m_nsid == (unsigned) -1) {
            m_init_status = KV_ERR_DEV_INIT;
            close(m_fd);
            m_fd = -1;
            m_ready = false;
            return;
        }
        m_efd = eventfd(0,0);
        if (m_efd < 0) {
            m_init_status = KV_ERR_DEV_INIT;
            close(m_fd);
            m_fd = -1;
            m_ready = false;
            return;
        }
        aioctx.eventfd = m_efd;
        aioctx.ctxid = 0;
        if (ioctl(m_fd, NVME_IOCTL_SET_AIOCTX, &aioctx) < 0) {
            m_init_status = KV_ERR_DEV_INIT;
            close(m_efd);
            m_efd = -1;
            close(m_fd);
            m_fd = -1;
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
        m_init_status = KV_SUCCESS;
    }

    kv_linux_kernel::~kv_linux_kernel() {

        m_shutdown = true;
        // notify observer thread to shutdown
        // m_cond_reqempty.notify_one();

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
        // uint8_t dev_option = 0;
        if (!m_ready) return KV_ERR_DEV_INIT;
        /*
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
        */
        memset(&cmd, 0, sizeof(struct nvme_passthru_kv_cmd));
        cmd.opcode = nvme_cmd_kv_store;
        cmd.nsid = m_nsid;
        cmd.cdw4 = option;
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
        // m_cond_reqempty.notify_one();
        return KV_SUCCESS;
    }

    kv_result kv_linux_kernel::kv_retrieve(const kv_key *key, uint8_t option, kv_value *value, void *ioctx) {
        struct nvme_passthru_kv_cmd cmd;
        // uint8_t dev_option = 0;
        if (!m_ready) return KV_ERR_DEV_INIT;

        /*
        switch(option) {
            case KV_RETRIEVE_OPT_DECOMPRESS:
                dev_option = 1;
                break;
            default:
                dev_option = 0;
                break;
        } */
        memset(&cmd, 0, sizeof(struct nvme_passthru_kv_cmd));
        cmd.opcode = nvme_cmd_kv_retrieve;
        cmd.nsid = m_nsid;
        cmd.cdw4 = option;
        cmd.cdw5 = value->offset;
        cmd.data_addr =(__u64)value->value;
        //Actual buffer size, was value->actual_value_size is being  returned after the call.
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
        // m_cond_reqempty.notify_one();
        return KV_SUCCESS;
    }


    kv_result kv_linux_kernel::kv_exist(const kv_key *key, uint32_t keycount, uint8_t *buffers, uint32_t &buffer_size, void *ioctx) {
        if (!m_ready) return KV_ERR_DEV_INIT;

        // currently only support checking one key
        if (keycount != 1 || key == NULL || key->key == NULL) {
            return KV_ERR_PARAM_INVALID;
        }

        // only deal with first key
        uint32_t key_len = (key + 0)->length;

        struct nvme_passthru_kv_cmd cmd;
        memset(&cmd, 0, sizeof (struct nvme_passthru_kv_cmd));
        cmd.opcode = nvme_cmd_kv_exist;
        cmd.nsid = m_nsid;
        cmd.cdw3 = 0;

        cmd.key_length = key_len;
        cmd.cdw11 = key->length - 1;
        cmd.cdw10 = 0;
        if (key_len > KVCMD_INLINE_KEY_MAX) {
            cmd.key_addr = (__u64)key->key;
        } else {
            memcpy(cmd.key, key->key, key_len);
        }
        cmd.reqid = (__u64)ioctx;
        cmd.ctxid = m_ctxid;
    
        if (ioctl(m_fd, NVME_IOCTL_AIO_CMD, &cmd) < 0) {
            return KV_ERR_SYS_IO;
        }

        m_req++;
        // m_cond_reqempty.notify_one();
        return KV_SUCCESS;
    }
    

    kv_result kv_linux_kernel::kv_purge(kv_purge_option option, void *ioctx) {
        return KV_ERR_DD_UNSUPPORTED_CMD;
    }

    kv_result kv_linux_kernel::kv_delete(const kv_key *key, uint8_t option, uint32_t *recovered_bytes, void *ioctx) {
        struct nvme_passthru_kv_cmd cmd;
        if (!m_ready) return KV_ERR_DEV_INIT;

        memset(&cmd, 0, sizeof(struct nvme_passthru_kv_cmd));
        cmd.opcode = nvme_cmd_kv_delete;
        cmd.nsid = m_nsid;
        cmd.key_length = key->length;
        if (key->length > KVCMD_INLINE_KEY_MAX) {
            cmd.key_addr = (__u64)(key->key);
        } else {
            memcpy(cmd.key, key->key, key->length);
        }
        cmd.cdw4 = option;
        cmd.cdw11 = key->length - 1;
        cmd.reqid = (__u64)ioctx;
        cmd.ctxid = m_ctxid;
        if (ioctl(m_fd, NVME_IOCTL_AIO_CMD, &cmd) < 0) {
            return KV_ERR_SYS_IO;
        }
        m_delete++;
        m_req++;
        // m_cond_reqempty.notify_one();
        return KV_SUCCESS;
    }

    // iterator
    kv_result kv_linux_kernel::kv_open_iterator(const kv_iterator_option option, const kv_group_condition *cond, bool_t keylen_fixed, kv_iterator_handle *iter_hdl, void *ioctx) {

        if (cond == NULL || iter_hdl == NULL || cond == NULL) {
            return KV_ERR_PARAM_INVALID;
        }

        struct nvme_passthru_kv_cmd cmd;
        if (!m_ready) return KV_ERR_DEV_INIT;
        memset(&cmd, 0, sizeof(struct nvme_passthru_kv_cmd));
        uint8_t dev_option; // =  option; // KV_ITERATOR_OPT_KEY; // 0x00 < [DEFAULT] iterator command gets only key entri
        switch(option){
            case KV_ITERATOR_OPT_KEY:
                dev_option = 0x04; 
                break;
            case KV_ITERATOR_OPT_KV:
                dev_option = 0x08;
                //return KV_ERR_SYS_IO; // Not supported yet- returns Key-Values both.
                break;
            case KV_ITERATOR_OPT_KV_WITH_DELETE:
                dev_option = 0x10;
                //return KV_ERR_SYS_IO; // Not supported yet- returns Key-Values both.
                break;
            default:
                ///< [DEFAULT] iterator command gets only key entries without values
                dev_option = 0x80;
        }

        cmd.opcode = nvme_cmd_kv_iter_req;
        cmd.nsid = m_nsid;
#ifdef ITER_EXT
        // cmd.cdw4 = (0x01|0x04); // For open option ITER_OPTION_OPEN
        cmd.cdw4 = (dev_option |0x01); // For open option ITER_OPTION_OPEN
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
        // m_cond_reqempty.notify_one();
        return KV_SUCCESS;
    }

    kv_result kv_linux_kernel::kv_close_iterator(kv_iterator_handle iter_hdl, void *ioctx) {

        struct nvme_passthru_kv_cmd cmd;
        if (!m_ready) return KV_ERR_DEV_INIT;
        memset(&cmd, 0, sizeof(struct nvme_passthru_kv_cmd));
        cmd.opcode = nvme_cmd_kv_iter_req;
        cmd.nsid = m_nsid;
        cmd.cdw4 = 0x02; // For close option ITER_OPTION_CLOSE
        cmd.cdw5 = iter_hdl; // Iterator handle id
        cmd.reqid = (__u64)ioctx;
        cmd.ctxid = m_ctxid;

        int ret = 0;
        ret = ioctl(m_fd, NVME_IOCTL_AIO_CMD, &cmd);
        if(ret<0){
            return KV_ERR_SYS_IO;
        } // rest of all are Vendor specific error (SCT x3)
        m_iter_close++;
        m_req++;
        // m_cond_reqempty.notify_one();
        return KV_SUCCESS;
    }

    kv_result kv_linux_kernel::kv_iterator_next_set(kv_iterator_handle iter_hdl, kv_iterator_list *iter_list, void *ioctx) {
        struct nvme_passthru_kv_cmd cmd;
        if (!m_ready) return KV_ERR_DEV_INIT;
        memset(&cmd, 0, sizeof(struct nvme_passthru_kv_cmd));

        cmd.opcode = nvme_cmd_kv_iter_read;
        cmd.nsid = m_nsid;
        // key space id set to namespace id for now
        cmd.cdw3 = m_nsid;
        cmd.cdw4 = 0;
        cmd.cdw5 = iter_hdl; // Iterator handle id
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
        // m_cond_reqempty.notify_one();
        return KV_SUCCESS;
    }

    kv_result kv_linux_kernel::kv_iterator_next(kv_iterator_handle iter_hdl, kv_key *key, kv_value *value, void *ioctx) {
        // not supported by device yet
        return KV_ERR_DD_UNSUPPORTED_CMD;
    }

    // this uses ADMIN command, which is carried out synchronously
    kv_result kv_linux_kernel::kv_list_iterators(kv_iterator *iter_list, uint32_t *count, void *ioctx) {
        if (!m_ready) return KV_ERR_DEV_INIT;
        if (*count == 0) {
            return KV_ERR_BUFFER_SMALL;
        }

        struct nvme_passthru_cmd cmd;
        memset(&cmd, 0, sizeof(struct nvme_passthru_cmd));

        io_cmd *ioreq = (io_cmd *) ioctx;

        /////////////////////////////////
        uint32_t log_page_id = 0xd0;
        char logbuf[MAX_LOG_PAGE_SIZE];
        uint32_t buffer_size = MAX_LOG_PAGE_SIZE;
        memset(logbuf, 0, buffer_size);

        uint32_t offset = 0;

        cmd.opcode = 0x02; //nvme_admin_get_log_page;
        cmd.addr = (uint64_t) logbuf;
        cmd.data_len = buffer_size;
        cmd.nsid = m_nsid;

        cmd.cdw10 = (__u32)(((buffer_size >> 2) -1) << 16) | ((__u32)log_page_id & 0x000000ff);

        int ret = ioctl(m_fd, NVME_IOCTL_ADMIN_CMD, &cmd);
        if (ret != 0) {
            return KV_ERR_SYS_IO;
        }

        // after command is finished
        /* FIXED FORMAT */
        int handle_info_size = 16;

        // count of open iterators
        uint32_t open_count = 0;
        for(uint32_t i=0; i < SAMSUNG_MAX_ITERATORS; i++) {
            offset = (i*handle_info_size);
            uint8_t status = (*(uint8_t*)(logbuf + offset + 1));

/*
  no filering just return
            if (status == 0) {
                continue;
            }
*/

            if (open_count >= *count) {
                break;
            }

            iter_list[open_count].status = status;
            iter_list[open_count].handle_id = (*(uint8_t*)(logbuf + offset + 0));
            iter_list[open_count].type = (*(uint8_t*)(logbuf + offset + 2));
            iter_list[open_count].keyspace_id = (*(uint8_t*)(logbuf + offset + 3));

            iter_list[open_count].prefix = (*(uint32_t*)(logbuf + offset + 4));
            iter_list[open_count].bitmask = (*(uint32_t*)(logbuf + offset + 8));
            iter_list[open_count].is_eof = (*(uint8_t*)(logbuf + offset + 12));
            iter_list[open_count].reserved[0] = (*(uint8_t*)(logbuf + offset + 13));
            iter_list[open_count].reserved[1] = (*(uint8_t*)(logbuf + offset + 14));
            iter_list[open_count].reserved[2] = (*(uint8_t*)(logbuf + offset + 15));
            // fprintf(stderr, "handle_id=%d status=%d type=%d prefix=%08x bitmask=%08x is_eof=%d\n",
            //             iter_list[open_count].handle_id, iter_list[open_count].status, iter_list[open_count].type, iter_list[open_count].prefix, iter_list[open_count].bitmask, iter_list[open_count].is_eof);
            open_count++;

        }
        *count = open_count;

        if (!m_dev->is_polling()) {
            // call interrupt handler function.
            if (m_interrupt_handler) {
                m_interrupt_handler->handler(m_interrupt_handler->private_data, m_interrupt_handler->number);
            }
        }

        // call postprocessing after completion of a command
        ioreq->call_post_process_func();
        delete ioreq;

        return KV_SUCCESS;
    }

    kv_result kv_linux_kernel::kv_delete_group(kv_group_condition *grp_cond, uint64_t *recovered_bytes, void *ioctx) {
        // fprintf(stderr, "kv_delete_group not supported yet\n");
        return KV_ERR_DD_UNSUPPORTED_CMD;
    }

    kv_result kv_linux_kernel::get_init_status() {
        return m_init_status;
    }

    typedef struct ns_capacity_t {
        uint64_t capacity;
        uint64_t available;
        float utilization;
    } ns_capacity_t;


    static bool identify_ns_nvme(int fd, int nsid, ns_capacity_t *cap)
    {

        struct nvme_admin_cmd cmd;
        memset(&cmd, 0, sizeof(cmd));
        struct nvme_id_ns idnsbuf;
        memset(&idnsbuf, 0, sizeof(idnsbuf));
        cmd.opcode = 0x06; //nvme_admin_identify;
        cmd.addr = (uint64_t) &idnsbuf;
        cmd.nsid = nsid;
        cmd.cdw10 = 0;
        cmd.data_len = sizeof(idnsbuf);

        int ret = ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd);
        if (ret != 0) {
            return false;
        }
    
        int sector_size = (int) pow(2, idnsbuf.lbaf[0].ds);
        cap->capacity = idnsbuf.nsze * sector_size;
        cap->utilization = idnsbuf.nuse;
        cap->available = 1.0 * cap->capacity * (1.0 - 1.0 * idnsbuf.nuse/100.0/100.0);

        // printf("sector size %dB\n", sector_size);
        // printf("capacity %lluB\n", cap->capacity);
        // printf("available %lluB\n", cap->available);
        // printf("utilization %.2f\%\n", cap->utilization/100.0);
        // can I also get the type of device info " KV or Block"??

        return true;
    }
    
    kv_result kv_linux_kernel::get_total_capacity(uint64_t *capacity) {
        //Identify namespace
        ns_capacity_t cap;
        if (!identify_ns_nvme(m_fd, m_nsid, &cap)) {
            return KV_ERR_SYS_IO;
        }
        *capacity = cap.capacity;
        return KV_SUCCESS;
    }

    kv_result kv_linux_kernel::get_available(uint64_t *available) {
        //Identify namespace
        ns_capacity_t cap;
        if (!identify_ns_nvme(m_fd, m_nsid, &cap)) {
            return KV_ERR_SYS_IO;
        }
        *available = cap.available;

        return KV_SUCCESS;
    }

    uint64_t kv_linux_kernel::get_total_capacity() {
        uint64_t capacity = 0;
        int i = 0;
        kv_result ret = get_total_capacity(&capacity);
        while (ret != KV_SUCCESS && i < 5) {
            ret = get_total_capacity(&capacity);
            i++;
        }
        return capacity;
    }
    uint64_t kv_linux_kernel::get_available() {
        uint64_t available = 0;
        int i = 0;
        kv_result ret = get_available(&available);
        while (ret != KV_SUCCESS && i < 5) {
            ret = get_available(&available);
            i++;
        }
        return available;
    }

} // end of namespace
