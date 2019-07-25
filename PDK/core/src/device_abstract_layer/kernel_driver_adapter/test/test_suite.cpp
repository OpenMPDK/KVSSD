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
#include "kadi.h"

// default settings
int TOTAL_KV_COUNT = 256;
int g_KEYLEN_FIXED = 0;
int g_KEYLEN = 16;
unsigned int PREFIX_KV = 0;
unsigned int BITMASK_KV = 0;
int g_complete_count = 0;

#define KVS_ERR_KEY_NOT_EXIST 0x310

// device run time structure
struct device_handle_t {
    kv_device_init_t dev_init;
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
    kv_iterator_list* iter_list;

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

int reformat_iterbuffer(kv_iterator_list *iter_list)
{
  static const int KEY_LEN_BYTES = 4;
  int ret = 0;
  unsigned int key_size = 0;
  int keydata_len_with_padding = 0;
  char *data_buff = (char *)iter_list->it_list;
  unsigned int buffer_size = iter_list->size;
  unsigned int key_count = iter_list->num_entries;
  // according to firmware command output format, convert them to KVAPI expected format without any padding
  // all data alreay in user provided buffer, but need to remove padding bytes to conform KVAPI format
  char *current_ptr = data_buff;
  unsigned int buffdata_len = buffer_size;

  if (current_ptr == 0) return KV_ERR_PARAM_INVALID;

  if (buffdata_len < KEY_LEN_BYTES) { return KV_ERR_SYS_IO;  }

  buffdata_len -= KEY_LEN_BYTES;
  data_buff += KEY_LEN_BYTES;
  for (uint32_t i = 0; i < key_count && buffdata_len > 0; i++)
  {
    if (buffdata_len < KEY_LEN_BYTES)
    {
      ret = KV_ERR_SYS_IO;
      break;
    }

    // move 4 byte key len
    memmove(current_ptr, data_buff, KEY_LEN_BYTES);
    current_ptr += KEY_LEN_BYTES;

    // get key size
    key_size = *((uint32_t *)data_buff);
    buffdata_len -= KEY_LEN_BYTES;
    data_buff += KEY_LEN_BYTES;

    if ((key_size > buffdata_len) || (key_size >= KVCMD_MAX_KEY_SIZE))
    {
      ret = KV_ERR_SYS_IO;
      break;
    }

    // move key data
    memmove(current_ptr, data_buff, key_size);
    current_ptr += key_size;

    // calculate 4 byte aligned current key len including padding bytes
    keydata_len_with_padding = (((key_size + 3) >> 2) << 2);

    // skip to start position of next key
    buffdata_len -= keydata_len_with_padding;
    data_buff += keydata_len_with_padding;
  }
  return ret;
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
    g_complete_count++;
    iohdl_t *iohdl = (iohdl_t *) ctx->private_data;
    iohdl->retcode = ctx->retcode;

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
        //iohdl->buffer_size = ctx->result.buffer_size;
        //iohdl->buffer_count = ctx->result.buffer_count;
    }
    if (ctx->opcode == nvme_cmd_kv_iter_read) {
        iohdl->iter_list->end = (bool_t)ctx->hiter.end;
        iohdl->iter_list->size =  ctx->hiter.buflength;
        if(ctx->hiter.buf){
            iohdl->iter_list->num_entries = *((uint32_t *)ctx->hiter.buf);
            reformat_iterbuffer(iohdl->iter_list);
        }
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

// delete a given key
uint64_t delete_key(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, kv_key *key, uint32_t count) {

    iohdl_t iohdl;
    memset(&iohdl, 0, sizeof (iohdl));
    iohdl.key = key;
    g_complete_count = 0;
    kv_postprocess_function post_fn_data = { on_IO_complete_func, &iohdl};
    kv_result res = kv_delete(sq_hdl, ns_hdl, key, KV_DELETE_OPT_DEFAULT, &post_fn_data);
    while (res != KV_SUCCESS) {
        printf("kv_store failed with error: 0x%X\n", res);
        res = kv_delete(sq_hdl, ns_hdl, key, KV_DELETE_OPT_DEFAULT, &post_fn_data);
    }

    // poll for completion
    uint32_t total_done = 0;
    while (g_complete_count < 1) {
        uint32_t finished = 0;
        kv_result res = kv_poll_completion(cq_hdl, 0, &finished);
        if (res != KV_SUCCESS && res != KV_WRN_MORE) {
            printf("kv_poll_completion error! exit\n");
            exit(1);
        }
    }

    if (iohdl.retcode != KV_SUCCESS) {
        printf("delete one key failed: return code 0x%03X\n", iohdl.retcode);
        exit(1);
    }

    return 0;
}

// check exist of a given key
bool check_key_exist(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, kv_key *key) {

    uint8_t status = 0;
    g_complete_count = 0;
    iohdl_t iohdl;
    memset(&iohdl, 0, sizeof (iohdl));
    iohdl.key = key;
    kv_postprocess_function post_fn_data = { on_IO_complete_func, &iohdl};
    kv_result res = kv_exist(sq_hdl, ns_hdl, key, 1, 1, &status, &post_fn_data);
    while (res != KV_SUCCESS) {
        printf("kv_exist failed with error: 0x%X\n", res);
        res = kv_exist(sq_hdl, ns_hdl, key, 1, 1, &status, &post_fn_data);
    }

    // poll for completion
    uint32_t total_done = 0;
    while (g_complete_count < 1) {
        uint32_t finished = 0;
        kv_result res = kv_poll_completion(cq_hdl, 0, &finished);
        if (res != KV_SUCCESS && res != KV_WRN_MORE) {
            printf("kv_poll_completion error! exit\n");
            exit(1);
        }
    }

    if (iohdl.retcode == KV_SUCCESS && status == 0) {
        // printf("key found\n");
        return 1;
    } else if (iohdl.retcode == KVS_ERR_KEY_NOT_EXIST && status == 0) {
        // printf("key not found\n");
        return 0;
    } else {
        printf("kv_exist returned error! exit\n");
        exit(1);
    }
}


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

// read back and compare
uint64_t process_one_round_read(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, iohdl_t *iohdls, uint32_t count) {

    g_complete_count = 0;
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

        kv_postprocess_function *post_fn_data = &((iohdls + i)->post_fn_data);
        kv_result res = kv_retrieve(sq_hdl, ns_hdl, key, KV_RETRIEVE_OPT_DEFAULT, value, post_fn_data);
        while (res != KV_SUCCESS) {
            printf("kv_retrieve failed with error: 0x%X\n", res);
            res = kv_retrieve(sq_hdl, ns_hdl, key, KV_RETRIEVE_OPT_DEFAULT, value, post_fn_data);
        }
    }

    // poll for completion
    while (g_complete_count < count) {
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
            printf("failed: value read not the same as value written, original data len %d, device returned data len %d\n", (iohdls + i)->value_save->length, (iohdls + i)->value->length);
            // printf("Note: please only use unique keys for this validation\n");
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
    g_complete_count = 0;
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
            exit(1);
        }
    }

