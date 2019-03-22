//
// Created by root on 2/25/19.
//

#ifndef CEPH_KVSSTORE_TEST_IMPL_H
#define CEPH_KVSSTORE_TEST_IMPL_H

#include <glob.h>
#include <stdio.h>
#include <string.h>
#include <iostream>
#include <time.h>
#include <sys/mount.h>
#include <boost/scoped_ptr.hpp>
#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int.hpp>
#include <boost/random/binomial_distribution.hpp>
#include <gtest/gtest.h>

#include "os/ObjectStore.h"
#include "os/kvsstore/KvsStore.h"
#include "include/Context.h"
#include "common/ceph_argparse.h"
#include "global/global_init.h"
#include "common/Mutex.h"
#include "common/Cond.h"
#include "common/errno.h"
#include "include/stringify.h"
#include "include/coredumpctl.h"

#include "include/unordered_map.h"
#include "store_test_fixture.h"

typedef boost::mt11213b gen_type;

#define dout_context g_ceph_context
//#if GTEST_HAS_PARAM_TEST

// Helpers
static bool bl_eq(bufferlist& expected, bufferlist& actual)
{
    if (expected.contents_equal(actual))
        return true;

    unsigned first = 0;
    if(expected.length() != actual.length()) {
        derr << "--- buffer lengths mismatch " 
             << "expected " << expected.length() << " != actual "
             << actual.length() << std::dec << dendl;
    }
    auto len = MIN(expected.length(), actual.length());
    while ( first<len && expected[first] == actual[first])
        ++first;
    unsigned last = len;
    while (last > 0 && expected[last-1] == actual[last-1])
        --last;
    if(len > 0) {
        derr << "--- buffer mismatch between offset "  << first
             << " and " << last << ", total " << len << std::dec
             << dendl;
        
        for (unsigned  i = first ; i < last; i++) {
            derr << "offset " << i << " exp. '" << expected[i] << ", but was " << actual[i] << dendl;
        }
    }
    return false;
}

template <typename T>
int apply_transaction(
        T &store,
        ObjectStore::Sequencer *osr,
        ObjectStore::Transaction &&t) {
    if (rand() % 2) {
        ObjectStore::Transaction t2;
        t2.append(t);
        return store->apply_transaction(osr, std::move(t2));
    } else {
        return store->apply_transaction(osr, std::move(t));
    }
}


bool sorted(const vector<ghobject_t> &in) {
    ghobject_t start;
    for (vector<ghobject_t>::const_iterator i = in.begin();
         i != in.end();
         ++i) {
        if (start > *i) {
            cout << start << " should follow " << *i << std::endl;
            return false;
        }
        start = *i;
    }
    return true;
}

class KvsStoreTest : public StoreTestFixture,
                     public ::testing::WithParamInterface<const char*> {
public:
    KvsStoreTest()
            : StoreTestFixture(GetParam())
    {}
};

class KvsStoreTestDeferredSetup : public KvsStoreTest {
    void SetUp() override {
        //do nothing
    }

protected:
    void DeferredSetup() {
        KvsStoreTest::SetUp();
    }

public:
};

class KvsStoreTestSpecificAUSize : public KvsStoreTestDeferredSetup {

public:
    typedef
    std::function<void(
            boost::scoped_ptr<ObjectStore>& store,
            uint64_t num_ops,
            uint64_t max_obj,
            uint64_t max_wr,
            uint64_t align)> MatrixTest;

    void StartDeferred(size_t min_alloc_size) {
        g_conf->set_val("bluestore_min_alloc_size", stringify(min_alloc_size));
        DeferredSetup();
    }

    void TearDown() override {
        g_conf->set_val("bluestore_min_alloc_size", "0");
        KvsStoreTestDeferredSetup::TearDown();
    }

private:
    // bluestore matrix testing
    uint64_t max_write = 40 * 1024;
    uint64_t max_size = 400 * 1024;
    uint64_t alignment = 0;
    uint64_t num_ops = 10000;

protected:
    string matrix_get(const char *k) {
        if (string(k) == "max_write") {
            return stringify(max_write);
        } else if (string(k) == "max_size") {
            return stringify(max_size);
        } else if (string(k) == "alignment") {
            return stringify(alignment);
        } else if (string(k) == "num_ops") {
            return stringify(num_ops);
        } else {
            char *buf;
            g_conf->get_val(k, &buf, -1);
            string v = buf;
            free(buf);
            return v;
        }
    }

    void matrix_set(const char *k, const char *v) {
        if (string(k) == "max_write") {
            max_write = atoll(v);
        } else if (string(k) == "max_size") {
            max_size = atoll(v);
        } else if (string(k) == "alignment") {
            alignment = atoll(v);
        } else if (string(k) == "num_ops") {
            num_ops = atoll(v);
        } else {
            g_conf->set_val(k, v);
        }
    }

    void do_matrix_choose(const char *matrix[][10],
                          int i, int pos, int num,
                          boost::scoped_ptr<ObjectStore>& store,
                          MatrixTest fn) {
        if (matrix[i][0]) {
            int count;
            for (count = 0; matrix[i][count+1]; ++count) ;
            for (int j = 1; matrix[i][j]; ++j) {
                matrix_set(matrix[i][0], matrix[i][j]);
                do_matrix_choose(matrix,
                                 i + 1,
                                 pos * count + j - 1,
                                 num * count,
                                 store,
                                 fn);
            }
        } else {
            cout << "---------------------- " << (pos + 1) << " / " << num
                 << " ----------------------" << std::endl;
            for (unsigned k=0; matrix[k][0]; ++k) {
                cout << "  " << matrix[k][0] << " = " << matrix_get(matrix[k][0])
                     << std::endl;
            }
            g_ceph_context->_conf->apply_changes(NULL);
            fn(store, num_ops, max_size, max_write, alignment);
        }
    }

