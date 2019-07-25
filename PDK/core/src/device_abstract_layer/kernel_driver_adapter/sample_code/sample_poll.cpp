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

// default settings
int TOTAL_KV_COUNT = 5000;

// device run time structure
struct device_handle_t {
    kv_device_handle dev_hdl;
    kv_namespace_handle ns_hdl;
    kv_queue_handle sq_hdl;
    kv_queue_handle cq_hdl;
    int qdepth;
    // char *configfile;
};


typedef struct {
    kv_key *key;
    kv_value *value;
    kv_result retcode;
    kv_postprocess_function post_fn_data;

    // value read back
    kv_value *value_save;

    // for kv_exist
    char *buffer;
    int buffer_size;
    int buffer_count;
    bool complete;

    // for iterator
    kv_iterator_handle hiter;

    // depends if individual IO sync control is needed
    std::condition_variable done_cond;
    std::mutex mutex;
    std::atomic<int> done;
} iohdl_t;


void free_value(kv_value *value) {
    // free value
    if (value != NULL) {
        if (value->value != NULL) {
            free(value->value);
        }
        free(value);
    }
}

void free_key(kv_key *key) {
    // free key
    if (key != NULL) {
        if (key->key != NULL) {
            free(key->key);
        }
        free(key);
    }
}


// this function will be called after completion of a command
void interrupt_func(void *data, int number) {
    (void) data;
    (void) number;
    // printf("inside interrupt\n");
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
// Please note ctx is internal data for read only.
void on_IO_complete_func(kv_io_context *ctx) {
    // depends if you allocated space for each command, you can manage
    // allocated spaces here. This should in sync with your IO cycle
    // management strategy.
    //
    // get completed async command results, free any allocated spaces if necessary.
    //
    // printf("finished one command\n");
    iohdl_t *iohdl = (iohdl_t *) ctx->private_data;
    iohdl->retcode = ctx->retcode;
    iohdl->complete = true;

    // this buffer was supplied by ASYNC call, just put it into IO handle
    // for easier processing
    if (ctx->opcode == KV_OPC_CHECK_KEY_EXIST) {
        iohdl->buffer_size = ctx->result.buffer_size;
        iohdl->buffer_count = ctx->result.buffer_count;
    }

    // this iterator is returned in response to ASYNC iterator open call
    // must be retrieved by caller for iterator next calls
    // then closed after use.
    if (ctx->opcode == KV_OPC_OPEN_ITERATOR) {
        iohdl->hiter = ctx->result.hiter;
        iohdl->buffer_size = ctx->result.buffer_size;
        iohdl->buffer_count = ctx->result.buffer_count;
    }

    // printf("command finished with result 0x%X\n", iohdl->retcode);
    // std::unique_lock<std::mutex> lock(iohdl->mutex);
    // iohdl->done = 1;
    // iohdl->done_cond.notify_one();
}

// fill key value with random data
void populate_key_value_startwith(uint32_t keystart, kv_key *key, kv_value *value) {

    char *buffer = (char *)key->key;
    uint32_t blen = key->length;

    uint8_t *base = (uint8_t *)&keystart;

    /*
    uint8_t prefix[4] = {
            *(base + 0),
            *(base + 1),
            *(base + 2),
            *(base + 3) };
    */

    // if prefix is 0, skip prefix setup in key value
    if (keystart != 0) {
        assert(blen > 4);
        // copy 4 bytes of prefix
        memcpy(buffer, (char *)base, 4);
        buffer += 4;
        blen -= 4;
    }

    // printf("got key 4MSB 0x%X\n", keystart);
    // printf("key 4MSB 0x%X\n", *(uint32_t *)(buffer - 4));

    // use this to see deterministic sequence of keys
    static unsigned long long current_time = 0;
    current_time++;

    char timestr[128];
    snprintf(timestr, 128, "%llu", current_time);
    memcpy(buffer, timestr, std::min(blen, (uint32_t)sizeof(timestr)));
    // done with key

    // generate value
    buffer = (char *) value->value;
    blen = value->length;
    // ending with null
    buffer[blen - 1] = 0;

    // copy key, then add random value
    memcpy(buffer, key->key, key->length);
    blen -= key->length - 1;
    buffer += key->length;
    for (unsigned int i = 0; i < blen - 1; i++) {
        unsigned int j = '1' + std::rand() % 30;
        buffer[i] = j;
    }

    // printf("genrate a key %s, value %s\n", (char *) (key->key), (char *)value->value);
    // printf("genrate a key %s\n", (char *) (key->key));
}

// set random values for those iohdls
void populate_iohdls(iohdl_t *iohdls, uint32_t prefix, int klen, int vlen, uint32_t count) {
    // set up IO handle and postprocessing

    for (uint32_t i = 0; i < count; i++) {
        // populate random value
        iohdl_t *iohdl = iohdls + i;
        populate_key_value_startwith(prefix, iohdl->key, iohdl->value);

        kv_value *value = iohdl->value;
        kv_value *value_save = iohdl->value_save;

        memcpy(value_save->value, value->value, value->length);
    }
}

// create array of io handles that hold keys and values
void create_iohdls(iohdl_t **iohdls, uint32_t prefix, int klen, int vlen, uint32_t count) {
    *iohdls = (iohdl_t *) calloc(count, sizeof(iohdl_t));

    for (uint32_t i = 0; i < count; i++) {
        iohdl_t *iohdl = (*iohdls) + i;

        iohdl->post_fn_data.post_fn = on_IO_complete_func;
        iohdl->post_fn_data.private_data = (void *) iohdl;

        // allocate key value for reuse
        kv_key *key = (kv_key *) malloc(sizeof(kv_key));
        key->length = klen;
        key->key = malloc(key->length);
        memset(key->key, 0, key->length);

        // value for insert
        kv_value *val = (kv_value *) malloc(sizeof(kv_value));
        val->length = vlen;
        val->actual_value_size = vlen;
        val->value = malloc(vlen);
        memset(val->value, 0, val->length);
        val->offset = 0;

        // value for read back validation
        kv_value *value_save = (kv_value *) malloc(sizeof(kv_value));
        value_save->length = vlen;
        value_save->actual_value_size = vlen;
        value_save->value = malloc(vlen);
        memset(value_save->value, 0, vlen);
        value_save->offset = 0;

        // save key value to the IO handle
        iohdl->key = key;
        iohdl->value = val;
        iohdl->value_save = value_save;
    }
}

void free_iohdls(iohdl_t *iohdls, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        iohdl_t *iohdl = iohdls + i;
        free_key(iohdl->key);
        free_value(iohdl->value);
        free_value(iohdl->value_save);
    }
    free(iohdls);
}

