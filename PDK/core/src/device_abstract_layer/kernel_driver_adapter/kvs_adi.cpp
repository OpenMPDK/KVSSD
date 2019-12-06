/**
 *   BSD LICENSE
 *
 *   Copyright (c) 2018 Samsung Electronics Co., Ltd.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Samsung Electronics Co., Ltd. nor the names of
 *       its contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <map>

/**
 * C implementation for kvs_adi.h
 * also a wrapper for many other ADI implementation C++ classes
 */
#include "kvs_adi.h"
#include "kvs_adi_internal.h"

#include "io_cmd.hpp"
#include "queue.hpp"

#include "kv_device.hpp"
#include "kv_namespace.hpp"
#include "kvs_adi_internal.h"
#include "thread_pool.hpp"
#include "kadi.h"
#include "kvs_utils.h"

#define FTRACE
#include "kadi_debug.h"

using namespace kvadi;
// namespace kvadi {

inline KADI *get_kadi_from_hdl(const kv_device_handle dev_hdl)
{
    if (dev_hdl->dev == NULL) {
        return 0;
    }

    return (KADI *) dev_hdl->dev;  
}

class devlist {
public:
    std::recursive_mutex s_mutex;
    std::set<void *> s_global_device_list;
    int add(void* adi) {
        std::lock_guard<std::recursive_mutex> lock(s_mutex);
        auto p = s_global_device_list.insert(adi);
        if (p.second) {
            return s_global_device_list.size() -1;
        }
        return -1;
    }
    int remove(void* adi) {
        std::lock_guard<std::recursive_mutex> lock(s_mutex);
        auto p = s_global_device_list.find(adi);
        if (p != s_global_device_list.end()) {
             s_global_device_list.erase(p);

             return 0;
        }
        return -1;
    }
};

static devlist g_devices;

//
// device APIs
kv_result kv_initialize_device(void *dev_init, kv_device_handle *dev_hdl) {
    FTRACE
    kv_device_init_t *options = (kv_device_init_t *) dev_init;
    std::string devpath (options->devpath);

    if (dev_hdl == NULL) {
        return KV_ERR_PARAM_INVALID;
    } else if (*dev_hdl != NULL) {
        return KV_ERR_DEV_INITIALIZED;
    }
    
    // create a new device object
    kv_device_handle hnd = new _kv_device_handle;
    hnd->dev = (void *) new KADI(options->queuedepth);
    int devid = g_devices.add(hnd->dev);
    
    if (devid == -1) {
        kv_cleanup_device(hnd);
        return KV_ERR_DEV_INIT;
    }
    
    KADI *dev = get_kadi_from_hdl(hnd);
    if(dev == NULL){
        return KV_ERR_DEV_NOT_EXIST;
    }
    int ret = dev->open(devpath);
    if (ret == 0) {
        dev->update_capacity();
        hnd->devid = devid;
    
        (*dev_hdl) = hnd;
    }
    else {
        if (errno == ENOENT) {
            ret = KV_ERR_DEV_NOT_EXIST;
        } else if (errno == EACCES) {
            ret = KV_ERR_PERMISSION;
        } else {
            ret = KV_ERR_UNCORRECTIBLE;
        }
        kv_cleanup_device(hnd);
    }
    
    return ret;  
}

kv_result kv_cleanup_device(kv_device_handle dev_hdl) {
    FTRACE
    if (!dev_hdl) return KV_ERR_PARAM_INVALID;
    if (dev_hdl->dev) {
        g_devices.remove(dev_hdl->dev);        
        delete (KADI*)dev_hdl->dev;
    }
    delete dev_hdl;
    return KV_SUCCESS;
}

kv_result kv_get_device_info(const kv_device_handle dev_hdl, kv_device *devinfo)
{
    FTRACE
    if (devinfo == NULL || dev_hdl == NULL ) {
        return KV_ERR_PARAM_INVALID;
    }
    KADI *dev =  get_kadi_from_hdl(dev_hdl);
    if (dev == NULL) { return KV_ERR_DEV_NOT_EXIST; }

    devinfo->version = 1;   
    devinfo->max_namespaces = 1;
    devinfo->max_queues = 1;
    devinfo->capacity = dev->capacity;      
    devinfo->min_value_len = SAMSUNG_KV_MIN_VALUE_LEN; 
    devinfo->max_value_len = SAMSUNG_KV_MAX_VALUE_LEN; 
    devinfo->min_key_len = SAMSUNG_KV_MIN_KEY_LEN;     
    devinfo->max_key_len = SAMSUNG_KV_MAX_KEY_LEN;     
    devinfo->extended_info = 0;

    return KV_SUCCESS;
}

