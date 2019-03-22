
//
// Created by root on 10/12/18.
//
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <bitset>

#include "os/ObjectStore.h"
#include "osd/osd_types.h"
#include "os/kv.h"
#include "include/compat.h"
#include "include/stringify.h"
#include "common/errno.h"
#include "common/safe_io.h"
#include "common/Formatter.h"
#include "kvsstore_types.h"
#include "kvs_debug.h"
#include "KvsStore.h"

//#define DUMP_IOWORKLOAD

#define dout_context cct
#define dout_subsys ceph_subsys_kvs


KvsOpSequencer::KvsOpSequencer(CephContext* cct, KvsStore *store)
: Sequencer_impl(cct), parent(NULL), store(store) {
    store->register_osr(this);
}


void KvsOpSequencer::_unregister() {
    FTRACE
    if (registered) {
        store->unregister_osr(this);
        registered = false;
    }
}

KvsOpSequencer::~KvsOpSequencer() {
    FTRACE
    assert(q.empty());
    _unregister();
}

void KvsOpSequencer::discard() {
    FTRACE
// Note that we may have txc's in flight when the parent Sequencer
// goes away.  Reflect this with zombie==registered==true and let
// _osr_drain_all clean up later.

    assert(!zombie);
    zombie = true;
    parent = nullptr;
    bool empty;
    {
        std::lock_guard<std::mutex> l(qlock);
        empty = q.empty();
    }
    if (empty) {
        _unregister();
    }
}


void KvsOpSequencer::drain() {
    FTRACE
    std::unique_lock<std::mutex> l(qlock);
    while (!q.empty())
        qcond.wait(l);
}

void KvsOpSequencer::drain_preceding(KvsTransContext *txc) {
    FTRACE
    std::unique_lock<std::mutex> l(qlock);
    while (!q.empty() && &q.front() != txc)
        qcond.wait(l);
}

bool KvsOpSequencer::_is_all_kv_submitted() {
    FTRACE
    // caller must hold qlock
    if (q.empty()) {
        return true;
    }
    KvsTransContext *txc = &q.back();
    if (txc->state >= KvsTransContext::STATE_AIO_WAIT) {
        return true;
    }
    return false;
}

void KvsOpSequencer::flush() {
    FTRACE
    std::unique_lock<std::mutex> l(qlock);
    while (true) {
// set flag before the check because the condition
// may become true outside qlock, and we need to make
// sure those threads see waiters and signal qcond.
        ++kv_submitted_waiters;
        if (_is_all_kv_submitted()) {
            return;
        }
        qcond.wait(l);
        --kv_submitted_waiters;
    }
}

bool KvsOpSequencer::flush_commit(Context *c) {
    FTRACE
    std::lock_guard<std::mutex> l(qlock);
    if (q.empty()) {
        return true;
    }
    KvsTransContext *txc = &q.back();
    if (txc->state >= KvsTransContext::STATE_IO_DONE) {
        return true;
    }
    txc->oncommits.push_back(c);
    return false;
}

KvsCache *KvsCache::create(CephContext* cct, PerfCounters *logger)
{
    KvsCache *c = new KvsLRUCache(cct);
    c->logger = logger;
    c->max_readcache = cct->_conf->kvsstore_readcache_bytes;
    c->max_onodes    = cct->_conf->kvsstore_max_cached_onodes;

    // = 1024*1024*1024
    return c;
}

void KvsCache::trim_all()
{
    std::lock_guard<std::recursive_mutex> l(lock);
    _trim(0, 0);
}

void KvsCache::trim()
{
    std::lock_guard<std::recursive_mutex> l(lock);
    _trim(max_onodes, max_readcache);
}

// LRUCache
#undef dout_prefix
#define dout_prefix *_dout << "KvsStore.LRUCache(" << this << ") "

void KvsLRUCache::_touch_onode(OnodeRef& o)
{
    auto p = onode_lru.iterator_to(*o);
    onode_lru.erase(p);
    onode_lru.push_front(*o);
}

void KvsLRUCache::_touch_data(ReadCacheBufferRef& o)
{
    auto p = buffer_lru.iterator_to(*o);
    buffer_lru.erase(p);
    buffer_lru.push_front(*o);
}

void KvsLRUCache::_trim(uint64_t onode_max, uint64_t buffer_max)
{
    /*derr << __func__ << " onodes " << onode_lru.size() << " / " << onode_max
                    << ", buffers" << buffer_size << "/" << buffer_max << dendl;
    */
    int skipped = 0;
    int max_skipped = 64;

    // free buffers
    auto i = buffer_lru.end();
    if (i != buffer_lru.begin()) {
        --i;
        bool needexit = false;
        // buffers
        while ((buffer_size > buffer_max || buffer_max == 0) && !needexit) {
            ReadCacheBuffer *b = &*i;

            if (b->nref.load() > 1) {
                if (++skipped >= max_skipped) {
                    break;
                }

                if (i == buffer_lru.begin()) {
                    break;
                } else {
                    i--;
                    continue;
                }
            }


            if (i != buffer_lru.begin()) {
                buffer_lru.erase(i--);
            } else {
                buffer_lru.erase(i);
                needexit = true;
            }
            buffer_size -= b->length();

            b->get();  // paranoia
            b->space->remove_data(b->oid);
            b->put();
        }

    }

    // onodes
    int num = onode_lru.size() - onode_max;
    if (num <= 0)
        return; // don't even try

    auto p = onode_lru.end();
    assert(p != onode_lru.begin());
    --p;

    skipped = 0;
    while (num > 0) {
        KvsOnode *o = &*p;
        int refs = o->nref.load();
        if (refs > 1) {
            dout(20) << __func__ << "  " << o->oid << " has " << refs
                     << " refs, skipping" << dendl;
            if (++skipped >= max_skipped) {
                dout(20) << __func__ << " maximum skip pinned reached; stopping with "
                         << num << " left to trim" << dendl;
                break;
            }

            if (p == onode_lru.begin()) {
                break;
            } else {
                p--;
                num--;
                continue;
            }
        }
        dout(20) << __func__ << "  rm " << o->oid << dendl;
        if (p != onode_lru.begin()) {
            onode_lru.erase(p--);
        } else {
            onode_lru.erase(p);
            assert(num == 1);
        }
        o->get();  // paranoia
        o->c->onode_map.remove(o->oid);
        o->put();
        --num;
    }
}