// read back and compare
uint64_t process_one_round_read(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, iohdl_t *iohdls, uint32_t count) {

    uint64_t start = current_timestamp();
    // insert key value
    for (uint32_t i = 0; i < count; i++) {
        // insert a key value pair
        // Please note key and value is part of IO handle inside post_fn_data
        kv_key *key = (iohdls + i)->key;
        // printf("reading key: %s\n", key->key);
        // prefix of keys
        // printf("reading key 4MSB: 0x%X\n", *(uint32_t *)&key->key);

        kv_value *value = (iohdls + i)->value;
        memset(value->value, '0', value->length);
        value->actual_value_size = 0;
        (iohdls + i)->complete = false;
        kv_postprocess_function *post_fn_data = &((iohdls + i)->post_fn_data);
        kv_result res = kv_retrieve(sq_hdl, ns_hdl, key, KV_RETRIEVE_OPT_DEFAULT, value, post_fn_data);
        while (res != KV_SUCCESS) {
            printf("kv_retrieve failed with error: 0x%X\n", res);
            res = kv_retrieve(sq_hdl, ns_hdl, key, KV_RETRIEVE_OPT_DEFAULT, value, post_fn_data);
        }
    }

    // poll for completion
    uint32_t total_done = 0;
    int i = 0;
    while (total_done < count) {
        if((iohdls + i)->complete){
            total_done++;
            i++;
        }
        uint32_t finished = 0;
        kv_result res = kv_poll_completion(cq_hdl, 0, &finished);
        if (res != KV_SUCCESS && res != KV_WRN_MORE) {
            printf("kv_poll_completion error! exit\n");
            exit(1);
        }
    }
    uint64_t end = current_timestamp();

    // compare read value with write value
    for (uint32_t i = 0; i < count; i++) {
        // printf("validating key: %s\n", (iohdls + i)->key->key);
        if (memcmp((iohdls + i)->value_save->value, (iohdls + i)->value->value, (iohdls + i)->value->length)) {
            // printf("failed: value read not the same as value written, key %s\n value + 16 %s\n value %s\n", (iohdls + i)->key->key, (iohdls + i)->value->value + 16, (iohdls + i)->value_save->value);
            // XXX please check if any duplicate keys are used, only unique keys should
            // be used, otherwise you may get false positive.
            printf("failed: value read not the same as value written\n");
            // printf("Note: please only use unique keys for this validation\n");
            exit(1);
        }

        if ((iohdls + i)->value_save->actual_value_size != (iohdls + i)->value->actual_value_size) {
            printf("failed: value full size not the same as value full size written\n");
            exit(1);
        }
    }

    // printf("polling total done: %d, inserted %d\n", total_done, count);
    // double iotime  = double(total) / count;
    // printf("average time per IO: %.1f (ns)\n", iotime);
    return (end - start);
}

