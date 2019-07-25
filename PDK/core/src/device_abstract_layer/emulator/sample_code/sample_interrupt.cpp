#include "assert.h"
#include <string>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <mutex>
#include <chrono>
#include <thread>
#include <set>
#include <unordered_set>
#include <map>
#include <unordered_map>
#include <iomanip>
#include <ctime>
#include <condition_variable>
#include <atomic>
#include "kvs_adi.h"

/* total kv to insert */
int TOTAL_KV_COUNT = 10000;

typedef struct {
    kv_key *key;
    kv_value *value;
    kv_result retcode;

    std::condition_variable done_cond;
    std::mutex mutex;
    std::atomic<int> done;
} iohdl_t;


void free_kv(kv_key *key, kv_value *value) {
    // free key
    if (key != NULL) {
        if (key->key != NULL) {
            free(key->key);
        }
        free(key);
    }

    // free value
    if (value != NULL) {
        if (value->value != NULL) {
            free(value->value);
        }
        free(value);
    }
}

// add check queue shutdown call
// add SQ --> CQ can be done through a middle queue
//
// entry point for polling thread
void poll_CQ(kv_queue_handle cq_hdl, uint32_t timeout) {
    // set up polling
    kv_result res = KV_SUCCESS;
    uint32_t count = 100;
    while (res != KV_ERR_QUEUE_IN_SHUTDOWN) {
        count = 100;
        res = kv_poll_completion(cq_hdl, timeout, &count);
    }

    printf("polling done\n");
}

// this function will be called after completion of a command
void interrupt_func(void *data, int number) {
    (void) data;
    (void) number;

    //printf("inside interrupt\n");
}

int create_sq(kv_device_handle dev_hdl, uint16_t sqid, uint16_t qsize, uint16_t cqid, kv_queue_handle *quehdl) {

    kv_queue qinfo;
    qinfo.queue_id = sqid;
    qinfo.queue_size = qsize;
    qinfo.completion_queue_id = cqid;
    qinfo.queue_type = SUBMISSION_Q_TYPE;
    qinfo.extended_info = NULL;

    if (KV_SUCCESS != kv_create_queue(dev_hdl, &qinfo, quehdl)) {
        printf("submission queue creation failed\n");
        exit(1);
    }
    return 0;
}

int create_cq(kv_device_handle dev_hdl, uint16_t cqid, uint16_t qsize, kv_queue_handle *quehdl) {

    kv_queue qinfo;
    qinfo.queue_id = cqid;
    qinfo.queue_size = qsize;
    qinfo.completion_queue_id = 0;
    qinfo.queue_type = COMPLETION_Q_TYPE;
    qinfo.extended_info = NULL;

    if (KV_SUCCESS != kv_create_queue(dev_hdl, &qinfo, quehdl)) {
        printf("completion queue creation failed\n");
        exit(1);
    }
    return 0;
}

uint64_t current_timestamp() {
    std::chrono::system_clock::time_point timepoint = std::chrono::system_clock::now();
    uint64_t nano_from_epoch = std::chrono::duration_cast<std::chrono::nanoseconds>(timepoint.time_since_epoch()).count();
    return nano_from_epoch;
}

// post processing function for a command
// op->private_data is the data caller passed in
void on_IO_complete_func(kv_io_context *ctx) {
    // depends if you allocated space for each command, you can manage
    // allocated spaces here. This should in sync with your IO cycle
    // management strategy.
    //
    // get completed async command results, free any allocated spaces if necessary.
    //
    // printf("finished one command\n");
    iohdl_t *iohdl = (iohdl_t *) ctx->private_data;

    std::unique_lock<std::mutex> lock(iohdl->mutex);
    iohdl->key = const_cast<kv_key*> (ctx->key);
    iohdl->value = ctx->value;
    iohdl->retcode = ctx->retcode;

    printf("command finished with result 0x%X\n", iohdl->retcode);
    iohdl->done = 1;
    iohdl->done_cond.notify_one();

}

