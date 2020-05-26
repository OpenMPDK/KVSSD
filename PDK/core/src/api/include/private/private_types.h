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



#ifndef INCLUDE_PRIVATE_PRIVATE_TYPES_H_
#define INCLUDE_PRIVATE_PRIVATE_TYPES_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <mutex>
#include <list>
#include <map>
#include <condition_variable>
#include "kvs_api.h"


#ifndef WITH_SPDK
#include "kvs_adi.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

const int MAX_DEV_PATH_LEN = 256;


// max sub-command number in a batch command
const int MAX_SUB_CMD_NUM = 8;
// max value size of sub-command in a batch command */
const int MAX_SUB_CMD_VALUE_LEN = 8192;

/* the max number of container that supported currently, as currently kv ssd only support two 
keyspace: 0 */ 
const int KS_MAX_CONT = 1; 
const int META_DATA_KEYSPACE_ID = 0; //use keyspace 0 as meta data key space
const int USER_DATA_KEYSPACE_START_ID = 1; //start keyspace id that used for user containers 
//extern const cf_digest cf_digest_zero;
extern const char* KEY_SPACE_LIST_KEY_NAME; //the key of kv pair that store key
                                            //spaces name list
const int PAGE_ALIGN = 4096; //page align (4KB)
const int DMA_ALIGN = 4; //DMA required size align(4B)
#ifndef WITH_SPDK
extern std::map<kv_result, kvs_result> code_map;

inline kvs_result convert_return_code(kv_result kv_return_code)
{
  if(code_map.count(kv_return_code)!=0) {
    return code_map[kv_return_code];
  } else {
    return KVS_ERR_SYS_IO;
  }
}
#endif

class kv_device_priv {
public:
  kv_device_priv() {
    nsid = 0;
    numanode = 0;
    iskv = 0;
    vendorid = 0;
    deviceid = 0;
    isopened = false;
    isattached = false;
    isspdkdev = false;
    isemul = false;
    iskerneldev = false;
    num_opened_qpairs = 0;
  }

  ~kv_device_priv();
  char                node[1024];
  char                spdkpath[1024];
  int                 nsid;
  char                trid[1024];
  char                pci_slot_name[1024];
  int                 numanode;
  int                 iskv;
  int                 vendorid;
  int                 deviceid;
  char                ven_dev_id[128];
  bool                isopened;
  bool 		    isattached;
  bool		    isspdkdev;
  bool		    isemul;
  bool                iskerneldev;
  int 		    num_opened_qpairs;
};

/*
 * KvsDevice represents a KV SSD
 *
 */
class KvsDriver {
public:
  kv_device_priv *dev;
  kvs_postprocess_function user_io_complete;
  std::list<kvs_key_space*> list_containers;
  std::list<kvs_key_space_handle> open_containers;

 public:
 KvsDriver(kv_device_priv *dev_, kvs_postprocess_function user_io_complete_):
	  dev(dev_), user_io_complete(user_io_complete_) {}

  virtual ~KvsDriver() {}

  bool load_data(const char *path);
  bool set_dumppath(const char *path);

  /**
   * Opens a device
   *
   * @param dev_path a path to a device. An emulator is used when a null path or '/dev/kvemul' is given.
   */
  int32_t open(const char *dev_path = 0, std::string dumppath = "auto", int dumpindex = 0);
  int32_t close();

  int32_t exit_env();

  void* operator new(std::size_t sz);
  void operator delete(void* p);
public:
  virtual int32_t init();
  virtual int32_t init(int socket) { return 0; }
  virtual int32_t init(const char* devpath, const char* configfile, int queuedepth, int is_polling) {return 0;}
  virtual int32_t init(const char* devpath, bool syncio, uint64_t sq_core, uint64_t cq_core, uint32_t mem_size_mb, int queue_depth) {return 0;}
  virtual int32_t init(const char* devpath, bool syncio) {return 0;}
  virtual int32_t process_completions(int max) =0;

