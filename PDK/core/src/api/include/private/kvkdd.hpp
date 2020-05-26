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


#ifndef KVS_KDD_HPP_
#define KVS_KDD_HPP_

#include "private_types.h"
#include <sstream>
#include <list>
#include <map>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <queue>
#include <kvs_adi.h>
#include <kadi.h>

class KDDriver: public KvsDriver
{

  kv_device_handle    devH;
  kv_namespace_handle nsH ;
  kv_queue_handle     sqH ;
  kv_queue_handle     cqH ;
  int queuedepth;
  //std::atomic_bool cb_stop = { false };

public:

  typedef struct {
    KDDriver* owner;

    kvs_postprocess_context iocb;
    kvs_postprocess_function on_complete;

    std::mutex lock_sync;
    std::condition_variable done_cond_sync;

    bool done;
    bool syncio;
  } kv_kdd_context;

  kv_interrupt_handler int_handler;
  std::mutex lock;
  
public:

  KDDriver(kv_device_priv *dev, kvs_postprocess_function user_io_complete_);
  virtual ~KDDriver();
  virtual int32_t init(const char*devpath, const char* configfile, int queuedepth, int is_polling) override;
  virtual int32_t process_completions(int max) override;
  virtual int32_t store_tuple(kvs_key_space_handle ks_hd, const kvs_key *key, const kvs_value *value, 
    kvs_option_store option, void *private1=NULL, void *private2=NULL, bool sync = false, kvs_postprocess_function cbfn = NULL) override;
  virtual int32_t retrieve_tuple(kvs_key_space_handle ks_hd, const kvs_key *key, kvs_value *value, 
    kvs_option_retrieve option, void *private1=NULL, void *private2=NULL, bool sync = false, kvs_postprocess_function cbfn = NULL) override;
  virtual int32_t delete_tuple(kvs_key_space_handle ks_hd, const kvs_key *key, kvs_option_delete option, 
    void *private1=NULL, void *private2=NULL, bool sync = false, kvs_postprocess_function cbfn = NULL) override;
  virtual int32_t exist_tuple(kvs_key_space_handle ks_hd, uint32_t key_cnt, const kvs_key *keys, kvs_exist_list *list, 
    void *private1=NULL, void *private2=NULL, bool sync = false, kvs_postprocess_function cbfn = NULL) override;
  virtual int32_t create_iterator(kvs_key_space_handle ks_hd, kvs_option_iterator option, uint32_t bitmask, 
    uint32_t bit_pattern, kvs_iterator_handle *iter_hd) override;
  virtual int32_t delete_iterator(kvs_key_space_handle ks_hd, kvs_iterator_handle hiter);
  virtual int32_t delete_iterator_all(kvs_key_space_handle ks_hd);
  virtual int32_t iterator_next(kvs_key_space_handle ks_hd, kvs_iterator_handle hiter, kvs_iterator_list *iter_list, 
    void *private1=NULL, void *private2=NULL, bool sync = false, kvs_postprocess_function cbfn = NULL);
  virtual float get_waf() override;
  virtual int32_t get_used_size(uint32_t *dev_util) override;
  virtual int32_t get_total_size(uint64_t *dev_capa) override;
  virtual int32_t get_device_info(kvs_device *dev_info) override;
  void _kv_callback_thread();

private:
  
  void wait_for_io(kv_kdd_context *ctx);
  int create_queue(int qdepth, uint16_t qtype, kv_queue_handle *handle, int cqid, int is_polling);
  kv_kdd_context* prep_io_context(kvs_context opcode, kvs_key_space_handle ks_hd,
    const kvs_key *key, const kvs_value *value, void *private1, void *private2,
    bool syncio, kvs_postprocess_function cbfn);
  int check_opened_iterators(uint32_t bitmask,uint32_t bit_pattern,
    kvs_iterator_handle *iter_hd);
  int32_t trans_iter_type(uint8_t dev_it_type, uint8_t* kvs_it_type);
  int32_t trans_store_cmd_opt(kvs_option_store kvs_opt, kv_store_option *kv_opt);

  bool ispersist;
  std::string datapath;
};

#endif /* KVDRAM_HPP_ */