    // poll for completion
    while (g_complete_count < count) {
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
        // Validating
        //printf("[%d] key:%d\n",num_entries,keys[num_entries-1]);
        buffer += klen;
        num_entries--;
    }
}

/////
// print key
/////
void print_key(std::string& key) {
    char *data = (char *)key.data();

    int len = key.size();
    int i = 0;

    printf("key is: ");
    while (i <= (len - 4)) {
        uint32_t prefix = 0;
        memcpy(&prefix, data + i, 4);

        printf(" %08X", prefix);
        i += 4;
    }
    printf("\n");
}

void print_key(kv_key *key) {
    char *data = (char *)key->key;

    int len = key->length;
    int i = 0;

    printf("key is: ");
    while (i <= (len - 4)) {
        uint32_t prefix = 0;
        memcpy(&prefix, data + i, 4);

        printf(" %08X", prefix);
        i += 4;
    }
    printf("\n");
}



////////////////////////////////////////////////
// the test assumes all keys and values are unique
// when data are generated and inserted 
void validate_iterator_results(iterator_entries_t& iterator_entries, std::vector<std::string>& keys, std::vector<std::string>& values) {

    std::set<std::string> returned_keys;

    //uint32_t inserted_n = iterator_entries.keys.size();
    uint32_t inserted_n = iterator_entries.expected_total;
    uint32_t keyn = keys.size();
    uint32_t valn = values.size();
    //printf("inserted_n:%d, keyn:%d\n",inserted_n,keyn);
    if (keyn != inserted_n) {
        printf("iterator returned different key counts from what's inserted\n");
        printf("inserted %d keys, iterator returned %d keys\n", inserted_n, keyn);
        exit(1);
    }

    // check iterator returned keys
    for (uint32_t i = 0; i < keyn; i++) {
        std::string kstr = keys[i];

        std::set<std::string>::iterator it1 = returned_keys.find(kstr);
        if (it1 == returned_keys.end()) {
            returned_keys.insert(keys[i]);
        } else {
            // repeated keys from iterator result
            printf("Info: iterator returned repeated key: ");
            print_key(kstr);
        }

        auto it = iterator_entries.keys.find(kstr);
        auto ite = iterator_entries.keys.end();
        if (it == ite) {
            printf("Error: iterator returned extra: ");
            print_key(kstr);
            // XXX
            // printf("iterator returned key: %s\n", kstr.c_str());
            // printf("iterator returned key not found in original inserted set\n");
            // exit(1);
        }
    }

    // check original inserted keys
    // printf("original inserted keys\n");
    for (std::string kstr : iterator_entries.keys) {
        auto it = returned_keys.find(kstr);
        auto ite = returned_keys.end();
        if (it == ite) {
            printf("Error: iterator missed key: ");
            print_key(kstr);
            // XXX
            // printf("iterator returned key: %s\n", kstr.c_str());
            // printf("iterator returned key not found in original inserted set\n");
            // exit(1);
        }
    }

    // check values 
    if (valn == 0) {
        return;
    }

    /*for (uint32_t i = 0; i < valn; i++) {
        std::string vstr = values[i];
        auto it = iterator_entries.values.find(vstr);
        if (it == iterator_entries.values.end()) {
            printf("iterator returned values not found in original inserted set\n");
            exit(1);
        }
    }*/
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

// XXX use a global to check if key is fixed size or not
// default is fixed size
// Currently it supports fixed size KEYS only.
void processing_iterator_returned_keyvals(kv_iterator_list *iter_list, kv_iterator_option iter_op, std::vector<std::string>& keys, std::vector<std::string>& values) {
    // now check returned key/values
    if (iter_op == KV_ITERATOR_OPT_KEY) {
        if (g_KEYLEN_FIXED) {
            processing_iterator_returned_keys_fixed(iter_list, keys);
        } else {
            processing_iterator_returned_keys_variable(iter_list, keys);
        }
    }

    /*if (iter_op == KV_ITERATOR_OPT_KV) {
        if (g_KEYLEN_FIXED) {
            processing_iterator_returned_keyvals_fixed(iter_list, keys, values);
        } else {
            processing_iterator_returned_keyvals_variable(iter_list, keys, values);
        }
    }*/
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
    kv_result res = kv_open_iterator_sync(sq_hdl, ns_hdl, iter_op, grp_cond, &iohdl.hiter);
    while (res != KV_SUCCESS) {
        printf("kv_open_iterator failed with error: 0x%X\n", res);
        exit(1);
    }

    // poll for completion
    /*uint32_t total_done = 0;
    while (total_done < 1) {
        uint32_t finished = 0;
        kv_result res = kv_poll_completion(cq_hdl, 0, &finished);
        if (res != KV_SUCCESS && res != KV_WRN_MORE) {
            printf("kv_poll_completion 1 error! exit\n");
            exit(1);
        }
        total_done += finished;
    }
    
    if (iohdl.retcode != KV_SUCCESS) {
        printf("kv_open_iterator failed failed: return code 0x%03X\n", iohdl.retcode);
        //res = kv_close_iterator(sq_hdl, ns_hdl, iohdl.hiter, &post_fn_data);
        if (iohdl.retcode == 0x903){
            // too many iterators open            
            printf("Too Many Iterators opens with error: 0x%03x",iohdl.retcode);
        }
        printf("Exiting\n");
        exit(1);
    }*/
 
    //////////////////////////////////
    // iterator next
    kv_iterator_handle hiter = iohdl.hiter;
    kv_iterator_list iter_list;
    iohdl.iter_list = &iter_list;
    uint32_t buffer_size = 32*1024;
    //uint8_t buffer[buffer_size];
    uint8_t *buffer;
    iter_list.size = 32*1024;
    //4K aligning the 32K buffer
    int ret = posix_memalign((void **)&buffer, 4096, buffer_size);
    if (ret || !buffer) {
        printf("fail to alloc buf size %d\n", buffer_size);
        //res = -ENOMEM;
        exit(1);

    }
    //printf("After posix\n");
    memset(buffer, 0, buffer_size);
    //printf("Before Seg\n");
    iter_list.it_list = (uint8_t *) buffer;
    //printf("After Seg\n");
    iter_list.num_entries = 0;
    iter_list.end = FALSE;
    uint32_t total = 0;
    uint32_t run_cnt = 0; 
    
    while (1) {
        iter_list.size = buffer_size;
        memset(iter_list.it_list, 0, buffer_size); 
        iter_list.num_entries = 0;        
        g_complete_count = 0;
        //printf("Before kv_iterator_next\n");
        res = kv_iterator_next(sq_hdl, ns_hdl, hiter, &iter_list, &post_fn_data);
        //printf("After kv_iterator_next\n");
        // poll for completion
        while (g_complete_count < 1) {
            uint32_t finished = 0;
            kv_result res = kv_poll_completion(cq_hdl, 0, &finished);
            if (res != KV_SUCCESS && !iter_list.end) {
                printf("kv_poll_completion 2 error! exit\n");
                exit(1);
            }
            if (iter_list.end){
                // Scan finished, these are the last entries
                g_complete_count = 1;
            }
        }
 
        if (iohdl.retcode != KV_SUCCESS && !iter_list.end) {
            printf("kv_iterator_next_set failed: return code 0x%03X\n", iohdl.retcode);
            exit(1);
        }

        // now check returned key/values
        //printf("Before processing_iterator_returned_keyvals");
        processing_iterator_returned_keyvals(&iter_list, iter_op, keys, values);
       
        printf("client got entries:%d\n", (uint32_t *)(iter_list.num_entries)); 
        total += iter_list.num_entries;
        //printf("going to stick in the loop\n");
        if (iter_list.end) {
            printf("Total Entries:%d\n",total);
            //printf("didnt get  stuck in the loop\n");
            break;
        }
    }
   
   // printf("client got total entries:%d\n", total);


    // close iterator
    //////////////////////////////////
    // iohdl_t iohdl;
    // iohdl.hiter (a handle)
    // kv_postprocess_function post_fn_data = { on_IO_complete_func, &iohdl};
    res = kv_close_iterator_sync(sq_hdl, ns_hdl, iohdl.hiter);
    
    if(buffer){ free(buffer);}
    
    while (res != KV_SUCCESS) {
        printf(" kv_close_iterator failed There with error: 0x%X\n", res);
        //res = kv_close_iterator(sq_hdl, ns_hdl, iohdl.hiter, &post_fn_data);
        exit(1);
    }

    // poll for completion
    /*total_done = 0;
    while (total_done < 1) {
        uint32_t finished = 0;
        kv_result res = kv_poll_completion(cq_hdl, 0, &finished);
        if (res != KV_SUCCESS && res != KV_WRN_MORE) {
            printf("kv_poll_completion Here error! exit\n");
            exit(1);
        }
        total_done += finished;
    }

    if (iohdl.retcode != KV_SUCCESS) {
        printf("In the end kv_close_iterator failed: return code 0x%03X\n", iohdl.retcode);
        exit(1);
    }*/
}

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
    //uint32_t expected_total = count;
    uint32_t expected_total = 0;
    uint32_t prefix = 0;
    prefix = PREFIX_KV;
    kv_group_condition grp_cond;

    //////////////////////////////
    //grp_cond.bitmask = 0xFF000000;
    //grp_cond.bit_pattern = prefix;
    // delete all existing entries -- not supported now 
    //delete_group(ns_hdl, sq_hdl, cq_hdl, &grp_cond);


    //////////////////////////////
    // generate a set
    grp_cond.bitmask = BITMASK_KV;
    grp_cond.bit_pattern = prefix;
    expected_total = count;
    generate_prefixed_dataset(ns_hdl, sq_hdl, cq_hdl, iohdls, count, iterator_entries, prefix, &grp_cond, iter_op, expected_total);
    
    iterator_entries.expected_total = expected_total;
    iterator_entries.grp_cond.bitmask = BITMASK_KV; // 0xFF000000;
    iterator_entries.grp_cond.bit_pattern = PREFIX_KV;

    }
    

