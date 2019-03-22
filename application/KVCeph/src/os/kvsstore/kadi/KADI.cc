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
#include <sys/epoll.h>
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
#include "../KvsStore.h"
#include "KADI.h"
#include "linux_nvme_ioctl.h"
#include "../kvs_debug.h"

#undef dout_prefix
#define dout_prefix *_dout << "[kadi] "
//#define DUMP_ISSUE_CMD 1

#define EPOLL_DEV 1

#ifdef EPOLL_DEV
int EpollFD_dev;
struct epoll_event watch_events;
struct epoll_event list_of_events[1];
#endif

atomic_int_fast64_t queuedepth = { 0 };

void write_callback(kv_io_context &op, void* private_data) {
    KvsTransContext *txc= (KvsTransContext *)private_data;
    if (!txc) { ceph_abort();  };
    /*if (op.opcode == nvme_cmd_kv_store) {
        char *val = (char *)op.value->value;
        if (op.value->length >= 5)
        lderr(txc->cct) << "WRITE_DONE op " << op.opcode << ", ret =  " << op.retcode << ", key " << print_key((const char *) op.key->key, op.key->length)
                        << ", len " << (int)op.key->length << ", value len " << (int)op.value->length << ", ret = " << (int)op.retcode
                        <<", data hash " << ceph_str_hash_linux((char *)op.value->value, op.value->length) 
                        << ", first 5 bytes = " << (int)val[0] << "," << (int)val[1] << "," << (int)val[2] << "," << (int)val[3] << "," << (int)val[4]     
                        << ", last 5 bytes = " << (int)val[op.value->length-1] << "," << (int)val[ op.value->length-2] << "," << (int)val[ op.value->length-3] << "," << (int)val[ op.value->length-4] << "," << (int)val[ op.value->length-5]               
                        << dendl;
    }*/

#ifdef DUMP_ISSUE_CMD
    if (op.opcode == nvme_cmd_kv_store) {
        lderr(txc->cct) << "op " << op.opcode << ", key " << print_key((const char *) op.key->key, op.key->length)
                        << ", len " << (int)op.key->length << ", value len " << (int)op.value->length << ", ret = " << (int)op.retcode
                        << dendl;
    } else {
        lderr(txc->cct) << "op " << op.opcode << ", key " << print_key((const char *) op.key->key, op.key->length)
                        << ", len " << (int)op.key->length << ", ret = " << op.retcode << dendl;
    }
#endif

    queuedepth--;
    txc->aio_finish(&op);
}


void sync_write_callback(kv_io_context &op, void* private_data) {
    KvsSyncWriteContext* txc = (KvsSyncWriteContext*)private_data;

#ifdef DUMP_ISSUE_CMD
    if (op.key) {
        lderr(txc->cct) << "read callback: key = " << op.key << dendl;
        lderr(txc->cct) << "read callback: key = " << print_key((const char *) op.key->key, op.key->length)
                        << ", len = " <<  (int)op.key->length << "retcode " << op.retcode << dendl;
    }
#endif
    queuedepth--;
    txc->retcode = op.retcode;
    txc->try_write_wake();
}

void read_callback(kv_io_context &op, void* private_data) {
    KvsReadContext* txc = (KvsReadContext*)private_data;
    if (!txc) { ceph_abort();  };

   /* lderr(txc->cct) << "READ DONE: op " << op.opcode << ", key " << print_key((const char *) op.key->key, op.key->length)
                    << ", len " << (int)op.key->length << ", value len " << (int)op.value->length << ", ret = " << (int)op.retcode 
                    <<", data hash " << ceph_str_hash_linux((char *)op.value->value, op.value->length)<< dendl;
*/
#ifdef DUMP_ISSUE_CMD
    lderr(txc->cct) << "op " << op.opcode << ", key " << print_key((const char *) op.key->key, op.key->length)
                    << ", len " << (int)op.key->length << ", value len " << (int)op.value->length << ", ret = " << (int)op.retcode << dendl;

#endif
    queuedepth--;
    txc->retcode = op.retcode;
    txc->try_read_wake();
}