    void do_matrix(const char *matrix[][10],
                   boost::scoped_ptr<ObjectStore>& store,
                   MatrixTest fn) {
        map<string,string> old;
        for (unsigned i=0; matrix[i][0]; ++i) {
            old[matrix[i][0]] = matrix_get(matrix[i][0]);
        }
        cout << "saved config options " << old << std::endl;

        if (strcmp(matrix[0][0], "bluestore_min_alloc_size") == 0) {
            int count;
            for (count = 0; matrix[0][count+1]; ++count) ;
            for (size_t j = 1; matrix[0][j]; ++j) {
                if (j > 1) {
                    TearDown();
                }
                StartDeferred(strtoll(matrix[0][j], NULL, 10));
                do_matrix_choose(matrix, 1, j - 1, count, store, fn);
            }
        } else {
            StartDeferred(0);
            do_matrix_choose(matrix, 0, 0, 1, store, fn);
        }

        cout << "restoring config options " << old << std::endl;
        for (auto p : old) {
            cout << "  " << p.first << " = " << p.second << std::endl;
            matrix_set(p.first.c_str(), p.second.c_str());
        }
        g_ceph_context->_conf->apply_changes(NULL);
    }

};

class ObjectGenerator {
public:
    virtual ghobject_t create_object(gen_type *gen) = 0;
    virtual ~ObjectGenerator() {}
};

class MixedGenerator : public ObjectGenerator {
public:
    unsigned seq;
    int64_t poolid;
    explicit MixedGenerator(int64_t p) : seq(0), poolid(p) {}
    ghobject_t create_object(gen_type *gen) override {
        char buf[100];
        snprintf(buf, sizeof(buf), "OBJ_%u", seq);
        string name(buf);
        if (seq % 2) {
            for (unsigned i = 0; i < 100; ++i) {
                name.push_back('a');
            }
        }
        ++seq;
        return ghobject_t(
                hobject_t(
                        name, string(), rand() & 2 ? CEPH_NOSNAP : rand(),
                        (((seq / 1024) % 2) * 0xF00 ) +
                        (seq & 0xFF),
                        poolid, ""));
    }
};

class SyntheticWorkloadState {
    struct Object {
        bufferlist data;
        map<string, bufferlist> attrs;
    };
public:
    static const unsigned max_in_flight = 16;
    static const unsigned max_objects = 500000;
    static const unsigned max_attr_size = 5;
    static const unsigned max_attr_name_len = 100;
    static const unsigned max_attr_value_len = 1024 * 64;
    coll_t cid;
    unsigned write_alignment;
    unsigned max_object_len, max_write_len;
    unsigned in_flight;
    map<ghobject_t, Object> contents;
    set<ghobject_t> available_objects;
    set<ghobject_t> in_flight_objects;
    ObjectGenerator *object_gen;
    gen_type *rng;
    ObjectStore *store;
    ObjectStore::Sequencer *osr;

    Mutex lock;
    Cond cond;

    struct EnterExit {
        const char *msg;
        explicit EnterExit(const char *m) : msg(m) {
            //cout << pthread_self() << " enter " << msg << std::endl;
        }
        ~EnterExit() {
            //cout << pthread_self() << " exit " << msg << std::endl;
        }
    };

    class C_SyntheticOnReadable : public Context {
    public:
        SyntheticWorkloadState *state;
        ghobject_t hoid;
        C_SyntheticOnReadable(SyntheticWorkloadState *state, ghobject_t hoid)
                : state(state), hoid(hoid) {}

        void finish(int r) override {
            Mutex::Locker locker(state->lock);
            EnterExit ee("onreadable finish");
            ASSERT_TRUE(state->in_flight_objects.count(hoid));
            ASSERT_EQ(r, 0);
            state->in_flight_objects.erase(hoid);
            if (state->contents.count(hoid))
                state->available_objects.insert(hoid);
            --(state->in_flight);
            state->cond.Signal();

            
            bufferlist r2;
            r = state->store->read(state->cid, hoid, 0, state->contents[hoid].data.length(), r2);
            if (r < 0)
                derr << "verify-error: oid = " << hoid << ", exist? " << r << ", data length =" << r2.length() << dendl;
            if (r >= 0) {
                uint32_t hash_ori = ceph_str_hash_linux(state->contents[hoid].data.c_str(), state->contents[hoid].data.length());
                uint32_t hash_read = ceph_str_hash_linux(r2.c_str(), r2.length());
                if (hash_ori != hash_read) {
                    derr << "data corruption:" << hoid << ", hash ori = " << hash_ori << ", hash read = " << hash_read  << dendl;
                }
            }
	        assert(bl_eq(state->contents[hoid].data, r2));
        
            state->cond.Signal();
        }
    };

    class C_SyntheticOnStash : public Context {
    public:
        SyntheticWorkloadState *state;
        ghobject_t oid, noid;

        C_SyntheticOnStash(SyntheticWorkloadState *state,
                           ghobject_t oid, ghobject_t noid)
                : state(state), oid(oid), noid(noid) {}

        void finish(int r) override {
            Mutex::Locker locker(state->lock);
            EnterExit ee("stash finish");
            ASSERT_TRUE(state->in_flight_objects.count(oid));
            ASSERT_EQ(r, 0);
            state->in_flight_objects.erase(oid);
            if (state->contents.count(noid))
                state->available_objects.insert(noid);
            --(state->in_flight);
            bufferlist r2;
            r = state->store->read(
                    state->cid, noid, 0,
                    state->contents[noid].data.length(), r2);

            ASSERT_EQ(state->contents[noid].data.length(), r2.length() );
            assert(bl_eq(state->contents[noid].data, r2));
            state->cond.Signal();
        }
    };

    class C_SyntheticOnClone : public Context {
    public:
        SyntheticWorkloadState *state;
        ghobject_t oid, noid;

        C_SyntheticOnClone(SyntheticWorkloadState *state,
                           ghobject_t oid, ghobject_t noid)
                : state(state), oid(oid), noid(noid) {}

