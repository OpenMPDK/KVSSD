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
int TOTAL_KV_COUNT = 10000;
int g_KEYLEN_FIXED = 0;
int g_KEYLEN = 16;

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

    if (ctx->opcode == KV_OPC_CHECK_KEY_EXIST) {
        iohdl->buffer_size = ctx->result.buffer_size;
        iohdl->buffer_count = ctx->result.buffer_count;
    }

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
        unsigned int j = '0' + std::rand() % 30;
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
        memset(value->value, 0, value->length);

        kv_postprocess_function *post_fn_data = &((iohdls + i)->post_fn_data);
        kv_result res = kv_retrieve(sq_hdl, ns_hdl, key, KV_RETRIEVE_OPT_DEFAULT, value, post_fn_data);
        while (res != KV_SUCCESS) {
            printf("kv_retrieve failed with error: 0x%X\n", res);
            res = kv_retrieve(sq_hdl, ns_hdl, key, KV_RETRIEVE_OPT_DEFAULT, value, post_fn_data);
        }
    }

    // poll for completion
    uint32_t total_done = 0;
    while (total_done < count) {
        uint32_t finished = 0;
        kv_result res = kv_poll_completion(cq_hdl, 0, &finished);
        if (res != KV_SUCCESS && res != KV_WRN_MORE) {
            printf("kv_poll_completion error! exit\n");
            exit(1);
        }
        total_done += finished;
    }
    uint64_t end = current_timestamp();

    // compare read value with write value
    for (uint32_t i = 0; i < count; i++) {
        // printf("validating key: %s\n", (iohdls + i)->key->key);
        if (memcmp((iohdls + i)->value_save->value, (iohdls + i)->value->value, (iohdls + i)->value->length)) {
            // printf("failed: value read not the same as value written, key %s.\n", (iohdls + i)->key->key);
            // XXX please check if any duplicate keys are used, only unique keys should
            // be used, otherwise you may get false positive.
            printf("failed: value read not the same as value written\n");
            // printf("Note: please only use unique keys for this validation\n");
            exit(1);
        }
    }

    // printf("polling total done: %d, inserted %d\n", total_done, count);
    // double iotime  = double(total) / count;
    // printf("average time per IO: %.1f (ns)\n", iotime);
    return (end - start);
}


/*
uint64_t process_one_round_write(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, iohdl_t *iohdls, uint32_t count);
    process_one_round_write(ns_hdl, sq_hdl, cq_hdl, iohdls, count);
*/
uint64_t process_one_round_read_partial(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, iohdl_t *iohdls, uint32_t count) {


    uint64_t start = current_timestamp();
    // insert key value
    for (uint32_t i = 0; i < count; i++) {
        // insert a key value pair
        // Please note key and value is part of IO handle inside post_fn_data
        kv_key *key = (iohdls + i)->key;

        // use another buffer to read back
        kv_value *value = (iohdls + i)->value;
        kv_value *value_save = (iohdls + i)->value_save;
        memcpy(value_save->value, value->value, value->length);
        value_save->actual_value_size = value->actual_value_size;

        // printf("original value XXX\n%s\n", value->value);
        // printf("copied value YYY key: %s\n%s\n", key->key, value_save->value);
        memset(value->value, 0, value->length);

        // reset full size to be filled
        (iohdls + i)->value->actual_value_size = 0;
        // set offset for retrieve
        (iohdls + i)->value->offset = std::rand() % (iohdls + i)->value->length;

        kv_postprocess_function *post_fn_data = &((iohdls + i)->post_fn_data);
        kv_result res = kv_retrieve(sq_hdl, ns_hdl, key, KV_RETRIEVE_OPT_DEFAULT, value, post_fn_data);
        while (res != KV_SUCCESS) {
            printf("kv_retrieve failed with error: 0x%X\n", res);
            res = kv_retrieve(sq_hdl, ns_hdl, key, KV_RETRIEVE_OPT_DEFAULT, value, post_fn_data);
        }
    }

    // poll for completion
    uint32_t total_done = 0;
    while (total_done < count) {
        uint32_t finished = 0;
        kv_result res = kv_poll_completion(cq_hdl, 0, &finished);
        if (res != KV_SUCCESS && res != KV_WRN_MORE) {
            printf("kv_poll_completion error! exit\n");
            exit(1);
        }
        total_done += finished;
    }
    uint64_t end = current_timestamp();

    // compare read value with write value
    for (uint32_t i = 0; i < count; i++) {
        // printf("validating key: %s\n", (iohdls + i)->key->key);
        uint32_t retrieved_len = (iohdls + i)->value->length;
        if (retrieved_len != ((iohdls + i)->value_save->length - (iohdls + i)->value->offset)) {
            printf("retrieved partial value length not matched\n");
            exit(1);
        }

        if (memcmp((char *)(iohdls + i)->value_save->value + (iohdls + i)->value->offset, (char *)(iohdls + i)->value->value, retrieved_len)) {
            printf("partial value read at offset %d, len %u not the same as value written, key %s.\n", (iohdls + i)->value->offset, retrieved_len, (char *)(iohdls + i)->key->key);
            // printf("partial value read is: XXX\n%s\n", (char *)(iohdls + i)->value->value);
            // printf("full value read is: XXX\n%s\n", (char *)(iohdls + i)->value_save->value);
            exit(1);
        }

        if ((iohdls + i)->value_save->actual_value_size != (iohdls + i)->value->actual_value_size) {
            printf("returned full value size doesn't match from partial read\n");
        }
    }

    // reset
    for (uint32_t i = 0; i < count; i++) {
        // reset offset after testing
        kv_value *value = (iohdls + i)->value;
        kv_value *value_save = (iohdls + i)->value_save;
        value->offset = 0;
        memcpy(value->value, value_save->value, value_save->length);
        value->length = value_save->length;
    }

    // printf("polling total done: %d, inserted %d\n", total_done, count);
    // double iotime  = double(total) / count;
    // printf("average time per IO: %.1f (ns)\n", iotime);
    return (end - start);
}


