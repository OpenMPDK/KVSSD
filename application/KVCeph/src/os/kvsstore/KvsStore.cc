//
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
#include <memory.h>
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

KvsCollection::KvsCollection(KvsStore *ns, KvsCache *c, KvsCache *d, coll_t cid)
        : store(ns),
          cache(c),
          cid(cid),
          lock("KvsStore::Collection::lock", true, false),
          exists(true),
          onode_map(c, d) {
}

#undef dout_prefix
#define dout_prefix *_dout << "[kvs] "

void KvsStore::prefetch_onode(const coll_t& cid, const ghobject_t *oid) {
  CollectionHandle c_ = _get_collection(cid);
  if(!c_){
    return;
  }
  KvsCollection *c = static_cast<KvsCollection *>(c_.get());
  if (!c->exists) return;
  // look up main onode cache
  {
    RWLock::RLocker l(c->lock);
    OnodeRef o = c->onode_map.lookup(*oid);
    if (o) { return; }
  }

  // prefetch if it does not exist in the prefetch map
  if (c->lookup_prefetch_map(*oid,false) == 0) {
    KvsReadContext *ctx = new KvsReadContext(cct,this);
    ctx->read_onode(*oid);
    ctx->onode = new KvsOnode(c, *oid);

    c->add_to_prefetch_map(*oid, ctx->onode);
    utime_t now = ceph_clock_now();
    ctx->set_prefetch_time(now);
    db.aio_submit_prefetch(ctx);
  }
}

int KvsCollection::get_data(KvsTransContext *txc, const ghobject_t &oid, uint64_t offset, size_t length, bufferlist &bl) {

    bl.clear();
    
    if (txc) {
        // modified objects within a transaction
        auto it = txc->tempbuffers.find(oid);
        if (it != txc->tempbuffers.end()) {

            bufferlist &data = it->second;

             int64_t len = length;

            if (offset + length > data.length() || (offset == 0 && length == 0)) {
                len = (int64_t)data.length() - offset;
                if (len < 0) len = 0;
            }
            if (length > 0)
                bl.append((const char *)((char*)data.c_str() + offset), length);

            //lderr(store->cct)  << "trx: returning" << bl.length()  << dendl;
            return bl.length();
        }
    }

    ReadCacheBufferRef o = onode_map.lookup_data(oid);
    if (o) {
        // read cache hit
        if (offset == 0 && (length == 0 || length >= o->buffer.length())) {
            bl = o->buffer;
            //lderr(store->cct)  << "cache: returning entire range value = " << ceph_str_hash_linux(bl.c_str(), bl.length()) << ", o->buffer" << ceph_str_hash_linux(o->buffer.c_str(), o->buffer.length())<< ", len = " << bl.length()  << dendl;
        } else {
            int64_t maxlength = std::min((int64_t)o->buffer.length() - (int64_t)offset, (int64_t)length);
            if (maxlength < 0) maxlength = 0;
            if (maxlength > 0) bl.append(o->buffer.c_str() + offset, maxlength);
            //lderr(store->cct)  << "cache: returning partial range " << bl.length()  << dendl;
        }
        
        return bl.length();
    }

    // cache miss
    
    //bufferlist buffer;
    KvsReadContext ctx(store->cct);
    ctx.read_data(oid);
    //lderr(store->cct)  << "READ SEND: oid = " << oid << ", key = " << print_key((const char *)ctx.key->key, ctx.key->length) << ", offset = " << offset << ", length = " << length << dendl;
    //store->db.aio_submit(&ctx); 
    bool ispartial = true;
    //int retcode = ctx.read_wait(bl, offset, length, ispartial);
    int retcode = store->db.kv_retrieve_sync(ctx.key, ctx.value, offset, length, bl, ispartial);

    if (retcode == KV_SUCCESS) {
        /*
        int64_t len = length;

        if (offset + length > ctx.value->length || (offset == 0 && length == 0)) {
            len  = (int64_t)ctx.value->length - offset;
            if (len < 0) len = 0;
        }

        //lderr(store->cct)  << "device: size = " << ctx.value->length << ", returning " << len << dendl;
        if (len > 0)
            bl.append((const char *)((char*)ctx.value->value + offset), len);

        const bool ispartial = ctx.value->actual_value_size == 0 || offset != 0 || len != ctx.value->actual_value_size;
        */
        if (!ispartial) {
            ReadCacheBuffer *buf = new ReadCacheBuffer(&onode_map, oid, &bl);
            o.reset(buf);
            onode_map.add_data(oid, o);
        }

    } else  if (retcode == KV_ERR_KEY_NOT_EXIST) {
        return -ENOENT;
    } else {
        //lderr(store->cct) << __func__ << ": IO ERR ret = " << retcode << ", oid = " << oid << ",read key = " << print_key((const char*)ctx.key->key, ctx.key->length) << ", read " << bl.length() << "bytes , value = " << ceph_str_hash_linux(bl.c_str(), bl.length()) << dendl;
        return -EIO;
    }
   // lderr(store->cct)  << "device: returning partial range " << bl.length()  << dendl;
    return bl.length();
}

OnodeRef KvsCollection::get_onode(
        const ghobject_t &oid,
        bool create) {
    FTRACE2
    assert(create ? lock.is_wlocked() : lock.is_locked());

    spg_t pgid;
    if (cid.is_pg(&pgid)) {
        if (!oid.match(cnode.bits, pgid.ps())) {
            lderr(store->cct) << __func__ << " oid " << oid << " not part of "
                              << pgid << " bits " << cnode.bits << dendl;
            ceph_abort();
        }
    }

    { // prefetched?
      KvsOnode* on = lookup_prefetch_map(oid, true);
      if (on) {
          auto hit_type = l_prefetch_onode_cache_hit;
          { // prefetch is not done yet
              std::unique_lock<std::mutex> plock(on->prefetch_lock);
              if (!on->exists) {
                  on->prefetch_cond.wait(plock);
                  hit_type = l_prefetch_onode_cache_slow;
              }
          }
          store->get_counters()->inc(hit_type);
          return onode_map.add(oid, on);
      }
    }

    OnodeRef o = onode_map.lookup(oid);
    if (o) {
      store->get_counters()->inc(l_prefetch_onode_cache_hit);
      return o;
    }

    // cache miss
    store->get_counters()->inc(l_prefetch_onode_cache_miss);

    bufferlist v;
    KvsReadContext ctx(store->cct);;

    ctx.read_onode(oid);
    bool ispartial;
    int ret = store->db.kv_retrieve_sync(ctx.key, ctx.value, 0, 0, v, ispartial);
//    store->db.aio_submit(&ctx);

    PRINTRKEY_CCT(store->cct, ctx.key);

  //  kv_result ret = ctx.read_wait(v);
    KvsOnode *on;
    if (ret == KV_ERR_KEY_NOT_EXIST) {

        if (!create) {
            return OnodeRef();
        }
        // new object, new onode
        on = new KvsOnode(this, oid);
        uint64_t lid = ++store->lid_last;
        on->onode.lid = lid;
        on->onode.size = 0;

    } else if (ret == KV_SUCCESS) {

        // load
        on = new KvsOnode(this, oid);
        on->exists = true;

        // avoid memory copy
        auto v = bufferlist::static_from_mem((char*)ctx.value->value, ctx.value->length);
        bufferptr::iterator p = v.front().begin_deep();
        on->onode.decode(p);


        for (auto &i : on->onode.attrs) {
            i.second.reassign_to_mempool(mempool::mempool_kvsstore_cache_other);
        }

    } else {
        lderr(store->cct) << __func__ << "I/O Error: ret = " << ret << dendl;
        ceph_abort_msg(store->cct, "Failed to read an onode due to an I/O error");
    }
    o.reset(on);

    return onode_map.add(oid, o);
}

