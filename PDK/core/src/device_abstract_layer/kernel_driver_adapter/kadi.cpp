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
#include "kadi.h"
#include "linux_nvme_ioctl.h"
#include "kadi_debug.h"

//#define DUMP_ISSUE_CMD 1

//#define EPOLL_DEV 1

#ifdef EPOLL_DEV
int EpollFD_dev;
struct epoll_event watch_events;
struct epoll_event list_of_events[1];
#endif

const int identify_ret_data_size = 4096;

kv_result KADI::iter_readall(uint8_t ks_id, kv_iter_context *iter_ctx, 
    nvme_kv_iter_req_option option, std::list<std::pair<void *, int>> &buflist)
{
    kv_result r = iter_open(ks_id, iter_ctx, option);
    if (r != 0)
        return r;
    while (!iter_ctx->end)
    {
        iter_ctx->byteswritten = 0;
        iter_ctx->buf = calloc(1, iter_ctx->buflen);
        iter_read(iter_ctx);

        if (iter_ctx->byteswritten > 0)
        {
            buflist.push_back(std::make_pair(iter_ctx->buf, 
                iter_ctx->byteswritten));
        }
    }
    r = iter_close(iter_ctx);
    return r;
}

int KADI::open(std::string &devpath)
{

    FTRACE
    int ret = 0;
    fd = ::open(devpath.c_str(), O_RDWR);
    if (fd < 0)
    {
        std::cerr << "can't open a device : " << devpath << std::endl;
        return fd;
    }

    nsid = ioctl(fd, NVME_IOCTL_ID);
    if (nsid == (unsigned)-1)
    {
        std::cerr << "can't get an ID" << std::endl;
        return KV_ERR_SYS_IO;
    }

    space_id = 0;

    for (int i = 0; i < qdepth; i++)
    {
        aio_cmd_ctx *ctx = (aio_cmd_ctx *)calloc(1, sizeof(aio_cmd_ctx));
        if(ctx){
            ctx->index = i;
        }
        free_cmdctxs.push_back(ctx);
    }
#ifdef EPOLL_DEV
    EpollFD_dev = epoll_create(1024);
    if (EpollFD_dev < 0)
    {
        std::cerr << "Unable to create Epoll FD; error = " << EpollFD_dev << std::endl;
        return -1;
    }
#endif

    int efd = eventfd(0, 0);
    if (efd < 0)
    {
        std::cerr << "fail to create an event." << std::endl;
#ifdef EPOLL_DEV
        ::close(EpollFD_dev);
#endif
        return -1;
    }

#ifdef EPOLL_DEV
    watch_events.events = EPOLLIN;
    watch_events.data.fd = efd;
    int register_event;
    register_event = epoll_ctl(EpollFD_dev, EPOLL_CTL_ADD, efd, &watch_events);
    if (register_event)
        std::cerr << " Failed to add FD = " << efd << ", to epoll FD = " << EpollFD_dev
                  << ", with error code  = " << register_event << std::endl;
#endif

    aioctx.ctxid = 0;
    aioctx.eventfd = efd;

    if (ioctl(fd, NVME_IOCTL_SET_AIOCTX, &aioctx) < 0)
    {
        std::cerr << "fail to set_aioctx" << std::endl;
        return KV_ERR_SYS_IO;
    }

    //std::cerr << "KV device is opened: fd " << fd << ", efd " << efd << ", dev " << devpath.c_str() <<std::endl;

    return ret;
}


void *KDThread::_entry_func(void *arg) {
  return ((KDThread*)arg)->entry();
}

void *KDThread::entry() {
  uint32_t num_events = 2048;
  while (!stop_) {
    dev->poll_completion(num_events, 500000);
  }
  return 0;
}


int KADI::start_cbthread() {
    if (!this->cb_thread.started)
        this->cb_thread.start();
    return 0;
}

int KADI::close()
{
    if (fd > 0)
    {
        this->cb_thread.stop();

        for (aio_cmd_ctx *p: free_cmdctxs) {
            free((void*)p);
        }

        if(ioctl(fd, NVME_IOCTL_DEL_AIOCTX, &aioctx) < 0){
            std::cerr << "KV device is closed error!" << std::endl;
            return KADI_ERR_IO;
        }
        ::close((int)aioctx.eventfd);
        ::close(fd);
        std::cerr << "KV device is closed: fd " << fd << std::endl;
        fd = -1;

#ifdef EPOLL_DEV
        ::close(EpollFD_dev);
#endif
    }
    return 0;
}