uint64_t process_one_round_read_offset_invalid(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, iohdl_t *iohdls, uint32_t count) {

    uint64_t start = current_timestamp();
    // insert key value
    for (uint32_t i = 0; i < count; i++) {
        // insert a key value pair
        // Please note key and value is part of IO handle inside post_fn_data
        kv_key *key = (iohdls + i)->key;

        // set wrong offset for retrieve
        kv_value *value = (iohdls + i)->value;
        value->offset = std::rand() + 1 + (iohdls + i)->value->length;

        kv_postprocess_function *post_fn_data = &((iohdls + i)->post_fn_data);
        kv_result res = kv_retrieve(sq_hdl, ns_hdl, key, KV_RETRIEVE_OPT_DEFAULT, value, post_fn_data);
        while (res != KV_SUCCESS) {
            printf("kv_retrieve failed with error: 0x%X\n", res);
            res = kv_retrieve(sq_hdl, ns_hdl, key, KV_RETRIEVE_OPT_DEFAULT, value, post_fn_data);
        }
    }

    // poll for completion
    uint32_t total_done = 0;
    while (total_done < count) {
        uint32_t finished = 0;
        kv_result res = kv_poll_completion(cq_hdl, 0, &finished);
        if (res != KV_SUCCESS && res != KV_WRN_MORE) {
            printf("kv_poll_completion error! exit\n");
            exit(1);
        }
        total_done += finished;
    }
    uint64_t end = current_timestamp();

    // compare read value with write value
    for (uint32_t i = 0; i < count; i++) {
        // printf("validating key: %s\n", (iohdls + i)->key->key);
        if ((iohdls + i)->retcode != KV_ERR_VALUE_OFFSET_INVALID) {
            printf("test invalid offset read failed\n");
            exit(1);
        }
    }

    for (uint32_t i = 0; i < count; i++) {
        // reset offset after testing
        kv_value *value = (iohdls + i)->value;
        value->offset = 0;
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
        kv_postprocess_function *post_fn_data = &((iohdls + i)->post_fn_data);
        kv_result res = kv_store(sq_hdl, ns_hdl, key, value, KV_STORE_OPT_DEFAULT, post_fn_data);
        while (res != KV_SUCCESS) {
            printf("kv_store failed with error: 0x%X\n", res);
            res = kv_store(sq_hdl, ns_hdl, key, value, KV_STORE_OPT_DEFAULT, post_fn_data);
        }
    }

    // poll for completion
    uint32_t total_done = 0;
    while (total_done < count) {
        uint32_t finished = 0;
        kv_result res = kv_poll_completion(cq_hdl, 0, &finished);
        if (res != KV_SUCCESS && res != KV_WRN_MORE) {
            printf("kv_poll_completion error! exit\n");
            exit(1);
        }
        total_done += finished;
    }
    uint64_t end = current_timestamp();


    // this allows overwrite, so all should succeed
    for (uint32_t i = 0; i < count; i++) {
        // insert a key value pair
        // Please note key and value is part of IO handle inside post_fn_data
        if ((iohdls + i)->retcode != KV_SUCCESS) {
            printf("kv_store key test failed: item %u: return code 0x%03X\n", i, (iohdls + i)->retcode);
            exit(1);
        }
    }

    // printf("polling total done: %d, inserted %d\n", total_done, count);
    // double iotime  = double(total) / count;
    // printf("average time per IO: %.1f (ns)\n", iotime);
    return (end - start);
}

// insert a key
uint64_t insert_key(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, kv_key *key, kv_value *value, uint32_t count) {

    iohdl_t iohdl;
    iohdl.key = key;
    iohdl.value = value;
    kv_postprocess_function post_fn_data = { on_IO_complete_func, &iohdl};

    kv_result res = kv_store(sq_hdl, ns_hdl, key, value, KV_STORE_OPT_DEFAULT, &post_fn_data);
    while (res != KV_SUCCESS) {
        printf("insert_key failed with error: 0x%03X\n", res);
        res = kv_store(sq_hdl, ns_hdl, key, value, KV_STORE_OPT_DEFAULT, &post_fn_data);
    }

    // poll for completion
    uint32_t total_done = 0;
    while (total_done < 1) {
        uint32_t finished = 0;
        kv_result res = kv_poll_completion(cq_hdl, 0, &finished);
        if (res != KV_SUCCESS && res != KV_WRN_MORE) {
            printf("kv_poll_completion error! exit\n");
            exit(1);
        }
        total_done += finished;
    }

    if (iohdl.retcode != KV_SUCCESS) {
        printf("insert one key failed: return code 0x%03X\n", iohdl.retcode);
        exit(1);
    }

    return 0;
}


// delete a given key
uint64_t delete_key(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, kv_key *key, uint32_t count) {

    iohdl_t iohdl;
    memset(&iohdl, 0, sizeof (iohdl));
    iohdl.key = key;
    kv_postprocess_function post_fn_data = { on_IO_complete_func, &iohdl};
    kv_result res = kv_delete(sq_hdl, ns_hdl, key, KV_DELETE_OPT_DEFAULT, &post_fn_data);
    while (res != KV_SUCCESS) {
        printf("kv_store failed with error: 0x%X\n", res);
        res = kv_delete(sq_hdl, ns_hdl, key, KV_DELETE_OPT_DEFAULT, &post_fn_data);
    }

    // poll for completion
    uint32_t total_done = 0;
    while (total_done < 1) {
        uint32_t finished = 0;
        kv_result res = kv_poll_completion(cq_hdl, 0, &finished);
        if (res != KV_SUCCESS && res != KV_WRN_MORE) {
            printf("kv_poll_completion error! exit\n");
            exit(1);
        }
        total_done += finished;
    }

    if (iohdl.retcode != KV_SUCCESS) {
        printf("delete one key failed: return code 0x%03X\n", iohdl.retcode);
        exit(1);
    }

    return 0;
}


uint64_t process_one_round_kv_exist(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, iohdl_t *iohdls, uint32_t count) {

    uint64_t start = current_timestamp();
    // insert key value
    for (uint32_t i = 0; i < count; i++) {
        // insert a key value pair
        // Please note key and value is part of IO handle inside post_fn_data
        kv_key *key = (iohdls + i)->key;
        kv_value *value = (iohdls + i)->value;
        kv_postprocess_function *post_fn_data = &((iohdls + i)->post_fn_data);
        kv_result res = kv_store(sq_hdl, ns_hdl, key, value, KV_STORE_OPT_DEFAULT, post_fn_data);
        while (res != KV_SUCCESS) {
            printf("kv_exit test kv_store failed with error: 0x%03X\n", res);
            res = kv_store(sq_hdl, ns_hdl, key, value, KV_STORE_OPT_DEFAULT, post_fn_data);
        }
    }

    // poll for completion
    uint32_t total_done = 0;
    while (total_done < count) {
        uint32_t finished = 0;
        kv_result res = kv_poll_completion(cq_hdl, 0, &finished);
        if (res != KV_SUCCESS && res != KV_WRN_MORE) {
            printf("kv_poll_completion error! exit\n");
            exit(1);
        }
        total_done += finished;
    }
    uint64_t end = current_timestamp();

    // io completion function got return code in iohdl
    for (uint32_t i = 0; i < count; i++) {
        // insert a key value pair
        // Please note key and value is part of IO handle inside post_fn_data
        if ((iohdls + i)->retcode != KV_SUCCESS) {
            printf("kv_exist write key test failed: item %u: return code 0x%03X\n", i, (iohdls + i)->retcode);
            exit(1);
        }
    }

    // set up first
    uint32_t blen = (count -1) / 8 + 1;

    uint8_t *buffer = (uint8_t *) calloc(blen, 1);
    uint8_t *buffer_expected = (uint8_t *) calloc(blen, 1);

    kv_key *keys = (kv_key *) calloc(count, sizeof(kv_key));
    // set half of keys that shouldn't exist
    int bitpos = 0;
    for (uint32_t i = 0; i < count; i++, bitpos++) {
        kv_key *key_i = keys + i;
        key_i->key = calloc((iohdls + i)->key->length, 1);
        memcpy(key_i->key, (iohdls + i)->key->key, (iohdls + i)->key->length);
        key_i->length = (iohdls + i)->key->length;

        // fprintf(stderr, "key len %d\n", key_i->length);
        if (i % 2 == 0) {
            // char *buffer = (char *) key_i->key;
            // delete these keys, so they are truly gone
            delete_key(ns_hdl, sq_hdl, cq_hdl, key_i, 1);
        } else {
            // insert a key to make sure it's there
            // insert_key(ns_hdl, sq_hdl, cq_hdl, (iohdls + i)->key, (iohdls + i)->value, 1);
            // set up expected bit results
            const int setidx = (bitpos / 8);
            const int bitoffset  =  bitpos - setidx * 8;
            buffer_expected[setidx] |= (1 << bitoffset);

            // printf("XXX byteoffset %d, bitoffset %d\n", setidx, bitoffset);
        }
    }

    kv_postprocess_function *post_fn_data = &((iohdls + 0)->post_fn_data);
    kv_result res = kv_exist(sq_hdl, ns_hdl, keys, count, blen, buffer, post_fn_data);
    while (res != KV_SUCCESS) {
        printf("kv_exist failed with error: 0x%03X\n", res);
        res = kv_exist(sq_hdl, ns_hdl, keys, count, blen, buffer, post_fn_data);
    }

    // poll for completion, just one operation
    total_done = 0;
    while (total_done < 1) {
        uint32_t finished = 0;
        kv_result res = kv_poll_completion(cq_hdl, 0, &finished);
        if (res != KV_SUCCESS && res != KV_WRN_MORE) {
            printf("kv_poll_completion error! exit\n");
            exit(1);
        }
        total_done += finished;
    }

    // compare results
    blen = (iohdls + 0)->buffer_size;
    // printf("XXX blen is %u\n", blen);
    if (memcmp(buffer, buffer_expected, blen)) {
        printf("kv_exist test failed\n");
        exit(1);
    }

    free(buffer);
    free(buffer_expected);
    for (uint32_t i = 0; i < count; i++, bitpos++) {
        free((keys + i)->key);
    }
    free(keys);

    return (end - start);
}


