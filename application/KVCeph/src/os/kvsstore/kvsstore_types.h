//
// Created by root on 10/12/18.
//

#ifndef CEPH_KVSSTORE_TYPES_H
#define CEPH_KVSSTORE_TYPES_H

#include <unistd.h>

#include <atomic>
#include <mutex>
#include <vector>
#include <map>
#include <condition_variable>

#include <boost/intrusive/list.hpp>
#include <boost/intrusive/unordered_set.hpp>
#include <boost/intrusive/set.hpp>
#include <boost/functional/hash.hpp>
#include <boost/dynamic_bitset.hpp>

#include "include/assert.h"
#include "include/unordered_map.h"
#include "include/memory.h"
#include "common/Finisher.h"
#include "common/RWLock.h"
#include "common/WorkQueue.h"
#include "os/ObjectStore.h"
#include "os/fs/FS.h"
#include "kvsstore_ondisk.h"

#include "kadi/KADI.h"

#define KVS_OBJECT_MAX_SIZE 32768

class KvsStore;

class KvsOpSequencer;
typedef boost::intrusive_ptr<KvsOpSequencer> OpSequencerRef;

struct KvsCollection;
typedef boost::intrusive_ptr<KvsCollection> CollectionRef;

enum {
    KVS_ONODE_CREATED        = -1,
    KVS_ONODE_FETCHING       =  0,
    KVS_ONODE_DOES_NOT_EXIST =  2,
    KVS_ONODE_PREFETCHED     =  3,
    KVS_ONODE_VALID          =  4
};

static const std::map<std::string, int> builtin_omapkeys = {
        {"snapper",  2},
        {"_info", 3},
        {"_", 4},
        {"snapset", 5},
        {"0000000007.00000000000000000001",6},
        {"_epoch", 7},
        {"_infover", 8},
        {"_biginfo", 9},
        {"may_include_deletes_in_missing", 10},
        {"_fastinfo", 11}
};

/// an in-memory object
struct KvsOnode {
    MEMPOOL_CLASS_HELPERS();

    std::atomic_int nref;  ///< reference count
    KvsCollection *c;
    ghobject_t oid;
    boost::intrusive::list_member_hook<> lru_item;
    kvsstore_onode_t onode;  ///< metadata stored as value in kv store
    bool exists;              ///< true if object logically exists


    int status;

    // track txc's that have not been committed to kv store (and whose
    // effects cannot be read via the kvdb read methods)
    std::atomic<int> flushing_count = {0};
    std::mutex flush_lock;  ///< protect flush_txns
    std::condition_variable flush_cond;   ///< wait here for uncommitted txns

    KvsOnode(KvsCollection *c, const ghobject_t& o)
            : nref(0),
              c(c),
              oid(o),
              exists(false),
              status(KVS_ONODE_CREATED){
    }

    void flush();
    void get() {
        ++nref;
    }
    void put() {
        if (--nref == 0)
            delete this;
    }

    uint8_t get_omap_index(std::string &key) {
        static std::atomic<uint8_t> index = {0};
        auto it = builtin_omapkeys.find(key);
        if (it != builtin_omapkeys.end()) {
            return it->second;
        }

        // TODO: add omap entry to the map
        return 12 + (++index);
    }
};

typedef boost::intrusive_ptr<KvsOnode> OnodeRef;

static inline void intrusive_ptr_add_ref(KvsOnode *o) {
    o->get();
}
static inline void intrusive_ptr_release(KvsOnode *o) {
    o->put();
}


/// a cache (shard) of onodes and buffers
struct KvsCache {
    CephContext* cct;
    PerfCounters *logger;
    std::recursive_mutex lock;          ///< protect lru and other structures

    static KvsCache *create(CephContext* cct, PerfCounters *logger);

    KvsCache(CephContext* _cct) : cct(_cct), logger(nullptr) {}
    virtual ~KvsCache() {}