KADI::aio_cmd_ctx *KADI::get_cmd_ctx(const kv_postprocess_function *cb)
{
    bool print_log_flag = true;
    std::unique_lock<std::mutex> lock(cmdctx_lock);
    while (free_cmdctxs.empty())
    {
        if (cmdctx_cond.wait_for(lock, std::chrono::seconds(5)) == 
          std::cv_status::timeout && print_log_flag == true) {
            std::cerr << "max queue depth has reached. wait..." << std::endl;
            print_log_flag = false;
        }
    }

    aio_cmd_ctx *p = free_cmdctxs.back();
    free_cmdctxs.pop_back();
    if(cb) {
      p->post_fn = cb->post_fn;
      p->post_data = cb->private_data;
    }else {
      p->post_fn = NULL;
      p->post_data = NULL;
    }
    pending_cmdctxs.insert(std::make_pair(p->index, p));
    return p;
}

void KADI::release_cmd_ctx(aio_cmd_ctx *p)
{
    std::lock_guard<std::mutex> lock(cmdctx_lock);

    pending_cmdctxs.erase(p->index);
    free_cmdctxs.push_back(p);
    cmdctx_cond.notify_one();
}

kv_result KADI::iter_open(uint8_t ks_id, kv_iter_context *iter_handle,
  nvme_kv_iter_req_option option)
{
    struct nvme_passthru_kv_cmd cmd;
    memset(&cmd, 0, sizeof(struct nvme_passthru_kv_cmd));

    cmd.opcode = nvme_cmd_kv_iter_req;
    cmd.cdw3 = ks_id;
    cmd.nsid = nsid;
    cmd.cdw4 = (ITER_OPTION_OPEN | option);
    cmd.cdw12 = iter_handle->prefix;
    cmd.cdw13 = iter_handle->bitmask;
#ifdef DUMP_ISSUE_CMD
    dump_cmd(&cmd);
#endif
    int ret = ioctl(fd, NVME_IOCTL_IO_KV_CMD, &cmd);
    if (ret < 0)
    {
        return KV_ERR_SYS_IO;
    }

    iter_handle->handle = cmd.result & 0xff;
    iter_handle->end = false;

    return cmd.status;
}

kv_result KADI::iter_close(kv_iter_context *iter_handle)
{
    struct nvme_passthru_kv_cmd cmd;
    memset(&cmd, 0, sizeof(struct nvme_passthru_kv_cmd));
    cmd.opcode = nvme_cmd_kv_iter_req;
    //cmd.cdw3 = space_id;  //iterator close and read don't need keyspace id
    cmd.nsid = nsid;
    cmd.cdw4 = ITER_OPTION_CLOSE;
    cmd.cdw5 = iter_handle->handle;
#ifdef DUMP_ISSUE_CMD
    dump_cmd(&cmd);
#endif
    if (ioctl(fd, NVME_IOCTL_IO_KV_CMD, &cmd) < 0)
    {
        return KV_ERR_SYS_IO;
    }
    return cmd.status;
}


kv_result KADI::iter_read_async(kv_iter_context *iter_handle, const kv_postprocess_function *cb)
{
    aio_cmd_ctx *ioctx = get_cmd_ctx(cb);
    memset((void *)&ioctx->cmd, 0, sizeof(struct nvme_passthru_kv_cmd));
    ioctx->buf = iter_handle->buf;
    ioctx->cmd.opcode = nvme_cmd_kv_iter_read;
    ioctx->cmd.nsid = nsid;
    //ioctx->cmd.cdw3 = space_id; //iterator close and read don't need keyspace id
    ioctx->cmd.cdw5 = iter_handle->handle;
    ioctx->cmd.data_addr = (__u64)iter_handle->buf;
    ioctx->cmd.data_length = iter_handle->buflen;
    ioctx->cmd.ctxid = aioctx.ctxid;
    ioctx->cmd.reqid = ioctx->index;

#ifdef DUMP_ISSUE_CMD
    dump_cmd(&cmd);
#endif
    int ret = ioctl(fd, NVME_IOCTL_AIO_CMD, &ioctx->cmd);
    if (ret < 0)
    {
        release_cmd_ctx(ioctx);
        return KV_ERR_SYS_IO;
    }

    return ret;
}