        void finish(int r) override {
            Mutex::Locker locker(state->lock);
            EnterExit ee("clone finish");
            ASSERT_TRUE(state->in_flight_objects.count(oid));
            ASSERT_EQ(r, 0);
            state->in_flight_objects.erase(oid);
            if (state->contents.count(oid))
                state->available_objects.insert(oid);
            if (state->contents.count(noid))
                state->available_objects.insert(noid);
            --(state->in_flight);
            bufferlist r2;
            r = state->store->read(state->cid, noid, 0, state->contents[noid].data.length(), r2);
            
            assert(bl_eq(state->contents[noid].data, r2));
            state->cond.Signal();
        }
    };

    static void filled_byte_array(bufferlist& bl, size_t size)
    {
        static const char alphanum[] = "0123456789"
                                       "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                       "abcdefghijklmnopqrstuvwxyz";
        if (!size) {
            return;
        }
        bufferptr bp(size);
        for (unsigned int i = 0; i < size - 1; i++) {
            // severely limit entropy so we can compress...
            bp[i] = alphanum[rand() % 10]; 
        }
        bp[size - 1] = '\0';

        bl.append(bp);
    }

    SyntheticWorkloadState(ObjectStore *store,
                           ObjectGenerator *gen,
                           gen_type *rng,
                           ObjectStore::Sequencer *osr,
                           coll_t cid,
                           unsigned max_size,
                           unsigned max_write,
                           unsigned alignment)
            : cid(cid), write_alignment(alignment), max_object_len(max_size),
              max_write_len(max_write), in_flight(0), object_gen(gen),
              rng(rng), store(store), osr(osr), lock("State lock") {}

    int init() {
        ObjectStore::Transaction t;
        t.create_collection(cid, 0);
        return apply_transaction(store, osr, std::move(t));
    }
    void shutdown() {
        while (1) {
            vector<ghobject_t> objects;
            int r = store->collection_list(cid, ghobject_t(), ghobject_t::get_max(),
                                           10, &objects, 0);
            assert(r >= 0);
        
            if (objects.empty())
                break;
            ObjectStore::Transaction t;
            for (vector<ghobject_t>::iterator p = objects.begin();
                 p != objects.end(); ++p) {

                t.remove(cid, *p);
            }
            apply_transaction(store, osr, std::move(t));
        }
        ObjectStore::Transaction t;
        t.remove_collection(cid);
        apply_transaction(store, osr, std::move(t));
    }
    void statfs(store_statfs_t& stat) {
        store->statfs(&stat);
    }

    ghobject_t get_uniform_random_object() {
        while (in_flight >= max_in_flight || available_objects.empty())
            cond.Wait(lock);
        boost::uniform_int<> choose(0, available_objects.size() - 1);
        int index = choose(*rng);
        set<ghobject_t>::iterator i = available_objects.begin();
        for ( ; index > 0; --index, ++i) ;
        ghobject_t ret = *i;
        return ret;
    }

    void wait_for_ready() {
        while (in_flight >= max_in_flight)
            cond.Wait(lock);
    }

    void wait_for_done() {
        osr->flush();
        Mutex::Locker locker(lock);
        while (in_flight)
            cond.Wait(lock);
    }

    bool can_create() {
        return (available_objects.size() + in_flight_objects.size()) < max_objects;
    }

    bool can_unlink() {
        return (available_objects.size() + in_flight_objects.size()) > 0;
    }

    unsigned get_random_alloc_hints() {
        unsigned f = 0;
        {
            boost::uniform_int<> u(0, 3);
            switch (u(*rng)) {
                case 1:
                    f |= CEPH_OSD_ALLOC_HINT_FLAG_SEQUENTIAL_WRITE;
                    break;
                case 2:
                    f |= CEPH_OSD_ALLOC_HINT_FLAG_RANDOM_WRITE;
                    break;
            }
        }
        {
            boost::uniform_int<> u(0, 3);
            switch (u(*rng)) {
                case 1:
                    f |= CEPH_OSD_ALLOC_HINT_FLAG_SEQUENTIAL_READ;
                    break;
                case 2:
                    f |= CEPH_OSD_ALLOC_HINT_FLAG_RANDOM_READ;
                    break;
            }
        }
        {
            // append_only, immutable
            boost::uniform_int<> u(0, 4);
            f |= u(*rng) << 4;
        }
        {
            boost::uniform_int<> u(0, 3);
            switch (u(*rng)) {
                case 1:
                    f |= CEPH_OSD_ALLOC_HINT_FLAG_SHORTLIVED;
                    break;
                case 2:
                    f |= CEPH_OSD_ALLOC_HINT_FLAG_LONGLIVED;
                    break;
            }
        }
        {
            boost::uniform_int<> u(0, 3);
            switch (u(*rng)) {
                case 1:
                    f |= CEPH_OSD_ALLOC_HINT_FLAG_COMPRESSIBLE;
                    break;
                case 2:
                    f |= CEPH_OSD_ALLOC_HINT_FLAG_INCOMPRESSIBLE;
                    break;
            }
        }
        return f;
    }

    int touch() {

            Mutex::Locker locker(lock);
            EnterExit ee("touch");
            if (!can_create())
                return -ENOSPC;
            wait_for_ready();
            ghobject_t new_obj = object_gen->create_object(rng);
            available_objects.erase(new_obj);
            ObjectStore::Transaction t;
            t.touch(cid, new_obj);

            boost::uniform_int<> u(17, 22);
            boost::uniform_int<> v(12, 17);
            t.set_alloc_hint(cid, new_obj,
                             1ull << u(*rng),
                             1ull << v(*rng),
                             get_random_alloc_hints());
            ++in_flight;
            in_flight_objects.insert(new_obj);
            if (!contents.count(new_obj))
                contents[new_obj] = Object();
        int status = store->queue_transaction(osr, std::move(t), new C_SyntheticOnReadable(this, new_obj));
        if (status == 0) {
            // verify

        }



        return status;
    }