// test iterators
uint64_t test_iterators_one_bitmask(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, iohdl_t *iohdls, uint32_t count, kv_iterator_option iter_op) {

    std::vector<std::string> keys;
    std::vector<std::string> values;

    iterator_entries_t iterator_entries;
    //printf("Before prepare_test_iterators_one_bitmask\n");
    prepare_test_iterators_one_bitmask(ns_hdl, sq_hdl, cq_hdl, iohdls, count, iterator_entries, iter_op);
    
    //printf("Before get_iterator_results\n");
    get_iterator_results(ns_hdl, sq_hdl, cq_hdl, iohdls, count, &iterator_entries.grp_cond, iter_op, keys, values);
    
    //printf("Validating 1 bitmask\n");
    validate_iterator_results(iterator_entries, keys, values);

    
    return 0;
}


// test iterators main routine
uint64_t test_iterators(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, iohdl_t *iohdls, uint32_t count, kv_iterator_option iter_op) {
    test_iterators_one_bitmask(ns_hdl, sq_hdl, cq_hdl, iohdls, count, iter_op);
    //test_iterators_multiple_bitmask(ns_hdl, sq_hdl, cq_hdl, iohdls, count, iter_op);
    return 0;
}

// test list iterators, return the count of open iterators
int test_list_iterators(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, std::vector<kv_iterator_handle>& iters) {

    kv_iterator kv_iters[SAMSUNG_MAX_ITERATORS];
    memset(kv_iters, 0, sizeof(kv_iters));
    uint32_t count = SAMSUNG_MAX_ITERATORS;

    iohdl_t iohdl;
    kv_postprocess_function post_fn_data = { on_IO_complete_func, &iohdl};

    // this sync admin call
    kv_result res = kv_list_iterators_sync(sq_hdl, ns_hdl, kv_iters, &count);

    if (res) {
        printf("kv_list_iterators with error: 0x%X\n", res);
        exit(1);
    }

    // simply print for any return iterators
    for (uint32_t i = 0; i < count; i++) {
        if (kv_iters[i].status == 1) {
            fprintf(stderr, "found open iterator id %d\n", kv_iters[i].handle_id);
            iters.push_back(kv_iters[i].handle_id);
        }
    }

    return iters.size();
}

