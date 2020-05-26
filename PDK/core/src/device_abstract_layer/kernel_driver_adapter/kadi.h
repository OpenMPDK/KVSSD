//
// Created by root on 11/8/18.
//

#ifndef CEPH_KADI_H
#define CEPH_KADI_H

#include <pthread.h>
#include <map>
#include <vector>
#include <mutex>
#include <list>
#include <atomic>
#include <stdbool.h>
#include <condition_variable>
#include <sys/eventfd.h>
#include <sys/select.h>
#include <sys/time.h>
#include "linux_nvme_ioctl.h"
#include <kvs_adi.h>
//#include "kv_nvme.h"


#define KVCMD_INLINE_KEY_MAX	(16)
#define KVCMD_MAX_KEY_SIZE		(255)
#define KVCMD_MIN_KEY_SIZE		(255)
#define ITER_LIST_ITER_TYPE_OFFSET (1)
//#define DUMP_ISSUE_CMD
#define KV_SPACE
//#define ITER_CLEANUP
//#define OLD_KV_FORMAT
#define BLOCK_SIZE 512

#define ITER_BUFSIZE 32768

class KvsTransContext;
class KvsReadContext;
class KvsSyncWriteContext;
class CephContext;
typedef uint16_t kv_key_t;
typedef uint32_t kv_value_t;
typedef int kv_result;

enum nvme_kv_store_option {
    STORE_OPTION_NOTHING = 0,
    STORE_OPTION_COMP = 1,
    STORE_OPTION_IDEMPOTENT = 2,
    STORE_OPTION_BGCOMP = 4,
    STORE_OPTION_UPDATE_ONLY = 8,
};

enum nvme_kv_retrieve_option {
    RETRIEVE_OPTION_NOTHING = 0,
    RETRIEVE_OPTION_DECOMP = 1,
    RETRIEVE_OPTION_ONLY_VALSIZE = 2
};

enum nvme_kv_delete_option {
    DELETE_OPTION_NOTHING = 0,
    DELETE_OPTION_CHECK_KEY_EXIST = 1
};

enum nvme_kv_iter_req_option {
    ITER_OPTION_NOTHING = 0x0,
    ITER_OPTION_OPEN = 0x01,
    ITER_OPTION_CLOSE = 0x02,
    ITER_OPTION_KEY_ONLY = 0x04,
    ITER_OPTION_KEY_VALUE = 0x08,
    ITER_OPTION_DEL_KEY_VALUE = 0x10,
};

enum nvme_kv_opcode {
    nvme_cmd_admin_get_log_page	= 0x02,
    nvme_cmd_admin_identify	= 0x06,
    nvme_cmd_kv_store	= 0x81,
    nvme_cmd_kv_append	= 0x83,
    nvme_cmd_kv_retrieve	= 0x90,
    nvme_cmd_kv_delete	= 0xA1,
    nvme_cmd_kv_iter_req	= 0xB1,
    nvme_cmd_kv_iter_read	= 0xB2,
    nvme_cmd_kv_exist	= 0xB3,
    nvme_cmd_kv_capacity = 0x06
};


/*
typedef struct {
    int opcode;
    int retcode;
    void *private_data;
    kv_key* key = 0;
    kv_value *value = 0;

    struct {
        int id;
        bool end;
        void *buf;
        int buflength;
    } hiter;

    struct {
        // holds information for kv_exist()
        // the same buffer from user input, not allocated internally
        uint8_t *buffer;
        uint32_t buffer_size;
        uint32_t buffer_count;

        // holds output iterator handle after calling kv_open_iterator()
        // Please call kv_close_iterator() to release it after use.
        kv_iterator_handle hiter;
    } result;

} kadi_io_context;
*/

class KvsStore;
class KADI;


class KDThread {
  pthread_t thread_id;
  std::atomic_bool stop_;
  
  KADI *dev;
public:
  std::atomic_bool started;

  KDThread(KADI *dev_):stop_(false), dev(dev_),started(false) { }

  void start() {
    int ret = pthread_create(&thread_id, NULL, _entry_func, (void*)this);
    if (ret != 0) {
      fprintf(stderr, "failed to create a interrupt thread\n");
      return;
    }
    started = true;
  }

  void stop() {
      stop_ = true;
      join(0);
  }

  void *entry();
  static void *_entry_func(void *arg);
  int join(void **prval)
  {
    if (!started) return 0;
    if (thread_id == 0) {
      return -EINVAL;
    }

    int status = pthread_join(thread_id, prval);
    if (status != 0) {
      fprintf(stderr, "interrupt thread: failed to join\n");
    }
    started = false;
    thread_id = 0;
    return status;
  }
};


typedef struct
{
    unsigned char handle;
    unsigned int prefix;
    unsigned int bitmask;
    void *buf;
    int buflen;
    int byteswritten;
    int bufoffset;
    bool end;
} kv_iter_context;

class iterbuf_reader {
    CephContext *cct;
    void *buf;
    int bufoffset;
    int byteswritten;

    int numkeys;
    KADI *db;
    KvsStore *store;
public:
    //iterbuf_reader(CephContext *c, kv_iter_context *ctx_);
    iterbuf_reader(CephContext *c, void *buf_, int length_, KADI *db, KvsStore *store_ = 0);

    bool hasnext() { return byteswritten - bufoffset > 0; }

    bool nextkey(void **key, int *length);
};