// insert enough keys but avoid excessive queue contention
uint64_t process_one_round_write(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, iohdl_t *iohdls, uint32_t count) {

    uint64_t start = current_timestamp();
    // insert key value
    for (uint32_t i = 0; i < count; i++) {
        // insert a key value pair
        // Please note key and value is part of IO handle inside post_fn_data
        kv_key *key = (iohdls + i)->key;
        kv_value *value = (iohdls + i)->value;
        (iohdls + i)->complete = false;
        kv_postprocess_function *post_fn_data = &((iohdls + i)->post_fn_data);
        kv_result res = kv_store(sq_hdl, ns_hdl, key, value, KV_STORE_OPT_DEFAULT, post_fn_data);
        while (res != KV_SUCCESS) {
            printf("kv_store failed with error: 0x%X\n", res);
            res = kv_store(sq_hdl, ns_hdl, key, value, KV_STORE_OPT_DEFAULT, post_fn_data);
        }
    }

    // poll for completion
    uint32_t total_done = 0;
    int i = 0;
    while (total_done < count) {
        if((iohdls + i)->complete){
            total_done++;
            i++;
        }
        uint32_t finished = 0;
        kv_result res = kv_poll_completion(cq_hdl, 0, &finished);
        if (res != KV_SUCCESS && res != KV_WRN_MORE) {
            printf("kv_poll_completion error! exit\n");
            exit(1);
        }
    }
    uint64_t end = current_timestamp();

    // printf("polling total done: %d, inserted %d\n", total_done, count);
    // double iotime  = double(total) / count;
    // printf("average time per IO: %.1f (ns)\n", iotime);
    return (end - start);
}

// test cycle of read and write in rounds
void sample_main(device_handle_t *device_handle, int klen, int vlen, int tcount, int qdepth) {

    //kv_device_handle dev_hdl = device_handle->dev_hdl;
    kv_namespace_handle ns_hdl = device_handle->ns_hdl;
    kv_queue_handle sq_hdl = device_handle->sq_hdl;
    kv_queue_handle cq_hdl = device_handle->cq_hdl;

    // uint32_t prefix = 0xFFFF1234;
    uint32_t prefix = 0;
    iohdl_t *iohdls = NULL;

    // break it into many rounds to minimize q contention
    uint32_t count = qdepth;
    create_iohdls(&iohdls, prefix, klen, vlen, count);

    uint32_t left = tcount;
    // total IO time in ns
    uint64_t total_w = 0;
    uint64_t total_r = 0;

    while (left > 0) {
        if (left < count) {
            count = left;
        }
        left -= count;

        // set up new random key values
        populate_iohdls(iohdls, prefix, klen, vlen, count);

        // test write and read
        uint64_t time_w = process_one_round_write(ns_hdl, sq_hdl, cq_hdl, iohdls, count);
        total_w += time_w;

        uint64_t time_r = process_one_round_read(ns_hdl, sq_hdl, cq_hdl, iohdls, count);
        total_r += time_r;
    }

    // done, free memories
    free_iohdls(iohdls, qdepth);
}

void create_qpair(kv_device_handle dev_hdl,
    kv_queue_handle *sq_hdl,
    kv_queue_handle *cq_hdl,
    uint16_t sqid,
    uint16_t cqid,
    uint16_t q_depth)
{
    
    // create a CQ
    // ONLY DO THIS, if you have NOT created the CQ earlier
    create_cq(dev_hdl, cqid, q_depth, cq_hdl);

    // this is only for completion Q
    // this will kick off completion Q processing
    // kv_set_interrupt_handler(cq_hdl, int_handler); 

    // create a SQ
    create_sq(dev_hdl, sqid, q_depth, cqid, sq_hdl);
}