kv_iterator_handle test_open_iterator(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, kv_iterator_handle iter_handle) {

    //////////////////////////////////
    // open iterator
    // iohdl.hiter (a handle)
    iohdl_t iohdl;
    iohdl.hiter = iter_handle;
    // kv_iterator_option iter_op = KV_ITERATOR_OPT_KV;
    kv_iterator_option iter_op = KV_ITERATOR_OPT_KEY;

    static int i = 0;
    i++;
    // prefix has to be different for successful open
    uint32_t prefix = 0x87654321 + i;
    kv_group_condition grp_cond;
    //////////////////////////////
    grp_cond.bitmask = 0xFF000000;
    grp_cond.bit_pattern = prefix;


    //////////////////////////////////
    // open iterator
    // iohdl.hiter (a handle)
    kv_result res = kv_open_iterator_sync(sq_hdl, ns_hdl, iter_op, &grp_cond, &iohdl.hiter);
    while (res != KV_SUCCESS) {
        printf("kv_open_iterator failed with error: 0x%X\n", res);
        res = kv_open_iterator_sync(sq_hdl, ns_hdl, iter_op, &grp_cond, &iohdl.hiter);
    }

    // poll for completion
    /*uint32_t total_done = 0;
    while (total_done < 1) {
        uint32_t finished = 0;
        kv_result res = kv_poll_completion(cq_hdl, 0, &finished);
        if (res != KV_SUCCESS && res != KV_WRN_MORE) {
            printf("kv_poll_completion error! exit\n");
            exit(1);
        }
        total_done += finished;
    }*/

    /*
    if (iohdl.retcode != KV_SUCCESS) {
        printf("kv_open_iterator failed failed: return code 0x%03X\n", iohdl.retcode);
        exit(1);
    }*/

    // XXX looks like device just returns 0 in case of max iterator has reached
    /*if (iohdl.retcode == KV_SUCCESS && iohdl.hiter) {
        iters.push_back(iohdl.hiter);
    }

    if (iohdl.hiter == 0) {
        fprintf(stderr, "kv_open_iterator error, got 0, return code: 0x%x\n", iohdl.retcode);
    }*/
    return iohdl.hiter;
}