    int stash() {
        Mutex::Locker locker(lock);
        EnterExit ee("stash");
        if (!can_unlink())
            return -ENOENT;
        if (!can_create())
            return -ENOSPC;
        wait_for_ready();

        ghobject_t old_obj;
        int max = 20;
        do {
            old_obj = get_uniform_random_object();
        } while (--max && !contents[old_obj].data.length());
        available_objects.erase(old_obj);
        ghobject_t new_obj = old_obj;
        new_obj.generation++;
        available_objects.erase(new_obj);

        ObjectStore::Transaction t;
        t.collection_move_rename(cid, old_obj, cid, new_obj);
        ++in_flight;
        in_flight_objects.insert(old_obj);

        contents[new_obj].attrs = contents[old_obj].attrs;
        contents[new_obj].data = contents[old_obj].data;
        contents.erase(old_obj);
        int status = store->queue_transaction(
                osr, std::move(t),
                new C_SyntheticOnStash(this, old_obj, new_obj));
        return status;
    }

    int clone() {
        Mutex::Locker locker(lock);
        EnterExit ee("clone");
        if (!can_unlink())
            return -ENOENT;
        if (!can_create())
            return -ENOSPC;
        wait_for_ready();

        ghobject_t old_obj;
        int max = 20;
        do {
            old_obj = get_uniform_random_object();
        } while (--max && !contents[old_obj].data.length());
        available_objects.erase(old_obj);
        ghobject_t new_obj = object_gen->create_object(rng);
        // make the hash match
        new_obj.hobj.set_hash(old_obj.hobj.get_hash());
        available_objects.erase(new_obj);

        
        ObjectStore::Transaction t;
        t.clone(cid, old_obj, new_obj);
        ++in_flight;
        in_flight_objects.insert(old_obj);

        contents[new_obj].attrs = contents[old_obj].attrs;
        contents[new_obj].data = contents[old_obj].data;

        int status = store->queue_transaction(
                osr, std::move(t),
                new C_SyntheticOnClone(this, old_obj, new_obj));
        return status;
    }

    int clone_range() {
        Mutex::Locker locker(lock);
        EnterExit ee("clone_range");
        if (!can_unlink())
            return -ENOENT;
        if (!can_create())
            return -ENOSPC;
        wait_for_ready();

        ghobject_t old_obj;
        int max = 20;
        do {
            old_obj = get_uniform_random_object();
        } while (--max && !contents[old_obj].data.length());
        bufferlist &srcdata = contents[old_obj].data;
        if (srcdata.length() == 0) {
            return 0;
        }
        available_objects.erase(old_obj);
        ghobject_t new_obj = get_uniform_random_object();
        available_objects.erase(new_obj);

        boost::uniform_int<> u1(0, max_object_len - max_write_len);
        boost::uniform_int<> u2(0, max_write_len);
        uint64_t srcoff = u1(*rng);
        // make src and dst offsets match, since that's what the osd does
        uint64_t dstoff = srcoff; //u1(*rng);
        uint64_t len = u2(*rng);
        if (write_alignment) {
            srcoff = ROUND_UP_TO(srcoff, write_alignment);
            dstoff = ROUND_UP_TO(dstoff, write_alignment);
            len = ROUND_UP_TO(len, write_alignment);
        }

        if (srcoff > srcdata.length() - 1) {
            srcoff = srcdata.length() - 1;
        }
        if (srcoff + len > srcdata.length()) {
            len = srcdata.length() - srcoff;
        }
        if (0)
            cout << __func__ << " from " << srcoff << "~" << len
                 << " (size " << srcdata.length() << ") to "
                 << dstoff << "~" << len << std::endl;

        ObjectStore::Transaction t;
        t.clone_range(cid, old_obj, new_obj, srcoff, len, dstoff);
        ++in_flight;
        in_flight_objects.insert(old_obj);

        bufferlist bl;
        if (srcoff < srcdata.length()) {
            if (srcoff + len > srcdata.length()) {
                bl.substr_of(srcdata, srcoff, srcdata.length() - srcoff);
            } else {
                bl.substr_of(srcdata, srcoff, len);
            }
        }

        bufferlist& dstdata = contents[new_obj].data;
        if (dstdata.length() <= dstoff) {
            if (bl.length() > 0) {
                dstdata.append_zero(dstoff - dstdata.length());
                dstdata.append(bl);
            }
        } else {
            bufferlist value;
            assert(dstdata.length() > dstoff);
            dstdata.copy(0, dstoff, value);
            value.append(bl);
            if (value.length() < dstdata.length())
                dstdata.copy(value.length(),
                             dstdata.length() - value.length(), value);
            value.swap(dstdata);
        }

        int status = store->queue_transaction(
                osr, std::move(t), new C_SyntheticOnClone(this, old_obj, new_obj));
        return status;
    }


    int write() {
        Mutex::Locker locker(lock);
        EnterExit ee("write");
        if (!can_unlink())
            return -ENOENT;
        wait_for_ready();

        ghobject_t new_obj = get_uniform_random_object();
        available_objects.erase(new_obj);
        ObjectStore::Transaction t;

        boost::uniform_int<> u1(0, max_object_len - max_write_len);
        boost::uniform_int<> u2(0, max_write_len);
        uint64_t offset = u1(*rng);
        uint64_t len = u2(*rng);
        bufferlist bl;
        if (write_alignment) {
            offset = ROUND_UP_TO(offset, write_alignment);
            len = ROUND_UP_TO(len, write_alignment);
        }

        filled_byte_array(bl, len);

        bufferlist& data = contents[new_obj].data;
        if (data.length() <= offset) {
            if (len > 0) {
                data.append_zero(offset-data.length());
                data.append(bl);
            }
        } else {
            bufferlist value;
            assert(data.length() > offset);
            data.copy(0, offset, value);
            value.append(bl);
            if (value.length() < data.length())
                data.copy(value.length(),
                          data.length()-value.length(), value);
            value.swap(data);
        }

        t.write(cid, new_obj, offset, len, bl);
        ++in_flight;
        in_flight_objects.insert(new_obj);
        int status = store->queue_transaction(
                osr, std::move(t), new C_SyntheticOnReadable(this, new_obj));
        return status;
    }

