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

// namespace kvadi {

using namespace kvadi;

//
// device APIs
kv_result kv_initialize_device(void *dev_init, kv_device_handle *dev_hdl) {
    return kv_device_internal::kv_initialize_device((kv_device_init_t *) dev_init, dev_hdl);  
}

kv_result kv_cleanup_device(kv_device_handle dev_hdl) {
    return kv_device_internal::kv_cleanup_device(dev_hdl);
}

kv_result kv_get_device_info(const kv_device_handle dev_hdl, kv_device *devinfo)
{
    return kv_device_internal::kv_get_device_info(dev_hdl, devinfo);
}

kv_result kv_get_device_stat(const kv_device_handle dev_hdl, kv_device_stat *dev_st) {
    return kv_device_internal::kv_get_device_stat(dev_hdl, dev_st);
}

kv_result kv_sanitize(kv_queue_handle que_hdl, kv_device_handle dev_hdl, kv_sanitize_option option, kv_sanitize_pattern *pattern, kv_postprocess_function *post_fn)
{
    return kv_device_internal::kv_sanitize(que_hdl, dev_hdl, option, pattern, post_fn);
}

// queue APIs
kv_result kv_create_queue(kv_device_handle dev_hdl, const kv_queue *que, kv_queue_handle *que_hdl) {
    return kv_device_internal::kv_create_queue(dev_hdl, que, que_hdl);
}

kv_result kv_delete_queue(kv_device_handle dev_hdl, kv_queue_handle que_hdl) {
    return kv_device_internal::kv_delete_queue(dev_hdl, que_hdl);
}

kv_result kv_get_queue_handles(const kv_device_handle dev_hdl, kv_queue_handle *que_hdls, uint16_t *que_cnt) {
    return kv_device_internal::kv_get_queue_handles(dev_hdl, que_hdls, que_cnt);
}

kv_result kv_get_queue_info(const kv_device_handle dev_hdl, const kv_queue_handle que_hdl, kv_queue *queinfo) {
    return kv_device_internal::kv_get_queue_info(dev_hdl, que_hdl, queinfo);
}

kv_result kv_get_queue_stat(const kv_device_handle dev_hdl, const kv_queue_handle que_hdl, kv_queue_stat *que_st) {
    return kv_device_internal::kv_get_queue_stat(dev_hdl, que_hdl, que_st);
}

// namespace APIs
kv_result kv_create_namespace(kv_device_handle dev_hdl, const kv_namespace *ns, kv_namespace_handle ns_hdl) {
    return kv_device_internal::kv_create_namespace(dev_hdl, ns, ns_hdl);
}

kv_result kv_delete_namespace(kv_device_handle dev_hdl, kv_namespace_handle ns_hdl) {
    return kv_device_internal::kv_delete_namespace(dev_hdl, ns_hdl);
}

kv_result kv_attach_namespace(kv_device_handle dev_hdl, kv_namespace_handle ns_hdl) {
    return kv_device_internal::kv_attach_namespace(dev_hdl, ns_hdl);
}

kv_result kv_detach_namespace(kv_device_handle dev_hdl, kv_namespace_handle ns_hdl) {
    return kv_device_internal::kv_detach_namespace(dev_hdl, ns_hdl);
}

kv_result kv_list_namespaces(const kv_device_handle dev_hdl, kv_namespace_handle *ns_hdls, uint32_t *ns_cnt) {
    return kv_device_internal::kv_list_namespaces(dev_hdl, ns_hdls, ns_cnt);
}

kv_result kv_get_namespace_info(const kv_device_handle dev_hdl, const kv_namespace_handle ns_hdl, kv_namespace *nsinfo) {
    return kv_device_internal::kv_get_namespace_info(dev_hdl, ns_hdl, nsinfo);
}

kv_result kv_get_namespace_stat(const kv_device_handle dev_hdl, const kv_namespace_handle ns_hdl, kv_namespace_stat *ns_st) {
    return kv_device_internal::kv_get_namespace_stat(dev_hdl, ns_hdl, ns_st);
}

// internal API, added for an emulator
kv_result _kv_bypass_namespace(const kv_device_handle dev_hdl, const kv_namespace_handle ns_hdl, bool_t bypass) {
    return kv_device_internal::_kv_bypass_namespace(dev_hdl, ns_hdl, bypass);
}

