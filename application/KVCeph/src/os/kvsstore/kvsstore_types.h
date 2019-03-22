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

#define KVS_OBJECT_MAX_SIZE 2*1024*1024
#define KVSSD_VAR_OMAP_KEY_MAX_SIZE 242

class KvsStore;
struct KvsOnodeSpace;

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

bool construct_ghobject_t(CephContext* cct, const char *key, int keylength, ghobject_t* oid);
void print_kvskey(char *s, int length, const char *header, std::string &out);
void encode_nspace_oid(const std::string &nspace, const std::string &oidkey, const std::string &oidname, std::string *out);
uint32_t get_object_group_id(const uint8_t  isonode,const int8_t shardid, const uint64_t poolid);
// OMAP Iterator helpers
void construct_omap_key(CephContext* cct, uint64_t lid, const char *name, const int name_len, kv_key *key);
bool belongs_toOmap(void *key, uint64_t lid);
void omap_iterator_init(CephContext *cct, uint64_t lid, kv_iter_context *iter_ctx);
void print_iterKeys(CephContext *cct, std::map<string, int> &keylist);
int populate_keylist(CephContext *cct, uint64_t lid, std::list<std::pair<void*, int>> &buflist, std::set<string> &keylist, KADI *db);
//


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

#define KVSSD_KEYVAR_MAX_SIZE 218
struct __attribute__((__packed__)) kvs_var_object_key
{
    uint32_t         grouphash;                    //4B
    uint8_t          group;                        //1B
    uint64_t         poolid;                       //8B 13
    int8_t           shardid;                      //1B 14
    uint32_t         bitwisekey;                   //4B 18
    uint64_t         snapid;                       //8B 26
    uint64_t         genid;                        //8B 34
    char             name[1024];                   //221B - MAX SIZE is 217. set to 1024 to avoid possible buffer overflow during encoding
};

struct __attribute__((__packed__)) kvs_journal_key
{
    uint32_t         hash;
    uint8_t          group;
    uint64_t         index;                        // 8B
    uint8_t          reserved1;
    uint8_t          reserved2;
    uint8_t          reserved3;
};

struct __attribute__((__packed__)) kvs_coll_key
{
    uint32_t         hash;
    uint8_t          group;
    char             name[250];                      //15B
};

struct __attribute__((__packed__)) kvs_omap_key_header
{
    uint8_t  hdr;
    uint64_t lid;
};

struct __attribute__((__packed__)) kvs_omap_key
{
   uint32_t	     hash;
   uint8_t       group;
   uint64_t       lid;  // unique key
   uint8_t       isheader;  // unique key
   char		     name[KVSSD_VAR_OMAP_KEY_MAX_SIZE];// 255-13B
};

struct __attribute__((__packed__)) kvs_omapheader_key
{
    uint32_t	  hash;     // 4
    uint8_t       group;    // 1
    uint64_t      lid;      // unique key       8  13 bytes
    uint8_t       reserved1;
    uint8_t       reserved2;
    uint8_t       reserved3;
};


struct __attribute__((__packed__)) kvs_sb_key
{
    uint32_t         prefix;                        //4B
    uint8_t          group;                         //1B
    char             name[11];                      //11B
};




struct __attribute__((__packed__)) journal_header {
    uint16_t isonode;
    uint16_t keylength;
    uint32_t vallength;
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
    std::mutex prefetch_lock;  ///< protect flush_txns
    std::condition_variable prefetch_cond;   ///< wait here for uncommitted txns

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



struct ReadCacheBuffer
{
    KvsOnodeSpace *space;
    bufferlist buffer;
    ghobject_t oid;
    std::atomic_int nref;  ///< reference count
    boost::intrusive::list_member_hook<> lru_item;

    ReadCacheBuffer(KvsOnodeSpace *space_, const ghobject_t& o, bufferlist* buffer_):
            space(space_), oid (o), nref(0) {
        // deep copy
        buffer = *buffer_;
    }

    ~ReadCacheBuffer() {}

    void get() {
        ++nref;
    }

    void put() {
        if (--nref == 0)
            delete this;
    }

    inline unsigned int length() {
        return buffer.length();
    }
};

typedef boost::intrusive_ptr<ReadCacheBuffer> ReadCacheBufferRef;

static inline void intrusive_ptr_add_ref(ReadCacheBuffer *o) {
    o->get();
}
static inline void intrusive_ptr_release(ReadCacheBuffer *o) {
    o->put();
}


/// a cache (shard) of onodes and buffers
struct KvsCache {
    CephContext* cct;
    PerfCounters *logger;
    std::recursive_mutex lock;          ///< protect lru and other structures
    std::recursive_mutex datalock;          ///< protect lru and other structures
    uint64_t max_readcache;
    uint64_t max_onodes;
    static KvsCache *create(CephContext* cct, PerfCounters *logger);

    KvsCache(CephContext* _cct) : cct(_cct), logger(nullptr) {}
    virtual ~KvsCache() {}