kv_result KADI::iter_read(kv_iter_context *iter_handle)
{
    struct nvme_passthru_kv_cmd cmd;
    memset(&cmd, 0, sizeof(struct nvme_passthru_kv_cmd));

    cmd.opcode = nvme_cmd_kv_iter_read;
    cmd.nsid = nsid;
    //cmd.cdw3 = space_id; //iterator close and read don't need keyspace id
    cmd.cdw5 = iter_handle->handle;
    cmd.data_addr = (__u64)iter_handle->buf;
    cmd.data_length = iter_handle->buflen;
#ifdef DUMP_ISSUE_CMD
    dump_cmd(&cmd);
#endif
    int ret = ioctl(fd, NVME_IOCTL_IO_KV_CMD, &cmd);
    if (ret < 0)
    {
        return KV_ERR_SYS_IO;
    }

    iter_handle->byteswritten = cmd.result & 0xffff;
    if (iter_handle->byteswritten > iter_handle->buflen)
    {
        std::cerr << " # of read bytes > buffer length" << std::endl;
        return -1;
    }

    if (cmd.status == 0x0393)
    { /* scan finished, but data is valid */
        iter_handle->end = true;
    }
    else
        iter_handle->end = false;

#ifdef DUMP_ISSUE_CMD
    std::cerr << "iterator: status = " << cmd.status << ", result = " << cmd.result << ", end = " << iter_handle->end << ", bytes read = " << iter_handle->byteswritten << std::endl;
#endif

    return cmd.status;
}

kv_result KADI::iter_list(kv_iterator *iter_list, uint32_t *count)
{
    const int MAX_LOG_PAGE_SIZE = 512;
    const uint32_t log_page_id = 0xd0;
    const int handle_info_size = 16;

    char logbuf[MAX_LOG_PAGE_SIZE];
    struct nvme_passthru_cmd cmd;
    
    if (*count == 0) { return KV_ERR_BUFFER_SMALL;    }
    
    memset(logbuf, 0, MAX_LOG_PAGE_SIZE);
    memset(&cmd, 0, sizeof(struct nvme_passthru_cmd));

    uint32_t buffer_size = MAX_LOG_PAGE_SIZE;
    cmd.opcode = nvme_cmd_admin_get_log_page;
    cmd.addr = (uint64_t)logbuf;
    cmd.data_len = buffer_size;
    cmd.nsid = nsid;
    cmd.cdw10 = (__u32)(((buffer_size >> 2) - 1) << 16) | ((__u32)log_page_id & 0x000000ff);

    int ret = ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd);
    if (ret != 0) { return KV_ERR_SYS_IO; }

    // start parsing 

    // count of open iterators
    uint32_t open_count = 0;
    uint32_t offset = 0;
    uint32_t max = std::min((uint32_t)SAMSUNG_MAX_ITERATORS, *count);

    for (uint32_t i = 0; i < max; i++)
    {
        offset = (i * handle_info_size);
        uint8_t status = (*(uint8_t *)(logbuf + offset + 1));
        iter_list[open_count].status = status;
        iter_list[open_count].handle_id = (*(uint8_t *)(logbuf + offset + 0));
        iter_list[open_count].type = (*(uint8_t *)(logbuf + offset + 2)) - \
           ITER_LIST_ITER_TYPE_OFFSET + KV_ITERATOR_OPT_KEY;
        iter_list[open_count].keyspace_id = (*(uint8_t *)(logbuf + offset + 3));
        iter_list[open_count].prefix = (*(uint32_t *)(logbuf + offset + 4));
        iter_list[open_count].bitmask = (*(uint32_t *)(logbuf + offset + 8));
        iter_list[open_count].is_eof = (*(uint8_t *)(logbuf + offset + 12));
        iter_list[open_count].reserved[0] = (*(uint8_t *)(logbuf + offset + 13));
        iter_list[open_count].reserved[1] = (*(uint8_t *)(logbuf + offset + 14));
        iter_list[open_count].reserved[2] = (*(uint8_t *)(logbuf + offset + 15));
        // fprintf(stderr, "handle_id=%d status=%d type=%d prefix=%08x bitmask=%08x is_eof=%d\n",
        //             iter_list[open_count].handle_id, iter_list[open_count].status, iter_list[open_count].type, iter_list[open_count].prefix, iter_list[open_count].bitmask, iter_list[open_count].is_eof);
        /*The bitpattern of the KV API is of big-endian mode. If the CPU is of little-endian mode,
                  the bit pattern and bit mask should be transformed.*/
        iter_list[open_count].prefix = htobe32(iter_list[open_count].prefix);
        iter_list[open_count].bitmask = htobe32(iter_list[open_count].bitmask);

        open_count++;
    }
    *count = open_count;

    return KV_SUCCESS;
}