    int truncate() {
        Mutex::Locker locker(lock);
        EnterExit ee("truncate");
        if (!can_unlink())
            return -ENOENT;
        wait_for_ready();

        ghobject_t obj = get_uniform_random_object();
        available_objects.erase(obj);
        ObjectStore::Transaction t;

        boost::uniform_int<> choose(0, max_object_len);
        size_t len = choose(*rng);
        if (write_alignment) {
            len = ROUND_UP_TO(len, write_alignment);
        }

        t.truncate(cid, obj, len);
        ++in_flight;
        in_flight_objects.insert(obj);
        bufferlist& data = contents[obj].data;
        if (data.length() <= len) {
            data.append_zero(len - data.length());
        } else {
            bufferlist bl;
            data.copy(0, len, bl);
            bl.swap(data);
        }

        int status = store->queue_transaction(
                osr, std::move(t), new C_SyntheticOnReadable(this, obj));
        return status;
    }

    int zero() {
        Mutex::Locker locker(lock);
        EnterExit ee("zero");
        if (!can_unlink())
            return -ENOENT;
        wait_for_ready();

        ghobject_t new_obj = get_uniform_random_object();
        available_objects.erase(new_obj);
        ObjectStore::Transaction t;

        boost::uniform_int<> u1(0, max_object_len - max_write_len);
        boost::uniform_int<> u2(0, max_write_len);
        uint64_t offset = u1(*rng);
        uint64_t len = u2(*rng);
        if (write_alignment) {
            offset = ROUND_UP_TO(offset, write_alignment);
            len = ROUND_UP_TO(len, write_alignment);
        }

        auto& data = contents[new_obj].data;
        if (data.length() < offset + len) {
            data.append_zero(offset+len-data.length());
        }
        bufferlist n;
        n.substr_of(data, 0, offset);
        n.append_zero(len);
        if (data.length() > offset + len)
            data.copy(offset + len, data.length() - offset - len, n);
        data.swap(n);

        t.zero(cid, new_obj, offset, len);
        ++in_flight;
        in_flight_objects.insert(new_obj);
        int status = store->queue_transaction(
                osr, std::move(t), new C_SyntheticOnReadable(this, new_obj));
        return status;
    }

    void read() {
        EnterExit ee("read");
        boost::uniform_int<> u1(0, max_object_len/2);
        boost::uniform_int<> u2(0, max_object_len);
        uint64_t offset = u1(*rng);
        uint64_t len = u2(*rng);
        if (offset > len)
            swap(offset, len);

        ghobject_t obj;
        bufferlist expected;
        int r;
        {
            Mutex::Locker locker(lock);
            EnterExit ee("read locked");
            if (!can_unlink())
                return ;
            wait_for_ready();

            obj = get_uniform_random_object();
            expected = contents[obj].data;
        }
        bufferlist bl, result;
        derr << "test read: obj " << obj
             << " size " << expected.length()
             << " offset " << offset
             << " len " << len << dendl;
        r = store->read(cid, obj, offset, len, result);
        if (offset >= expected.length()) {
            ASSERT_EQ(r, 0);
        } else {
            derr << "max len = " << expected.length() - offset << dendl;
            derr << "result.len = " << result.length() << dendl;
            derr << "len = " << len << dendl;
            size_t max_len = expected.length() - offset;
            if (len > max_len)
                len = max_len;
            derr << "len 2= " << len << dendl;
            assert(len == result.length());
            ASSERT_EQ(len, result.length());
            expected.copy(offset, len, bl);
            ASSERT_EQ(r, (int)len);
            ASSERT_TRUE(bl_eq(bl, result));
        }
    }

    int setattrs() {
        Mutex::Locker locker(lock);
        EnterExit ee("setattrs");
        if (!can_unlink())
            return -ENOENT;
        wait_for_ready();

        ghobject_t obj = get_uniform_random_object();
        available_objects.erase(obj);
        ObjectStore::Transaction t;

        boost::uniform_int<> u0(1, max_attr_size);
        boost::uniform_int<> u1(4, max_attr_name_len);
        boost::uniform_int<> u2(4, max_attr_value_len);
        boost::uniform_int<> u3(0, 100);
        uint64_t size = u0(*rng);
        uint64_t name_len;
        map<string, bufferlist> attrs;
        set<string> keys;
        for (map<string, bufferlist>::iterator it = contents[obj].attrs.begin();
             it != contents[obj].attrs.end(); ++it)
            keys.insert(it->first);

        while (size--) {
            bufferlist name, value;
            uint64_t get_exist = u3(*rng);
            uint64_t value_len = u2(*rng);
            filled_byte_array(value, value_len);
            if (get_exist < 50 && keys.size()) {
                set<string>::iterator k = keys.begin();
                attrs[*k] = value;
                contents[obj].attrs[*k] = value;
                keys.erase(k);
            } else {
                name_len = u1(*rng);
                filled_byte_array(name, name_len);
                attrs[name.c_str()] = value;
                contents[obj].attrs[name.c_str()] = value;
            }
        }
        t.setattrs(cid, obj, attrs);
        ++in_flight;
        in_flight_objects.insert(obj);
        int status = store->queue_transaction(
                osr, std::move(t), new C_SyntheticOnReadable(this, obj));
        return status;
    }

    void getattrs() {
        EnterExit ee("getattrs");
        ghobject_t obj;
        map<string, bufferlist> expected;
        {
            Mutex::Locker locker(lock);
            EnterExit ee("getattrs locked");
            if (!can_unlink())
                return ;
            wait_for_ready();

            int retry = 10;
            do {
                obj = get_uniform_random_object();
                if (!--retry)
                    return ;
            } while (contents[obj].attrs.empty());
            expected = contents[obj].attrs;
        }
        map<string, bufferlist> attrs;
        int r = store->getattrs(cid, obj, attrs);
        ASSERT_TRUE(r == 0);
        ASSERT_TRUE(attrs.size() == expected.size());
        for (map<string, bufferlist>::iterator it = expected.begin();
             it != expected.end(); ++it) {
            ASSERT_TRUE(bl_eq(attrs[it->first], it->second));
        }
    }

