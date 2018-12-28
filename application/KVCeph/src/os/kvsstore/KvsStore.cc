//
// Created by root on 10/12/18.
//
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <bitset>
#include "KvsStore.h"

#include "osd/osd_types.h"
#include "os/kv.h"
#include "include/compat.h"
#include "include/stringify.h"
#include "common/errno.h"
#include "common/safe_io.h"
#include "common/Formatter.h"
#include "common/EventTrace.h"
#include "common/stack_trace.h"
#include "kvs_debug.h"


#define dout_context cct
#define dout_subsys ceph_subsys_kvs

#undef dout_prefix
#define dout_prefix *_dout << "[kvs] "

#define NOTSUPPORTED_EXIT do { std::string msg = std::string(__func__ ) + " is not implemented yet";  /* BACKTRACE(msg); */ derr << msg << dendl; return 0; } while (0)
#define NOTSUPPORTED do { std::string msg = std::string(__func__ ) + " is not implemented yet";  /*BACKTRACE(msg); */ derr << msg << dendl;  } while (0)


MEMPOOL_DEFINE_OBJECT_FACTORY(KvsOnode, kvsstore_onode, kvsstore_cache_onode);
MEMPOOL_DEFINE_OBJECT_FACTORY(KvsTransContext, kvsstore_transcontext, kvsstore_txc);

#undef dout_prefix
#define dout_prefix *_dout << "[kvs-collection] " << cid << " " << this << ") "

KvsCollection::KvsCollection(KvsStore *ns, KvsCache *c, coll_t cid)
        : store(ns),
          cache(c),
          cid(cid),
          lock("KvsStore::Collection::lock", true, false),
          exists(true),
          onode_map(c)
{
}

#undef dout_prefix
#define dout_prefix *_dout << "[kvs] "



OnodeRef KvsCollection::get_onode(
        const ghobject_t& oid,
        bool create)
{
    assert(create ? lock.is_wlocked() : lock.is_locked());
    //lderr(store->cct) << __func__ << " oid " << oid << dendl;

    spg_t pgid;
    if (cid.is_pg(&pgid)) {
        if (!oid.match(cnode.bits, pgid.ps())) {
            lderr(store->cct) << __func__ << " oid " << oid << " not part of "
                              << pgid << " bits " << cnode.bits << dendl;
            ceph_abort();
        }
    }

    OnodeRef o = onode_map.lookup(oid);
    if (o)
        return o;
    bufferlist v;
    KvsReadContext ctx(store->cct);;
    ctx.read_onode(oid);
    store->db.aio_submit(&ctx);

    PRINTRKEY_CCT(store->cct, ctx.key);

    kv_result ret = ctx.read_wait(v);

    KvsOnode *on;
    if (v.length() == 0) {
        if (ret != KV_ERR_KEY_NOT_EXIST) {
            lderr(store->cct) << "v.length == 0: " << dendl;
            lderr(store->cct) << "  ret = " << store->db.errstr(ret) << dendl;
            lderr(store->cct) << "  value->length = " << ctx.value->length << dendl;
            lderr(store->cct) << "  value->actual length = " << ctx.value->actual_value_size << dendl;
        }
        assert(ret == KV_ERR_KEY_NOT_EXIST);
        if (!create) {
            return OnodeRef();
        }

        // new object, new onode
        on = new KvsOnode(this, oid);
    } else {
        assert(ret == KV_SUCCESS);

        // load
        on = new KvsOnode(this, oid);
        on->exists = true;
        bufferptr::iterator p = v.front().begin_deep();
        on->onode.decode(p);
        for (auto& i : on->onode.attrs) {
            i.second.reassign_to_mempool(mempool::mempool_kvsstore_cache_other);
        }
    }
    o.reset(on);
    return onode_map.add(oid, o);
}

void *KvsStore::MempoolThread::entry()
{
    FTRACE2
    Mutex::Locker l(lock);
    while (!stop) {

        for (auto i : store->cache_shards) {
            i->trim();
        }

        utime_t wait;
        wait += 0.2;
        cond.WaitInterval(lock, wait);
    }
    stop = false;
    return NULL;
}



KvsStore::KvsStore(CephContext *cct, const std::string& path)
        :ObjectStore(cct, path), db(cct), kv_callback_thread (this), kv_finalize_thread (this),mempool_thread(this)

{
    FTRACE
    m_finisher_num = 1;

    // perf counter
    PerfCountersBuilder b(cct, "KvsStore", l_kvsstore_first, l_kvsstore_last);
    b.add_u64_counter(l_kvsstore_read_lat, "read latency", "read latency");
    b.add_u64_counter(l_kvsstore_queue_to_submit_lat, "queue_to_submit_lat", "queue_to_submit_lat");
    b.add_u64_counter(l_kvsstore_submit_to_complete_lat, "submit_to_complete_lat", "submit_to_complete_lat");
    b.add_u64_counter(l_kvsstore_onode_read_miss_lat, "onode_read_miss_lat", "onode_read_miss_lat");
    b.add_u64_counter(l_kvsstore_onode_hit, "# of onode_hits", "# of onode_hits");
    b.add_u64_counter(l_kvsstore_onode_miss, "# of onode_misses", "\"# of onode_misses");
    logger = b.create_perf_counters();
    cct->get_perfcounters_collection()->add(logger);

    // create onode LRU cache
    set_cache_shards(1);
}

KvsStore::~KvsStore()
{
    FTRACE
    for (auto f : finishers) {
        delete f;
    }
    finishers.clear();

    cct->get_perfcounters_collection()->remove(logger);
    delete logger;

    assert(!mounted);

    assert(fsid_fd < 0);
    assert(path_fd < 0);
    for (auto i : cache_shards) {
        delete i;
    }
    cache_shards.clear();
}

void KvsStore::set_cache_shards(unsigned num) {
    size_t old = cache_shards.size();
    assert(num >= old);
    cache_shards.resize(num);
    for (unsigned i = old; i < num; ++i) {
        cache_shards[i] = KvsCache::create(cct, logger);
    }

}

/// -------------------
///  MOUNT
/// -------------------

int KvsStore::mount()
{
    FTRACE

    int r = _open_path();
    if (r < 0)
        return r;
    r = _open_fsid(false);
    if (r < 0)
        goto out_path;

    r = _read_fsid(&fsid);
    if (r < 0)
        goto out_fsid;

    r = _lock_fsid();
    if (r < 0)
        goto out_fsid;

    r = _open_db(false);
    if (r < 0)
        goto out_fsid;

    r = _open_collections();
    if (r < 0)
        goto out_db;

    mempool_thread.init();

    mounted = true;
    return 0;

    out_db:
    _close_db();
    out_fsid:
    _close_fsid();
    out_path:
    _close_path();
    return r;
}

int KvsStore::umount()
{
    FTRACE

    assert(mounted);

    _osr_drain_all();
    _osr_unregister_all();

    mounted = false;

    mempool_thread.shutdown();

    dout(20) << __func__ << " stopping kv thread" << dendl;
    _close_db();
    _reap_collections();
    _flush_cache();
    dout(20) << __func__ << " closing" << dendl;

    _close_fsid();
    _close_path();

    return 0;
}


void KvsStore::_osr_drain_all()
{
    dout(10) << __func__ << dendl;

    set<OpSequencerRef> s;
    {
        std::lock_guard<std::mutex> l(osr_lock);
        s = osr_set;
    }

    for (auto osr : s) {
        dout(20) << __func__ << " drain " << osr << dendl;
        osr->drain();
    }

}


void KvsStore::_osr_unregister_all()
{
    set<OpSequencerRef> s;
    {
        std::lock_guard<std::mutex> l(osr_lock);
        s = osr_set;
    }
    dout(10) << __func__ << " " << s << dendl;
    for (auto osr : s) {
        osr->_unregister();

        if (!osr->zombie) {
            // break link from Sequencer to us so that this OpSequencer
            // instance can die with this mount/umount cycle.  note that
            // we assume umount() will not race against ~Sequencer.
            assert(osr->parent);
            osr->parent->p.reset();
        }
    }
    // nobody should be creating sequencers during umount either.
    {
        std::lock_guard<std::mutex> l(osr_lock);
        assert(osr_set.empty());
    }
}

///  -----------------------------------------------
///  I/O Functions
///  -----------------------------------------------



int KvsStore::_open_collections()
{
    static int ITER_BUFSIZE = 32768;
    FTRACE
    kv_result ret = 0;
    void *key;
    int length;

    std::list<std::pair<void*, int> > buflist;
    std::list<std::pair<void*, int> > keylist;

    kv_iter_context iter_ctx;
    iter_ctx.prefix  = GROUP_PREFIX_COLL;
    iter_ctx.bitmask = 0x000000FF;  
    iter_ctx.buflen  = ITER_BUFSIZE;
    // check if there's any opened iterators
    for (int i = 0; i < 3; i++) {
        iter_ctx.handle = i;
        db.iter_close(&iter_ctx);
    }

    // read collections
    ret = db.iter_readall(&iter_ctx, buflist);
    if (ret != 0) return ret;

    // parse the key buffers
    for (const auto &p : buflist) {
        iterbuf_reader reader(cct, p.first, p.second);

        while  (reader.nextkey(&key, &length)) {
            if (length > 255) break;

            keylist.push_back(std::make_pair((char*) key, length));
        }
    }

    if (keylist.size() == 0) {  ret = 0;  goto release; }

    // load the keys
    for (const auto &p : keylist) {

        kv_key iterkey;
        iterkey.key = p.first;
        iterkey.length = p.second;

        bufferlist bl;
        kv_result res = db.sync_read(&iterkey, bl, ITER_BUFSIZE);
        if(res == 0 && bl.length() > 0) {
            coll_t cid;
            struct kvs_coll_key *collkey = (struct kvs_coll_key *)iterkey.key;

            std::string name(collkey->name);
            if (cid.parse(name)) {

                CollectionRef c(new KvsCollection(this, cache_shards[0], cid));

                bufferlist::iterator p = bl.begin();
                try {
                    ::decode(c->cnode, p);

                } catch (buffer::error& e) {
                    derr << __func__ << " failed to decode cnode" << dendl;
                    return -EIO;
                }

                coll_map[cid] = c;
                ret = 0;
            }
        }
    }
release:
    for (const auto &p : buflist) {
        free(p.first);
    }

    return ret;
}