// fill key value with random data
void populate_key_value_startwith(uint32_t keystart, kv_key *key, kv_value *value) {

    char *buffer = (char *)key->key;
    uint32_t blen = key->length;

    uint8_t *base = (uint8_t *)&keystart;
    uint8_t prefix[4] = {
            *(base + 3),
            *(base + 2),
            *(base + 1),
            *(base + 0) };

    // if prefix is 0, skip prefix setup in key value
    if (keystart != 0) {
        assert(blen > 4);
        // copy 4 bytes of prefix
        memcpy(buffer, (char *)prefix, 4);
        buffer += 4;
        blen -= 4;
    }

    int rand_num = std::rand();
    unsigned long long current_time = current_timestamp() + rand_num;
    char timestr[16];
    snprintf(timestr, 16, "%llu", current_time);
    memcpy(buffer, timestr, std::min(blen, (uint32_t)sizeof(timestr)));
    // done with key

    // generate value
    buffer = (char *) value->value;
    blen = value->length;
    // ending with null
    buffer[blen - 1] = 0;

    // create random value
    for (unsigned int i = 0; i < blen - 1; i++) {
        unsigned int j = 'a';//(char)(i%128); //std::rand() % 128;
        buffer[i] = j;
    }

    printf("generate a key %s, value legngth = %d\n", (char *) (key->key), value->length);
}

// how many keys to insert
void insert_kv_store(kv_device_handle dev_hdl, kv_queue_handle sq_hdl, const int klen, const int vlen, int count) {
    // set up IO handle and postprocessing
    iohdl_t *iohdl = (iohdl_t *) malloc(sizeof(iohdl_t));
    memset(iohdl, 0, sizeof(iohdl_t));
    kv_postprocess_function *post_fn_data = (kv_postprocess_function *)
                                malloc(sizeof(kv_postprocess_function));
    post_fn_data->post_fn = on_IO_complete_func;
    post_fn_data->private_data = (void *) iohdl;

    // allocate key value for reuse
    kv_key *key = (kv_key *) malloc(sizeof(kv_key));
    key->length = klen;
    key->key = malloc(key->length);
    memset(key->key, 0, key->length);

    // value for insert
    kv_value *val = (kv_value *) malloc(sizeof(kv_value));
    val->length = vlen;

    val->actual_value_size = vlen;
    val->value = malloc(val->length);
    memset(val->value, 0, val->length);
    val->offset = 0;

    // value for read back validation
    kv_value *valread = (kv_value *) malloc(sizeof(kv_value));
    valread->length = vlen;

    valread->actual_value_size = vlen;
    valread->value = malloc(val->length);
    memset(valread->value, 0, valread->length);
    valread->offset = 0;

    // save key value to the IO handle
    iohdl->key = key;
    iohdl->value = val;

    // set up namespace
    kv_namespace_handle ns_hdl = NULL;
    get_namespace_default(dev_hdl, &ns_hdl);

    // insert key value
    for (int i = 0; i < count; i++) {

        // populate key value with random content
        populate_key_value_startwith(0, iohdl->key, iohdl->value);

        iohdl->done  = 0;

        // insert a key value pair
        // Please note key and value is part of IO handle inside post_fn_data
        kv_result res = kv_store(sq_hdl, ns_hdl, key, val, KV_STORE_OPT_DEFAULT, post_fn_data);
        while (res != KV_SUCCESS) {
            printf("kv_store failed with error: 0x%X\n", res);
            res = kv_store(sq_hdl, ns_hdl, key, val, KV_STORE_OPT_DEFAULT, post_fn_data);
        }

        // check result asynchronously
        std::unique_lock<std::mutex> lock(iohdl->mutex);
        while (iohdl->done == 0) {
            iohdl->done_cond.wait(lock);
        }
        // expected result
        if (iohdl->retcode != KV_SUCCESS) {
            printf("kv_store a new key failed: 0x%X\n", iohdl->retcode);
            exit(1);
        } else {
            printf("kv_store succeeded: value length = %d\n", val->length);
        }
        iohdl->done = 0;
        iohdl->retcode = KV_ERR_COMMAND_SUBMITTED;
        lock.unlock();


        // save original
        // read it back
        kv_value *valwritten = iohdl->value;
        iohdl->value = valread;
        memset(valread->value, 0, valread->length);
        
        printf("value read = %p\n", valread);
        // read value
        res = kv_retrieve(sq_hdl, ns_hdl, key, KV_RETRIEVE_OPT_DEFAULT, valread, post_fn_data);
        while (res != KV_SUCCESS) {
            printf("kv_retrieve failed with error: 0x%X\n", res);
            res = kv_retrieve(sq_hdl, ns_hdl, key, KV_RETRIEVE_OPT_DEFAULT, valread, post_fn_data);
        }

        // check result asynchronously
        lock.lock();
        while (iohdl->done == 0) {
            iohdl->done_cond.wait(lock);
        }
        lock.unlock();

        // expected result
        if (iohdl->retcode != KV_SUCCESS) {
            printf("kv_retrieve failed: 0x%X, done = %d\n", iohdl->retcode, iohdl->done.load());
            exit(1);
        }

        // compare values
        if (valread->length != valwritten->length) {
            printf("value size is different: %u, %u\n", valread->length, valwritten->length);
            exit(1);
        }

        if (memcmp(valread->value, valwritten->value, valread->length) != 0) {
            printf("value is different: %u, %u, vlen = %d\n", valread->length, valwritten->length, vlen);
            printf("value is different: %p, %s, %s\n", valread, (char*)valread->value, (char*)valwritten->value);
            exit(1);
        }

        printf("kv_retrieve succeeded\n");

        // restore IO handle to original
        valread = iohdl->value;
        iohdl->value = valwritten;
    }

    // done, free memories
    free(valread->value);
    free(valread);
    free_kv(iohdl->key, iohdl->value);
    free(post_fn_data);
    free(iohdl);
}