void create_device(device_handle_t *device_handle, const char *devpath, int qdepth) {
    // int klen = 16;
    // int vlen = 4096;
    // int qdepth = 64;
    kv_device_init_t dev_init;

    // you can supply your own configuration file
    dev_init.configfile = "";
    dev_init.devpath = devpath;
    dev_init.need_persistency = FALSE;
    dev_init.is_polling = TRUE;
    dev_init.queuedepth = qdepth;

    kv_device_handle dev_hdl = NULL;

    device_handle->qdepth = qdepth;

    // initialize the device
    kv_result ret = kv_initialize_device(&dev_init, &dev_hdl);
    if (ret != KV_SUCCESS) {
        printf("device creation error\n");
        exit(1);
    }

    kv_namespace_handle ns_hdl = NULL;
    get_namespace_default(dev_hdl, &ns_hdl);

    // set up interrupt handler
    // _kv_interrupt_handler int_func = {interrupt_func, (void *)4, 4};
    // kv_interrupt_handler int_handler = &int_func;
    kv_queue_handle sq_hdl;
    kv_queue_handle cq_hdl;

    // convenient wrapper function
    // create a SQ/CQ pair
    create_qpair(dev_hdl, &sq_hdl, &cq_hdl,
            1,  // sqid
            2,  // cqid
            qdepth); // q depth

    device_handle->dev_hdl = dev_hdl;
    device_handle->ns_hdl = ns_hdl;
    device_handle->sq_hdl = sq_hdl;
    device_handle->cq_hdl = cq_hdl;
    device_handle->qdepth = qdepth;
}


void shutdown_device(device_handle_t *device_handle) {
    if (device_handle == NULL) {
        return;
    }

    kv_device_handle dev_hdl = device_handle->dev_hdl;
    kv_namespace_handle ns_hdl = device_handle->ns_hdl;
    kv_queue_handle sq_hdl = device_handle->sq_hdl;
    kv_queue_handle cq_hdl = device_handle->cq_hdl;
    
    // delete queues
    if (kv_delete_queue(dev_hdl, sq_hdl) != KV_SUCCESS) {
        printf("kv_delete_queue failed\n");
        exit(1);
    }
    if (kv_delete_queue(dev_hdl, cq_hdl) != KV_SUCCESS) {
        printf("kv_delete_queue failed\n");
        exit(1);
    }

    // shutdown
    kv_delete_namespace(dev_hdl, ns_hdl);
    kv_cleanup_device(dev_hdl);
}


int main(int argc, char**argv) {

    char *device = NULL;
    
    if (argc <  3) {
        printf("Please run\n  %s <number of keys> <device>\n", argv[0]);
        exit(1);

    } else {
        TOTAL_KV_COUNT = std::stoi(argv[1]);
        printf("insert keys: %d\n", TOTAL_KV_COUNT);

        device = argv[2];
    }

    int klen = 16;
    int vlen = 4096;
    int qdepth = 64;

    device_handle_t *device1 = (device_handle_t *) malloc(sizeof (device_handle_t));
    device_handle_t *device2 = (device_handle_t *) malloc(sizeof (device_handle_t));

    printf("creating device 1\n");
    create_device(device1, device, qdepth);
    printf("creating device 2\n");
    create_device(device2, device, qdepth);

    // start a thread to insert key values through submission Q
    // then read them back to validate correctness
    printf("starting operation on device 1\n");
    std::thread th = std::thread(sample_main, device1, klen, vlen, TOTAL_KV_COUNT, qdepth);

    printf("starting operation on device 2\n");
    std::thread th1 = std::thread(sample_main, device2, klen, vlen, TOTAL_KV_COUNT, qdepth);

    if (th.joinable()) {
        th.join();
    }
    printf("stop operation on device 1\n");

    if (th1.joinable()) {
        th1.join();
    }
    printf("stop operation on device 2\n");

    printf("shutdown all devices\n");
    shutdown_device(device1);
    shutdown_device(device2);
    free(device1);
    free(device2);

    printf("all done\n");
    return 0;
}