int KvsStore::mkfs()
{
    // BACKTRACE
    FTRACE
    //dout(0) << __func__ << " path " << path << dendl;
    int r;
    uuid_d old_fsid;
    r = _open_path();
    if (r < 0)
        return r;
    r = _open_fsid(true);
    if (r < 0)
        goto out_path_fd;

    r = _lock_fsid();
    if (r < 0)
        goto out_close_fsid;

    r = _read_fsid(&old_fsid);
    if (r < 0 || old_fsid.is_zero()) {
        if (fsid.is_zero()) {
            fsid.generate_random();
        } else {
        }
        // we'll write it last.
    } else {
        if (!fsid.is_zero() && fsid != old_fsid) {
            r = -EINVAL;
            goto out_close_fsid;
        }
        fsid = old_fsid;
        goto out_close_fsid;
    }

    r = _open_db(true);
    if (r < 0)
        goto out_close_fsid;

    r = write_meta("type", "kvsstore");
    if (r < 0)
        goto out_close_db;

    // indicate mkfs completion/success by writing the fsid file
    r = _write_fsid();
    if (r == 0)
        dout(10) << __func__ << " success" << dendl;
    else
        derr << __func__ << " error writing fsid: " << cpp_strerror(r) << dendl;

    out_close_db:
    _close_db();
    out_close_fsid:
    _close_fsid();
    out_path_fd:
    _close_path();
    //derr << "done: r = " << r << dendl;
    return r;
}


int KvsStore::statfs(struct store_statfs_t *buf)
{
    FTRACE
    buf->reset();
    buf->total = 1099511627776L;
    buf->available = 1009511627776L;
    return 0;
}

CollectionRef KvsStore::_get_collection(const coll_t& cid) {
    RWLock::RLocker l(coll_lock);
    ceph::unordered_map<coll_t, CollectionRef>::iterator cp = coll_map.find(cid);
    if (cp == coll_map.end()) {
        return CollectionRef();
    }

    return cp->second;
}



ObjectStore::CollectionHandle KvsStore::open_collection(const coll_t& cid)
{
    return _get_collection(cid);
}

bool KvsStore::exists(const coll_t& cid, const ghobject_t& oid)
{
    FTRACE
    CollectionHandle c = _get_collection(cid);
    if (!c)
        return false;
    return exists(c, oid);
}

bool KvsStore::exists(CollectionHandle &c_, const ghobject_t& oid)
{
    FTRACE
    KvsCollection *c = static_cast<KvsCollection *>(c_.get());
    dout(10) << __func__ << " " << c->cid << " " << oid << dendl;
    if (!c->exists)
        return false;

    bool r = true;

    {
        RWLock::RLocker l(c->lock);
        OnodeRef o = c->get_onode(oid, false);
        if (!o || !o->exists)
            r = false;
    }

    return r;
}


int KvsStore::stat(
        const coll_t& cid,
        const ghobject_t& oid,
        struct stat *st,
        bool allow_eio)
{
    FTRACE
    CollectionHandle c = _get_collection(cid);
    if (!c)
        return -ENOENT;
    return stat(c, oid, st, allow_eio);
}

int KvsStore::stat(
        CollectionHandle &c_,
        const ghobject_t& oid,
        struct stat *st,
        bool allow_eio)
{
    FTRACE
    KvsCollection *c = static_cast<KvsCollection *>(c_.get());
    if (!c->exists)
        return -ENOENT;
    dout(10) << __func__ << " " << c->get_cid() << " " << oid << dendl;

    {
        RWLock::RLocker l(c->lock);
        OnodeRef o = c->get_onode(oid, false);
        if (!o || !o->exists)
            return -ENOENT;
        st->st_size = 8192;
        st->st_blksize = 4096;
        st->st_blocks = (st->st_size + st->st_blksize - 1) / st->st_blksize;
        st->st_nlink = 1;
    }


    return 0;
}

int  KvsStore::set_collection_opts( const coll_t& cid, const pool_opts_t& opts)
{
    FTRACE
    CollectionHandle ch = _get_collection(cid);
    if (!ch)
        return -ENOENT;
    KvsCollection *c = static_cast<KvsCollection *>(ch.get());
    if (!c->exists)
        return -ENOENT;
    return 0;
}


int KvsStore::read(
        const coll_t& cid,
        const ghobject_t& oid,
        uint64_t offset,
        size_t length,
        bufferlist& bl,
        uint32_t op_flags)
{
    FTRACE
    CollectionHandle c = _get_collection(cid);
    if (!c)
        return -ENOENT;
    return read(c, oid, offset, length, bl, op_flags);
}

int KvsStore::read(
        CollectionHandle &c_,
        const ghobject_t& oid,
        uint64_t offset,
        size_t length,
        bufferlist& bl,
        uint32_t op_flags)
{
    FTRACE

    KvsCollection *c = static_cast<KvsCollection *>(c_.get());
    const coll_t &cid = c->get_cid();
    dout(15) << __func__ << " " << cid << " " << oid
             << " 0x" << std::hex << offset << "~" << length << std::dec
             << dendl;
    if (!c->exists)
        return -ENOENT;

    bl.clear();
    {
        RWLock::RLocker l(c->lock);
        OnodeRef o = c->get_onode(oid, false);
        if (!o || !o->exists) {
            return -ENOENT;
        }

        if (offset == length && offset == 0)
            length = o->onode.size;

        KvsReadContext ctx(cct);
        ctx.read_data(oid);
        this->db.aio_submit(&ctx);

        PRINTRKEY(ctx.key);

        kv_result ret = ctx.read_wait(bl);

        if (ret == KV_ERR_KEY_NOT_EXIST)
            return -ENOENT;

        if (ret != KV_SUCCESS) {
            return -EIO;
        }
        return 0;
    }
}



bool KvsStore::test_mount_in_use()
{
    FTRACE
    // most error conditions mean the mount is not in use (e.g., because
    // it doesn't exist).  only if we fail to lock do we conclude it is
    // in use.
    bool ret = false;
    int r = _open_path();
    if (r < 0)
        return false;
    r = _open_fsid(false);
    if (r < 0)
        goto out_path;
    r = _lock_fsid();
    if (r < 0)
        ret = true; // if we can't lock, it is in use
    _close_fsid();
    out_path:
    _close_path();
    return ret;
}

int KvsStore::fiemap(const coll_t& cid, const ghobject_t& oid, uint64_t offset, size_t len, bufferlist& bl)
{
    FTRACE
    NOTSUPPORTED_EXIT;
}

int KvsStore::fiemap(CollectionHandle &c, const ghobject_t& oid,uint64_t offset, size_t len, bufferlist& bl)
{
    FTRACE
    NOTSUPPORTED_EXIT;
}

int KvsStore::fiemap(const coll_t& cid, const ghobject_t& oid, uint64_t offset, size_t len, map<uint64_t, uint64_t>& destmap)
{
    FTRACE
    NOTSUPPORTED_EXIT;
}

int KvsStore::fiemap(CollectionHandle &c, const ghobject_t& oid, uint64_t offset, size_t len, map<uint64_t, uint64_t>& destmap)
{
    FTRACE
    NOTSUPPORTED_EXIT;
}


int KvsStore::getattr(
        const coll_t& cid,
        const ghobject_t& oid,
        const char *name,
        bufferptr& value)
{
    FTRACE
    CollectionHandle c = _get_collection(cid);
    if (!c)
        return -ENOENT;
    return getattr(c, oid, name, value);
}

int KvsStore::getattr(
        CollectionHandle &c_,
        const ghobject_t& oid,
        const char *name,
        bufferptr& value)
{
    FTRACE
    KvsCollection *c = static_cast<KvsCollection *>(c_.get());
    dout(15) << __func__ << " " << c->cid << " " << oid << " " << name << dendl;
    if (!c->exists)
        return -ENOENT;

    int r;
    {
        RWLock::RLocker l(c->lock);
        mempool::kvsstore_cache_other::string k(name);

        OnodeRef o = c->get_onode(oid, false);
        if (!o || !o->exists) {
            r = -ENOENT;
            goto out;
        }

        int attrid = o->onode.get_attr_id(k.c_str(), false);

        if (attrid == -1) {
            r = -ENODATA;
            goto out;
        }
        value = o->onode.attrs[attrid];
        r = 0;
    }
out:
    return r;
}