// OnodeSpace

#undef dout_prefix
#define dout_prefix *_dout << "kvsstore.OnodeSpace(" << this << " in " << cache << ") "

OnodeRef KvsOnodeSpace::add(const ghobject_t& oid, OnodeRef o)
{
    std::lock_guard<std::recursive_mutex> l(cache->lock);
    auto p = onode_map.find(oid);
    if (p != onode_map.end()) {
        ldout(cache->cct, 30) << __func__ << " " << oid << " " << o
                              << " raced, returning existing " << p->second
                              << dendl;
        return p->second;
    }
    ldout(cache->cct, 30) << __func__ << " " << oid << " " << o << dendl;
    onode_map[oid] = o;
    cache->_add_onode(o, 1);
    return o;
}

void KvsOnodeSpace::add_data(const ghobject_t &oid, ReadCacheBufferRef b)
{
    std::lock_guard<std::recursive_mutex> l(cache->datalock);
    auto p = data_map.find(oid);
    if (p != data_map.end()) {
        ldout(cache->cct, 30) << __func__ << " " << oid  << " raced" << dendl;
        return;
    }

    ldout(cache->cct, 30) << __func__ << " " << oid << " " << dendl;

    data_map[oid] = b;
    cache->_add_data(b, 1);
}

bool KvsOnodeSpace::invalidate_data(const ghobject_t &oid)
{
    std::lock_guard<std::recursive_mutex> l(cache->datalock);
    auto p = data_map.find(oid);
    if (p != data_map.end()) {
        cache->_rm_data(p->second);
        data_map.erase(p);
        return true;
    }
    return false;
}

bool KvsOnodeSpace::invalidate_onode(const ghobject_t &oid)
{
    std::lock_guard<std::recursive_mutex> l(cache->lock);
    ceph::unordered_map<ghobject_t,OnodeRef>::iterator p = onode_map.find(oid);
    if (p == onode_map.end()) {
        cache->_rm_onode(p->second);
        return true;
    }
    return false;
}

ReadCacheBufferRef KvsOnodeSpace::lookup_data(const ghobject_t &oid)
{
    ldout(cache->cct, 20) << __func__ << dendl;
    ReadCacheBufferRef o;
    {
        std::lock_guard<std::recursive_mutex> l(cache->datalock);
        ceph::unordered_map<ghobject_t,ReadCacheBufferRef>::iterator p = data_map.find(oid);
        if (p == data_map.end()) {
            ldout(cache->cct, 20) << __func__ << " " << oid << " miss" << dendl;
        } else {
            ldout(cache->cct, 20) << __func__ << " " << oid << " hit " << p->second
                                  << dendl;
            cache->_touch_data(p->second);
            o = p->second;
        }
    }

    return o;
}

OnodeRef KvsOnodeSpace::lookup(const ghobject_t& oid)
{
    ldout(cache->cct, 20) << __func__ << dendl;
    OnodeRef o;
    {
        std::lock_guard<std::recursive_mutex> l(cache->lock);
        ceph::unordered_map<ghobject_t,OnodeRef>::iterator p = onode_map.find(oid);
        if (p == onode_map.end()) {
            ldout(cache->cct, 20) << __func__ << " " << oid << " miss" << dendl;
        } else {
            ldout(cache->cct, 20) << __func__ << " " << oid << " hit " << p->second
                                  << dendl;
            cache->_touch_onode(p->second);
            o = p->second;
        }
    }

    return o;
}

void KvsOnodeSpace::clear() {
    {
        std::lock_guard<std::recursive_mutex> l(cache->lock);
        ldout(cache->cct, 10) << __func__ << dendl;
        for (auto &p : onode_map) {
            cache->_rm_onode(p.second);
        }
        onode_map.clear();
    }

    {
        std::lock_guard<std::recursive_mutex> l(cache->datalock);
        ldout(cache->cct, 10) << __func__ << dendl;
        for (auto &p : data_map) {
            cache->_rm_data(p.second);
        }
        data_map.clear();
    }
}

#undef dout_prefix
#define dout_prefix *_dout << "kvsstore.KvsLRUCache "

void KvsLRUCache::_rm_data(ReadCacheBufferRef& o) {
    auto q = buffer_lru.iterator_to(*o);
    buffer_lru.erase(q);
    buffer_size -= o->length();
}


bool KvsOnodeSpace::empty()
{
    bool b = false;
    {
        std::lock_guard<std::recursive_mutex> l(cache->lock);
        b = onode_map.empty();
    }
    if (b)
    {
        std::lock_guard<std::recursive_mutex> l(cache->datalock);
        b = b & data_map.empty();
    }

    return b;
}

bool KvsOnodeSpace::map_any(std::function<bool(OnodeRef)> f)
{
    std::lock_guard<std::recursive_mutex> l(cache->lock);
    ldout(cache->cct, 20) << __func__ << dendl;
    for (auto& i : onode_map) {
        if (f(i.second)) {
            return true;
        }
    }
    return false;
}

