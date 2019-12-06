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

#include <future>
#include <thread>
#include <string>
#include <algorithm>

#include "kvs_adi.h"
#include "kvs_adi_internal.h"

#include "queue.hpp"
#include "kv_device.hpp"
#include "kvs_utils.h"

namespace kvadi {
// default location for device configuration
const std::string kv_device_internal::configfile = "./kvssd_emul.conf";


std::recursive_mutex kv_device_internal::s_mutex;
uint32_t kv_device_internal::s_next_devid = 1;
std::unordered_map<uint32_t, kv_device_internal *> kv_device_internal::s_global_device_list;

kv_result kv_device_internal::get_init_status() {
    return m_init_status;
}
void kv_device_internal::set_init_status(kv_result result) {
    m_init_status = result;
}

uint64_t kv_device_internal::get_capacity() {
    return m_capacity;
}

uint64_t kv_device_internal::get_available() {
    return get_namespace(KV_NAMESPACE_DEFAULT)->get_available();
}

kv_config*& kv_device_internal::get_config() {
    return m_config;
}

bool_t kv_device_internal::is_polling() {
    return m_is_poll;
}

kv_raw_dev_type kv_device_internal::get_dev_type() {
    return m_dev_type;
}

// private constructor
// options must be valid for emulator
// please note kv_device_internal is just a container, the real kvstore
// operations are carried out by kv_namespace, which contains kvstore objects
kv_device_internal::kv_device_internal(kv_device_init_t *options) {
    static const uint64_t GB = 1024 * 1024 * 1024UL;
    m_dev_type = KV_DEV_TYPE_EMULATOR;

    // from caller's init option
    m_is_poll = options->is_polling;

    m_capacity = 7 * GB;
    m_has_fixed_keylen = TRUE;

    // load configuration
    // these configurations are only for emulator
    m_need_persisency = options->need_persistency;
    m_config = NULL;
    if (m_dev_type == KV_DEV_TYPE_EMULATOR) {
        m_config = new kv_config(options->configfile);
        std::string cap_str = m_config->getkv("general", "capacity");

        // for emulator, default is unlimited unless specified from configuration file
        // but get_capacity return 0 seems to be an issue, so change default capacity to 512GB
        m_capacity = 512 * GB;

        if (!cap_str.empty()) {
            std::size_t pos = 0;
            m_capacity = std::stoull(cap_str, &pos, 10);
            // WRITE_INFO("got capacity %llu\n", m_capacity);

            std::string unit_str = cap_str.substr(pos);
            std::transform(unit_str.begin(), unit_str.end(), unit_str.begin(), ::tolower);

            // in case of invalid configuration, default 4GB
            if (m_capacity <= 0) {
                m_capacity = 4 * GB;
            }

            if (unit_str.find("gb") != std::string::npos) {
                m_capacity *= GB;
            } else if (unit_str.find("mb") != std::string::npos) {
                m_capacity *= 1024 * 1024;
            } else {
                m_capacity *= 1024 * 1024;
                WRITE_WARN("capacity has no units specified, use MB\n");
            }
        }
        // printf("configured capacity %s %llu\n", cap_str.c_str(), m_capacity);

        // check polling mode setting
        // default is true for polling
        m_is_poll = options->is_polling;
        std::string poll = m_config->getkv("general", "polling");
        if (!strcasecmp(poll.c_str(), "false")) {
            m_is_poll = FALSE;
        } else if (!strcasecmp(poll.c_str(), "true")) {
            m_is_poll = TRUE;
        }

        // check fixed key len
        m_has_fixed_keylen = TRUE;
        std::string fixed_keylen_str = m_config->getkv("general", "keylen_fixed");
        if (!strcasecmp(fixed_keylen_str.c_str(), "false")) {
            m_has_fixed_keylen = FALSE;
        }


        // check use_iops_model
        m_use_iops_model = TRUE;
        std::string use_iops_model_str = m_config->getkv("general", "use_iops_model");
        if (!strcasecmp(use_iops_model_str.c_str(), "false")) {
            m_use_iops_model = FALSE;
        }
    }
    // XXX TODO how to get capacity or other parameters from a physical device??
    // such as m_has_fixed_keylen, which is used by iterator

    m_devpath = options->devpath;

    m_next_qid = 1;
    m_devid = kv_device_internal::get_next_devid();

    // only support 1, recast back to correct type
    m_nsid = (uint32_t) KV_NAMESPACE_DEFAULT;
    
    // fill in default values
    m_device_info.version = 1;
    m_device_type = KV_DEVICE_TYPE_KV;

    // restrictions and vendor settings
    // device info
    m_device_info.max_namespaces = 1;
    m_device_info.max_queues = 64*1024 - 1;
    // defined in kvs_adi_internal.h
    // 64
    m_device_info.min_value_len = (kv_value_t) SAMSUNG_KV_MIN_VALUE_LEN;
    // 2M
    m_device_info.max_value_len = (kv_value_t) SAMSUNG_KV_MAX_VALUE_LEN;
    // 16
    m_device_info.min_key_len = (kv_key_t) SAMSUNG_KV_MIN_KEY_LEN;
    // 255
    m_device_info.max_key_len = (kv_key_t) SAMSUNG_KV_MAX_KEY_LEN;

    m_device_info.capacity = m_capacity;

    // fill in vendor info
    m_device_info.extended_info = (kv_samsung_device *) malloc(sizeof(kv_samsung_device));
    kv_samsung_device *vendor_info = (kv_samsung_device *) m_device_info.extended_info;
    // for best internal real device performance
    vendor_info->key_unit = 255;
    vendor_info->value_unit = 32;

    // device stat init
    m_device_stat.namespace_count = 1;
    m_device_stat.queue_count = 2;
    m_device_stat.utilization = 0;
    m_device_stat.waf = 0;
    m_device_stat.extended_info = NULL;

    // IO queues needs creation separately by user
    // device should have default admin SQ and CQ setup
    //

    // record current timepoint
    m_start_timepoint = std::chrono::system_clock::now();

    m_initialized = TRUE;
}

// deconstructor
kv_device_internal::~kv_device_internal() {
    if (m_device_info.extended_info != NULL) {
        free(m_device_info.extended_info);
    }
    if (m_device_stat.extended_info != NULL) {
        free(m_device_stat.extended_info);
    }

    // shutdown all queues
    shutdown_all_queues();

    // delete default namespace
    auto it = m_ns_list.find(KV_NAMESPACE_DEFAULT);
    kv_namespace_internal *ns = it->second;
    delete ns;

    if (m_config != NULL) {
        delete m_config;
    }
}

bool_t kv_device_internal::is_initialized() {
    return m_initialized;
}

// static singleton API
kv_result kv_device_internal::kv_initialize_device(kv_device_init_t *options, kv_device_handle *dev_hdl) {
    // thread safety
    std::lock_guard<std::recursive_mutex> lock(s_mutex);

    if (dev_hdl == NULL) {
        return KV_ERR_PARAM_INVALID;
    } else {
        if (*dev_hdl != NULL) {
            kv_device_internal *dev = (kv_device_internal *) (*dev_hdl)->dev;
            // if the caller passes in an invalid handle, it will crash here
            if (dev != NULL && dev->is_initialized()) {
                return KV_ERR_DEV_INITIALIZED;
            }
        }
    }

    (*dev_hdl) = new _kv_device_handle;
    // create a new device object
    kv_device_internal *dev = new kv_device_internal(options);
    if (dev == NULL) {
        return KV_ERR_DEV_INIT;
    } else {
        (*dev_hdl)->dev = (void *) dev;
        (*dev_hdl)->devid = dev->get_devid();

        // save the newly initialized dev into global list
        s_global_device_list.insert(std::make_pair((*dev_hdl)->devid, dev));

        // create default namespace
        // assign all capacity for the default namespace
        // as only 1 namespace is supported currently
        kv_namespace nsinfo;
        nsinfo.capacity = dev->get_capacity();
        nsinfo.nsid = KV_NAMESPACE_DEFAULT;
        nsinfo.attached = TRUE;

        kv_namespace_internal *ns = new kv_namespace_internal(dev, (uint32_t) KV_NAMESPACE_DEFAULT, &nsinfo);
        if (ns == NULL) {
            dev->set_init_status(KV_ERR_DEV_INIT);
            return KV_ERR_DEV_INIT;
        }

        dev->insert_namespace((uint32_t) KV_NAMESPACE_DEFAULT, ns);
        kv_result status = ns->get_init_status();
        dev->set_init_status(status);

        nsinfo.extended_info = NULL;

        return status;
    }
}

bool_t kv_device_internal::insert_namespace(uint32_t nsid, kv_namespace_internal *ns) {
    m_ns_list.insert(std::make_pair(nsid, ns));
    return TRUE;
}

uint32_t kv_device_internal::get_devid() {
    return m_devid;
}

kv_result kv_device_internal::get_io_queue_info(uint16_t qid, kv_queue *queue_info) {
    if (queue_info == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    std::unordered_map<uint16_t, ioqueue *>::const_iterator it = m_ioque_list.find(qid);
    if (it != m_ioque_list.end()) {
        return (it->second->kv_get_queue_info(queue_info));
    } else {
        return KV_ERR_QUEUE_QID_INVALID;
    }
}

// get a set of submission queue ids from completion queue id
std::set<uint16_t> kv_device_internal::get_paired_SQ_ids(uint16_t CQ_id) {

    auto it = m_sq_to_cq_pairs.begin();
    auto ite = m_sq_to_cq_pairs.end();

    std::set<uint16_t> s;
    for (; it != ite; it++) {
        if (it->second == CQ_id) {
            s.insert(it->first);
        }
    }

    return s;
}

// check if a qid exist in a queue list
static bool_t qid_exist(uint16_t qid, std::unordered_map<uint16_t, ioqueue *>& qlist) {
    std::unordered_map<uint16_t, ioqueue *>::iterator ite = qlist.end();
    std::unordered_map<uint16_t, ioqueue *>::iterator it =  qlist.find(qid);
    if (it == ite) {
        return FALSE;
    } else {
        return TRUE;
    }
}

kv_result kv_device_internal::create_queue(const kv_queue *queinfo, kv_queue_handle *que_hdl) {
    // thread safety
    std::lock_guard<std::mutex> lock(m_mutex);

    if (queinfo == NULL || que_hdl == NULL) {
        WRITE_WARN("parameter can't be NULL.\n");
        return KV_ERR_PARAM_INVALID;
    }

    // max queue count is 64k
    if (m_ioque_list.size() == 64*1024 - 1) {
        WRITE_WARN("Max queue count reached.\n");
        return KV_ERR_QUEUE_MAX_QUEUE;
    }

    // also check max queue size
    // emul_ioqueue::MAX_QUEUE_SIZE
    if (queinfo->queue_size > 64*1024 - 1) {
        WRITE_WARN("queue size too big.\n");
        return KV_ERR_QUEUE_QSIZE_INVALID;
    }

    // for both type, new qid shouldn't exist
    // use supplied que id
    uint16_t qid = queinfo->queue_id;
    if (qid_exist(qid, m_ioque_list)) {
        if (queinfo->queue_type == SUBMISSION_Q_TYPE) {
            WRITE_WARN("submission queue id %d already exists\n", qid);
            return KV_ERR_QUEUE_QID_INVALID;
        } else {
            WRITE_WARN("completion queue id %d already exists\n", qid);
            return KV_ERR_QUEUE_CQID_INVALID;
        }
    }

    ioqueue *out_q = 0;
    // check if CQ already exists
    if (queinfo->queue_type == SUBMISSION_Q_TYPE) {
        out_q = get_ioqueue(queinfo->completion_queue_id);
        if (out_q == 0) {
            WRITE_WARN("completion queue id %d doesn't exist\n", queinfo->completion_queue_id);
            return KV_ERR_QUEUE_CQID_INVALID;
        }
    }

    ioqueue *que = 0;
    if (get_dev_type() == KV_DEV_TYPE_LINUX_KERNEL) {
        que = new kernel_ioqueue(queinfo, this);
    }
    else {
        que = new emul_ioqueue(queinfo, this, (emul_ioqueue*) out_q);
    }

    m_device_stat.queue_count++;

    // save to device queue list
    m_ioque_list.insert(std::make_pair(qid, que));

    // update sq to cq pairs, if this is a pair
    if (queinfo->queue_type == SUBMISSION_Q_TYPE) {
        m_sq_to_cq_pairs.insert(std::make_pair(qid, queinfo->completion_queue_id));
    }

    (*que_hdl) = new _kv_queue_handle();

    // fill in queue handle info
    (*que_hdl)->qid = qid;
    (*que_hdl)->dev = (void *)this;
    (*que_hdl)->queue = (void *)que;



    return KV_SUCCESS;
}

kv_device_internal *kv_device_internal::get_device_instance(kv_device_handle dev_hdl) {
    if (dev_hdl == NULL) {
        return NULL;
    }
    return (kv_device_internal *) dev_hdl->dev;
}

kv_device_internal *kv_device_internal::get_device_instance(uint32_t devid) {
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    
    std::unordered_map<uint32_t, kv_device_internal *>::iterator it = kv_device_internal::s_global_device_list.find(devid);
    if (it != s_global_device_list.end()) {
        return it->second;
    } else {
        return NULL;
    }
}

// how to make sure this is thread safe
kv_result kv_device_internal::kv_cleanup_device(kv_device_handle dev_hdl) {
    std::lock_guard<std::recursive_mutex> lock(s_mutex);

    kv_device_internal *dev = (kv_device_internal *) dev_hdl->dev;

    // remove from handle
    s_global_device_list.erase(dev_hdl->devid);

    if (dev != NULL) {
        delete dev;
    }

    delete dev_hdl;

    return KV_SUCCESS;
}

void kv_device_internal::shutdown_all_queues() {
    // thread safety for queue operation
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& it : m_ioque_list) {
        ioqueue *que = it.second;
        que->terminate();
        delete que;
    }
}

kv_device kv_device_internal::get_devinfo() {
    return m_device_info;
}

kv_device_stat kv_device_internal::get_dev_stat() {
    return m_device_stat;
}

kv_result kv_device_internal::kv_get_device_info(const kv_device_handle dev_hdl, kv_device *devinfo) {
    if (devinfo == NULL) {
        return KV_ERR_PARAM_INVALID;
    }
    kv_device_internal *dev = (kv_device_internal *) dev_hdl->dev;  
    if (dev == NULL) {
        return KV_ERR_DEV_NOT_EXIST;
    }

    dev->update_capacity_consumed();
    *devinfo = dev->get_devinfo();
    return KV_SUCCESS;
}

kv_result kv_device_internal::kv_get_device_stat(const kv_device_handle dev_hdl, kv_device_stat *devstat) {
    if (devstat == NULL) {
        return KV_ERR_PARAM_INVALID;
    }
    kv_device_internal *dev = (kv_device_internal *) dev_hdl->dev;  
    if (dev == NULL) {
        return KV_ERR_DEV_NOT_EXIST;
    }

    dev->update_capacity_consumed();
    *devstat = dev->get_dev_stat();
    return KV_SUCCESS;
}

ioqueue *kv_device_internal::get_ioqueue(uint16_t qid) {
    std::unordered_map<uint16_t, ioqueue *>::const_iterator it = m_ioque_list.find(qid);
    if (it != m_ioque_list.end()) {
        return it->second;
    } else {
        return NULL;
    }
}

kv_namespace_internal *kv_device_internal::get_namespace_default() {
    return get_namespace(KV_NAMESPACE_DEFAULT);
}

kv_namespace_internal *kv_device_internal::get_namespace(const uint32_t nsid) {
    std::unordered_map<int32_t, kv_namespace_internal *>::iterator it = m_ns_list.find(nsid);
    if (it != m_ns_list.end()) {
        return it->second;
    } else {
        return NULL;
    }
}

// async IO
// static function
kv_result kv_device_internal::kv_sanitize(kv_queue_handle que_hdl, kv_device_handle dev_hdl, kv_sanitize_option option, kv_sanitize_pattern *pattern, kv_postprocess_function *post_fn) {

    if (option != KV_SANITIZE_OPT_KV_ERASE && option != KV_SANITIZE_OPT_CRYPTO_ERASE && option != KV_SANITIZE_OPT_OVERWRITE) {
        return KV_ERR_OPTION_INVALID;
    }

    if (que_hdl == NULL || dev_hdl == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    kv_device_internal *dev = (kv_device_internal *) dev_hdl->dev;  
    if (dev == NULL) {
        return KV_ERR_DEV_NOT_EXIST;
    }

    ioqueue *que = (ioqueue *)(que_hdl->queue);
    if (que == NULL) {
        return KV_ERR_QUEUE_QID_INVALID;
    }

    // only handle default one for now, otherwise it would have been to
    // sanitize all namespaces
    // don't do much other than delete all keys
    kv_namespace_internal *ns = dev->get_namespace(KV_NAMESPACE_DEFAULT);
    if (ns == NULL) {
        return KV_ERR_VENDOR;
    }

    op_sanitize_struct_t info;
    info.pattern = pattern;
    info.option = option;

    io_cmd *cmd = new io_cmd(dev, ns, que_hdl);

    cmd->ioctx.timeout_usec = 0;
    if (post_fn) {
        cmd->ioctx.post_fn = post_fn->post_fn;
        cmd->ioctx.private_data = post_fn->private_data;
    } else {
        cmd->ioctx.post_fn = NULL;
    }
    cmd->ioctx.opcode = KV_OPC_SANITIZE_DEVICE;
    cmd->ioctx.command.sanitize_info = info;
    cmd->ioctx.key = NULL;
    cmd->ioctx.value = NULL;

    // pass ioctx by value, as there is no ownership of those memories in the
    // structure
    // XXX only sanitize default one

    return dev->submit_io(que_hdl, cmd);
}

kv_result kv_device_internal::remove_queue(uint16_t qid) {
    // thread safety for queue operation
    std::lock_guard<std::mutex> lock(m_mutex);

    ioqueue *que = get_ioqueue(qid);
    if (que == NULL) {
        WRITE_WARN("Queue id %d invalid", qid);
        return KV_ERR_QUEUE_QID_INVALID;
    }

    uint16_t qtype = que->get_type();

    // only allow removal if there are no submission queues
    // still pointing to the CQ
    if (qtype == COMPLETION_Q_TYPE) {

        // find all submission queues
        std::set<uint16_t> sqids = get_paired_SQ_ids(qid);
        std::set<uint16_t>::iterator it = sqids.begin();
        std::set<uint16_t>::iterator ite = sqids.end();

        // if any sq is still there, fail.
        for(; it != ite; it++) {
            if (m_ioque_list.find(*it) != m_ioque_list.end()) {
                WRITE_WARN("can't delete a completion queue before all associated submission queues have been deleted\n");
                return (KV_ERR_QUEUE_DELETION_INVALID);
            }
        }

    }

    // now ready to remove
    // abort any IOs left, shutdown the queue
    m_ioque_list.erase(qid);
    m_sq_to_cq_pairs.erase(qid);
    que->terminate();
    delete que;
    m_device_stat.queue_count--;

    return KV_SUCCESS;
}

// static function
kv_result kv_device_internal::kv_delete_queue(kv_device_handle dev_hdl, kv_queue_handle que_hdl) {
    if (dev_hdl == NULL || que_hdl == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    kv_device_internal *dev = (kv_device_internal *) dev_hdl->dev;  
    if (dev == NULL) {
        return KV_ERR_DEV_NOT_EXIST;
    }

    auto ret = dev->remove_queue(que_hdl->qid);
    delete que_hdl;

    return ret;
}

// static function
kv_result kv_device_internal::kv_create_queue(kv_device_handle dev_hdl, const kv_queue *queinfo, kv_queue_handle *que_hdl) {

    if (dev_hdl == NULL || queinfo == NULL || que_hdl == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    kv_device_internal *dev = (kv_device_internal *) dev_hdl->dev;  
    if (dev == NULL) {
        // WRITE_WARN("dev not exit, inside kv_device_internal::create_queue\n");
        return KV_ERR_DEV_NOT_EXIST;
    }

    return dev->create_queue(queinfo, que_hdl);
}

// static funcion
kv_result kv_device_internal::kv_get_queue_handles(const kv_device_handle dev_hdl, kv_queue_handle *que_hdls, uint16_t *que_cnt) {
    if (dev_hdl == NULL || que_hdls == NULL || que_cnt == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    kv_device_internal *dev = (kv_device_internal *) dev_hdl->dev;  
    if (dev == NULL) {
        return KV_ERR_DEV_NOT_EXIST;
    }
    return dev->kv_get_queue_handles(que_hdls, que_cnt);
}

// non static function
kv_result kv_device_internal::kv_get_queue_handles(kv_queue_handle *que_hdls, uint16_t *que_cnt) {
    kv_result res = KV_SUCCESS;
    // not enough to hold all queue info
    if (m_ioque_list.size() > *que_cnt) {
        res = KV_WRN_MORE;
    }

    uint32_t i = 0;
    for (auto& it : m_ioque_list) {
        if (i == *que_cnt) {
            break;
        }
        uint16_t qid = it.first;
        que_hdls[i]->qid = qid;
        que_hdls[i]->dev = (void *) this;
        i++;
    }
    *que_cnt = i;
    return res;
}

kv_result kv_device_internal::kv_get_queue_info(const kv_device_handle dev_hdl, const kv_queue_handle que_hdl, kv_queue *queinfo) {
    if (queinfo == NULL || dev_hdl == NULL || que_hdl == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    kv_device_internal *dev = (kv_device_internal *) dev_hdl->dev;  
    if (dev == NULL) {
        return KV_ERR_DEV_NOT_EXIST;
    }

    ioqueue *que = (ioqueue *)(que_hdl->queue);
    if (que == NULL) {
        return KV_ERR_QUEUE_QID_INVALID;
    }

    return (que->kv_get_queue_info(queinfo));
}

kv_result kv_device_internal::kv_get_queue_stat(const kv_device_handle dev_hdl, const kv_queue_handle que_hdl, kv_queue_stat *que_stat) {
    if (que_stat == NULL || dev_hdl == NULL || que_hdl == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    kv_device_internal *dev = (kv_device_internal *) dev_hdl->dev;  
    if (dev == NULL) {
        return KV_ERR_DEV_NOT_EXIST;
    }

    ioqueue *que = (ioqueue *)(que_hdl->queue);
    if (que == NULL) {
        return KV_ERR_QUEUE_QID_INVALID;
    }

    return (que->kv_get_queue_stat(que_stat));
}

kv_result kv_device_internal::kv_create_namespace(kv_device_handle dev_hdl, const kv_namespace *ns, kv_namespace_handle ns_hdl) {
    (void) dev_hdl;
    (void) ns;
    (void) ns_hdl;
    return KV_ERR_DEV_MAX_NS;
}

kv_result kv_device_internal::kv_delete_namespace(kv_device_handle dev_hdl, kv_namespace_handle ns_hdl) {
    if (ns_hdl) {
        delete ns_hdl;
        return KV_SUCCESS;
    }
    return KV_ERR_NS_DEFAULT;
}

kv_result kv_device_internal::kv_attach_namespace(kv_device_handle dev_hdl, kv_namespace_handle ns_hdl) {
    (void) dev_hdl;
    (void) ns_hdl;
    return KV_ERR_NS_DEFAULT; 
}

kv_result kv_device_internal::kv_detach_namespace(kv_device_handle dev_hdl, kv_namespace_handle ns_hdl) {
    (void) dev_hdl;
    (void) ns_hdl;
    return KV_ERR_NS_DEFAULT; 
}

kv_result kv_device_internal::kv_list_namespaces(const kv_device_handle dev_hdl, kv_namespace_handle *ns_hdls, uint32_t *ns_cnt) {
    if (dev_hdl == NULL || ns_hdls == NULL || ns_cnt == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    kv_device_internal *dev = (kv_device_internal *) dev_hdl->dev;  
    if (dev == NULL) {
        return KV_ERR_DEV_NOT_EXIST;
    }

    // always return the default one
    *ns_cnt = 1;
    ns_hdls[0]->nsid = (uint32_t) KV_NAMESPACE_DEFAULT;
    ns_hdls[0]->dev = dev_hdl->dev;
    ns_hdls[0]->ns = (void *) dev->get_namespace(KV_NAMESPACE_DEFAULT);

    return KV_SUCCESS;
}


kv_result kv_device_internal::kv_get_namespace_info(const kv_device_handle dev_hdl, const kv_namespace_handle ns_hdl, kv_namespace *nsinfo) {
    if (dev_hdl == NULL || nsinfo == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    kv_device_internal *dev = (kv_device_internal *) dev_hdl->dev;  
    if (dev == NULL) {
        return KV_ERR_DEV_NOT_EXIST;
    }

    kv_namespace_internal *ns = (kv_namespace_internal *) ns_hdl->ns;
    if (ns == NULL) {
        return KV_ERR_NS_INVALID;
    }

    return (ns->kv_get_namespace_info(nsinfo));
}


kv_result kv_device_internal::_kv_bypass_namespace(const kv_device_handle dev_hdl, const kv_namespace_handle ns_hdl, bool_t bypass) {
    kv_namespace_internal *ns = (kv_namespace_internal *) ns_hdl->ns;
    if (ns == NULL) {
        return KV_ERR_NS_INVALID;
    }
    ns->swap_device(bypass);
    return KV_SUCCESS;
}

kv_result kv_device_internal::kv_get_namespace_stat(const kv_device_handle dev_hdl, const kv_namespace_handle ns_hdl, kv_namespace_stat *ns_stat) {
    if (dev_hdl == NULL || ns_stat == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    kv_device_internal *dev = (kv_device_internal *) dev_hdl->dev;  
    if (dev == NULL) {
        return KV_ERR_DEV_NOT_EXIST;
    }

    kv_namespace_internal *ns = (kv_namespace_internal *) ns_hdl->ns;
    if (ns == NULL) {
        return KV_ERR_NS_INVALID;
    }

    return (ns->kv_get_namespace_stat(ns_stat));
}

// more important IO APIs below
// operate on a device object, can access kv_device_internal members
// ASYNC IO in a device context
kv_result kv_device_internal::kv_purge(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, uint8_t ks_id, kv_purge_option option, kv_postprocess_function *post_fn) {

    if (que_hdl == NULL || ns_hdl == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    if (option != KV_PURGE_OPT_DEFAULT && option != KV_PURGE_OPT_KV_ERASE && option != KV_PURGE_OPT_CRYPTO_ERASE) {
        return KV_ERR_OPTION_INVALID;
    }

    if(ks_id < SAMSUNG_MIN_KEYSPACE_ID || ks_id >= SAMSUNG_MAX_KEYSPACE_CNT){
          return KV_ERR_KEYSPACE_INVALID;
    }

    kv_device_internal *dev = (kv_device_internal *) que_hdl->dev;
    if (dev == NULL) {
        return KV_ERR_DEV_NOT_EXIST;
    }

    kv_namespace_internal *ns = (kv_namespace_internal *) ns_hdl->ns;
    if (ns == NULL) {
        return KV_ERR_NS_INVALID;
    }

    ioqueue *que = (ioqueue *)(que_hdl->queue);
    if (que == NULL) {
        return KV_ERR_QUEUE_QID_INVALID;
    }

    op_purge_struct_t info; 
    info.option = option;

    io_cmd *cmd = new io_cmd(dev, ns, que_hdl);
    cmd->ioctx.timeout_usec = 0;
    if (post_fn) {
        cmd->ioctx.post_fn = post_fn->post_fn;
        cmd->ioctx.private_data = post_fn->private_data;
    } else {
        cmd->ioctx.post_fn = NULL;
    }
    cmd->ioctx.opcode = KV_OPC_PURGE;
    cmd->ioctx.command.purge_info = info;
    cmd->ioctx.key = NULL;
    cmd->ioctx.value = NULL;
    cmd->ioctx.ks_id = ks_id;

    // pass ioctx by value, as there is no ownership of those memories in the
    // structure

    return dev->submit_io(que_hdl, cmd);
}

// async IO
kv_result kv_device_internal::kv_open_iterator(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, uint8_t ks_id, const kv_iterator_option it_op, const kv_group_condition *it_cond, kv_postprocess_function *post_fn) {

    if (que_hdl == NULL || ns_hdl == NULL || it_cond == NULL) {
        return KV_ERR_PARAM_INVALID;
    }
    if (it_op != KV_ITERATOR_OPT_KEY && it_op != KV_ITERATOR_OPT_KV && it_op != KV_ITERATOR_OPT_KV_WITH_DELETE) {
        return KV_ERR_OPTION_INVALID;
    }
    if(ks_id < SAMSUNG_MIN_KEYSPACE_ID || ks_id >= SAMSUNG_MAX_KEYSPACE_CNT){
          return KV_ERR_KEYSPACE_INVALID;
    }

    kv_namespace_internal *ns = (kv_namespace_internal *) ns_hdl->ns;
    if (ns == NULL) {
        return KV_ERR_NS_INVALID;
    }

    kv_device_internal *dev = (kv_device_internal *) que_hdl->dev;
    if (dev == NULL) {
        return KV_ERR_DEV_NOT_EXIST;
    }

    ioqueue *que = (ioqueue *)(que_hdl->queue);
    if (que == NULL) {
        return KV_ERR_QUEUE_QID_INVALID;
    }

    io_cmd *cmd = new io_cmd(dev, ns, que_hdl);
    cmd->ioctx.timeout_usec = 0;
    cmd->ioctx.result.hiter = 0;
    if (post_fn) {
        cmd->ioctx.post_fn = post_fn->post_fn;
        cmd->ioctx.private_data = post_fn->private_data;
    } else {
        cmd->ioctx.post_fn = NULL;
    }
    cmd->ioctx.opcode = KV_OPC_OPEN_ITERATOR;
    cmd->ioctx.command.iterator_open_info.it_op = it_op;
    cmd->ioctx.command.iterator_open_info.it_cond.bitmask = it_cond->bitmask;
    cmd->ioctx.command.iterator_open_info.it_cond.bit_pattern = it_cond->bit_pattern;
    cmd->ioctx.key = NULL;
    cmd->ioctx.value = NULL;
    cmd->ioctx.ks_id = ks_id;
    return dev->submit_io(que_hdl, cmd);
}

kv_result kv_device_internal::kv_close_iterator(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, kv_postprocess_function *post_fn, kv_iterator_handle iter_hdl) {
    if (que_hdl == NULL || ns_hdl == NULL || iter_hdl == 0) {
        return KV_ERR_PARAM_INVALID;
    }

    kv_namespace_internal *ns = (kv_namespace_internal *) ns_hdl->ns;
    if (ns == NULL) {
        return KV_ERR_NS_INVALID;
    }

    kv_device_internal *dev = (kv_device_internal *) que_hdl->dev;
    if (dev == NULL) {
        return KV_ERR_DEV_NOT_EXIST;
    }

    ioqueue *que = (ioqueue *)(que_hdl->queue);
    if (que == NULL) {
        return KV_ERR_QUEUE_QID_INVALID;
    }

    op_close_iterator_struct_t info; 
    info.iter_hdl = iter_hdl;

    io_cmd *cmd = new io_cmd(dev, ns, que_hdl);
    cmd->ioctx.timeout_usec = 0;
    cmd->ioctx.result.hiter = iter_hdl;
    if (post_fn) {
        cmd->ioctx.post_fn = post_fn->post_fn;
        cmd->ioctx.private_data = post_fn->private_data;
    } else {
        cmd->ioctx.post_fn = NULL;
    }
    cmd->ioctx.opcode = KV_OPC_CLOSE_ITERATOR;
    cmd->ioctx.command.iterator_close_info = info;
    cmd->ioctx.key = NULL;
    cmd->ioctx.value = NULL;

    return dev->submit_io(que_hdl, cmd);

}

bool_t kv_device_internal::use_iops_model() {
    return m_use_iops_model;
}

bool_t kv_device_internal::is_keylen_fixed() {
    return m_has_fixed_keylen;
}

// async IO
kv_result kv_device_internal::kv_iterator_next_set(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, kv_iterator_handle iter_hdl, kv_postprocess_function *post_fn, kv_iterator_list *iter_list) {
    if (que_hdl == NULL || ns_hdl == NULL || iter_hdl == 0) {
        return KV_ERR_PARAM_INVALID;
    }

    ioqueue *queue = (ioqueue *)(que_hdl->queue);
    if (queue == NULL) {
        return KV_ERR_QUEUE_QID_INVALID;
    }

    kv_device_internal *dev = (kv_device_internal *) que_hdl->dev;
    if (dev == NULL) {
        return KV_ERR_DEV_NOT_EXIST;
    }

    kv_namespace_internal *ns = (kv_namespace_internal *) ns_hdl->ns;
    if (ns == NULL) {
        return KV_ERR_NS_INVALID;
    }

    op_iterator_next_struct_t info;
    info.iter_list = iter_list;
    info.iter_hdl = iter_hdl;

    io_cmd *cmd = new io_cmd(dev, ns, que_hdl);
    cmd->ioctx.timeout_usec = 0;
    cmd->ioctx.result.hiter = iter_hdl;
    if (post_fn) {
        cmd->ioctx.post_fn = post_fn->post_fn;
        cmd->ioctx.private_data = post_fn->private_data;
    } else {
        cmd->ioctx.post_fn = NULL;
    }
    cmd->ioctx.opcode = KV_OPC_ITERATE_NEXT;
    cmd->ioctx.command.iterator_next_info = info;
    cmd->ioctx.key = NULL;
    cmd->ioctx.value = NULL;


    return dev->submit_io(que_hdl, cmd);
}

// async IO
kv_result kv_device_internal::kv_iterator_next(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, kv_iterator_handle iter_hdl, kv_postprocess_function *post_fn, kv_key *key, kv_value *value) {
    if (que_hdl == NULL || ns_hdl == NULL || iter_hdl == 0) {
        return KV_ERR_PARAM_INVALID;
    }

    ioqueue *queue = (ioqueue *)(que_hdl->queue);
    if (queue == NULL) {
        return KV_ERR_QUEUE_QID_INVALID;
    }

    kv_device_internal *dev = (kv_device_internal *) que_hdl->dev;
    if (dev == NULL) {
        return KV_ERR_DEV_NOT_EXIST;
    }

    kv_namespace_internal *ns = (kv_namespace_internal *) ns_hdl->ns;
    if (ns == NULL) {
        return KV_ERR_NS_INVALID;
    }

    op_iterator_next_struct_t info;
    info.key = key;
    info.value = value;
    info.iter_hdl = iter_hdl;

    io_cmd *cmd = new io_cmd(dev, ns, que_hdl);
    cmd->ioctx.timeout_usec = 0;
    cmd->ioctx.result.hiter = iter_hdl;
    if (post_fn) {
        cmd->ioctx.post_fn = post_fn->post_fn;
        cmd->ioctx.private_data = post_fn->private_data;
    } else {
        cmd->ioctx.post_fn = NULL;
    }
    cmd->ioctx.opcode = KV_OPC_ITERATE_NEXT_SINGLE_KV;
    cmd->ioctx.command.iterator_next_info = info;
    cmd->ioctx.key = NULL;
    cmd->ioctx.value = NULL;


    return dev->submit_io(que_hdl, cmd);
}

// this is sync call
kv_result kv_device_internal::kv_list_iterators(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, kv_postprocess_function  *post_fn, kv_iterator *kv_iters, uint32_t *iter_cnt) {
    if (que_hdl == NULL || ns_hdl == NULL || kv_iters == NULL || iter_cnt == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    ioqueue *queue = (ioqueue *)(que_hdl->queue);
    if (queue == NULL) {
        return KV_ERR_QUEUE_QID_INVALID;
    }

    kv_device_internal *dev = (kv_device_internal *) que_hdl->dev;
    if (dev == NULL) {
        return KV_ERR_DEV_NOT_EXIST;
    }

    kv_namespace_internal *ns = (kv_namespace_internal *) ns_hdl->ns;
    if (ns == NULL) {
        return KV_ERR_NS_INVALID;
    }

    op_list_iterator_struct_t info; 
    info.kv_iters = kv_iters;
    info.iter_cnt = iter_cnt;

    io_cmd *cmd = new io_cmd(dev, ns, que_hdl);
    cmd->ioctx.timeout_usec = 0;
    cmd->ioctx.result.hiter = 0;
    if (post_fn) {
        cmd->ioctx.post_fn = post_fn->post_fn;
        cmd->ioctx.private_data = post_fn->private_data;
    } else {
        cmd->ioctx.post_fn = NULL;
    }
    cmd->ioctx.opcode = KV_OPC_LIST_ITERATOR;
    cmd->ioctx.command.iterator_list_info = info;
    cmd->ioctx.key = NULL;
    cmd->ioctx.value = NULL;

    // since this is sync admin call, directly call and return result
    return ns->kv_list_iterators(kv_iters, iter_cnt, cmd);
}

// async IO
kv_result kv_device_internal::kv_delete(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, uint8_t ks_id, const kv_key *key, kv_delete_option option, kv_postprocess_function *post_fn) {
    if (que_hdl == NULL || ns_hdl == NULL || key == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    kv_result res = validate_key_value(key, NULL);
    if (res != KV_SUCCESS) {
        return res;
    }
 
    if(ks_id < SAMSUNG_MIN_KEYSPACE_ID || ks_id >= SAMSUNG_MAX_KEYSPACE_CNT){
          return KV_ERR_KEYSPACE_INVALID;
    }

    /*
    if (option != KV_DELETE_OPT_DEFAULT) {
        return KV_ERR_OPTION_INVALID;
    }
    */

    kv_device_internal *dev = (kv_device_internal *) que_hdl->dev;
    if (dev == NULL) {
        return KV_ERR_DEV_NOT_EXIST;
    }

    emul_ioqueue *que = (emul_ioqueue *)(que_hdl->queue);
    if (que == NULL) {
        return KV_ERR_QUEUE_QID_INVALID;
    }

    kv_namespace_internal *ns = (kv_namespace_internal *) ns_hdl->ns;
    if (ns == NULL) {
        return KV_ERR_NS_INVALID;
    }

    op_delete_struct_t info; 
    info.option = option;

    io_cmd *cmd = new io_cmd(dev, ns, que_hdl);
    cmd->ioctx.key = key;
    cmd->ioctx.value = NULL;
    cmd->ioctx.timeout_usec = 0;
    if (post_fn) {
        cmd->ioctx.post_fn = post_fn->post_fn;
        cmd->ioctx.private_data = post_fn->private_data;
    } else {
        cmd->ioctx.post_fn = NULL;
    }
    cmd->ioctx.opcode = KV_OPC_DELETE;
    cmd->ioctx.command.delete_info = info;
    cmd->ioctx.ks_id = ks_id;


    return dev->submit_io(que_hdl, cmd);
}

kv_result kv_device_internal::kv_delete_group(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, uint8_t ks_id, kv_group_condition *grp_cond, kv_postprocess_function *post_fn) {
    if (que_hdl == NULL || ns_hdl == NULL || grp_cond == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    kv_device_internal *dev = (kv_device_internal *) que_hdl->dev;
    if (dev == NULL) {
        return KV_ERR_DEV_NOT_EXIST;
    }

    if(ks_id <SAMSUNG_MIN_KEYSPACE_ID || ks_id >= SAMSUNG_MAX_KEYSPACE_CNT){
          return KV_ERR_KEYSPACE_INVALID;
    }

    emul_ioqueue *que = (emul_ioqueue *)(que_hdl->queue);
    if (que == NULL) {
        return KV_ERR_QUEUE_QID_INVALID;
    }

    kv_namespace_internal *ns = (kv_namespace_internal *) ns_hdl->ns;
    if (ns == NULL) {
        return KV_ERR_NS_INVALID;
    }

    op_delete_group_struct_t info; 
    info.grp_cond = grp_cond;

    io_cmd *cmd = new io_cmd(dev, ns, que_hdl);
    cmd->ioctx.key = NULL;
    cmd->ioctx.value = NULL;
    cmd->ioctx.timeout_usec = 0;
    if (post_fn) {
        cmd->ioctx.post_fn = post_fn->post_fn;
        cmd->ioctx.private_data = post_fn->private_data;
    } else {
        cmd->ioctx.post_fn = NULL;
    }
    cmd->ioctx.opcode = KV_OPC_DELETE_GROUP;
    cmd->ioctx.command.delete_group_info= info;
    cmd->ioctx.ks_id = ks_id;


    return dev->submit_io(que_hdl, cmd);
}

kv_result kv_device_internal::kv_exist(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, uint8_t ks_id, const kv_key *keys, uint32_t key_cnt, kv_postprocess_function *post_fn, uint32_t buffer_size, uint8_t *buffer) {

    if (que_hdl == NULL || ns_hdl == NULL || keys == NULL || buffer == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    // validate each key
    for (uint32_t i = 0; i < key_cnt; i++) {
        kv_result res = validate_key_value(keys + i, NULL);
        if (res != KV_SUCCESS) {
            return res;
        }
    }

    if(ks_id <SAMSUNG_MIN_KEYSPACE_ID || ks_id >= SAMSUNG_MAX_KEYSPACE_CNT){
        return KV_ERR_KEYSPACE_INVALID;
    }

    ioqueue *queue = (ioqueue *)(que_hdl->queue);
    if (queue == NULL) {
        return KV_ERR_QUEUE_QID_INVALID;
    }
    
    kv_device_internal *dev = (kv_device_internal *) que_hdl->dev;
    if (dev == NULL) {
        return KV_ERR_DEV_NOT_EXIST;
    }

    kv_namespace_internal *ns = (kv_namespace_internal *) ns_hdl->ns;
    if (ns == NULL) {
        return KV_ERR_NS_INVALID;
    }


    io_cmd *cmd = new io_cmd(dev, ns, que_hdl);
    cmd->ioctx.key = keys;
    cmd->ioctx.value = 0;
    cmd->ioctx.timeout_usec = 0;
    if (post_fn) {
        cmd->ioctx.post_fn = post_fn->post_fn;
        cmd->ioctx.private_data = post_fn->private_data;
    } else {
        cmd->ioctx.post_fn = NULL;
    }
    cmd->ioctx.opcode = KV_OPC_CHECK_KEY_EXIST;
    cmd->ioctx.result.buffer = buffer;
    cmd->ioctx.command.key_exist_info.result = buffer;
    cmd->ioctx.command.key_exist_info.result_size = buffer_size;
    cmd->ioctx.command.key_exist_info.keycount = key_cnt;
    cmd->ioctx.ks_id = ks_id;

    return dev->submit_io(que_hdl, cmd);
}

// ASYNC IO in a device context
kv_result kv_device_internal::kv_retrieve(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, uint8_t ks_id, const kv_key *key, kv_retrieve_option option, const kv_postprocess_function *post_fn, kv_value *value) {
    if (que_hdl == NULL || ns_hdl == NULL || key == NULL || value == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    kv_result res = validate_key_value(key, value);
    if (res != KV_SUCCESS) {
        return res;
    }

    if(ks_id < SAMSUNG_MIN_KEYSPACE_ID || ks_id >= SAMSUNG_MAX_KEYSPACE_CNT){
          return KV_ERR_KEYSPACE_INVALID;
    }

    /*
    if (option != KV_RETRIEVE_OPT_DEFAULT && option != KV_RETRIEVE_OPT_DECOMPRESS) {
        return KV_ERR_OPTION_INVALID;
    }
    */

    kv_device_internal *dev = (kv_device_internal *) que_hdl->dev;
    if (dev == NULL) {
        return KV_ERR_DEV_NOT_EXIST;
    }

    ioqueue *queue = (ioqueue *)(que_hdl->queue);
    if (queue == NULL) {
        // fprintf(stderr, "queue is null\n");
        return KV_ERR_QUEUE_QID_INVALID;
    }

    kv_namespace_internal *ns = (kv_namespace_internal *) ns_hdl->ns;
    if (ns == NULL) {
        return KV_ERR_NS_INVALID;
    }

    op_get_struct_t info; 
    info.option = option;

    io_cmd *cmd = new io_cmd(dev, ns, que_hdl);
    cmd->ioctx.key = key;
    cmd->ioctx.value = value;
    cmd->ioctx.timeout_usec = 0;
    if (post_fn) {
        cmd->ioctx.post_fn = post_fn->post_fn;
        cmd->ioctx.private_data = post_fn->private_data;
    } else {
        cmd->ioctx.post_fn = NULL;
    }
    cmd->ioctx.opcode = KV_OPC_GET;
    cmd->ioctx.command.get_info = info;
    cmd->ioctx.ks_id = ks_id;
   

    return dev->submit_io(que_hdl, cmd);
}


// Async IO
kv_result kv_device_internal::kv_store(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, uint8_t ks_id, const kv_key *key, const kv_value *value, kv_store_option option, const kv_postprocess_function *post_fn) {
    if (que_hdl == NULL || ns_hdl == NULL || key == NULL || value == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    kv_result res = validate_key_value(key, value);
    if (res != KV_SUCCESS) {
        return res;
    }

    if(ks_id < SAMSUNG_MIN_KEYSPACE_ID || ks_id >= SAMSUNG_MAX_KEYSPACE_CNT){
          return KV_ERR_KEYSPACE_INVALID;
    }

    /* disable checking
    if (option != KV_STORE_OPT_DEFAULT && option != KV_STORE_OPT_IDEMPOTENT && option != KV_STORE_OPT_COMPRESS) {
        return KV_ERR_OPTION_INVALID;
    }
    */

    kv_device_internal *dev = (kv_device_internal *) que_hdl->dev;
    if (dev == NULL) {
        return KV_ERR_DEV_NOT_EXIST;
    }

    ioqueue *queue = (ioqueue *)(que_hdl->queue);
    if (queue == NULL) {
        return KV_ERR_QUEUE_QID_INVALID;
    }

    kv_namespace_internal *ns = (kv_namespace_internal *) ns_hdl->ns;
    if (ns == NULL) {
        return KV_ERR_NS_INVALID;
    }

    op_store_struct_t info; 
    info.option = option;

    io_cmd *cmd = new io_cmd(dev, ns, que_hdl);

    cmd->ioctx.key = key;
    cmd->ioctx.value = const_cast<kv_value *>(value);
    cmd->ioctx.timeout_usec = 0;
    if (post_fn) {
        cmd->ioctx.post_fn = post_fn->post_fn;
        cmd->ioctx.private_data = post_fn->private_data;
    } else {
        cmd->ioctx.post_fn = NULL;
    }
    cmd->ioctx.opcode = KV_OPC_STORE;
    cmd->ioctx.command.store_info = info;
    cmd->ioctx.ks_id = ks_id;

    return dev->submit_io(que_hdl, cmd);
}

// check if any IO commands are done, and call post process function associated with each command
// this is called by host application to directly check completion queue
// host application needs to call this repeatedly
kv_result kv_device_internal::kv_poll_completion(kv_queue_handle que_hdl, uint32_t timeout_usec, uint32_t *num_events) {
    if (que_hdl == NULL || num_events == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    ioqueue *queue = (ioqueue *)(que_hdl->queue);
    if (queue == NULL || queue->get_type() != COMPLETION_Q_TYPE) {
        return KV_ERR_QUEUE_QID_INVALID;
    }

    return queue->poll_completion(timeout_usec, num_events);
}

// set interrupt handler for the device
// called by host application
kv_result kv_device_internal::kv_set_interrupt_handler(kv_queue_handle que_hdl, const kv_interrupt_handler int_hdl) {
    if (que_hdl == NULL || int_hdl == NULL) { 
        return KV_ERR_PARAM_INVALID;
    }

    ioqueue *queue = (ioqueue *)(que_hdl->queue);
    if (queue == NULL) {
        return KV_ERR_QUEUE_QID_INVALID;
    }

    // make sure this is completion queue
    if (queue->get_type() != COMPLETION_Q_TYPE) {
        WRITE_WARN("queue id %d is not completion queue for interrupt setup", que_hdl->qid);
        return KV_ERR_QUEUE_QID_INVALID;
    }

    if (int_hdl == NULL) {
        return KV_ERR_PARAM_INVALID;
    }

    return queue->init_interrupt_handler(this, int_hdl);
}

// XXX TODO with physical device, how this should work is an open question
// Currently it's not quite useful even for emulator. postprocessing callback
// is the main mechanism for caller to control async IO actions
kv_interrupt_handler kv_device_internal::kv_get_interrupt_handler(kv_queue_handle que_hdl) {
    ioqueue *queue = (ioqueue *)(que_hdl->queue);
    return queue->get_interrupt_handler();
}

// for emulator, XXX only use queue with id 1
kv_result kv_device_internal::submit_io(kv_queue_handle que_hdl, io_cmd *cmd) {
    kv_result res = KV_SUCCESS;
    ioqueue *queue = (ioqueue *)(que_hdl->queue);
    emul_ioqueue* eque = (emul_ioqueue*)queue;
    if (que_hdl == NULL || cmd == NULL) {
        res = KV_ERR_PARAM_INVALID;
        goto free_io_cmd;
    }

    // skip this, as higher layer should have checked it.
    // kv_result res = validate_key_value(cmd->get_key(), cmd->get_value());
    // if (res != KV_SUCCESS) {
    //     return res;
    // }
    if (queue == NULL) {
        res = KV_ERR_QUEUE_QID_INVALID;
        goto free_io_cmd;
    }

    if (queue->get_type() != SUBMISSION_Q_TYPE) {
        res = KV_ERR_QUEUE_QID_INVALID;
        goto free_io_cmd;
    }
    res = eque->enqueue(cmd, true);
    if(res != KV_SUCCESS){
        goto free_io_cmd;
    }
    return res;
    
free_io_cmd:
    if(cmd){
        delete cmd;
        cmd = NULL;
    }
    return res;
}

// 0 means invalid
uint16_t kv_device_internal::get_cqid_from_sqid(uint16_t sqid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::unordered_map<uint16_t, uint16_t>::iterator it = m_sq_to_cq_pairs.find(sqid);
    if (it != m_sq_to_cq_pairs.end()) {
        return it->second;
    }
    return 0;
}

uint32_t kv_device_internal::get_next_devid() {
    // thread safety
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    uint32_t devid = kv_device_internal::s_next_devid;
    kv_device_internal::s_next_devid++;
    return devid;
}


// only support 64 bit capacity for now
bool_t kv_device_internal::update_capacity_consumed() {
    std::lock_guard<std::mutex> lock(m_mutex);
    uint64_t consumed = 0;
    uint64_t capacity = 0;

    for (auto& it : m_ns_list) {
        kv_namespace_internal *ns = it.second;

        consumed += ns->get_consumed_space();
        capacity += ns->get_total_capacity();
    }

    float utilization = (1.0 * consumed / capacity) * 10000;

    // update internal stats
    m_capacity = capacity;
    m_device_info.capacity = capacity;

    m_device_stat.utilization = round(utilization);

    // printf("capacity %llu\n", capacity);
    // printf("consumed %llu\n", consumed);
    // printf("utilization %f\n", utilization);
    return TRUE;
}

std::string& kv_device_internal::get_devpath() {
    return m_devpath;
}

} // end of namespace
