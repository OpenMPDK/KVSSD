//
// Created by root on 11/8/18.
//

#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/eventfd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <math.h>
#include <time.h>
#include <vector>
#include "../kvsstore_types.h"
#include "KADI.h"
#include "linux_nvme_ioctl.h"
#include "../kvs_debug.h"

#undef dout_prefix
#define dout_prefix *_dout << "[kadi] "
//#define DUMP_ISSUE_CMD 1
void write_callback(kv_io_context &op, void* private_data) {
    KvsTransContext *txc= (KvsTransContext *)private_data;
    if (!txc) { ceph_abort();  };

#ifdef DUMP_ISSUE_CMD
    if (op.key) {
        lderr(txc->cct) << "write callback " << op.key << dendl;
        lderr(txc->cct) << "write callback: key = " << print_key((const char *)op.key->key, op.key->length) << ", len = " <<  (int)op.key->length << ", retcode " << op.retcode << dendl;
    }
#endif
    txc->aio_finish(&op);
}

void read_callback(kv_io_context &op, void* private_data) {
    KvsReadContext* txc = (KvsReadContext*)private_data;

#ifdef DUMP_ISSUE_CMD
    if (op.key) {
        lderr(txc->cct) << "read callback: key = " << op.key << dendl;
        lderr(txc->cct) << "read callback: key = " << print_key((const char *) op.key->key, op.key->length)
                        << ", len = " <<  (int)op.key->length << "retcode " << op.retcode << dendl;
    }
#endif

    txc->retcode = op.retcode;
    txc->try_read_wake();
}

kv_result KADI::iter_readall(kv_iter_context *iter_ctx, std::list<std::pair<void*, int> > &buflist)
{
    kv_result r = iter_open(iter_ctx);
    if (r != 0) return r;
    while (!iter_ctx->end) {
        iter_ctx->byteswritten = 0;
        iter_ctx->buf = malloc(iter_ctx->buflen);
        iter_read(iter_ctx);

        if (iter_ctx->byteswritten > 0) {
            buflist.push_back(std::make_pair(iter_ctx->buf, iter_ctx->byteswritten));
        }

    }
    r = iter_close(iter_ctx);
    return r;
}



int KADI::open(std::string &devpath) {

    FTRACE
    int ret = 0;
    fd = ::open(devpath.c_str(), O_RDWR);
    if (fd < 0) {
        derr <<  "can't open a device : " << devpath << dendl;
        return fd;
    }

    nsid = ioctl(fd, NVME_IOCTL_ID);
    if (nsid == (unsigned) -1) {
        derr <<  "can't get an ID" << dendl;
        return -1;
    }

    space_id = 0;

    for (int i =0  ; i < qdepth; i++) {
        aio_cmd_ctx *ctx = (aio_cmd_ctx *)calloc(1, sizeof(aio_cmd_ctx));
        ctx->index = i;
        free_cmdctxs.push_back(ctx);
    }

    int efd = eventfd(0,0);
    if (efd < 0) {
        fprintf(stderr, "fail to create an event.\n");
        return -1;
    }
    aioctx.ctxid   = 0;
    aioctx.eventfd = efd;

    if (ioctl(fd, NVME_IOCTL_SET_AIOCTX, &aioctx) < 0) {
        derr <<  "fail to set_aioctx" << dendl;
        return -1;
    }

    derr << "KADI is opened successfully, fd " << fd << ", efd " << efd << ", dev " << devpath.c_str() << dendl;

    return ret;
}

int KADI::close() {
    if (fd > 0) {

        ioctl(fd, NVME_IOCTL_DEL_AIOCTX, &aioctx);
        ::close((int)aioctx.eventfd);
        ::close(fd);
        fd = -1;
    }
    return 0;
}