void KvsOnodeSpace::dump(CephContext *cct, int lvl)
{
    for (auto& i : onode_map) {
        ldout(cct, lvl) << i.first << " : " << i.second << dendl;
    }
}

#if 0
void KvsOnodeSpace::rename(
        OnodeRef& oldo,
        const ghobject_t& old_oid,
        const ghobject_t& new_oid,
        const mempool::kvsstore_cache_other::string& new_okey)
{
    std::lock_guard<std::recursive_mutex> l(cache->lock);
    ldout(cache->cct, 30) << __func__ << " " << old_oid << " -> " << new_oid
                          << dendl;
    ceph::unordered_map<ghobject_t,OnodeRef>::iterator po, pn;
    po = onode_map.find(old_oid);
    pn = onode_map.find(new_oid);
    assert(po != pn);

    assert(po != onode_map.end());
    if (pn != onode_map.end()) {
        ldout(cache->cct, 30) << __func__ << "  removing target " << pn->second
                              << dendl;
        cache->_rm_onode(pn->second);
        onode_map.erase(pn);
    }
    OnodeRef o = po->second;

    // install a non-existent onode at old location
    oldo.reset(new Onode(o->c, old_oid, o->key));
    po->second = oldo;
    cache->_add_onode(po->second, 1);

    // add at new position and fix oid, key
    onode_map.insert(make_pair(new_oid, o));
    cache->_touch_onode(o);
    o->oid = new_oid;
    o->key = new_okey;
}
#endif


#undef dout_prefix
#define dout_prefix *_dout << "kvsstore.onode(" << this << ")." << __func__ << " "

void KvsOnode::flush()
{
    if (flushing_count.load()) {
        lderr(c->store->cct) << __func__ << " cnt:" << flushing_count << dendl;
        std::unique_lock<std::mutex> l(flush_lock);
        while (flushing_count.load()) {
            flush_cond.wait(l);
        }
    }
}


#undef dout_prefix
#define dout_prefix *_dout << "kvsstore.OmapIteratorImpl "



void omap_iterator_init(CephContext *cct, uint64_t lid, kv_iter_context *iter_ctx){

    kvs_omap_key_header hdr = { GROUP_PREFIX_OMAP, lid};
    iter_ctx->prefix =  ceph_str_hash_linux((char*)&hdr, sizeof(struct kvs_omap_key_header));
    iter_ctx->bitmask = 0xFFFFFFFF;
    iter_ctx->buflen = ITER_BUFSIZE;
}

void print_iterKeys(CephContext *cct, std::map<string, int> &keylist){
	for (std::map<string, int> ::iterator it=keylist.begin(); it!=keylist.end(); ++it){
                derr << __func__ << " Iter keylist: Key = " << (it->first) <<
			" => " << (uint32_t)it->second  << dendl;}
}

int populate_keylist(CephContext *cct, uint64_t lid, std::list<std::pair<void*, int>> &buflist, std::set<string> &keylist, KADI *db) {

	void *key;
        int length;


    for(const auto &p: buflist){

            iterbuf_reader reader(cct, p.first, p.second, db);

            while (reader.nextkey(&key, &length)){

                    if(length > 255 || (char*)key == NULL || length < 14 ) continue;

                    kvs_omap_key* okey = (kvs_omap_key*)key;

                    if (okey->group != GROUP_PREFIX_OMAP || okey->lid != lid) {
                        continue;
                    }


                    if (length == 14) {
                        // header
                        keylist.insert(std::string(""));
                    }
                    else {
                        keylist.insert(std::string(okey->name, length-14));
                    }
            }
    }


    if (keylist.size() == 0){

        return -1;
    }
    return 0;
}

///
/// KvsOmapIteratorImpl
///


KvsOmapIterator::KvsOmapIterator(
        CollectionRef c, OnodeRef o, KvsStore *s)
        : c(c), o(o), store(s)
{
}
void KvsOmapIterator::makeready()
{
    it = keylist.begin();
    // remove header from the list

    if (valid() && (*it).length() == 0) {
        keylist.erase(it);
        it = keylist.begin();
    }

}
int KvsOmapIterator::seek_to_first()
{
    it = keylist.begin();
    if (o->onode.has_omap()){ it = keylist.begin(); }
    else{ it = std::set<string>::iterator(); }
    return 0;
}

 bool KvsOmapIterator::header(bufferlist &hdr)
{
    kv_key *key = KvsMemPool::Alloc_key();

    construct_omap_key(c->store->cct, o->onode.lid, "", 0, key);

    kv_result res = c->store->db.sync_read(key, hdr, ITER_BUFSIZE);

    if (key){ KvsMemPool::Release_key(key); }

    return (res == 0);

}

int KvsOmapIterator::upper_bound(const string& after)
{
    if (o->onode.has_omap()){
        it = std::upper_bound(keylist.begin(), keylist.end(), after);
    }
    else{ it = std::set<string>::iterator();}
    return 0;
}

int KvsOmapIterator::lower_bound(const string& to)
{
    if(o->onode.has_omap()){
        it = std::lower_bound(keylist.begin(), keylist.end(), to);
    }
    else{
        it = std::set<string>::iterator();
    }
    return 0;
}

bool KvsOmapIterator::valid()
{
    const bool iter = (it != keylist.end() && it != std::set<string>::iterator());
    return o->onode.has_omap() && iter;
}

int KvsOmapIterator::next(bool validate)
{
    if (!o->onode.has_omap())
        return -1;

    if (it != keylist.end()) {
        ++it;
        return 0;
    }

    return -1;
}

string KvsOmapIterator::key()
{
    if (it != keylist.end() || it != std::set<string>::iterator())
    	 return *it;
    else
         return  "";
}