int KvsStore::getattrs(
        const coll_t& cid,
        const ghobject_t& oid,
        map<string,bufferptr>& aset)
{
    FTRACE
    CollectionHandle c = _get_collection(cid);
    if (!c)
        return -ENOENT;
    return getattrs(c, oid, aset);
}

int KvsStore::getattrs(
        CollectionHandle &c_,
        const ghobject_t& oid,
        map<string,bufferptr>& aset)
{
    FTRACE
    KvsCollection *c = static_cast<KvsCollection *>(c_.get());
    dout(15) << __func__ << " " << c->cid << " " << oid << dendl;
    if (!c->exists)
        return -ENOENT;

    int r;
    {
        RWLock::RLocker l(c->lock);

        OnodeRef o = c->get_onode(oid, false);
        if (!o || !o->exists) {
            r = -ENOENT;
            goto out;
        }
        for (auto& i : o->onode.attr_names) {

            int attrid = o->onode.get_attr_id(i.first.c_str(), false);
            if (attrid == -1) continue;
            aset.emplace(i.first.c_str(), o->onode.attrs[attrid]);
        }
        r = 0;
    }

out:
    return r;
}


int KvsStore::list_collections(vector<coll_t>& ls)
{
    FTRACE
    RWLock::RLocker l(coll_lock);
    for (ceph::unordered_map<coll_t, CollectionRef>::iterator p = coll_map.begin();
         p != coll_map.end();
         ++p)
        ls.push_back(p->first);
    return 0;
}

bool KvsStore::collection_exists(const coll_t& c)
{
    FTRACE
    RWLock::RLocker l(coll_lock);
    return coll_map.count(c);
}

int KvsStore::collection_empty(const coll_t& cid, bool *empty)
{
    FTRACE
    dout(15) << __func__ << " " << cid << dendl;
    vector<ghobject_t> ls;
    ghobject_t next;
    int r = collection_list(cid, ghobject_t(), ghobject_t::get_max(), 1,
                            &ls, &next);
    if (r < 0) {
        derr << __func__ << " collection_list returned: " << cpp_strerror(r)
             << dendl;
        return r;
    }
    *empty = ls.empty();
    dout(10) << __func__ << " " << cid << " = " << (int)(*empty) << dendl;
    return 0;
}

int KvsStore::collection_bits(const coll_t& cid)
{
    FTRACE
    dout(15) << __func__ << " " << cid << dendl;
    CollectionRef c = _get_collection(cid);
    if (!c)
        return -ENOENT;
    RWLock::RLocker l(c->lock);
    dout(10) << __func__ << " " << cid << " = " << c->cnode.bits << dendl;
    return c->cnode.bits;
}

int KvsStore::collection_list(
        const coll_t& cid, const ghobject_t& start, const ghobject_t& end, int max,
        vector<ghobject_t> *ls, ghobject_t *pnext)
{
    FTRACE
    CollectionHandle c = _get_collection(cid);
    if (!c)
        return -ENOENT;
    return collection_list(c, start, end, max, ls, pnext);
}

int KvsStore::collection_list(
        CollectionHandle &c_, const ghobject_t& start, const ghobject_t& end, int max,
        vector<ghobject_t> *ls, ghobject_t *pnext)
{
    FTRACE
    KvsCollection *c = static_cast<KvsCollection *>(c_.get());
    dout(15) << __func__ << " " << c->cid
             << " start " << start << " end " << end << " max " << max << dendl;
    int r;
    {
        RWLock::RLocker l(c->lock);
        r = _collection_list(c, start, end, max, ls, pnext);
    }

    dout(10) << __func__ << " " << c->cid
             << " start " << start << " end " << end << " max " << max
             << " = " << r << ", ls.size() = " << ls->size()
             << ", next = " << (pnext ? *pnext : ghobject_t())  << dendl;
    return r;
}

int KvsStore::_collection_list(
        KvsCollection *c, const ghobject_t& start, const ghobject_t& end, int max,
        vector<ghobject_t> *ls, ghobject_t *pnext)
{
    FTRACE
    if (!c->exists)
        return -ENOENT;

    // TODO:: read from KVSSD
    return 0;
}

int KvsStore::omap_get(
        const coll_t& cid,                ///< [in] Collection containing oid
        const ghobject_t &oid,   ///< [in] Object containing omap
        bufferlist *header,      ///< [out] omap header
        map<string, bufferlist> *out /// < [out] Key to value map
)
{
    FTRACE
    CollectionHandle c = _get_collection(cid);
    if (!c)
        return -ENOENT;
    return omap_get(c, oid, header, out);
}

int KvsStore::omap_get(
        CollectionHandle &c_,    ///< [in] Collection containing oid
        const ghobject_t &oid,   ///< [in] Object containing omap
        bufferlist *header,      ///< [out] omap header
        map<string, bufferlist> *out /// < [out] Key to value map
)
{
    FTRACE
    KvsCollection *c = static_cast<KvsCollection *>(c_.get());
    dout(15) << __func__ << " " << c->get_cid() << " oid " << oid << dendl;
    if (!c->exists)
        return -ENOENT;
    RWLock::RLocker l(c->lock);
    int r = 0;
    OnodeRef o = c->get_onode(oid, false);
    if (!o || !o->exists) {
        r = -ENOENT;
        goto out;
    }
    if (!o->onode.has_omap())
        goto out;
    o->flush();
    {
        // TODO: use iterator
        /*
        KeyValueDB::Iterator it = db->get_iterator(PREFIX_OMAP);
        string head, tail;
        get_omap_header(o->onode.nid, &head);
        get_omap_tail(o->onode.nid, &tail);
        it->lower_bound(head);
        while (it->valid()) {
            if (it->key() == head) {
                dout(30) << __func__ << "  got header" << dendl;
                *header = it->value();
            } else if (it->key() >= tail) {
                dout(30) << __func__ << "  reached tail" << dendl;
                break;
            } else {
                string user_key;
                decode_omap_key(it->key(), &user_key);
                dout(30) << __func__ << "  got " << pretty_binary_string(it->key())
                         << " -> " << user_key << dendl;
                (*out)[user_key] = it->value();
            }
            it->next();
        }
        */
    }
    out:

    return r;
}

int KvsStore::omap_get_header(
        const coll_t& cid,                ///< [in] Collection containing oid
        const ghobject_t &oid,   ///< [in] Object containing omap
        bufferlist *header,      ///< [out] omap header
        bool allow_eio ///< [in] don't assert on eio
)
{
    FTRACE
    CollectionHandle c = _get_collection(cid);
    if (!c)
        return -ENOENT;
    return omap_get_header(c, oid, header, allow_eio);
}

int KvsStore::omap_get_header(
        CollectionHandle &c_,                ///< [in] Collection containing oid
        const ghobject_t &oid,   ///< [in] Object containing omap
        bufferlist *header,      ///< [out] omap header
        bool allow_eio ///< [in] don't assert on eio
)
{
    FTRACE
    KvsCollection *c = static_cast<KvsCollection *>(c_.get());
    dout(15) << __func__ << " " << c->get_cid() << " oid " << oid << dendl;
    if (!c->exists)
        return -ENOENT;
    RWLock::RLocker l(c->lock);
    int r = 0;
    OnodeRef o = c->get_onode(oid, false);
    if (!o || !o->exists) {
        r = -ENOENT;
        goto out;
    }
    if (!o->onode.has_omap())
        goto out;
    o->flush();

    *header = bufferlist();
out:
    return r;
}

int KvsStore::omap_get_keys(
        const coll_t& cid,              ///< [in] Collection containing oid
        const ghobject_t &oid, ///< [in] Object containing omap
        set<string> *keys      ///< [out] Keys defined on oid
)
{
    FTRACE
    CollectionHandle c = _get_collection(cid);
    if (!c)
        return -ENOENT;
    return omap_get_keys(c, oid, keys);
}

int KvsStore::omap_get_keys(
        CollectionHandle &c_,              ///< [in] Collection containing oid
        const ghobject_t &oid, ///< [in] Object containing omap
        set<string> *keys      ///< [out] Keys defined on oid
)
{
    FTRACE
    KvsCollection *c = static_cast<KvsCollection *>(c_.get());
    dout(15) << __func__ << " " << c->get_cid() << " oid " << oid << dendl;
    if (!c->exists)
        return -ENOENT;
    RWLock::RLocker l(c->lock);
    int r = 0;
    OnodeRef o = c->get_onode(oid, false);
    if (!o || !o->exists) {
        r = -ENOENT;
        goto out;
    }
    if (!o->onode.has_omap())
        goto out;
    o->flush();

    // TODO: iterator
    /*{
        KeyValueDB::Iterator it = db->get_iterator(PREFIX_OMAP);
        string head, tail;
        get_omap_key(o->onode.nid, string(), &head);
        get_omap_tail(o->onode.nid, &tail);
        it->lower_bound(head);
        while (it->valid()) {
            if (it->key() >= tail) {
                dout(30) << __func__ << "  reached tail" << dendl;
                break;
            }
            string user_key;
            decode_omap_key(it->key(), &user_key);
            dout(30) << __func__ << "  got " << pretty_binary_string(it->key())
                     << " -> " << user_key << dendl;
            keys->insert(user_key);
            it->next();
        }
    }*/
    out:
    dout(10) << __func__ << " " << c->get_cid() << " oid " << oid << " = " << r
             << dendl;
    return r;
}