    virtual void _add_onode(OnodeRef& o, int level) = 0;
    virtual void _rm_onode(OnodeRef& o) = 0;
    virtual void _touch_onode(OnodeRef& o) = 0;

    virtual uint64_t _get_num_onodes() = 0;
    virtual uint64_t _get_buffer_bytes() = 0;

    void trim();

    void trim_all();

    virtual void _trim(uint64_t onode_max, uint64_t buffer_max) = 0;

    bool empty() {
        std::lock_guard<std::recursive_mutex> l(lock);
        return _get_num_onodes() == 0 && _get_buffer_bytes() == 0;
    }
};

/// simple LRU cache for onodes and buffers
struct KvsLRUCache : public KvsCache  {
private:
    CephContext* cct;
    typedef boost::intrusive::list<
            KvsOnode,
            boost::intrusive::member_hook<
                    KvsOnode,
                    boost::intrusive::list_member_hook<>,
                    &KvsOnode::lru_item> > onode_lru_list_t;

    onode_lru_list_t onode_lru;

public:
    KvsLRUCache(CephContext* _cct) : KvsCache(_cct), cct(_cct) {}
    uint64_t _get_num_onodes()  {
        return onode_lru.size();
    }
    void _add_onode(OnodeRef& o, int level) override {
        if (level > 0)
            onode_lru.push_front(*o);
        else
            onode_lru.push_back(*o);
    }
    void _rm_onode(OnodeRef& o) override {
        auto q = onode_lru.iterator_to(*o);
        onode_lru.erase(q);
    }

    void _touch_onode(OnodeRef& o) override;


    void _trim(uint64_t onode_max, uint64_t buffer_max) override;


    uint64_t _get_buffer_bytes() override {
        return 0;
    }
};

struct KvsOnodeSpace {
private:
    KvsCache *cache;

    /// forward lookups
    mempool::kvsstore_cache_other::unordered_map<ghobject_t,OnodeRef> onode_map;

    friend class Collection; // for split_cache()

public:
    KvsOnodeSpace(KvsCache *c) : cache(c) {}
    ~KvsOnodeSpace() {
        clear();
    }

    //OnodeRef test();


    OnodeRef add(const ghobject_t& oid, OnodeRef o);
    OnodeRef lookup(const ghobject_t& o);
    void remove(const ghobject_t& oid) {
        onode_map.erase(oid);
    }
/*
    void rename(OnodeRef& o, const ghobject_t& old_oid,
                const ghobject_t& new_oid,
                const mempool::kvsstore_cache_other::string& new_okey);
*/
    void clear();
    bool empty();

    void dump(CephContext *cct, int lvl);

    /// return true if f true for any item
    bool map_any(std::function<bool(OnodeRef)> f);
};


struct KvsCollection : public ObjectStore::CollectionImpl {
    KvsStore *store;
    KvsCache *cache;       ///< our cache shard
    coll_t cid;
    kvsstore_cnode_t cnode;
    RWLock lock;

    bool exists;

    // cache onodes on a per-collection basis to avoid lock
    // contention.
    KvsOnodeSpace onode_map;


    OnodeRef get_onode(const ghobject_t& oid, bool create);

    const coll_t &get_cid() override {
        return cid;
    }

    bool contains(const ghobject_t& oid) {
        if (cid.is_meta())
            return oid.hobj.pool == -1;
        spg_t spgid;
        if (cid.is_pg(&spgid))
            return
                    spgid.pgid.contains(cnode.bits, oid) &&
                    oid.shard_id == spgid.shard;
        return false;
    }

    KvsCollection(KvsStore *ns, KvsCache *ca, coll_t c);
};