bufferlist KvsOmapIterator::value()
{
    bufferlist output;

    if(!(it != keylist.end() || it != std::set<string>::iterator())) {
        return output;
    }

    kv_key *key = KvsMemPool::Alloc_key();
    if (key == 0){	lderr(c->store->cct) << __func__ << "key = " << key << dendl; exit(1); }

    const string user_key = *it;
	construct_omap_key(c->store->cct, o->onode.lid, user_key.c_str(), user_key.length(), key);

	kv_result res = c->store->db.sync_read(key, output, ITER_BUFSIZE);
	if (res != 0) {
        lderr(c->store->cct) << __func__ << ": sync_read failed res = " << res << ", bufferlist length = " << output.length()  << dendl;
        goto release;
    }

release:
	if (key){ KvsMemPool::Release_key(key); }

    return output;
}


KvsOmapIteratorImpl::KvsOmapIteratorImpl(
        CollectionRef c, KvsOmapIterator *it_)
        : c(c), it(it_)
{

}

int KvsOmapIteratorImpl::seek_to_first()
{
    RWLock::RLocker l(c->lock);
    return it->seek_to_first();
}

int KvsOmapIteratorImpl::upper_bound(const string& after)
{
    RWLock::RLocker l(c->lock);
    return it->upper_bound(after);

}

int KvsOmapIteratorImpl::lower_bound(const string& to)
{
    RWLock::RLocker l(c->lock);
    return it->lower_bound(to);
}

bool KvsOmapIteratorImpl::valid()
{
    RWLock::RLocker l(c->lock);
    return it->valid();
}

int KvsOmapIteratorImpl::next(bool validate)
{
    RWLock::RLocker l(c->lock);
    return it->next();
}

string KvsOmapIteratorImpl::key()
{
    RWLock::RLocker l(c->lock);
    return it->key();
}

bufferlist KvsOmapIteratorImpl::value()
{
    RWLock::RLocker l(c->lock);
    return it->value();
}

#undef dout_prefix
#define dout_prefix *_dout << "kvsstore "


///
/// Transaction Contexts
///


void KvsTransContext::aio_finish(kv_io_context *op) {
    store->txc_aio_finish(op, this);
}

///
/// write I/O request handlers
///



static void append_escaped(const string &in, string *out)
{
    char hexbyte[8];
    for (string::const_iterator i = in.begin(); i != in.end(); ++i) {
        if (*i <= '#') {
            snprintf(hexbyte, sizeof(hexbyte), "#%02x", (uint8_t)*i);
            out->append(hexbyte);
        } else if (*i >= '~') {
            snprintf(hexbyte, sizeof(hexbyte), "~%02x", (uint8_t)*i);
            out->append(hexbyte);
        } else {
            out->push_back(*i);
        }
    }
    out->push_back('!');
}


static char *append_escaped(const string &in, char *key)
{
    char *pos = key;
    for (string::const_iterator i = in.begin(); i != in.end(); ++i) {
        if (*i <= '#') {
            pos += snprintf(pos, 8, "#%02x", (uint8_t)*i);

        } else if (*i >= '~') {
            pos += snprintf(pos, 8, "~%02x", (uint8_t)*i);
        } else {
            *pos = *i;
            pos += 1;
        }
    }
    *pos = '!';
    pos += 1;
    return pos;
}

static int decode_escaped(const char *p, string *out)
{
    const char *orig_p = p;
    while (*p && *p != '!') {
        if (*p == '#' || *p == '~') {
            unsigned hex;
            int r = sscanf(++p, "%2x", &hex);
            if (r < 1)
                return -EINVAL;
            out->push_back((char)hex);
            p += 2;
        } else {
            out->push_back(*p++);
        }
    }
    return p - orig_p;
}


void encode_nspace_oid(const std::string &nspace, const std::string &oidkey, const std::string &oidname, std::string *out)
{
    out->clear();
    append_escaped(nspace, out);

    if (oidkey.length()) {
        // is a key... could be < = or >.
        // (ASCII chars < = and > sort in that order, yay)
        if (oidkey < oidname) {
            out->append("<");
            append_escaped(oidkey, out);
            append_escaped(oidname, out);
        } else if (oidkey > oidname) {
            out->append(">");
             append_escaped(oidkey, out);
            append_escaped(oidname, out);
        } else {
            // same as no key
            out->append("=");
            append_escaped(oidname, out);
        }
    } else {
        // no key
        out->append("=");
        append_escaped(oidname, out);
    }
}



inline char *encode_nspace_oid(const std::string &nspace, const std::string &oidkey, const std::string &oidname, char *pos)
{
    pos = append_escaped(nspace, pos);

    if (oidkey.length()) {
        // is a key... could be < = or >.
        // (ASCII chars < = and > sort in that order, yay)
        if (oidkey < oidname) {
            *pos++ = '<';
            pos = append_escaped(oidkey, pos);
            pos = append_escaped(oidname, pos);
        } else if (oidkey > oidname) {
            *pos++ = '>';
            pos = append_escaped(oidkey, pos);
            pos = append_escaped(oidname, pos);
        } else {
            // same as no key
            *pos++ = '=';
            pos = append_escaped(oidname, pos);
        }
    } else {
        // no key
        *pos++ = '=';
        pos = append_escaped(oidname, pos);
    }

    return pos;
}

inline int decode_nspace_oid(char *p, ghobject_t* oid)
{
    int r;
    p += decode_escaped(p, &oid->hobj.nspace) + 1;

    if (*p == '=') {
        // no key
        ++p;
        r = decode_escaped(p, &oid->hobj.oid.name);
        if (r < 0)
            return -7;
        p += r + 1;
    } else if (*p == '<' || *p == '>') {
        // key + name
        ++p;
        string okey;
        r = decode_escaped(p, &okey);
        if (r < 0)
            return -8;
        p += r + 1;
        r = decode_escaped(p, &oid->hobj.oid.name);
        if (r < 0)
            return -9;
        p += r + 1;
        oid->hobj.set_key(okey);
    } else {
        // malformed
        return -10;
    }

    return 0;

}