int KvsStore::omap_get_values(
        const coll_t& cid,                    ///< [in] Collection containing oid
        const ghobject_t &oid,       ///< [in] Object containing omap
        const set<string> &keys,     ///< [in] Keys to get
        map<string, bufferlist> *out ///< [out] Returned keys and values
)
{
    CollectionHandle c = _get_collection(cid);
    if (!c)
        return -ENOENT;
    return omap_get_values(c, oid, keys, out);
}

int KvsStore::omap_get_values(
        CollectionHandle &c_,        ///< [in] Collection containing oid
        const ghobject_t &oid,       ///< [in] Object containing omap
        const set<string> &keys,     ///< [in] Keys to get
        map<string, bufferlist> *out ///< [out] Returned keys and values
)
{
    FTRACE
    KvsCollection *c = static_cast<KvsCollection *>(c_.get());
    dout(15) << __func__ << " " << c->get_cid() << " oid " << oid << dendl;
    if (!c->exists)
        return -ENOENT;
    RWLock::RLocker l(c->lock);
    int r = 0;
    string final_key;
    OnodeRef o = c->get_onode(oid, false);
    if (!o || !o->exists) {
        r = -ENOENT;
        goto out;
    }
    if (!o->onode.has_omap())
        goto out;
    o->flush();

    for (set<string>::const_iterator p = keys.begin(); p != keys.end(); ++p) {
        // TODO: read OMAP keys
        /*
        final_key.resize(9); // keep prefix
        final_key += *p;
        bufferlist val;
        if (db->get(PREFIX_OMAP, final_key, &val) >= 0) {
            dout(30) << __func__ << "  got " << pretty_binary_string(final_key)
                     << " -> " << *p << dendl;
            out->insert(make_pair(*p, val));
        }
         */
    }
out:

    return r;
}

int KvsStore::omap_check_keys(
        const coll_t& cid,                ///< [in] Collection containing oid
        const ghobject_t &oid,   ///< [in] Object containing omap
        const set<string> &keys, ///< [in] Keys to check
        set<string> *out         ///< [out] Subset of keys defined on oid
)
{
    FTRACE
    CollectionHandle c = _get_collection(cid);
    if (!c)
        return -ENOENT;
    return omap_check_keys(c, oid, keys, out);
}

int KvsStore::omap_check_keys(
        CollectionHandle &c_,    ///< [in] Collection containing oid
        const ghobject_t &oid,   ///< [in] Object containing omap
        const set<string> &keys, ///< [in] Keys to check
        set<string> *out         ///< [out] Subset of keys defined on oid
)
{
    FTRACE
    KvsCollection *c = static_cast<KvsCollection *>(c_.get());
    dout(15) << __func__ << " " << c->get_cid() << " oid " << oid << dendl;
    if (!c->exists)
        return -ENOENT;
    RWLock::RLocker l(c->lock);
    int r = 0;
    string final_key;
    OnodeRef o = c->get_onode(oid, false);
    if (!o || !o->exists) {
        r = -ENOENT;
        goto out;
    }
    if (!o->onode.has_omap())
        goto out;
    o->flush();


    for (set<string>::const_iterator p = keys.begin(); p != keys.end(); ++p) {
        // TODO: read omap key
        /*
        final_key.resize(9); // keep prefix
        final_key += *p;
        bufferlist val;
        if (db->get(PREFIX_OMAP, final_key, &val) >= 0) {
            dout(30) << __func__ << "  have " << pretty_binary_string(final_key)
                     << " -> " << *p << dendl;
            out->insert(*p);
        } else {
            dout(30) << __func__ << "  miss " << pretty_binary_string(final_key)
                     << " -> " << *p << dendl;
        }
        */
    }
out:
    return r;
}

ObjectMap::ObjectMapIterator KvsStore::get_omap_iterator(
        const coll_t& cid,              ///< [in] collection
        const ghobject_t &oid  ///< [in] object
)
{
    FTRACE
    CollectionHandle c = _get_collection(cid);
    if (!c) {
        dout(10) << __func__ << " " << cid << "doesn't exist" <<dendl;
        return ObjectMap::ObjectMapIterator();
    }
    return get_omap_iterator(c, oid);
}

ObjectMap::ObjectMapIterator KvsStore::get_omap_iterator(
        CollectionHandle &c_,              ///< [in] collection
        const ghobject_t &oid  ///< [in] object
)
{
    FTRACE
    KvsCollection *c = static_cast<KvsCollection *>(c_.get());
    dout(10) << __func__ << " " << c->get_cid() << " " << oid << dendl;
    if (!c->exists) {
        return ObjectMap::ObjectMapIterator();
    }
    RWLock::RLocker l(c->lock);
    OnodeRef o = c->get_onode(oid, false);
    if (!o || !o->exists) {
        dout(10) << __func__ << " " << oid << "doesn't exist" <<dendl;
        return ObjectMap::ObjectMapIterator();
    }
    o->flush();

    // TODO: OMAP iterator
    /*
    dout(10) << __func__ << " has_omap = " << (int)o->onode.has_omap() <<dendl;
    KeyValueDB::Iterator it = db->get_iterator(PREFIX_OMAP);
    return ObjectMap::ObjectMapIterator(new OmapIteratorImpl(c, o, it));
     */
    return ObjectMap::ObjectMapIterator();
}



/// -------------------
///  WRITE I/O
/// -------------------


KvsTransContext *KvsStore::_txc_create(KvsOpSequencer *osr)
{
    FTRACE
    KvsTransContext *txc = new KvsTransContext(cct, this, osr);
    osr->queue_new(txc);
    dout(20) << __func__ << " osr " << osr << " = " << txc
             << " seq " << txc->seq << dendl;
    return txc;
}


void KvsStore::_txc_aio_submit(KvsTransContext *txc)
{
   FTRACE
   db.aio_submit(txc);
}

// write callback
void KvsStore::txc_aio_finish(kv_io_context *op, KvsTransContext *txc)
{
    FTRACE
    if (op->retcode != KV_SUCCESS) {
        derr << "I/O failed ( write_callback ): " << this->db.errstr(op->retcode) << dendl;
    }

    if (--txc->ioc.num_running == 0) {
        // last I/O -> proceed the transaction status
        txc->store->_txc_state_proc(txc);
    }
}


void KvsStore::_txc_state_proc(KvsTransContext *txc)
{
    FTRACE
    while (true) {
        //derr << __func__ << " txc " << txc
        //         << " " << txc->get_state_name() << dendl;
        switch (txc->state) {
            case KvsTransContext::STATE_PREPARE:

                if (txc->ioc.has_pending_aios()) {
                    //derr << "submit" << dendl;
                    txc->state = KvsTransContext::STATE_AIO_WAIT;
                    txc->had_ios = true;
                    _txc_aio_submit(txc);
                    return;
                }
                // ** fall-thru **

            case KvsTransContext::STATE_AIO_WAIT:
                /* called by kv_callback_thread */
                _txc_finish_io(txc);  // add txc -> commiting_to_finalize queue, state = IO_DONE -> txc_state_proc
                return;

            case KvsTransContext::STATE_IO_DONE:
                /* called by kv_callback_thread */
                txc->state = KvsTransContext::STATE_FINISHING;

                // add it to the finisher
                _txc_committed_kv(txc);

                {
                    std::unique_lock<std::mutex> l(kv_finalize_lock);
                    kv_committing_to_finalize.push_back(txc);
                    kv_finalize_cond.notify_one();
                }

                return;
            case KvsTransContext::STATE_FINISHING:
                /* called by kv_finalize_thread */
                _txc_finish(txc);
                return;

            default:
                derr << __func__ << " unexpected txc " << txc
                     << " state " << txc->get_state_name() << dendl;
                assert(0 == "unexpected txc state");
                return;
        }
    }
}


void KvsStore::_txc_finish_io(KvsTransContext *txc)
{
    FTRACE
    dout(20) << __func__ << " " << txc << dendl;

    /*
     * we need to preserve the order of kv transactions,
     * even though aio will complete in any order.
     */

    KvsOpSequencer *osr = txc->osr.get();
    std::lock_guard<std::mutex> l(osr->qlock);
    txc->state = KvsTransContext::STATE_IO_DONE;

    // NOTE: we will release running_aios in _txc_release_alloc

    KvsOpSequencer::q_list_t::iterator p = osr->q.iterator_to(*txc);
    while (p != osr->q.begin()) {
        --p;
        if (p->state < KvsTransContext::STATE_IO_DONE) {
            dout(20) << __func__ << " " << txc << " blocked by " << &*p << " "
                     << p->get_state_name() << dendl;
            return;
        }
        if (p->state > KvsTransContext::STATE_IO_DONE) {
            ++p;
            break;
        }
    }
    do {
        _txc_state_proc(&*p++);
    } while (p != osr->q.end() &&
             p->state == KvsTransContext::STATE_IO_DONE);

    // wake up waiting flush() if needed.
    if (osr->kv_submitted_waiters &&
        osr->_is_all_kv_submitted()) {
        osr->qcond.notify_all();
    }
}