void test_close_iterator(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, std::vector<kv_iterator_handle>& iters) {
 
    for (kv_iterator_handle hiter : iters) {
        // close iterator
        if (hiter <= 0) {
            continue;
        }
        kv_result res = kv_close_iterator_sync(sq_hdl, ns_hdl, hiter);
        while (res != KV_SUCCESS) {
            printf("kv_close_iterator submit failed with error: 0x%X, retry\n", res);
            res = kv_close_iterator_sync(sq_hdl, ns_hdl, hiter );
        }
        // poll for completion
        /*int total_done = 0;
        while (g_complete_count < 1) {
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
        }*/
    }
}

// test max iterators allowed and list iterators
void test_max_allowed_iterators(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl) {
    std::vector<kv_iterator_handle> iters;

    // close all existing iterators
    int count = test_list_iterators(ns_hdl, sq_hdl, cq_hdl, iters);
    test_close_iterator(ns_hdl, sq_hdl, cq_hdl, iters);
    iters.clear();
 
    // open max iterators
    kv_iterator_handle iter_handle;
    for (int i = 0; i < SAMSUNG_MAX_ITERATORS; i++) {
        kv_iterator_handle itid = test_open_iterator(ns_hdl, sq_hdl, cq_hdl, iter_handle);
        if (itid <= 0) {
            printf("open iterators within limit failed, return invalid iterator handle id %u\n", itid);
            // exit(1);
        }
    }

    std::vector<kv_iterator_handle> iters_returned;
    // test count open iterators
    count = test_list_iterators(ns_hdl, sq_hdl, cq_hdl, iters_returned);
    if (count != SAMSUNG_MAX_ITERATORS) {
        printf("kv_list_iterators doesn't return correct count of open iterators\n");
        printf("kv_list_iterators returned %lu expect %u\n", iters_returned.size(), SAMSUNG_MAX_ITERATORS);
        exit(1);
    }

    // close opened iterators by this test
    test_close_iterator(ns_hdl, sq_hdl, cq_hdl, iters_returned);

    iters_returned.clear();
    count = test_list_iterators(ns_hdl, sq_hdl, cq_hdl, iters_returned);
    if (count != 0) {
        printf("kv_list_iterators doesn't return correct 0 count of after closing all iterators\n");
        exit(1);
    }

}