void print_kvskey(char *s, int length, const char *header, std::string &out) {
    std::stringstream ss;
    kvs_var_object_key *key = (kvs_var_object_key *)s;

    if (key->group == GROUP_PREFIX_SUPER) {
        struct kvs_sb_key* key = (struct kvs_sb_key*)s;
        ss << header << ", length = " << length << "\n";
        ss << "hash:  " << (int)key->prefix << "\n, ";
        ss << "group:  " << (int)key->group << "\n, ";
        ss << "name: " << key->name << "\n";
        ss << "key length = " << length << "\n";
        out = ss.str();
    }
    else {
        ss << header << ", length = " << length << "\n";
        ss << "group:  " << (int)key->group << "\n, ";
        ss << "shareid: " << (int)key->shardid<< "\n, ";
        ss << "poolid: " << key->poolid << "\n, ";
        ss << "bitwisekey: " << key->bitwisekey << "\n, ";
        ss << "snapid: " << key->snapid << "\n, ";
        ss << "genid: " << key->genid << "\n, ";
        ss << "name: " << key->name << "\n";
        ss << "key length = " << length << "\n";
        out = ss.str();
    }
}

struct kvs_key_header
{
    uint8_t  hdr;
    uint8_t  isonode;
    int8_t  shardid;
    uint64_t poolid;
};

inline void _construct_var_object_key(CephContext* cct, const uint8_t keyprefix, const bool isonode, const ghobject_t& oid, kv_key *key) {

    struct kvs_var_object_key* kvskey = (struct kvs_var_object_key*)key->key;

    kvskey->group = (isonode)? GROUP_PREFIX_ONODE:GROUP_PREFIX_DATA;
    kvskey->shardid = int8_t(oid.shard_id);
    kvskey->poolid  = oid.hobj.pool + 0x8000000000000000ull;
    kvskey->bitwisekey = oid.hobj.get_bitwise_key_u32();
    kvskey->snapid = uint64_t(oid.hobj.snap);
    kvskey->genid = (uint64_t)oid.generation;
    kvskey->grouphash = get_object_group_id (kvskey->group, kvskey->shardid, kvskey->poolid);

    char *pos = encode_nspace_oid(oid.hobj.nspace, oid.hobj.get_key(), oid.hobj.oid.name, &kvskey->name[0]);

    const int namelen = (pos - &kvskey->name[0]);

    if (namelen > 221) {
        // panic: name is too long
        std::string output;
        print_kvskey((char*)key->key, key->length, "key is too long (> 221B) ", output);
        ceph_abort_msg(cct, output);
    }

    key->length = 34 + namelen;

}

uint32_t get_object_group_id(const uint8_t  isonode,const int8_t shardid, const uint64_t poolid) {
    struct kvs_key_header hdr = { GROUP_PREFIX_DATA, isonode, shardid, poolid };
    return ceph_str_hash_linux((char*)&hdr, sizeof(struct kvs_key_header));
}

inline void construct_var_object_key(CephContext* cct, const uint8_t keyprefix, const ghobject_t& oid, kv_key *key) {
    _construct_var_object_key(cct, keyprefix, false, oid, key);
}

inline void construct_var_onode_key(CephContext* cct, const uint8_t keyprefix, const ghobject_t& oid, kv_key *key) {
    _construct_var_object_key(cct, keyprefix, true, oid, key);
}


bool construct_ghobject_t(CephContext* cct, const char *key, int keylength, ghobject_t* oid) {

    struct kvs_var_object_key* kvskey = (struct kvs_var_object_key*)key;
    if (kvskey->group != GROUP_PREFIX_DATA && kvskey->group != GROUP_PREFIX_ONODE) return false;

    oid->shard_id.id = kvskey->shardid;
    oid->hobj.pool = kvskey->poolid - 0x8000000000000000ull;
    oid->hobj.set_bitwise_key_u32(kvskey->bitwisekey);
    oid->hobj.snap.val = kvskey->snapid;
    oid->generation = kvskey->genid;

    if (decode_nspace_oid(&kvskey->name[0], oid) < 0) {
        // panic: name is too long
        std::string output;
        print_kvskey((char*)key, keylength, "malformed key - decoding failed ", output);
        ceph_abort_msg(cct, output);
    }
    return true;
}


inline int construct_journalkey(kv_key *kvkey, uint64_t index) {
    struct kvs_journal_key *jrnlkey = (struct kvs_journal_key *)kvkey->key;
    jrnlkey->hash  = GROUP_PREFIX_JOURNAL;
    jrnlkey->group = GROUP_PREFIX_JOURNAL;
    jrnlkey->index   = index;
    jrnlkey->reserved1= 0;
    jrnlkey->reserved2= 0;
    jrnlkey->reserved3= 0;
    kvkey->length = 16;
    return 0;
}

void construct_omap_key(CephContext* cct, uint64_t lid, const char *name, const int name_len, kv_key *key){
    if (name_len > KVSSD_VAR_OMAP_KEY_MAX_SIZE){
        lderr(cct) << "ERR: onode name '" << name <<"' is too long (" << name_len  << "> 218B (max))" << dendl;
    }
    kvs_omap_key_header hdr = { GROUP_PREFIX_OMAP, lid};
    struct kvs_omap_key* kvskey = (struct kvs_omap_key*)key->key;

    kvskey->hash = ceph_str_hash_linux((char*)&hdr, sizeof(struct kvs_omap_key_header));
    kvskey->group= GROUP_PREFIX_OMAP;
    kvskey->lid  = lid;

    if (name_len == 0) {
        kvskey->isheader = 1;
        key->length = 14;
    } else {
        kvskey->isheader = 0;
        memcpy(kvskey->name, name, name_len);
        key->length = 14 + name_len;
    }
}