void KvsStore::_txc_committed_kv(KvsTransContext *txc)
{
    FTRACE
    dout(20) << __func__ << " txc " << txc << dendl;

    // warning: we're calling onreadable_sync inside the sequencer lock
    if (txc->onreadable_sync) {
        txc->onreadable_sync->complete(0);
        txc->onreadable_sync = NULL;
    }
    unsigned n = txc->osr->parent->shard_hint.hash_to_shard(m_finisher_num);
    if (txc->oncommit) {
        finishers[n]->queue(txc->oncommit);
        txc->oncommit = NULL;
    }
    if (txc->onreadable) {
        finishers[n]->queue(txc->onreadable);
        txc->onreadable = NULL;
    }

    if (!txc->oncommits.empty()) {
        finishers[n]->queue(txc->oncommits);
    }
}


void KvsStore::_txc_finish(KvsTransContext *txc)
{
    FTRACE
    dout(20) << __func__ << " " << txc << " onodes " << txc->onodes << dendl;
    assert(txc->state == KvsTransContext::STATE_FINISHING);

    while (!txc->removed_collections.empty()) {
        _queue_reap_collection(txc->removed_collections.front());
        txc->removed_collections.pop_front();
    }

    OpSequencerRef osr = txc->osr;
    bool empty = false;

    KvsOpSequencer::q_list_t releasing_txc;
    {
        std::lock_guard<std::mutex> l(osr->qlock);
        txc->state = KvsTransContext::STATE_DONE;
        bool notify = false;
        while (!osr->q.empty()) {
            KvsTransContext *txc = &osr->q.front();
            dout(20) << __func__ << "  txc " << txc << " " << txc->get_state_name()
                     << dendl;
            if (txc->state != KvsTransContext::STATE_DONE) {
                break;
            }

            osr->q.pop_front();
            releasing_txc.push_back(*txc);
            notify = true;
        }
        if (notify) {
            osr->qcond.notify_all();
        }
        if (osr->q.empty()) {
            dout(20) << __func__ << " osr " << osr << " q now empty" << dendl;
            empty = true;
        }
    }

    while (!releasing_txc.empty()) {
        // release to allocator only after all preceding txc's have also
        // finished any deferred writes that potentially land in these
        // blocks
        auto txc = &releasing_txc.front();
        _txc_release_alloc(txc);
        releasing_txc.pop_front();
        delete txc;
    }


    if (empty && osr->zombie) {
        dout(10) << __func__ << " reaping empty zombie osr " << osr << dendl;
        osr->_unregister();
    }
}

void KvsStore::_txc_release_alloc(KvsTransContext *txc)
{
    FTRACE
    KvsIoContext *ioc = &txc->ioc;

    // release memory
    for (const auto &p : ioc->running_aios) {
        KvsMemPool::Release_key(p.first);
        if (p.second)
            KvsMemPool::Release_value(p.second);
    }
    

    txc->onodes.clear();
}


void KvsStore::_txc_write_nodes(KvsTransContext *txc)
{
    FTRACE
    dout(20) << __func__ << " txc " << txc
             << " onodes " << txc->onodes
             << dendl;

    // finalize onodes
    for (auto o : txc->onodes) {
        // bound encode
        size_t bound = 0;
        denc(o->onode, bound);

        // encode
        bufferlist bl;
        {
            auto p = bl.get_contiguous_appender(bound, true);
            denc(o->onode, p);
        }

        dout(20) << "  onode " << o->oid << " is " << bl.length() << dendl;

        txc->ioc.add_onode(o->oid, 0, bl);
    }
}

void KvsStore::_kv_finalize_thread()
{
    FTRACE
    deque<KvsTransContext*> kv_committed;
    derr << __func__ << " start" << dendl;
    std::unique_lock<std::mutex> l(kv_finalize_lock);
    assert(!kv_finalize_started);
    kv_finalize_started = true;
    kv_finalize_cond.notify_all();

    while (true) {
        assert(kv_committed.empty());
        if (kv_committing_to_finalize.empty()) {
            if (kv_finalize_stop)
                break;
            kv_finalize_cond.wait(l);
        } else {
            kv_committed.swap(kv_committing_to_finalize);
            l.unlock();
            //dout(20) << __func__ << " kv_committed " << kv_committed << dendl;

            while (!kv_committed.empty()) {

                KvsTransContext *txc = kv_committed.front();
		if (txc->ioc.submitted.load() == false) continue; 
                
                assert(txc->state == KvsTransContext::STATE_FINISHING);
                _txc_state_proc(txc);
                kv_committed.pop_front();
            }

            // this is as good a place as any ...
            _reap_collections();

            l.lock();
        }
    }
    derr << __func__ << " finish" << dendl;
    kv_finalize_started = false;

}


void KvsStore::_kv_callback_thread()
{
    FTRACE
    assert(!kv_callback_started);
    kv_callback_started = true;

    uint32_t toread = 1024;

    derr << __func__ << " start" << dendl;
    while (!kv_stop) {
        if (this->db.is_opened()) {
            this->db.poll_completion(toread, 1000000);
            usleep(1);
        }
    }

    derr << __func__ << " finish" << dendl;
    kv_callback_started = false;
}

void KvsStore::_queue_reap_collection(CollectionRef& c)
{
    FTRACE
    dout(10) << __func__ << " " << c << " " << c->cid << dendl;
    std::lock_guard<std::mutex> l(reap_lock);
    removed_collections.push_back(c);
}


void KvsStore::_reap_collections()
{
    FTRACE
    list<CollectionRef> removed_colls;
    {
        std::lock_guard<std::mutex> l(reap_lock);
        removed_colls.swap(removed_collections);
    }

    bool all_reaped = true;

    for (list<CollectionRef>::iterator p = removed_colls.begin(); p != removed_colls.end(); ++p) {
        CollectionRef c = *p;
        dout(10) << __func__ << " " << c << " " << c->cid << dendl;
        if (c->onode_map.map_any([&](OnodeRef o) {
            assert(!o->exists);
            if (o->flushing_count.load()) {
                dout(10) << __func__ << " " << c << " " << c->cid << " " << o->oid
                         << " flush_txns " << o->flushing_count << dendl;
                return false;
            }
            return true;
        })) {
            all_reaped = false;
            continue;
        }
        c->onode_map.clear();
        dout(10) << __func__ << " " << c << " " << c->cid << " done" << dendl;
    }

    if (all_reaped) {
        dout(10) << __func__ << " all reaped" << dendl;
    }
}

/// -------------------
///  TRANSACTIONS
/// -------------------


int KvsStore::queue_transactions(
        Sequencer *posr,
        vector<Transaction>& tls,
        TrackedOpRef op,
        ThreadPool::TPHandle *handle)
{
    FTRACE;
    Context *onreadable;
    Context *ondisk;
    Context *onreadable_sync;
    ObjectStore::Transaction::collect_contexts(
            tls, &onreadable, &ondisk, &onreadable_sync);

    if (cct->_conf->objectstore_blackhole) {
        delete ondisk;
        delete onreadable;
        delete onreadable_sync;
        return 0;
    }

    // set up the sequencer
    KvsOpSequencer *osr;
    assert(posr);
    if (posr->p) {
        osr = static_cast<KvsOpSequencer *>(posr->p.get());
        dout(10) << __func__ << " existing " << osr << " " << *osr << dendl;
    } else {
        osr = new KvsOpSequencer(cct, this);
        osr->parent = posr;
        posr->p = osr;
        dout(10) << __func__ << " new " << osr << " " << *osr << dendl;
    }

    // prepare
    KvsTransContext *txc = _txc_create(osr);
    txc->onreadable = onreadable;
    txc->onreadable_sync = onreadable_sync;
    txc->oncommit = ondisk;

    for (vector<Transaction>::iterator p = tls.begin(); p != tls.end(); ++p) {
        (*p).set_osr(osr);
        _txc_add_transaction(txc, &(*p));
    }

    _txc_write_nodes(txc);

    // TODO: journal keys here

    // execute (start)
    _txc_state_proc(txc);

    return 0;
}