iterbuf_reader::iterbuf_reader(CephContext *c, void *buf_, int length_, KADI *db_, KvsStore *store_) : cct(c), buf(buf_), bufoffset(0), byteswritten(length_), numkeys(0), db(db_), store(store_)
{
    if (hasnext())
    {
        numkeys = *((unsigned int *)buf);
        bufoffset += 4;
    }
}

bool iterbuf_reader::nextkey(void **key, int *length)
{
redo:
    int afterKeygap = 0;
    char *current_pos = ((char *)buf);

    if (bufoffset + 4 >= byteswritten)
        return false;

    *length = *((unsigned int *)(current_pos + bufoffset));
    bufoffset += 4;

    if (bufoffset + *length > byteswritten)
        return false;

    *key = (current_pos + bufoffset);
    afterKeygap = (((*length + 3) >> 2) << 2);
    bufoffset += afterKeygap;

    if (!db){
        return false;
    }
    if (db && !db->exist(0, *key, *length, 0))
    {//currently, class iterbuf_reader is not used.
        goto redo;
    }

    return true;
}

uint32_t KADI::get_dev_waf()
{
    const int MAX_LOG_PAGE_SIZE = 512;
    const uint32_t log_page_id = 0xCA;

    char logbuf[MAX_LOG_PAGE_SIZE];
    struct nvme_passthru_cmd cmd;
    uint32_t buffer_size = MAX_LOG_PAGE_SIZE;

    memset(logbuf, 0, MAX_LOG_PAGE_SIZE);
    memset(&cmd, 0, sizeof(struct nvme_passthru_cmd));

    cmd.opcode = 0x2; //nvme_admin_get_log_page;
    cmd.nsid = nsid;
    cmd.addr = (uint64_t)logbuf;
    cmd.data_len = buffer_size;

    cmd.cdw10 = (__u32)(((buffer_size >> 2) - 1) << 16) | ((__u32)log_page_id & 0x000000ff);

    int ret = ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd);
    if (ret != 0) {
        return KV_ERR_SYS_IO;
    }

    uint32_t waf = 0;
    memcpy(&waf, logbuf + 256, sizeof(waf));

    return waf;
}
kv_result KADI::kv_store(uint8_t ks_id, kv_key *key, kv_value *value,
  nvme_kv_store_option option, const kv_postprocess_function *cb)
{
  if (!key || !key->key || !value)
   {
      return KADI_ERR_NULL_INPUT;
   }
    aio_cmd_ctx *ioctx = get_cmd_ctx(cb);
    memset((void *)&ioctx->cmd, 0, sizeof(struct nvme_passthru_kv_cmd));

    ioctx->key = key;
    ioctx->value = value;

    ioctx->cmd.opcode = nvme_cmd_kv_store;
    ioctx->cmd.nsid = nsid;
    ioctx->cmd.cdw3 = ks_id;
    if (key->length > KVCMD_INLINE_KEY_MAX)
    {
        ioctx->cmd.key_addr = (__u64)key->key;
    }
    else
    {
        memcpy((void *)ioctx->cmd.key, (void *)key->key, key->length);
    }
    ioctx->cmd.cdw5 = value->offset;
    ioctx->cmd.key_length = key->length;
    ioctx->cmd.cdw4 = option;
    ioctx->cmd.cdw11 = key->length - 1;
    ioctx->cmd.data_addr = (__u64)value->value;
    ioctx->cmd.data_length = value->length;
    ioctx->cmd.cdw10 = (value->length >> 2);
    ioctx->cmd.ctxid = aioctx.ctxid;
    ioctx->cmd.reqid = ioctx->index;
 
#ifdef DUMP_ISSUE_CMD
    //dump_cmd(&ioctx->cmd);
    std::cerr << "IO:kv_store: key = " << print_key((const char *)key->key, key->length) << ", len = " << (int)key->length << std::endl;
#endif

    int ret;
    if ((ret = ioctl(fd, NVME_IOCTL_AIO_CMD, &ioctx->cmd)) < 0)
    {
        release_cmd_ctx(ioctx);
        return KV_ERR_SYS_IO;
    }

    return 0;
}