uint64_t process_one_round_delete(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, iohdl_t *iohdls, uint32_t count) {

    uint64_t start = current_timestamp();
    // insert key value
    g_complete_count = 0;
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
    while (g_complete_count < count) {
        uint32_t finished = 0;
        kv_result res = kv_poll_completion(cq_hdl, 0, &finished);
        if (res != KV_SUCCESS && res != KV_WRN_MORE) {
            printf("kv_poll_completion error! exit\n");
            exit(1);
        }
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
    g_complete_count = 0;
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
    while (g_complete_count < count) {
        uint32_t finished = 0;
        kv_result res = kv_poll_completion(cq_hdl, 0, &finished);
        if (res != KV_SUCCESS && res != KV_WRN_MORE) {
            printf("kv_poll_completion error! exit\n");
            exit(1);
        }
    }

    // compare read value with write value
    for (uint32_t i = 0; i < count; i++) {
        if ((iohdls + i)->retcode != KVS_ERR_KEY_NOT_EXIST) {
            printf("delete key test failed, key shouldn't be found: item %u: return code 0x%03X\n", i, (iohdls + i)->retcode);
            exit(1);
        }
    }

    return (end - start);
}


// read i th key and validate if it's really there
bool process_one_key_read(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, iohdl_t *iohdls, uint32_t i, int skip_value_compare) {

    kv_key *key = (iohdls + i)->key;
    // printf("reading key: %s\n", key->key);
    // prefix of keys
    // printf("reading key 4MSB: 0x%X\n", *(uint32_t *)&key->key);

    kv_value *value = (iohdls + i)->value;
    memset(value->value, '0', value->length);
    g_complete_count = 0;
    kv_postprocess_function *post_fn_data = &((iohdls + i)->post_fn_data);
    kv_result res = kv_retrieve(sq_hdl, ns_hdl, key, KV_RETRIEVE_OPT_DEFAULT, value, post_fn_data);
    while (res != KV_SUCCESS) {
        printf("kv_retrieve failed with error: 0x%X\n", res);
        res = kv_retrieve(sq_hdl, ns_hdl, key, KV_RETRIEVE_OPT_DEFAULT, value, post_fn_data);
    }

    // poll for completion
    while (g_complete_count < 1) {
        uint32_t finished = 0;
        kv_result res = kv_poll_completion(cq_hdl, 0, &finished);
        if (res != KV_SUCCESS && res != KV_WRN_MORE) {
            printf("kv_poll_completion error! exit\n");
            exit(1);
        }
    }

    if ((iohdls + i)->retcode == KVS_ERR_KEY_NOT_EXIST) {
        return false;
    }

    // compare read value with write value
    // printf("validating key: %s\n", (iohdls + i)->key->key);
    if (!skip_value_compare && memcmp((iohdls + i)->value_save->value, (iohdls + i)->value->value, (iohdls + i)->value->length)) {
        // printf("failed: value read not the same as value written, key %s\n value + 16 %s\n value %s\n", (iohdls + i)->key->key, (iohdls + i)->value->value + 16, (iohdls + i)->value_save->value);
        // XXX please check if any duplicate keys are used, only unique keys should
        // be used, otherwise you may get false positive.
        printf("failed: value read not the same as value written, original data len %d, device returned data len %d\n", (iohdls + i)->value_save->length, (iohdls + i)->value->length);
        // printf("Note: please only use unique keys for this validation\n");
        exit(1);
    }

    return true;
}



uint64_t process_one_round_kv_exist(kv_namespace_handle ns_hdl, kv_queue_handle sq_hdl, kv_queue_handle cq_hdl, iohdl_t *iohdls, uint32_t count) {

    uint64_t start = current_timestamp();
    // delete keys first
    for (uint32_t i = 0; i < count; i++) {
        delete_key(ns_hdl, sq_hdl, cq_hdl, (iohdls + i)->key, 1);
    }
 
    g_complete_count = 0;
    for (uint32_t i = 0; i < count; i++) {
        // insert a key value pair
        // Please note key and value is part of IO handle inside post_fn_data
        kv_key *key = (iohdls + i)->key;
        kv_value *value = (iohdls + i)->value;
        kv_postprocess_function *post_fn_data = &((iohdls + i)->post_fn_data);

        // save for comparison later
        memcpy((iohdls + i)->value_save->value, (iohdls + i)->value->value, (iohdls + i)->value->length);

        kv_result res = kv_store(sq_hdl, ns_hdl, key, value, KV_STORE_OPT_DEFAULT, post_fn_data);
        while (res != KV_SUCCESS) {
            printf("kv_exit test kv_store failed with error: 0x%03X\n", res);
            res = kv_store(sq_hdl, ns_hdl, key, value, KV_STORE_OPT_DEFAULT, post_fn_data);
        }

    }

    // poll for completion
    while (g_complete_count < count) {
        uint32_t finished = 0;
        kv_result res = kv_poll_completion(cq_hdl, 0, &finished);
        if (res != KV_SUCCESS && res != KV_WRN_MORE) {
            printf("kv_poll_completion error! exit\n");
            exit(1);
        }
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

    // double check all keys are there
    for (uint32_t i = 0; i < count; i++) {
        if (!process_one_key_read(ns_hdl, sq_hdl, cq_hdl, iohdls, i, 0)) {
            printf("inserted keys are not there\n");
            exit(1);
        }
    }

    kv_key *keys = (kv_key *) calloc(count, sizeof(kv_key));
    // set half of keys that shouldn't exist
    for (uint32_t i = 0; i < count; i++) {
        kv_key *key_i = keys + i;
        key_i->key = calloc((iohdls + i)->key->length, 1);
        memcpy(key_i->key, (iohdls + i)->key->key, (iohdls + i)->key->length);
        key_i->length = (iohdls + i)->key->length;

        if (i % 2 == 0) {
            // char *buffer = (char *) key_i->key;
            // delete these keys, so they are truly gone
            delete_key(ns_hdl, sq_hdl, cq_hdl, key_i, 1);

            // printf("\n\nXXX check a deleted key\n");
            // print_key(key_i);
 
            if (process_one_key_read(ns_hdl, sq_hdl, cq_hdl, iohdls, i, 1)) {
                printf("Error: a deleted key is still retrieved.\n");
                exit(1);
            }

            if (check_key_exist(ns_hdl, sq_hdl, cq_hdl, key_i)) {
                printf("Error: kv_exist found a deleted key\n");
                exit(1);
            }
        } else {
            // printf("\n\nXXX check a valid key\n");
            // print_key(key_i);

            if (!process_one_key_read(ns_hdl, sq_hdl, cq_hdl, iohdls, i, 0)) {
                printf("Error: an existent key is not retrieved.\n");
                exit(1);
            }

            if (!check_key_exist(ns_hdl, sq_hdl, cq_hdl, key_i)) {
                printf("Error: kv_exist didn't find an existent key\n");
                exit(1);
            }
        }
    }

    for (uint32_t i = 0; i < count; i++) {
        free((keys + i)->key);
    }
    free(keys);

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

    //int i = 0;

    while (left > 0) {
        if (left < count) {
            count = left;
        }
        left -= count;

        // printf("round %d\n", i++);
        // set up new random key valueis
 
        populate_iohdls(iohdls, prefix, klen, vlen, count);

        // test write and read
        uint64_t time_w = process_one_round_write(ns_hdl, sq_hdl, cq_hdl, iohdls, count);
        total_w += time_w;

        uint64_t time_r = process_one_round_read(ns_hdl, sq_hdl, cq_hdl, iohdls, count);
        total_r += time_r;

        process_one_round_delete(ns_hdl, sq_hdl, cq_hdl, iohdls, count);

        process_one_round_kv_exist(ns_hdl, sq_hdl, cq_hdl, iohdls, count);

        // test list iterators
        test_max_allowed_iterators(ns_hdl, sq_hdl, cq_hdl);

        // test partial read, not supported by device yet
        // process_one_round_read_partial(ns_hdl, sq_hdl, cq_hdl, iohdls, count);
    }

    // test iterators
    test_iterators(ns_hdl, sq_hdl, cq_hdl, iohdls, count, KV_ITERATOR_OPT_KEY);
   
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

    printf("XXX, %.1f, %.1f\n", iops_w, iops_r);
    // printf("IOs %u, IO time %llu (ns), app time %llu (ns)\n", tcount, (long long unsigned) total_w, (long long unsigned) (end - start));


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

// for linux kernel based device
void create_device(const char *devpath, bool_t is_polling, int qdepth, device_handle_t *device_handle) {
    // int klen = 16;
    // int vlen = 4096;
    // int qdepth = 64;
    kv_device_init_t dev_init;

    // you can supply your own configuration file
    // dev_init.configfile = "./kvssd_emul.conf";
    // the emulator device path is fixed and virtual
    // you don't need to create it.
    dev_init.devpath = strdup(devpath);
    dev_init.need_persistency = FALSE;
    dev_init.is_polling = is_polling;
    dev_init.queuedepth = qdepth;
    
    kv_device_handle dev_hdl = NULL;
    device_handle->qdepth = qdepth;

    // initialize the device
    kv_result ret = kv_initialize_device(&dev_init, &dev_hdl);
    if (ret != KV_SUCCESS) {
        printf("device creation error 0x%x\n", ret);
        exit(1);
    }
    // print device stats
    kv_device devinfo;
    kv_device_stat devst;
    kv_get_device_info(dev_hdl, &devinfo);
    kv_get_device_stat(dev_hdl, &devst);
    printf("device version is %uB\n", devinfo.version);
    printf("capacity is %lluB\n", (unsigned long long) devinfo.capacity);
    printf("device stats utilization %u\n", devst.utilization);
    printf("device stats ns count %u\n", devst.namespace_count);
    printf("device stats queue count %u\n", devst.queue_count);
    // printf("device waf %u\n", devst.waf);

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

    device_handle->dev_init = dev_init;
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
 
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
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
    free((char *) device_handle->dev_init.devpath);
}


int main(int argc, char**argv) {
    char *device = NULL;
    if (argc <  5) {
        printf("Please run\n  %s <number of keys>   <bitmask> <prefix>  <device>\nPlease use hexdecimal or bitmask and prefix", argv[0]);
        printf("Example: sudo LD_LIBRARY_PATH=. ./test_suite 12356 0xffffffff 0x12345678 /dev/nvme0n1\n");
        printf("Exiting..\n");
        exit(1);
        //printf("Default: %d\n", TOTAL_KV_COUNT);
    } else {
        char *prefix_str = NULL;
        char *bitmask_str = NULL;
        TOTAL_KV_COUNT = std::stoi(argv[1]);
        printf("Insert keys: %d\n", TOTAL_KV_COUNT);        

        char *pend;
        bitmask_str = argv[2]; 
        BITMASK_KV = strtoull(bitmask_str, &pend, 16);
        printf("Bitmask:0x%x\n",BITMASK_KV);        
        
        prefix_str = argv[3];
        PREFIX_KV = strtoull(prefix_str, &pend, 16);
        printf("Prefix: 0x%x\n", PREFIX_KV);
        device = argv[4];
    
    }

    int klen = 16;
    int vlen = 4096;
    int qdepth = TOTAL_KV_COUNT;

    
    // initialize globals
    g_KEYLEN_FIXED = 0;
    g_KEYLEN = klen;    
    device_handle_t *device1 = (device_handle_t *) malloc(sizeof (device_handle_t));

    /*
    device_handle_t *device1 = (device_handle_t *) malloc(sizeof (device_handle_t));
    device_handle_t *device2 = (device_handle_t *) malloc(sizeof (device_handle_t));
    device_handle_t *device3 = (device_handle_t *) malloc(sizeof (device_handle_t));
    device_handle_t *device4 = (device_handle_t *) malloc(sizeof (device_handle_t));
    */

    /*
     * device to test
    # /dev/nvme10n1
    # /dev/nvme11n1
    # /dev/nvme12n1
    # /dev/nvme13n1
    */
    bool_t is_polling = TRUE;

    printf("creating device 1\n");
    create_device(device, is_polling, qdepth, device1);

    /*
    printf("creating device 2\n");
    create_device("/dev/nvme11n1", is_polling, qdepth, device2);

    printf("creating device 3\n");
    create_device("/dev/nvme12n1", is_polling, qdepth, device3);

    printf("creating device 4\n");
    create_device("/dev/nvme13n1", is_polling, qdepth, device4);
    */


    printf("starting operation on device 1\n");
    std::thread th = std::thread(sample_main, device1, klen, vlen, TOTAL_KV_COUNT, qdepth);

    if (th.joinable()) {
        th.join();
    }

    /*
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
    */

    printf("shutdown all devices\n");
    shutdown_device(device1);

    free(device1);

    /*
    shutdown_device(device2);
    shutdown_device(device3);
    shutdown_device(device4);

    free(device1);
    free(device2);
    free(device3);
    free(device4);
    */

    printf("all done\n");
    return 0;
}