inline int construct_collkey(kv_key *kvkey, const char *name, const int namelen)
{
    if (namelen > 250) return -1;

    struct kvs_coll_key *collkey = (struct kvs_coll_key *)kvkey->key;

    collkey->hash  = GROUP_PREFIX_COLL;
    collkey->group = GROUP_PREFIX_COLL;

    memcpy(collkey->name, name, namelen);

    kvkey->length = 5 + namelen;


    return 0;
}

inline void construct_sb_key(kv_key *key) {
    memset((void*)key->key, 0, 16);
    struct kvs_sb_key* kvskey = (struct kvs_sb_key*)key->key;
    kvskey->prefix = GROUP_PREFIX_SUPER;
    kvskey->group  = GROUP_PREFIX_SUPER;
    sprintf(kvskey->name, "%s", "kvsb");
    key->length = 16;
}


///
/// Write operations
///


#undef dout_prefix
#define dout_prefix *_dout << "[kvs-ioctx] "


inline kv_value *to_kv_value(char *data, int length) {
    kv_value *value = KvsMemPool::Alloc_value(length);
    memcpy((void*)value->value, data, length);
    return value;
}

inline kv_value *to_kv_value(bufferlist &bl) {
    return to_kv_value(bl.c_str(), bl.length());
}

void KvsIoContext::add_coll(const coll_t &cid, bufferlist &bl)
{
    FTRACE
    kv_key *key;
    kv_value *value;
    const char *cidkey_str = cid.c_str();
    const int   cidkey_len = (int)strlen(cidkey_str);
    if (cidkey_len > 250) {
        derr << __func__ << "collection name is too long (>250B) " << cidkey_str << dendl;
        return;
    }

    key   = KvsMemPool::Alloc_key();
    if (construct_collkey(key, cidkey_str, cidkey_len) < 0) {
        derr << "failed to create a collection key: key is too long - key =" << cidkey_str << ", length=" << cidkey_len << "B ( should be less than 245B)" << dendl;
        ceph_assert("cidkey is too long");
    }

    value = to_kv_value(bl);

#ifdef DUMP_IOWORKLOAD
    derr << "IO: add_coll: key = " << print_key((const char*)key->key, (int)key->length ) << ", value length =  " << value->length << ", bllength = " << bl.length() << dendl;
#endif


    this->add(key, value);
}

void KvsIoContext::rm_coll(const coll_t &cid)
{
    FTRACE
    kv_key *key;

    const char *cidkey_str = cid.c_str();
    const int   cidkey_len = (int)strlen(cidkey_str);

    key   = KvsMemPool::Alloc_key();
    construct_collkey(key, cidkey_str, cidkey_len);

#ifdef DUMP_IOWORKLOAD
    derr << "IO: rm_coll: key = " << print_key((const char*)key->key, (int)key->length ) << dendl;
#endif

    if (key == 0) { derr << __func__ << "key = " << key  << dendl; exit(1); }
    this->del(key);
}





void KvsIoContext::add_onode(const ghobject_t &oid, bufferlist &bl)
{
    FTRACE
    kv_key *key;
    kv_value *value;

    key = KvsMemPool::Alloc_key();

    construct_var_onode_key(cct, GROUP_PREFIX_ONODE, oid, key);

    value = to_kv_value(bl);

#ifdef DUMP_IOWORKLOAD
    derr << "IO: add_onode: key = " << print_key((const char*)key->key, (int)key->length ) << ", value length =  " << value->length << ", bllength = " << bl.length() << dendl;
#endif

    this->add(key, value, true);
}

void KvsIoContext::rm_onode(const ghobject_t& oid)
{
    FTRACE
    kv_key *key;

    key = KvsMemPool::Alloc_key();

    construct_var_onode_key(cct, GROUP_PREFIX_ONODE, oid, key);

#ifdef DUMP_IOWORKLOAD
    derr << "IO: rm_onode: key = " << print_key((const char*)key->key, (int)key->length ) << dendl;
#endif

    if (key == 0) { derr << __func__ << "key = " << key  << dendl; exit(1); }
    this->del(key, true);
}


void KvsIoContext::add_omapheader(const ghobject_t& oid, uint64_t index, bufferlist &bl)
{
    FTRACE
    kv_key *key;
    kv_value *value;

    key = KvsMemPool::Alloc_key();

    // add header key
    construct_omap_key(cct, index, 0, 0, key);

    value = to_kv_value(bl);

#ifdef DUMP_IOWORKLOAD
    derr << "IO: add_omap header: key = " << print_key((const char*)key->key, (int)key->length ) << ", value length =  " << value->length << ", bllength = " << bl.length() << dendl;
#endif
    this->add(key, value, true);
}


void KvsIoContext::add_omap(const ghobject_t& oid, uint64_t index, std::string &strkey, bufferlist &bl)
{
    FTRACE
    kv_key *key;
    kv_value *value;

    key = KvsMemPool::Alloc_key();


    construct_omap_key(cct, index, strkey.c_str(), strkey.length(), key);

    value = to_kv_value(bl);

#ifdef DUMP_IOWORKLOAD
    derr << "IO: add_omap: name= " << strkey.c_str() << ", key = " << print_key((const char*)key->key, (int)key->length ) << ", value length =  " << value->length << ", bllength = " << bl.length() << dendl;
#endif
    this->add(key, value, true);
}