KADI::aio_cmd_ctx* KADI::get_cmd_ctx(kv_cb& cb) {
    std::unique_lock<std::mutex> lock (cmdctx_lock);
    while (free_cmdctxs.empty())
        cmdctx_cond.wait(lock);

    aio_cmd_ctx *p = free_cmdctxs.back();
    free_cmdctxs.pop_back();

    p->post_fn   = cb.post_fn;
    p->post_data = cb.private_data;
    pending_cmdctxs.insert(std::make_pair(p->index, p));

    return p;
}

void KADI::release_cmd_ctx(aio_cmd_ctx *p) {
    std::lock_guard<std::mutex> lock (cmdctx_lock);

    pending_cmdctxs.erase(p->index);
    free_cmdctxs.push_back(p);
    cmdctx_cond.notify_one();
}



kv_result KADI::iter_open(kv_iter_context *iter_handle)
{
    struct nvme_passthru_kv_cmd cmd;
    memset(&cmd, 0, sizeof(struct nvme_passthru_kv_cmd));

    cmd.opcode = nvme_cmd_kv_iter_req;
    cmd.cdw3 = space_id;
    cmd.nsid = nsid;
    cmd.cdw4 = (ITER_OPTION_OPEN | ITER_OPTION_KEY_ONLY);
    cmd.cdw12 = iter_handle->prefix;
    cmd.cdw13 = iter_handle->bitmask;
#ifdef DUMP_ISSUE_CMD
    dump_cmd(&cmd);
#endif
    int ret = ioctl(fd, NVME_IOCTL_IO_KV_CMD, &cmd);
    if (ret < 0) {
        return -1;
    }

    iter_handle->handle = cmd.result & 0xff;
    iter_handle->end    = false;


    return cmd.status;
}

kv_result KADI::iter_close(kv_iter_context *iter_handle) {
    struct nvme_passthru_kv_cmd cmd;
    memset(&cmd, 0, sizeof(struct nvme_passthru_kv_cmd));
    cmd.opcode = nvme_cmd_kv_iter_req;
    cmd.cdw3 = space_id;
    cmd.nsid = nsid;
    cmd.cdw4 = ITER_OPTION_CLOSE;
    cmd.cdw5 = iter_handle->handle;
#ifdef DUMP_ISSUE_CMD
    dump_cmd(&cmd);
#endif
    if (ioctl(fd, NVME_IOCTL_IO_KV_CMD, &cmd) < 0) {
        return -1;
    }
    return cmd.status;
}

kv_result KADI::iter_read(kv_iter_context *iter_handle) {

    struct nvme_passthru_kv_cmd cmd;
    memset(&cmd, 0, sizeof(struct nvme_passthru_kv_cmd));

    cmd.opcode = nvme_cmd_kv_iter_read;
    cmd.nsid = nsid;
    cmd.cdw3 = space_id;
    cmd.cdw5 = iter_handle->handle;
    cmd.data_addr = (__u64)iter_handle->buf;
    cmd.data_length = iter_handle->buflen;
#ifdef DUMP_ISSUE_CMD
    dump_cmd(&cmd);
#endif
    int ret = ioctl(fd, NVME_IOCTL_IO_KV_CMD, &cmd);
    if (ret < 0) { return -1;    }

    iter_handle->byteswritten = cmd.result & 0xffff;
    if (iter_handle->byteswritten > iter_handle->buflen) { 
                derr <<" # of read bytes > buffer length" << dendl;
return -1; 
    }
    
    if (cmd.status == 0x0393) { /* scan finished, but data is valid */
        iter_handle->end = true;
    }
    else
        iter_handle->end = false;

#ifdef DUMP_ISSUE_CMD
    derr << "iterator: status = " << cmd.status << ", result = " << cmd.result << ", end = " << iter_handle->end << ", bytes read = " << iter_handle->byteswritten << dendl;
#endif

    return cmd.status;
}


