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

#include <unistd.h>
#include "kvs_utils.h"
#include "uddenv.h"
#include "private_types.h"
#ifdef WITH_EMU
#include "kvemul.hpp"
#elif WITH_KDD
#include "kvkdd.hpp"
#else
#include "udd.hpp"
#endif
#include <string.h>
#include <map>
#include <list>
//#include <regex>
#include <string>

namespace api_private {
const char* KEY_SPACE_LIST_KEY_NAME = "key_space_list"; //the key of kv pair that store key
                                                        //spaces name list
struct {
  bool initialized = false;
  bool use_spdk = false;
  int queuedepth;
  int is_polling = 0;
  std::map<std::string, kv_device_priv *> list_devices;
  std::list<kvs_device_handle> open_devices;
  std::list<kvs_container_handle> list_open_container;
#if defined WITH_SPDK
  struct {
    uint64_t cq_masks[NR_MAX_SSD];
    uint64_t core_masks[NR_MAX_SSD];
    uint32_t mem_size_mb;
    int num_devices;
    int syncio;
  } udd_option;
#endif
  char configfile[256];
} g_env;

std::map<int, std::string> errortable;


typedef uint8_t keyspace_id_t;
typedef uint8_t cont_opened_t;
typedef uint8_t cont_key_order_t;
typedef uint64_t cont_capacity_t;
typedef uint64_t cont_free_size_t;
typedef uint64_t cont_kv_count_t;
typedef struct {
  keyspace_id_t keyspace_id;    // the keyspace id that this container is corresponding
  cont_opened_t opened;         // is this container opened, 1:open, 0:close
  cont_key_order_t key_order;   // key order, 0:default(order not defined), 1:ascend, 2:descend
  cont_capacity_t capacity;     // container capacity in bytes
  cont_free_size_t free_size;   // available space of container in bytes
  cont_kv_count_t kv_count;     // # of Key Value tuples that exist in this container
  const char* name;             // container name
} cont_metadata;

typedef struct {
  char names_buffer[MAX_CONT_PATH_LEN];
  keyspace_id_t keyspace_id;
} cont_list_entry;

typedef uint8_t container_id_t;
typedef struct {
  container_id_t cont_num;
  cont_list_entry entries[NR_MAX_CONT];
} cont_list;

const keyspace_id_t _INVALID_USER_KEYSPACE_ID = 0;  //invalid keyspace id


/*!Check wether the iterator bitmask is valid 
 * \desc:  bitmask should be set from the first bit of a key and it is not 
 *            allowed setting bitmask froma middle position of a key. Hence, 
  *           Setting bitmask / prefix as 0xFF/0x0F is allowed while 0x0F/0x0F
 *             is not allowed.
 * \return bool : if input is valid bitmask return true, else return false
 */
inline bool _is_valid_bitmask(uint32_t bitmask){
  const uint32_t BITMASK_LEN = 32;
  //scan prefix bits whose value is 1; scan order: from high bits to low bits
  uint32_t cnt = 0;
  uint32_t bit_idx = 0;
  while(cnt < BITMASK_LEN){
    bit_idx = BITMASK_LEN - cnt - 1;
    if(!(bitmask & (1<<bit_idx)))
      break;
    cnt++;
  }

  //scan remain bits, if has bits whose value is 1, return false, else return true;
  if(cnt == BITMASK_LEN)
    return true;

  cnt++;
  while(cnt < BITMASK_LEN){
    bit_idx = BITMASK_LEN - cnt - 1;
    if(bitmask & (1<<bit_idx))
      return false;
    cnt++;
  }

  return true;
}

kvs_result kvs_exit_env() {
  g_env.initialized = false;
  std::list<kvs_device_handle > clone(g_env.open_devices.begin(),
				      g_env.open_devices.end());
  
  //fprintf(stderr, "KVSSD: Close %d unclosed devices\n", (int) clone.size());
  for (kvs_device_handle t : clone) {
      kvs_close_device(t);
  }
  
  return KVS_SUCCESS;
}

kvs_result kvs_init_env_opts(kvs_init_options* options) {
  memset((void*) options, 0, sizeof(kvs_init_options));
  options->memory.use_dpdk = 1;
  options->memory.dpdk_mastercoreid = -1;
  options->memory.nr_hugepages_per_socket = 1024;
  options->memory.socketmask = 0;
  return KVS_SUCCESS;
}

int kvs_list_kvdevices(kv_device_info **devs, int size) {
  int index = 0;
  int max = std::min((int) g_env.list_devices.size(), size);
  
  for (const auto &t : g_env.list_devices) {
    kv_device_priv* dev_i = t.second;
    kv_device_info* dev = (kv_device_info*) malloc(sizeof(kv_device_info));

    if(dev){
      snprintf(dev->node, sizeof(dev->node), "%s", dev_i->node);
      snprintf(dev->spdkpath, sizeof(dev->spdkpath), "%s", dev_i->spdkpath);
      dev->nsid = dev_i->nsid;
      snprintf(dev->pci_slot_name, sizeof(dev->pci_slot_name), "%s", dev_i->pci_slot_name);
      dev->numanode = dev_i->numanode;
      dev->vendorid = dev_i->vendorid;
      dev->deviceid = dev_i->deviceid;
      snprintf(dev->ven_dev_id, sizeof(dev->ven_dev_id), "%s", dev_i->ven_dev_id);
    }
    devs[index++] = dev;

    if (index == max)
      break;
  }
  return index;
}

#if defined WITH_SPDK
int initialize_udd_options(kvs_init_options* options){
  int i = 0;
  std::string delim = ",";
  char *pt;
  char *saveptr = NULL;
  pt = strtok_r(options->udd.core_mask_str, ",", &saveptr);
  while(pt != NULL) {
    g_env.udd_option.core_masks[i++] = std::stol(pt);
    pt = strtok_r(NULL, ",", &saveptr);
  }
  
  i = 0;
  pt = strtok_r(options->udd.cq_thread_mask, ",", &saveptr);
  while(pt != NULL) {
    g_env.udd_option.cq_masks[i++] = std::stol(pt);
    pt = strtok_r(NULL, ",", &saveptr);
  }
  
  g_env.udd_option.num_devices = 0;
  g_env.udd_option.mem_size_mb = options->udd.mem_size_mb;
  g_env.udd_option.syncio = options->udd.syncio;
  
  return 0;
}
#endif

kvs_result kvs_init_env(kvs_init_options* options) {
  if (g_env.initialized)
    return KVS_SUCCESS;

  if (options) {
    g_env.queuedepth = options->aio.queuedepth > 0 ? options->aio.queuedepth : 256;
    // initialize memory
    if (options->memory.use_dpdk == 1) {
#if defined WITH_SPDK
      init_udd(options);
      g_env.use_spdk = true;
      initialize_udd_options(options);
#else
      fprintf(stderr, "SPDK driver is not available\n");
      exit(0);
#endif
    } else {
#if defined WITH_EMU
      if(options->emul_config_file == NULL){
        WRITE_WARNING("Emulator configure file can not be NULL\n");
        return KVS_ERR_OPTION_INVALID;
      }
      if(access(options->emul_config_file, F_OK)){
        WRITE_WARNING("Emulator configure file does not exist\n");
        return KVS_ERR_OPTION_INVALID;
      }
      if(access(options->emul_config_file, R_OK)){
        WRITE_WARNING("Emulator configure file can not be readed\n");
        return KVS_ERR_OPTION_INVALID;
      }
      snprintf(g_env.configfile, sizeof(g_env.configfile), "%s", options->emul_config_file);
#endif
      // emulator or kdd
      //fprintf(stdout, "Using KV Emulator or Kernel\n");
      //g_env.is_polling = options->aio.is_polling;
    }

    // initialize cache if needed
    if (options->memory.max_cachesize_mb > 0) {
      WRITE_WARNING("Key-value caching is not supported yet\n");
      return KVS_ERR_OPTION_INVALID;//KVS_ERR_OPTION_NOT_SUPPORTED;
    }
    /*	  
    if (options->aio.iocomplete_fn != 0) { // async io
      g_env.iocomplete_fn = options->aio.iocomplete_fn;
      g_env.use_async = true;
      
      // initialize aio threads if needed
      if (options->memory.use_dpdk == 0) {
	// create aio threads for non-spdk drivers
      }
    } else { // sync io
      //g_env.is_polling = 1; // always polling for sync mode
      g_env.is_polling = 0; // interrupt for sync
    }
    */
  }

  g_env.initialized = true;

  //WRITE_LOG("INIT_ENV Finished: async? %d\n", g_env.use_async);
  return KVS_SUCCESS;
}


kv_device_priv *_find_local_device_from_path(const std::string &devpath,
		std::map<std::string, kv_device_priv *> *list_devices) {

  //static std::regex emu_pattern("/dev/kvemul*");
  kv_device_priv *dev = 0;
  //std::smatch matches;

#if defined WITH_SPDK
  //static std::regex pciaddr_pattern("[^:]*:(.*)$");
  //if (std::regex_search(devpath, matches, pciaddr_pattern)) {
    kv_device_priv *udd = new kv_device_priv();
    static int uddnsid = 0;
    udd->nsid = uddnsid++;
    udd->isemul = false;
    udd->iskerneldev = false;
    udd->isspdkdev = true;
    return udd;
    /*
  } else {
    fprintf(stderr, "WRN: Please specify spdk device path properly\n");
    exit(1);
  }
    */
#elif defined WITH_EMU
  static int emulnsid = 0;
  kv_device_priv *emul = new kv_device_priv();
  snprintf(emul->node, sizeof(emul->node), "%s", devpath.c_str());
  emul->nsid = emulnsid++;
  emul->isemul = true;
  emul->iskerneldev = false;
  return emul;
#elif defined WITH_KDD
  kv_device_priv *kdd = new kv_device_priv();
  static int kddnsid = 0;
  kdd->nsid = kddnsid++;
  kdd->isemul = false;
  kdd->isspdkdev = false;
  kdd->iskerneldev = true;
  return kdd;
#endif
  return dev;
}

KvsDriver *_select_driver(kv_device_priv *dev) {
#if defined WITH_SPDK
  if (dev->isspdkdev) {
    if (!g_env.use_spdk) {
      WRITE_ERR(
		"DPDK is not initialized. Please call the init_env with DPDK options\n");
    }
    //return new KUDDriver(dev, g_env.iocomplete_fn);
    return new KUDDriver(dev, 0);
  }
#else
  #if defined WITH_EMU
    
      return new KvEmulator(dev, 0);
      //return new KvEmulator(dev, g_env.iocomplete_fn );
    
  #elif defined WITH_KDD
      return new KDDriver(dev, 0); 
      //return new KDDriver(dev, g_env.iocomplete_fn);
    
  #endif
#endif
    return nullptr;
}

void build_error_table() {
  errortable[0x0]="KVS_SUCCESS";
  errortable[0x001]="KVS_ERR_BUFFER_SMALL";
  errortable[0x002]="KVS_ERR_COMMAND_INITIALIZED";
  errortable[0x003]="KVS_ERR_COMMAND_SUBMITTED ";
  errortable[0x004]="KVS_ERR_DEV_CAPACITY";
  errortable[0x005]="KVS_ERR_DEV_INIT";
  errortable[0x006]="KVS_ERR_DEV_INITIALIZED";
  errortable[0x007]="KVS_ERR_DEV_NOT_EXIST";
  errortable[0x008]="KVS_ERR_DEV_SANITIZE_FAILED";
  errortable[0x009]="KVS_ERR_DEV_SANIZE_IN_PROGRESS";
  errortable[0x00A]="KVS_ERR_ITERATOR_COND_INVALID";
  errortable[0x00B]="KVS_ERR_ITERATOR_MAX";
  errortable[0x00C]="KVS_ERR_ITERATOR_NOT_EXIST";
  errortable[0x00D]="KVS_ERR_ITERATOR_OPEN";
  errortable[0x00E]="KVS_ERR_KEY_EXIST";
  errortable[0x00F]="KVS_ERR_KEY_INVALID";
  errortable[0x010]="KVS_ERR_KEY_LENGTH_INVALID";
  errortable[0x011]="KVS_ERR_KEY_NOT_EXIST";
  errortable[0x012]="KVS_ERR_OPTION_INVALID";
  errortable[0x013]="KVS_ERR_PARAM_INVALID";
  errortable[0x014]="KVS_ERR_PURGE_IN_PROGRESS  ";
  errortable[0x015]="KVS_ERR_QUEUE_CQID_INVALID";
  errortable[0x016]="KVS_ERR_QUEUE_DELETION_INVALID";
  errortable[0x017]="KVS_ERR_QUEUE_IN_SUTDOWN";
  errortable[0x018]="KVS_ERR_QUEUE_IS_FULL";
  errortable[0x019]="KVS_ERR_QUEUE_MAX_QUEUE";
  errortable[0x01A]="KVS_ERR_QUEUE_QID_INVALID";
  errortable[0x01B]="KVS_ERR_QUEUE_QSIZE_INVALID";
  errortable[0x01C]="KVS_ERR_QUEUE_SQID_INVALID";
  errortable[0x01D]="KVS_ERR_SYS_BUSY  ";
  errortable[0x01E]="KVS_ERR_SYS_IO";
  errortable[0x01F]="KVS_ERR_TIMEOUT";
  errortable[0x020]="KVS_ERR_UNCORRECTIBLE";
  errortable[0x021]="KVS_ERR_VALUE_LENGTH_INVALID";
  errortable[0x022]="KVS_ERR_VALUE_LENGTH_MISALIGNED";
  errortable[0x023]="KVS_ERR_VALUE_OFFSET_INVALID";
  errortable[0x024]="KVS_ERR_VALUE_UPDATE_NOT_ALLOWED";
  errortable[0x025]="KVS_ERR_VENDOR";
  errortable[0x026]="KVS_ERR_PERMISSION";
  errortable[0x027]="KVS_ERR_ENV_NOT_INITIALIZED";
  errortable[0x028]="KVS_ERR_DEV_NOT_OPENED";
  errortable[0x029]="KVS_ERR_DEV_ALREADY_OPENED";
  errortable[0x02A]="KVS_ERR_DEV_PATH_TOO_LONG";
  errortable[0x02B]="KVS_ERR_ITERATOR_NUM_OUT_RANGE";
  errortable[0x02C]="KVS_ERR_DD_UNSUPPORTED";
  errortable[0x02D]="KVS_ERR_ITERATOR_BUFFER_SIZE";
  errortable[0x032]="KVS_ERR_MEMORY_MALLOC_FAIL";
  errortable[0x200]="KVS_ERR_CACHE_INVALID_PARAM";
  errortable[0x201]="KVS_ERR_CACHE_NO_CACHED_KEY";
  errortable[0x202]="KVS_ERR_DD_INVALID_QUEUE_TYPE";
  errortable[0x203]="KVS_ERR_DD_NO_AVAILABLE_RESOURCE";
  errortable[0x204]="KVS_ERR_DD_NO_DEVICE";
  errortable[0x205]="KVS_ERR_DD_UNSUPPORTED_CMD";
  errortable[0x206]="KVS_ERR_DECOMPRESSION";
  errortable[0x207]="KVS_ERR_HEAP_ALLOC_FAILURE";
  errortable[0x208]="KVS_ERR_ITERATE_HANDLE_ALREADY_OPENED";
  errortable[0x209]="KVS_ERR_ITERATE_REQUEST_FAIL";
  errortable[0x20A]="KVS_ERR_MAXIMUM_VALUE_SIZE_LIMIT_EXCEEDED";
  errortable[0x20B]="KVS_ERR_MISALIGNED_KEY_SIZE";
  errortable[0x20C]="KVS_ERR_MISALIGNED_VALUE_OFFSET";
  errortable[0x20D]="KVS_ERR_SDK_CLOSE";
  errortable[0x20E]="KVS_ERR_SDK_INVALID_PARAM";
  errortable[0x20F]="KVS_ERR_SDK_OPEN";
  errortable[0x210]="KVS_ERR_SLAB_ALLOC_FAILURE";
  errortable[0x211]="KVS_ERR_UNRECOVERED_ERROR";
  errortable[0x300]="KVS_ERR_NS_ATTACHED";
  errortable[0x301]="KVS_ERR_NS_CAPACITY";
  errortable[0x302]="KVS_ERR_NS_DEFAULT";
  errortable[0x303]="KVS_ERR_NS_INVALID";
  errortable[0x304]="KVS_ERR_NS_MAX";
  errortable[0x305]="KVS_ERR_NS_NOT_ATTACHED";
  errortable[0x400]="KVS_ERR_CONT_CAPACITY";
  errortable[0x401]="KVS_ERR_CONT_CLOSE";
  errortable[0x402]="KVS_ERR_CONT_EXIST";
  errortable[0x403]="KVS_ERR_CONT_GROUP_BY";
  errortable[0x404]="KVS_ERR_CONT_INDEX";
  errortable[0x405]="KVS_ERR_CONT_NAME";
  errortable[0x406]="KVS_ERR_CONT_NOT_EXIST";
  errortable[0x407]="KVS_ERR_CONT_OPEN";
  errortable[0x408]="KVS_ERR_CONT_PATH_TOO_LONG";
  errortable[0x409]="KVS_ERR_CONT_MAX";
}

const char *kvs_errstr(int32_t errorno) {
  return errortable[errorno].c_str();
}

bool _device_opened(kvs_device_handle dev_hd){
  auto t  = find(g_env.open_devices.begin(), g_env.open_devices.end(), dev_hd);
  if (t == g_env.open_devices.end()){
    return false;
  }
  return true;
}

bool _device_opened(const char* dev_path){
  std::string dev(dev_path);
  for (const auto &t : g_env.open_devices) {
    if(t->dev_path == dev){
      return true;
    }
  }
  return false;
}

bool _container_opened(kvs_device_handle dev_hd, const char* name) {
  if(dev_hd->open_cont_hds.empty()) {
    return false;
  }
  for (const auto &t : dev_hd->open_cont_hds) {
    if(strcmp(t->name, name) == 0) {
      return true;
    }
  }
  return false;
}

bool _container_opened(kvs_container_handle cont_hd) {
  auto t = find(g_env.list_open_container.begin(), g_env.list_open_container.end(), cont_hd);
  if (t == g_env.list_open_container.end()) {
    return false;
  }
  return true;
}

void _remove_container_from_g_env(kvs_container_handle cont_hd) {
  auto t = find(g_env.list_open_container.begin(), g_env.list_open_container.end(), cont_hd);
  if (t != g_env.list_open_container.end()) {
    g_env.list_open_container.erase(t);
  }
}

inline kvs_result _check_container_handle(kvs_container_handle cont_hd) {
  if (cont_hd == NULL) {
    return KVS_ERR_PARAM_INVALID;
  }
  if (!_container_opened(cont_hd)) {
    return KVS_ERR_CONT_CLOSE;
  }
  if ((cont_hd->dev == NULL) || (cont_hd->dev->driver == NULL)) {
    return KVS_ERR_PARAM_INVALID;
  }
  if (!_device_opened(cont_hd->dev)) {
    return KVS_ERR_DEV_NOT_OPENED;
  }
  return KVS_SUCCESS;
}

uint16_t _get_container_payload_size() {
  cont_metadata cont;
  uint16_t payload_size = sizeof(cont.keyspace_id);
  payload_size += sizeof(cont.opened);
  payload_size += sizeof(cont.key_order);
  payload_size += sizeof(cont.capacity);
  payload_size += sizeof(cont.free_size);
  payload_size += sizeof(cont.kv_count);
  return payload_size;
}

// translate byte order from host cpu end to little end, will change the content of buffer inputted
// the size of of inputted interger should be in 1/2/4/8 byte
bool _trans_host_to_little_end(void* data, uint8_t size) {
  if(size == 1)
    return true;
  if(size == 2) {
    uint16_t data_le = htole16(*((uint16_t*)data));
    memcpy(data, (void*)(&data_le), size);
    return true;
  }
  if(size == 4) {
    uint32_t data_le = htole32(*((uint32_t*)data));
    memcpy(data, (void*)(&data_le), size);
    return true;
  }
  if(size == 8) {
    uint64_t data_le = htole64(*((uint64_t*)data));
    memcpy(data, (void*)(&data_le), size);
    return true;
  }
  return false;
}

//copy a integer in to payload. Integer will translate to little end, and then copied to buffer 
#define _copy_int_to_payload(integer, payload_buff, curr_posi) \
do { \
  uint8_t integer_size = sizeof(integer); \
  _trans_host_to_little_end(&integer, integer_size); \
  memcpy(payload_buff + curr_posi, &integer, integer_size); \
  curr_posi += integer_size; \
} while(0)

void _construct_container_metadata_payload(const cont_metadata *cont,
  char* payload_buff) {
  cont_metadata cont_le = *cont;
  uint16_t curr_posi = 0;
  _copy_int_to_payload(cont_le.keyspace_id, payload_buff, curr_posi);
  _copy_int_to_payload(cont_le.opened, payload_buff, curr_posi);
  _copy_int_to_payload(cont_le.key_order, payload_buff, curr_posi);
  _copy_int_to_payload(cont_le.capacity, payload_buff, curr_posi);
  _copy_int_to_payload(cont_le.free_size, payload_buff, curr_posi);
  _copy_int_to_payload(cont_le.kv_count, payload_buff, curr_posi);
}

// translate byte order from little end to host cpu end, will change the content of buffer inputted
// the size of of inputted interger should be in 1/2/4/8 byte
bool _trans_little_end_to_host(void* data, uint8_t size) {
  if(size == 1)
    return true;
  if(size == 2) {
    uint16_t data_le = le16toh(*((uint16_t*)data));
    memcpy(data, (void*)(&data_le), size);
    return true;
  }
  if(size == 4) {
    uint32_t data_le = le32toh(*((uint32_t*)data));
    memcpy(data, (void*)(&data_le), size);
    return true;
  }
  if(size == 8) {
    uint64_t data_le = le64toh(*((uint64_t*)data));
    memcpy(data, (void*)(&data_le), size);
    return true;
  }
  return false;
}

//Copy data from payload to a integer. 
//Copied to integer type buffer, and  translate to host cpu end
#define _copy_payload_to_int(integer, payload_buff, curr_posi) \
do { \
  uint16_t integer_size = sizeof(integer); \
  memcpy(&integer, payload_buff + curr_posi, integer_size); \
  _trans_little_end_to_host(&integer, integer_size); \
  curr_posi += integer_size; \
} while(0)

void _parse_container_metadata_payload(cont_metadata *cont,
  const char* payload_buff) {
  uint16_t curr_posi = 0;
  _copy_payload_to_int(cont->keyspace_id, payload_buff, curr_posi);
  _copy_payload_to_int(cont->opened, payload_buff, curr_posi);
  _copy_payload_to_int(cont->key_order, payload_buff, curr_posi);
  _copy_payload_to_int(cont->capacity, payload_buff, curr_posi);
  _copy_payload_to_int(cont->free_size, payload_buff, curr_posi);
  _copy_payload_to_int(cont->kv_count, payload_buff, curr_posi); 
}

void _metadata_keyspace_aio_complete_handle(
  kvs_callback_context* ioctx) {
  uint32_t *complete_ptr = (uint32_t *)ioctx->private1;
  *complete_ptr = 1;
  kvs_result *result_ptr = (kvs_result *)ioctx->private2; 
  *result_ptr = ioctx->result;
}

kvs_result _sync_io_to_meta_keyspace(kvs_device_handle dev_hd, 
  const kvs_key *key, kvs_value *value, void* io_option, kvs_op io_op) {
  kvs_result ret = KVS_SUCCESS;
  bool syncio = 1;
  kvs_callback_function cbfn = NULL;
  /* as async_completed and async_result may be modify by other cpu(in complete handle),
        so should be volatile type */
  volatile uint32_t async_completed = 0; 
  volatile kvs_result async_result = KVS_SUCCESS;

  /*If use kdd or emulator sync store can be use always.
       If use UDD should use corresponding interface, 
       sync mode: sync interface, async mode: async interface*/
#if defined WITH_SPDK
  if(!g_env.udd_option.syncio) {//uses async interface
    syncio = 0;
    cbfn = _metadata_keyspace_aio_complete_handle;
  }
#endif

  kvs_container_handle cont_hd = dev_hd->meta_cont_hd;
  if(io_op == IOCB_ASYNC_PUT_CMD) {
    ret = (kvs_result)dev_hd->meta_cont_hd->dev->driver->store_tuple(
      cont_hd, key, value, *((kvs_store_option*)io_option), (void*)&async_completed,
      (void*)&async_result, syncio, cbfn);
  }else if(io_op == IOCB_ASYNC_GET_CMD){
    ret = (kvs_result)dev_hd->meta_cont_hd->dev->driver->retrieve_tuple(
      cont_hd, key, value, *((kvs_retrieve_option*)io_option), (void*)&async_completed,
      (void*)&async_result, syncio, cbfn);
  }else if(io_op == IOCB_ASYNC_DEL_CMD){
    ret = (kvs_result)dev_hd->meta_cont_hd->dev->driver->delete_tuple(
      cont_hd, key, *((kvs_delete_option*)io_option), (void*)&async_completed,
      (void*)&async_result, syncio, cbfn);
  }else if(io_op == IOCB_ASYNC_CHECK_KEY_EXIST_CMD){
    ret = (kvs_result)dev_hd->meta_cont_hd->dev->driver->exist_tuple(
      cont_hd, 1, key, value->length, (uint8_t *)value->value, (void*)&async_completed,
      (void*)&async_result, syncio, cbfn);
  }else {
    fprintf(stderr, "KVAPI internal error unsupported aio type:%d passed.\n",
      io_op);
    return KVS_ERR_UNCORRECTIBLE;
  }
  if(ret != KVS_SUCCESS) {
    return ret;
  }
  if(!syncio) {//used async io interface
    while(true) {//wait for complete
      if(async_completed) {
        break;
      }
    }
    ret = async_result;
  }

  return ret;
}
  

kvs_result _store_container_metadata(kvs_device_handle dev_hd,
  const cont_metadata *cont, kvs_store_type st_type) {
  kvs_result ret = KVS_SUCCESS;
  //calculate key size and payload size
  uint16_t payload_size = _get_container_payload_size();
  kvs_key_t klen = strnlen(cont->name, MAX_CONT_PATH_LEN - 1) + 1; //contains end '\0'
  char* key = (char*)kvs_zalloc(klen, PAGE_ALIGN);
  char* payload_buff = (char*)kvs_zalloc(payload_size, PAGE_ALIGN);
  if(!key || !payload_buff) {
    if(key) kvs_free(key);
    if(payload_buff) kvs_free(payload_buff);
    return KVS_ERR_HEAP_ALLOC_FAILURE;
  }
  // construct the payload content
  snprintf(key, klen, "%s", cont->name);
  _construct_container_metadata_payload(cont, payload_buff);

  //store to meta data keyspace
  kvs_store_option option;
  option.st_type = st_type;
  option.kvs_store_compress = false;
  const kvs_key  kvskey = {key, klen};
  kvs_value kvsvalue = {payload_buff, payload_size, 0, 0};
  ret = _sync_io_to_meta_keyspace(dev_hd, &kvskey, &kvsvalue, &option,
    IOCB_ASYNC_PUT_CMD);
  if(ret != KVS_SUCCESS) {
    fprintf(stderr, "store keyspace meta data failed with error 0x%x - %s\n",
       ret, kvs_errstr(ret));
  }

  kvs_free(key);
  kvs_free(payload_buff);
  return ret;
}

kvs_result _retrieve_container_metadata(kvs_device_handle dev_hd,
  cont_metadata *cont) {
  kvs_result ret = KVS_SUCCESS;

  //malloc resources
  uint16_t vlen = _get_container_payload_size();
  vlen = ((vlen - 1)/DMA_ALIGN + 1) * DMA_ALIGN;
  kvs_key_t klen = strlen(cont->name) + 1; //contains end '\0'
  char *key   = (char*)kvs_malloc(klen, PAGE_ALIGN);
  char *value = (char*)kvs_malloc(vlen, PAGE_ALIGN);
  if(key == NULL || value == NULL) {
    fprintf(stderr, "failed to allocate\n");
    if(key) kvs_free(key);
    if(value) kvs_free(value);
    return KVS_ERR_HEAP_ALLOC_FAILURE;
  }
  snprintf(key, klen, "%s", cont->name);

  kvs_retrieve_option option;
  memset(&option, 0, sizeof(kvs_retrieve_option));
  option.kvs_retrieve_decompress = false;
  option.kvs_retrieve_delete = false;
  const kvs_key  kvskey = {key, klen };
  kvs_value kvsvalue = {value, vlen , 0, 0 /*offset */};
  ret = _sync_io_to_meta_keyspace(dev_hd, &kvskey, &kvsvalue, &option,
    IOCB_ASYNC_GET_CMD);
  if(ret != KVS_SUCCESS) {
    fprintf(stderr, "retrieve container %s metatdata failed error 0x%x - %s\n",
      key, ret, kvs_errstr(ret));
    kvs_free(key);
    kvs_free(value);
    return ret;
  }

  //parse continers information
  _parse_container_metadata_payload(cont, value);

  kvs_free(key);
  kvs_free(value);
  return ret;
}

kvs_result _open_container(kvs_container_handle cont_hd) {
  kvs_result ret = KVS_SUCCESS;
  cont_metadata cont = {0, 0, 0, 0, 0, 0, cont_hd->name};
  ret = _retrieve_container_metadata(cont_hd->dev, &cont);
  if(ret != KVS_SUCCESS) {
    return ret;
  }
  cont.opened = 1;
  cont_hd->keyspace_id = cont.keyspace_id;
  ret = _store_container_metadata(cont_hd->dev, &cont, KVS_STORE_POST);
  return ret;
}

kvs_result _close_container(kvs_container_handle cont_hd) {
  kvs_result ret = KVS_SUCCESS;
  cont_metadata cont = {0, 0, 0, 0, 0, 0, cont_hd->name};
  ret = _retrieve_container_metadata(cont_hd->dev, &cont);
  if(ret != KVS_SUCCESS) {
    return ret;
  }
  cont.opened = 0;
  ret = _store_container_metadata(cont_hd->dev, &cont, KVS_STORE_POST);
  return ret;
}

kvs_result _create_container_entry(kvs_device_handle dev_hd,
  const char *name, uint8_t keyspace_id, uint64_t cap_size,
  const kvs_container_context *ctx) {
  int ret = KVS_SUCCESS;
  cont_metadata cont = {
    keyspace_id, 0, ctx->option.ordering, cap_size, 0, 0, name};
  ret = _store_container_metadata(dev_hd, &cont, KVS_STORE_NOOVERWRITE);
  return (kvs_result)ret;
}

kvs_result _delete_container_entry(kvs_device_handle dev_hd,
  const char *name) {
  kvs_result ret = KVS_SUCCESS;
  kvs_key_t klen = strlen(name) + 1; //contains end '\0'
  char* key = (char*)kvs_zalloc(klen, PAGE_ALIGN);
  if(!key) {
    return KVS_ERR_HEAP_ALLOC_FAILURE;
  }
  snprintf(key, klen, "%s", name);

  const kvs_key  kvskey = {key, klen};
  kvs_delete_option option = {true};
  ret = _sync_io_to_meta_keyspace(dev_hd, &kvskey, NULL, &option,
    IOCB_ASYNC_DEL_CMD);
  if(ret != KVS_SUCCESS) {
    fprintf(stderr, "delete container failed with error 0x%x - %s\n", ret,
      kvs_errstr(ret));
  }

  kvs_free(key);
  return ret;
}

kvs_result _exist_container_entry(kvs_device_handle dev_hd,
  const char *name, uint8_t* exist_buff) {
  kvs_result ret = KVS_SUCCESS;
  kvs_key_t klen = strlen(name) + 1; //contains end '\0'
  char* key = (char*)kvs_zalloc(klen, PAGE_ALIGN);
  if(!key) {
    return KVS_ERR_HEAP_ALLOC_FAILURE;
  }

  snprintf(key, klen, "%s", name);
  const kvs_key  kvskey = {key, klen};
  *exist_buff = 0;
  kvs_value kvsvalue = {exist_buff, 1 , 0, 0};
  /*exist io no option need*/
  ret = _sync_io_to_meta_keyspace(dev_hd, &kvskey, &kvsvalue, NULL,
    IOCB_ASYNC_CHECK_KEY_EXIST_CMD);
  if(ret != KVS_SUCCESS) {
    fprintf(stderr, "Check container exist failed with error 0x%x - %s\n", ret,
      kvs_errstr(ret));
  }

  return ret;
}


void _construct_container_list_payload(const cont_list* conts,
  char* payload_buff, uint32_t *data_len) {
  uint16_t curr_position = 0;
  //total container number
  memcpy(payload_buff + curr_position, &conts->cont_num,
         sizeof(conts->cont_num));
  curr_position += sizeof(conts->cont_num);
  //name of every container, 
  //every container name has fixed size(MAX_CONT_PATH_LEN) in ssd
  for(uint8_t idx = 0; idx < conts->cont_num; idx++){
    memcpy(payload_buff+curr_position, &conts->entries[idx].keyspace_id,
           sizeof(conts->entries[idx].keyspace_id));
    curr_position += sizeof(conts->entries[idx].keyspace_id);

    memcpy(payload_buff+curr_position, conts->entries[idx].names_buffer,
           MAX_CONT_PATH_LEN);
    curr_position += MAX_CONT_PATH_LEN;
  }
  *data_len = curr_position;
}

void _parse_container_list_payload(const char *payload, uint32_t data_len,
  cont_list* conts) {
  //parse continers information, format as following:
  //first four bytes is the number of containers, then keyspace id and name  of every container
  //is stored, the keyspace id and name size of every container is 4 and MAX_CONT_PATH_LEN(256)
  if(data_len < sizeof(conts->cont_num)) { //means number of container is 0
    conts->cont_num = 0;
    return;
  }
  uint32_t curr_posi = 0;
  conts->cont_num = *((container_id_t *)payload);
  curr_posi += sizeof(conts->cont_num);
  for(uint8_t idx = 0; idx < conts->cont_num; idx++){
    conts->entries[idx].keyspace_id = *((keyspace_id_t *)(payload + curr_posi));
    curr_posi += sizeof(conts->entries[idx].keyspace_id);

    memcpy(conts->entries[idx].names_buffer, payload + curr_posi,
      MAX_CONT_PATH_LEN);
    curr_posi += MAX_CONT_PATH_LEN;
  }
}

kvs_result _retreive_container_list(kvs_device_handle dev_hd,
  cont_list* conts) {
  kvs_result ret = KVS_SUCCESS;
  kvs_key_t klen = strlen(KEY_SPACE_LIST_KEY_NAME) + 1;
  uint32_t act_len = sizeof(*conts);
  uint32_t vlen = ((act_len + 3) / 4) * 4;

  char *key   = (char*)kvs_malloc(klen, PAGE_ALIGN);
  char *value = (char*)kvs_malloc(vlen, PAGE_ALIGN);
  if(key == NULL || value == NULL) {
    fprintf(stderr, "failed to allocate\n");
    if(key) kvs_free(key);
    if(value) kvs_free(value);
    return KVS_ERR_HEAP_ALLOC_FAILURE;
  }

  //retrieve container list data from KVSSD metadata keyspace
  snprintf(key, klen, "%s", KEY_SPACE_LIST_KEY_NAME);
  kvs_retrieve_option option;
  memset(&option, 0, sizeof(kvs_retrieve_option));
  option.kvs_retrieve_decompress = false;
  option.kvs_retrieve_delete = false;
  const kvs_key  kvskey = {key, klen };
  kvs_value kvsvalue = {value, vlen , 0, 0 /*offset */};
  ret = _sync_io_to_meta_keyspace(dev_hd, &kvskey, &kvsvalue, &option,
    IOCB_ASYNC_GET_CMD);
  if(ret != KVS_SUCCESS) {
    if(ret == KVS_ERR_KEY_NOT_EXIST) {//before create first container, key isn't exist
      ret = KVS_SUCCESS;
    } else {
      fprintf(stderr, "Input value buffer len:%d. actual data length:%d.\n", kvsvalue.length, 
        kvsvalue.actual_value_size);
      fprintf(stderr, "retrieve container list failed error 0x%x - %s\n", ret,
        kvs_errstr(ret));
    }
    conts->cont_num = 0;
    kvs_free(key);
    kvs_free(value);
    return ret;
  }
  _parse_container_list_payload((const char*)kvsvalue.value, kvsvalue.length,
    conts);

  kvs_free(key);
  kvs_free(value);
  return ret;
}

kvs_result _store_container_list(kvs_device_handle dev_hd,
  const cont_list* conts) {
  kvs_result ret = KVS_SUCCESS;
  // construct the payload content
  kvs_key_t klen = strlen(KEY_SPACE_LIST_KEY_NAME) + 1; 
  char* key = (char*)kvs_zalloc(klen, PAGE_ALIGN);
  char* payload_buff = (char*)kvs_zalloc(sizeof(*conts), PAGE_ALIGN);
  if(!key || !payload_buff) {
    if(key) kvs_free(key);
    if(payload_buff) kvs_free(payload_buff);
    return KVS_ERR_HEAP_ALLOC_FAILURE;
  }
  snprintf(key, klen, "%s", KEY_SPACE_LIST_KEY_NAME);
  
  uint32_t vlen = 0;
  _construct_container_list_payload(conts, payload_buff, &vlen);

  //store to meta data keyspace
  kvs_store_option option;
  option.st_type = KVS_STORE_POST;
  option.kvs_store_compress = false;
  const kvs_key  kvskey = {key, klen};
  kvs_value kvsvalue = {payload_buff, vlen, 0, 0};  
  ret = _sync_io_to_meta_keyspace(dev_hd, &kvskey, &kvsvalue, &option,
    IOCB_ASYNC_PUT_CMD);
  if(ret != KVS_SUCCESS ) {
    fprintf(stderr, "store container list failed with error 0x%x - %s\n", ret,
      kvs_errstr(ret));
  }

  kvs_free(key);
  kvs_free(payload_buff);
  return ret;
}

kvs_result _remove_from_container_list(kvs_device_handle dev_hd,
  const char *name, keyspace_id_t *keyspace_id) {
  kvs_result ret = KVS_SUCCESS;
  //check whether key space is exist
  cont_list* conts = (cont_list*)calloc(1, sizeof(cont_list));
  if(!conts)
    return KVS_ERR_HEAP_ALLOC_FAILURE;
  conts->cont_num = 0;
  ret = _retreive_container_list(dev_hd, conts);
  if(ret != KVS_SUCCESS) {
    free(conts);
    return ret;
  }
  uint8_t idx = 0;
  for(idx = 0; idx < conts->cont_num; idx++){
    if(!strcmp(name, conts->entries[idx].names_buffer))
      break;
  }

  //remove the found contain from list
  if(idx >= conts->cont_num) { //not find the input container
    free(conts);
    return KVS_ERR_CONT_NOT_EXIST;
  }else {
    *keyspace_id = conts->entries[idx].keyspace_id;
    // move forward valid contains
    for(; idx < conts->cont_num - 1; idx++){
      memcpy(conts->entries + idx, conts->entries + idx + 1,
        sizeof(cont_list_entry));
    }
    conts->cont_num--;
  }

  // store contain list to ssd
  ret = _store_container_list(dev_hd, conts);
  if(ret != KVS_SUCCESS)
    *keyspace_id = _INVALID_USER_KEYSPACE_ID;

  free(conts);
  return ret;
}

keyspace_id_t _search_an_avaliable_keyspace_id(const cont_list* conts) {
  keyspace_id_t keyspace_id = USER_DATA_KEYSPACE_START_ID;
  /* TODO when KVSSD support more than two containers, add code to find a keyspace id, that 
     *  not used, that is to say not in the inputted containers list(#conts) .
     */ 
  return keyspace_id;
}
bool _contain_keyspace(const cont_list* conts, keyspace_id_t ks_id) {
  for(uint8_t idx = 0; idx < conts->cont_num; idx++){
    if(ks_id == conts->entries[idx].keyspace_id) {
      return true;
    }
  }
  return false;
}

kvs_result _add_to_container_list(kvs_device_handle dev_hd,
  const char *name, keyspace_id_t *keyspace_id/*in and out param*/) {
  kvs_result ret = KVS_SUCCESS;
  //check whether key space has exist or has reach max number supported
  cont_list* conts = (cont_list*)calloc(1, sizeof(cont_list));
  if(!conts)
    return KVS_ERR_HEAP_ALLOC_FAILURE;

  conts->cont_num = 0;
  ret = _retreive_container_list(dev_hd, conts);
  if(ret != KVS_SUCCESS) {
    free(conts);
    return ret;
  }
  if(conts->cont_num >= NR_MAX_CONT) {
    free(conts);
    fprintf(stderr, "Max container number %d has reached", NR_MAX_CONT);
    fprintf(stderr, " add container %s failed.\n", name);
    return KVS_ERR_CONT_MAX;
  }
  for(uint8_t idx = 0; idx < conts->cont_num; idx++) {
    if(!strcmp(name, conts->entries[idx].names_buffer)) {
      free(conts);
      return KVS_ERR_CONT_EXIST;
    }
  }
  //get the keyspace id
  keyspace_id_t ks_id = *keyspace_id;
  if(ks_id == _INVALID_USER_KEYSPACE_ID) {
    ks_id = _search_an_avaliable_keyspace_id(conts);
  } else if(_contain_keyspace(conts, ks_id)){
    free(conts);
    return KVS_ERR_UNRECOVERED_ERROR; //this keyspace has been used
  }
  //add new continainer to container list
  snprintf(conts->entries[conts->cont_num].names_buffer,
    MAX_CONT_PATH_LEN, "%s", name);
  conts->entries[conts->cont_num].keyspace_id = ks_id;
  conts->cont_num++;

  // store contain list to ssd
  ret = _store_container_list(dev_hd, conts);
  if(ret == KVS_SUCCESS) {
    *keyspace_id = ks_id;
  }
  free(conts);
  return ret;
}

kvs_result kvs_open_device(const char *dev_path, kvs_device_handle *dev_hd) {
  int ret = 0;

  if((dev_path == NULL) || (dev_hd == NULL))
  {
    //fprintf(stderr, "Please specify a valid device path\n");
    return KVS_ERR_PARAM_INVALID;
  }
  int dev_path_len = strnlen(dev_path, MAX_DEV_PATH_LEN);
  if(dev_path_len == 0)
    return KVS_ERR_DEV_NOT_EXIST;
  if(dev_path_len > MAX_DEV_PATH_LEN - 1)
    return KVS_ERR_DEV_PATH_TOO_LONG;
  if(_device_opened(dev_path))
    return KVS_ERR_DEV_ALREADY_OPENED;

  build_error_table();
  if (!g_env.initialized) {
    WRITE_WARNING(
		  "the library is not properly configured: please run kvs_init_env() first\n");
    return KVS_ERR_ENV_NOT_INITIALIZED;
  }
  kvs_device_handle user_dev = new _kvs_device_handle();
  if(user_dev == NULL){
    WRITE_ERR("Memory is not enough,malloc failed!\n");
    return KVS_ERR_MEMORY_MALLOC_FAIL;
  }
  kv_device_priv *dev  = _find_local_device_from_path(dev_path, &(g_env.list_devices));
  if (dev == NULL) {
    WRITE_ERR("can't find the device: %s\n", dev_path);
    delete user_dev;
    return KVS_ERR_DEV_NOT_EXIST;
  }
  
  dev->isopened = true;
  user_dev->dev = dev;
  user_dev->driver = _select_driver(dev);

#if defined WITH_SPDK
  if(dev->isspdkdev){
    int curr_dev = g_env.udd_option.num_devices;
    uint64_t sq_core = g_env.udd_option.core_masks[curr_dev];
    uint64_t cq_core = g_env.udd_option.cq_masks[curr_dev];
    ret = user_dev->driver->init(dev_path, g_env.udd_option.syncio/*!g_env.use_async*/, sq_core, cq_core, g_env.udd_option.mem_size_mb, g_env.queuedepth);
    g_env.udd_option.num_devices++;
  } else {
    fprintf(stderr, "WRN: Please specify spdk device path properly\n");
    exit(1);
  }
#else
  if(dev->isemul || dev->iskerneldev)
    ret = user_dev->driver->init(dev_path, g_env.configfile, g_env.queuedepth,
      g_env.is_polling);
  if(ret != KVS_SUCCESS) {
    delete user_dev;
    return (kvs_result)ret;
  }

#endif
  user_dev->dev_path = (char*)malloc(strlen(dev_path)+1);
  if(user_dev->dev_path == NULL){
    delete user_dev;
    return KVS_ERR_MEMORY_MALLOC_FAIL;
  }
  snprintf(user_dev->dev_path, (strlen(dev_path)+1), "%s", dev_path);
  g_env.open_devices.push_back(user_dev);

  //create meta data key space
  kvs_container_handle cont_handle = 
    (kvs_container_handle)malloc(sizeof(struct _kvs_container_handle));
  if(!cont_handle) {
    user_dev->meta_cont_hd = NULL;
    g_env.open_devices.remove(user_dev);
    kvs_close_device(user_dev);
    *dev_hd = NULL;
    return KVS_ERR_MEMORY_MALLOC_FAIL;
  }
  user_dev->meta_cont_hd = cont_handle;
  cont_handle->keyspace_id = META_DATA_KEYSPACE_ID;
  cont_handle->dev = user_dev;
  snprintf(cont_handle->name, sizeof(cont_handle->name), "%s", "meta_data_keyspace");

  *dev_hd = user_dev;
  return (kvs_result)ret;
}

kvs_result kvs_close_device(kvs_device_handle user_dev) {
  if(user_dev == NULL) {
    return KVS_ERR_PARAM_INVALID;
  }
  if (!_device_opened(user_dev)) {
    return KVS_ERR_DEV_NOT_OPENED;
  }

  //free all opened container handle in this device
  if(user_dev->meta_cont_hd)
    free(user_dev->meta_cont_hd);

  for (const auto &t : user_dev->open_cont_hds) {
    _remove_container_from_g_env(t);
    free(t);
  }
  
  delete user_dev->driver;
  delete user_dev->dev;
  g_env.open_devices.remove(user_dev);
  free(user_dev->dev_path);
  delete user_dev;
  return KVS_SUCCESS;
}

kvs_result kvs_create_container(kvs_device_handle dev_hd, const char *name,
  uint64_t size, const kvs_container_context *ctx) {
  if((dev_hd == NULL) || (name == NULL)) {
    return KVS_ERR_PARAM_INVALID;
  }
  if(*name == '\0'){
    return KVS_ERR_CONT_NAME;
  }
  uint32_t name_len = strnlen(name, MAX_CONT_PATH_LEN) + 1;
  if(name_len > MAX_CONT_PATH_LEN) {
    return KVS_ERR_CONT_PATH_TOO_LONG;
  }
  if (!_device_opened(dev_hd)) {
    return KVS_ERR_DEV_NOT_EXIST;
  }
  // Fix SS-563: If the kvs_container_option is not equal to KVS_KEY_ORDER_NONE,
  // return KVS_ERR_OPTION_INVALID
  if (ctx->option.ordering != KVS_KEY_ORDER_NONE) {
    fprintf(stderr, "Do not support key order %d!\n",ctx->option.ordering);
    return KVS_ERR_OPTION_INVALID;
  }
  keyspace_id_t keyspace_id = _INVALID_USER_KEYSPACE_ID;
  kvs_result ret = _add_to_container_list(dev_hd, name, &keyspace_id);
  if(ret != KVS_SUCCESS) {
    return ret;
  }
  ret = _create_container_entry(dev_hd, name, keyspace_id, size, ctx);
  if(ret != KVS_SUCCESS) {
    _remove_from_container_list(dev_hd, name, &keyspace_id);
    return ret;
  }
  return ret;
}

kvs_result kvs_delete_container(kvs_device_handle dev_hd,
  const char *cont_name) {
  if((dev_hd == NULL) || (cont_name == NULL)) {
    return KVS_ERR_PARAM_INVALID;
  }
  if (!_device_opened(dev_hd)) {
    return KVS_ERR_DEV_NOT_EXIST;
  }
  //before delete container, container should in close state
  if (_container_opened(dev_hd, cont_name)) {
    return KVS_ERR_CONT_OPEN;
  }

  kvs_result ret = KVS_SUCCESS;
  //remove container from container list
  keyspace_id_t keyspace_id_removed = _INVALID_USER_KEYSPACE_ID;
  ret = _remove_from_container_list(dev_hd, cont_name, &keyspace_id_removed);
  if(ret != KVS_SUCCESS) {
    return ret;
  }
  //delete container entry, if delete failed, should recover original state
  ret = _delete_container_entry(dev_hd, cont_name);
  if(ret != KVS_SUCCESS) {
    _add_to_container_list(dev_hd, cont_name, &keyspace_id_removed);
    return ret;
  }
  return KVS_SUCCESS;
}

kvs_result kvs_open_container(kvs_device_handle dev_hd, const char* name, kvs_container_handle *cont_hd) {
  if((dev_hd == NULL) || (name == NULL) || (cont_hd == NULL)) {
    return KVS_ERR_PARAM_INVALID;
  }
  if(*name == '\0') {
    return KVS_ERR_CONT_NAME;
  }
  if (!_device_opened(dev_hd)) {
    return KVS_ERR_DEV_NOT_EXIST;
  } 
  if (_container_opened(dev_hd, name)) {
    return KVS_ERR_CONT_OPEN;
  }

  //check container exist
  uint8_t exist = 0;
  kvs_result ret = _exist_container_entry(dev_hd, name, &exist);
  if(ret != KVS_SUCCESS) {
    fprintf(stderr, "Check container exist failed. error code:0x%x.\n", ret);
    return ret;
  }
  if(!exist) {
    return KVS_ERR_CONT_NOT_EXIST;
  }
  //open container
  kvs_container_handle cont_handle = (kvs_container_handle)malloc(
    sizeof(struct _kvs_container_handle));
  if(!cont_handle) {
    return KVS_ERR_MEMORY_MALLOC_FAIL;
  }
  cont_handle->dev = dev_hd;
  snprintf(cont_handle->name, sizeof(cont_handle->name), "%s", name);

  ret = _open_container(cont_handle);
  if(ret != KVS_SUCCESS) {
    fprintf(stderr, "Update container state failed. error code:0x%x.\n", ret);
    free(cont_handle);
    return ret;
  }

  //dev_hd->driver->open_containers.push_back(cont_handle);
  dev_hd->open_cont_hds.push_back(cont_handle);
  g_env.list_open_container.push_back(cont_handle);
  *cont_hd = cont_handle;

  return KVS_SUCCESS;
}

kvs_result kvs_close_container(kvs_container_handle cont_hd){
  kvs_result ret = _check_container_handle(cont_hd);
  if (ret!=KVS_SUCCESS) {
    return ret;
  }

  ret = _close_container(cont_hd);
  if(ret != KVS_SUCCESS) {
    fprintf(stderr, "Close container failed. error code:0x%x-%s.\n", ret,
      kvs_errstr(ret));
    return ret;
  }
  kvs_device_handle dev_hd = cont_hd->dev;
  dev_hd->open_cont_hds.remove(cont_hd);
  g_env.list_open_container.remove(cont_hd);

  free(cont_hd);
  return ret;
}

kvs_result kvs_get_container_info(kvs_container_handle cont_hd,
  kvs_container *cont) {
  if(cont == NULL) {
    return KVS_ERR_PARAM_INVALID;
  }
  kvs_result ret = _check_container_handle(cont_hd);
  if (ret != KVS_SUCCESS) {
    return ret;
  }

  cont_metadata cont_meta = {0, 0, 0, 0, 0, 0, cont_hd->name};
  ret = _retrieve_container_metadata(cont_hd->dev, &cont_meta);
  if(ret != KVS_SUCCESS)
    return ret;
  cont->opened = cont_meta.opened;
  cont->capacity = cont_meta.capacity;
  cont->free_size = cont_meta.free_size;
  cont->count = cont_meta.kv_count;
  cont->scale = (cont_meta.free_size * 0.1)/(cont_meta.capacity * 0.1) * 100;
  cont->name->name_len = strnlen(cont_hd->name,MAX_CONT_PATH_LEN);
  snprintf(cont->name->name, cont->name->name_len + 1, "%s", cont_hd->name);
  return ret;
}

kvs_result kvs_list_containers(kvs_device_handle dev_hd, uint32_t index,
  uint32_t buffer_size, kvs_container_name *names, uint32_t *cont_cnt) {
  kvs_result ret = KVS_SUCCESS;
  if((dev_hd == NULL) || (names == NULL) || (cont_cnt == NULL)) {
    return KVS_ERR_PARAM_INVALID;
  }
  if(index < 1 ||  index > NR_MAX_CONT) {
    WRITE_ERR("Index of container/keyspace should be start form 1 to less or equal MAX container number!\n");
    return KVS_ERR_CONT_INDEX;
  }
  if (!_device_opened(dev_hd)) {
    return KVS_ERR_DEV_NOT_EXIST;
  }

  *cont_cnt = 0;
  cont_list* conts = (cont_list*)calloc(1, sizeof(cont_list));
  if(!conts)
    return KVS_ERR_HEAP_ALLOC_FAILURE;
  conts->cont_num = 0;
  ret = _retreive_container_list(dev_hd, conts);
  if(ret != KVS_SUCCESS) {
    free(conts);
    return ret;
  }
  if(index > conts->cont_num && conts->cont_num != 0) {
    WRITE_ERR("Index of container/keyspace inputted is too bigger.\n");
    return KVS_ERR_CONT_INDEX;
  }

  uint32_t items_buff_cnt = buffer_size/sizeof(kvs_container_name);
   *cont_cnt = 0;
  for(uint8_t idx = index - 1; idx < conts->cont_num; idx++) {//index start from 1
    if(*cont_cnt >= items_buff_cnt) {
      if(!items_buff_cnt) {
        WRITE_ERR("At least one container to read, buffer inputted is empty\n");
        ret = KVS_ERR_BUFFER_SMALL;
      }
      break; //buffer exhausted 
    }
    uint32_t len = strlen(conts->entries[idx].names_buffer) + 1;
    if(len > names[idx].name_len) {
      WRITE_ERR("The buffer that used to store name is too small.\n");
      return KVS_ERR_BUFFER_SMALL;
    }
    snprintf(names[idx].name, names[idx].name_len, "%s",
      conts->entries[idx].names_buffer);
    names[idx].name_len = len; //include end '\0'
    *cont_cnt += 1;
  }

  free(conts);
  return ret;
}

kvs_result kvs_get_tuple_info (kvs_container_handle cont_hd, const kvs_key *key, kvs_tuple_info *info) {
  int ret = _check_container_handle(cont_hd);
  if (ret!=KVS_SUCCESS) {
    return (kvs_result)ret;
  }

  if(key == NULL || info == NULL) {
    return KVS_ERR_PARAM_INVALID;
  }
  
  uint32_t vlen = 32;
  char *value = (char*)kvs_malloc(vlen, 4096);
  
  kvs_retrieve_option option;
  memset(&option, 0, sizeof(kvs_retrieve_option));
  option.kvs_retrieve_decompress = false;
  option.kvs_retrieve_delete = false;
  const kvs_retrieve_context ret_ctx = {option, 0, 0};
  kvs_value kvsvalue = { value, vlen , 0, 0 /*offset */};

  ret = kvs_retrieve_tuple(cont_hd, key, &kvsvalue, &ret_ctx);
  if(ret == KVS_ERR_BUFFER_SMALL) {
    // for kvs_get_tuple_info only get value length, don't care value content
    // fprintf(stderr, "kvs_retrieve_tuple(): KVS_ERR_BUFFER_SMALL (Key: %s with buffer_len = %d, actual vlen = %d)\n", (char *) key->key, kvsvalue.length, kvsvalue.actual_value_size);
    ret = KVS_SUCCESS;
  } 

  if(ret != KVS_SUCCESS) {
    fprintf(stderr, "get_tuple_info failed: key= %s error= 0x%x - %s\n", (char *) key->key, ret, kvs_errstr(ret));
  } else {
    info->key_length = key->length;
    info->value_length = kvsvalue.actual_value_size;
    memcpy(info->key, key->key, key->length);
    //fprintf(stdout, "get_tuple_info: %s with vlen = %d, actual vlen = %d \n", (char *) key->key, kvsvalue.length, kvsvalue.actual_value_size);
  }
  if(value) kvs_free(value);
  
  return (kvs_result)ret;
}

kvs_result kvs_store_tuple(kvs_container_handle cont_hd, const kvs_key *key,
		const kvs_value *value, const kvs_store_context *ctx) {
  int ret = _check_container_handle(cont_hd);
  if (ret!=KVS_SUCCESS) {
    return (kvs_result)ret;
  }

  if((key == NULL) || (value == NULL) || (ctx == NULL)) {
    return KVS_ERR_PARAM_INVALID;
  }

  ret = validate_request(key, value);
  if(ret)
    return (kvs_result)ret;

  ret = cont_hd->dev->driver->store_tuple(cont_hd, key, value,
    ctx->option, ctx->private1, ctx->private2, 1, 0);
  return (kvs_result)ret;
}

kvs_result kvs_store_tuple_async(kvs_container_handle cont_hd, const kvs_key *key,
				 const kvs_value *value, const kvs_store_context *ctx,
				 kvs_callback_function cbfn) {
  int ret = _check_container_handle(cont_hd);
  if (ret!=KVS_SUCCESS) {
    return (kvs_result)ret;
  }

  if(key == NULL || value == NULL || ctx == NULL || cbfn == NULL)
    return KVS_ERR_PARAM_INVALID;

  ret = validate_request(key, value);
  if(ret)
    return (kvs_result)ret;

  ret = cont_hd->dev->driver->store_tuple(cont_hd, key, value,
    ctx->option, ctx->private1, ctx->private2, 0, cbfn);
  return (kvs_result)ret;
}


kvs_result kvs_retrieve_tuple(kvs_container_handle cont_hd, const kvs_key *key,
			      kvs_value *value, const kvs_retrieve_context *ctx) {
  int ret = _check_container_handle(cont_hd);
  if (ret!=KVS_SUCCESS) {
    return (kvs_result)ret;
  }
  if((key == NULL) || (value == NULL) || (ctx == NULL)) {
    return KVS_ERR_PARAM_INVALID;
  }
  ret = validate_request(key, value);
  if(ret)
    return (kvs_result)ret;
  if (value->length & (KVS_VALUE_LENGTH_ALIGNMENT_UNIT - 1))
      return KVS_ERR_VALUE_LENGTH_MISALIGNED;

  ret = cont_hd->dev->driver->retrieve_tuple(cont_hd, key, value,
    ctx->option, ctx->private1, ctx->private2, 1, 0);
  return (kvs_result)ret;
}

kvs_result kvs_retrieve_tuple_async(kvs_container_handle cont_hd, const kvs_key *key,
				    kvs_value *value, const kvs_retrieve_context *ctx,
				    kvs_callback_function cbfn) {
  int ret = _check_container_handle(cont_hd);
  if (ret!=KVS_SUCCESS) {
    return (kvs_result)ret;
  }
  if(key == NULL || value == NULL || ctx == NULL || cbfn == NULL)
    return KVS_ERR_PARAM_INVALID;
  ret = validate_request(key, value);
  if(ret)
    return (kvs_result)ret;
  if (value->length & (KVS_VALUE_LENGTH_ALIGNMENT_UNIT - 1))
      return KVS_ERR_VALUE_LENGTH_MISALIGNED;

  ret = cont_hd->dev->driver->retrieve_tuple(cont_hd, key, value,
    ctx->option, ctx->private1, ctx->private2, 0, cbfn);
  return (kvs_result)ret;
}

kvs_result kvs_delete_tuple(kvs_container_handle cont_hd, const kvs_key *key,
		const kvs_delete_context *ctx) {
  int ret = _check_container_handle(cont_hd);
  if (ret!=KVS_SUCCESS) {
    return (kvs_result)ret;
  }
  if((key == NULL) || (ctx == NULL))
    return KVS_ERR_PARAM_INVALID;

  ret = validate_request(key, 0);
  if(ret)
    return (kvs_result)ret;

  ret = cont_hd->dev->driver->delete_tuple(cont_hd, key,
    ctx->option, ctx->private1, ctx->private2, 1, 0);
  return (kvs_result)ret;
}

kvs_result kvs_delete_tuple_async(kvs_container_handle cont_hd, const kvs_key* key,
				  const kvs_delete_context* ctx, kvs_callback_function cbfn) {
  int ret = _check_container_handle(cont_hd);
  if (ret!=KVS_SUCCESS) {
    return (kvs_result)ret;
  }
  if((key == NULL) || (ctx == NULL) || (cbfn == NULL))
    return KVS_ERR_PARAM_INVALID;

  ret = validate_request(key, 0);
  if(ret) return (kvs_result)ret;
  
  ret = cont_hd->dev->driver->delete_tuple(cont_hd, key,
    ctx->option, ctx->private1, ctx->private2, 0, cbfn);
  return (kvs_result)ret;
}

kvs_result kvs_exist_tuples(kvs_container_handle cont_hd, uint32_t key_cnt, const kvs_key *keys, uint32_t buffer_size, uint8_t *result_buffer, const kvs_exist_context *ctx) {
  int ret = _check_container_handle(cont_hd);
  if (ret!=KVS_SUCCESS) {
    return (kvs_result)ret;
  }
  if((keys == NULL) || (result_buffer == NULL) || (ctx == NULL) || (key_cnt <= 0)) {
    return KVS_ERR_PARAM_INVALID;
  }
  if(buffer_size <= 0)
      return KVS_ERR_BUFFER_SMALL;

  ret = validate_request(keys, 0);
  if(ret) return (kvs_result)ret;
  
  ret = cont_hd->dev->driver->exist_tuple(cont_hd, key_cnt, keys,
    buffer_size, result_buffer, ctx->private1, ctx->private2, 1, 0); 
  return (kvs_result)ret;

}

kvs_result kvs_exist_tuples_async(kvs_container_handle cont_hd, uint32_t key_cnt, const kvs_key *keys, uint32_t buffer_size, 
                                  uint8_t *result_buffer, const kvs_exist_context *ctx, kvs_callback_function cbfn) {
  int ret = _check_container_handle(cont_hd);
  if (ret!=KVS_SUCCESS) {
    return (kvs_result)ret;
  }
  if(keys == NULL || result_buffer == NULL || (key_cnt <= 0) || ctx == NULL || cbfn == NULL)
    return KVS_ERR_PARAM_INVALID;
  if(buffer_size <= 0)
    return KVS_ERR_BUFFER_SMALL;

  ret = validate_request(keys, 0);
  if(ret) return (kvs_result)ret;
  
  ret = cont_hd->dev->driver->exist_tuple(cont_hd, key_cnt, keys,
    buffer_size, result_buffer, ctx->private1, ctx->private2, 0, cbfn);

  return (kvs_result)ret;
}

kvs_result kvs_open_iterator(kvs_container_handle cont_hd, const kvs_iterator_context *ctx,
			  kvs_iterator_handle *iter_hd) {
  int ret = _check_container_handle(cont_hd);
  if (ret != KVS_SUCCESS) {
    return (kvs_result)ret;
  }
  if(ctx == NULL || iter_hd == NULL) {
    return KVS_ERR_PARAM_INVALID;
  }
  if(!_is_valid_bitmask(ctx->bitmask))
    return KVS_ERR_ITERATOR_COND_INVALID;

  ret = cont_hd->dev->driver->open_iterator(cont_hd, ctx->option,
    ctx->bitmask, ctx->bit_pattern, iter_hd);
  return (kvs_result)ret;
}

kvs_result kvs_close_iterator(kvs_container_handle cont_hd, kvs_iterator_handle hiter,
			   const kvs_iterator_context *ctx) {
  int ret = _check_container_handle(cont_hd);
  if (ret != KVS_SUCCESS) {
    return (kvs_result)ret;
  }
  if(ctx == NULL){
    return KVS_ERR_PARAM_INVALID;
  }

  ret =  cont_hd->dev->driver->close_iterator(cont_hd, hiter);
  return (kvs_result)ret;
}


kvs_result kvs_close_iterator_all(kvs_container_handle cont_hd) {
  int ret = _check_container_handle(cont_hd);
  if (ret != KVS_SUCCESS) {
    return (kvs_result)ret;
  }

  ret = cont_hd->dev->driver->close_iterator_all(cont_hd);
  return (kvs_result)ret;
}

kvs_result kvs_list_iterators(kvs_container_handle cont_hd, kvs_iterator_info *kvs_iters, int count) {
  int ret = _check_container_handle(cont_hd);
  if (ret != KVS_SUCCESS) {
    return (kvs_result)ret;
  }
  if(kvs_iters == NULL)
    return KVS_ERR_PARAM_INVALID;
  if(count < 1 || count > KVS_MAX_ITERATE_HANDLE)
    return KVS_ERR_ITERATOR_NUM_OUT_RANGE;

  ret = cont_hd->dev->driver->list_iterators(cont_hd, kvs_iters,
    count);
  return (kvs_result)ret;
}

kvs_result kvs_iterator_next(kvs_container_handle cont_hd, kvs_iterator_handle hiter,
			  kvs_iterator_list *iter_list, const kvs_iterator_context *ctx) {
  int ret = _check_container_handle(cont_hd);
  if (ret != KVS_SUCCESS) {
    return (kvs_result)ret;
  }
  if(iter_list == NULL || iter_list->it_list == NULL || ctx == NULL)
    return KVS_ERR_PARAM_INVALID;
  if(iter_list->size!=KVS_ITERATOR_BUFFER_SIZE){
    return KVS_ERR_ITERATOR_BUFFER_SIZE;
  }

  ret = cont_hd->dev->driver->iterator_next(cont_hd, hiter, iter_list, ctx->private1, ctx->private2, 1, 0);
  return (kvs_result)ret;
}

kvs_result kvs_iterator_next_async(kvs_container_handle cont_hd, kvs_iterator_handle hiter,
				   kvs_iterator_list *iter_list, const kvs_iterator_context *ctx,
				   kvs_callback_function cbfn) {
  int ret = _check_container_handle(cont_hd);
  if (ret != KVS_SUCCESS) {
    return (kvs_result)ret;
  }
  if(iter_list == NULL || iter_list->it_list == NULL|| ctx == NULL || cbfn == NULL)
    return KVS_ERR_PARAM_INVALID;
  if(iter_list->size!=KVS_ITERATOR_BUFFER_SIZE){
    return KVS_ERR_ITERATOR_BUFFER_SIZE;
  }

  ret = cont_hd->dev->driver->iterator_next(cont_hd, hiter, iter_list, ctx->private1, ctx->private2, 0, cbfn);
  return (kvs_result)ret;
}

kvs_result kvs_get_device_waf(kvs_device_handle dev_hd, float *waf) {
  if(dev_hd == NULL || waf == NULL) {
    return KVS_ERR_PARAM_INVALID;
  }
  if (!_device_opened(dev_hd)) {
    return KVS_ERR_DEV_NOT_OPENED;
  }

  *waf = dev_hd->driver->get_waf();

  return KVS_SUCCESS;
}

kvs_result kvs_get_device_info(kvs_device_handle dev_hd, kvs_device *dev_info) {
  int ret;
  if((dev_hd == NULL) || (dev_info == NULL)) {
    return KVS_ERR_PARAM_INVALID;
  }
  if (!_device_opened(dev_hd)) {
    ret = KVS_ERR_DEV_NOT_OPENED;
  } else {  
    ret = dev_hd->driver->get_total_size(&dev_info->capacity);
    dev_info->unalloc_capacity = 0;
    dev_info->max_value_len = KVS_MAX_VALUE_LENGTH;
    dev_info->max_key_len = KVS_MAX_KEY_LENGTH;
    dev_info->optimal_value_len = KVS_OPTIMAL_VALUE_LENGTH;
  }
  return (kvs_result)ret;
}

kvs_result kvs_get_device_utilization(kvs_device_handle dev_hd, int32_t *dev_util) {
  int ret;
  if((dev_hd == NULL) || (dev_util == NULL)) {
    return KVS_ERR_PARAM_INVALID;
  }
  if (!_device_opened(dev_hd)) {
    ret = KVS_ERR_DEV_NOT_OPENED;
  } else {
    ret = dev_hd->driver->get_used_size(dev_util);
  }
  return (kvs_result)ret;
}

kvs_result kvs_get_device_capacity(kvs_device_handle dev_hd, int64_t *dev_capa) {
  int ret;
  if((dev_hd == NULL) || (dev_capa == NULL)) {
    return KVS_ERR_PARAM_INVALID;
  }
  if (!_device_opened(dev_hd)) {
    ret = KVS_ERR_DEV_NOT_OPENED;
  } else {
    ret = dev_hd->driver->get_total_size(dev_capa);
  }
  return (kvs_result)ret;
}

kvs_result kvs_get_min_key_length (kvs_device_handle dev_hd, int32_t *min_key_length){
  if((dev_hd == NULL) || (min_key_length == NULL)) {
    return KVS_ERR_PARAM_INVALID;
  }
  if(!_device_opened(dev_hd)){
    return KVS_ERR_DEV_NOT_OPENED;
  }
  *min_key_length = KVS_MIN_KEY_LENGTH;
  return KVS_SUCCESS;
}

kvs_result kvs_get_max_key_length (kvs_device_handle dev_hd, int32_t *max_key_length){
  if((dev_hd == NULL) || (max_key_length == NULL)) {
    return KVS_ERR_PARAM_INVALID;
  }
  if(!_device_opened(dev_hd)){
    return KVS_ERR_DEV_NOT_OPENED;
  }
  *max_key_length = KVS_MAX_KEY_LENGTH;
  return KVS_SUCCESS;
}

kvs_result kvs_get_min_value_length (kvs_device_handle dev_hd, int32_t *min_value_length){
  if((dev_hd == NULL) || (min_value_length == NULL)) {
    return KVS_ERR_PARAM_INVALID;
  }
  if(!_device_opened(dev_hd)){
    return KVS_ERR_DEV_NOT_OPENED;
  }
  *min_value_length = KVS_MIN_VALUE_LENGTH;
  return KVS_SUCCESS;
}

kvs_result kvs_get_max_value_length (kvs_device_handle dev_hd, int32_t *max_value_length){
  if((dev_hd == NULL) || (max_value_length == NULL)) {
    return KVS_ERR_PARAM_INVALID;
  }
  if(!_device_opened(dev_hd)){
    return KVS_ERR_DEV_NOT_OPENED;
  }
  *max_value_length = KVS_MAX_VALUE_LENGTH;
  return KVS_SUCCESS;
}

kvs_result kvs_get_optimal_value_length (kvs_device_handle dev_hd, int32_t *opt_value_length){
  if((dev_hd == NULL) || (opt_value_length == NULL)) {
    return KVS_ERR_PARAM_INVALID;
  }
  if(!_device_opened(dev_hd)){
    return KVS_ERR_DEV_NOT_OPENED;
  }
  *opt_value_length = KVS_OPTIMAL_VALUE_LENGTH;
  return KVS_SUCCESS;
}


void *_kvs_zalloc(size_t size_bytes, size_t alignment, const char *file) {
  WRITE_LOG("kvs_zalloc size: %ld, align: %ld, from %s\n", size_bytes, alignment, file);
#if defined WITH_SPDK
  return (g_env.use_spdk) ?
    _udd_zalloc(size_bytes, alignment) : malloc(size_bytes);
#else
  return malloc(size_bytes);
#endif
}

void *_kvs_malloc(size_t size_bytes, size_t alignment, const char *file) {

  WRITE_LOG("kvs_malloc size: %ld, align: %ld, from %s\n", size_bytes, alignment, file);
#if defined WITH_SPDK
  return (g_env.use_spdk) ?
    _udd_malloc(size_bytes, alignment) : malloc(size_bytes);
#else
  return malloc(size_bytes);
#endif
}
void _kvs_free(void * buf, const char *file) {
  WRITE_LOG("kvs_free..\n");
#if defined WITH_SPDK
  return (g_env.use_spdk) ? _udd_free(buf) : free(buf);
#else
  return free(buf);
#endif
}

int32_t kvs_get_ioevents(kvs_container_handle cont_hd, int maxevents) {
  return cont_hd->dev->driver->process_completions(maxevents);
}
}// namespace api_private