void *KvsStore::MempoolThread::entry() {
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


KvsStore::KvsStore(CephContext *cct, const std::string &path)
        : ObjectStore(cct, path), db(cct), kv_callback_thread(this), kv_finalize_thread(this), mempool_thread(this) {
    FTRACE
    m_finisher_num = 1;

    // perf counter
    PerfCountersBuilder b(cct, "KvsStore", l_kvsstore_first, l_kvsstore_last);
    b.add_u64_counter(l_kvsstore_read_lat, "read latency", "read latency");
    b.add_u64_counter(l_kvsstore_queue_to_submit_lat, "queue_to_submit_lat", "queue_to_submit_lat");
    b.add_u64_counter(l_kvsstore_submit_to_complete_lat, "submit_to_complete_lat", "submit_to_complete_lat");
    b.add_u64_counter(l_kvsstore_onode_read_miss_lat, "onode_read_miss_lat", "onode_read_miss_lat");
    b.add_u64(l_kvsstore_pending_trx_ios, "# of pending write I/Os in the device queue", "# of pending write I/Os in the device queue");

    b.add_time_avg(l_kvsstore_read_latency, "read_lat", "Average read latency");
    b.add_time_avg(l_kvsstore_write_latency, "write_lat", "Average write latency");
    b.add_time_avg(l_kvsstore_tr_latency, "tr_lat", "Average transaction latency");
    b.add_time_avg(l_kvsstore_delete_latency, "delete_lat", "Average delete latency");

    // measute prefetch onode cache hit and miss
    b.add_u64_counter(l_prefetch_onode_cache_hit, "prefetch_onode_cache_hit", "# of onode cache hit");
    b.add_u64_counter(l_prefetch_onode_cache_slow, "prefetch_onode_cache_wait", "# of onode cache waits");
    b.add_u64_counter(l_prefetch_onode_cache_miss, "prefetch_onode_cache_miss", "# of onode cache miss");

    logger = b.create_perf_counters();
    cct->get_perfcounters_collection()->add(logger);

    logger->set(l_kvsstore_pending_trx_ios, 0);

    // create onode LRU cache
    set_cache_shards(2);
}

KvsStore::~KvsStore() {
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
    cache_shards.resize(num);

    for (unsigned i = old; i < num; ++i) {
        cache_shards[i] = KvsCache::create(cct, logger);
    }

}

/// -------------------
///  MOUNT
/// -------------------

int KvsStore::mount() {
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

    r = _fsck();
    if (r < 0)
        goto out_db;

    // load lid_last for atomic accesses
    this->lid_last = this->kvsb.lid_last;

    // to update superblock
    this->kvsb.is_uptodate = 0;
    r = _write_sb();
    if (r < 0)
        goto out_db;

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


bool compare_journal_key(const kvs_journal_key *a, const kvs_journal_key *b) {
    return a->index > b->index;
}

int KvsStore::_kvs_replay_journal(kvs_journal_key *j) {
    // read a journal entry je
    KvsReadContext ctx(cct);
    ctx.read_journal(j);
    this->db.aio_submit(&ctx);

    kv_result ret = ctx.read_wait();
    if (ret != 0) return -1;

    char *curpos = (char *) ctx.value->value;
    const char *endpos = curpos + ctx.value->length;

    while (curpos < endpos) {
        uint64_t lid;
        KvsSyncWriteContext wctx(cct);
        curpos = wctx.write_journal_entry(curpos, lid);
        db.aio_submit(&wctx);

        ret = wctx.write_wait();
        if (ret != 0) {
            derr << "error: writing a journal entry" << dendl;
            return -1;
        }

        // if je.lid > this->kvsb.lid_last, update this->kvsb.lid_last;
        if (lid > this->kvsb.lid_last) {
            this->kvsb.lid_last = lid;
        }
    }
    return 0;
}
int KvsStore::_fsck_with_mount() {
    int r = this->mount();  // includes fsck
    if (r < 0) return r;
    r = this->umount();
    if (r < 0) return r;
    return 0;
}

int KvsStore::_fsck() {
    FTRACE
    kv_iter_context iter_ctx;
    std::set<kvs_journal_key *> keyset;
    std::list<kvs_journal_key *> keylist;
    std::list<std::pair<void *, int> > buflist;

    int ret = _read_sb();
    if (ret < 0) return ret;

    iter_ctx.prefix = GROUP_PREFIX_JOURNAL;
    iter_ctx.bitmask = 0xFFFFFFFF;
    iter_ctx.buflen = ITER_BUFSIZE;

    // check if there's any opened iterators
    for (int i = 0; i < 3; i++) {
        iter_ctx.handle = i;
        db.iter_close(&iter_ctx);
    }

    // read collections
    ret = db.iter_readall(&iter_ctx, buflist);
    if (ret != 0) return ret;

    void *key;
    int length;

    for (const auto &p : buflist) {
        iterbuf_reader reader(cct, p.first, p.second, &db);

        while (reader.nextkey(&key, &length)) {
            if (length > 255) break;
            kvs_journal_key *k = (kvs_journal_key *) key;
            if (k->group == GROUP_PREFIX_JOURNAL)
                keylist.push_back(k);
        }
    }

    keylist.sort(compare_journal_key);

    for (kvs_journal_key *k : keylist) {
        keyset.insert(k);
    }

    for (kvs_journal_key *k : keyset) {

        // replay
        if (this->kvsb.is_uptodate == 0) {
            _kvs_replay_journal(k);  // update kvsb->lid_last;
        }

        // delete journal
        KvsSyncWriteContext del_txc(cct);
        del_txc.delete_journal_key(k);
        db.aio_submit(&del_txc);
        int ret = del_txc.write_wait();
        if (ret != 0 && ret != 784) {
            derr << "error: deleting a journal key, ret = " << ret << dendl;
            return ret;
        }
    }

    keylist.clear();
    keyset.clear();

    for (const auto &p : buflist) {
        free(p.first);
    }
    return 0;
}

int KvsStore::umount() {
    FTRACE

    assert(mounted);

    _osr_drain_all();
    _osr_unregister_all();

    mounted = false;

    this->kvsb.is_uptodate = 1;
    this->kvsb.lid_last = this->lid_last;   // atomic -> local

    int r = _write_sb();
    if (r < 0)
        derr << __func__ << "err: could not store a superblock, closing anyway .. retcode = " << r << dendl;

    mempool_thread.shutdown();

    dout(20) << __func__ << " stopping kv thread" << dendl;
    _close_db();
    _reap_collections();
    _flush_cache();

    _close_fsid();
    _close_path();

    return 0;
}


void KvsStore::_osr_drain_all() {
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


void KvsStore::_osr_unregister_all() {
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



int KvsStore::_open_collections() {
    FTRACE
    kv_result ret = 0;
    void *key;
    int length;

    std::list<std::pair<void *, int> > buflist;
    std::unordered_set<std::string> keylist;

    kv_iter_context iter_ctx;
    iter_ctx.prefix = GROUP_PREFIX_COLL;
    iter_ctx.bitmask = 0xFFFFFFFF;
    iter_ctx.buflen = ITER_BUFSIZE;
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

        iterbuf_reader reader(cct, p.first, p.second, &db);

        while (reader.nextkey(&key, &length)) {

            if (length > 255) break;
            if (((kvs_coll_key*)key)->group == GROUP_PREFIX_COLL) {
                //derr << "open_collections " << print_key((char*)key, length) << dendl;
                keylist.insert(std::string((char *) key, length));
            }
        }

    }


    if (keylist.size() == 0) {
        ret = 0;
        goto release;
    }

    // load the keys
    for (const auto &p : keylist) {

        kv_key iterkey;
        iterkey.key = (void*)p.c_str();
        iterkey.length = p.length();

        bufferlist bl;
        kv_result res = db.sync_read(&iterkey, bl, ITER_BUFSIZE);

        if (res == 0 && bl.length() > 0) {
            coll_t cid;
            struct kvs_coll_key *collkey = (struct kvs_coll_key *) iterkey.key;

            std::string name(collkey->name, iterkey.length - 5);
            if (cid.parse(name)) {
                //derr << " found collection " << name << dendl;
                CollectionRef c(new KvsCollection(this, cache_shards[0], cache_shards[1], cid));

                bufferlist::iterator p = bl.begin();
                try {
                    ::decode(c->cnode, p);

                } catch (buffer::error &e) {
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


int KvsStore::mkfs() {
    FTRACE

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

    this->kvsb.is_uptodate = 1;
    r = _write_sb();
    if (r < 0)
        goto out_close_db;

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
    //derr << "mkfs finished: r = " << r << dendl;
    return r;
}


int KvsStore::statfs(struct store_statfs_t *buf) {
    FTRACE
    buf->reset();

    uint64_t bytesused, capacity;
    double utilization;

    db.get_freespace(bytesused, capacity, utilization);
    buf->total =    capacity;
    buf->available = capacity - bytesused;
    return 0;
}

CollectionRef KvsStore::_get_collection(const coll_t &cid) {
    RWLock::RLocker l(coll_lock);
    ceph::unordered_map<coll_t, CollectionRef>::iterator cp = coll_map.find(cid);
    if (cp == coll_map.end()) {
        return CollectionRef();
    }

    return cp->second;
}


ObjectStore::CollectionHandle KvsStore::open_collection(const coll_t &cid) {
    return _get_collection(cid);
}

bool KvsStore::exists(const coll_t &cid, const ghobject_t &oid) {
    FTRACE
    CollectionHandle c = _get_collection(cid);
    if (!c)
        return false;
    return exists(c, oid);
}

bool KvsStore::exists(CollectionHandle &c_, const ghobject_t &oid) {
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
        const coll_t &cid,
        const ghobject_t &oid,
        struct stat *st,
        bool allow_eio) {
    FTRACE
    CollectionHandle c = _get_collection(cid);
    if (!c)
        return -ENOENT;
    return stat(c, oid, st, allow_eio);
}

int KvsStore::stat(
        CollectionHandle &c_,
        const ghobject_t &oid,
        struct stat *st,
        bool allow_eio) {
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
        st->st_size = o->onode.size;
        st->st_blksize = 4096;
        st->st_blocks = (st->st_size + st->st_blksize - 1) / st->st_blksize;
        st->st_nlink = 1;
    }


    return 0;
}

int KvsStore::set_collection_opts(const coll_t &cid, const pool_opts_t &opts) {
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
        const coll_t &cid,
        const ghobject_t &oid,
        uint64_t offset,
        size_t length,
        bufferlist &bl,
        uint32_t op_flags) {
    FTRACE
    CollectionHandle c = _get_collection(cid);
    if (!c)
        return -ENOENT;

    return read(c, oid, offset, length, bl, op_flags);
}

int KvsStore::read(
        CollectionHandle &c_,
        const ghobject_t &oid,
        uint64_t offset,
        size_t length,
        bufferlist &bl,
        uint32_t op_flags) {
    FTRACE

    KvsCollection *c = static_cast<KvsCollection *>(c_.get());
    const coll_t &cid = c->get_cid();
    dout(15) << __func__ << " " << cid << " " << oid
             << " 0x" << std::hex << offset << "~" << length << std::dec
             << dendl;

    if (!c->exists)
        return -ENOENT;

    RWLock::RLocker l(c->lock);
    return c->get_data(0, oid, offset, length, bl);
}


bool KvsStore::test_mount_in_use() {
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


int KvsStore::fiemap(
        const coll_t& cid,
        const ghobject_t& oid,
        uint64_t offset,
        size_t len,
        bufferlist& bl)
{
    CollectionHandle c = _get_collection(cid);
    if (!c)
        return -ENOENT;
    return fiemap(c, oid, offset, len, bl);
}

int KvsStore::fiemap(
        CollectionHandle &c_,
        const ghobject_t& oid,
        uint64_t offset,
        size_t length,
        bufferlist& bl)
{
    map<uint64_t, uint64_t> m;
    int r = _fiemap(c_, oid, offset, length, m);
    if (r >= 0) {
        ::encode(m, bl);
    }
    return r;
}

int KvsStore::fiemap(
        const coll_t& cid,
        const ghobject_t& oid,
        uint64_t offset,
        size_t len,
        map<uint64_t, uint64_t>& destmap)
{
    CollectionHandle c = _get_collection(cid);
    if (!c)
        return -ENOENT;
    return fiemap(c, oid, offset, len, destmap);
}

int KvsStore::fiemap(
        CollectionHandle &c_,
        const ghobject_t& oid,
        uint64_t offset,
        size_t length,
        map<uint64_t, uint64_t>& destmap)
{
    int r = _fiemap(c_, oid, offset, length, destmap);
    if (r < 0) {
        destmap.clear();
    }
    return r;
}

int KvsStore::_fiemap(
        CollectionHandle &c_,
        const ghobject_t& oid,
        uint64_t offset,
        size_t len,
        map<uint64_t, uint64_t>& destmap)
{
    KvsCollection *c = static_cast<KvsCollection *>(c_.get());
    if (!c->exists)
        return -ENOENT;

    RWLock::RLocker l(c->lock);

    OnodeRef o = c->get_onode(oid, false);
    if (!o || !o->exists) {
        return -ENOENT;
    }

    if (offset > o->onode.size)
        goto out;

    if (offset + len > o->onode.size) {
        len = o->onode.size - offset;
    }

    dout(20) << __func__ << " " << offset << "~" << len << " size "
             << o->onode.size << dendl;

    destmap[0] = o->onode.size;

    out:
    dout(20) << __func__ << " " << offset << "~" << len
             << " size = 0 (" << destmap << ")" << dendl;
    return 0;
}


int KvsStore::getattr(
        const coll_t &cid,
        const ghobject_t &oid,
        const char *name,
        bufferptr &value) {
    FTRACE
    CollectionHandle c = _get_collection(cid);
    if (!c)
        return -ENOENT;
    return getattr(c, oid, name, value);
}

int KvsStore::getattr(
        CollectionHandle &c_,
        const ghobject_t &oid,
        const char *name,
        bufferptr &value) {
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

        if (!o->onode.attrs.count(k)) {
            r = -ENODATA;
            goto out;
        }
        value = o->onode.attrs[k];

        r = 0;
    }
    out:
    return r;
}


int KvsStore::getattrs(
        const coll_t &cid,
        const ghobject_t &oid,
        map<string, bufferptr> &aset) {
    FTRACE
    CollectionHandle c = _get_collection(cid);
    if (!c)
        return -ENOENT;
    return getattrs(c, oid, aset);
}

int KvsStore::getattrs(
        CollectionHandle &c_,
        const ghobject_t &oid,
        map<string, bufferptr> &aset) {
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
        for (auto& i : o->onode.attrs) {
            aset.emplace(i.first.c_str(), i.second);
        }
        r = 0;
    }

    out:
    return r;
}


int KvsStore::list_collections(vector<coll_t> &ls) {
    FTRACE
    RWLock::RLocker l(coll_lock);
    for (ceph::unordered_map<coll_t, CollectionRef>::iterator p = coll_map.begin();
         p != coll_map.end();
         ++p)
        ls.push_back(p->first);
    return 0;
}

bool KvsStore::collection_exists(const coll_t &c) {
    FTRACE
    RWLock::RLocker l(coll_lock);
    return coll_map.count(c);
}

int KvsStore::collection_empty(const coll_t &cid, bool *empty) {
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
    dout(10) << __func__ << " " << cid << " = " << (int) (*empty) << dendl;
    return 0;
}

int KvsStore::collection_bits(const coll_t &cid) {
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
        const coll_t &cid, const ghobject_t &start, const ghobject_t &end, int max,
        vector<ghobject_t> *ls, ghobject_t *pnext) {
    FTRACE
    CollectionHandle c = _get_collection(cid);
    if (!c)
        return -ENOENT;
    return collection_list(c, start, end, max, ls, pnext);
}

int KvsStore::collection_list(
        CollectionHandle &c_, const ghobject_t &start, const ghobject_t &end, int max,
        vector<ghobject_t> *ls, ghobject_t *pnext) {
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
             << ", next = " << (pnext ? *pnext : ghobject_t()) << dendl;
    return r;
}



static void get_coll_key_range(CephContext *cct, const coll_t& cid, int bits, int8_t &shardid, uint64_t &temppoolid, uint64_t &objpoolid,
                               uint32_t &starthash, uint32_t &endhash, uint32_t &tempstarthash, uint32_t &tempendhash)
{
    spg_t pgid;
    if (cid.is_pg(&pgid)) {
        shardid = int8_t(pgid.shard);

        objpoolid  = pgid.pool() + 0x8000000000000000ull;
        starthash  = hobject_t::_reverse_bits(pgid.ps());

        const uint64_t end_hash = hobject_t::_reverse_bits(pgid.ps()) + (1ull << (32-bits));
        if (end_hash <= 0xffffffffull) {
            endhash = end_hash;
        } else {
            endhash = 0xffffffff;
        }

        temppoolid = (-2ll - pgid.pool()) + 0x8000000000000000ull;
        tempstarthash = starthash;
        tempendhash = endhash;

    } else {
        shardid = int8_t(shard_id_t::NO_SHARD);
        objpoolid  = -1ull + 0x8000000000000000ull;
        starthash = 0;
        endhash    = 0xffffffff;

        temppoolid    = objpoolid;
        tempstarthash = endhash;
        tempendhash   = endhash;
    }
}

static bool compare_bitwise_key(uint32_t key, uint32_t starthash, uint32_t endhash, bool inclusive)
{
    if (inclusive)
        return key >= starthash && key <= endhash;
    else
        return key >= starthash && key < endhash;
}


int KvsStore::iterate_objects_in_device(CephContext *cct,
        int8_t shardid, uint64_t poolid, uint32_t starthash, uint32_t endhash, bool inclusive, const ghobject_t &start, const ghobject_t &end, set<ghobject_t> *ls)
{
    int ret = 0;
    void *key;
    int length;
    kv_iter_context iter_ctx;
    std::list<std::pair<void *, int> > buflist;

    iter_ctx.prefix  = get_object_group_id(GROUP_PREFIX_DATA, shardid, poolid);
    iter_ctx.bitmask = 0xFFFFFFFF;
    iter_ctx.buflen  = ITER_BUFSIZE;

    // check if there's any opened iterators
    for (int i = 0; i < 3; i++) {
        iter_ctx.handle = i;
        db.iter_close(&iter_ctx);
    }
    // read collections
    ret = db.iter_readall(&iter_ctx, buflist);
    if (ret != 0) { ret = -1; goto out; }

    // parse the key buffers
    for (const auto &p : buflist) {
        iterbuf_reader reader(cct, p.first, p.second, &db);

        while (reader.nextkey(&key, &length)) {
            if (length > 255) break;

            kvs_var_object_key *collkey = (kvs_var_object_key *) key;

            if (collkey->group == GROUP_PREFIX_DATA && collkey->shardid == shardid && collkey->poolid == poolid
                && compare_bitwise_key(collkey->bitwisekey,starthash,endhash, inclusive)) {

                ghobject_t oid;
                construct_ghobject_t(cct, (const char *) key, length, &oid);


                if (oid >= start && oid < end) {
                    ls->insert(oid);
                }
            }
            else {
                ghobject_t oid;
                construct_ghobject_t(cct, (const char *) key, length, &oid);
            }
        }
    }

out:
    for (const auto &p : buflist) {
        free(p.first);
    }

    return ret;
}

int KvsStore::_collection_list(
        KvsCollection *c, const ghobject_t &start, const ghobject_t &end, int max,
        vector<ghobject_t> *ls, ghobject_t *pnext) {
    FTRACE
    if (!c->exists)
        return -ENOENT;

    kv_result ret = 0;
    bool need_extra_search = false;
    ghobject_t static_next;

    if (!pnext) {
        pnext = &static_next;
    }
    if (!pnext->is_min() && !pnext->is_max()) {
        // use the cached entries
        auto it = c->lsset.upper_bound(*pnext);

        while (it != c->lsset.end()) {
            ls->push_back(*it);
            if ((long)ls->size() >= max) {
                *pnext = *it;
                return 0;
            }
            it++;
        }

        *pnext = ghobject_t::get_max();
        c->lsset.clear();

        return 0;
    }

    int8_t shardid;
    uint64_t poolid;
    uint32_t starthash, endhash = 0;
    uint64_t objectpoolid;
    uint32_t objstarthash, objendhash, tempstarthash, tempendhash;
    bool inclusive;

    if (start == ghobject_t::get_max() || start.hobj.is_max()) {


        goto release;
    }

    { // set search boundary
        bool temp = false;
        uint64_t temppoolid;

        get_coll_key_range(cct, c->cid, c->cnode.bits, shardid, temppoolid, objectpoolid, objstarthash, objendhash, tempstarthash, tempendhash);
        if (start == ghobject_t() || start.hobj == hobject_t() || start == c->cid.get_min_hobj()) {
            poolid    = temppoolid;
            starthash = tempstarthash;
            inclusive = false; // search the keys that are greater than this
            temp = true;
        } else {
            poolid = start.hobj.pool + 0x8000000000000000ull;
            starthash = start.hobj.get_bitwise_key_u32();

            inclusive = true;
            if (start.hobj.is_temp()) {
                temp = true;
            } else {
                temp = false;
            }
        }
        if (!end.hobj.is_max()) {
            endhash = (temp)? tempendhash:endhash;
        } else {
            if (end.hobj.is_temp()) {
                if (temp)
                    endhash = objendhash;
                else
                    goto release;
            }
            else {
                endhash = (temp)? tempendhash:objendhash;
            }
        }

        need_extra_search = (temp && !end.hobj.is_temp());
    }

    c->lsset.clear();

    ret = iterate_objects_in_device(cct, shardid, poolid, starthash, endhash, inclusive, start, end, &c->lsset);

    if (ret == 0 && need_extra_search) {
        poolid = objectpoolid;
        starthash = start.hobj.get_bitwise_key_u32();
        endhash   = objendhash;
        inclusive = false;

        ret = iterate_objects_in_device(cct, shardid, poolid, starthash, endhash, inclusive, start, end, &c->lsset);
    }


release:
    if (ret == 0) {
        if (max >= (long)c->lsset.size()) {
            ls->assign(c->lsset.begin(), c->lsset.end());
            c->lsset.clear();
            *pnext = ghobject_t::get_max();
        } else {
            int i = 0;
            for (const auto &it : c->lsset) {
                ls->push_back(it);
                i++;
                if (i == max) {
                    *pnext = it;
                    break;
                }
            }
        }
    }

    return (ret < 0)? ret:0;
}


// OMAPS

int KvsStore::omap_get(
        const coll_t &cid,                ///< [in] Collection containing oid
        const ghobject_t &oid,   ///< [in] Object containing omap
        bufferlist *header,      ///< [out] omap header
        map<string, bufferlist> *out /// < [out] Key to value map
) {
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
) {
    FTRACE
    KvsCollection *c = static_cast<KvsCollection *>(c_.get());
    dout(15) << __func__ << " " << c->get_cid() << " oid " << oid << dendl;
    if (!c->exists)
        return -ENOENT;

    kv_result ret = 0;
    kv_iter_context iter_ctx;
    std::list<std::pair<void *, int>> buflist;
    std::set<string> keylist;


    RWLock::RLocker l(c->lock);
    int r = 0;
    OnodeRef o = c->get_onode(oid, false);
    if (!o || !o->exists) {
        r = -ENOENT;
        goto release;
    }

    if (!o->onode.has_omap())
        goto release;

    o->flush();

    {
        omap_iterator_init(cct, o->onode.lid, &iter_ctx);

        for (int i = 0; i < 3; i++) {
            iter_ctx.handle = i;
            db.iter_close(&iter_ctx);
        }


        ret = db.iter_readall(&iter_ctx, buflist);
        if (ret != 0) {
            r = -ENOENT;
            goto release;
        }

        r = populate_keylist(cct, o->onode.lid, buflist, keylist, &db);
        if (r == -1) {
            r = 0;
            goto release;

        }


        kv_key *omapkey = KvsMemPool::Alloc_key();

        bufferlist bl;

        *header = bl;

        for (std::set<string>::iterator it = keylist.begin(); it != keylist.end(); ++it) {
            const string &user_key = *it;

            bl.clear();
            construct_omap_key(cct, o->onode.lid, user_key.c_str(), user_key.length(), omapkey);

            kv_result res = db.sync_read(omapkey, bl, ITER_BUFSIZE);

            if (res == 0) {
                if (user_key.length() == 0) {
                    *header = bl;
                } else {
                    (*out)[user_key] = bl;
                }


            } else {
                derr << __func__ << " sync_read failed " << res << dendl;
                continue;
            }

        }

        if (omapkey) {
            KvsMemPool::Release_key(omapkey);
        }
    }

release:
    for (const auto &p : buflist) {
        free(p.first);
    }
    return r;
}

int KvsStore::omap_get_header(
        const coll_t &cid,                ///< [in] Collection containing oid
        const ghobject_t &oid,   ///< [in] Object containing omap
        bufferlist *header,      ///< [out] omap header
        bool allow_eio ///< [in] don't assert on eio
) {
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
) {
    FTRACE
    kv_key *omapkey = 0;
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

    omapkey = KvsMemPool::Alloc_key();
    construct_omap_key(cct, o->onode.lid, 0, 0, omapkey);

    r = db.sync_read(omapkey, *header, ITER_BUFSIZE);
    KvsMemPool::Release_key(omapkey);

out:
    return r;
}


int KvsStore::omap_get_keys(
        const coll_t &cid,              ///< [in] Collection containing oid
        const ghobject_t &oid, ///< [in] Object containing omap
        set<string> *keys      ///< [out] Keys defined on oid
) {
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
) {
    FTRACE
    KvsCollection *c = static_cast<KvsCollection *>(c_.get());
    dout(15) << __func__ << " " << c->get_cid() << " oid " << oid << dendl;
    if (!c->exists)
        return -ENOENT;
    RWLock::RLocker l(c->lock);
    int r = 0;
    OnodeRef o = c->get_onode(oid, false);
    if (!o || !o->exists) {
        return -ENOENT;
    }
    if (!o->onode.has_omap())
        return r;
    o->flush();

    kv_result ret = 0;
    uint64_t lid = o->onode.lid;

    kv_iter_context iter_ctx;
    std::list<std::pair<void *, int>> buflist;

    omap_iterator_init(cct, lid, &iter_ctx);

    for (int i = 0; i < 3; i++) {
        iter_ctx.handle = i;
        db.iter_close(&iter_ctx);
    }

    ret = db.iter_readall(&iter_ctx, buflist);
    if (ret != 0) {
        r = -ENOENT;
        goto release;
    }

    r = populate_keylist(cct, lid, buflist, *keys, &db);
    if (r != 0) {
        r = -ENOENT;
        goto release;
    }

    if (keys->size() > 0){
     auto it = keys->begin();
     if ((*it).length() == 0) {
         keys->erase(it);
     }
    }

release:
    for (const auto &p : buflist) {
        free(p.first);
    }
    return r;
}

int KvsStore::omap_get_values(
        const coll_t &cid,                    ///< [in] Collection containing oid
        const ghobject_t &oid,       ///< [in] Object containing omap
        const set<string> &keys,     ///< [in] Keys to get
        map<string, bufferlist> *out ///< [out] Returned keys and values
) {
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
) {
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
        return  -ENOENT;
    }
    if (!o->onode.has_omap())
        return 0;

    o->flush();

    bufferlist bl;
    kv_key *omapkey = KvsMemPool::Alloc_key();
    if (omapkey == 0) { ceph_abort_msg(cct, "memory allocation failure"); }

    for (set<string>::const_iterator p = keys.begin(); p != keys.end(); ++p) {
        const string user_key = *p;
        construct_omap_key(cct, o->onode.lid, user_key.c_str(), user_key.length(), omapkey);

        bl.clear();

        kv_result res = db.sync_read(omapkey, bl, ITER_BUFSIZE);

        if (res == 0) {
            (*out)[user_key] = bl;
        } else {
            derr << __func__ << "err: omap entry not found: ret= " << res << ", key = " << user_key << dendl;
            continue;
        }
    }

    KvsMemPool::Release_key(omapkey);

    return r;
}

int KvsStore::omap_check_keys(
        const coll_t &cid,                ///< [in] Collection containing oid
        const ghobject_t &oid,   ///< [in] Object containing omap
        const set<string> &keys, ///< [in] Keys to check
        set<string> *out         ///< [out] Subset of keys defined on oid
) {
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
) {
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
        goto release;
    }
    if (!o->onode.has_omap())
        goto release;
    o->flush();
    {
        bufferlist bl;
        kv_key *omapkey = KvsMemPool::Alloc_key();
        if (omapkey == 0) {
            ceph_abort_msg(cct, "memory allocation failure");
        }

        for (set<string>::const_iterator p = keys.begin(); p != keys.end(); ++p) {
            const string user_key = *p;

            construct_omap_key(cct, o->onode.lid, user_key.c_str(), user_key.length(), omapkey);

            bl.clear();

            kv_result res = db.sync_read(omapkey, bl, ITER_BUFSIZE);
            if (res == 0) {
                out->insert(user_key);
            }
        }

        KvsMemPool::Release_key(omapkey);
    }
    release:

    return r;
}

ObjectMap::ObjectMapIterator KvsStore::get_omap_iterator(
        const coll_t &cid,              ///< [in] collection
        const ghobject_t &oid  ///< [in] object
) {
    FTRACE
    CollectionHandle c = _get_collection(cid);
    if (!c) {
        dout(10) << __func__ << " " << cid << "doesn't exist" << dendl;
        return ObjectMap::ObjectMapIterator();
    }
    return get_omap_iterator(c, oid);
}

ObjectMap::ObjectMapIterator KvsStore::_get_omap_iterator(
        KvsCollection *c,   ///< [in] collection
        OnodeRef &o  ///< [in] object
)
{
    o->flush();

    kv_iter_context iter_ctx;

    for (int i = 0; i < 3; i++) {
        iter_ctx.handle = i;
        db.iter_close(&iter_ctx);
    }

    KvsOmapIterator *impl  = _get_kvsomapiterator(c, o);
    if (impl == 0)
        return ObjectMap::ObjectMapIterator();

    return ObjectMap::ObjectMapIterator(new KvsOmapIteratorImpl(c, impl));
}

KvsOmapIterator* KvsStore::_get_kvsomapiterator(KvsCollection *c, OnodeRef &o) {

    kv_iter_context iter_ctx;

    KvsOmapIterator *impl = new KvsOmapIterator(c, o, this);

    omap_iterator_init(cct, o->onode.lid, &iter_ctx);

    int ret = db.iter_readall(&iter_ctx, impl->buflist);

    if (ret != 0) {
        delete impl;
        return 0;
    }
    populate_keylist(cct, o->onode.lid, impl->buflist, impl->keylist, &db);
    impl->makeready();
    return impl;
}

ObjectMap::ObjectMapIterator KvsStore::get_omap_iterator(
        CollectionHandle &c_,              ///< [in] collection
        const ghobject_t &oid  ///< [in] object
) {
    FTRACE
    KvsCollection *c = static_cast<KvsCollection *>(c_.get());
    dout(10) << __func__ << " " << c->get_cid() << " " << oid << dendl;
    if (!c->exists) {
        return ObjectMap::ObjectMapIterator();
    }

    RWLock::RLocker l(c->lock);
    OnodeRef o = c->get_onode(oid, false);
    if (!o || !o->exists) {
        dout(10) << __func__ << " " << oid << "doesn't exist" << dendl;
        return ObjectMap::ObjectMapIterator();
    }
    return _get_omap_iterator(c, o);


}


/// -------------------
///  WRITE I/O
/// -------------------


KvsTransContext *KvsStore::_txc_create(KvsOpSequencer *osr) {
    FTRACE
    KvsTransContext *txc = new KvsTransContext(cct, this, osr);
    osr->queue_new(txc);
    dout(20) << __func__ << " osr " << osr << " = " << txc
             << " seq " << txc->seq << dendl;
    return txc;
}


void KvsStore::_txc_aio_submit(KvsTransContext *txc) {
    FTRACE
    for (auto &it : txc->tempbuffers) {
        txc->ioc.add_userdata(it.first, it.second);
    }

    db.aio_submit(txc);
}

// write callback
void KvsStore::txc_aio_finish(kv_io_context *op, KvsTransContext *txc) {
    FTRACE
    if (op->retcode != KV_SUCCESS && op->retcode != 784) {
        derr << "I/O failed ( write_callback ): op " << op->opcode  << ", retcode = " << op->retcode << dendl;
        ceph_abort_msg(cct, "write failed: disk full?");
    }



    if (logger)
        logger->dec(l_kvsstore_pending_trx_ios, 1);

    if (--txc->ioc.num_running == 0) {
        logger->tinc(l_kvsstore_tr_latency,
                     ceph_clock_now() - txc->ioc.start);

        // last I/O -> proceed the transaction status
        txc->store->_txc_state_proc(txc);
    }
}


void KvsStore::_txc_state_proc(KvsTransContext *txc) {
    FTRACE
    while (true) {
        switch (txc->state) {
            case KvsTransContext::STATE_PREPARE:

                if (txc->ioc.has_pending_aios()) {
                    txc->state = KvsTransContext::STATE_AIO_WAIT;
                    txc->had_ios = true;
                    _txc_aio_submit(txc);
                    return;
                }
                // ** fall-thru **

            case KvsTransContext::STATE_AIO_WAIT:
                /* called by kv_callback_thread */
                _txc_finish_io(txc);
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


void KvsStore::_txc_finish_io(KvsTransContext *txc) {
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


void KvsStore::_txc_committed_kv(KvsTransContext *txc) {
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


void KvsStore::_txc_finish(KvsTransContext *txc) {
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

void KvsStore::_txc_release_alloc(KvsTransContext *txc) {
    FTRACE
    KvsIoContext *ioc = &txc->ioc;
   {
        std::unique_lock<std::mutex> lk(ioc->running_aio_lock);
        // release memory
        for (const auto &p : ioc->running_aios) {
            KvsMemPool::Release_key(p.first);
            if (p.second)
                KvsMemPool::Release_value(p.second);
        }
    }

    txc->onodes.clear();
}


void KvsStore::_txc_write_nodes(KvsTransContext *txc) {
    FTRACE
    dout(20) << __func__ << " txc " << txc
             << " onodes " << txc->onodes
             << dendl;

    // finalize onodes
    for (auto o : txc->onodes) {
        if (!o->exists) continue;
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

        txc->ioc.add_onode(o->oid, bl);
    }
}

void KvsStore::_kv_finalize_thread() {
    FTRACE
    deque<KvsTransContext *> kv_committed;

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

            while (!kv_committed.empty()) {
                KvsTransContext *txc = kv_committed.front();

                assert(txc->state == KvsTransContext::STATE_FINISHING);
                _txc_state_proc(txc);
                kv_committed.pop_front();
            }

            // this is as good a place as any ...
            _reap_collections();

            l.lock();
        }
    }

    kv_finalize_started = false;

}


void KvsStore::_kv_callback_thread() {
    FTRACE
    assert(!kv_callback_started);
    kv_callback_started = true;

    uint32_t toread = 2048;

    while (!kv_stop) {
        if (this->db.is_opened()) {
            this->db.poll_completion(toread, 1000000);
        }
    }

    kv_callback_started = false;
}

void KvsStore::_queue_reap_collection(CollectionRef &c) {
    FTRACE
    dout(10) << __func__ << " " << c << " " << c->cid << dendl;
    std::lock_guard<std::mutex> l(reap_lock);
    removed_collections.push_back(c);
}


void KvsStore::_reap_collections() {
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
        vector<Transaction> &tls,
        TrackedOpRef op,
        ThreadPool::TPHandle *handle) {
    static std::mutex journal_index_lock;
    static const uint32_t MAX_JOURNAL_INDEX = 5000;
    static uint32_t journal_index;
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

    // meta journaling

    uint32_t cur_journal_index;
    {
        std::lock_guard<std::mutex> lock(journal_index_lock);
        cur_journal_index = journal_index++;
        if (journal_index == MAX_JOURNAL_INDEX) journal_index = 0;
    }

    _txc_journal_meta(txc, cur_journal_index);

    // execute (start)
    _txc_state_proc(txc);

    return 0;
}

void KvsStore::_txc_journal_meta(KvsTransContext *txc, uint64_t index) {
    KvsSyncWriteContext ctx(cct);
    ctx.write_journal(index, txc->ioc.journal_entries);
    db.aio_submit(&ctx);
    kv_result ret = ctx.write_wait();

    if (ret != KV_SUCCESS) {
        derr << "write failed, error = " << ret << dendl;
        ceph_abort_msg(cct, "_txc_journal_meta - journal write failed");
    }
}


void KvsStore::_txc_add_transaction(KvsTransContext *txc, Transaction *t) {
    FTRACE

    Transaction::iterator i = t->begin();

    vector<CollectionRef> cvec(i.colls.size());

    unsigned j = 0;
    for (vector<coll_t>::iterator p = i.colls.begin(); p != i.colls.end();
         ++p, ++j) {
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
            case Transaction::OP_RMCOLL: {
                const coll_t &cid = i.get_cid(op->cid);
                r = _remove_collection(txc, cid, &c);
                if (!r)
                    continue;
            }
                break;

            case Transaction::OP_MKCOLL: {
                const coll_t &cid = i.get_cid(op->cid);

                r = _create_collection(txc, cid, op->split_bits, &c);
                if (!r)
                    continue;
            }
                break;

            case Transaction::OP_SPLIT_COLLECTION:
                assert(0 == "deprecated");
                break;

            case Transaction::OP_SPLIT_COLLECTION2:
            {
                uint32_t bits = op->split_bits;
                uint32_t rem = op->split_rem;
                r = _split_collection(txc, c, cvec[op->dest_cid], bits, rem);
                if (!r)
                    continue;
            }
                break;

            case Transaction::OP_COLL_HINT: {
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

            case Transaction::OP_WRITE: {
                uint64_t off = op->off;
                uint64_t len = op->len;
                uint32_t fadvise_flags = i.get_fadvise_flags();
                bufferlist bl;
                i.decode_bl(bl);
                r = _write(txc, c, o, off, len, &bl, fadvise_flags);
            }
                break;

            case Transaction::OP_ZERO: {
                uint64_t off = op->off;
                uint64_t len = op->len;
                r = _zero(txc, c, o, off, len);
            }
                break;

            case Transaction::OP_TRIMCACHE: {
                // deprecated, no-op
            }
                break;

            case Transaction::OP_TRUNCATE: {
                uint64_t off = op->off;
                r = _truncate(txc, c, o, off);
            }
                break;

            case Transaction::OP_REMOVE: {
                r = _remove(txc, c, o);
            }
                break;

            case Transaction::OP_SETATTR: {
                string name = i.decode_string();
                bufferptr bp;
                i.decode_bp(bp);
                r = _setattr(txc, c, o, name, bp);
            }
                break;

            case Transaction::OP_SETATTRS: {
                map<string, bufferptr> aset;
                i.decode_attrset(aset);
                r = _setattrs(txc, c, o, aset);
            }
                break;

            case Transaction::OP_RMATTR: {
                string name = i.decode_string();
                r = _rmattr(txc, c, o, name);
            }
                break;

            case Transaction::OP_RMATTRS: {
                r = _rmattrs(txc, c, o);
            }
                break;

            case Transaction::OP_CLONE: {
                OnodeRef& no = ovec[op->dest_oid];
                if (!no) {
                    const ghobject_t& noid = i.get_oid(op->dest_oid);
                    no = c->get_onode(noid, true);
                }
                r = _clone(txc, c, o, no);
            }
                break;

            case Transaction::OP_CLONERANGE:
                assert(0 == "deprecated");
                break;

            case Transaction::OP_CLONERANGE2: {
                OnodeRef& no = ovec[op->dest_oid];
                if (!no) {
                    const ghobject_t& noid = i.get_oid(op->dest_oid);
                    no = c->get_onode(noid, true);
                }
                uint64_t srcoff = op->off;
                uint64_t len = op->len;
                uint64_t dstoff = op->dest_off;
                r = _clone_range(txc, c, o, no, srcoff, len, dstoff);
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
            case Transaction::OP_TRY_RENAME: {
                assert(op->cid == op->dest_cid);
                const ghobject_t& noid = i.get_oid(op->dest_oid);
                OnodeRef& no = ovec[op->dest_oid];
                if (!no) {
                    no = c->get_onode(noid, true);
                }
                r = _rename(txc, c, o, no, noid);
                //derr << "rename = " << r << dendl;
            }
                break;

            case Transaction::OP_OMAP_CLEAR: {
                r = _omap_clear(txc, c, o);
            }
                break;
            case Transaction::OP_OMAP_SETKEYS: {
                bufferlist aset_bl;
                i.decode_attrset_bl(&aset_bl);
                r = _omap_setkeys(txc, c, o, aset_bl);
            }
                break;
            case Transaction::OP_OMAP_RMKEYS: {
                bufferlist keys_bl;
                i.decode_keyset_bl(&keys_bl);
                r = _omap_rmkeys(txc, c, o, keys_bl);
            }
                break;
            case Transaction::OP_OMAP_RMKEYRANGE: {
                string first, last;
                first = i.decode_string();
                last = i.decode_string();
                r = _omap_rmkey_range(txc, c, o, first, last);
            }
                break;
            case Transaction::OP_OMAP_SETHEADER: {
                bufferlist bl;
                i.decode_bl(bl);
                r = _omap_setheader(txc, c, o, bl);
            }
                break;

            case Transaction::OP_SETALLOCHINT: {
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

            if (r == -E2BIG && (op->op == Transaction::OP_WRITE || Transaction::OP_TRUNCATE))
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

                derr << __func__ << " error: code = " << r << "(" << cpp_strerror(r)
                     << ") not handled on operation " << op->op
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
                     CollectionRef &c,
                     OnodeRef &o) {
    FTRACE
    dout(15) << __func__ << " " << c->cid << " " << o->oid << dendl;

    int r = this->_write(txc, c, o, 0, 0, 0, 0);

    dout(10) << __func__ << " " << c->cid << " " << o->oid << " = " << r << dendl;
    return r;
}



void _update_buffer(CephContext *cct, bufferlist &data, uint64_t offset, uint64_t length, bufferlist *towrite, bool truncate) {
    if (length == 0) {
        data.clear();
        return;
    }

    if (data.length() == 0) {
        if (offset > 0) {
            data.append_zero(offset);
        }
        if (towrite) {
            data.append(towrite->c_str(), length);
        } else {
            data.append_zero(length);
        }

    } else if (offset + length <= data.length()) {
        // offset-in-range
        if (truncate) {
            unsigned len = data.length() - length;
            if (len > 0)
                data.splice(length, len);
        } else {

            if (towrite) {
                data.copy_in(offset, length, *towrite);

            } else {
                data.zero(offset, length);

            }
        }
    }
    else {
        if (truncate) {
            const int64_t length_to_end = (int64_t)data.length() - (int64_t)length;
            if (length_to_end < 0) {
                data.append_zero(abs(length_to_end));
            } else {
                data.splice(length, length_to_end);
            }

        } else {
           
            const int64_t length_to_end = (int64_t)data.length() - (int64_t)offset;

            if (length_to_end < 0){
                data.append_zero(abs(length_to_end));
                if (towrite) {
                    data.append(towrite->c_str(), length);
                   
                } else {
                    data.append_zero(length);
                    
                }
                
            }
            else {
                if (towrite) {
                    const char *newdata = towrite->c_str();
                    data.copy_in(offset, length_to_end, newdata);
                    data.append(newdata + length_to_end, length - length_to_end);
                } else {
                    data.zero(offset, length_to_end);
                    data.append_zero(length - length_to_end);
                }
            }
        }
    }

}

int KvsStore::_rename(KvsTransContext *txc, CollectionRef& c,
            OnodeRef& oldo, OnodeRef& newo,
            const ghobject_t& new_oid)
{
    dout(10) << __func__
        << " cid = " << c->cid
        << ", old->oid =  " << oldo->oid << " -> "
        << ", new->oid =  " << newo->oid
        << dendl;

    int r = 0;

    ghobject_t old_oid = oldo->oid;
    bufferlist bl;
    string old_key, new_key;

    if(newo) {
        if (newo->exists) {
            r = -EEXIST;
            if (r < 0) {
                derr << __func__ << " New Object " << new_oid << " exists"
                     << " r = " << r << dendl;
                goto release;
            }
        }
    }

    // copy old object to new object
    r = this->_clone(txc, c, oldo, newo);
    if (r < 0) {
        derr << __func__ << " clone failed" << r << dendl;
        return r;
    }

    /*derr << "oldo->length = " << oldo->onode.size << dendl;
    derr << "newo->length = " << newo->onode.size << dendl;*/

    r = _do_remove(txc, c, oldo);
    if (r < 0){
        derr << __func__ << " _do_remove old object = " << oldo->oid
             << ", r = " << r << dendl;
        goto release;
    }

    r = 0;

release:
    /*derr << __func__ << " cid = " << c->cid << ", old->oid =  " << oldo->oid << " -> "
         << ", new_oid "<< new_oid << ", ret = " << r << dendl;*/

    return r;
}

int KvsStore::_update_write_buffer(OnodeRef &o, uint64_t offset, size_t length, bufferlist *towrite, bufferlist &out, bool truncate)
{
    //derr << "update write buffer oid " << o->oid << ", offset = " << offset << ", length = " << length << ", existing = " << ceph_str_hash_linux(out.c_str(), out.length()) << ", existing vallen = " << out.length() << ", truncate?"<< truncate << dendl;
    _update_buffer(cct, out, offset, length, towrite, truncate);
    o->onode.size = out.length();
    o->exists = true;
    //derr << "update write buffer oid " << o->oid << ", value = " << ceph_str_hash_linux(out.c_str(), out.length()) << ", vallen = " << out.length() << dendl;
    
    return 0;
}


int KvsStore::_write(KvsTransContext *txc,
                     CollectionRef &c,
                     OnodeRef &o,
                     uint64_t offset, size_t length,
                     bufferlist *bl,    /* write zero if null */
                     uint32_t fadvise_flags, bool truncate) {
    FTRACE

    dout(10) << __func__ << " " << c->cid << " " << o->oid << ","
             << offset << "~" << length 
             << dendl;
    int r = 0;

    if (offset + length > KVS_OBJECT_MAX_SIZE) {
        derr << "object is too large: requested:  " << offset + length << dendl;
        return -E2BIG;
    }

    auto it = txc->tempbuffers.find(o->oid);

    if (it != txc->tempbuffers.end()) {
        // previous written in this transaction
        r = _update_write_buffer(o, offset, length, bl, it->second, truncate);
    }
    else {
        bufferlist &data = txc->tempbuffers[o->oid];

        if ((length > 0 && (offset != 0 || o->onode.size > length)) || truncate) {
            // read previously stored
            bool ispartial;
            KvsReadContext ctx(cct);
            ctx.read_data(o->oid);
            r = db.kv_retrieve_sync(ctx.key, ctx.value, 0,0, data, ispartial);
            if (r != 0 && r != 784) { return r; }
            
        }
        if (length != 0 || data.length() == 0) {
            r = _update_write_buffer(o, offset, length, bl, data, truncate);
        }
    }

    txc->write_onode(o);

    {
        // invalidate the read cache
        KvsCollection *kc = static_cast<KvsCollection *>(c->get());
        kc->onode_map.invalidate_data(o->oid);
    }

    dout(10) << __func__ << " " << c->cid << " " << o->oid
             << " 0x" << std::hex << offset << "~" << length << std::dec
             << " = " << r << dendl;

    return r;
}

int KvsStore::_zero(KvsTransContext *txc,
                    CollectionRef &c,
                    OnodeRef &o,
                    uint64_t offset, size_t length) {
    FTRACE
    dout(15) << __func__ << " " << c->cid << " " << o->oid
             << " 0x" << std::hex << offset << "~" << length << std::dec
             << dendl;
    int r = 0;
    if (offset + length >= KVS_OBJECT_MAX_SIZE) {
        derr << "too big " << dendl;
        r = -E2BIG;
    } else {

        r = _do_zero(txc, c, o, offset, length);
    }
    dout(10) << __func__ << " " << c->cid << " " << o->oid
             << " 0x" << std::hex << offset << "~" << length << std::dec
             << " = " << r << dendl;
    return r;
}

int KvsStore::_do_zero(KvsTransContext *txc,
                       CollectionRef &c,
                       OnodeRef &o,
                       uint64_t offset, size_t length) {
    FTRACE
    dout(15) << __func__ << " " << c->cid << " " << o->oid
             << " 0x" << std::hex << offset << "~" << length << std::dec
             << dendl;

    int r = _write(txc, c, o, offset, length, 0, 0);

    return r;
}


int KvsStore::_do_truncate(
        KvsTransContext *txc, CollectionRef &c, OnodeRef o, uint64_t offset) {
    FTRACE
    dout(15) << __func__ << " " << c->cid << " " << o->oid
             << " 0x" << std::hex << offset << std::dec << dendl;

    if (offset == o->onode.size)
        return 0;

    return _write(txc, c, o, 0, offset, 0, 0, true);
}

int KvsStore::_truncate(KvsTransContext *txc,
                        CollectionRef &c,
                        OnodeRef &o,
                        uint64_t offset) {
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
        CollectionRef &c,
        OnodeRef o) {
    FTRACE
    {
        KvsCollection *kc = static_cast<KvsCollection *>(c->get());
        kc->onode_map.invalidate_data(o->oid);
        kc->onode_map.invalidate_onode(o->oid);
    }
    if (o->onode.has_omap()) {
        _do_omap_clear(txc, o);
    }

    auto it = txc->tempbuffers.find(o->oid);
    if (it != txc->tempbuffers.end()) {
        txc->tempbuffers.erase(it);
    }
    o->exists = false;
    txc->ioc.rm_onode(o->oid);
    txc->ioc.rm_data(o->oid);
    txc->removed(o);
    o->onode = kvsstore_onode_t();

    return 0;
}

int KvsStore::_remove(KvsTransContext *txc,
                      CollectionRef &c,
                      OnodeRef &o) {
    FTRACE
    dout(15) << __func__ << " " << c->cid << " " << o->oid << dendl;
    int r = _do_remove(txc, c, o);
    dout(10) << __func__ << " " << c->cid << " " << o->oid << " = " << r << dendl;
    return r;
}

int KvsStore::_setattr(KvsTransContext *txc,
                       CollectionRef &c,
                       OnodeRef &o,
                       const string &name,
                       bufferptr &val) {
    FTRACE
    dout(15) << __func__ << " " << c->cid << " " << o->oid
             << " " << name << " (" << val.length() << " bytes)"
             << dendl;
    int r = 0;

    if (val.is_partial()) {
        auto &b = o->onode.attrs[name.c_str()] = bufferptr(val.c_str(),
                                                     val.length());
        b.reassign_to_mempool(mempool::mempool_kvsstore_cache_other);
    } else {
        auto &b = o->onode.attrs[name.c_str()] = val;
        b.reassign_to_mempool(mempool::mempool_kvsstore_cache_other);
    }
    txc->write_onode(o);
    dout(10) << __func__ << " " << c->cid << " " << o->oid
             << " " << name << " (" << val.length() << " bytes)"
             << " = " << r << dendl;
    return r;
}

int KvsStore::_setattrs(KvsTransContext *txc,
                        CollectionRef &c,
                        OnodeRef &o,
                        const map<string, bufferptr> &aset) {
    FTRACE
    dout(15) << __func__ << " " << c->cid << " " << o->oid
             << " " << aset.size() << " keys"
             << dendl;
    int r = 0;


    for (map<string, bufferptr>::const_iterator p = aset.begin();
         p != aset.end(); ++p) {

        if (p->second.is_partial()) {
            auto &b = o->onode.attrs[p->first.c_str()] =
                              bufferptr(p->second.c_str(), p->second.length());
            b.reassign_to_mempool(mempool::mempool_kvsstore_cache_other);
        } else {
            auto &b = o->onode.attrs[p->first.c_str()] = p->second;
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
                      CollectionRef &c,
                      OnodeRef &o,
                      const string &name) {
    FTRACE
    dout(15) << __func__ << " " << c->cid << " " << o->oid
             << " " << name << dendl;
    int r = 0;
    auto it = o->onode.attrs.find(name.c_str());
    if (it == o->onode.attrs.end())
        goto out;

    o->onode.attrs.erase(it);
    txc->write_onode(o);


out:
    dout(10) << __func__ << " " << c->cid << " " << o->oid
             << " " << name << " = " << r << dendl;
    return r;
}

int KvsStore::_rmattrs(KvsTransContext *txc,
                       CollectionRef &c,
                       OnodeRef &o) {
    FTRACE
    dout(15) << __func__ << " " << c->cid << " " << o->oid << dendl;
    int r = 0;

    if (o->onode.attrs.empty())
        goto out;

    o->onode.attrs.clear();
    txc->write_onode(o);

    out:
    dout(10) << __func__ << " " << c->cid << " " << o->oid << " = " << r << dendl;
    return r;
}

void KvsStore::_do_omap_clear(KvsTransContext *txc, OnodeRef &o) {
    FTRACE
    int r = 0;

    if(!o->onode.has_omap())
        goto release;
    o->flush();
    {
        uint64_t lid = o->onode.lid;

        kv_iter_context iter_ctx;
        std::list<std::pair<void*, int>> buflist;
        std::set<string> keylist;

        omap_iterator_init(cct, lid, &iter_ctx);

        for (int i = 0; i < 3; i++){
                iter_ctx.handle = i;
                db.iter_close(&iter_ctx);
        }

        if (db.iter_readall(&iter_ctx, buflist) < 0) {
            ceph_abort_msg(cct, "iterator read failed");
        };

        r = populate_keylist(cct, lid, buflist, keylist, &db);

        if (r == -1) return ;

        for (auto it = keylist.begin(); it != keylist.end(); ++it){
                string user_key = *it;

                txc->ioc.rm_omap(o->oid, o->onode.lid, user_key);
        }
    }
release:
        return ;
}



int KvsStore::_omap_clear(KvsTransContext *txc,
                          CollectionRef &c,
                          OnodeRef &o) {
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
                            CollectionRef &c,
                            OnodeRef &o,
                            bufferlist &bl) {
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
        txc->ioc.add_omap(o->oid, o->onode.lid, key, value);
    }
    r = 0;
    dout(10) << __func__ << " " << c->cid << " " << o->oid << " = " << r << dendl;
    return r;
}

int KvsStore::_omap_setheader(KvsTransContext *txc,
                              CollectionRef &c,
                              OnodeRef &o,
                              bufferlist &bl) {
    FTRACE
    dout(15) << __func__ << " " << c->cid << " " << o->oid << dendl;
    int r;
    if (!o->onode.has_omap()) {
        o->onode.set_omap_flag();
        txc->write_onode(o);
    }

    string key = "";
    txc->ioc.add_omap(o->oid, o->onode.lid, key, bl);

    r = 0;
    dout(10) << __func__ << " " << c->cid << " " << o->oid << " = " << r << dendl;
    return r;
}

int KvsStore::_omap_rmkeys(KvsTransContext *txc,
                           CollectionRef &c,
                           OnodeRef &o,
                           bufferlist &bl) {
    FTRACE
    dout(15) << __func__ << " " << c->cid << " " << o->oid << dendl;
    int r = 0;
    bufferlist::iterator p = bl.begin();
    __u32 num;

    if (!o->onode.has_omap()) {
        goto out;
    }

    ::decode(num, p);
    while (num--) {
        string key;
        ::decode(key, p);

        txc->ioc.rm_omap(o->oid, o->onode.lid, key);
    }

    out:
    dout(10) << __func__ << " " << c->cid << " " << o->oid << " = " << r << dendl;
    return r;
}

int KvsStore::_omap_rmkey_range(KvsTransContext *txc,
                                CollectionRef &c,
                                OnodeRef &o,
                                const string &first, const string &last) {
    FTRACE
    // TODO: with an iterator
    int r = 0;
    if (!o->onode.has_omap())
        goto release;
    o->flush();
    {
        kv_result ret = 0;
        uint64_t lid = o->onode.lid;

        kv_iter_context iter_ctx;
        std::list<std::pair<void *, int>> buflist;
        std::set<string> keylist;

        omap_iterator_init(cct, lid, &iter_ctx);

        for (int i = 0; i < 3; i++) {
            iter_ctx.handle = i;
            db.iter_close(&iter_ctx);
        }

        ret = db.iter_readall(&iter_ctx, buflist);
        if (ret != 0) return 0;

        r = populate_keylist(cct, lid, buflist, keylist, &db);
        if (r == -1) return 0;

        // End Iterator --

        auto startKey = std::lower_bound(keylist.begin(), keylist.end(), first);
        auto lastKey  = std::lower_bound(keylist.begin(), keylist.end(), last);

        for (auto it = startKey; it != lastKey; ++it) {
            string user_key = *it;
            txc->ioc.rm_omap(o->oid, o->onode.lid, user_key);
        }

    }
    release:
    //derr << __func__ << " " << c->cid << " " << o->oid << " = " << r << dendl;
    return r;
}

int KvsStore::_clone(KvsTransContext *txc,CollectionRef& c, OnodeRef& oldo,OnodeRef& newo)
{
    int r = 0;

    if (oldo->oid.hobj.get_hash() != newo->oid.hobj.get_hash()) {
        derr << __func__ << " mismatched hash on " << oldo->oid
             << " and " << newo->oid << dendl;
        return -EINVAL;
    }

    //derr <<  "clone: old " << oldo->oid << "," << oldo->onode.size << " -> " << newo->oid << dendl;

    oldo->flush();

    // clone data
    bufferlist oldbl;
    r = c->get_data(txc, oldo->oid, 0, 0, oldbl);
    if (r < 0) return r;

   // derr << "original data = " << ceph_str_hash_linux(oldbl.c_str(),  oldbl.length()) << dendl;

//    derr <<  "clone: read =  " << oldbl.length() << dendl;
    r = _write(txc, c, newo, 0, r, &oldbl, 0);
    //derr <<  "clone: newo =  " << newo->onode.size << dendl;

    // clone attrs
    newo->onode.attrs = oldo->onode.attrs;

    // clear newo's omap
    if (newo->onode.has_omap()) {
        dout(20) << __func__ << " clearing old omap data" << dendl;
        newo->flush();
        _do_omap_clear(txc, newo);
    }

    // clone oldo's omap
    if (oldo->onode.has_omap()) {
        dout(20) << __func__ << " copying omap data" << dendl;
        if (!newo->onode.has_omap()) {
            newo->onode.set_omap_flag();
        }

        KvsOmapIterator *it  = _get_kvsomapiterator((KvsCollection*)c->get(), oldo);

        if (it) {

            while (it->valid()) {
                std::string name = it->key();
                bufferlist b = it->value();
                txc->ioc.add_omap(newo->oid, newo->onode.lid, name, b);
                it->next();
            }

            bufferlist hdr;
            if (it->header(hdr)) {
                std::string n = "";
                txc->ioc.add_omap(newo->oid, newo->onode.lid, n, hdr);
            }


            delete it;
        }

    } else {
        newo->onode.clear_omap_flag();
    }

    return r;
}

int KvsStore::_clone_range(KvsTransContext *txc,CollectionRef& c,OnodeRef& oldo,OnodeRef& newo,
                 uint64_t srcoff, uint64_t length, uint64_t dstoff) {
    FTRACE
    int r = 0;
    if (srcoff + length >= KVS_OBJECT_MAX_SIZE ||
        dstoff + length >= KVS_OBJECT_MAX_SIZE) {
        return -E2BIG;
    }


    if (srcoff + length > oldo->onode.size) {
        return -EINVAL;
    }

    if (length > 0) {
        bufferlist oldbl;
        r = c->get_data(txc, oldo->oid, srcoff, length, oldbl);
        if (r < 0) return r;

        r = _write(txc, c, newo, dstoff, oldbl.length(), &oldbl, 0);
        if (r < 0) return r;
    }
    r = 0;
    return r;
}

int KvsStore::_set_alloc_hint(
        KvsTransContext *txc,
        CollectionRef &c,
        OnodeRef &o,
        uint64_t expected_object_size,
        uint64_t expected_write_size,
        uint32_t flags) {
    FTRACE
    return 0;
}


// collections
int KvsStore::_create_collection(
        KvsTransContext *txc,
        const coll_t &cid,
        unsigned bits,
        CollectionRef *c) {
    FTRACE

    int r;
    bufferlist bl;
    {
        RWLock::WLocker l(coll_lock);
        if (*c) {
            derr <<" error exist " << dendl;
            r = -EEXIST;
            goto out;
        }
        c->reset(
                new KvsCollection(
                        this,
                        cache_shards[0],
                        cache_shards[1],
                        cid));
        (*c)->cnode.bits = bits;
        coll_map[cid] = *c;
    }

    ::encode((*c)->cnode, bl);

    txc->ioc.add_coll(cid, bl);
    r = 0;

    out:

    return r;
}

int KvsStore::_remove_collection(KvsTransContext *txc, const coll_t &cid,
                                 CollectionRef *c) {
    FTRACE
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
                derr << __func__ << " " << o->oid << " " << o
                         << " exists in onode_map" << dendl;
                return true;
            }
            ++nonexistent_count;
            return false;
        })) {
            derr << "collection is not empty" << dendl;
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
                if (onode) {
                    exists = onode->exists;
                    if (exists) {
                        dout(10) << __func__ << " " << *it
                                 << " exists in db" << dendl;
                    }
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
                derr << __func__ << " " << cid
                         << " is non-empty" << dendl;
                r = -ENOTEMPTY;
            }
        }
    }

    out:
    dout(10) << __func__ << " " << cid << " = " << r << dendl;
    return r;
}



void KvsStore::_flush_cache() {
    FTRACE
    dout(10) << __func__ << dendl;
    for (auto i : cache_shards) {
        i->trim_all();
        assert(i->empty());
    }
    for (auto &p : coll_map) {
        if (!p.second->onode_map.empty()) {
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
void KvsStore::flush_cache() {
    FTRACE
    dout(10) << __func__ << dendl;
    for (auto i : cache_shards) {
        i->trim_all();
    }
}



/// -------------------
///  PATH & FSID
/// -------------------

int KvsStore::_open_path() {
    FTRACE
    assert(path_fd < 0);
    path_fd = ::open(path.c_str(), O_DIRECTORY);
    if (path_fd < 0) {
        return -errno;
    }
    return 0;
}

void KvsStore::_close_path() {
    FTRACE
    VOID_TEMP_FAILURE_RETRY(::close(path_fd));
    path_fd = -1;
}

int KvsStore::_open_fsid(bool create) {
    FTRACE
    assert(fsid_fd < 0);
    int flags = O_RDWR;
    if (create)
        flags |= O_CREAT;
    fsid_fd = ::openat(path_fd, "fsid", flags, 0644);
    if (fsid_fd < 0) {
        int err = -errno;
        derr << __func__ << " " << cpp_strerror(err) << dendl;
        return err;
    }
    return 0;
}

int KvsStore::_read_fsid(uuid_d *uuid) {
    FTRACE
    char fsid_str[40];
    memset(fsid_str, 0, sizeof(fsid_str));
    int ret = safe_read(fsid_fd, fsid_str, sizeof(fsid_str));
    if (ret < 0) {
        derr << __func__ << " failed: " << cpp_strerror(ret) << dendl;
        return ret;
    }
    if (ret > 36)
        fsid_str[36] = 0;
    else
        fsid_str[ret] = 0;
    if (!uuid->parse(fsid_str)) {
        derr << __func__ << " unparsable uuid " << fsid_str << dendl;
        return -EINVAL;
    }
    return 0;
}

int KvsStore::_write_fsid() {
    FTRACE
    int r = ::ftruncate(fsid_fd, 0);
    if (r < 0) {
        r = -errno;
        derr << __func__ << " fsid truncate failed: " << cpp_strerror(r) << dendl;
        return r;
    }
    string str = stringify(fsid) + "\n";
    r = safe_write(fsid_fd, str.c_str(), str.length());
    if (r < 0) {
        derr << __func__ << " fsid write failed: " << cpp_strerror(r) << dendl;
        return r;
    }
    r = ::fsync(fsid_fd);
    if (r < 0) {
        r = -errno;
        derr << __func__ << " fsid fsync failed: " << cpp_strerror(r) << dendl;
        return r;
    }
    return 0;
}

void KvsStore::_close_fsid() {
    FTRACE
    VOID_TEMP_FAILURE_RETRY(::close(fsid_fd));
    fsid_fd = -1;
}

int KvsStore::_lock_fsid() {
    FTRACE
    struct flock l;
    memset(&l, 0, sizeof(l));
    l.l_type = F_WRLCK;
    l.l_whence = SEEK_SET;
    int r = ::fcntl(fsid_fd, F_SETLK, &l);
    if (r < 0) {
        int err = errno;
        derr << __func__ << " failed to lock " << path << "/fsid"
             << " (is another ceph-osd still running?)"
             << cpp_strerror(err) << dendl;
        return -err;
    }
    return 0;
}


int KvsStore::_open_db(bool create) {
    FTRACE

    for (int i = 0; i < m_finisher_num; ++i) {
        ostringstream oss;
        oss << "kvs-finisher-" << i;
        Finisher *f = new Finisher(cct, oss.str(), "finisher");
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

int KvsStore::_read_sb() {
    bufferlist v;
    KvsReadContext ctx(cct);;
    ctx.read_sb();
    bool ispartial;
    int ret= db.kv_retrieve_sync(ctx.key, ctx.value, 0, 0, v, ispartial);
    
    if (v.length() == 0) {
        derr << "sb not found: ret = " << ret << dendl;
        return -1;
    } else {
        assert(ret == KV_SUCCESS);

        // not in the performance path
        bufferptr::iterator p = v.front().begin_deep();

        this->kvsb.decode(p);

    }
    return 0;
}

int KvsStore::_write_sb() {
    // encode
    bufferlist bl;
    ::encode(this->kvsb, bl);

    KvsSyncWriteContext ctx(cct);;
    ctx.write_sb(bl);
    db.aio_submit(&ctx);

    kv_result ret = ctx.write_wait();

    return ret;
}


void KvsStore::_close_db() {
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



int KvsStore::_split_collection(KvsTransContext *txc,
                                CollectionRef &c,
                                CollectionRef &d,
                                unsigned bits, int rem) {
    FTRACE
    dout(20) << __func__ << " " << c->cid << " to " << d->cid << " "
         << " bits " << bits << dendl;
    RWLock::WLocker l(c->lock);
    RWLock::WLocker l2(d->lock);

    // flush all previous deferred writes on this sequencer.  this is a bit
    // heavyweight, but we need to make sure all deferred writes complete
    // before we split as the new collection's sequencer may need to order
    // this after those writes, and we don't bother with the complexity of
    // moving those TransContexts over to the new osr.
    {
        KvsOpSequencer *osr = txc->osr.get();
        osr->drain_preceding(txc);
    }


    // move any cached items (onodes and referenced shared blobs) that will
    // belong to the child collection post-split.  leave everything else behind.
    // this may include things that don't strictly belong to the now-smaller
    // parent split, but the OSD will always send us a split for every new
    // child.

    spg_t pgid, dest_pgid;
    bool is_pg = c->cid.is_pg(&pgid);
    assert(is_pg);
    is_pg = d->cid.is_pg(&dest_pgid);
    assert(is_pg);

    // the destination should initially be empty.
    assert(d->onode_map.empty());
    assert(d->cnode.bits == bits);

    c->split_cache(d.get());

    // adjust bits.  note that this will be redundant for all but the first
    // split call for this parent (first child).
    c->cnode.bits = bits;
    assert(d->cnode.bits == bits);

    bufferlist bl;
    ::encode(c->cnode, bl);
    txc->ioc.add_coll(c->cid, bl);


    return 0;
}


void KvsCollection::split_cache(KvsCollection *dest)
{
    ldout(store->cct, 10) << __func__ << " to " << dest << dendl;

    // lock (one or both) cache shards
    std::lock(cache->lock, dest->cache->lock);
    std::lock_guard<std::recursive_mutex> l(cache->lock, std::adopt_lock);
    std::lock_guard<std::recursive_mutex> l2(dest->cache->lock, std::adopt_lock);

    int destbits = dest->cnode.bits;
    spg_t destpg;
    bool is_pg = dest->cid.is_pg(&destpg);
    assert(is_pg);

    auto p = onode_map.onode_map.begin();
    while (p != onode_map.onode_map.end()) {
        if (!p->second->oid.match(destbits, destpg.pgid.ps())) {
            // onode does not belong to this child
            ++p;
        } else {
            OnodeRef o = p->second;
            ldout(store->cct, 20) << __func__ << " moving " << o << " " << o->oid
                                  << dendl;

            cache->_rm_onode(p->second);
            p = onode_map.onode_map.erase(p);

            o->c = dest;
            dest->cache->_add_onode(o, 1);
            dest->onode_map.onode_map[o->oid] = o;
            dest->onode_map.cache = dest->cache;

            auto dp = onode_map.data_map.find(o->oid);
            if (dp != onode_map.data_map.end()) {
                dest->onode_map.data_map[o->oid] = dp->second;
                cache->_rm_data(dp->second);
                onode_map.data_map.erase(dp);
            }

        }

    }
}


#if 0
int ret = 0;
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

        CollectionRef c(new KvsCollection(this, cache_shards[0],cache_shards[1], cid));

        if (bl.length() > 0) {

            bufferlist::iterator p = bl.begin();
            try {
                ::decode(c->cnode , p);
            } catch (buffer::error& e) {
                derr << __func__ << " failed to decode cnode" << dendl;
                return -EIO;
            }

        }
        derr << "coll_map added : " << cid << dendl;
        coll_map[cid] = c;
        ret = 0;
    }
    return ret;
    auto endtime = ceph_clock_now();
    double elapsed_sec = (double)(endtime-starttime);

    lderr(store->cct) << "Perf: ops " << num_ops / elapsed_sec << ", throughput: " << (num_ops * 4096)/1024.0/1024.0 / elapsed_sec << dendl;


    
#endif