// pull Write Amplification Factor
kv_result kv_get_device_waf(const kv_device_handle dev_hdl, uint32_t *waf) {

    KADI *dev =  get_kadi_from_hdl(dev_hdl);
    if (dev == NULL) { 
	 return KV_ERR_DEV_NOT_EXIST; 
    }

    *waf = dev->get_dev_waf();

    return 0;

}

kv_result kv_get_device_stat(const kv_device_handle dev_hdl, kv_device_stat *dev_st) {
    FTRACE
    if (dev_st == NULL) {
        return KV_ERR_PARAM_INVALID;
    }
    
    uint64_t bytesused, capacity;
    double utilization;

    KADI *dev =  get_kadi_from_hdl(dev_hdl);
    if (dev == NULL) { return KV_ERR_DEV_NOT_EXIST; }

    int r = dev->get_freespace(bytesused, capacity, utilization);
    if (r != 0) { return KV_ERR_SYS_IO;    }

    dev_st->namespace_count = 1;
    dev_st->queue_count = 1;
    dev_st->utilization = (uint16_t)round(utilization * 10000);
    dev_st->waf = 0;
    dev_st->extended_info = 0;

    return KV_SUCCESS;
}

kv_result kv_sanitize(kv_queue_handle que_hdl, kv_device_handle dev_hdl, kv_sanitize_option option, kv_sanitize_pattern *pattern, kv_postprocess_function *post_fn)
{
    FTRACE
    return KV_ERR_DD_UNSUPPORTED_CMD;
}