void KvsStore::_txc_add_transaction(KvsTransContext *txc, Transaction *t)
{
    FTRACE

    Transaction::iterator i = t->begin();

    vector<CollectionRef> cvec(i.colls.size());

    unsigned j = 0;
    for (vector<coll_t>::iterator p = i.colls.begin(); p != i.colls.end();
         ++p, ++j) {
        //derr << "cvec = " << j << ", coll_t = " << (*p).c_str() << dendl;

        cvec[j] = _get_collection(*p);

    }

    vector<OnodeRef> ovec(i.objects.size());

    for (int pos = 0; i.have_op(); ++pos) {
        Transaction::Op *op = i.decode_op();
        int r = 0;

        // no coll or obj
        if (op->op == Transaction::OP_NOP)
            continue;

        // collection operations
        CollectionRef &c = cvec[op->cid];
        switch (op->op) {
            case Transaction::OP_RMCOLL:
            {
                const coll_t &cid = i.get_cid(op->cid);
                r = _remove_collection(txc, cid, &c);
                if (!r)
                    continue;
            }
                break;

            case Transaction::OP_MKCOLL:
            {
                //derr << "c = " << c <<"," <<  !c  << dendl;
                if (c) { i.get_cid(op->cid); r = 0; continue; }   // to support the kvssd bypass mode
                //assert(!c);
                const coll_t &cid = i.get_cid(op->cid);
                //derr << "cid = " << cid << dendl;
                r = _create_collection(txc, cid, op->split_bits, &c);
                if (!r)
                    continue;
            }
                break;

            case Transaction::OP_SPLIT_COLLECTION:
                assert(0 == "deprecated");
                break;

            case Transaction::OP_SPLIT_COLLECTION2:
                r = -EOPNOTSUPP;
                break;

            case Transaction::OP_COLL_HINT:
            {
                bufferlist hint;
                i.decode_bl(hint);
                continue;
            }
                break;

            case Transaction::OP_COLL_SETATTR:
                r = -EOPNOTSUPP;
                break;

            case Transaction::OP_COLL_RMATTR:
                r = -EOPNOTSUPP;
                break;

            case Transaction::OP_COLL_RENAME:
                assert(0 == "not implemented");
                break;
        }

        if (r < 0) {
            derr << __func__ << " error " << cpp_strerror(r)
                 << " not handled on operation " << op->op
                 << " (op " << pos << ", counting from 0)" << dendl;
            assert(0 == "unexpected error");
        }

        // these operations implicity create the object
        bool create = false;
        if (op->op == Transaction::OP_TOUCH ||
            op->op == Transaction::OP_WRITE ||
            op->op == Transaction::OP_ZERO) {
            create = true;
        }

        // object operations
        RWLock::WLocker l(c->lock);
        OnodeRef &o = ovec[op->oid];
        if (!o) {
            ghobject_t oid = i.get_oid(op->oid);
            o = c->get_onode(oid, create);
        }
        if (!create && (!o || !o->exists)) {
            dout(10) << __func__ << " op " << op->op << " got ENOENT on "
                     << i.get_oid(op->oid) << dendl;

            r = -ENOENT;
            goto endop;
        }

        switch (op->op) {
            case Transaction::OP_TOUCH:
                r = _touch(txc, c, o);
                break;

            case Transaction::OP_WRITE:
            {
                uint64_t off = op->off;
                uint64_t len = op->len;
                uint32_t fadvise_flags = i.get_fadvise_flags();
                bufferlist bl;
                i.decode_bl(bl);
                r = _write(txc, c, o, off, len, bl, fadvise_flags);
            }
                break;

            case Transaction::OP_ZERO:
            {
                uint64_t off = op->off;
                uint64_t len = op->len;
                r = _zero(txc, c, o, off, len);
            }
                break;

            case Transaction::OP_TRIMCACHE:
            {
                // deprecated, no-op
            }
                break;

            case Transaction::OP_TRUNCATE:
            {
                // TODO
                r = 0;
            }
                break;

            case Transaction::OP_REMOVE:
            {
                r = _remove(txc, c, o);
            }
                break;

            case Transaction::OP_SETATTR:
            {
                string name = i.decode_string();
                bufferptr bp;
                i.decode_bp(bp);
                r = _setattr(txc, c, o, name, bp);
            }
                break;

            case Transaction::OP_SETATTRS:
            {
                map<string, bufferptr> aset;
                i.decode_attrset(aset);
                r = _setattrs(txc, c, o, aset);
            }
                break;

            case Transaction::OP_RMATTR:
            {
                string name = i.decode_string();
                r = _rmattr(txc, c, o, name);
            }
                break;

            case Transaction::OP_RMATTRS:
            {
                r = _rmattrs(txc, c, o);
            }
                break;

            case Transaction::OP_CLONE:
            {
                // TODO
                assert(0 == "not implemented");
            }
                break;

            case Transaction::OP_CLONERANGE:
                assert(0 == "deprecated");
                break;

            case Transaction::OP_CLONERANGE2:
            {
                // TODO
                assert(0 == "not implemented");
            }
                break;

            case Transaction::OP_COLL_ADD:
                assert(0 == "not implemented");
                break;

            case Transaction::OP_COLL_REMOVE:
                assert(0 == "not implemented");
                break;

            case Transaction::OP_COLL_MOVE:
                assert(0 == "deprecated");
                break;

            case Transaction::OP_COLL_MOVE_RENAME:
            case Transaction::OP_TRY_RENAME:
            {
                // TODO
                assert(0 == "not implemented");
            }
                break;

            case Transaction::OP_OMAP_CLEAR:
            {
                r = _omap_clear(txc, c, o);
            }
                break;
            case Transaction::OP_OMAP_SETKEYS:
            {
                bufferlist aset_bl;
                i.decode_attrset_bl(&aset_bl);
                r = _omap_setkeys(txc, c, o, aset_bl);
            }
                break;
            case Transaction::OP_OMAP_RMKEYS:
            {
                bufferlist keys_bl;
                i.decode_keyset_bl(&keys_bl);
                r = _omap_rmkeys(txc, c, o, keys_bl);
            }
                break;
            case Transaction::OP_OMAP_RMKEYRANGE:
            {
                string first, last;
                first = i.decode_string();
                last = i.decode_string();
                r = _omap_rmkey_range(txc, c, o, first, last);
            }
                break;
            case Transaction::OP_OMAP_SETHEADER:
            {
                bufferlist bl;
                i.decode_bl(bl);
                r = 0; // the OMAP header is not needed
            }
                break;

            case Transaction::OP_SETALLOCHINT:
            {
                r = 0; // alloc hint not needed
            }
                break;

            default:
                derr << __func__ << "bad op " << op->op << dendl;
                ceph_abort();
        }

        endop:
        if (r < 0) {
            bool ok = false;

            if (r == -ENOENT && !(op->op == Transaction::OP_CLONERANGE ||
                                  op->op == Transaction::OP_CLONE ||
                                  op->op == Transaction::OP_CLONERANGE2 ||
                                  op->op == Transaction::OP_COLL_ADD ||
                                  op->op == Transaction::OP_SETATTR ||
                                  op->op == Transaction::OP_SETATTRS ||
                                  op->op == Transaction::OP_RMATTR ||
                                  op->op == Transaction::OP_OMAP_SETKEYS ||
                                  op->op == Transaction::OP_OMAP_RMKEYS ||
                                  op->op == Transaction::OP_OMAP_RMKEYRANGE ||
                                  op->op == Transaction::OP_OMAP_SETHEADER))
                // -ENOENT is usually okay
                ok = true;
            if (r == -ENODATA)
                ok = true;

            if (!ok) {
                const char *msg = "unexpected error code";

                if (r == -ENOENT && (op->op == Transaction::OP_CLONERANGE ||
                                     op->op == Transaction::OP_CLONE ||
                                     op->op == Transaction::OP_CLONERANGE2))
                    msg = "ENOENT on clone suggests osd bug";

                if (r == -ENOSPC)
                    // For now, if we hit _any_ ENOSPC, crash, before we do any damage
                    // by partially applying transactions.
                    msg = "ENOSPC from bluestore, misconfigured cluster";

                if (r == -ENOTEMPTY) {
                    msg = "ENOTEMPTY suggests garbage data in osd data dir";
                }

                derr << __func__ << " error2 " << cpp_strerror(r)
                     << " not handled on operation " << op->op
                     << " (op " << pos << ", counting from 0)"
                     << dendl;
                derr << msg << dendl;
                assert(0 == "unexpected error");
            }
        }
    }
}

/// -------------------
///  Write OPs
/// -------------------


int KvsStore::_touch(KvsTransContext *txc,
                      CollectionRef& c,
                      OnodeRef &o)
{
    FTRACE
    dout(15) << __func__ << " " << c->cid << " " << o->oid << dendl;
    int r = 0;
    o->exists = true;
    txc->write_onode(o);
    dout(10) << __func__ << " " << c->cid << " " << o->oid << " = " << r << dendl;
    return r;
}

int KvsStore::_write(KvsTransContext *txc,
                      CollectionRef& c,
                      OnodeRef& o,
                      uint64_t offset, size_t length,
                      bufferlist& bl,
                      uint32_t fadvise_flags)
{
    FTRACE
    //print_stacktrace_s();
    dout(15) << __func__ << " " << c->cid << " " << o->oid
             << " 0x" << std::hex << offset << "~" << length << std::dec
             << dendl;
    int r = 0;
    if (offset + length > KVS_OBJECT_MAX_SIZE) {
        r = -E2BIG;
    } else {
        o->exists = true;
        o->onode.size = bl.length();

        txc->ioc.add_userdata(o->oid, bl);
        txc->write_onode(o);

        r = 0;
    }
    dout(10) << __func__ << " " << c->cid << " " << o->oid
             << " 0x" << std::hex << offset << "~" << length << std::dec
             << " = " << r << dendl;
    return r;
}

int KvsStore::_zero(KvsTransContext *txc,
                     CollectionRef& c,
                     OnodeRef& o,
                     uint64_t offset, size_t length)
{
    FTRACE
    dout(15) << __func__ << " " << c->cid << " " << o->oid
             << " 0x" << std::hex << offset << "~" << length << std::dec
             << dendl;
    int r = 0;
    if (offset + length >= KVS_OBJECT_MAX_SIZE) {
        r = -E2BIG;
    } else {
        if (offset + length > o->onode.size) {
            o->onode.size = offset + length;
        }
        o->exists = true;
        r = _do_zero(txc, c, o, offset, length);
    }
    dout(10) << __func__ << " " << c->cid << " " << o->oid
             << " 0x" << std::hex << offset << "~" << length << std::dec
             << " = " << r << dendl;
    return r;
}

int KvsStore::_do_zero(KvsTransContext *txc,
                        CollectionRef& c,
                        OnodeRef& o,
                        uint64_t offset, size_t length)
{
    FTRACE
    dout(15) << __func__ << " " << c->cid << " " << o->oid
             << " 0x" << std::hex << offset << "~" << length << std::dec
             << dendl;
    int r = 0;

//    _dump_onode(o);

    //TODO: need offset writes

    if (offset + length > o->onode.size) {
        o->onode.size = offset + length;
        dout(20) << __func__ << " extending size to " << offset + length
                 << dendl;
    }

    txc->write_onode(o);

    return r;
}