void change_value(kv_value *value) {
    // generate value
    char *buffer = (char *) value->value;
    uint32_t blen = value->length;
    // ending with null
    buffer[blen - 1] = 0;
    for (unsigned int i = 0; i < blen - 1; i++) {
        unsigned int j = '0' + std::rand() % 30;
        buffer[i] = j;
    }
}

// insert enough keys but avoid excessive queue contention
uint64_t process_one_round_write_duplicatekeys_notallowed(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, iohdl_t *iohdls, uint32_t count) {

    uint64_t start = current_timestamp();
    // insert key value
    for (uint32_t i = 0; i < count; i++) {
        // insert a key value pair
        // Please note key and value is part of IO handle inside post_fn_data
        kv_key *key = (iohdls + i)->key;
        kv_value *value = (iohdls + i)->value;
        kv_postprocess_function *post_fn_data = &((iohdls + i)->post_fn_data);
        kv_result res = kv_store(sq_hdl, ns_hdl, key, value, KV_STORE_OPT_IDEMPOTENT, post_fn_data);
        while (res != KV_SUCCESS) {
            printf("kv_store failed with error: 0x%X\n", res);
            res = kv_store(sq_hdl, ns_hdl, key, value, KV_STORE_OPT_IDEMPOTENT, post_fn_data);
        }
    }

    // poll for completion
    uint32_t total_done = 0;
    while (total_done < count) {
        uint32_t finished = 0;
        kv_result res = kv_poll_completion(cq_hdl, 0, &finished);
        if (res != KV_SUCCESS && res != KV_WRN_MORE) {
            printf("kv_poll_completion error! exit\n");
            exit(1);
        }
        total_done += finished;
    }
    uint64_t end = current_timestamp();

    // io completion function got return code in iohdl
    for (uint32_t i = 0; i < count; i++) {
        // insert a key value pair
        // Please note key and value is part of IO handle inside post_fn_data
        if ((iohdls + i)->retcode != KV_ERR_KEY_EXIST) {
            printf("duplicate key not allowed test failed\n");
            exit(1);
        }
    }

    // printf("polling total done: %d, inserted %d\n", total_done, count);
    // double iotime  = double(total) / count;
    // printf("average time per IO: %.1f (ns)\n", iotime);
    return (end - start);
}


// duplicate key to overwrite
uint64_t process_one_round_write_duplicatekeys_allowed(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, iohdl_t *iohdls, uint32_t count) {

    uint64_t start = current_timestamp();
    // insert key value
    for (uint32_t i = 0; i < count; i++) {
        // insert a key value pair
        // Please note key and value is part of IO handle inside post_fn_data
        kv_key *key = (iohdls + i)->key;
        kv_value *value = (iohdls + i)->value;

        // set a new random value for overwrite test for read back test
        change_value(value);
        memcpy((iohdls + i)->value_save->value, value->value, value->length);

        kv_postprocess_function *post_fn_data = &((iohdls + i)->post_fn_data);
        kv_result res = kv_store(sq_hdl, ns_hdl, key, value, KV_STORE_OPT_DEFAULT, post_fn_data);
        while (res != KV_SUCCESS) {
            printf("kv_store failed with error: 0x%X\n", res);
            res = kv_store(sq_hdl, ns_hdl, key, value, KV_STORE_OPT_DEFAULT, post_fn_data);
        }
    }

    // poll for completion
    uint32_t total_done = 0;
    while (total_done < count) {
        uint32_t finished = 0;
        kv_result res = kv_poll_completion(cq_hdl, 0, &finished);
        if (res != KV_SUCCESS && res != KV_WRN_MORE) {
            printf("kv_poll_completion error! exit\n");
            exit(1);
        }
        total_done += finished;
    }
    uint64_t end = current_timestamp();

    // io completion function got return code in iohdl
    for (uint32_t i = 0; i < count; i++) {
        // insert a key value pair
        // Please note key and value is part of IO handle inside post_fn_data
        if ((iohdls + i)->retcode != KV_SUCCESS) {
            printf("duplicate key allowed test failed: item %u: return code 0x%03X\n", i, (iohdls + i)->retcode);
            exit(1);
        }
    }

    // printf("polling total done: %d, inserted %d\n", total_done, count);
    // double iotime  = double(total) / count;
    // printf("average time per IO: %.1f (ns)\n", iotime);
    return (end - start);
}