/**kv_result KADI::iter_read(kv_iter_context *iter_handle) {

    struct nvme_passthru_kv_cmd cmd;
    memset(&cmd, 0, sizeof(struct nvme_passthru_kv_cmd));

    cmd.opcode = nvme_cmd_kv_iter_read;
    cmd.nsid = nsid;
    cmd.cdw3 = space_id;
    cmd.cdw5 = iter_handle->handle;
    cmd.data_addr = (__u64)iter_handle->buf;
    cmd.data_length = iter_handle->buflen;
#ifdef DUMP_ISSUE_CMD
    dump_cmd(&cmd);
#endif
    int ret = ioctl(fd, NVME_IOCTL_IO_KV_CMD, &cmd);
    if (ret < 0) { return -1;    }

    iter_handle->byteswritten = cmd.result & 0xffff;

    if (cmd.status == 0x0393) { // scan finished, but data is valid 
        iter_handle->end = true;
    }
    else
        iter_handle->end = false;

#ifdef DUMP_ISSUE_CMD
    derr << "iterator: status = " << cmd.status << ", result = " << cmd.result << ", end = " << iter_handle->end << ", bytes read = " << iter_handle->byteswritten << dendl;
#endif

    return cmd.status;
}
**/

iterbuf_reader::iterbuf_reader(CephContext *c, kv_iter_context *ctx_):
        iterbuf_reader(c, ctx_->buf, ctx_->byteswritten)
{}

iterbuf_reader::iterbuf_reader(CephContext *c, void *buf_, int length_):
    cct(c), buf(buf_), bufoffset(0), byteswritten(length_),  numkeys(0)
{
    if (hasnext()) {
        numkeys = *((unsigned int*)buf);

#ifdef DUMP_ISSUE_CMD
#endif
        bufoffset += 4;
    } else {
	
        derr << " has_next = false " << dendl;
    }
}

bool iterbuf_reader::nextkey(void **key, int *length)
{
    char *current_pos = ((char *)buf) ;

    if (bufoffset + 4 >= byteswritten) return false;

    *length = *((unsigned int*)(current_pos+bufoffset)); bufoffset += 4;
    if (bufoffset + *length > byteswritten) return false;

    *key    = (current_pos+bufoffset); bufoffset += *length;

    return true;
}


/**bool iterbuf_reader::nextkey(void **key, int *length)
{
    if (!hasnext()) return false;

    char *current_pos = ((char *)ctx->buf) ;

    *length = *((unsigned int*)(current_pos+ctx->bufoffset)); ctx->bufoffset += 4;
    *key    = (current_pos+ctx->bufoffset); ctx->bufoffset += *length;

    return true;
}**/


kv_result KADI::kv_store(kv_key *key, kv_value *value, kv_cb& cb) {
    aio_cmd_ctx *ioctx = get_cmd_ctx(cb);
    memset(&ioctx->cmd, 0, sizeof(struct nvme_passthru_kv_cmd));

    ioctx->key = key;
    ioctx->value = value;



    ioctx->cmd.opcode = nvme_cmd_kv_store;
    ioctx->cmd.nsid = nsid;
    if (key->length > KVCMD_INLINE_KEY_MAX) {
        ioctx->cmd.key_addr = (__u64)key->key;
    } else {
        memcpy(ioctx->cmd.key, key->key, key->length);
    }
    ioctx->cmd.cdw5 = value->offset;
    ioctx->cmd.key_length = key->length;
    ioctx->cmd.cdw11 = key->length -1;
    ioctx->cmd.data_addr = (__u64)value->value;
    ioctx->cmd.data_length = value->length;
    ioctx->cmd.cdw10 = (value->length >>  2);
    ioctx->cmd.ctxid = aioctx.ctxid;
    ioctx->cmd.reqid = ioctx->index;

#ifdef DUMP_ISSUE_CMD
    dump_cmd(&ioctx->cmd);
    derr << "kv_store: key = " << print_key((const char *)key->key, key->length) << ", len = " << (int)key->length << dendl;
#endif

    if (ioctl(fd, NVME_IOCTL_AIO_CMD, &ioctx->cmd) < 0) {
        release_cmd_ctx(ioctx);
        return -1;
    }


    return 0;
}