void KvsIoContext::rm_omap (const ghobject_t& oid, uint64_t index, std::string &strkey)
{
    FTRACE
    kv_key *key;

    key = KvsMemPool::Alloc_key();

    construct_omap_key(cct, index, strkey.c_str(), strkey.length(), key);

#ifdef DUMP_IOWORKLOAD
    derr << "IO: rm_omap: key = " << print_key((const char*)key->key, (int)key->length ) << dendl;
#endif

    this->del(key, true);
}

void KvsIoContext::add_userdata(const ghobject_t& oid, char *data, int length)
{
    FTRACE
    kv_key *key;
    kv_value *value;

    key = KvsMemPool::Alloc_key();
    if (key == 0) {  derr << "key is null" << dendl; exit(1);  }

    construct_var_object_key(cct, GROUP_PREFIX_DATA, oid, key);

    value = to_kv_value(data, length);


#ifdef DUMP_IOWORKLOAD
    derr << "IO: add_user: key = " << print_key((const char*)key->key, (int)key->length ) << ", value length =  " << value->length << dendl;
#endif

    this->add(key, value);
}

void KvsIoContext::rm_data(const ghobject_t& oid)
{
    FTRACE
    kv_key *key;

    key = KvsMemPool::Alloc_key();

    construct_var_object_key(cct, GROUP_PREFIX_DATA, oid, key);

#ifdef DUMP_IOWORKLOAD
    derr << "IO: rm_data: key = " << print_key((const char*)key->key, (int)key->length ) << dendl;
#endif

    this->del(key, false);
}

void KvsIoContext::add_userdata(const ghobject_t& oid, bufferlist &bl)
{
    return add_userdata(oid, bl.c_str(), bl.length());
}

///
/// Read operations
///

#define DEFAULT_READBUF_SIZE 8192
void KvsReadContext::read_coll(const char *name, const int namelen)
{
    FTRACE
    if (namelen > 250) {
        derr << __func__ << "collection name is too long (>= 250B)" << dendl;
        this->retcode = -1;
        return;
    }

    this->key   = KvsMemPool::Alloc_key();
    this->value = KvsMemPool::Alloc_value(DEFAULT_READBUF_SIZE);
    construct_collkey(key, name, namelen);

#ifdef DUMP_IOWORKLOAD
    derr << "IO: read_coll: key = " << print_key((const char*)key->key, (int)key->length ) << ", value length =  " << value->length << dendl;
#endif

}


void KvsReadContext::read_sb()
{
    FTRACE
    this->key = KvsMemPool::Alloc_key();
    this->value = KvsMemPool::Alloc_value(DEFAULT_READBUF_SIZE);
    construct_sb_key(key);

#ifdef DUMP_IOWORKLOAD
    derr << "IO: read_sb: key = " << print_key((const char*)key->key, (int)key->length ) << ", value length =  " << value->length << dendl;
#endif
}


void KvsReadContext::read_journal(kvs_journal_key *jkey) {
    this->key = KvsMemPool::Alloc_key();
    this->value = KvsMemPool::Alloc_value(DEFAULT_READBUF_SIZE);
    this->key->length = sizeof(kvs_journal_key);
    memcpy((void*)this->key->key, jkey, this->key->length);

#ifdef DUMP_IOWORKLOAD
    derr << "IO: read_journal: key = " << print_key((const char*)key->key, (int)key->length ) << ", value length =  " << value->length << dendl;
#endif
}


void KvsReadContext::read_data(const ghobject_t &oid)
{
    FTRACE
    this->key = KvsMemPool::Alloc_key();
    this->value = KvsMemPool::Alloc_value(DEFAULT_READBUF_SIZE);

    construct_var_object_key(cct, GROUP_PREFIX_DATA, oid, key);

#ifdef DUMP_IOWORKLOAD
    derr << "IO: read_data: key = " << print_key((const char*)key->key, (int)key->length ) << ", value length =  " << value->length << dendl;
#endif

}

void KvsReadContext::read_onode(const ghobject_t &oid)
{
    FTRACE
    this->key   = KvsMemPool::Alloc_key();
    this->value = KvsMemPool::Alloc_value(DEFAULT_READBUF_SIZE);

    construct_var_onode_key(cct, GROUP_PREFIX_ONODE, oid, key);

#ifdef DUMP_IOWORKLOAD
    derr << "IO: read_onode: key = " << print_key((const char*)key->key, (int)key->length ) << ", value length =  " << value->length << dendl;
#endif

}
kv_result KvsReadContext::read_wait2()
{
    std::unique_lock<std::mutex> l(lock);
    if (num_running > 0) {
        cond.wait(l);
    }
    
    return retcode;
}
kv_result KvsReadContext::read_wait()
{
    std::chrono::duration<int64_t> 		    seconds(3);
    std::unique_lock<std::mutex> l(lock);
    // see _aio_thread for waker logic
    while (num_running > 0) {
        
        dout(10) << __func__ << " " << this
                 << " waiting for " << num_running << " aios to complete"
                 << dendl;
        if (cond.wait_for(l, seconds) == std::cv_status::timeout) {
            derr << "warning: key = " << print_key ((const char*)this->key->key, this->key->length ) << ", key length = " << this->key->length << ", " << dendl;
            derr << "warning: buffer = " << this->value->value << ", buflen = " <<  this->value->length << dendl;
            derr << "warning: but device read command did not respond for 3 seconds....waiting" << dendl;
        }
    }

    return retcode;
}

kv_result KvsReadContext::read_wait(bufferlist &bl) {
    FTRACE

    read_wait2();

    if (retcode == KV_SUCCESS && value != 0) {

        bl.append((const char *)value->value, value->length);
    }

    return retcode;
}