class KADI {
public:
    uint64_t capacity;
    typedef std::list<std::pair<kv_key *, kv_value *> >::iterator aio_iter;
    typedef struct {
        unsigned int index;
        kv_key* key = 0;
        kv_value *value = 0;
        void *buf = 0;
        int buflen = 0;

        void (*post_fn)(kv_io_context *result);
        void *post_data;

        volatile struct nvme_passthru_kv_cmd cmd;

        void call_post_fn(kv_io_context &result) {
            if (post_fn != NULL) post_fn(&result);
        }
    } aio_cmd_ctx;

    interrupt_handler_t int_handler;
    KDThread cb_thread;
    KADI(int queuedepth_): capacity(0),cb_thread(this), qdepth(queuedepth_) { int_handler.handler = 0; }
    ~KADI() { close(); }

    int start_cbthread();

private:

    int fd = -1;
    unsigned nsid;
    int space_id;

    fd_set rfds;
    struct timeval timeout;
    std::mutex cmdctx_lock;
    std::condition_variable cmdctx_cond;

    std::vector<aio_cmd_ctx *>   free_cmdctxs;
    std::map<unsigned, aio_cmd_ctx *> pending_cmdctxs;

    int qdepth;
    
    struct nvme_aioctx aioctx;

    aio_cmd_ctx *get_cmd_ctx(const kv_postprocess_function *cb);
    
    inline aio_cmd_ctx* get_cmdctx(int reqid) {
        std::unique_lock<std::mutex> lock (cmdctx_lock);
        auto p = pending_cmdctxs.find(reqid);
        if (p == pending_cmdctxs.end())
            return 0;
        return p->second;
    }

    void release_cmd_ctx(aio_cmd_ctx *p);

public:

    uint32_t  get_dev_waf();
    kv_result kv_store(uint8_t ks_id, kv_key *key, kv_value *value, nvme_kv_store_option option, const kv_postprocess_function* cb);
    kv_result kv_retrieve(uint8_t ks_id, kv_key *key, kv_value *value, const kv_postprocess_function* cb);
    kv_result kv_retrieve_sync(uint8_t ks_id, kv_key *key, kv_value *value);
    kv_result kv_delete(uint8_t ks_id, kv_key *key, const kv_postprocess_function* cb, int check_exist = 0);
    kv_result iter_open(uint8_t ks_id, kv_iter_context *iter_handle, nvme_kv_iter_req_option option);
    kv_result iter_close(kv_iter_context *iter_handle);
    kv_result iter_read(kv_iter_context *iter_handle);
    kv_result iter_read_async(kv_iter_context *iter_handle, const kv_postprocess_function *cb);
    kv_result iter_readall(uint8_t ks_id, kv_iter_context *iter_ctx,
      nvme_kv_iter_req_option option, std::list<std::pair<void*, int> > &buflist);
    kv_result iter_list(kv_iterator *iter_list, uint32_t *count);
    kv_result poll_completion(uint32_t &num_events, uint32_t timeout_us);
    bool exist(uint8_t ks_id, kv_key *key, const kv_postprocess_function *cb = 0);
    bool exist(uint8_t ks_id, void *key, int length, const kv_postprocess_function *cb = 0);
    int open(std::string &devpath);
    int close();
    int fill_ioresult(const aio_cmd_ctx &ioctx, const struct nvme_aioevent &event, kv_io_context &result);
    kv_result update_capacity();
    kv_result get_freespace(uint64_t &bytesused, uint64_t &capacity, double &utilization);

    std::string errstr(int res) {
        if (res == KV_SUCCESS) return "SUCCESS";
        return "ERROR";
    }
    bool is_opened() { return (fd != -1); }
    void dump_cmd(struct nvme_passthru_kv_cmd *cmd);

};


#define	KADI_SUCCESS		0
#define KADI_ERR_ALIGNMENT	(-1)
#define KADI_ERR_CAPAPCITY	(-2)
#define KADI_ERR_CLOSE	(-3)
#define KADI_ERR_CONT_EXIST	(-4)
#define KADI_ERR_CONT_NAME	(-5)
#define KADI_ERR_CONT_NOT_EXIST	(-6)
#define KADI_ERR_DEVICE_NOT_EXIST (-7)
#define KADI_ERR_GROUP	(-8)
#define KADI_ERR_INDEX	(-9)
#define KADI_ERR_IO	(-10)
#define KADI_ERR_KEY	(-11)
#define KADI_ERR_KEY_TYPE	(-12)
#define KADI_ERR_MEMORY	(-13)
#define KADI_ERR_NULL_INPUT	(-14)
#define KADI_ERR_OFFSET	(-15)
#define KADI_ERR_OPEN	(-16)
#define KADI_ERR_OPTION_NOT_SUPPORTED	(-17)
#define KADI_ERR_PERMISSION	(-18)
#define KADI_ERR_SPACE	(-19)
#define KADI_ERR_TIMEOUT	(-20)
#define KADI_ERR_TUPLE_EXIST	(-21)
#define KADI_ERR_TUPLE_NOT_EXIST	(-22)
#define KADI_ERR_VALUE	(-23)

#endif


#if 0
/* test */
    int nvme_kv_retrieve2(int space_id, int fd, unsigned int nsid,
                                const char *key, int key_len,
                                const char *value, int *value_len,
                                int offset, enum nvme_kv_retrieve_option option);
    int nvme_kv_iterate_req(int space_id, int fd, unsigned int nsid,
                                  unsigned char *iter_handle,
                                  unsigned iter_mask, unsigned iter_value,
                            unsigned int  option);

    int nvme_kv_iterate_read(int space_id, int fd, unsigned int nsid,
                             unsigned char iter_handle,
                             const char *result, int* result_len);
#endif