    void getattr() {
        EnterExit ee("getattr");
        ghobject_t obj;
        int r;
        int retry;
        map<string, bufferlist> expected;
        {
            Mutex::Locker locker(lock);
            EnterExit ee("getattr locked");
            if (!can_unlink())
                return ;
            wait_for_ready();

            retry = 10;
            do {
                obj = get_uniform_random_object();
                if (!--retry)
                    return ;
            } while (contents[obj].attrs.empty());
            expected = contents[obj].attrs;
        }
        boost::uniform_int<> u(0, expected.size()-1);
        retry = u(*rng);
        map<string, bufferlist>::iterator it = expected.begin();
        while (retry) {
            retry--;
            ++it;
        }

        bufferlist bl;
        r = store->getattr(cid, obj, it->first, bl);
        ASSERT_EQ(r, 0);
        ASSERT_TRUE(bl_eq(it->second, bl));
    }

    int rmattr() {
        Mutex::Locker locker(lock);
        EnterExit ee("rmattr");
        if (!can_unlink())
            return -ENOENT;
        wait_for_ready();

        ghobject_t obj;
        int retry = 10;
        do {
            obj = get_uniform_random_object();
            if (!--retry)
                return 0;
        } while (contents[obj].attrs.empty());

        boost::uniform_int<> u(0, contents[obj].attrs.size()-1);
        retry = u(*rng);
        map<string, bufferlist>::iterator it = contents[obj].attrs.begin();
        while (retry) {
            retry--;
            ++it;
        }

        available_objects.erase(obj);
        ObjectStore::Transaction t;
        t.rmattr(cid, obj, it->first);

        contents[obj].attrs.erase(it->first);
        ++in_flight;
        in_flight_objects.insert(obj);
        int status = store->queue_transaction(
                osr, std::move(t), new C_SyntheticOnReadable(this, obj));
        return status;
    }

    void fsck(bool deep) {
        Mutex::Locker locker(lock);
        EnterExit ee("fsck");
        while (in_flight)
            cond.Wait(lock);
        store->umount();
        int r = store->fsck(deep);
        assert(r == 0 || r == -EOPNOTSUPP);
        store->mount();
    }

    void scan() {
        Mutex::Locker locker(lock);
        EnterExit ee("scan");
        while (in_flight)
            cond.Wait(lock);
        vector<ghobject_t> objects;
        set<ghobject_t> objects_set, objects_set2;
        ghobject_t next, current;
        while (1) {
            int r = store->collection_list(cid, current, ghobject_t::get_max(), 100,
                                           &objects, &next);
            ASSERT_EQ(r, 0);
            ASSERT_TRUE(sorted(objects));
            objects_set.insert(objects.begin(), objects.end());
            objects.clear();
            if (next.is_max()) break;
            current = next;
        }

        derr << " object set size " << objects_set.size() << dendl;
        derr << " available  size " << available_objects.size() << dendl;

        if (objects_set.size() != available_objects.size()) {
            for (set<ghobject_t>::iterator p = objects_set.begin();
                 p != objects_set.end();
                 ++p)
                if (available_objects.count(*p) == 0) {
                    derr << "+ " << *p << dendl;
                    ceph_abort();
                }
            for (set<ghobject_t>::iterator p = available_objects.begin();
                 p != available_objects.end();
                 ++p)
                if (objects_set.count(*p) == 0) {
                    derr << "can't find - " << *p << dendl;
                    {
                        const ghobject_t &oid = *p;
                        struct kvs_var_object_key kvskey;
                        kvskey.group = GROUP_PREFIX_DATA;
                        kvskey.shardid = int8_t(oid.shard_id);
                        kvskey.poolid = oid.hobj.pool + 0x8000000000000000ull;
                        kvskey.bitwisekey = oid.hobj.get_bitwise_key_u32();
                        kvskey.snapid = uint64_t(oid.hobj.snap);
                        kvskey.genid = (uint64_t) oid.generation;
                        kvskey.grouphash = get_object_group_id(kvskey.group, kvskey.shardid, kvskey.poolid);

                        derr << "shard " << (int)kvskey.shardid << dendl;
                        derr << "pool " << kvskey.poolid << dendl;
                        derr << "hash " << kvskey.grouphash << dendl;
                    }

                }
            cerr << " objects_set: " << objects_set.size() << std::endl;
            cerr << " available_set: " << available_objects.size() << std::endl;
            assert(0 == "badness");
        }

        ASSERT_EQ(objects_set.size(), available_objects.size());
        for (set<ghobject_t>::iterator i = objects_set.begin();
             i != objects_set.end();
             ++i) {
            ASSERT_GT(available_objects.count(*i), (unsigned)0);
        }
        

        int r = store->collection_list(cid, ghobject_t(), ghobject_t::get_max(),
                                       INT_MAX, &objects, 0);
        ASSERT_EQ(r, 0);
        objects_set2.insert(objects.begin(), objects.end());

        derr << " object set size " << objects_set2.size() << dendl;
        derr << " available  size " << available_objects.size() << dendl;

        for (set<ghobject_t>::iterator i = objects_set2.begin();
             i != objects_set2.end();
             ++i) {
            if (available_objects.count(*i) == 0) {
                derr << "+ " << *i << dendl;
                {
                    const ghobject_t &oid = *i;
                    struct kvs_var_object_key kvskey;
                    kvskey.group = GROUP_PREFIX_DATA;
                    kvskey.shardid = int8_t(oid.shard_id);
                    kvskey.poolid = oid.hobj.pool + 0x8000000000000000ull;
                    kvskey.bitwisekey = oid.hobj.get_bitwise_key_u32();
                    kvskey.snapid = uint64_t(oid.hobj.snap);
                    kvskey.genid = (uint64_t) oid.generation;
                    kvskey.grouphash = get_object_group_id(kvskey.group, kvskey.shardid, kvskey.poolid);

                    derr << "shard " << kvskey.shardid << dendl;
                    derr << "pool " << kvskey.poolid << dendl;
                    derr << "hash " << kvskey.grouphash << dendl;
                }
            }
        }
        for (set<ghobject_t>::iterator p = available_objects.begin();
             p != available_objects.end();
             ++p)
            if (objects_set2.count(*p) == 0) {
                derr << "can't find - " << *p << dendl;
                {
                    const ghobject_t &oid = *p;
                    struct kvs_var_object_key kvskey;
                    kvskey.group = GROUP_PREFIX_DATA;
                    kvskey.shardid = int8_t(oid.shard_id);
                    kvskey.poolid = oid.hobj.pool + 0x8000000000000000ull;
                    kvskey.bitwisekey = oid.hobj.get_bitwise_key_u32();
                    kvskey.snapid = uint64_t(oid.hobj.snap);
                    kvskey.genid = (uint64_t) oid.generation;
                    kvskey.grouphash = get_object_group_id(kvskey.group, kvskey.shardid, kvskey.poolid);

                    derr << "shard " << (int)kvskey.shardid << dendl;
                    derr << "pool " << kvskey.poolid << dendl;
                    derr << "hash " << kvskey.grouphash << dendl;
                }

            }
        ASSERT_EQ(objects_set2.size(), available_objects.size());

    }