    virtual void _add_onode(OnodeRef& o, int level) = 0;
    virtual void _rm_onode(OnodeRef& o) = 0;
    virtual void _touch_onode(OnodeRef& o) = 0;
    virtual void _add_data(ReadCacheBufferRef&, int level) {}
    virtual void _rm_data(ReadCacheBufferRef&) {};
    virtual void _touch_data(ReadCacheBufferRef&) {};

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
    typedef boost::intrusive::list<
            ReadCacheBuffer,
            boost::intrusive::member_hook<
                    ReadCacheBuffer,
                    boost::intrusive::list_member_hook<>,
                    &ReadCacheBuffer::lru_item> > buffer_lru_list_t;
    onode_lru_list_t onode_lru;
    buffer_lru_list_t buffer_lru;
    uint64_t buffer_size;

public:
    KvsLRUCache(CephContext* _cct) : KvsCache(_cct), cct(_cct), buffer_size(0) {}
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

    uint64_t _get_num_data()  {
        return buffer_lru.size();
    }



    void _add_data(ReadCacheBufferRef& o, int level) override {
        if (level > 0)
            buffer_lru.push_front(*o);
        else
            buffer_lru.push_back(*o);

        buffer_size += o->length();
    }

    void _rm_data(ReadCacheBufferRef& o) override;


    uint64_t _get_buffer_bytes() override {
        return buffer_size;
    }

    void _touch_data(ReadCacheBufferRef &o) override;

    void _trim(uint64_t onode_max, uint64_t buffer_max) override;

};


struct KvsOnodeSpace {
public:
    KvsCache *cache;

    /// forward lookups
    mempool::kvsstore_cache_other::unordered_map<ghobject_t,OnodeRef> onode_map;
    mempool::kvsstore_cache_other::unordered_map<ghobject_t,ReadCacheBufferRef> data_map;

    friend class Collection; // for split_cache()

public:
    KvsOnodeSpace(KvsCache *c, KvsCache *d) : cache(c){}
    ~KvsOnodeSpace() {
        clear();
    }

    //OnodeRef test();


    OnodeRef add(const ghobject_t& oid, OnodeRef o);
    OnodeRef lookup(const ghobject_t& o);
    void remove(const ghobject_t& oid) { onode_map.erase(oid); }
    bool invalidate_data(const ghobject_t &oid);
    bool invalidate_onode(const ghobject_t &oid);
    void add_data(const ghobject_t &oid, ReadCacheBufferRef p);
    ReadCacheBufferRef lookup_data(const ghobject_t &oid);

    void remove_data(const ghobject_t& oid) { data_map.erase(oid); }


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
    std::mutex l_prefetch;
    bool exists;

    // cache onodes on a per-collection basis to avoid lock
    // contention.
    KvsOnodeSpace onode_map;

    std::unordered_map<ghobject_t, KvsOnode *> onode_prefetch_map;
    std::set<ghobject_t> lsset;

    KvsOnode *lookup_prefetch_map(const ghobject_t &oid, bool erase) {
        KvsOnode *ret = 0;
        std::unique_lock<std::mutex> lock(l_prefetch);
        auto it = onode_prefetch_map.find(oid);
        if (it != onode_prefetch_map.end()) {
                ret = it->second;
                if (erase) onode_prefetch_map.erase(it);
                return ret;
        }
        return ret;
    }

    void add_to_prefetch_map(const ghobject_t &oid, KvsOnode *on) {
        std::unique_lock<std::mutex> lock(l_prefetch);
        onode_prefetch_map[oid] = on;
    }

    void split_cache(KvsCollection *dest);
    OnodeRef get_onode(const ghobject_t& oid, bool create);
    int get_data(KvsTransContext *txc, const ghobject_t& oid, uint64_t offset, size_t length, bufferlist &bl);

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

    KvsCollection(KvsStore *ns, KvsCache *ca, KvsCache *dc, coll_t c);
};

class KvsOmapIterator : public ObjectMap::ObjectMapIteratorImpl {
    CollectionRef c;
    OnodeRef o;
    string head, tail;
    KvsStore *store;
    std::set<string>:: iterator it;
public:
    std::set<string> keylist;
    std::list<std::pair<void *, int>> buflist;

    KvsOmapIterator(CollectionRef c, OnodeRef o, KvsStore *store);
    virtual ~KvsOmapIterator() {
        for (const auto &p : buflist) {
            free(p.first);
        }
    }
    void makeready();
    bool header(bufferlist  &hdr);
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

class KvsOmapIteratorImpl: public ObjectMap::ObjectMapIteratorImpl {
    CollectionRef c;
    KvsOmapIterator *it;
public:

    KvsOmapIteratorImpl(CollectionRef c, KvsOmapIterator *it);
    virtual ~KvsOmapIteratorImpl() {
        if (it) delete it;
    }

    int seek_to_first() override;
    int upper_bound(const string &after) override;
    int lower_bound(const string &to) override;
    bool valid() override;
    int next(bool validate=true) override;
    string key() override;
    bufferlist value() override;
    int status() override { return 0; }
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
    utime_t start;

    // value == 0 ==> delete
    std::list<std::pair<kv_key *, kv_value *> > pending_aios;    ///< not yet submitted
    std::list<std::pair<kv_key *, kv_value *> > running_aios;    ///< submitting or submitted
    std::list<std::pair<kv_key *, kv_value *> > journal_entries;
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