void KvsStore::_do_truncate(
        KvsTransContext *txc, CollectionRef& c, OnodeRef o, uint64_t offset)
{
    FTRACE
    dout(15) << __func__ << " " << c->cid << " " << o->oid
             << " 0x" << std::hex << offset << std::dec << dendl;

    if (offset == o->onode.size)
        return;

    o->onode.size = offset;

    txc->write_onode(o);
}

int KvsStore::_truncate(KvsTransContext *txc,
                         CollectionRef& c,
                         OnodeRef& o,
                         uint64_t offset)
{
    FTRACE
    dout(15) << __func__ << " " << c->cid << " " << o->oid
             << " 0x" << std::hex << offset << std::dec
             << dendl;
    int r = 0;
    if (offset >= KVS_OBJECT_MAX_SIZE) {
        r = -E2BIG;
    } else {
        _do_truncate(txc, c, o, offset);
    }
    dout(10) << __func__ << " " << c->cid << " " << o->oid
             << " 0x" << std::hex << offset << std::dec
             << " = " << r << dendl;
    return r;
}

int KvsStore::_do_remove(
        KvsTransContext *txc,
        CollectionRef& c,
        OnodeRef o)
{
    FTRACE
    if (o->onode.has_omap()) {
        o->flush();
        _do_omap_clear(txc, o);
    }
    o->exists = false;
    txc->ioc.rm_onode(o->oid);
    txc->removed(o);

    o->onode = kvsstore_onode_t();

    return 0;
}

int KvsStore::_remove(KvsTransContext *txc,
                       CollectionRef& c,
                       OnodeRef &o)
{
    FTRACE
    dout(15) << __func__ << " " << c->cid << " " << o->oid << dendl;
    int r = _do_remove(txc, c, o);
    dout(10) << __func__ << " " << c->cid << " " << o->oid << " = " << r << dendl;
    return r;
}

int KvsStore::_setattr(KvsTransContext *txc,
                        CollectionRef& c,
                        OnodeRef& o,
                        const string& name,
                        bufferptr& val)
{
    FTRACE
    dout(15) << __func__ << " " << c->cid << " " << o->oid
             << " " << name << " (" << val.length() << " bytes)"
             << dendl;
    int r = 0;

    int attrid = o->onode.get_attr_id(name.c_str());

    if (val.is_partial()) {
        auto& b = o->onode.attrs[attrid] = bufferptr(val.c_str(),
                                                           val.length());
        b.reassign_to_mempool(mempool::mempool_kvsstore_cache_other);
    } else {
        auto& b = o->onode.attrs[attrid] = val;
        b.reassign_to_mempool(mempool::mempool_kvsstore_cache_other);
    }
    txc->write_onode(o);
    dout(10) << __func__ << " " << c->cid << " " << o->oid
             << " " << name << " (" << val.length() << " bytes)"
             << " = " << r << dendl;
    return r;
}

int KvsStore::_setattrs(KvsTransContext *txc,
                         CollectionRef& c,
                         OnodeRef& o,
                         const map<string,bufferptr>& aset)
{
    FTRACE
    dout(15) << __func__ << " " << c->cid << " " << o->oid
             << " " << aset.size() << " keys"
             << dendl;
    int r = 0;



    for (map<string,bufferptr>::const_iterator p = aset.begin();
         p != aset.end(); ++p) {

        int attrid = o->onode.get_attr_id(p->first.c_str());

        if (p->second.is_partial()) {
            auto& b = o->onode.attrs[attrid] =
                              bufferptr(p->second.c_str(), p->second.length());
            b.reassign_to_mempool(mempool::mempool_kvsstore_cache_other);
        } else {
            auto& b = o->onode.attrs[attrid] = p->second;
            b.reassign_to_mempool(mempool::mempool_kvsstore_cache_other);
        }
    }
    txc->write_onode(o);
    dout(10) << __func__ << " " << c->cid << " " << o->oid
             << " " << aset.size() << " keys"
             << " = " << r << dendl;
    return r;
}


int KvsStore::_rmattr(KvsTransContext *txc,
                       CollectionRef& c,
                       OnodeRef& o,
                       const string& name)
{
    FTRACE
    dout(15) << __func__ << " " << c->cid << " " << o->oid
             << " " << name << dendl;
    int r = 0;
    int attrid;
    auto it = o->onode.attr_names.find(name.c_str());
    if (it == o->onode.attr_names.end())
        goto out;

    attrid = it->second;

    o->onode.attr_names.erase(it);
    o->onode.attrs.erase(attrid);

    txc->write_onode(o);

    out:
    dout(10) << __func__ << " " << c->cid << " " << o->oid
             << " " << name << " = " << r << dendl;
    return r;
}

int KvsStore::_rmattrs(KvsTransContext *txc,
                        CollectionRef& c,
                        OnodeRef& o)
{
    FTRACE
    dout(15) << __func__ << " " << c->cid << " " << o->oid << dendl;
    int r = 0;

    if (o->onode.attrs.empty())
        goto out;

    o->onode.attr_names.clear();
    o->onode.attrs.clear();
    txc->write_onode(o);

    out:
    dout(10) << __func__ << " " << c->cid << " " << o->oid << " = " << r << dendl;
    return r;
}

void KvsStore::_do_omap_clear(KvsTransContext *txc, OnodeRef &o)
{
    FTRACE
    // TODO: with an iterator
}

int KvsStore::_omap_clear(KvsTransContext *txc,
                           CollectionRef& c,
                           OnodeRef& o)
{
    FTRACE
    dout(15) << __func__ << " " << c->cid << " " << o->oid << dendl;
    int r = 0;
    if (o->onode.has_omap()) {
        o->flush();
        _do_omap_clear(txc, o);
        o->onode.clear_omap_flag();
        txc->write_onode(o);
    }
    dout(10) << __func__ << " " << c->cid << " " << o->oid << " = " << r << dendl;
    return r;
}

int KvsStore::_omap_setkeys(KvsTransContext *txc,
                             CollectionRef& c,
                             OnodeRef& o,
                             bufferlist &bl)
{
    FTRACE
    dout(15) << __func__ << " " << c->cid << " " << o->oid << dendl;
    int r;
    bufferlist::iterator p = bl.begin();
    __u32 num;
    if (!o->onode.has_omap()) {
        o->onode.set_omap_flag();
        txc->write_onode(o);
    }

    ::decode(num, p);
    while (num--) {
        string key;
        bufferlist value;
        ::decode(key, p);
        ::decode(value, p);
        dout(30) << __func__ << "  " << pretty_binary_string(key) << dendl;
        txc->ioc.add_omap(o->oid, o->get_omap_index(key), value);
    }
    r = 0;
    dout(10) << __func__ << " " << c->cid << " " << o->oid << " = " << r << dendl;
    return r;
}

int KvsStore::_omap_setheader(KvsTransContext *txc,
                               CollectionRef& c,
                               OnodeRef &o,
                               bufferlist& bl)
{
    FTRACE
    dout(15) << __func__ << " " << c->cid << " " << o->oid << dendl;
    int r;
    string key;
    if (!o->onode.has_omap()) {
        o->onode.set_omap_flag();
        txc->write_onode(o);
    }
    r = 0;
    dout(10) << __func__ << " " << c->cid << " " << o->oid << " = " << r << dendl;
    return r;
}

int KvsStore::_omap_rmkeys(KvsTransContext *txc,
                            CollectionRef& c,
                            OnodeRef& o,
                            bufferlist& bl)
{
    FTRACE
    dout(15) << __func__ << " " << c->cid << " " << o->oid << dendl;
    int r = 0;
    bufferlist::iterator p = bl.begin();
    __u32 num;

    if (!o->onode.has_omap()) {
        goto out;
    }

    ::decode(num, p);
    //derr << __func__ << "  num omaps to delete : " << num << dendl;
    while (num--) {
        string key;
        ::decode(key, p);
        //derr << __func__ << "  rm omap" << " <- " << key << dendl;

        txc->ioc.rm_omap(o->oid, o->get_omap_index(key));
    }

    out:
    dout(10) << __func__ << " " << c->cid << " " << o->oid << " = " << r << dendl;
    return r;
}

int KvsStore::_omap_rmkey_range(KvsTransContext *txc,
                                 CollectionRef& c,
                                 OnodeRef& o,
                                 const string& first, const string& last)
{
    FTRACE
    // TODO: with an iterator
    int r = 0;

    dout(10) << __func__ << " " << c->cid << " " << o->oid << " = " << r << dendl;
    return r;
}

int KvsStore::_set_alloc_hint(
        KvsTransContext *txc,
        CollectionRef& c,
        OnodeRef& o,
        uint64_t expected_object_size,
        uint64_t expected_write_size,
        uint32_t flags)
{
    FTRACE
    return 0;
}