// IO APIs
kv_result kv_purge(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, uint8_t ks_id, kv_purge_option option, kv_postprocess_function *post_fn) {

    if (que_hdl == NULL || ns_hdl == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    kv_device_internal *dev = (kv_device_internal *) que_hdl->dev;
    return (dev->kv_purge(que_hdl, ns_hdl, ks_id, option, post_fn));
}

kv_result kv_open_iterator(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, uint8_t ks_id, const kv_iterator_option it_op, const kv_group_condition *it_cond, kv_postprocess_function *post_fn) {

    if (que_hdl == NULL || ns_hdl == NULL || it_cond == NULL) {
        return KV_ERR_PARAM_INVALID;
    }
    // Change to big end When current cpu is little end
    kv_group_condition it_cond_new;
    it_cond_new.bitmask = htobe32(it_cond->bitmask); 
    it_cond_new.bit_pattern = htobe32(it_cond->bit_pattern);

    kv_device_internal *dev = (kv_device_internal *) que_hdl->dev;
    return (dev->kv_open_iterator(que_hdl, ns_hdl, ks_id, it_op, &it_cond_new, post_fn));
}

kv_result kv_close_iterator(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, kv_iterator_handle iter_hdl, kv_postprocess_function *post_fn) {
    if (que_hdl == NULL || ns_hdl == NULL || iter_hdl == 0) {
        return KV_ERR_PARAM_INVALID;
    }

    kv_device_internal *dev = (kv_device_internal *) que_hdl->dev;
    return (dev->kv_close_iterator(que_hdl, ns_hdl, post_fn, iter_hdl));

}

kv_result kv_iterator_next(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, kv_iterator_handle iter_hdl, kv_iterator_list *iter_list, kv_postprocess_function *post_fn) {

    if (que_hdl == NULL || ns_hdl == NULL || iter_hdl == 0 || iter_list == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    kv_device_internal *dev = (kv_device_internal *) que_hdl->dev;
    return (dev->kv_iterator_next_set(que_hdl, ns_hdl, iter_hdl, post_fn, iter_list));
}

kv_result kv_list_iterators(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, kv_iterator *kv_iters, uint32_t *iter_cnt, kv_postprocess_function  *post_fn) {
    if (que_hdl == NULL || ns_hdl == NULL || kv_iters == NULL || iter_cnt == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    kv_device_internal *dev = (kv_device_internal *) que_hdl->dev;
    return (dev->kv_list_iterators(que_hdl, ns_hdl, post_fn, kv_iters, iter_cnt));
}

kv_result kv_delete(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, uint8_t ks_id, const kv_key *key, kv_delete_option option, kv_postprocess_function *post_fn) {

    if (que_hdl == NULL || ns_hdl == NULL || key == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    kv_device_internal *dev = (kv_device_internal *) que_hdl->dev;
    return (dev->kv_delete(que_hdl, ns_hdl, ks_id, key, option, post_fn));
}

kv_result kv_delete_group(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, uint8_t ks_id, kv_group_condition *grp_cond, kv_postprocess_function *post_fn) {

    if (que_hdl == NULL || ns_hdl == NULL || grp_cond == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    kv_device_internal *dev = (kv_device_internal *) que_hdl->dev;
    return (dev->kv_delete_group(que_hdl, ns_hdl, ks_id, grp_cond, post_fn));

}

kv_result kv_exist(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, uint8_t ks_id, const kv_key *key, uint32_t key_cnt, uint32_t buffer_size, uint8_t *buffer, kv_postprocess_function *post_fn) {

    if (que_hdl == NULL || ns_hdl == NULL || key == NULL || buffer == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    kv_device_internal *dev = (kv_device_internal *) que_hdl->dev;
    return (dev->kv_exist(que_hdl, ns_hdl, ks_id, key, key_cnt, post_fn, buffer_size, buffer));
}

kv_result kv_retrieve(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, uint8_t ks_id, const kv_key *key, kv_retrieve_option option, kv_value *value, const kv_postprocess_function *post_fn) {
    if (que_hdl == NULL || ns_hdl == NULL || key == NULL || value == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    kv_device_internal *dev = (kv_device_internal *) que_hdl->dev;
    return (dev->kv_retrieve(que_hdl, ns_hdl, ks_id, key, option, post_fn, value));

}

kv_result kv_store(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl,
  uint8_t ks_id, const kv_key *key, const kv_value *value, kv_store_option option,
  const kv_postprocess_function *post_fn) {
    if (que_hdl == NULL || ns_hdl == NULL || key == NULL || value == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    kv_device_internal *dev = (kv_device_internal *) que_hdl->dev;
    return (dev->kv_store(que_hdl, ns_hdl, ks_id, key, value, option, post_fn));
}

kv_result kv_poll_completion(kv_queue_handle que_hdl, uint32_t timeout_usec, uint32_t *num_events) {
    if (que_hdl == NULL || num_events == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    kv_device_internal *dev = (kv_device_internal *) que_hdl->dev;
    return (dev->kv_poll_completion(que_hdl, timeout_usec, num_events));
}

kv_result kv_set_interrupt_handler(kv_queue_handle que_hdl, const kv_interrupt_handler int_hdl) {
    if (que_hdl == NULL || int_hdl == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    kv_device_internal *dev = (kv_device_internal *) que_hdl->dev;
    return (dev->kv_set_interrupt_handler(que_hdl, int_hdl));
}


kv_result get_namespace_default(kv_device_handle dev_hdl, kv_namespace_handle *ns_hdl) {
    if (dev_hdl == NULL  || ns_hdl == NULL) {
        return KV_ERR_PARAM_INVALID;
    }
    kv_device_internal *dev = (kv_device_internal *) dev_hdl->dev;
    if (dev == NULL) {
        return KV_ERR_DEV_NOT_EXIST;
    }

    kv_namespace_internal *ns = dev->get_namespace_default();
    if (ns == NULL) {
        return KV_ERR_NS_INVALID;
    }

    (*ns_hdl) = new _kv_namespace_handle();
    (*ns_hdl)->dev = dev;
    (*ns_hdl)->ns = ns;
    (*ns_hdl)->nsid = ns->get_nsid();

    return KV_SUCCESS;
}


uint32_t get_queued_commands_count(kv_queue_handle que_hdl) {
    if (que_hdl != NULL) {
        ioqueue *que = (ioqueue *) que_hdl->queue;
        if (que != NULL) {
            return que->size();
        }
    }

    return 0;
}


// } // end of namespace