kv_result KADI::kv_retrieve(uint8_t ks_id, kv_key *key, kv_value *value, const kv_postprocess_function *cb)
{
   if (!key || !key->key || !value)
   {
       return KADI_ERR_NULL_INPUT;
   }
    aio_cmd_ctx *ioctx = get_cmd_ctx(cb);
    memset((void *)&ioctx->cmd, 0, sizeof(struct nvme_passthru_kv_cmd));

    ioctx->key = key;
    ioctx->value = value;

    ioctx->cmd.opcode = nvme_cmd_kv_retrieve;
    ioctx->cmd.nsid = nsid;
    ioctx->cmd.cdw3 = ks_id;
    ioctx->cmd.cdw4 = 0;
    ioctx->cmd.cdw5 = value->offset;
    ioctx->cmd.data_addr = (__u64)value->value;
    ioctx->cmd.data_length = value->length;
    if (key->length <= KVCMD_INLINE_KEY_MAX)
    {
        memcpy((void *)ioctx->cmd.key, (void *)key->key, key->length);
    }
    else
    {
        ioctx->cmd.key_addr = (__u64)key->key;
    }
    ioctx->cmd.key_length = key->length;
    ioctx->cmd.reqid = ioctx->index;
    ioctx->cmd.ctxid = aioctx.ctxid;

#ifdef DUMP_ISSUE_CMD
    dump_retrieve_cmd(&ioctx->cmd);
    std::cerr << "IO:kv_retrieve: key = " << print_key((const char *)key->key, key->length) << ", len = " << (int)key->length << std::endl;
#endif
    int ret = ioctl(fd, NVME_IOCTL_AIO_CMD, &ioctx->cmd);
    if (ret < 0)
    {
        //std::cerr << "kv_retrieve I/O failed: cmd = " << (unsigned int)NVME_IOCTL_AIO_CMD << ", fd = " << fd << ", cmd = " << (unsigned int)ioctx->cmd.opcode << ", ret = " << ret <<std::endl;

        release_cmd_ctx(ioctx);
        return KV_ERR_SYS_IO;
    }
    return 0;
}

kv_result KADI::kv_retrieve_sync(uint8_t ks_id, kv_key *key, kv_value *value)
{
    if (!key || !key->key || !value || !value->value)
    {
        return KADI_ERR_NULL_INPUT;
    }

    struct nvme_passthru_kv_cmd cmd;
    memset((void *)&cmd, 0, sizeof(struct nvme_passthru_kv_cmd));
    cmd.opcode = nvme_cmd_kv_retrieve;
    cmd.nsid = nsid;
    cmd.cdw3 = ks_id;
    cmd.cdw4 = 0;
    cmd.cdw5 = value->offset;
    cmd.data_addr = (__u64)value->value;
    cmd.data_length = value->length;
    if (key->length <= KVCMD_INLINE_KEY_MAX)
    {
        //memcpy((void*)cmd.key, (void*)key->key, key->length);
    }
    else
    {
        cmd.key_addr = (__u64)key->key;
    }
    cmd.key_length = key->length;

#ifdef DUMP_ISSUE_CMD
    dump_retrieve_cmd(&cmd);
    std::cerr << "IO:kv_retrieve sync: key = " << print_key((const char *)key->key, key->length) << ", len = " << (int)key->length << std::endl;
#endif

    int ret = ioctl(fd, NVME_IOCTL_IO_KV_CMD, &cmd);
    if (ret == 0)
    {
        value->actual_value_size = cmd.result;
        value->length = std::min(cmd.result, value->length);
    }
    else {
      ret = KV_ERR_SYS_IO;
    }

    return ret;
}

kv_result KADI::update_capacity()
{
    uint64_t bytesused, capacity;
    double utilization;
    int r = get_freespace(bytesused, capacity, utilization);
    if (r != 0)
    {
        return -1;
    }
    this->capacity = capacity;
    return r;
}