// duplicate key to overwrite
uint64_t process_one_round_delete(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, iohdl_t *iohdls, uint32_t count) {

    uint64_t start = current_timestamp();
    // insert key value
    for (uint32_t i = 0; i < count; i++) {
        // insert a key value pair
        // Please note key and value is part of IO handle inside post_fn_data
        kv_key *key = (iohdls + i)->key;
        kv_value *value = (iohdls + i)->value;

        // set a new random value for overwrite test for read back test
        change_value(value);
        memcpy((iohdls + i)->value_save->value, value->value, value->length);

        kv_postprocess_function *post_fn_data = &((iohdls + i)->post_fn_data);
        kv_result res = kv_delete(sq_hdl, ns_hdl, key, KV_DELETE_OPT_DEFAULT, post_fn_data);
        while (res != KV_SUCCESS) {
            printf("kv_store failed with error: 0x%X\n", res);
            res = kv_delete(sq_hdl, ns_hdl, key, KV_DELETE_OPT_DEFAULT, post_fn_data);
        }
    }

    // poll for completion
    uint32_t total_done = 0;
    while (total_done < count) {
        uint32_t finished = 0;
        kv_result res = kv_poll_completion(cq_hdl, 0, &finished);
        if (res != KV_SUCCESS && res != KV_WRN_MORE) {
            printf("kv_poll_completion error! exit\n");
            exit(1);
        }
        total_done += finished;
    }
    uint64_t end = current_timestamp();

    // io completion function got return code in iohdl
    for (uint32_t i = 0; i < count; i++) {
        // insert a key value pair
        // Please note key and value is part of IO handle inside post_fn_data
        if ((iohdls + i)->retcode != KV_SUCCESS) {
            printf("delete key test failed: item %u: return code 0x%03X\n", i, (iohdls + i)->retcode);
            exit(1);
        }
    }

    // test read back, all should fail
    for (uint32_t i = 0; i < count; i++) {
        kv_key *key = (iohdls + i)->key;
        kv_value *value = (iohdls + i)->value;
        memset(value->value, 0, value->length);

        kv_postprocess_function *post_fn_data = &((iohdls + i)->post_fn_data);
        kv_result res = kv_retrieve(sq_hdl, ns_hdl, key, KV_RETRIEVE_OPT_DEFAULT, value, post_fn_data);
        while (res != KV_SUCCESS) {
            printf("kv_retrieve failed with error: 0x%X\n", res);
            res = kv_retrieve(sq_hdl, ns_hdl, key, KV_RETRIEVE_OPT_DEFAULT, value, post_fn_data);
        }
    }

    // poll for completion
    total_done = 0;
    while (total_done < count) {
        uint32_t finished = 0;
        kv_result res = kv_poll_completion(cq_hdl, 0, &finished);
        if (res != KV_SUCCESS && res != KV_WRN_MORE) {
            printf("kv_poll_completion error! exit\n");
            exit(1);
        }
        total_done += finished;
    }

    // compare read value with write value
    for (uint32_t i = 0; i < count; i++) {
        if ((iohdls + i)->retcode != KV_ERR_KEY_NOT_EXIST) {
            printf("delete key test failed, key shouldn't be found: item %u: return code 0x%03X\n", i, (iohdls + i)->retcode);
            exit(1);
        }
    }

    return (end - start);
}

// delete a given key
uint64_t delete_group(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, kv_group_condition *grp_cond) {

    iohdl_t iohdl;
    memset(&iohdl, 0, sizeof (iohdl));
    kv_postprocess_function post_fn_data = { on_IO_complete_func, &iohdl};
    kv_result res = kv_delete_group(sq_hdl, ns_hdl, grp_cond, &post_fn_data);

    while (res != KV_SUCCESS) {
        printf("kv_delete_group failed with error: 0x%X\n", res);
        res = kv_delete_group(sq_hdl, ns_hdl, grp_cond, &post_fn_data);
    }

    // poll for completion
    uint32_t total_done = 0;
    while (total_done < 1) {
        uint32_t finished = 0;
        kv_result res = kv_poll_completion(cq_hdl, 0, &finished);
        if (res != KV_SUCCESS && res != KV_WRN_MORE) {
            printf("kv_poll_completion error! exit\n");
            exit(1);
        }
        total_done += finished;
    }

    if (iohdl.retcode != KV_SUCCESS) {
        printf("delete group failed: return code 0x%03X\n", iohdl.retcode);
        exit(1);
    }

    return 0;
}



// duplicate key to overwrite
uint64_t process_one_round_delete_group(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, iohdl_t *iohdls, uint32_t count) {

    uint32_t prefix = 0x87654321;

    kv_group_condition grp_cond;
    grp_cond.bitmask = 0xFFFFFF00;
    grp_cond.bit_pattern = prefix;

    // set new value
    for (uint32_t i = 0; i < count; i++) {
        // populate random value
        iohdl_t *iohdl = iohdls + i;
        populate_key_value_startwith(prefix, iohdl->key, iohdl->value);

        kv_value *value = iohdl->value;
        kv_value *value_save = iohdl->value_save;

        memcpy(value_save->value, value->value, value->length);
    }

    // save the data
    process_one_round_write(ns_hdl, sq_hdl, cq_hdl, iohdls, count);

    delete_group(ns_hdl, sq_hdl, cq_hdl, &grp_cond);

    // test read back, all should fail
    for (uint32_t i = 0; i < count; i++) {
        kv_key *key = (iohdls + i)->key;
        kv_value *value = (iohdls + i)->value;
        memset(value->value, 0, value->length);
        (iohdls + i)->retcode = KV_ERR_COMMAND_INITIALIZED;

        kv_postprocess_function *post_fn_data = &((iohdls + i)->post_fn_data);
        kv_result res = kv_retrieve(sq_hdl, ns_hdl, key, KV_RETRIEVE_OPT_DEFAULT, value, post_fn_data);
        while (res != KV_SUCCESS) {
            printf("kv_retrieve failed with error: 0x%X\n", res);
            res = kv_retrieve(sq_hdl, ns_hdl, key, KV_RETRIEVE_OPT_DEFAULT, value, post_fn_data);
        }
    }

    // poll for completion
    uint32_t total_done = 0;
    while (total_done < count) {
        uint32_t finished = 0;
        kv_result res = kv_poll_completion(cq_hdl, 0, &finished);
        if (res != KV_SUCCESS && res != KV_WRN_MORE) {
            printf("kv_poll_completion error! exit\n");
            exit(1);
        }
        total_done += finished;
    }

    // compare read value with write value
    for (uint32_t i = 0; i < count; i++) {
        if ((iohdls + i)->retcode != KV_ERR_KEY_NOT_EXIST) {
            printf("delete group test failed, key shouldn't be found: item %u: return code 0x%03X\n", i, (iohdls + i)->retcode);

            if ((iohdls + i)->key && (iohdls + i)->key->key) {
                uint32_t *addr = (uint32_t *) (iohdls + i)->key->key;
                uint32_t *addr1 = addr + 1;
                uint32_t *addr2 = addr + 2;
                printf("leading command key bytes: 0x%X %X %X\n", *addr, *addr1, *addr2);
            }

            exit(1);
        }
    }

    return 0;
}

typedef struct iterator_entries_t {
    kv_group_condition grp_cond;
    kv_iterator_option iter_op;
    uint32_t prefix;

    // expected total # for iteration
    uint32_t expected_total;

    // key values should be matched these
    std::set<std::string> keys;
    std::set<std::string> values;
} iterator_entries_t;


// generate a set of data, and insert them, record them for iterator use
void generate_prefixed_dataset(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, iohdl_t *iohdls, uint32_t count, iterator_entries_t& entries, uint32_t prefix, kv_group_condition *grp_cond, kv_iterator_option iter_op, uint32_t expected_total) {
    //////////////////////////////
    // generate a set
    entries.grp_cond = *grp_cond;
    entries.iter_op = iter_op;
    entries.prefix = prefix;
    entries.expected_total = expected_total;

    for (uint32_t i = 0; i < count; i++) {
        // populate random value
        iohdl_t *iohdl = iohdls + i;
        populate_key_value_startwith(prefix, iohdl->key, iohdl->value);

        kv_value *value = iohdl->value;
        kv_value *value_save = iohdl->value_save;

        memcpy(value_save->value, value->value, value->length);

        std::string kstr = std::string((char *) iohdl->key->key, iohdl->key->length);
        if (entries.keys.find(kstr) != entries.keys.end()) {
            printf("INFO: repeated keys\n");
        }
        entries.keys.insert(kstr);

        std::string vstr = std::string((char *) iohdl->value->value, iohdl->value->length);
        if (entries.values.find(vstr) != entries.values.end()) {
            printf("INFO: repeated values\n");
        }
        entries.values.insert(vstr);
        // printf("saved klen %d, vlen %d\n", iohdl->key->length, iohdl->value->length);
    }
    // save the data for iteration
    process_one_round_write(ns_hdl, sq_hdl, cq_hdl, iohdls, count);
}