    void add_coll(const coll_t &cid, bufferlist &bl);
    void rm_coll(const coll_t &cid);


    // oid name -> name
    void add_onode(const ghobject_t &oid, bufferlist &bl);
    void rm_onode(const ghobject_t& oid);
    void add_userdata(const ghobject_t& oid, bufferlist &bl);
    void add_userdata(const ghobject_t& oid, char *data, int length);
    void rm_data(const ghobject_t& oid);

    // omap name -> name
    void add_omap(const ghobject_t& oid, uint64_t index, std::string &strkey, bufferlist &bl);
    void rm_omap (const ghobject_t& oid, uint64_t index, std::string &strkey);
    void add_omapheader(const ghobject_t& oid, uint64_t index, bufferlist &bl);

    // userdata will be deleted by rm_onode


    inline void add(kv_key *kvkey, kv_value *kvvalue, bool journal = false) {
        std::unique_lock<std::mutex> l(lock);
        pending_aios.push_back(std::make_pair(kvkey, kvvalue));
        if (journal)
            journal_entries.push_back(std::make_pair(kvkey, kvvalue));
        num_pending++;
    }

    inline void del(kv_key *kvkey, bool journal = false) {
        std::unique_lock<std::mutex> l(lock);
        pending_aios.push_back(std::make_pair(kvkey, (kv_value*)0));
        if (journal)
            journal_entries.push_back(std::make_pair(kvkey, (kv_value*)0));
        num_pending++;
    }





};

struct KvsReadContext {
private:
    std::mutex lock;
    std::condition_variable cond;
public:
    CephContext* cct;

    const ghobject_t *oid;
    KvsOnode *onode;
    utime_t prefetch_onode_begin_time;

    kv_key *key;
    kv_value *value;
    KvsStore *store;
    kv_result retcode;
    //std::atomic_int num_running = {0};
    int num_running = 0;
    utime_t start;
    explicit KvsReadContext(CephContext* _cct)
    : cct(_cct), key(0), value(0)
    {
    }
    
    explicit KvsReadContext(CephContext* _cct, KvsStore* _store)
    : cct(_cct), key(0), value(0), store(_store)
    {
    }
    
    ~KvsReadContext();

    void set_prefetch_time(utime_t pr_time) {prefetch_onode_begin_time = pr_time;};
    utime_t get_prefetch_time() {return prefetch_onode_begin_time;}
    
    void read_sb();
    void read_onode(const ghobject_t &oid);
    void read_data(const ghobject_t &oid);
    void read_coll(const char *name, const int namelen);
    void read_journal(kvs_journal_key *key);

    void try_read_wake();
    void try_read_wake2();
    kv_result read_wait(bufferlist &bl);
    kv_result read_wait(bufferlist &bl, uint64_t offset, size_t length, bool &ispartial);
    kv_result read_wait();
    kv_result read_wait2();
};


struct KvsSyncWriteContext {
private:
    std::mutex lock;
    std::condition_variable cond;
public:
    CephContext* cct;
    kv_key *key;
    kv_value *value;
    kv_result retcode;
    std::atomic_int num_running = {0};

    explicit KvsSyncWriteContext(CephContext* _cct)
            : cct(_cct), key(0), value(0)
    {
    }

    ~KvsSyncWriteContext();

    void write_sb(bufferlist &bl);

    void write_journal(uint64_t index, std::list<std::pair<kv_key *, kv_value *> > &list);
    char *write_journal_entry(char *entry, uint64_t &lid);
    void delete_journal_key(struct kvs_journal_key* k);
    void try_write_wake();
    kv_result write_wait();
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
    map<const ghobject_t, bufferlist> tempbuffers;
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

#define KVSSTORE_POOL_VALUE_SIZE 8192

class KvsMemPool {

public:
    static kv_key *Alloc_key(int keybuf_size = 256) {
        kv_key *key = (kv_key *)calloc(1, sizeof(kv_key));
        key->key = alloc_memory(get_aligned_size(keybuf_size, 256));
        key->length = keybuf_size;
        return key;
    }

    static kv_value *Alloc_value(int valuesize = KVSSTORE_POOL_VALUE_SIZE) {
        kv_value *value = (kv_value *)calloc(1, sizeof(kv_value));
               
        value->value  = alloc_memory(get_aligned_size(valuesize, 4096)); 
        value->length = valuesize;
        return value;
    }

    static inline int get_aligned_size(const int valuesize, const int align) {
        return ((valuesize + align -1 ) / align) * align;
    }

    static inline void* alloc_memory(const int valuesize) {
        return calloc(1, valuesize);
    }

     static inline void free_memory(void *ptr) {
        free(ptr);
    }

    static void Release_key(kv_key *key) {
        assert(key != 0);
        free((void*)key->key);
        free(key);
    }

    static void Release_value (kv_value *value) {
        assert(value != 0);
        free((void*)value->value);
        free(value);
    }

};




#endif //CEPH_KVSSTORE_TYPES_H