kv_result KADI::kv_retrieve(kv_key *key, kv_value *value, kv_cb& cb){
    aio_cmd_ctx *ioctx = get_cmd_ctx(cb);
    memset(&ioctx->cmd, 0, sizeof(struct nvme_passthru_kv_cmd));

    ioctx->key = key;
    ioctx->value = value;

    ioctx->cmd.opcode = nvme_cmd_kv_retrieve;
    ioctx->cmd.nsid = nsid;
    ioctx->cmd.cdw3 = space_id;
    ioctx->cmd.cdw4 = 0;
    if (value->offset) {
        ioctx->cmd.cdw5 = value->offset;
    }
    else {
        ioctx->cmd.cdw5 = 0;
    }
    ioctx->cmd.data_addr = (__u64)value->value;
    ioctx->cmd.data_length = value->length;
    if (key->length <= KVCMD_INLINE_KEY_MAX) {
        memcpy(ioctx->cmd.key, key->key, key->length);
    } else {
        ioctx->cmd.key_addr = (__u64)key->key;
    }
    ioctx->cmd.key_length = key->length;
    ioctx->cmd.reqid = ioctx->index;
    ioctx->cmd.ctxid = aioctx.ctxid;

#ifdef DUMP_ISSUE_CMD
    dump_cmd(&ioctx->cmd);
    derr << "kv_retrieve: key = " << print_key((const char *)key->key, key->length) << ", len = " << (int)key->length << dendl;
#endif

    if (ioctl(fd, NVME_IOCTL_AIO_CMD, &ioctx->cmd) < 0) {
        release_cmd_ctx(ioctx);
        return -1;

    }
    return 0;
}

kv_result KADI::kv_delete(kv_key *key, kv_cb& cb, int check_exist) {
    aio_cmd_ctx *ioctx = get_cmd_ctx(cb);
    memset(&ioctx->cmd, 0, sizeof(struct nvme_passthru_kv_cmd));
    ioctx->key = key;
    ioctx->value = 0;

    ioctx->cmd.opcode = nvme_cmd_kv_delete;
    ioctx->cmd.nsid = nsid;
    ioctx->cmd.cdw3 = space_id;
    ioctx->cmd.cdw4 = check_exist;
    if (key->length <= KVCMD_INLINE_KEY_MAX) {
        memcpy(ioctx->cmd.key, key->key, key->length);
    } else {
        ioctx->cmd.key_addr = (__u64)key;
    }
    ioctx->cmd.key_length = key->length;
    ioctx->cmd.reqid = ioctx->index;
    ioctx->cmd.ctxid = aioctx.ctxid;

    if (ioctl(fd, NVME_IOCTL_AIO_CMD, &ioctx->cmd) < 0) {
        release_cmd_ctx(ioctx);
        return -1;
    }
    return 0;
}