class KvsOmapIteratorImpl : public ObjectMap::ObjectMapIteratorImpl {
    CollectionRef c;
    OnodeRef o;
    string head, tail;
    KvsStore *store;
public:
    KvsOmapIteratorImpl(CollectionRef c, OnodeRef o, KvsStore *store);
    int seek_to_first() override;
    int upper_bound(const string &after) override;
    int lower_bound(const string &to) override;
    bool valid() override;
    int next(bool validate=true) override;
    string key() override;
    bufferlist value() override;
    int status() override {
        return 0;
    }
};

struct KvsIoContext {
private:
    std::mutex lock;
    std::condition_variable cond;

public:
    std::mutex running_aio_lock;
    atomic_bool submitted = { false };
    CephContext* cct;
    void *priv;

    // value == 0 ==> delete
    std::list<std::pair<kv_key *, kv_value *> > pending_aios;    ///< not yet submitted
    std::list<std::pair<kv_key *, kv_value *> > running_aios;    ///< submitting or submitted
    std::atomic_int num_pending = {0};
    std::atomic_int num_running = {0};

    explicit KvsIoContext(CephContext* _cct) : cct(_cct)
    {}

    // no copying
    KvsIoContext(const KvsIoContext& other) = delete;
    KvsIoContext &operator=(const KvsIoContext& other) = delete;

    bool has_pending_aios() {
        return num_pending.load();
    }

    // TODO:
    void add_coll(const coll_t &cid, bufferlist &bl);
    void rm_coll(const coll_t &cid);

    void add_userdata(const ghobject_t& oid, bufferlist &bl);
    // userdata will be deleted by rm_onode

    void add_onode(const ghobject_t &oid, uint8_t index, bufferlist &bl);
    void rm_onode(const ghobject_t& oid);

    void add_omap(const ghobject_t& oid, uint8_t index, bufferlist &bl);
    void rm_omap (const ghobject_t& oid, uint8_t index);




    inline void add(kv_key *kvkey, kv_value *kvvalue) {
        std::unique_lock<std::mutex> l(lock);
        pending_aios.push_back(std::make_pair(kvkey, kvvalue));
        num_pending++;
    }

    inline void del(kv_key *kvkey) {
        std::unique_lock<std::mutex> l(lock);
        pending_aios.push_back(std::make_pair(kvkey, (kv_value*)0));
        num_pending++;
    }





};

struct KvsReadContext {
private:
    std::mutex lock;
    std::condition_variable cond;
public:
    CephContext* cct;
    //KvsStore *store;
    kv_key *key;
    kv_value *value;
    kv_result retcode;
    std::atomic_int num_running = {0};

    explicit KvsReadContext(CephContext* _cct)
    : cct(_cct), key(0), value(0)
    {
    }

    ~KvsReadContext();

    void read_onode(const ghobject_t &oid);
    void read_data(const ghobject_t &oid);
    void read_coll(const char *name, const int namelen);

    void try_read_wake();
    kv_result read_wait(bufferlist &bl);
};

struct KvsTransContext  {
    MEMPOOL_CLASS_HELPERS();

    typedef enum {
        STATE_PREPARE,
        STATE_AIO_WAIT,// submitted to kv; not yet synced
        STATE_IO_DONE,
        STATE_FINISHING,
        STATE_DONE,
    } state_t;

    state_t state = STATE_PREPARE;

    const char *get_state_name() {
        switch (state) {
            case STATE_PREPARE: return "STATE_PREPARE";
            case STATE_AIO_WAIT: return "STATE_AIO_WAIT - submitted IO are done(called by c)";
            case STATE_IO_DONE: return "STATE_IO_DONE - processing IO done events (called by cb)";
            case STATE_FINISHING: return "STATE_FINISHING - releasing resources for IO (called by cb)";
            case STATE_DONE: return "done";
        }
        return "???";
    }

    OpSequencerRef osr;
    boost::intrusive::list_member_hook<> sequencer_item;

    uint64_t bytes = 0, cost = 0;

    set<OnodeRef> onodes;     ///< these need to be updated/written