void prepare_test_iterators_one_bitmask(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, iohdl_t *iohdls, uint32_t count, iterator_entries_t& iterator_entries, kv_iterator_option iter_op) {
    uint32_t expected_total = count;
    uint32_t prefix = 0x87654321;
    kv_group_condition grp_cond;

    //////////////////////////////
    grp_cond.bitmask = 0xFF000000;
    grp_cond.bit_pattern = prefix;
    // delete all existing entries
    delete_group(ns_hdl, sq_hdl, cq_hdl, &grp_cond);


    //////////////////////////////
    // generate a set
    grp_cond.bitmask = 0xFFFFFFFF;
    grp_cond.bit_pattern = prefix;
    generate_prefixed_dataset(ns_hdl, sq_hdl, cq_hdl, iohdls, count, iterator_entries, prefix, &grp_cond, iter_op, expected_total);
}


// insert entries, and record results for validation
// gradually increase matching entries for each dataset
void prepare_test_iterators_multiple_bitmask(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, iohdl_t *iohdls, uint32_t count, iterator_entries_t& iterator_entries, kv_iterator_option iter_op) {
    // these will match all 0xFF...
    uint32_t expected_total = 0;
    uint32_t prefix = 0x87654321;
    kv_group_condition grp_cond;

    //////////////////////////////
    grp_cond.bitmask = 0xFF000000;
    grp_cond.bit_pattern = prefix;
    // delete all existing entries
    delete_group(ns_hdl, sq_hdl, cq_hdl, &grp_cond);


    //////////////////////////////
    // generate a another set
    grp_cond.bitmask = 0xFFFFFFFF;
    grp_cond.bit_pattern = prefix;
    expected_total = count; 
    generate_prefixed_dataset(ns_hdl, sq_hdl, cq_hdl, iohdls, count, iterator_entries, prefix, &grp_cond, iter_op, expected_total);


    //////////////////////////////
    // generate a another set
    grp_cond.bitmask = 0xFFFFFF00;
    grp_cond.bit_pattern = prefix;
    expected_total += count; 
    generate_prefixed_dataset(ns_hdl, sq_hdl, cq_hdl, iohdls, count, iterator_entries, prefix, &grp_cond, iter_op, expected_total);


    //////////////////////////////
    // generate a another set
    grp_cond.bitmask = 0xFFFF0000;
    grp_cond.bit_pattern = prefix;
    expected_total += count; 

    generate_prefixed_dataset(ns_hdl, sq_hdl, cq_hdl, iohdls, count, iterator_entries, prefix, &grp_cond, iter_op, expected_total);

    // printf("XXX total inserted size %lu\n", expected_total);
    // printf("XXX key inserted size %lu\n", iterator_entries.keys.size());
    // printf("XXX value inserted size %lu\n", iterator_entries.values.size());
    // set final matched total for validation
    iterator_entries.expected_total = expected_total;
    iterator_entries.grp_cond.bitmask = 0xFF000000;
    iterator_entries.grp_cond.bit_pattern = 0x87654321;
}

//////////////////////////////////////////
// process iterator returned buffer
// use global g_KEYLEN to decide the fixed length of keys
void processing_iterator_returned_keys_fixed(kv_iterator_list *iter_list, std::vector<std::string>& keys) {

    uint8_t *buffer = (uint8_t *) iter_list->it_list;
    // uint32_t blen = iter_list->size;
    uint32_t num_entries = iter_list->num_entries;

    uint32_t klen = g_KEYLEN;
    // uint32_t vlen = sizeof(kv_value_t);

    while (num_entries > 0) {
        keys.push_back(std::string((char *) buffer, klen));
        buffer += klen;
        num_entries--;
    }
}

void processing_iterator_returned_keys_variable(kv_iterator_list *iter_list, std::vector<std::string>& keys) {
    uint8_t *buffer = (uint8_t *) iter_list->it_list;
    // uint32_t blen = iter_list->size;
    uint32_t num_entries = iter_list->num_entries;

    uint32_t klen = sizeof(uint32_t);
    uint32_t klen_value = 0;
    // uint32_t vlen = sizeof(kv_value_t);
    // uint32_t vlen_value = 0;

    while (num_entries > 0) {
        // first get klen
        uint8_t* addr = (uint8_t *) &klen_value;
        for (unsigned int i = 0; i < klen; i++) {
            *(addr + i) = *(buffer + i);
        }
        buffer += klen;

        keys.push_back(std::string((char *) buffer, klen_value));
        buffer += klen_value;

        num_entries--;
    }
}

void processing_iterator_returned_keyvals_fixed(kv_iterator_list *iter_list, std::vector<std::string>& keys, std::vector<std::string>& values) {

    uint8_t *buffer = (uint8_t *) iter_list->it_list;
    // uint32_t blen = iter_list->size;
    uint32_t num_entries = iter_list->num_entries;

    uint32_t klen = g_KEYLEN;
    // uint32_t klen_value = 0;
    uint32_t vlen = sizeof(kv_value_t);
    uint32_t vlen_value = 0;

    while (num_entries > 0) {
        // get fixed key
        keys.push_back(std::string((char *)buffer, klen));
        buffer += klen;

        // get vlen
        uint8_t *addr = (uint8_t *)&vlen_value;
        for (unsigned int i = 0; i < vlen; i++) {
            *(addr + i) = *(buffer + i);
        }
        buffer += vlen;

        values.push_back(std::string((char *)buffer, vlen_value));
        buffer += vlen_value;

        num_entries--;
    }
}

void processing_iterator_returned_keyvals_variable(kv_iterator_list *iter_list, std::vector<std::string>& keys, std::vector<std::string>& values) {

    uint8_t *buffer = (uint8_t *) iter_list->it_list;
    // uint32_t blen = iter_list->size;
    uint32_t num_entries = iter_list->num_entries;

    uint32_t klen = sizeof(uint32_t);
    uint32_t klen_value = 0;
    uint32_t vlen = sizeof(kv_value_t);
    uint32_t vlen_value = 0;

    while (num_entries > 0) {
        // first get klen
        uint8_t *addr = (uint8_t *) &klen_value;
        for (unsigned int i = 0; i < klen; i++) {
            *(addr + i) = *(buffer + i);
        }
        buffer += klen;

        // printf("XXX got klen %u\n", klen_value);

        keys.push_back(std::string((char *) buffer, klen_value));
        buffer += klen_value;

        // get vlen
        addr = (uint8_t *) &vlen_value;
        for (unsigned int i = 0; i < vlen; i++) {
            *(addr + i) = *(buffer + i);
        }
        buffer += vlen;

        // printf("XXX got vlen %u\n", vlen_value);

        values.push_back(std::string((char *) buffer, vlen_value));
        buffer += vlen_value;

        num_entries--;
    }
}
 