kv_result KADI::poll_completion(uint32_t &num_events, uint32_t timeout_us) {

    FD_ZERO(&rfds);
    FD_SET(aioctx.eventfd, &rfds);

    memset(&timeout, 0, sizeof(timeout));
    timeout.tv_usec = timeout_us;

    int nr_changed_fds = select(aioctx.eventfd+1, &rfds, NULL, NULL, &timeout);

    if ( nr_changed_fds == 0 || nr_changed_fds < 0) { num_events = 0; return 0; }

    //derr << "nr_changed_fds = " << nr_changed_fds << ", event fd = " << aioctx.eventfd << dendl;

    unsigned long long eftd_ctx = 0;
    int read_s = read(aioctx.eventfd, &eftd_ctx, sizeof(unsigned long long));

    if (read_s != sizeof(unsigned long long)) {
        fprintf(stderr, "failt to read from eventfd ..\n");
        return -1;
    }

#ifdef DUMP_ISSUE_CMD
    derr << "# of events = " << eftd_ctx << dendl;
#endif

    while (eftd_ctx) {
        struct nvme_aioevents aioevents;

        int check_nr = eftd_ctx;
        if (check_nr > MAX_AIO_EVENTS) {
            check_nr = MAX_AIO_EVENTS;
        }

        if (check_nr > qdepth) {
            check_nr = qdepth;
        }

        aioevents.nr = check_nr;
        aioevents.ctxid = aioctx.ctxid;

        if (ioctl(fd, NVME_IOCTL_GET_AIOEVENT, &aioevents) < 0) {
            fprintf(stderr, "fail to read IOEVETS \n");
            return -1;
        }

        eftd_ctx -= check_nr;

        //derr << "# of events read = " << aioevents.nr << dendl;
        for (int i = 0; i < aioevents.nr; i++) {
            kv_io_context ioresult;
            const struct nvme_aioevent &event  = aioevents.events[i];

#ifdef DUMP_ISSUE_CMD
            derr << "reqid  = " << event.reqid << ", ret " << (int)event.status << "," << (int) aioevents.events[i].status << dendl;
#endif
            aio_cmd_ctx *ioctx = get_cmdctx(event.reqid);

            if (ioctx == 0) {
                ceph_abort_msg(this->cct, "ioctx is null");
            }
            fill_ioresult(*ioctx, event, ioresult);
            ioctx->call_post_fn(ioresult);
            release_cmd_ctx(ioctx);
        }
    }

    return 0;
}

kv_result KADI::fill_ioresult(const aio_cmd_ctx &ioctx, const struct nvme_aioevent &event,
                        kv_io_context &ioresult)
{
    ioresult.opcode  = ioctx.cmd.opcode;
    ioresult.retcode = event.status;

    //derr << "fill_ioresult: status =  " << event.status << ", result = " << event.result << dendl;

    ioresult.key   = ioctx.key;
    ioresult.value   = ioctx.value;

    // exceptions
    switch(ioresult.retcode) {
        case 0x393:
            if (ioresult.opcode == nvme_cmd_kv_iter_read) {
                ioresult.hiter.end = true;
                ioresult.retcode = 0;
            }
            break;
    }

    if (ioresult.retcode != 0) return 0;

    switch(ioresult.opcode) {

        case nvme_cmd_kv_retrieve:
            //derr << __func__ << "4" << dendl;
            if (ioctx.value) {
                ioresult.value->actual_value_size = event.result;
                ioresult.value->length = std::min(event.result, ioctx.value->length);
            }
            break;

        case nvme_cmd_kv_iter_req:
            //derr << "nvme_cmd_kv_iter_req" << dendl;
            if ((ioctx.cmd.cdw4 & ITER_OPTION_OPEN) != 0) {
                ioresult.hiter.id  = (event.result & 0x000000FF);
                //derr << "id = " << ioresult.hiter.id << dendl;
                ioresult.hiter.end = false;
            }
            break;

        case nvme_cmd_kv_iter_read:
            if (ioctx.buf) {
                ioresult.hiter.buf = ioctx.buf;
                ioresult.hiter.buflength = (event.result & 0xffff);
            }
            break;
    };

    return 0;
}

kv_result KADI::aio_submit(KvsTransContext *txc)
{
    int num_pending = txc->ioc.num_pending.load();
    if (num_pending == 0) return KV_SUCCESS;

    std::list<std::pair<kv_key *, kv_value *> >::iterator e = txc->ioc.running_aios.begin();
    txc->ioc.running_aios.splice(e, txc->ioc.pending_aios);

    int pending = txc->ioc.num_pending.load();
    txc->ioc.num_running += pending;
    txc->ioc.num_pending -= pending;
    assert(txc->ioc.num_pending.load() == 0);  // we should be only thread doing this
    assert(txc->ioc.pending_aios.size() == 0);

    txc->ioc.submitted = false;
    kv_result res = submit_batch(txc->ioc.running_aios.begin(),
            txc->ioc.running_aios.end(), static_cast<void*>(txc), true);
    txc->ioc.submitted = true;
    return res;
}