    Context *oncommit = nullptr;         ///< signal on commit
    Context *onreadable = nullptr;       ///< signal on readable
    Context *onreadable_sync = nullptr;  ///< signal on readable
    list<Context*> oncommits;  ///< more commit completions
    list<CollectionRef> removed_collections; ///< colls we removed

    KvsIoContext ioc;

    bool had_ios = false;  ///< true if we submitted IOs before our kv txn
    uint64_t seq = 0;
    CephContext* cct;
    KvsStore *store;

    explicit KvsTransContext(CephContext* _cct, KvsStore *_store, KvsOpSequencer *o)
            : osr(o), ioc(_cct), cct(_cct), store(_store)
   {
    }

    ~KvsTransContext() {

    }


    void write_onode(OnodeRef &o) {
        o->status = KVS_ONODE_VALID;
        onodes.insert(o);
    }

    void removed(OnodeRef& o) {
        onodes.erase(o);
    }

    void aio_finish(kv_io_context *op);


};


class KvsOpSequencer : public ObjectStore::Sequencer_impl {
public:
    std::mutex qlock;
    std::condition_variable qcond;
    typedef boost::intrusive::list<
            KvsTransContext,
            boost::intrusive::member_hook<
                    KvsTransContext,
                    boost::intrusive::list_member_hook<>,
                    &KvsTransContext::sequencer_item> > q_list_t;
    q_list_t q;  ///< transactions

    boost::intrusive::list_member_hook<> deferred_osr_queue_item;

    ObjectStore::Sequencer *parent;
    KvsStore *store;

    uint64_t last_seq = 0;

    std::atomic_int txc_with_unstable_io = {0};  ///< num txcs with unstable io
    std::atomic_int kv_committing_serially = {0};
    std::atomic_int kv_submitted_waiters = {0};

    std::atomic_bool registered = {true}; ///< registered in BlueStore's osr_set
    std::atomic_bool zombie = {false};    ///< owning Sequencer has gone away

    KvsOpSequencer(CephContext* cct, KvsStore *store);
    ~KvsOpSequencer() override;

    void discard() override;
    void drain();
    void flush() override;
    bool _is_all_kv_submitted();
    void drain_preceding(KvsTransContext *txc);
    bool flush_commit(Context *c) override;



    void _unregister();


    void queue_new(KvsTransContext *txc) {
        std::lock_guard<std::mutex> l(qlock);
        txc->seq = ++last_seq;
        q.push_back(*txc);
    }
};

class KvsMemPool {

public:
    static kv_key *Alloc_key(int keybuf_size = 16) {
        kv_key *key = (kv_key *)calloc(1, sizeof(kv_key));
        key->key = calloc(1, keybuf_size);
        key->length = keybuf_size;
        return key;
    }

    static kv_value *Alloc_value(int valuesize) {
        kv_value *value = (kv_value *)calloc(1, sizeof(kv_value));
        value->value = calloc(1, valuesize);
        value->length = valuesize;
        return value;
    }

    static void Release_key(kv_key *key) {
        assert(key != 0);
        free(key->key);
        free(key);
    }

    static void Release_value (kv_value *value) {
        assert(value != 0);
        free(value->value);
        free(value);
    }

};

struct __attribute__((__packed__)) kvs_coll_key
{
    uint8_t          prefix;                        //1B
    char             name[15];                      //15B
};


template<typename T>
inline std::string stringify_16B_collkey(const T& a) {
#if defined(__GNUC__) && !(defined(__clang__) || defined(__INTEL_COMPILER))
    static __thread std::ostringstream ss;
    static char padding[16] = {0};
    ss.str("");
#else
    std::ostringstream ss;
#endif
    ss << (char)GROUP_PREFIX_COLL;
    ss << a;

    const int bytes_to_write = 16 - ss.tellp();
    if (bytes_to_write > 0) ss.write(padding, bytes_to_write);

    return ss.str();
}

#endif //CEPH_KVSSTORE_TYPES_H