void create_qpair(kv_device_handle dev_hdl,
    std::map<kv_queue_handle, kv_queue_handle>& qpairs,
    kv_queue_handle *sq_hdl,
    kv_queue_handle *cq_hdl,
    uint16_t sqid,
    uint16_t cqid,
    uint16_t q_depth,
    kv_interrupt_handler int_handler) {
    
    // create a CQ
    // ONLY DO THIS, if you have NOT created the CQ earlier
    create_cq(dev_hdl, cqid, q_depth, cq_hdl);

    // NOT doing polling
    // see sample_poll.cpp for reference
    // completion q poll thread
    // std::thread th = std::thread(poll_CQ, *cq_hdl, 0);
    // th.detach();

    // this is only for completion Q
    // this will kick off completion Q processing
    kv_set_interrupt_handler(*cq_hdl, int_handler); 

    // create a SQ
    create_sq(dev_hdl, sqid, q_depth, cqid, sq_hdl);

    qpairs.insert(std::make_pair(*sq_hdl, *cq_hdl));

}


int main(int argc, char**argv) {
    
    if (argc <  2) {
        printf("Please run\n  %s <number of keys>\n", argv[0]);
        printf("Default: %d\n", TOTAL_KV_COUNT);
    } else {
        TOTAL_KV_COUNT = std::stoi(argv[1]);
        printf("Insert keys: %d\n", TOTAL_KV_COUNT);
    }

    int klen = 16;
    int vlen = 4096;

    kv_device_init_t dev_init;

    // you can supply your own configuration file
    dev_init.configfile = "./kvssd_emul.conf";

    // the emulator device path is fixed.
    dev_init.devpath = "/dev/kvemul";
    dev_init.need_persistency = FALSE;
    dev_init.is_polling = FALSE;

    kv_device_handle dev_hdl = NULL;

    // initialize the device
    kv_result ret = kv_initialize_device(&dev_init, &dev_hdl);
    if (ret != KV_SUCCESS) {
        printf("device creation error\n");
        exit(1);
    }

    // set up interrupt handler
    _kv_interrupt_handler int_func = {interrupt_func, (void *)4, 4};
    kv_interrupt_handler int_handler = &int_func;

    // to keep track all opened queue pairs
    std::map<kv_queue_handle, kv_queue_handle> qpairs;
    kv_queue_handle sq_hdl;
    kv_queue_handle cq_hdl;

    // convenient wrapper function
    // create a SQ/CQ pair, and start a thread to insert key value pairs
    create_qpair(dev_hdl, qpairs,  &sq_hdl, &cq_hdl,
            1,  // sqid
            2,  // cqid
            64, // q depth
            int_handler);


    // start a thread to insert key values through submission Q
    printf("starting thread to insert key values\n");
    std::thread th = std::thread(insert_kv_store, dev_hdl, sq_hdl, klen, vlen, TOTAL_KV_COUNT);
    if (th.joinable()) {
        th.join();
    }

    // graceful shutdown
    // watch if all Qs are done
    for (auto& qpair : qpairs) {
        kv_queue_handle sqhdl = qpair.first;
        kv_queue_handle cqhdl = qpair.second;
        while (get_queued_commands_count(cqhdl) > 0 || get_queued_commands_count(sqhdl) > 0) {
            // wait for CQ to complete before shutdown
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    
    /*
    // delete queues
    for (auto& p : qpairs) {
        if (kv_delete_queue(dev_hdl, p.first) != KV_SUCCESS) {
            printf("kv_delete_queue failed\n");
            exit(1);
        }
        if (kv_delete_queue(dev_hdl, p.second) != KV_SUCCESS) {
            printf("kv_delete_queue failed\n");
            exit(1);
        }
    }
    */
    // shutdown
    kv_cleanup_device(dev_hdl);
    fprintf(stderr, "done\n");
    return 0;
}