kv_result KADI::get_freespace(uint64_t &bytesused, uint64_t &capacity, double &utilization)
{
    char *data = (char *)calloc(1, identify_ret_data_size);
    if(data == NULL){
       return KADI_ERR_MEMORY;
    }
    struct nvme_passthru_cmd cmd;
    memset(&cmd, 0, sizeof(struct nvme_passthru_cmd));
    cmd.opcode = nvme_cmd_admin_identify;
    cmd.nsid = nsid;
    cmd.addr = (__u64)data;
    cmd.data_len = identify_ret_data_size;
    cmd.cdw10 = 0;

    if (ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd) < 0)
    {
        if (data){
            free(data);
            data = NULL;
        }
        return KV_ERR_SYS_IO;
    }

    const __u64 namespace_size = *((__u64 *)data);
    const __u64 namespace_utilization = *((__u64 *)&data[16]);
    capacity = namespace_size * BLOCK_SIZE;
    bytesused = namespace_utilization * BLOCK_SIZE;
    utilization = (1.0 * namespace_utilization) / namespace_size;
    if (data){
        free(data);
        data = NULL;
    }
    return 0;
}

bool KADI::exist(uint8_t ks_id, void *key, int length, const kv_postprocess_function *cb)
{
    int ret = 0;
    aio_cmd_ctx *ioctx = get_cmd_ctx(cb);
    memset((void *)&ioctx->cmd, 0, sizeof(struct nvme_passthru_kv_cmd));
    ioctx->cmd.opcode = nvme_cmd_kv_exist;
    ioctx->cmd.nsid = nsid;
    ioctx->cmd.cdw3 = ks_id;
    ioctx->cmd.key_length = length;
    if (length > KVCMD_INLINE_KEY_MAX)
    {
        ioctx->cmd.key_addr = (__u64)key;
    }
    else
    {
        memcpy((void*)ioctx->cmd.key, key, length);
    }
    ioctx->cmd.ctxid = aioctx.ctxid;
    ioctx->cmd.reqid = ioctx->index;

#ifdef DUMP_ISSUE_CMD
    dump_cmd(&cmd);
#endif
    if (cb == 0) {
        ret = ioctl(fd, NVME_IOCTL_IO_KV_CMD, &ioctx->cmd);
        release_cmd_ctx(ioctx);
    } else {
        // async 
        ret = ioctl(fd, NVME_IOCTL_AIO_CMD, &ioctx->cmd);
        if (ret < 0)
        {
            release_cmd_ctx(ioctx);
            return KADI_ERR_IO;
        }
    }
    return (ret == 0) ? true : false;
}

bool KADI::exist(uint8_t ks_id, kv_key *key, const kv_postprocess_function *cb)
{
    return exist(ks_id, (void *)key->key, key->length, cb);
}

kv_result KADI::kv_delete(uint8_t ks_id, kv_key *key,
  const kv_postprocess_function *cb, int check_exist)
{
    if (!key || !key->key)
    {
        return KADI_ERR_NULL_INPUT;
    }

    aio_cmd_ctx *ioctx = get_cmd_ctx(cb);
    memset((void *)&ioctx->cmd, 0, sizeof(struct nvme_passthru_kv_cmd));
    ioctx->key = key;
    ioctx->value = 0;

    ioctx->cmd.opcode = nvme_cmd_kv_delete;
    ioctx->cmd.nsid = nsid;
    ioctx->cmd.cdw3 = ks_id;
    ioctx->cmd.cdw4 = check_exist;
    if (key->length <= KVCMD_INLINE_KEY_MAX)
    {
        memcpy((void *)ioctx->cmd.key, (void *)key->key, key->length);
    }
    else
    {
        ioctx->cmd.key_addr = (__u64)key->key;
    }
    ioctx->cmd.key_length = key->length;
    ioctx->cmd.reqid = ioctx->index;
    ioctx->cmd.ctxid = aioctx.ctxid;

#ifdef DUMP_ISSUE_CMD
    dump_delete_cmd(&ioctx->cmd);
    std::cerr << "IO:kv_delete: key = " << print_key((const char *)key->key, key->length) << ", len = " << (int)key->length << std::endl;
#endif

    if (ioctl(fd, NVME_IOCTL_AIO_CMD, &ioctx->cmd) < 0)
    {
        release_cmd_ctx(ioctx);
        return KV_ERR_SYS_IO;
    }

    return 0;
}