    void scan2() {
        Mutex::Locker locker(lock);
        EnterExit ee("scan");
        while (in_flight)
            cond.Wait(lock);
        vector<ghobject_t> objects;
        set<ghobject_t> objects_set, objects_set2;
        ghobject_t next, current;
        while (1) {
            //cerr << "scanning..." << std::endl;
            int r = store->collection_list(cid, current, ghobject_t::get_max(), 100,
                                           &objects, &next);
            ASSERT_EQ(r, 0);
            ASSERT_TRUE(sorted(objects));
            objects_set.insert(objects.begin(), objects.end());
            objects.clear();
            if (next.is_max()) break;
            current = next;
        }

        derr << " object set size " << objects_set.size() << dendl;
        derr << " available  size " << available_objects.size() << dendl;
        derr << " 1" << dendl;
        if (objects_set.size() != available_objects.size()) {
            for (set<ghobject_t>::iterator p = objects_set.begin();
                 p != objects_set.end();
                 ++p)
                if (available_objects.count(*p) == 0) {
                    derr << "+ " << *p << dendl;
                    ceph_abort();
                }
            for (set<ghobject_t>::iterator p = available_objects.begin();
                 p != available_objects.end();
                 ++p)
                if (objects_set.count(*p) == 0) {
                    derr << "can't find - " << *p << dendl;

                }
            cerr << " objects_set: " << objects_set.size() << std::endl;
            cerr << " available_set: " << available_objects.size() << std::endl;
            assert(0 == "badness");
        }

        ASSERT_EQ(objects_set.size(), available_objects.size());
        for (set<ghobject_t>::iterator i = objects_set.begin();
             i != objects_set.end();
             ++i) {
            ASSERT_GT(available_objects.count(*i), (unsigned)0);
        }

        int r = store->collection_list(cid, ghobject_t(), ghobject_t::get_max(),
                                       INT_MAX, &objects, 0);
        ASSERT_EQ(r, 0);
        objects_set2.insert(objects.begin(), objects.end());

        derr << " object set size " << objects_set2.size() << dendl;
        derr << " available  size " << available_objects.size() << dendl;

        for (set<ghobject_t>::iterator i = objects_set2.begin();
             i != objects_set2.end();
             ++i) {
            //ASSERT_GT(available_objects.count(*i), (unsigned)0);
            if (available_objects.count(*i) == 0) {
                derr << "+ " << *i << dendl;
            }
        }

        derr << "!1" << dendl;
        for (set<ghobject_t>::iterator p = available_objects.begin();
             p != available_objects.end();
             ++p)
            if (objects_set2.count(*p) == 0) {
                derr << "can't find - " << *p << dendl;
            }

        derr << "check " << objects_set2.size() << dendl;
        derr << "check2 " << available_objects.size() << dendl;
        ASSERT_EQ(objects_set2.size(), available_objects.size());
        derr << "done" << dendl;

            }
    void stat() {
        EnterExit ee("stat");
        ghobject_t hoid;
        uint64_t expected;
        {
            Mutex::Locker locker(lock);
            EnterExit ee("stat lock1");
            if (!can_unlink())
                return ;
            hoid = get_uniform_random_object();
            in_flight_objects.insert(hoid);
            available_objects.erase(hoid);
            ++in_flight;
            expected = contents[hoid].data.length();
        }
        struct stat buf;
        int r = store->stat(cid, hoid, &buf);
        ASSERT_EQ(0, r);
        assert((uint64_t)buf.st_size == expected);
        ASSERT_TRUE((uint64_t)buf.st_size == expected);
        {
            Mutex::Locker locker(lock);
            EnterExit ee("stat lock2");
            --in_flight;
            cond.Signal();
            in_flight_objects.erase(hoid);
            available_objects.insert(hoid);
        }
    }

    int unlink() {
        Mutex::Locker locker(lock);
        EnterExit ee("unlink");
        if (!can_unlink())
            return -ENOENT;
        ghobject_t to_remove = get_uniform_random_object();
        ObjectStore::Transaction t;
        t.remove(cid, to_remove);
        ++in_flight;
        available_objects.erase(to_remove);
        in_flight_objects.insert(to_remove);
        contents.erase(to_remove);
        int status = store->queue_transaction(osr, std::move(t), new C_SyntheticOnReadable(this, to_remove));
        return status;
    }

    void print_internal_state() {
        Mutex::Locker locker(lock);
        cerr << "available_objects: " << available_objects.size()
             << " in_flight_objects: " << in_flight_objects.size()
             << " total objects: " << in_flight_objects.size() + available_objects.size()
             << " in_flight " << in_flight << std::endl;
    }
};


