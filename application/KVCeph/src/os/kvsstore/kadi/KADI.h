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
#include <stdbool.h>
#include <condition_variable>
#include <sys/eventfd.h>
#include <sys/select.h>
#include <sys/time.h>
#include "linux_nvme_ioctl.h"
//#include "kv_nvme.h"


#define KVCMD_INLINE_KEY_MAX	(16)
#define KVCMD_MAX_KEY_SIZE		(255)
#define KVCMD_MIN_KEY_SIZE		(255)
//#define DUMP_ISSUE_CMD
#define KV_SPACE
//#define ITER_CLEANUP
//#define OLD_KV_FORMAT


#define ITER_BUFSIZE 32768


class KvsTransContext;
class KvsReadContext;
class KvsSyncWriteContext;
class CephContext;
typedef uint8_t kv_key_t;
typedef uint32_t kv_value_t;
typedef int kv_result;

enum nvme_kv_store_option {
    STORE_OPTION_NOTHING = 0,
    STORE_OPTION_COMP = 1,
    STORE_OPTION_IDEMPOTENT = 2,
    STORE_OPTION_BGCOMP = 4
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
    nvme_cmd_kv_store	= 0x81,
    nvme_cmd_kv_append	= 0x83,
    nvme_cmd_kv_retrieve	= 0x90,
    nvme_cmd_kv_delete	= 0xA1,
    nvme_cmd_kv_iter_req	= 0xB1,
    nvme_cmd_kv_iter_read	= 0xB2,
    nvme_cmd_kv_exist	= 0xB3,
    nvme_cmd_kv_capacity = 0x06
};

enum {
    KV_SUCCESS = 0,
    KV_ERR_KEY_NOT_EXIST = 0x310
};

typedef struct {
    void *key;        ///< a pointer to a key
    kv_key_t length;  ///< key length in byte unit, based on KV_MAX_KEY_LENGHT
} kv_key;

typedef struct {
    void *value;                 ///< buffer address for value
    kv_value_t length;           ///< value buffer size in byte unit for input and the retuned value length for output
    kv_value_t actual_value_size;
    kv_value_t offset;           ///< offset for value
} kv_value;


typedef struct {
    int opcode;
    int retcode;

    kv_key* key = 0;
    kv_value *value = 0;

    struct {
        int id;
        bool end;
        void *buf;
        int buflength;
    } hiter;

} kv_io_context;

typedef struct {
    void (*post_fn)(kv_io_context &op, void *post_data);   ///< asynchronous notification callback (valid only for async I/O)
    void *private_data;       ///< private data address which can be used in callback (valid only for async I/O)
} kv_cb;
class KvsStore;
class KADI;
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

    typedef std::list<std::pair<kv_key *, kv_value *> >::iterator aio_iter;
    typedef struct {
        int index;
        kv_key* key = 0;
        kv_value *value = 0;
        void *buf = 0;
        int buflen = 0;

        void (*post_fn)(kv_io_context &result, void *data);
        void *post_data;

        volatile struct nvme_passthru_kv_cmd cmd;

        void call_post_fn(kv_io_context &result) {
            if (post_fn != NULL) post_fn(result, post_data);
        }
    } aio_cmd_ctx;

    KADI(CephContext *c): cct(c) { }
    ~KADI() { close(); }

private:

    int fd = -1;
    unsigned nsid;
    int space_id;

    fd_set rfds;
    struct timeval timeout;
    CephContext *cct;
    std::mutex cmdctx_lock;
    std::condition_variable cmdctx_cond;

    std::vector<aio_cmd_ctx *>   free_cmdctxs;
    std::map<int, aio_cmd_ctx *> pending_cmdctxs;

    const int qdepth = 128;
    struct nvme_aioctx aioctx;

    aio_cmd_ctx* get_cmd_ctx(kv_cb& cb);

    inline aio_cmd_ctx* get_cmdctx(int reqid) {
        std::unique_lock<std::mutex> lock (cmdctx_lock);
        auto p = pending_cmdctxs.find(reqid);
        if (p == pending_cmdctxs.end())
            return 0;
        return p->second;
    }

    void release_cmd_ctx(aio_cmd_ctx *p);
    void dump_delete_cmd(struct nvme_passthru_kv_cmd *cmd);
    void dump_retrieve_cmd(struct nvme_passthru_kv_cmd *cmd);

public:

    kv_result kv_store(kv_key *key, kv_value *value, kv_cb& cb);
    kv_result kv_retrieve(kv_key *key, kv_value *value, kv_cb& cb);
    kv_result kv_retrieve_sync(kv_key *key, kv_value *value);
    kv_result kv_retrieve_sync(kv_key *key, kv_value *value, uint64_t offset, size_t length, bufferlist &bl, bool &ispartial);
    kv_result kv_delete(kv_key *key, kv_cb& cb, int check_exist = 0);
    kv_result iter_open(kv_iter_context *iter_handle);
    kv_result iter_close(kv_iter_context *iter_handle);
    kv_result iter_read(kv_iter_context *iter_handle);
    kv_result iter_readall(kv_iter_context *iter_ctx, std::list<std::pair<void*, int> > &buflist);
    kv_result poll_completion(uint32_t &num_events, uint32_t timeout_us);
    bool exist(kv_key *key);
    bool exist(void *key, int length);
    int open(std::string &devpath);
    int close();
    int fill_ioresult(const aio_cmd_ctx &ioctx, const struct nvme_aioevent &event, kv_io_context &result);
    kv_result submit_batch(aio_iter begin, aio_iter end, void *priv, bool write );
    kv_result aio_submit(KvsTransContext *txc);
    kv_result aio_submit(KvsReadContext *txc);
    kv_result aio_submit(KvsSyncWriteContext *txc);
    kv_result sync_submit(KvsReadContext *txc);
    kv_result aio_submit_prefetch(KvsReadContext *txc);
    kv_result sync_read(kv_key *key, bufferlist &bl, int valuesize = 4096);
    kv_result get_freespace(uint64_t &bytesused, uint64_t &capacity, double &utilization);

    std::string errstr(int res) {
        if (res == KV_SUCCESS) return "SUCCESS";
        return "ERROR";
    }
    bool is_opened() { return (fd != -1); }
    void dump_cmd(struct nvme_passthru_kv_cmd *cmd);

};


#endif //CEPH_KADI_H


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