kv_result KADI::aio_submit(KvsReadContext *txc) {

    if (txc->key == 0 || txc->value == 0) return KV_SUCCESS;

    txc->num_running = 1;

    kv_cb f = { read_callback, txc };

    return kv_retrieve(txc->key, txc->value, f);

}

kv_result KADI::sync_read(kv_key *key, bufferlist &bl, int valuesize) {

    KvsReadContext txc(cct);
    txc.value = KvsMemPool::Alloc_value(valuesize);

    txc.num_running = 1;

    kv_cb f = { read_callback, &txc };
    kv_result ret = kv_retrieve(key, txc.value, f);
    if (ret != 0) return ret;

    return txc.read_wait(bl);
}




kv_result KADI::submit_batch(aio_iter begin, aio_iter end, void *priv, bool write )
{

    aio_iter cur = begin;
    while (cur != end) {
        kv_result res;

        //if (cur->first == 0 || cur->first->length != 16 )break;
        if (write) {
            kv_cb f = { write_callback, priv };
            if (cur->second == 0) { // delete
                res = kv_delete(cur->first, f);
            }
            else {
                res = kv_store(cur->first, cur->second, f);
            }
        }
        else {
            kv_cb f = { read_callback, priv };
            res = kv_retrieve(cur->first, cur->second, f);
            //derr << "sent retrieve cmd : res = " << res << dendl;
        }

        if (res != 0) {
            return -1;
        }

        ++cur;
    }

    return KV_SUCCESS;
}



void KADI::dump_cmd(struct nvme_passthru_kv_cmd *cmd)
{
    char buf[2048];
    int offset = sprintf(buf, "[dump issued cmd opcode (%02x)]\n", cmd->opcode);
    offset += sprintf(buf+offset, "\t opcode(%02x)\n", cmd->opcode);
    offset += sprintf(buf+offset, "\t flags(%02x)\n", cmd->flags);
    offset += sprintf(buf+offset, "\t rsvd1(%04d)\n", cmd->rsvd1);
    offset += sprintf(buf+offset, "\t nsid(%08x)\n", cmd->nsid);
    offset += sprintf(buf+offset, "\t cdw2(%08x)\n", cmd->cdw2);
    offset += sprintf(buf+offset, "\t cdw3(%08x)\n", cmd->cdw3);
    offset += sprintf(buf+offset, "\t rsvd2(%08x)\n", cmd->cdw4);
    offset += sprintf(buf+offset, "\t cdw5(%08x)\n", cmd->cdw5);
    offset += sprintf(buf+offset, "\t data_addr(%p)\n",(void *)cmd->data_addr);
    offset += sprintf(buf+offset, "\t data_length(%08x)\n", cmd->data_length);
    offset += sprintf(buf+offset, "\t key_length(%08x)\n", cmd->key_length);
    offset += sprintf(buf+offset, "\t cdw10(%08x)\n", cmd->cdw10);
    offset += sprintf(buf+offset, "\t cdw11(%08x)\n", cmd->cdw11);
    offset += sprintf(buf+offset, "\t cdw12(%08x)\n", cmd->cdw12);
    offset += sprintf(buf+offset, "\t cdw13(%08x)\n", cmd->cdw13);
    offset += sprintf(buf+offset, "\t cdw14(%08x)\n", cmd->cdw14);
    offset += sprintf(buf+offset, "\t cdw15(%08x)\n", cmd->cdw15);
    offset += sprintf(buf+offset, "\t timeout_ms(%08x)\n", cmd->timeout_ms);
    offset += sprintf(buf+offset, "\t result(%08x)\n", cmd->result);
    derr << buf << dendl;
}