kv_result KADI::poll_completion(uint32_t &num_events, uint32_t timeout_us)
{

    FD_ZERO(&rfds);

#ifdef EPOLL_DEV
    int timeout = timeout_us / 1000;
    int nr_changed_fds = epoll_wait(EpollFD_dev, list_of_events, 1, timeout);
    if (nr_changed_fds == 0 || nr_changed_fds < 0)
    {
        num_events = 0;
        return 0;
    }
#else
    FD_SET(aioctx.eventfd, &rfds);

    memset(&timeout, 0, sizeof(timeout));
    timeout.tv_usec = timeout_us;
    int nr_changed_fds = select(aioctx.eventfd + 1, &rfds, NULL, NULL, &timeout);

    if (nr_changed_fds == 0 || nr_changed_fds < 0)
    {
        num_events = 0;
        return 0;
    }
#endif

    unsigned long long eftd_ctx = 0;
    int read_s = read(aioctx.eventfd, &eftd_ctx, sizeof(unsigned long long));

    if (read_s != sizeof(unsigned long long))
    {
        std::cerr << "fail to read from eventfd .." << std::endl;
        return KADI_ERR_IO;
    }

#ifdef DUMP_ISSUE_CMD
    std::cerr << "# of events = " << eftd_ctx << std::endl;
#endif

    while (eftd_ctx)
    {
        struct nvme_aioevents aioevents;

        int check_nr = eftd_ctx;
        if (check_nr > MAX_AIO_EVENTS)
        {
            check_nr = MAX_AIO_EVENTS;
        }

        if (check_nr > qdepth)
        {
            check_nr = qdepth;
        }

        aioevents.nr = check_nr;
        aioevents.ctxid = aioctx.ctxid;
        num_events += check_nr;

        if (ioctl(fd, NVME_IOCTL_GET_AIOEVENT, &aioevents) < 0)
        {
            std::cerr << "NVME_IOCTL_GET_AIOEVENT failed" << std::endl;
            return KADI_ERR_IO;
        }

        eftd_ctx -= check_nr;

        //std::cerr << "# of events read = " << aioevents.nr <<std::endl;
        for (int i = 0; i < aioevents.nr; i++)
        {
            kv_io_context ioresult;
            const struct nvme_aioevent &event = aioevents.events[i];

#ifdef DUMP_ISSUE_CMD
            std::cerr << "reqid  = " << event.reqid << ", ret " << (int)event.status << "," << (int)aioevents.events[i].status << std::endl;
#endif
            aio_cmd_ctx *ioctx = get_cmdctx(event.reqid);

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
    ioresult.opcode = ioctx.cmd.opcode;
    ioresult.retcode = event.status;
    ioresult.key = ioctx.key;
    ioresult.value = ioctx.value;
    ioresult.private_data = ioctx.post_data;

    // exceptions
    switch (ioresult.retcode)
    {
    case 0x393:
        if (ioresult.opcode == nvme_cmd_kv_iter_read)
        {
            ioresult.hiter.buf = ioctx.buf;
            ioresult.hiter.buflength = (event.result & 0xffff);
            ioresult.hiter.end = true;
            ioresult.retcode = 0;
            return 0;
        }
        break;
    }

    if (ioresult.retcode != 0) {
        return 0;
    }

    switch (ioresult.opcode)
    {

    case nvme_cmd_kv_retrieve:

        if (ioctx.value)
        {
            ioresult.value->actual_value_size = event.result - ioctx.value->offset;
            //event.result is the total value length that returned from ssd, 
            //remain value length = event.result - offset, user inputted buffer length may
            //big or little than remain_val_len
            ioresult.value->length = std::min(ioresult.value->actual_value_size,
              ioctx.value->length);
            //std::cerr << "length = " << ioresult.value->length << ", actual = " << ioresult.value->actual_value_size <<std::endl;
        }
        break;

    case nvme_cmd_kv_iter_req:
        //std::cerr << "nvme_cmd_kv_iter_req" <<std::endl;
        if ((ioctx.cmd.cdw4 & ITER_OPTION_OPEN) != 0)
        {
            ioresult.hiter.id = (event.result & 0x000000FF);
            //std::cerr << "id = " << ioresult.hiter.id <<std::endl;
            ioresult.hiter.end = false;
        }
        break;

    case nvme_cmd_kv_iter_read:
        if (ioctx.buf)
        {
            ioresult.hiter.buf = ioctx.buf;
            ioresult.hiter.buflength = (event.result & 0xffff);
        }
        ioresult.hiter.end = false;
        break;
    };

    return 0;
}