void colsplittest(
        ObjectStore *store,
        unsigned num_objects,
        unsigned common_suffix_size,
        bool clones
) {
    ObjectStore::Sequencer osr("test");
    coll_t cid(spg_t(pg_t(0,52),shard_id_t::NO_SHARD));
    coll_t tid(spg_t(pg_t(1<<common_suffix_size,52),shard_id_t::NO_SHARD));
    int r = 0;

    {
        ObjectStore::Transaction t;
        t.create_collection(cid, common_suffix_size);
        r = apply_transaction(store, &osr, std::move(t));
        ASSERT_EQ(r, 0);
    }

    bufferlist small;
    small.append("small");
    {
        ObjectStore::Transaction t;
        for (uint32_t i = 0; i < (2 - (int)clones)*num_objects; ++i) {
            stringstream objname;
            objname << "obj" << i;
            ghobject_t a(hobject_t(
                    objname.str(),
                    "",
                    CEPH_NOSNAP,
                    i<<common_suffix_size,
                    52, ""));

            t.write(cid, a, 0, small.length(), small,
                    CEPH_OSD_OP_FLAG_FADVISE_WILLNEED);
            if (clones) {
                objname << "-clone";
                ghobject_t b(hobject_t(
                        objname.str(),
                        "",
                        CEPH_NOSNAP,
                        i<<common_suffix_size,
                        52, ""));
                t.clone(cid, a, b);
            }
            if (i % 100) {
                r = apply_transaction(store, &osr, std::move(t));
                ASSERT_EQ(r, 0);
                t = ObjectStore::Transaction();
            }
        }
        r = apply_transaction(store, &osr, std::move(t));
        ASSERT_EQ(r, 0);
    }

    {
        ObjectStore::Transaction t;
        t.create_collection(tid, common_suffix_size + 1);
        t.split_collection(cid, common_suffix_size+1, 1<<common_suffix_size, tid);
        r = apply_transaction(store, &osr, std::move(t));
        ASSERT_EQ(r, 0);
    }

    ObjectStore::Transaction t;
    vector<ghobject_t> objects;
    r = store->collection_list(cid, ghobject_t(), ghobject_t::get_max(),
                               INT_MAX, &objects, 0);

    ASSERT_EQ(r, 0);
    ASSERT_EQ(objects.size(), num_objects);
    unsigned size = 0;
    for (vector<ghobject_t>::iterator i = objects.begin();
         i != objects.end();
         ++i) {

        ASSERT_EQ(!!(i->hobj.get_hash() & (1<<common_suffix_size)), 0u);

        t.remove(cid, *i);

        if (++size > 100) {
            size = 0;

            r = apply_transaction(store, &osr, std::move(t));
            ASSERT_EQ(r, 0);
            t = ObjectStore::Transaction();
        }
    }

    if (size  > 0) {
        size = 0;

        r = apply_transaction(store, &osr, std::move(t));
        ASSERT_EQ(r, 0);
    }

    objects.clear();
    r = store->collection_list(tid, ghobject_t(), ghobject_t::get_max(),
                               INT_MAX, &objects, 0);

    ASSERT_EQ(r, 0);
    ASSERT_EQ(objects.size(), num_objects);

    for (vector<ghobject_t>::iterator i = objects.begin();
         i != objects.end();
         ++i) {

        ASSERT_EQ(!(i->hobj.get_hash() & (1<<common_suffix_size)), 0u);

        t.remove(tid, *i);

        if (++size > 100) {
            size = 0;

            r = apply_transaction(store, &osr, std::move(t));
            ASSERT_EQ(r, 0);
            t = ObjectStore::Transaction();
        }
    }

    t.remove_collection(cid);

    t.remove_collection(tid);

    r = apply_transaction(store, &osr, std::move(t));

    ASSERT_EQ(r, 0);
}


void doSyntheticTest(boost::scoped_ptr<ObjectStore>& store,
                     int num_ops,
                     uint64_t max_obj, uint64_t max_wr, uint64_t align) {
    ObjectStore::Sequencer osr("test");
    MixedGenerator gen(555);
    gen_type rng(time(NULL));
    coll_t cid(spg_t(pg_t(0, 555), shard_id_t::NO_SHARD));

    g_ceph_context->_conf->set_val("bluestore_fsck_on_mount", "false");
    g_ceph_context->_conf->set_val("bluestore_fsck_on_umount", "false");
    g_ceph_context->_conf->apply_changes(NULL);

    SyntheticWorkloadState test_obj(store.get(), &gen, &rng, &osr, cid,
                                    max_obj, max_wr, align);
    test_obj.init();
    for (int i = 0; i < num_ops / 10; ++i) {
        if (!(i % 500)) cerr << "seeding object " << i << std::endl;
        test_obj.touch();
    }

    for (int i = 0; i < num_ops; ++i) {
        if (!(i % 1000)) {
            cerr << "Op " << i << std::endl;
            test_obj.print_internal_state();
        }
        boost::uniform_int<> true_false(0, 999);
        int val = true_false(rng);
        if (val > 998) {
            derr << "----fsck true" << dendl;
            test_obj.fsck(true);
        } else if (val > 997) {
            derr << "----fsck false" << dendl;
            test_obj.fsck(false);
        } else if (val > 970) {
            derr << "----scan " << dendl;
            test_obj.scan();
        } else if (val > 950) {
            derr << "----stat " << dendl;
            test_obj.stat();
        } else if (val > 850) {
            derr << "----zero " << dendl;
            test_obj.zero();
        } else if (val > 800) {
            derr << "----unlink " << dendl;
            test_obj.unlink();
        } else if (val > 550) {
            derr << "----write " << dendl;
            test_obj.write();
        } else if (val > 500) {
            derr << "----clone " << dendl;
            test_obj.clone();
        } else if (val > 450) {
            derr << "----clone_range " << dendl;
            test_obj.clone_range();
        } else if (val > 300) {
            derr << "----stash " << dendl;
            test_obj.stash();
        } else if (val > 100) {
            derr << "----read " << dendl;
            test_obj.read();
        } else {
            derr << "----truncate " << dendl;
            test_obj.truncate();
        }
    }
    test_obj.wait_for_done();
    test_obj.shutdown();

    g_ceph_context->_conf->set_val("bluestore_fsck_on_mount", "true");
    g_ceph_context->_conf->set_val("bluestore_fsck_on_umount", "true");
    g_ceph_context->_conf->apply_changes(NULL);
    derr << "done" << dendl;
}


#endif //CEPH_KVSSTORE_TEST_IMPL_H