// XXX use a global to check if key is fixed size or not
// default is fixed size
void processing_iterator_returned_keyvals(kv_iterator_list *iter_list, kv_iterator_option iter_op, std::vector<std::string>& keys, std::vector<std::string>& values) {
    // now check returned key/values
    if (iter_op == KV_ITERATOR_OPT_KEY) {
        if (g_KEYLEN_FIXED) {
            processing_iterator_returned_keys_fixed(iter_list, keys);
        } else {
            processing_iterator_returned_keys_variable(iter_list, keys);
        }
    }

    if (iter_op == KV_ITERATOR_OPT_KV) {
        if (g_KEYLEN_FIXED) {
            processing_iterator_returned_keyvals_fixed(iter_list, keys, values);
        } else {
            processing_iterator_returned_keyvals_variable(iter_list, keys, values);
        }
    }
}


void get_iterator_results(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, 
    iohdl_t *iohdls, uint32_t count,
    kv_group_condition *grp_cond, kv_iterator_option iter_op, 
    std::vector<std::string>& keys, std::vector<std::string>& values) {

    //////////////////////////////////
    // open iterator
    // iohdl.hiter (a handle)
    iohdl_t iohdl;
    // kv_iterator_option iter_op = KV_ITERATOR_OPT_KEY;
    // kv_iterator_option iter_op = KV_ITERATOR_OPT_KV;
    
    kv_postprocess_function post_fn_data = { on_IO_complete_func, &iohdl};
    kv_result res = kv_open_iterator(sq_hdl, ns_hdl, iter_op, grp_cond, &post_fn_data);

    while (res != KV_SUCCESS) {
        printf("kv_open_iterator failed with error: 0x%X\n", res);
        res = kv_open_iterator(sq_hdl, ns_hdl, iter_op, grp_cond, &post_fn_data);
    }

    // poll for completion
    uint32_t total_done = 0;
    while (total_done < 1) {
        uint32_t finished = 0;
        kv_result res = kv_poll_completion(cq_hdl, 0, &finished);
        if (res != KV_SUCCESS && res != KV_WRN_MORE) {
            printf("kv_poll_completion error! exit\n");
            exit(1);
        }
        total_done += finished;
    }

    if (iohdl.retcode != KV_SUCCESS) {
        printf("kv_open_iterator failed failed: return code 0x%03X\n", iohdl.retcode);
        exit(1);
    }

    //////////////////////////////////
    // iterator next
    kv_iterator_handle hiter = iohdl.hiter;
    kv_iterator_list iter_list;

    uint32_t buffer_size = 6 * 4096 + 1234;
    uint8_t buffer[buffer_size];
    iter_list.size = 6 * 4096 + 1234;
    iter_list.it_list = (uint8_t *) buffer;
    iter_list.num_entries = 0;
    iter_list.end = FALSE;
    uint32_t total = 0;

    while (1) {
        res = kv_iterator_next(sq_hdl, ns_hdl, hiter, &iter_list, &post_fn_data);

        // poll for completion
        uint32_t total_done = 0;
        while (total_done < 1) {
            uint32_t finished = 0;
            kv_result res = kv_poll_completion(cq_hdl, 0, &finished);
            if (res != KV_SUCCESS && res != KV_WRN_MORE) {
                printf("kv_poll_completion error! exit\n");
                exit(1);
            }
            total_done += finished;
        }

        if (iohdl.retcode != KV_SUCCESS && iohdl.retcode != KV_WRN_MORE) {
            printf("kv_iterator_next failed: return code 0x%03X\n", iohdl.retcode);
            exit(1);
        }

        // now check returned key/values
        processing_iterator_returned_keyvals(&iter_list, iter_op, keys, values);
        // printf("client got entries:%d\n", iter_list.num_entries);
        total += iter_list.num_entries;

        if (iter_list.end) {
            break;
        }
    }
    // printf("client got total entries:%d\n", total);


    // close iterator
    //////////////////////////////////
    // iohdl_t iohdl;
    // iohdl.hiter (a handle)
    // kv_postprocess_function post_fn_data = { on_IO_complete_func, &iohdl};
    res = kv_close_iterator(sq_hdl, ns_hdl, iohdl.hiter, &post_fn_data);

    while (res != KV_SUCCESS) {
        printf("kv_close_iterator failed with error: 0x%X\n", res);
        res = kv_close_iterator(sq_hdl, ns_hdl, iohdl.hiter, &post_fn_data);
    }

    // poll for completion
    total_done = 0;
    while (total_done < 1) {
        uint32_t finished = 0;
        kv_result res = kv_poll_completion(cq_hdl, 0, &finished);
        if (res != KV_SUCCESS && res != KV_WRN_MORE) {
            printf("kv_poll_completion error! exit\n");
            exit(1);
        }
        total_done += finished;
    }

    if (iohdl.retcode != KV_SUCCESS) {
        printf("kv_close_iterator failed: return code 0x%03X\n", iohdl.retcode);
        exit(1);
    }
}

////////////////////////////////////////////////
// the test assumes all keys and values are unique
// when data are generated and inserted 
void validate_iterator_results(iterator_entries_t& iterator_entries, std::vector<std::string>& keys, std::vector<std::string>& values) {

    uint32_t inserted_n = iterator_entries.keys.size();

    uint32_t keyn = keys.size();
    uint32_t valn = values.size();

    if (keyn != inserted_n) {
        printf("iterator returned different key counts from what's inserted\n");
        printf("inserted %d keys, iterator returned %d keys\n", inserted_n, keyn);
        exit(1);
    }

    // check keys
    for (uint32_t i = 0; i < keyn; i++) {
        std::string kstr = keys[i];

        auto it = iterator_entries.keys.find(kstr);
        auto ite = iterator_entries.keys.end();
        if (it == ite) {
            printf("original key size %lu: klen: %lu\n", iterator_entries.keys.size(), it->size());
            // printf("iterator returned key: %s\n", kstr.c_str());
            printf("iterator returned keys not found in original inserted set\n");
            exit(1);
        }
    }

    // check values 
    if (valn == 0) {
        return;
    }

    for (uint32_t i = 0; i < valn; i++) {
        std::string vstr = values[i];
        auto it = iterator_entries.values.find(vstr);
        if (it == iterator_entries.values.end()) {
            printf("iterator returned values not found in original inserted set\n");
            exit(1);
        }
    }
}

// test iterators
uint64_t test_iterators_one_bitmask(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, iohdl_t *iohdls, uint32_t count, kv_iterator_option iter_op) {

    std::vector<std::string> keys;
    std::vector<std::string> values;

    iterator_entries_t iterator_entries;
    prepare_test_iterators_one_bitmask(ns_hdl, sq_hdl, cq_hdl, iohdls, count, iterator_entries, iter_op);

    get_iterator_results(ns_hdl, sq_hdl, cq_hdl, iohdls, count, &iterator_entries.grp_cond, iter_op, keys, values);

    validate_iterator_results(iterator_entries, keys, values);

    return 0;
}