void prefetch_callback(kv_io_context &op, void *private_data){
  KvsReadContext* txc = (KvsReadContext*)private_data;
  txc->retcode = op.retcode;
  bufferlist bl;
  bl.append((const char *)op.value->value, op.value->length);
  KvsOnode *on = txc->onode;
  if(bl.length() != 0 && txc->retcode == 0) {
    std::unique_lock<std::mutex> plock (on->prefetch_lock);
    on->exists = true;
    bufferptr::iterator p = bl.front().begin_deep();
    on->onode.decode(p);
    for (auto& i : on->onode.attrs){
      i.second.reassign_to_mempool(mempool::mempool_kvsstore_cache_other);
    }
    on->prefetch_cond.notify_all();
  }
  delete txc;
}

kv_result KADI::iter_readall(kv_iter_context *iter_ctx, std::list<std::pair<void*, int> > &buflist)
{
    kv_result r = iter_open(iter_ctx);
    if (r != 0) return r;
    while (!iter_ctx->end) {
        iter_ctx->byteswritten = 0;
        iter_ctx->buf = calloc(1, iter_ctx->buflen); //= malloc(iter_ctx->buflen);
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
#ifdef EPOLL_DEV
	EpollFD_dev = epoll_create(1024);
	if(EpollFD_dev<0){
		derr << "Unable to create Epoll FD; error = " << EpollFD_dev << dendl;
		return -1;
	}
#endif

    int efd = eventfd(0,0);
    if (efd < 0) {
        fprintf(stderr, "fail to create an event.\n");
     	::close(EpollFD_dev);
        return -1;
    }
    

#ifdef EPOLL_DEV
    watch_events.events = EPOLLIN;
    watch_events.data.fd = efd;
    int register_event;
    register_event = epoll_ctl(EpollFD_dev, EPOLL_CTL_ADD, efd, &watch_events);
    if (register_event)
	derr << " Failed to add FD = " << efd << ", to epoll FD = " << EpollFD_dev
		<< ", with error code  = " << register_event << dendl;
#endif

    aioctx.ctxid   = 0;
    aioctx.eventfd = efd;

    if (ioctl(fd, NVME_IOCTL_SET_AIOCTX, &aioctx) < 0) {
        derr <<  "fail to set_aioctx" << dendl;
        return -1;
    }

    derr << "KV device is opened: fd " << fd << ", efd " << efd << ", dev " << devpath.c_str() << dendl;

    return ret;
}

int KADI::close() {
    if (fd > 0) {

        ioctl(fd, NVME_IOCTL_DEL_AIOCTX, &aioctx);
        ::close((int)aioctx.eventfd);
        ::close(fd);
        derr << "KV device is closed: fd " << fd << dendl;
        fd = -1;

#ifdef EPOLL_DEV
	::close(EpollFD_dev);
#endif
    }
    return 0;
}

KADI::aio_cmd_ctx* KADI::get_cmd_ctx(kv_cb& cb) {
    std::unique_lock<std::mutex> lock (cmdctx_lock);

    while (free_cmdctxs.empty()) {
        if (cmdctx_cond.wait_for(lock, std::chrono::seconds(5)) == std::cv_status::timeout) {
            derr << "max queue depth has reached. wait..." << dendl;
        }
    }

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


iterbuf_reader::iterbuf_reader(CephContext *c, void *buf_, int length_, KADI* db_, KvsStore *store_):
    cct(c), buf(buf_), bufoffset(0), byteswritten(length_),  numkeys(0), db(db_), store(store_)
{
    if (hasnext()) {
        numkeys = *((unsigned int*)buf);
#ifdef DUMP_ISSUE_CMD
#endif
        bufoffset += 4;
    }
}


bool iterbuf_reader::nextkey(void **key, int *length)
{
redo:
    int afterKeygap = 0;
    char *current_pos = ((char *)buf) ;

    if (bufoffset + 4 >= byteswritten) return false;

    *length = *((unsigned int*)(current_pos+bufoffset)); bufoffset += 4;

    if (bufoffset + *length > byteswritten) return false;

    *key    = (current_pos+bufoffset); 
    afterKeygap = (((*length + 3) >> 2) << 2);
    bufoffset += afterKeygap;

    if (db && !db->exist(*key, *length)) {
        goto redo;
    }

    return true;
}


kv_result KADI::kv_store(kv_key *key, kv_value *value, kv_cb& cb) {
    aio_cmd_ctx *ioctx = get_cmd_ctx(cb);
    memset((void*)&ioctx->cmd, 0, sizeof(struct nvme_passthru_kv_cmd));

    if (key == 0 || key->key == 0 || value == 0 || value->value == 0) {
        ceph_abort_msg(cct, "NULL parameters in kv_store");
    }

    ioctx->key = key;
    ioctx->value = value;

    ioctx->cmd.opcode = nvme_cmd_kv_store;
    ioctx->cmd.nsid = nsid;

    if (key->length > KVCMD_INLINE_KEY_MAX) {
        ioctx->cmd.key_addr = (__u64)key->key;
    } else {
        memcpy((void*)ioctx->cmd.key, (void*)key->key, key->length);
    }
    ioctx->cmd.cdw5 = value->offset;
    ioctx->cmd.key_length = key->length;
    ioctx->cmd.cdw11 = key->length -1;
    ioctx->cmd.data_addr = (__u64)value->value;
    ioctx->cmd.data_length = value->length;
    ioctx->cmd.cdw10 = (value->length >>  2);
    ioctx->cmd.ctxid = aioctx.ctxid;
    ioctx->cmd.reqid = ioctx->index;

//derr << "WRITE SEND: key = " << print_key((const char *)key->key, key->length) << ", len = " << (int)key->length << ", value hash = " 
//<< ceph_str_hash_linux((const char*)value->value, (int)value->length) << ", val len = " << (int)value->length << dendl;
#ifdef DUMP_ISSUE_CMD
    //dump_cmd(&ioctx->cmd);
derr << "Send write IO(kv_store): key = " << print_key((const char *)key->key, key->length) << ", len = " << (int)key->length << dendl;
#endif

    int ret;
    if ((ret = ioctl(fd, NVME_IOCTL_AIO_CMD, &ioctx->cmd)) < 0) {
        release_cmd_ctx(ioctx);
        return -1;
    }

    return 0;
}

kv_result KADI::kv_retrieve(kv_key *key, kv_value *value, kv_cb& cb){
    aio_cmd_ctx *ioctx = get_cmd_ctx(cb);
    memset((void*)&ioctx->cmd, 0, sizeof(struct nvme_passthru_kv_cmd));

    if (key == 0 || key->key == 0 || value == 0 || value->value == 0) {
        ceph_abort_msg(cct, "NULL parameters in kv_retrieve");
    }

    ioctx->key = key;
    ioctx->value = value;

    ioctx->cmd.opcode = nvme_cmd_kv_retrieve;
    ioctx->cmd.nsid = nsid;
    ioctx->cmd.cdw3 = space_id;
    ioctx->cmd.cdw4 = 0;
    ioctx->cmd.cdw5 = value->offset;
    ioctx->cmd.data_addr = (__u64)value->value;
    ioctx->cmd.data_length = value->length;
    if (key->length <= KVCMD_INLINE_KEY_MAX) {
        memcpy((void*)ioctx->cmd.key, (void*)key->key, key->length);
    } else {
        ioctx->cmd.key_addr = (__u64)key->key;
    }
    ioctx->cmd.key_length = key->length;
    ioctx->cmd.reqid = ioctx->index;
    ioctx->cmd.ctxid = aioctx.ctxid;


#ifdef DUMP_ISSUE_CMD
    dump_retrieve_cmd(&ioctx->cmd);
    derr << "IO:kv_retrieve: key = " << print_key((const char *)key->key, key->length) << ", len = " << (int)key->length << dendl;
#endif
    int ret = ioctl(fd, NVME_IOCTL_AIO_CMD, &ioctx->cmd);
    if (ret < 0) {
       // derr << "kv_retrieve I/O failed: cmd = " << (unsigned int)NVME_IOCTL_AIO_CMD << ", fd = " << fd << ", cmd = " << (unsigned int)ioctx->cmd.opcode << ", ret = " << ret << dendl;

        release_cmd_ctx(ioctx);
        return -1;
    }
    return 0;
}

kv_result KADI::kv_retrieve_sync(kv_key *key, kv_value *value, uint64_t offset, size_t length, bufferlist &bl, bool &ispartial) {
    const int retcode = kv_retrieve_sync(key, value);
    if (retcode == KV_SUCCESS) {
       // derr << "IO:kv_retrieve sync-2 offset = " << offset << ", length = " << length << ", key = " << print_key((const char *)key->key, key->length) << ", hash = " << ceph_str_hash_linux((char*)value->value, value->length) <<",value length = " << value->length << dendl;
        
        int64_t len = length;

        if (offset + length > value->length || (offset == 0 && length == 0)) {
            len  = (int64_t)value->length - offset;
            if (len < 0) len = 0;
        }

        //derr << "IO:kv_retrieve sync-2 len = " << len << dendl;
        if (len > 0)
            bl.append((const char *)((char*)value->value + offset), len);

        ispartial = value->actual_value_size == 0 || offset != 0 || len != value->actual_value_size;
    }
    return retcode;
    
}
kv_result KADI::kv_retrieve_sync(kv_key *key, kv_value *value){
    struct nvme_passthru_kv_cmd cmd;
    memset((void*)&cmd, 0, sizeof(struct nvme_passthru_kv_cmd));

    if (key == 0 || key->key == 0 || value == 0 || value->value == 0) {
        ceph_abort_msg(cct, "NULL parameters in kv_retrieve");
    }
    int retried = 0;


    cmd.opcode = nvme_cmd_kv_retrieve;
    cmd.nsid = nsid;
    cmd.cdw3 = space_id;
    cmd.cdw4 = 0;
    cmd.cdw5 = value->offset;
    cmd.data_addr = (__u64)value->value;
    cmd.data_length = value->length;
    if (key->length <= KVCMD_INLINE_KEY_MAX) {
        memcpy((void*)cmd.key, (void*)key->key, key->length);
    } else {
        cmd.key_addr = (__u64)key->key;
    }
    cmd.key_length = key->length;




retry:
    int ret = ioctl(fd, NVME_IOCTL_IO_KV_CMD, &cmd);
    if (ret == 0) {
        value->actual_value_size = cmd.result;
        value->length = std::min(cmd.result,  value->length);
        if (value->length != value->actual_value_size) {
           /* derr << "retry: partially read: hash = " << ceph_str_hash_linux((char*)value->value, value->length) 
            << ", first 5 bytes = " << (int)val[0] << "," << (int)val[1] << "," << (int)val[2] << "," << (int)val[3] << "," << (int)val[4]     
                        << ", last 5 bytes = " << (int)val[value->length-1] << "," << (int)val[ value->length-2] << "," << (int)val[ value->length-3] << "," << (int)val[ value->length-4] << "," << (int)val[ value->length-5]               
                         << dendl;*/
            KvsMemPool::free_memory(value->value);
            
            //derr << "retry: length = " << value->length << ", actual size = " << value->actual_value_size << dendl;
            value->length = KvsMemPool::get_aligned_size(value->actual_value_size, 4096);
            //derr << "retry: new value length = " << value->length << dendl;
            value->value = KvsMemPool::alloc_memory(value->length);
            cmd.data_addr = (__u64)value->value;
            cmd.data_length = value->length;
            retried++;
            goto retry;
        } 
    }

    if (retried) {
        /*derr << "IO:kv_retrieve sync: retries = " << retried << ", ret = " << ret << ", key = " << print_key((const char *)key->key, key->length) << ", hash = " << ceph_str_hash_linux((char*)value->value, value->length) <<",value length = " << value->length
        << ", first 5 bytes = " << (int)val[0] << "," << (int)val[1] << "," << (int)val[2] << "," << (int)val[3] << "," << (int)val[4]     
        << ", last 5 bytes = " << (int)val[value->length-1] << "," << (int)val[ value->length-2] << "," << (int)val[ value->length-3] << "," << (int)val[ value->length-4] << "," << (int)val[ value->length-5]               
        << dendl;*/
    }
    

#ifdef DUMP_ISSUE_CMD
    dump_retrieve_cmd(&cmd);
    
#endif
    return ret;
}

kv_result KADI::get_freespace(uint64_t &bytesused, uint64_t &capacity, double &utilization) {
    char *data = (char*)calloc(1, 4096);
    struct nvme_passthru_cmd cmd;
    memset(&cmd, 0, sizeof(struct nvme_passthru_cmd));
    cmd.opcode = 0x6;
    cmd.nsid = nsid;
    cmd.addr = (__u64)data;
    cmd.data_len = 4096;
    cmd.cdw10 = 0;

    if (ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd) < 0) {
        if (data) free( data);
        return -1;
    }

    const __u64 sectorsused   = *((__u64*)data);
    const __u64 namespace_utilization =  *((__u64*)&data[16]);

    utilization = (1.0*namespace_utilization/10000.0);
    capacity   = sectorsused * 512;
    bytesused  = utilization * capacity;

    if (data) free(data);
    return 0;
}


bool KADI::exist(void *key, int length)
{
#if 1
    kv_key rk;
    rk.key = key;
    rk.length = length;
    bufferlist bl;
    int ret = sync_read(&rk, bl, 1);

    return (ret == 0);
#else
    int ret = 0;
    struct nvme_passthru_kv_cmd cmd;
    memset(&cmd, 0, sizeof (struct nvme_passthru_kv_cmd));
    cmd.opcode = nvme_cmd_kv_exist;
    cmd.nsid = nsid;
    cmd.cdw3 = space_id;
    cmd.key_length = length;
    if (length > KVCMD_INLINE_KEY_MAX) {
            cmd.key_addr = (__u64)key;
    } else {
            memcpy(cmd.key, key, length);
    }

#ifdef DUMP_ISSUE_CMD
    dump_cmd(&cmd);
#endif

    ret = ioctl(fd, NVME_IOCTL_IO_KV_CMD, &cmd);

    return (ret == 0)? true:false;
#endif
}

bool KADI::exist(kv_key *key)
{
	return exist((void*)key->key, key->length);
}

kv_result KADI::kv_delete(kv_key *key, kv_cb& cb, int check_exist) {
    aio_cmd_ctx *ioctx = get_cmd_ctx(cb);
    memset((void*)&ioctx->cmd, 0, sizeof(struct nvme_passthru_kv_cmd));

    if (key == 0 || key->key == 0) {
        ceph_abort_msg(cct, "NULL parameters in kv_delete");
    }
    ioctx->key = key;
    ioctx->value = 0;

    ioctx->cmd.opcode = nvme_cmd_kv_delete;
    ioctx->cmd.nsid = nsid;
    ioctx->cmd.cdw3 = space_id;
    ioctx->cmd.cdw4 = 1;
    if (key->length <= KVCMD_INLINE_KEY_MAX) {
        memcpy((void*)ioctx->cmd.key, (void*)key->key, key->length);
    } else {
        ioctx->cmd.key_addr = (__u64)key->key;
    }
    ioctx->cmd.key_length = key->length;
    ioctx->cmd.reqid = ioctx->index;
    ioctx->cmd.ctxid = aioctx.ctxid;

#ifdef DUMP_ISSUE_CMD
    dump_delete_cmd(&ioctx->cmd);
    derr << "IO:kv_delete: key = " << print_key((const char *)key->key, key->length) << ", len = " << (int)key->length << dendl;
#endif

    if (ioctl(fd, NVME_IOCTL_AIO_CMD, &ioctx->cmd) < 0) {
        release_cmd_ctx(ioctx);
        return -1;
    }

    return 0;
}

kv_result KADI::poll_completion(uint32_t &num_events, uint32_t timeout_us) {

    FD_ZERO(&rfds);

#ifdef EPOLL_DEV
    int timeout = timeout_us/1000;
    int nr_changed_fds = epoll_wait(EpollFD_dev, list_of_events, 1, timeout);
    if( nr_changed_fds == 0 || nr_changed_fds < 0){ num_events = 0; return 0;}
#else
    FD_SET(aioctx.eventfd, &rfds);

    memset(&timeout, 0, sizeof(timeout));
    timeout.tv_usec = timeout_us;
    int nr_changed_fds = select(aioctx.eventfd+1, &rfds, NULL, NULL, &timeout);

    if ( nr_changed_fds == 0 || nr_changed_fds < 0) { num_events = 0; return 0; }
#endif

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

            if (ioctx.value) {
                ioresult.value->actual_value_size = event.result;
                ioresult.value->length = std::min(event.result, ioctx.value->length);
                //derr << "length = " << ioresult.value->length << ", actual = " << ioresult.value->actual_value_size << dendl;
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
    kv_result res = 0;
    
    std::list<std::pair<kv_key *, kv_value *> >::iterator e = txc->ioc.running_aios.begin();
    txc->ioc.running_aios.splice(e, txc->ioc.pending_aios);
    txc->ioc.num_running = txc->ioc.running_aios.size();

        //std::list<std::pair<kv_key *, kv_value *> > temp = txc->ioc.pending_aios;
    {
        std::unique_lock<std::mutex> lk(txc->ioc.running_aio_lock);
        res = submit_batch(txc->ioc.running_aios.begin(), txc->ioc.running_aios.end(), static_cast<void*>(txc), true);
    }
    return res;
 }



kv_result KADI::aio_submit(KvsReadContext *txc) {

    if (txc->key == 0 || txc->value == 0) return KV_SUCCESS;

    txc->num_running = 1;

    kv_cb f = { read_callback, txc };
    queuedepth++;
    txc->start = ceph_clock_now();
    return kv_retrieve(txc->key, txc->value, f);

}

kv_result KADI::sync_submit(KvsReadContext *txc) {

    if (txc->key == 0 || txc->value == 0) return KV_SUCCESS;

    return kv_retrieve_sync(txc->key, txc->value);

}

kv_result KADI::aio_submit(KvsSyncWriteContext *txc) {

    if (txc->key == 0) return KV_SUCCESS;

    txc->num_running = 1;

    kv_cb f = { sync_write_callback, txc };

    if (txc->value == 0) {
        queuedepth++;
        return kv_delete(txc->key, f, 0);
    }
    else {
        queuedepth++;
        return kv_store(txc->key, txc->value, f);
    }
}

kv_result KADI::aio_submit_prefetch(KvsReadContext *txc){
    if (txc->key == 0 || txc->value == 0) return KV_SUCCESS;
    txc->num_running = 1;
    kv_cb f = { prefetch_callback, txc};
    kv_result ret = kv_retrieve(txc->key, txc->value, f);
    return ret;
}


kv_result KADI::sync_read(kv_key *key, bufferlist &bl, int valuesize) {

    KvsReadContext txc(cct);
    txc.value = KvsMemPool::Alloc_value(valuesize);

    txc.num_running = 1;
    txc.start = ceph_clock_now();

    kv_cb f = { read_callback, &txc };
    queuedepth++;
    kv_result ret = kv_retrieve(key, txc.value, f);
    if (ret != 0) return ret;

    return txc.read_wait(bl);
}



kv_result KADI::submit_batch(aio_iter begin, aio_iter end, void *priv, bool write )
{

    aio_iter cur = begin;
    while (cur != end) {
        kv_result res;

        if (write) {
            kv_cb f = { write_callback, priv };
            if (cur->second == 0) { // delete
                queuedepth++;
                res = kv_delete(cur->first, f);
            }
            else {
                queuedepth++;
                res = kv_store(cur->first, cur->second, f);
            }
        }
        else {
            kv_cb f = { read_callback, priv };
            queuedepth++;
            res = kv_retrieve(cur->first, cur->second, f);
        }

        if (res != 0) {
            return -1;
        }

        ++cur;
    }

    return KV_SUCCESS;
}

void KADI::dump_delete_cmd(struct nvme_passthru_kv_cmd *cmd) {
    char buf[2048];
    int offset = sprintf(buf, "[dump delete cmd (%02x)]\n", cmd->opcode);

    offset += sprintf(buf+offset, "\t opcode(%02x)\n", cmd->opcode);
    offset += sprintf(buf+offset, "\t nsid(%04x)\n", cmd->nsid);
    offset += sprintf(buf+offset, "\t cdw3(%04x)\n", cmd->cdw3);
    offset += sprintf(buf+offset, "\t cdw4(%04x)\n", cmd->cdw4);
    offset += sprintf(buf+offset, "\t cdw5(%04x)\n", cmd->cdw5);

    offset += sprintf(buf+offset, "\t cmd.key_length(%02x)\n", cmd->key_length);

    if (cmd->key_length <= KVCMD_INLINE_KEY_MAX) {
        offset += sprintf(buf+offset, "\t cmd.key (%s)\n", print_key((char*)cmd->key, cmd->key_length).c_str());
    }
    else {
        offset += sprintf(buf+offset, "\t cmd.key (%s)\n", print_key((char*)cmd->key_addr, cmd->key_length).c_str());
    }
    offset += sprintf(buf+offset, "\t reqid(%04llu)\n", cmd->reqid);
    offset += sprintf(buf+offset, "\t ctxid(%04d)\n", cmd->ctxid);
    derr << buf << dendl;
}


void KADI::dump_retrieve_cmd(struct nvme_passthru_kv_cmd *cmd) {
    char buf[2048];
    int offset = sprintf(buf, "[dump retrieve cmd (%02x)]\n", cmd->opcode);

    offset += sprintf(buf+offset, "\t opcode(%02x)\n", cmd->opcode);
    offset += sprintf(buf+offset, "\t nsid(%04x)\n", cmd->nsid);
    offset += sprintf(buf+offset, "\t cdw3(%04x)\n", cmd->cdw3);
    offset += sprintf(buf+offset, "\t cdw4(%04x)\n", cmd->cdw4);
    offset += sprintf(buf+offset, "\t cdw5(%04x)\n", cmd->cdw5);

    offset += sprintf(buf+offset, "\t cmd.key_length(%02x)\n", cmd->key_length);

    if (cmd->key_length <= KVCMD_INLINE_KEY_MAX) {
        offset += sprintf(buf+offset, "\t cmd.key (%s)\n", print_key((char*)cmd->key, cmd->key_length).c_str());
    }
    else {
        offset += sprintf(buf+offset, "\t cmd.key (%s)\n", print_key((char*)cmd->key_addr, cmd->key_length).c_str());
    }

    offset += sprintf(buf+offset, "\t cmd.data_length(%02x)\n", cmd->data_length);
    offset += sprintf(buf+offset, "\t cmd.data(%p)\n", (void*)cmd->data_addr);
    offset += sprintf(buf+offset, "\t reqid(%04llu)\n", cmd->reqid);
    offset += sprintf(buf+offset, "\t ctxid(%04d)\n", cmd->ctxid);
    derr << buf << dendl;
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