kv_result KvsReadContext::read_wait(bufferlist &bl, uint64_t offset, size_t length, bool &ispartial) {
    FTRACE

    read_wait2();

    if (retcode == KV_SUCCESS && value != 0) {

        if (value->length == 0 || value->length < offset) {
            ispartial = true;
            return 0;
        }

        int64_t len = (int64_t)value->length;
        if (offset + length > value->length || (offset == 0 && length == 0)) {
            len = (int64_t)value->length - offset;
            if(len < 0) len = 0;
        }

        ispartial = offset != 0 || len  != value->actual_value_size;
        bl.append((const char *)((char*)value->value + offset), len);
    }

    return retcode;
}
void KvsReadContext::try_read_wake2() {
    FTRACE

    std::lock_guard<std::mutex> l(lock);
    num_running--;
    if (num_running == 0)
        cond.notify_one();

}

void KvsReadContext::try_read_wake() {
    FTRACE

    std::lock_guard<std::mutex> l(lock);
    if (num_running == 1) {
        
        // we might have some pending IOs submitted after the check
        // as there is no lock protection for aio_submit.
        // Hence we might have false conditional trigger.
        // aio_wait has to handle that hence do not care here.

        --num_running;
        assert(num_running >= 0);
        cond.notify_all();
    } else {
        --num_running;
    }
}

KvsReadContext::~KvsReadContext() {
    if (key) {
        KvsMemPool::Release_key(key); key = 0;
    }

    if (value) {
        KvsMemPool::Release_value(value); value = 0;
    }
}


void KvsSyncWriteContext::write_sb(bufferlist &bl)
{
    FTRACE
    this->key = KvsMemPool::Alloc_key();
    this->value = to_kv_value(bl);
    construct_sb_key(key);
}

void KvsSyncWriteContext::delete_journal_key(struct kvs_journal_key* k) {
    this->key = KvsMemPool::Alloc_key(sizeof(kvs_journal_key));
    this->value = 0;
#ifdef DUMP_IOWORKLOAD
    derr << "IO: delete journal key " << print_key((const char*)key->key, (int)key->length ) << ", length = " << (int) this->key->length << dendl;
#endif

    memcpy((void*)key->key, (char*) k, this->key->length);
}

char *KvsSyncWriteContext::write_journal_entry(char *entry, uint64_t &lid) {
    char *curpos = entry;

    struct journal_header* header = (struct journal_header*) curpos; curpos += sizeof(struct journal_header);
    this->key = KvsMemPool::Alloc_key(header->keylength);
    this->value = KvsMemPool::Alloc_value(header->vallength);

    memcpy((void*)key->key, curpos, header->keylength); curpos += header->keylength;
    memcpy((void*)value->value, curpos, header->vallength);

    if (header->isonode) {
        kvsstore_onode_t onode;
        bufferlist v;
        v.append(curpos, header->vallength);
        bufferptr::iterator p = v.front().begin();
        onode.decode(p);
        lid = onode.lid;
    }
    else
        lid = 0;

    curpos += header->vallength;

#ifdef DUMP_IOWORKLOAD
    derr << "IO: journal replay key " << print_key((const char*)key->key, (int)key->length ) << ", length = " << (int) this->key->length << ", value length =  " << value->length << dendl;

#endif

    return curpos;
}

void KvsSyncWriteContext::write_journal(uint64_t index, std::list<std::pair<kv_key *, kv_value *> > &list)
{
    uint32_t len = 0;
    for (const auto &pair : list) {
        len += pair.first->length  ;
        if (pair.second)
            len += pair.second->length ;
        len += sizeof(journal_header);
    }

    this->key = KvsMemPool::Alloc_key();
    this->value = KvsMemPool::Alloc_value(len);
    construct_journalkey(key, index);

    // construct journal value
    char *curpos = (char*)this->value->value;
    bool isonode = true;
    for (const auto &pair : list) {
        struct journal_header* header = (struct journal_header*) curpos;
        header->isonode   = isonode;
        header->keylength = pair.first->length;
        if (pair.second) {
            header->vallength = pair.second->length;
            curpos += sizeof(struct journal_header);
            memcpy(curpos, (void*)pair.first->key, header->keylength); curpos += header->keylength;
            memcpy(curpos, (void*)pair.second->value, header->vallength); curpos += header->vallength;
        } else {
            header->vallength = 0;
        }
        isonode = false;
    }
#ifdef DUMP_IOWORKLOAD
    derr << "IO: journal write " << print_key((const char*)key->key, (int)key->length ) << ", length = " << (int) this->key->length << ", value length =  " << value->length << dendl;
#endif
}


kv_result KvsSyncWriteContext::write_wait() {
    FTRACE
    std::unique_lock<std::mutex> l(lock);
    // see _aio_thread for waker logic
    while (num_running.load() > 0) {
        dout(10) << __func__ << " " << this
                 << " waiting for " << num_running.load() << " aios to complete"
                 << dendl;
        cond.wait(l);
    }

    return retcode;
}

void KvsSyncWriteContext::try_write_wake() {
    FTRACE
    if (num_running == 1) {

        // we might have some pending IOs submitted after the check
        // as there is no lock protection for aio_submit.
        // Hence we might have false conditional trigger.
        // aio_wait has to handle that hence do not care here.
        std::lock_guard<std::mutex> l(lock);
        cond.notify_all();
        --num_running;
        assert(num_running >= 0);
    } else {
        --num_running;
    }
}

KvsSyncWriteContext::~KvsSyncWriteContext() {
    if (key) {
        KvsMemPool::Release_key(key); key = 0;
    }

    if (value) {
        KvsMemPool::Release_value(value); value = 0;
    }
}