// test iterators
uint64_t test_iterators_multiple_bitmask(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, iohdl_t *iohdls, uint32_t count, kv_iterator_option iter_op) {

    std::vector<std::string> keys;
    std::vector<std::string> values;

    iterator_entries_t iterator_entries;
    prepare_test_iterators_multiple_bitmask(ns_hdl, sq_hdl, cq_hdl, iohdls, count, iterator_entries, iter_op);

    get_iterator_results(ns_hdl, sq_hdl, cq_hdl, iohdls, count, &iterator_entries.grp_cond, iter_op, keys, values);

    validate_iterator_results(iterator_entries, keys, values);

    return 0;
}


void test_close_iterator(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, std::vector<kv_iterator_handle>& iters) {

    for (kv_iterator_handle hiter : iters) {
        // close iterator
        iohdl_t iohdl;
        iohdl.hiter = hiter;
        kv_postprocess_function post_fn_data = { on_IO_complete_func, &iohdl};

        kv_result res = kv_close_iterator(sq_hdl, ns_hdl, iohdl.hiter, &post_fn_data);
        while (res != KV_SUCCESS) {
            printf("kv_close_iterator failed with error: 0x%X\n", res);
            res = kv_close_iterator(sq_hdl, ns_hdl, iohdl.hiter, &post_fn_data);
        }

        // poll for completion
        int total_done = 0;
        while (total_done < 1) {
            uint32_t finished = 0;
            kv_result res = kv_poll_completion(cq_hdl, 0, &finished);
            if (res != KV_SUCCESS && res != KV_WRN_MORE) {
                printf("kv_poll_completion error! exit\n");
                exit(1);
            }
            total_done += finished;
        }

        if (iohdl.retcode != KV_SUCCESS) {
            printf("kv_close_iterator failed: return code 0x%03X\n", iohdl.retcode);
            exit(1);
        }
    }
}


kv_result test_open_iterator(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, std::vector<kv_iterator_handle>& iters) {

    //////////////////////////////////
    // open iterator
    // iohdl.hiter (a handle)
    iohdl_t iohdl;
    // kv_iterator_option iter_op = KV_ITERATOR_OPT_KV;
    kv_iterator_option iter_op = KV_ITERATOR_OPT_KEY;

    static int i = 0;
    i++;

    // only 1 iterator with the same prefix can be opened
    uint32_t prefix = 0x87654321 + i;
    kv_group_condition grp_cond;
    //////////////////////////////
    grp_cond.bitmask = 0xFF000000;
    grp_cond.bit_pattern = prefix;


    //////////////////////////////////
    // open iterator
    // iohdl.hiter (a handle)
    kv_postprocess_function post_fn_data = { on_IO_complete_func, &iohdl};
    kv_result res = kv_open_iterator(sq_hdl, ns_hdl, iter_op, &grp_cond, &post_fn_data);

    while (res != KV_SUCCESS) {
        printf("kv_open_iterator failed with error: 0x%X\n", res);
        res = kv_open_iterator(sq_hdl, ns_hdl, iter_op, &grp_cond, &post_fn_data);
    }

    // poll for completion
    uint32_t total_done = 0;
    while (total_done < 1) {
        uint32_t finished = 0;
        kv_result res = kv_poll_completion(cq_hdl, 0, &finished);
        if (res != KV_SUCCESS && res != KV_WRN_MORE) {
            printf("kv_poll_completion error! exit\n");
            exit(1);
        }
        total_done += finished;
    }

    /*
    if (iohdl.retcode != KV_SUCCESS) {
        printf("kv_open_iterator failed failed: return code 0x%03X\n", iohdl.retcode);
        exit(1);
    }*/

    if (iohdl.retcode == KV_SUCCESS) {
        iters.push_back(iohdl.hiter);
    }
    return iohdl.retcode;
}

// test list iterators, return the count of open iterators
int test_list_iterators(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl) {

    kv_iterator kv_iters[SAMSUNG_MAX_ITERATORS];
    memset(kv_iters, 0, sizeof(kv_iters));
    uint32_t count = SAMSUNG_MAX_ITERATORS;

    iohdl_t iohdl;
    kv_postprocess_function post_fn_data = { on_IO_complete_func, &iohdl};

    // this sync admin call
    kv_result res = kv_list_iterators(sq_hdl, ns_hdl, kv_iters, &count, &post_fn_data);

    if (count != SAMSUNG_MAX_ITERATORS) {
        printf("kv_list_iterators doesn't return all iterators (open or closed), returned only %u\n", count);
        exit(1);
    }

    if (res) {
        printf("kv_list_iterators with error: 0x%X\n", res);
	exit(1);
    }

    int valid_count = 0;
    // simply print for any return iterators
    for (uint32_t i = 0; i < count; i++) {
        if (kv_iters[i].handle_id > 0 && kv_iters[i].status) {
            // fprintf(stderr, "found iterator id %d\n", kv_iters[i].handle_id);
            valid_count++;
        }
    }

    // printf("valid count %d\n", valid_count);
    return valid_count;
}


// test enforcement of max iterators allowed
void test_max_allowed_iterators(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl) {
    std::vector<kv_iterator_handle> iters;

    // test count open iterators
    int count = test_list_iterators(ns_hdl, sq_hdl, cq_hdl);
    if (count != 0) {
    	printf("kv_list_iterators doesn't return 0 count of open iterators at fresh start\n");
	exit(1);
    }

    for (int i = 0; i < SAMSUNG_MAX_ITERATORS; i++) {
        kv_result ret = test_open_iterator(ns_hdl, sq_hdl, cq_hdl, iters);

        if (ret != KV_SUCCESS) {
            printf("open iterators within limit failed, return code 0x%03X\n", ret);
            exit(1);
        }
    }

    // test count open iterators
    count = test_list_iterators(ns_hdl, sq_hdl, cq_hdl);
    if (count != SAMSUNG_MAX_ITERATORS) {
    	printf("kv_list_iterators doesn't return correct count of open iterators\n");
	exit(1);
    }

    for (int i = 0; i < SAMSUNG_MAX_ITERATORS; i++) {
        kv_result ret = test_open_iterator(ns_hdl, sq_hdl, cq_hdl, iters);

        if (ret != KV_ERR_TOO_MANY_ITERATORS_OPEN) {
            printf("open iterators exceeding limit test failed, return code 0x%03X\n", ret);
            exit(1);
        }
    }

    // close opened iterators
    test_close_iterator(ns_hdl, sq_hdl, cq_hdl, iters);

    count = test_list_iterators(ns_hdl, sq_hdl, cq_hdl);
    if (count != 0) {
    	printf("kv_list_iterators doesn't return correct 0 count of after closing all iterators, instead returned %u\n", count);
	exit(1);
    }
}


// test iterators main routine
uint64_t test_iterators(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, iohdl_t *iohdls, uint32_t count, kv_iterator_option iter_op) {
    test_iterators_one_bitmask(ns_hdl, sq_hdl, cq_hdl, iohdls, count, iter_op);
    test_iterators_multiple_bitmask(ns_hdl, sq_hdl, cq_hdl, iohdls, count, iter_op);
    return 0;
}