// queue APIs
kv_result kv_create_queue(kv_device_handle dev_hdl, const kv_queue *que, kv_queue_handle *que_hdl) {
    FTRACE
    if (dev_hdl == NULL || que == NULL || que_hdl == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    KADI *dev =  get_kadi_from_hdl(dev_hdl);
    if (dev == NULL) { return KV_ERR_DEV_NOT_EXIST; }

    (*que_hdl) = new _kv_queue_handle();
    (*que_hdl)->qid = 0;
    (*que_hdl)->dev = (void *)dev;
    (*que_hdl)->queue = (void *)que;

    return KV_SUCCESS;
}

kv_result kv_delete_queue(kv_device_handle dev_hdl, kv_queue_handle que_hdl) {
    FTRACE
    if (que_hdl)
        delete que_hdl;
    return 0;
}

kv_result kv_get_queue_handles(const kv_device_handle dev_hdl, kv_queue_handle *que_hdls, uint16_t *que_cnt) {
    FTRACE
    return KV_ERR_DD_UNSUPPORTED_CMD;
}

kv_result kv_get_queue_info(const kv_device_handle dev_hdl, const kv_queue_handle que_hdl, kv_queue *queinfo) {
    FTRACE
    return KV_ERR_DD_UNSUPPORTED_CMD;
}

kv_result kv_get_queue_stat(const kv_device_handle dev_hdl, const kv_queue_handle que_hdl, kv_queue_stat *que_st) {
    FTRACE
    return KV_ERR_DD_UNSUPPORTED_CMD;
}

// namespace APIs
kv_result kv_create_namespace(kv_device_handle dev_hdl, const kv_namespace *ns, kv_namespace_handle ns_hdl) {
    FTRACE
    return KV_ERR_DEV_MAX_NS;
}

kv_result kv_delete_namespace(kv_device_handle dev_hdl, kv_namespace_handle ns_hdl) {
    FTRACE
    if (ns_hdl) {
        delete ns_hdl;
        return KV_SUCCESS;
    }
    return KV_ERR_NS_DEFAULT;
}

kv_result kv_attach_namespace(kv_device_handle dev_hdl, kv_namespace_handle ns_hdl) {
    FTRACE
    return KV_ERR_NS_DEFAULT; 
}

kv_result kv_detach_namespace(kv_device_handle dev_hdl, kv_namespace_handle ns_hdl) {
    FTRACE
    return KV_ERR_NS_DEFAULT; 
}

kv_result kv_list_namespaces(const kv_device_handle dev_hdl, kv_namespace_handle *ns_hdls, uint32_t *ns_cnt) {
    FTRACE
    if (dev_hdl == NULL || ns_hdls == NULL || ns_cnt == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    KADI *dev = get_kadi_from_hdl(dev_hdl);  
    if (dev == NULL) {
        return KV_ERR_DEV_NOT_EXIST;
    }

    // always return the default one
    *ns_cnt = 1;
    ns_hdls[0]->nsid = (uint32_t) KV_NAMESPACE_DEFAULT;
    ns_hdls[0]->dev = dev_hdl->dev;
    ns_hdls[0]->ns = (void *) 0;

    return KV_SUCCESS;
}

kv_result kv_get_namespace_info(const kv_device_handle dev_hdl, const kv_namespace_handle ns_hdl, kv_namespace *nsinfo) {
    FTRACE
    if (nsinfo == NULL || ns_hdl == NULL ) {
        return KV_ERR_PARAM_INVALID;
    }
    KADI *dev = get_kadi_from_hdl(dev_hdl);  
    if (dev == NULL) {
        return KV_ERR_DEV_NOT_EXIST;
    }

    nsinfo->nsid = 0;        
    nsinfo->attached = TRUE;   
    nsinfo->capacity = dev->capacity;
    nsinfo->extended_info = 0;  

    return KV_SUCCESS;
}

kv_result kv_get_namespace_stat(const kv_device_handle dev_hdl, const kv_namespace_handle ns_hdl, kv_namespace_stat *ns_st) {
    FTRACE
    uint64_t bytesused, capacity;
    double utilization;

    KADI *dev =  get_kadi_from_hdl(dev_hdl);
    if (dev == NULL) { return KV_ERR_DEV_NOT_EXIST; }

    int r = dev->get_freespace(bytesused, capacity, utilization);
    if (r != 0) { return KV_ERR_SYS_IO;    }
    
    ns_st->unallocated_capacity = capacity - bytesused;
    ns_st->nsid = 0;

    return KV_SUCCESS;
}

kv_result get_namespace_default(kv_device_handle dev_hdl, kv_namespace_handle *ns_hdl) {
    if (dev_hdl == NULL  || ns_hdl == NULL) {
        return KV_ERR_PARAM_INVALID;
    }
    KADI *dev = (KADI *) dev_hdl->dev;
    if (dev == NULL) {
        return KV_ERR_DEV_NOT_EXIST;
    }

    (*ns_hdl) = new _kv_namespace_handle();
    (*ns_hdl)->dev = dev;
    (*ns_hdl)->ns = 0;
    (*ns_hdl)->nsid = 0;

    return KV_SUCCESS;
}

// internal API, added for an emulator
kv_result _kv_bypass_namespace(const kv_device_handle dev_hdl, const kv_namespace_handle ns_hdl, bool_t bypass) {
    FTRACE
    return KV_ERR_DD_UNSUPPORTED_CMD;
}

// IO APIs
kv_result kv_purge(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl,
    uint8_t ks_id, kv_purge_option option, kv_postprocess_function *post_fn) {
    FTRACE

    if (que_hdl == NULL || ns_hdl == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    return KV_ERR_DD_UNSUPPORTED_CMD;
}

kv_result kv_open_iterator_sync(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl,
    uint8_t ks_id, const kv_iterator_option it_op, const kv_group_condition *it_cond,
    uint8_t *iter_handle) {
    kv_iter_context iter_ctx;
    /* The bitpattern of the KV API is of big-endian mode. If the CPU is of little-
endian mode, the bit pattern and bit mask should be transformed.
 */
    iter_ctx.prefix  = htobe32(it_cond->bit_pattern);  
    iter_ctx.bitmask = htobe32(it_cond->bitmask);     
    iter_ctx.buflen  = ITER_BUFSIZE;

    KADI *dev = (KADI *) que_hdl->dev;
    if (dev == NULL) { return KV_ERR_DEV_NOT_EXIST; }

    nvme_kv_iter_req_option dev_option;
    switch(it_op){
      case KV_ITERATOR_OPT_KEY:
        dev_option = ITER_OPTION_KEY_ONLY;
        break;
      case KV_ITERATOR_OPT_KV:
        dev_option = ITER_OPTION_KEY_VALUE;
        break;
      case KV_ITERATOR_OPT_KV_WITH_DELETE:
        dev_option = ITER_OPTION_DEL_KEY_VALUE;
        break;
      default:
        return KV_ERR_OPTION_INVALID;
    }
    kv_result ret = dev->iter_open(ks_id, &iter_ctx, dev_option);
    if (ret == KV_SUCCESS) {
        *iter_handle = (uint8_t)iter_ctx.handle;
    }
    return ret;
}

kv_result kv_close_iterator_sync(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl,
  kv_iterator_handle iter_hdl) {
    FTRACE
    if (que_hdl == NULL || ns_hdl == NULL || iter_hdl == 0) {
        return KV_ERR_PARAM_INVALID;
    }
    kv_iter_context iter_ctx;
    iter_ctx.handle  = iter_hdl;

    KADI *dev = (KADI *) que_hdl->dev;
    if (dev == NULL) { return KV_ERR_DEV_NOT_EXIST; }
  
    return  dev->iter_close(&iter_ctx);
}
kv_result kv_iterator_next(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl,
  kv_iterator_handle iter_hdl, kv_iterator_list *iter_list, kv_postprocess_function *post_fn)
{
    if (que_hdl == NULL || ns_hdl == NULL || iter_hdl <= 0
        || iter_hdl > SAMSUNG_MAX_ITERATORS || iter_list == NULL) {
        return KV_ERR_PARAM_INVALID;
    }
    kv_iter_context iter_ctx;
    iter_ctx.handle  = iter_hdl;
    iter_ctx.buf =  iter_list->it_list;
    iter_ctx.buflen = iter_list->size;

    KADI  *dev = (KADI  *) que_hdl->dev;
    return dev->iter_read_async(&iter_ctx, post_fn);
}

kv_result kv_iterator_next_sync(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl,
  kv_iterator_handle iter_hdl, kv_iterator_list *iter_list) {
    FTRACE
    if (que_hdl == NULL || ns_hdl == NULL || iter_hdl <= 0
        || iter_hdl > SAMSUNG_MAX_ITERATORS || iter_list == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    kv_iter_context iter_ctx;
    iter_ctx.handle  = iter_hdl;
    iter_ctx.buf =  iter_list->it_list;
    iter_ctx.buflen = iter_list->size;

    KADI  *dev = (KADI  *) que_hdl->dev;
    kv_result ret = dev->iter_read(&iter_ctx);

    if (ret == KV_SUCCESS || ret == 0x393){
      if(iter_ctx.byteswritten > 0){
        const unsigned int key_count = *((uint32_t *)iter_ctx.buf);
        iter_list->num_entries = key_count;
        iter_list->size = iter_ctx.byteswritten;
      }else{
        iter_list->num_entries = 0;
        iter_list->size = 0;
      }
      if(ret == KV_SUCCESS)
        iter_list->end  = (iter_ctx.end)? TRUE:FALSE;
      else{
        iter_list->end = TRUE;
        ret = KV_SUCCESS;
      }
    }
    return ret;
}
kv_result kv_list_iterators(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl,
  kv_iterator *kv_iters, uint32_t *iter_cnt, kv_postprocess_function  *post_fn) {
    return KV_ERR_DD_UNSUPPORTED_CMD;
}

kv_result kv_list_iterators_sync(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl,
  kv_iterator *kv_iters, uint32_t *iter_cnt) {
    FTRACE
    if (que_hdl == NULL || ns_hdl == NULL || kv_iters == NULL || iter_cnt == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    KADI *dev = (KADI *) que_hdl->dev;
    return dev->iter_list(kv_iters, iter_cnt);
}

kv_result kv_delete(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl,
  uint8_t ks_id, const kv_key *key, kv_delete_option option, kv_postprocess_function *post_fn) {
    FTRACE

    if (que_hdl == NULL || ns_hdl == NULL || key == NULL) {
        return KV_ERR_PARAM_INVALID;
    }
    KADI *dev = (KADI *) que_hdl->dev;
    
    return dev->kv_delete(ks_id, (kv_key *)key, post_fn, (int)option);
}

kv_result kv_delete_group(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl,
  uint8_t ks_id, kv_group_condition *grp_cond, kv_postprocess_function *post_fn) {
    FTRACE
    if (que_hdl == NULL || ns_hdl == NULL || grp_cond == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    return KV_ERR_DD_UNSUPPORTED_CMD;
}

kv_result kv_exist(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl,
  uint8_t ks_id, const kv_key *keys, uint32_t key_cnt, uint32_t buffer_size,
  uint8_t *buffer, kv_postprocess_function *post_fn) {
    FTRACE
    if (que_hdl == NULL || ns_hdl == NULL || key_cnt < 1 ) {
        return KV_ERR_PARAM_INVALID;
    }

    // current firmware supports only one key
    // result buffer is not going to be used.

    for (uint32_t i = 0; i < 1; i++) {
        kv_result res = validate_key_value(&keys[i], NULL);
        if (res != KV_SUCCESS) {
            return res;
        }
    }

    KADI *dev = (KADI *) que_hdl->dev;
    bool e =  dev->exist(ks_id, (kv_key *)&keys[0], post_fn);
    if (post_fn == NULL) {
        // sync 
        if (buffer && buffer_size > 0) buffer[0] = (e)? 1:0;
    } else if (e == false){
        // async submit failed 
        return KV_ERR_SYS_IO;
    }
    
    return KV_SUCCESS;
}

kv_result kv_retrieve(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl,
  uint8_t ks_id, const kv_key *key, kv_retrieve_option option, kv_value *value,
  const kv_postprocess_function *post_fn) {
    FTRACE
    if (que_hdl == NULL || ns_hdl == NULL || key == NULL || value == NULL) {
        return KV_ERR_PARAM_INVALID;
    }
    KADI *dev = (KADI *) que_hdl->dev;
    return dev->kv_retrieve(ks_id, (kv_key*)key, value, post_fn);
}

kv_result kv_retrieve_sync(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl,
  uint8_t ks_id, const kv_key *key, kv_retrieve_option option, kv_value *value) {
    FTRACE
    if (que_hdl == NULL || ns_hdl == NULL || key == NULL || value == NULL) {
        return KV_ERR_PARAM_INVALID;
    }
    KADI *dev = (KADI *) que_hdl->dev;
    return dev->kv_retrieve_sync(ks_id, (kv_key*)key, value);
}

kv_result kv_store(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl,
  uint8_t ks_id, const kv_key *key, const kv_value *value, kv_store_option option,
  const kv_postprocess_function *post_fn) {
    FTRACE
    if (que_hdl == NULL || ns_hdl == NULL || key == NULL || value == NULL) {
        return KV_ERR_PARAM_INVALID;
    }
    KADI *dev = (KADI *) que_hdl->dev;
    nvme_kv_store_option dev_option;
    switch(option){
      case KV_STORE_OPT_DEFAULT:
        dev_option = STORE_OPTION_NOTHING;
        break;
      case KV_STORE_OPT_COMPRESS:
        dev_option = STORE_OPTION_COMP;
        break;
      case KV_STORE_OPT_IDEMPOTENT:
        dev_option = STORE_OPTION_IDEMPOTENT;
        break;
      case KV_STORE_OPT_UPDATE_ONLY:
        dev_option = STORE_OPTION_UPDATE_ONLY;
        break;
      default:
        return KV_ERR_OPTION_INVALID;
    }
    return dev->kv_store(ks_id, (kv_key*)key, (kv_value*)value, dev_option, post_fn);
}

kv_result kv_poll_completion(kv_queue_handle que_hdl, uint32_t timeout_usec, uint32_t *num_events) {
    FTRACE
    if (que_hdl == NULL || num_events == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    KADI *dev = (KADI *) que_hdl->dev;
    return dev->poll_completion(*num_events, timeout_usec);
}

kv_result kv_set_interrupt_handler(kv_queue_handle que_hdl, const kv_interrupt_handler int_hdl) {
    FTRACE
    if (que_hdl == NULL || int_hdl == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    KADI *dev = (KADI *) que_hdl->dev;
    dev->int_handler.handler = int_hdl->handler;
    dev->int_handler.number = int_hdl->number;
    dev->int_handler.private_data = int_hdl->private_data;

    return KV_SUCCESS;
}



// } // end of namespace