  //SNIA API
  virtual int32_t store_tuple(kvs_key_space_handle ks_hd, const kvs_key *key,const kvs_value *value, 
    kvs_option_store option, void *private1=NULL, void *private2=NULL, bool sync = false, kvs_postprocess_function cbfn = NULL) = 0;
  virtual int32_t retrieve_tuple(kvs_key_space_handle ks_hd, const kvs_key *key, 
  	kvs_value *value, kvs_option_retrieve option, void *private1=NULL, void *private2=NULL, bool sync = false, kvs_postprocess_function cbfn = NULL) = 0;
  virtual int32_t delete_tuple(kvs_key_space_handle ks_hd, const kvs_key *key, 
  	kvs_option_delete option, void *private1=NULL, void *private2=NULL, bool sync = false, kvs_postprocess_function cbfn = NULL) = 0;
  virtual int32_t exist_tuple(kvs_key_space_handle ks_hd, uint32_t key_cnt, const kvs_key *keys, 
  	kvs_exist_list *list, void *private1=NULL, void *private2=NULL, bool sync = false, kvs_postprocess_function cbfn = NULL) = 0;
  virtual int32_t create_iterator(kvs_key_space_handle ks_hd, kvs_option_iterator option, uint32_t bitmask, 
    uint32_t bit_pattern, kvs_iterator_handle *iter_hd) = 0;
  virtual int32_t delete_iterator(kvs_key_space_handle ks_hd, kvs_iterator_handle hiter) = 0;
  virtual int32_t delete_iterator_all(kvs_key_space_handle ks_hd) = 0;
  virtual int32_t iterator_next(kvs_key_space_handle ks_hd, kvs_iterator_handle hiter, 
    kvs_iterator_list *iter_list, void *private1=NULL, void *private2=NULL, bool sync = false, kvs_postprocess_function cbfn = NULL) = 0;
  virtual float get_waf() {return 0.0;}
  virtual int32_t get_used_size(uint32_t *dev_util) {return 0;}
  virtual int32_t get_total_size(uint64_t *dev_capa) {return 0;}
  virtual int32_t get_device_info(kvs_device *dev_info) {return 0;}
  
  std::string path;
};

struct _kvs_device_handle {
  kv_device_priv * dev;
  KvsDriver* driver;
  char* dev_path;
  kvs_key_space_handle meta_ks_hd;
  std::list<kvs_key_space_handle> open_ks_hds; //containers opened by user
};

struct _kvs_key_space_handle {
  uint8_t container_id;
  uint8_t keyspace_id; //corresponding keyspace id in KVSSD
  kvs_device_handle dev;
  char name[MAX_CONT_PATH_LEN + 1];
};

typedef struct {
  struct {
    int use_dpdk;                 /*!< use DPDK as a memory allocator. It should be 1 if SPDK driver is in use. */
    int dpdk_mastercoreid;        /*!< specify a DPDK master core ID */
    int nr_hugepages_per_socket;  /*!< number of 2MB huge pages per socket available in the socket mask */
    uint16_t socketmask;          /*!< a bitmask for CPU sockets to be used */
    uint64_t max_memorysize_mb;   /*!< the maximum amount of memory */
    uint64_t max_cachesize_mb;    /*!< the maximum cache size in MB */
  } memory;
  
  struct {
    uint64_t iocoremask;          /*!< a bitmask for CPUs to be used for I/O */
    uint32_t queuedepth;          /*!< a maximum queue depth */
  } aio;

  //int ssd_type;
  struct {
    char core_mask_str[256];
    char cq_thread_mask[256];
    uint32_t mem_size_mb;
    int syncio;
  } udd;
    char *emul_config_file;
} kvs_init_options;

// key space data strucure
typedef uint8_t keyspace_id_t;
typedef uint8_t ks_opened_t;
typedef uint8_t ks_key_order_t;
typedef uint64_t ks_capacity_t;
typedef uint64_t ks_free_size_t;
typedef uint64_t ks_kv_count_t;
typedef struct {
  keyspace_id_t keyspace_id;    // the keyspace id that this key space is corresponding
  ks_opened_t opened;         // is this key space opened, 1:open, 0:close
  ks_key_order_t key_order;   // key order, 0:default(order not defined), 1:ascend, 2:descend
  ks_capacity_t capacity;     // key space capacity in bytes
  ks_free_size_t free_size;   // available space of key space in bytes
  ks_kv_count_t kv_count;     // # of Key Value tuples that exist in this key space
  const char* name;             // key space name
} ks_metadata;

typedef struct {
  char names_buffer[MAX_CONT_PATH_LEN + 1];
  keyspace_id_t keyspace_id;
} ks_list_entry;

typedef uint8_t key_space_id_t;
typedef struct {
  key_space_id_t ks_num;
  ks_list_entry entries[KS_MAX_CONT];
} ks_list;

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* INCLUDE_PRIVATE_PRIVATE_TYPES_H_ */