// test cycle of read and write in rounds
void API_test_main(kv_device_handle dev_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, int klen, int vlen, int tcount, int qdepth) {

    // set up namespace
    kv_namespace_handle ns_hdl = NULL;
    get_namespace_default(dev_hdl, &ns_hdl);

    // uint32_t prefix = 0xFFFF1234;
    // XXX
    uint32_t prefix = 0x12345678;
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

        // test no overwrite
        process_one_round_write_duplicatekeys_notallowed(ns_hdl, sq_hdl, cq_hdl, iohdls, count);

        // test overwrite
        process_one_round_write_duplicatekeys_allowed(ns_hdl, sq_hdl, cq_hdl, iohdls, count);
        process_one_round_read(ns_hdl, sq_hdl, cq_hdl, iohdls, count);

        // test partial read
        process_one_round_read_partial(ns_hdl, sq_hdl, cq_hdl, iohdls, count);

        // test partial invalid read
        process_one_round_read_offset_invalid(ns_hdl, sq_hdl, cq_hdl, iohdls, count);

        // test kv exist
        process_one_round_kv_exist(ns_hdl, sq_hdl, cq_hdl, iohdls, count);

        // test delete then read
        process_one_round_delete(ns_hdl, sq_hdl, cq_hdl, iohdls, count);

        // test delete group
        process_one_round_delete_group(ns_hdl, sq_hdl, cq_hdl, iohdls, count);

        // test iterators
        test_iterators(ns_hdl, sq_hdl, cq_hdl, iohdls, count, KV_ITERATOR_OPT_KEY);
        test_iterators(ns_hdl, sq_hdl, cq_hdl, iohdls, count, KV_ITERATOR_OPT_KV);

        // validate max # of open iterators is enforced
        test_max_allowed_iterators(ns_hdl, sq_hdl, cq_hdl);
    }

    // done, free memories
    free_iohdls(iohdls, qdepth);

    printf("all tests succeeded\n");

    /* only use kvperf to measure performance
     *
    // IOPS = IO count * 1000 * 1000 / time_ns
    double iops_w = tcount * 1000.0 * 1000 / total_w;
    double mean_iotime_w = total_w/1000.0/tcount;
    printf("IOPS_w: %.1f K/s\n", iops_w);
    printf("mean IO duration(write): %.1f us\n", mean_iotime_w);

    // bytes * 10**9 / ( time * 1024 * 1024)
    double throughput_w = tcount * (klen + vlen) / 1024.0 * 1000 * 1000 / total_w * 1000 / 1024;
    printf("device throughput (write): %.1f MB/s\n", throughput_w);

    double iops_r = tcount * 1000.0 * 1000 / total_r;
    double mean_iotime_r = total_r/1000.0/tcount;
    printf("IOPS_r: %.1f K/s\n", iops_r);
    printf("mean IO duration(read): %.1f us\n", mean_iotime_r);

    // bytes * 10**9 / ( time * 1024 * 1024)
    double throughput_r = tcount * (klen + vlen) / 1024.0 * 1000 * 1000 / total_r * 1000 / 1024;
    printf("device throughput (read): %.1f MB/s\n", throughput_r);

    printf("IOs %u, IO time %llu (ns), app time %llu (ns)\n", tcount, (long long unsigned) total_w, (long long unsigned) (end - start));

    */
}

void create_qpair(kv_device_handle dev_hdl,
    std::map<kv_queue_handle, kv_queue_handle>& qpairs,
    kv_queue_handle *sq_hdl,
    kv_queue_handle *cq_hdl,
    uint16_t sqid,
    uint16_t cqid,
    uint16_t q_depth,
    kv_interrupt_handler *int_handler) {
    
    // create a CQ
    // ONLY DO THIS, if you have NOT created the CQ earlier
    create_cq(dev_hdl, cqid, q_depth, cq_hdl);

    // this is only for completion Q
    // this will kick off completion Q processing
    // kv_set_interrupt_handler(cq_hdl, int_handler); 

    // create a SQ
    create_sq(dev_hdl, sqid, q_depth, cqid, sq_hdl);

    qpairs.insert(std::make_pair(*sq_hdl, *cq_hdl));

}


int main(int argc, char**argv) {
    
    if (argc <  2) {
        printf("This program will excerise all critical APIs.\n"
               "If it runs into any errors during the test,\n"
               "it will stop without reporting success.\n");

        printf("    Please run\n  %s <number of keys>\n", argv[0]);
        printf("    Default: %d\n", TOTAL_KV_COUNT);
    } else {
        TOTAL_KV_COUNT = std::stoi(argv[1]);
        printf("    Keys to insert: %d\n", TOTAL_KV_COUNT);
    }

    int klen = 16;
    int vlen = 4096;
    int qdepth = 64;

    // initialize globals
    g_KEYLEN_FIXED = 0;
    g_KEYLEN = klen;

    kv_device_init_t dev_init;

    // you can supply your own configuration file
    dev_init.configfile = "kvssd_emul.conf";

    // the emulator device path is fixed.
    // it's virtual, you don't need to create it
    dev_init.devpath = "/dev/kvemul";
    dev_init.need_persistency = FALSE;
    dev_init.is_polling = TRUE;

    kv_device_handle dev_hdl = NULL;

    // initialize the device
    kv_result ret = kv_initialize_device(&dev_init, &dev_hdl);
    if (ret != KV_SUCCESS) {
        printf("device creation error\n");
        exit(1);
    }

    // print device stats
    kv_device devinfo;
    kv_device_stat devst;
    kv_get_device_info(dev_hdl, &devinfo);
    kv_get_device_stat(dev_hdl, &devst);
    printf("capacity is %luB\n", devinfo.capacity);
    printf("device stats utilization %u\n", devst.utilization);
    // printf("device stats ns count %u\n", devst.namespace_count);
    // printf("device stats queue count %u\n", devst.queue_count);
    // printf("device waf %u\n", devst.waf);
 
    // set up interrupt handler
    _kv_interrupt_handler int_func = {interrupt_func, (void *)4, 4};
    kv_interrupt_handler int_handler = &int_func;

    // to keep track all opened queue pairs
    std::map<kv_queue_handle, kv_queue_handle> qpairs;
    kv_queue_handle sq_hdl;
    kv_queue_handle cq_hdl;

    // convenient wrapper function
    // create a SQ/CQ pair
    create_qpair(dev_hdl, qpairs,  &sq_hdl, &cq_hdl,
            1,  // sqid
            2,  // cqid
            qdepth, // q depth
            &int_handler);

    // start a thread to insert key values through submission Q
    printf("starting thread for testing\n");
    std::thread th = std::thread(API_test_main, dev_hdl, sq_hdl, cq_hdl, klen, vlen, TOTAL_KV_COUNT, qdepth);
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

    // print device stats
    kv_get_device_info(dev_hdl, &devinfo);
    kv_get_device_stat(dev_hdl, &devst);
    printf("capacity is %luB\n", devinfo.capacity);
    printf("device stats utilization %u\n", devst.utilization);
    // printf("device stats ns count %u\n", devst.namespace_count);
    // printf("device stats queue count %u\n", devst.queue_count);
    // printf("device waf %u\n", devst.waf);
 
    // shutdown
    kv_cleanup_device(dev_hdl);
    
    return 0;
}