// collections
int KvsStore::_create_collection(
        KvsTransContext *txc,
        const coll_t &cid,
        unsigned bits,
        CollectionRef *c)
{
    FTRACE
    //dout(0) << __func__ << " " << cid << " bits " << bits << dendl;
    int r;
    bufferlist bl;

    {
        RWLock::WLocker l(coll_lock);
        if (*c) {
            r = -EEXIST;
            goto out;
        }
        c->reset(
                new KvsCollection(
                        this,
                        cache_shards[0],
                        cid));
        (*c)->cnode.bits = bits;
        coll_map[cid] = *c;
    }
    
    ::encode((*c)->cnode, bl);

    txc->ioc.add_coll(cid, bl);
    r = 0;

out:
    //dout(0) << __func__ << " " << cid << " bits " << bits << " = " << r << dendl;
    return r;
}

int KvsStore::_remove_collection(KvsTransContext *txc, const coll_t &cid,
                                  CollectionRef *c)
{
    FTRACE
    dout(15) << __func__ << " " << cid << dendl;
    int r;

    {
        RWLock::WLocker l(coll_lock);
        if (!*c) {
            r = -ENOENT;
            goto out;
        }
        size_t nonexistent_count = 0;
        assert((*c)->exists);
        if ((*c)->onode_map.map_any([&](OnodeRef o) {
            if (o->exists) {
                dout(10) << __func__ << " " << o->oid << " " << o
                         << " exists in onode_map" << dendl;
                return true;
            }
            ++nonexistent_count;
            return false;
        })) {
            r = -ENOTEMPTY;
            goto out;
        }

        vector<ghobject_t> ls;
        ghobject_t next;
        // Enumerate onodes in db, up to nonexistent_count + 1
        // then check if all of them are marked as non-existent.
        // Bypass the check if returned number is greater than nonexistent_count
        r = _collection_list(c->get(), ghobject_t(), ghobject_t::get_max(),
                             nonexistent_count + 1, &ls, &next);
        if (r >= 0) {
            bool exists = false; //ls.size() > nonexistent_count;
            for (auto it = ls.begin(); !exists && it < ls.end(); ++it) {
                dout(10) << __func__ << " oid " << *it << dendl;
                auto onode = (*c)->onode_map.lookup(*it);
                exists = !onode || onode->exists;
                if (exists) {
                    dout(10) << __func__ << " " << *it
                             << " exists in db" << dendl;
                }
            }
            if (!exists) {
                coll_map.erase(cid);
                txc->removed_collections.push_back(*c);
                (*c)->exists = false;
                c->reset();
                txc->ioc.rm_coll(cid);
                r = 0;
            } else {
                dout(10) << __func__ << " " << cid
                         << " is non-empty" << dendl;
                r = -ENOTEMPTY;
            }
        }
    }

    out:
    dout(10) << __func__ << " " << cid << " = " << r << dendl;
    return r;
}
int KvsStore::_split_collection(KvsTransContext *txc,
                                 CollectionRef& c,
                                 CollectionRef& d,
                                 unsigned bits, int rem)
{
    FTRACE
    return 0;
}


void KvsStore::_flush_cache()
{
    FTRACE
    dout(10) << __func__ << dendl;
    for (auto i : cache_shards) {
        i->trim_all();
        assert(i->empty());
    }
    for (auto& p : coll_map) {
        if (!p.second->onode_map.empty()) {
            //derr << __func__ << "stray onodes on " << p.first << dendl;
            p.second->onode_map.dump(cct, 0);
        }
        assert(p.second->onode_map.empty());
    }
    coll_map.clear();
}

// For external caller.
// We use a best-effort policy instead, e.g.,
// we don't care if there are still some pinned onodes/data in the cache
// after this command is completed.
void KvsStore::flush_cache()
{
    FTRACE
    dout(10) << __func__ << dendl;
    for (auto i : cache_shards) {
        i->trim_all();
    }
}



/// -------------------
///  PATH & FSID
/// -------------------

int KvsStore::_open_path()
{
    FTRACE
    assert(path_fd < 0);
    //derr << "opening " << path << dendl;
    path_fd = ::open(path.c_str(), O_DIRECTORY);
    if (path_fd < 0) {
        int r = -errno;
        //derr << __func__ << " unable to open " << path << ": " << cpp_strerror(r) << dendl;
        return r;
    }
    return 0;
}

void KvsStore::_close_path()
{
    FTRACE
    VOID_TEMP_FAILURE_RETRY(::close(path_fd));
    path_fd = -1;
}

int KvsStore::_open_fsid(bool create)
{
    FTRACE
    assert(fsid_fd < 0);
    int flags = O_RDWR;
    if (create)
        flags |= O_CREAT;
    fsid_fd = ::openat(path_fd, "fsid", flags, 0644);
    if (fsid_fd < 0) {
        int err = -errno;
        //derr << __func__ << " " << cpp_strerror(err) << dendl;
        return err;
    }
    return 0;
}

int KvsStore::_read_fsid(uuid_d *uuid)
{
    FTRACE
    char fsid_str[40];
    memset(fsid_str, 0, sizeof(fsid_str));
    int ret = safe_read(fsid_fd, fsid_str, sizeof(fsid_str));
    if (ret < 0) {
        //derr << __func__ << " failed: " << cpp_strerror(ret) << dendl;
        return ret;
    }
    if (ret > 36)
        fsid_str[36] = 0;
    else
        fsid_str[ret] = 0;
    if (!uuid->parse(fsid_str)) {
        //derr << __func__ << " unparsable uuid " << fsid_str << dendl;
        return -EINVAL;
    }
    return 0;
}

int KvsStore::_write_fsid()
{
    FTRACE
    int r = ::ftruncate(fsid_fd, 0);
    if (r < 0) {
        r = -errno;
        //derr << __func__ << " fsid truncate failed: " << cpp_strerror(r) << dendl;
        return r;
    }
    string str = stringify(fsid) + "\n";
    r = safe_write(fsid_fd, str.c_str(), str.length());
    if (r < 0) {
        //derr << __func__ << " fsid write failed: " << cpp_strerror(r) << dendl;
        return r;
    }
    r = ::fsync(fsid_fd);
    if (r < 0) {
        r = -errno;
        //derr << __func__ << " fsid fsync failed: " << cpp_strerror(r) << dendl;
        return r;
    }
    return 0;
}

void KvsStore::_close_fsid()
{
    FTRACE
    VOID_TEMP_FAILURE_RETRY(::close(fsid_fd));
    fsid_fd = -1;
}

int KvsStore::_lock_fsid()
{
    FTRACE
    struct flock l;
    memset(&l, 0, sizeof(l));
    l.l_type = F_WRLCK;
    l.l_whence = SEEK_SET;
    int r = ::fcntl(fsid_fd, F_SETLK, &l);
    if (r < 0) {
        int err = errno;
        /*derr << __func__ << " failed to lock " << path << "/fsid"
             << " (is another ceph-osd still running?)"
             << cpp_strerror(err) << dendl;*/
        return -err;
    }
    return 0;
}




int KvsStore::_open_db(bool create)
{
    FTRACE

    //this->prefetcher.start();

    for (int i = 0; i < m_finisher_num; ++i) {
        ostringstream oss;
        oss << "kvs-finisher-" << i;
        Finisher *f = new Finisher(cct, oss.str(), "finisher");
        //derr << oss.str() << " is created" << dendl;
        finishers.push_back(f);
    }

    for (auto f : finishers) {
        f->start();
    }

    kv_stop = false;

    if (cct->_conf->kvsstore_dev_path == "") {
        return -1;
    }

    if (this->db.open(cct->_conf->kvsstore_dev_path) != 0) {
        return -1;
    }

    kv_callback_thread.create("kvscallback");
    kv_finalize_thread.create("kvsfinalize");

    return 0;
}

void KvsStore::_close_db()
{
    FTRACE

    kv_stop = true;
    {
        std::unique_lock<std::mutex> l(kv_finalize_lock);
        while (!kv_finalize_started) {
            kv_finalize_cond.wait(l);
        }
        kv_finalize_stop = true;
        kv_finalize_cond.notify_all();
    }
    kv_callback_thread.join();
    kv_finalize_thread.join();

    kv_stop = false;

    {
        std::lock_guard<std::mutex> l(kv_finalize_lock);
        kv_finalize_stop = false;
    }

    for (auto f : finishers) {
        f->wait_for_empty();
        f->stop();
    }

    this->db.close();

}

#if 0
    if (!inmkfs) { 
        {
		bufferlist bl;
                KvsReadContext ctx(cct);
                ctx.read_coll("meta", 4);
                db.aio_submit(&ctx);
                ctx.read_wait(bl);
		PRINTRKEY(ctx.key);

		if (ctx.retcode == KV_SUCCESS ) {

			struct kvs_coll_key *collkey = (struct kvs_coll_key *)ctx.key->key;

			coll_t cid;
			if (!cid.parse(collkey->name)) {
				derr << "ERR: " <<  __func__ << " unrecognized collection " << std::string(collkey->name) << dendl;
				return -1;
			}

			CollectionRef c(new KvsCollection(this, cache_shards[0], cid));

			if (bl.length() > 0) {

	    			derr << "test coll value length = " << bl.length() << "value = " << ceph_str_hash_linux(bl.c_str(),bl.length()) << dendl;
				bufferlist::iterator p = bl.begin();
				try {
					::decode(c->cnode , p);
				} catch (buffer::error& e) {
					derr << __func__ << " failed to decode cnode" << dendl;
					return -EIO;
				}

			}
			derr << "000 coll_map added : " << cid << dendl;
			coll_map[cid] = c;
			ret = 0;
		}

		goto release;
	}
    }

#endif
