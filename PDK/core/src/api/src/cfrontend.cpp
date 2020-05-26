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
#include <string.h>
#include <map>
#include <list>
#include "kvs_utils.h"
#include "private_types.h"
#ifdef WITH_EMU
#include "kvemul.hpp"
#elif WITH_KDD
#include "kvkdd.hpp"
#else
#include "udd.hpp"
#include <private/uddenv.h>
#endif
//#include <regex>
#include "kv_config.hpp"

#define CFG_PATH  "../env_init.conf"

//the key of kv pair that store key spaces name list
const char* KEY_SPACE_LIST_KEY_NAME = "key_space_list"; 
//invalid keyspace id
const keyspace_id_t _INVALID_USER_KEYSPACE_ID = 0;  

pthread_mutex_t env_mutex = PTHREAD_MUTEX_INITIALIZER;

struct {
  bool initialized = false;
  bool use_spdk = false;
  int queuedepth;
  int is_polling = 0;
  int opened_device_num = 0;
  std::map<std::string, kv_device_priv *> list_devices;
  std::list<kvs_device_handle> open_devices;
  std::list<kvs_key_space_handle> list_open_ks;
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

#define stringify(name) # name
#define kvs_errstr(name) (errortable[name])
const char* errortable[] = {
  stringify(KVS_SUCCESS),
  stringify(KVS_ERR_BUFFER_SMALL),
  stringify(KVS_ERR_DEV_CAPAPCITY),
  stringify(KVS_ERR_DEV_NOT_EXIST),
  stringify(KVS_ERR_KS_CAPACITY),
  stringify(KVS_ERR_KS_EXIST),
  stringify(KVS_ERR_KS_INDEX),
  stringify(KVS_ERR_KS_NAME),
  stringify(KVS_ERR_KS_NOT_EXIST),
  stringify(KVS_ERR_KS_NOT_OPEN),
  stringify(KVS_ERR_KS_OPEN),
  stringify(KVS_ERR_ITERATOR_FILTER_INVALID),
  stringify(KVS_ERR_ITERATOR_MAX),
  stringify(KVS_ERR_ITERATOR_NOT_EXIST),
  stringify(KVS_ERR_ITERATOR_OPEN),
  stringify(KVS_ERR_KEY_LENGTH_INVALID),
  stringify(KVS_ERR_KEY_NOT_EXIST),
  stringify(KVS_ERR_OPTION_INVALID),
  stringify(KVS_ERR_PARAM_INVALID),
  stringify(KVS_ERR_SYS_IO),
  stringify(KVS_ERR_VALUE_LENGTH_INVALID),
  stringify(KVS_ERR_VALUE_OFFSET_INVALID),
  stringify(KVS_ERR_VALUE_OFFSET_MISALIGNED),
  stringify(KVS_ERR_VALUE_UPDATE_NOT_ALLOWED),
  stringify(KVS_ERR_DEV_NOT_OPENED),
};

void init_default_option(kvs_init_options &options) {
  memset(&options, 0, sizeof(kvs_init_options));
  options.memory.use_dpdk = 0;
  options.memory.dpdk_mastercoreid = -1;
  options.memory.nr_hugepages_per_socket = 1024;
  options.memory.socketmask = 0;

  options.aio.iocoremask = 0;
  options.aio.queuedepth = 64;
  const char* configfile = "../kvssd_emul.conf";
  options.emul_config_file = (char*)malloc(PATH_MAX);
  strncpy(options.emul_config_file, configfile, strlen(configfile) + 1);
  char* core;
  core = options.udd.core_mask_str;
  *core = '0';
  core = options.udd.cq_thread_mask;
  *core = '0';
  options.udd.mem_size_mb = 1024;
  options.udd.syncio = 1;
#ifdef WITH_SPDK
  options.memory.use_dpdk = 1;
#endif
}

void init_env_from_cfgfile(kvs_init_options &options) {
  std::string file_path(CFG_PATH);
  kvadi::kv_config cfg(file_path);

  int queue_depth = atoi(cfg.getkv("aio", "queue_depth").c_str());;
  options.aio.iocoremask = (uint64_t)atoi(cfg.getkv("aio", "iocoremask").c_str());
  options.aio.queuedepth = queue_depth == 0 ? options.aio.queuedepth : (uint32_t)queue_depth;
  std::string cfg_file_path = cfg.getkv("emu", "cfg_file");
  if (cfg_file_path != "") {
    strncpy(options.emul_config_file, cfg_file_path.c_str(), cfg_file_path.length() + 1);
  }
#ifdef WITH_SPDK
  options.memory.use_dpdk = 1;
  if (strcmp(cfg.getkv("udd", "core_mask_str").c_str(), ""))
    strcpy(options.udd.core_mask_str, cfg.getkv("udd", "core_mask_str").c_str());
  if (strcmp(cfg.getkv("udd", "cq_thread_mask").c_str(), ""))
    strcpy(options.udd.cq_thread_mask, cfg.getkv("udd", "cq_thread_mask").c_str());
  int mem_size = atoi(cfg.getkv("udd", "memory_size").c_str());
  options.udd.mem_size_mb = mem_size == 0 ? options.udd.mem_size_mb : (uint32_t)mem_size;
  if (strcmp(cfg.getkv("udd", "syncio").c_str(), ""))
    options.udd.syncio = cfg.getkv("udd", "syncio") == "0" ? 0 : 1;
#endif

  return;
}

void init_env_from_environ(kvs_init_options &options) {
  char* env_str;

  env_str = getenv("KVSSD_QUEUE_DEPTH");
  if (env_str) options.aio.queuedepth = (uint32_t)atoi(env_str);
  env_str = getenv("KVSSD_IOCOREMASK");
  if (env_str) options.aio.iocoremask = (uint64_t)atoi(env_str);
  env_str = getenv("KVSSD_EMU_CONFIGFILE");
  if (env_str) strncpy(options.emul_config_file, env_str, PATH_MAX);
#ifdef WITH_SPDK
  options.memory.use_dpdk = 1;
  env_str = getenv("KVSSD_COREMASK_STR");
  if (env_str) strncpy(options.udd.core_mask_str, env_str, 256);
  env_str = getenv("KVSSD_CQ_THREAD_MASK");
  if (env_str) strncpy(options.udd.cq_thread_mask, env_str, 256);
  env_str = getenv("KVSSD_UDD_MEMSIZE");
  if (env_str) options.udd.mem_size_mb = atoi(env_str);
  env_str = getenv("KVSSD_UDD_SYNC");
  if (env_str) options.udd.syncio = atoi(env_str);
#endif
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

kvs_result init_env() {
  kvs_result ret;
  kvs_init_options options;

  if (g_env.initialized)
    return KVS_SUCCESS;
  /* config file > environ var > default */
  init_default_option(options);
  init_env_from_environ(options);
  init_env_from_cfgfile(options);

  ret = kvs_init_env(&options);
  if (options.emul_config_file) free(options.emul_config_file);
  if (ret == KVS_SUCCESS)
    g_env.initialized = true;
  return ret;
}

bool _device_opened(const char* dev_path) {
  std::string dev(dev_path);
  for (auto &t : g_env.open_devices) {
    if (t->dev_path == dev) return true;
  }
  return false;
}

bool _device_opened(kvs_device_handle dev_hd){
  auto t  = find(g_env.open_devices.begin(), g_env.open_devices.end(), dev_hd);
  if (t == g_env.open_devices.end()){
    return false;
  }
  return true;
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

void _remove_key_space_from_g_env(kvs_key_space_handle ks_hd) {
  auto t = find(g_env.list_open_ks.begin(), g_env.list_open_ks.end(), ks_hd);
  if (t != g_env.list_open_ks.end()) {
    g_env.list_open_ks.erase(t);
  }
}

kvs_result _kvs_exit_env() {
  g_env.initialized = false;
  std::list<kvs_device_handle > clone(g_env.open_devices.begin(),
                                      g_env.open_devices.end());
  
  //fprintf(stderr, "KVSSD: Close %d unclosed devices\n", (int) clone.size());
  for (kvs_device_handle t : clone) {
      kvs_close_device(t);
  }
  
  return KVS_SUCCESS;
}

kvs_result kvs_open_device(char *URI, kvs_device_handle *dev_hd) {
  kvs_result ret;

  pthread_mutex_lock(&env_mutex);
  ret = init_env();
  if (ret != KVS_SUCCESS) {
    pthread_mutex_unlock(&env_mutex);
    return ret; 
  }
  if ((URI == NULL) || (dev_hd == NULL)) {
    pthread_mutex_unlock(&env_mutex);
    return KVS_ERR_PARAM_INVALID;
  }
  int dev_len = strnlen(URI, MAX_DEV_PATH_LEN);
  if (dev_len == 0 || dev_len > MAX_DEV_PATH_LEN - 1){
    pthread_mutex_unlock(&env_mutex);
    return KVS_ERR_PARAM_INVALID;
  }
  if (_device_opened(URI)){
    pthread_mutex_unlock(&env_mutex);
    return KVS_ERR_SYS_IO;
  }

  kvs_device_handle user_dev = new _kvs_device_handle();
  if (user_dev == NULL) {
    WRITE_ERR("Out of memory, malloc failed\n");
    pthread_mutex_unlock(&env_mutex);
    return KVS_ERR_SYS_IO;
  }
  kv_device_priv *dev = _find_local_device_from_path(URI, &(g_env.list_devices));
  if (dev == NULL) {
    WRITE_ERR("can't find the device: %s\n", URI);
    delete user_dev;
    pthread_mutex_unlock(&env_mutex);
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
    ret = (kvs_result)user_dev->driver->init(URI, g_env.udd_option.syncio/*!g_env.use_async*/, sq_core, cq_core, g_env.udd_option.mem_size_mb, g_env.queuedepth);
    g_env.udd_option.num_devices++;
  } else {
    fprintf(stderr, "WRN: Please specify spdk device path properly\n");
    exit(1);
  }
#else
  if(dev->isemul || dev->iskerneldev)
    ret = (kvs_result)user_dev->driver->init(URI, g_env.configfile, g_env.queuedepth,
      g_env.is_polling);
  if(ret != KVS_SUCCESS) {
    delete user_dev;
    pthread_mutex_unlock(&env_mutex);
    return ret;
  }
#endif
  user_dev->dev_path = (char*)malloc(strlen(URI) + 1);
  if (user_dev->dev_path == NULL) {
    delete user_dev;
    pthread_mutex_unlock(&env_mutex);
    return KVS_ERR_SYS_IO;
  }
  snprintf(user_dev->dev_path, strlen(URI) + 1, "%s", URI);
  g_env.open_devices.push_back(user_dev);

  //create meta data key space
  kvs_key_space_handle ks_handle = (kvs_key_space_handle)malloc(sizeof(struct _kvs_key_space_handle));
  if (!ks_handle) {
    user_dev->meta_ks_hd = NULL;
    g_env.open_devices.remove(user_dev);
    pthread_mutex_unlock(&env_mutex);
    kvs_close_device(user_dev);
    *dev_hd = NULL;
    return KVS_ERR_SYS_IO;
  }
  user_dev->meta_ks_hd = ks_handle;
  ks_handle->keyspace_id = META_DATA_KEYSPACE_ID;
  ks_handle->dev = user_dev;
  snprintf(ks_handle->name, sizeof(ks_handle->name), "%s", "meta_data_keyspace");
  *dev_hd = user_dev;

  ++g_env.opened_device_num;
  pthread_mutex_unlock(&env_mutex);
  return ret;
}

kvs_result kvs_get_device_info(kvs_device_handle dev_hd, kvs_device *dev_info) {
  kvs_result ret;
  if((dev_hd == NULL) || (dev_info == NULL)) {
    return KVS_ERR_PARAM_INVALID;
  }
  if (!_device_opened(dev_hd)) {
    ret = KVS_ERR_DEV_NOT_OPENED;
  } else {  
    ret = (kvs_result)dev_hd->driver->get_total_size(&dev_info->capacity);
    dev_info->unalloc_capacity = 0;
    dev_info->max_value_len = KVS_MAX_VALUE_LENGTH;
    dev_info->max_key_len = KVS_MAX_KEY_LENGTH;
    dev_info->optimal_value_len = KVS_OPTIMAL_VALUE_LENGTH;
  }
  return ret;
}

kvs_result kvs_close_device(kvs_device_handle dev_hd) {
  pthread_mutex_lock(&env_mutex);
  if(dev_hd == NULL) {
    pthread_mutex_unlock(&env_mutex);
    return KVS_ERR_PARAM_INVALID;
  }
  if (!_device_opened(dev_hd)) {
    pthread_mutex_unlock(&env_mutex);
    return KVS_ERR_DEV_NOT_OPENED;
  }

  //free all opened key space handle in this device
  if(dev_hd->meta_ks_hd)
    free(dev_hd->meta_ks_hd);

  for (const auto &t : dev_hd->open_ks_hds) {
    _remove_key_space_from_g_env(t);
    free(t);
  }
  
  delete dev_hd->driver;
  delete dev_hd->dev;
  g_env.open_devices.remove(dev_hd);
  free(dev_hd->dev_path);
  delete dev_hd;

  if (--g_env.opened_device_num == 0) {
    _kvs_exit_env();
    g_env.initialized = false;
  }
  pthread_mutex_unlock(&env_mutex);
  return KVS_SUCCESS;
}

kvs_result kvs_get_device_capacity(kvs_device_handle dev_hd, 
  uint64_t *dev_capa) {
  kvs_result ret;
  if((dev_hd == NULL) || (dev_capa == NULL)) {
    return KVS_ERR_PARAM_INVALID;
  }
  if (!_device_opened(dev_hd)) {
    ret = KVS_ERR_DEV_NOT_OPENED;
  } else {
    ret = (kvs_result)dev_hd->driver->get_total_size(dev_capa);
  }
  return ret;
}

kvs_result kvs_get_device_utilization(kvs_device_handle dev_hd,
  uint32_t *dev_utilization) {
  kvs_result ret;
  if((dev_hd == NULL) || (dev_utilization == NULL)) {
    return KVS_ERR_PARAM_INVALID;
  }
  if (!_device_opened(dev_hd)) {
    ret = KVS_ERR_DEV_NOT_OPENED;
  } else {
    ret = (kvs_result)dev_hd->driver->get_used_size(dev_utilization);
  }
  return ret;
}

kvs_result kvs_get_min_key_length (kvs_device_handle dev_hd,
  uint32_t *min_key_length) {
  if((dev_hd == NULL) || (min_key_length == NULL)) {
    return KVS_ERR_PARAM_INVALID;
  }
  if(!_device_opened(dev_hd)){
    return KVS_ERR_DEV_NOT_OPENED;
  }
  *min_key_length = KVS_MIN_KEY_LENGTH;
  return KVS_SUCCESS;
}

kvs_result kvs_get_max_key_length (kvs_device_handle dev_hd,
  uint32_t *max_key_length) {
  if((dev_hd == NULL) || (max_key_length == NULL)) {
    return KVS_ERR_PARAM_INVALID;
  }
  if(!_device_opened(dev_hd)){
    return KVS_ERR_DEV_NOT_OPENED;
  }
  *max_key_length = KVS_MAX_KEY_LENGTH;
  return KVS_SUCCESS;
}

kvs_result kvs_get_min_value_length (kvs_device_handle dev_hd,
  uint32_t *min_value_length) {
  if((dev_hd == NULL) || (min_value_length == NULL)) {
    return KVS_ERR_PARAM_INVALID;
  }
  if(!_device_opened(dev_hd)){
    return KVS_ERR_DEV_NOT_OPENED;
  }
  *min_value_length = KVS_MIN_VALUE_LENGTH;
  return KVS_SUCCESS;
}

kvs_result kvs_get_max_value_length (kvs_device_handle dev_hd,
  uint32_t *max_value_length) {
  if((dev_hd == NULL) || (max_value_length == NULL)) {
    return KVS_ERR_PARAM_INVALID;
  }
  if(!_device_opened(dev_hd)){
    return KVS_ERR_DEV_NOT_OPENED;
  }
  *max_value_length = KVS_MAX_VALUE_LENGTH;
  return KVS_SUCCESS;
}

kvs_result kvs_get_optimal_value_length (kvs_device_handle dev_hd,
  uint32_t *opt_value_length) {
  if((dev_hd == NULL) || (opt_value_length == NULL)) {
    return KVS_ERR_PARAM_INVALID;
  }
  if(!_device_opened(dev_hd)){
    return KVS_ERR_DEV_NOT_OPENED;
  }
  *opt_value_length = KVS_OPTIMAL_VALUE_LENGTH;
  return KVS_SUCCESS;
}

bool _key_space_opened(kvs_device_handle dev_hd, const char* name) {
  if (dev_hd->open_ks_hds.empty()) return false;
  for (const auto &t : dev_hd->open_ks_hds) {
    if (strcmp(t->name, name) == 0) return true;
  }
  return false;
}

bool _key_space_opened(kvs_key_space_handle ks_hd) {
  auto t = find(g_env.list_open_ks.begin(), g_env.list_open_ks.end(), ks_hd);
  if (t == g_env.list_open_ks.end()) return false;
  else return true;
}

inline kvs_result _check_key_space_handle(kvs_key_space_handle ks_hd) {
  if (ks_hd == NULL) return KVS_ERR_PARAM_INVALID;
  if (!_key_space_opened(ks_hd)) return KVS_ERR_KS_NOT_OPEN;
  if ((ks_hd->dev == NULL) || (ks_hd->dev->driver == NULL))
    return KVS_ERR_PARAM_INVALID;
  if (!_device_opened(ks_hd->dev)) return KVS_ERR_DEV_NOT_OPENED;
  
  return KVS_SUCCESS;
}

static void filter2context(kvs_key_group_filter* fltr, uint32_t* bitmask, uint32_t* bit_pattern) {
  *bitmask = (uint32_t)fltr->bitmask[3] | ((uint32_t)fltr->bitmask[2]) << 8 |
    ((uint32_t)fltr->bitmask[1]) << 16 | ((uint32_t)fltr->bitmask[0]) << 24;
  *bit_pattern = (uint32_t)fltr->bit_pattern[3] |
    ((uint32_t)fltr->bit_pattern[2]) << 8 |
    ((uint32_t)fltr->bit_pattern[1]) << 16 |
    ((uint8_t)fltr->bit_pattern[0]) << 24;
}

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

void _metadata_keyspace_aio_complete_handle(kvs_postprocess_context *ioctx) {
  uint32_t *complete_ptr = (uint32_t*)ioctx->private1;
  *complete_ptr = 1;
  kvs_result *result_ptr = (kvs_result*)ioctx->private2;
  *result_ptr = ioctx->result;
  if(ioctx->result_buffer.list) 
    free(ioctx->result_buffer.list);
}

kvs_result _sync_io_to_meta_keyspace(kvs_device_handle dev_hd, const kvs_key* key,
    kvs_value *value, void* io_option, kvs_context io_op) {
  kvs_result ret = KVS_SUCCESS;
  bool syncio = true;
  kvs_postprocess_function cbfn = NULL;
  /* as async_completed and async_result may be modify by other cpu(in complete handle),
        so should be volatile type */
  volatile uint32_t async_completed = 0;
  volatile kvs_result async_result = KVS_SUCCESS;

  /*If use kdd or emulator sync store can be use always.
       If use UDD should use corresponding interface, 
       sync mode: sync interface, async mode: async interface*/
#ifdef WITH_SPDK
  if (!g_env.udd_option.syncio) {//uses async interface
    syncio = false;
    cbfn = _metadata_keyspace_aio_complete_handle;
  }
#endif
  kvs_key_space_handle ks_hd = dev_hd->meta_ks_hd;
  if (io_op == KVS_CMD_STORE) {
    ret = (kvs_result)dev_hd->meta_ks_hd->dev->driver->store_tuple(ks_hd,
        key, value, *((kvs_option_store*)io_option), (void*)&async_completed,
        (void*)&async_result, syncio, cbfn);
  } else if (io_op == KVS_CMD_RETRIEVE) {
    ret = (kvs_result)dev_hd->meta_ks_hd->dev->driver->retrieve_tuple(
      ks_hd, key, value, *((kvs_option_retrieve*)io_option), (void*)&async_completed,
      (void*)&async_result, syncio, cbfn);
  }else if(io_op == KVS_CMD_DELETE){
    ret = (kvs_result)dev_hd->meta_ks_hd->dev->driver->delete_tuple(
      ks_hd, key, *((kvs_option_delete*)io_option), (void*)&async_completed,
      (void*)&async_result, syncio, cbfn);
  }else {
    fprintf(stderr, "KVAPI internal error unsupported aio type:%d passed.\n",
      io_op);
    return KVS_ERR_SYS_IO;
  }
  if (ret != KVS_SUCCESS) return ret;
  if (!syncio) {
    while (true) {
      if (async_completed) break;
    }
  }
  return ret;
}

kvs_result _exist_key_space_entry(kvs_device_handle dev_hd, const char* name,
    uint8_t* exist_buffer) {
  kvs_result ret = KVS_SUCCESS;
  uint16_t klen = strlen(name);
  char* key = (char*)kvs_zalloc(klen, PAGE_ALIGN);
  if (!key) return KVS_ERR_SYS_IO;

  snprintf(key, klen, "%s", name);
  kvs_key kvskey = {key, klen};
  *exist_buffer = 0;
  kvs_value kvsvalue = {exist_buffer, 1, 0, 0};
  volatile uint32_t async_completed = 0; 
  volatile kvs_result async_result = KVS_SUCCESS; 
  kvs_postprocess_function cbfn = NULL; 
  bool syncio = true;
  
  kvs_exist_list *list = (kvs_exist_list*)malloc(sizeof(kvs_exist_list));
  if(list){
    list->keys = &kvskey;
    list->length = (&kvsvalue)->length;
    list->num_keys = 1;
    list->result_buffer = (uint8_t *)((&kvsvalue)->value);
  }
  
#ifdef WITH_SPDK
    if (!g_env.udd_option.syncio) {//uses async interface
      syncio = false;
      cbfn = _metadata_keyspace_aio_complete_handle;
    }
#endif

  kvs_key_space_handle ks_hd = dev_hd->meta_ks_hd;  
  ret = (kvs_result)dev_hd->meta_ks_hd->dev->driver->exist_tuple(
        ks_hd, 1, &kvskey, list, (void*)&async_completed,(void*)&async_result, syncio, cbfn);

  if(syncio){
    if(list) free(list);
  }
  if (ret != KVS_SUCCESS){
    fprintf(stderr, "Check key space exist failed with error 0x%x - %s\n", ret,
      kvs_errstr(ret));
    return ret;
  }
  	
  if (!syncio) {
    while (true) {
      if (async_completed) break;
    }
  }  
  return ret;
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

//copy a integer in to payload. Integer will translate to little end, and then copied to buffer 
#define _copy_int_to_payload(integer, payload_buff, curr_posi) \
do { \
  uint8_t integer_size = sizeof(integer); \
  _trans_host_to_little_end(&integer, integer_size); \
  memcpy(payload_buff + curr_posi, &integer, integer_size); \
  curr_posi += integer_size; \
} while(0)

uint16_t _get_key_space_payload_size() {
  ks_metadata cont;
  uint16_t payload_size = sizeof(cont.keyspace_id);
  payload_size += sizeof(cont.opened);
  payload_size += sizeof(cont.key_order);
  payload_size += sizeof(cont.capacity);
  payload_size += sizeof(cont.free_size);
  payload_size += sizeof(cont.kv_count);
  return payload_size;
}

void _parse_key_space_metadata_payload(ks_metadata *cont, const char* payload_buff) {
  uint16_t curr_posi = 0;
  _copy_payload_to_int(cont->keyspace_id, payload_buff, curr_posi);
  _copy_payload_to_int(cont->opened, payload_buff, curr_posi);
  _copy_payload_to_int(cont->key_order, payload_buff, curr_posi);
  _copy_payload_to_int(cont->capacity, payload_buff, curr_posi);
  _copy_payload_to_int(cont->free_size, payload_buff, curr_posi);
  _copy_payload_to_int(cont->kv_count, payload_buff, curr_posi); 
}

void _construct_key_space_metadata_payload(const ks_metadata *cont,
  char* payload_buff) {
  ks_metadata cont_le = *cont;
  uint16_t curr_posi = 0;
  _copy_int_to_payload(cont_le.keyspace_id, payload_buff, curr_posi);
  _copy_int_to_payload(cont_le.opened, payload_buff, curr_posi);
  _copy_int_to_payload(cont_le.key_order, payload_buff, curr_posi);
  _copy_int_to_payload(cont_le.capacity, payload_buff, curr_posi);
  _copy_int_to_payload(cont_le.free_size, payload_buff, curr_posi);
  _copy_int_to_payload(cont_le.kv_count, payload_buff, curr_posi);
}

kvs_result _retrieve_key_space_metadata(kvs_device_handle dev_hd, ks_metadata *cont) {
  kvs_result ret = KVS_SUCCESS;

  //malloc resources
  uint16_t vlen = _get_key_space_payload_size();
  vlen = ((vlen - 1)/DMA_ALIGN + 1) * DMA_ALIGN;
  uint16_t klen = strlen(cont->name);
  char *key = (char*)kvs_malloc(klen, PAGE_ALIGN);
  char *value = (char*)kvs_malloc(vlen, PAGE_ALIGN);
  if (key == NULL || value == NULL) {
    fprintf(stderr, "failed to allocate\n");
    if (key) kvs_free(key);
    if (value) kvs_free(value);
    return KVS_ERR_SYS_IO;
  }
  snprintf(key, klen, "%s", cont->name);

  kvs_option_retrieve option;
  memset(&option, 0, sizeof(kvs_option_retrieve));
  option.kvs_retrieve_delete = false;
  const kvs_key kvskey = {key, klen};
  kvs_value kvsvalue = {value, vlen, 0, 0};
  ret = _sync_io_to_meta_keyspace(dev_hd, &kvskey, &kvsvalue, &option, KVS_CMD_RETRIEVE);
  if (ret != KVS_SUCCESS) {
    fprintf(stderr, "retrieve key space %s metadata failed. error 0x%x - %s\n",
        key, ret, kvs_errstr(ret));
    kvs_free(key);
    kvs_free(value);
    return ret;
  }

  _parse_key_space_metadata_payload(cont, value);
  kvs_free(key);
  kvs_free(value);
  return ret;
}

kvs_result _store_key_space_metadata(kvs_device_handle dev_hd,
    const ks_metadata *cont, kvs_store_type st_type) {
  kvs_result ret = KVS_SUCCESS;
  //calculate key size and payload size
  uint16_t payload_size = _get_key_space_payload_size();
  uint16_t klen = strnlen(cont->name, MAX_CONT_PATH_LEN);
  char* key = (char*)kvs_zalloc(klen, PAGE_ALIGN);
  char* payload_buff = (char*)kvs_zalloc(payload_size, PAGE_ALIGN);
  if (!key || !payload_buff) {
    if (key) kvs_free(key);
    if (payload_buff) kvs_free(payload_buff);
    return KVS_ERR_SYS_IO;
  }
  //construct the payload content
  snprintf(key, klen, "%s", cont->name);
  _construct_key_space_metadata_payload(cont, payload_buff);

  //store to metadata keyspace
  kvs_option_store option;
  option.st_type = st_type;
  const kvs_key kvskey = {key, klen};
  kvs_value kvsvalue = {payload_buff, payload_size, 0, 0};
  ret = _sync_io_to_meta_keyspace(dev_hd, &kvskey, &kvsvalue, &option, KVS_CMD_STORE);
  if (ret != KVS_SUCCESS) {
    fprintf(stderr, "store keyspace meta data failed with error 0x%x - %s\n",
        ret, kvs_errstr(ret));
  }

  kvs_free(key);
  kvs_free(payload_buff);
  return ret;
}

void _parse_key_space_list_payload(const char *payload, uint32_t data_len,
  ks_list* kslist) {
  //parse key spaces information, format as following:
  //first four bytes is the number of key spaces, then keyspace id and name  of every key space
  //is stored, the keyspace id and name size of every key space is 4 and MAX_CONT_PATH_LEN(256)
  if(data_len < sizeof(kslist->ks_num)) { //means number of key space is 0
    kslist->ks_num = 0;
    return;
  }
  uint32_t curr_posi = 0;
  kslist->ks_num = *((key_space_id_t *)payload);
  curr_posi += sizeof(kslist->ks_num);
  for(uint8_t idx = 0; idx < kslist->ks_num; idx++){
    kslist->entries[idx].keyspace_id = *((keyspace_id_t *)(payload + curr_posi));
    curr_posi += sizeof(kslist->entries[idx].keyspace_id);

    memcpy(kslist->entries[idx].names_buffer, payload + curr_posi,
      MAX_CONT_PATH_LEN);
    curr_posi += MAX_CONT_PATH_LEN;
  }
}

kvs_result _retreive_key_space_list(kvs_device_handle dev_hd,
  ks_list* kslist) {
  kvs_result ret = KVS_SUCCESS;
  uint16_t klen = strlen(KEY_SPACE_LIST_KEY_NAME) + 1;
  uint32_t act_len = sizeof(*kslist);
  uint32_t vlen = ((act_len + 3) / 4) * 4;

  char *key   = (char*)kvs_malloc(klen, PAGE_ALIGN);
  char *value = (char*)kvs_malloc(vlen, PAGE_ALIGN);
  if(key == NULL || value == NULL) {
    fprintf(stderr, "failed to allocate\n");
    if(key) kvs_free(key);
    if(value) kvs_free(value);
    return KVS_ERR_SYS_IO;
  }

  //retrieve key space list data from KVSSD metadata keyspace
  snprintf(key, klen, "%s", KEY_SPACE_LIST_KEY_NAME);
  kvs_option_retrieve option;
  memset(&option, 0, sizeof(kvs_option_retrieve));
  option.kvs_retrieve_delete = false;
  const kvs_key  kvskey = {key, klen };
  kvs_value kvsvalue = {value, vlen , 0, 0 /*offset */};
  ret = _sync_io_to_meta_keyspace(dev_hd, &kvskey, &kvsvalue, &option,
    KVS_CMD_RETRIEVE);
  if(ret != KVS_SUCCESS) {
    if(ret == KVS_ERR_KEY_NOT_EXIST) {//before create first key space, key isn't exist
      ret = KVS_SUCCESS;
    } else {
      fprintf(stderr, "Input value buffer len:%d. actual data length:%d.\n", kvsvalue.length, 
        kvsvalue.actual_value_size);
      fprintf(stderr, "retrieve key space list failed error 0x%x - %s\n", ret,
        kvs_errstr(ret));
    }
    kslist->ks_num = 0;
    kvs_free(key);
    kvs_free(value);
    return ret;
  }
  _parse_key_space_list_payload((const char*)kvsvalue.value, kvsvalue.length,
    kslist);

  kvs_free(key);
  kvs_free(value);
  return ret;
}

keyspace_id_t _search_an_avaliable_keyspace_id(const ks_list* kslist) {
  keyspace_id_t keyspace_id = USER_DATA_KEYSPACE_START_ID;
  /* TODO when KVSSD support more than two containers, add code to find a keyspace id, that 
     *  not used, that is to say not in the inputted containers list(#conts) .
     */ 
  return keyspace_id;
}
bool _contain_keyspace(const ks_list* kslist, keyspace_id_t ks_id) {
  for(uint8_t idx = 0; idx < kslist->ks_num; idx++){
    if(ks_id == kslist->entries[idx].keyspace_id) {
      return true;
    }
  }
  return false;
}

void _construct_key_space_list_payload(const ks_list* kslist,
  char* payload_buff, uint32_t *data_len) {
  uint16_t curr_position = 0;
  //total key space number
  memcpy(payload_buff + curr_position, &kslist->ks_num,
         sizeof(kslist->ks_num));
  curr_position += sizeof(kslist->ks_num);
  //name of every key space, 
  //every key space name has fixed size(MAX_KEYSPACE_NAME_LEN) in ssd
  for(uint8_t idx = 0; idx < kslist->ks_num; idx++){
    memcpy(payload_buff+curr_position, &kslist->entries[idx].keyspace_id,
           sizeof(kslist->entries[idx].keyspace_id));
    curr_position += sizeof(kslist->entries[idx].keyspace_id);

    memcpy(payload_buff+curr_position, kslist->entries[idx].names_buffer,
           MAX_KEYSPACE_NAME_LEN + 1);
    curr_position += MAX_KEYSPACE_NAME_LEN + 1;
  }
  *data_len = curr_position;
}

kvs_result _store_key_space_list(kvs_device_handle dev_hd,
  const ks_list* kslist) {
  kvs_result ret = KVS_SUCCESS;
  // construct the payload content
  uint16_t klen = strlen(KEY_SPACE_LIST_KEY_NAME) + 1; 
  char* key = (char*)kvs_zalloc(klen, PAGE_ALIGN);
  char* payload_buff = (char*)kvs_zalloc(sizeof(*kslist), PAGE_ALIGN);
  if(!key || !payload_buff) {
    if(key) kvs_free(key);
    if(payload_buff) kvs_free(payload_buff);
    return KVS_ERR_SYS_IO;
  }
  snprintf(key, klen, "%s", KEY_SPACE_LIST_KEY_NAME);
  
  uint32_t vlen = 0;
  _construct_key_space_list_payload(kslist, payload_buff, &vlen);

  //store to meta data keyspace
  kvs_option_store option;
  option.st_type = KVS_STORE_POST;
  const kvs_key  kvskey = {key, klen};
  kvs_value kvsvalue = {payload_buff, vlen, 0, 0};  
  ret = _sync_io_to_meta_keyspace(dev_hd, &kvskey, &kvsvalue, &option,
    KVS_CMD_STORE);
  if(ret != KVS_SUCCESS ) {
    fprintf(stderr, "store key space list failed with error 0x%x - %s\n", ret,
      kvs_errstr(ret));
  }

  kvs_free(key);
  kvs_free(payload_buff);
  return ret;
}

kvs_result _add_to_key_space_list(kvs_device_handle dev_hd,
  const char *name, keyspace_id_t *keyspace_id/*in and out param*/) {
  kvs_result ret = KVS_SUCCESS;
  //check whether key space has exist or has reach max number supported
  ks_list* kslist = (ks_list*)calloc(1, sizeof(ks_list));
  if(!kslist)
    return KVS_ERR_SYS_IO;

  kslist->ks_num = 0;
  ret = _retreive_key_space_list(dev_hd, kslist);
  if(ret != KVS_SUCCESS) {
    free(kslist);
    return ret;
  }
  if(kslist->ks_num >= KS_MAX_CONT) {
    free(kslist);
    fprintf(stderr, "Max key space number %d has reached", KS_MAX_CONT);
    fprintf(stderr, " add key space %s failed.\n", name);
    return KVS_ERR_SYS_IO;
  }
  for(uint8_t idx = 0; idx < kslist->ks_num; idx++) {
    if(!strncmp(name, kslist->entries[idx].names_buffer, MAX_KEYSPACE_NAME_LEN)) {
      free(kslist);
      return KVS_ERR_KS_EXIST;
    }
  }
  //get the keyspace id
  keyspace_id_t ks_id = *keyspace_id;
  if(ks_id == _INVALID_USER_KEYSPACE_ID) {
    ks_id = _search_an_avaliable_keyspace_id(kslist);
  } else if(_contain_keyspace(kslist, ks_id)){
    free(kslist);
    return KVS_ERR_SYS_IO; //this keyspace has been used
  }
  //add new key space to key space list
  snprintf(kslist->entries[kslist->ks_num].names_buffer,
    MAX_KEYSPACE_NAME_LEN + 1, "%s", name);
  kslist->entries[kslist->ks_num].keyspace_id = ks_id;
  kslist->ks_num++;

  // store key space list to ssd
  ret = _store_key_space_list(dev_hd, kslist);
  if(ret == KVS_SUCCESS) {
    *keyspace_id = ks_id;
  }
  free(kslist);
  return ret;
}

kvs_result _remove_from_key_space_list(kvs_device_handle dev_hd,
  const char *name, keyspace_id_t *keyspace_id) {
  kvs_result ret = KVS_SUCCESS;
  //check whether key space is exist
  ks_list* kslist = (ks_list*)calloc(1, sizeof(ks_list));
  if(!kslist)
    return KVS_ERR_SYS_IO;
  kslist->ks_num = 0;
  ret = _retreive_key_space_list(dev_hd, kslist);
  if(ret != KVS_SUCCESS) {
    free(kslist);
    return ret;
  }
  uint8_t idx = 0;
  for(idx = 0; idx < kslist->ks_num; idx++){
    if(!strncmp(name, kslist->entries[idx].names_buffer, MAX_KEYSPACE_NAME_LEN))
      break;
  }

  //remove the found key space from list
  if(idx >= kslist->ks_num) { //not find the input key space
    free(kslist);
    return KVS_ERR_KS_NOT_EXIST;
  }else {
    *keyspace_id = kslist->entries[idx].keyspace_id;
    // move forward valid key spaces
    for(; idx < kslist->ks_num - 1; idx++){
      memcpy(kslist->entries + idx, kslist->entries + idx + 1,
        sizeof(ks_list_entry));
    }
    kslist->ks_num--;
  }

  // store key space list to ssd
  ret = _store_key_space_list(dev_hd, kslist);
  if(ret != KVS_SUCCESS)
    *keyspace_id = _INVALID_USER_KEYSPACE_ID;

  free(kslist);
  return ret;
}

kvs_result _open_key_space(kvs_key_space_handle ks_hd) {
  kvs_result ret = KVS_SUCCESS;
  ks_metadata cont = {0, 0, 0, 0, 0, 0, ks_hd->name};
  ret = _retrieve_key_space_metadata(ks_hd->dev, &cont);
  if (ret != KVS_SUCCESS) return ret;
  cont.opened = 1;
  ks_hd->keyspace_id = cont.keyspace_id;
  ret = _store_key_space_metadata(ks_hd->dev, &cont, KVS_STORE_POST);
  return ret;
}

kvs_result _create_key_space_entry(kvs_device_handle dev_hd,
  const char *name, uint8_t keyspace_id, uint64_t cap_size,
  const kvs_option_key_space opt) {
  kvs_result ret = KVS_SUCCESS;
  ks_metadata ks = {
    keyspace_id, 0, opt.ordering, cap_size, 0, 0, name};
  ret = _store_key_space_metadata(dev_hd, &ks, KVS_STORE_NOOVERWRITE);
  return ret;
}

kvs_result _delete_key_space_entry(kvs_device_handle dev_hd,
  const char *name) {
  kvs_result ret = KVS_SUCCESS;
  uint16_t klen = strlen(name);
  char* key = (char*)kvs_zalloc(klen, PAGE_ALIGN);
  if(!key) {
    WRITE_ERR("Out of memory, malloc failed\n");
    return KVS_ERR_SYS_IO;
  }
  snprintf(key, klen, "%s", name);

  const kvs_key  kvskey = {key, klen};
  kvs_option_delete option = {true};
  ret = _sync_io_to_meta_keyspace(dev_hd, &kvskey, NULL, &option,
    KVS_CMD_DELETE);
  if(ret != KVS_SUCCESS) {
    fprintf(stderr, "delete key space failed with error 0x%x - %s\n", ret,
      kvs_errstr(ret));
  }

  kvs_free(key);
  return ret;
}

kvs_result kvs_create_key_space(kvs_device_handle dev_hd,
  kvs_key_space_name *key_space_name, uint64_t size, kvs_option_key_space opt) {
  if(key_space_name == NULL || key_space_name->name == NULL || dev_hd == NULL){
    return KVS_ERR_PARAM_INVALID;
  }

  uint32_t name_len = strlen(key_space_name->name);
  if(name_len != key_space_name->name_len 
    || name_len == 0 || name_len > MAX_KEYSPACE_NAME_LEN){
    return KVS_ERR_KS_NAME;
  }

  if (!_device_opened(dev_hd)) {
    return KVS_ERR_DEV_NOT_EXIST;
  }
  
  if (opt.ordering != KVS_KEY_ORDER_NONE) {
    fprintf(stderr, "Do not support key order %d!\n", opt.ordering);
    return KVS_ERR_OPTION_INVALID;
  }
  keyspace_id_t keyspace_id = _INVALID_USER_KEYSPACE_ID;
  kvs_result ret = _add_to_key_space_list(dev_hd, key_space_name->name,
                      &keyspace_id);
  if(ret != KVS_SUCCESS) {
    return ret;
  }
  ret = _create_key_space_entry(dev_hd, key_space_name->name, keyspace_id, size,
                                 opt);
  if(ret != KVS_SUCCESS) {
    _remove_from_key_space_list(dev_hd, key_space_name->name, &keyspace_id);
    return ret;
  }
  return ret;
}

kvs_result kvs_delete_key_space(kvs_device_handle dev_hd,
  kvs_key_space_name *key_space_name) {
  if((dev_hd == NULL) || (key_space_name == NULL) || (key_space_name->name == NULL)) {
    return KVS_ERR_PARAM_INVALID;
  }
  if (!_device_opened(dev_hd)) {
    return KVS_ERR_DEV_NOT_EXIST;
  }
  //before delete key space, key space should in close state
  if (_key_space_opened(dev_hd, key_space_name->name)) {
    return KVS_ERR_SYS_IO;
  }

  kvs_result ret = KVS_SUCCESS;
  //remove from key space list
  keyspace_id_t keyspace_id_removed = _INVALID_USER_KEYSPACE_ID;
  ret = _remove_from_key_space_list(dev_hd, key_space_name->name, &keyspace_id_removed);
  if(ret != KVS_SUCCESS) {
    return ret;
  }
  //delete key space entry, if delete failed, should recover original state
  ret = _delete_key_space_entry(dev_hd, key_space_name->name);
  if(ret != KVS_SUCCESS) {
    _add_to_key_space_list(dev_hd, key_space_name->name, &keyspace_id_removed);
    return ret;
  }
  return KVS_SUCCESS;
}

kvs_result kvs_list_key_spaces(kvs_device_handle dev_hd, uint32_t index,
  uint32_t buffer_size, kvs_key_space_name *names, uint32_t *ks_cnt) {
  if((dev_hd == NULL) || (names == NULL) || (ks_cnt == NULL)) {
    return KVS_ERR_PARAM_INVALID;
  }
  if(index < 1 ||  index > KS_MAX_CONT) {
    WRITE_ERR("Index of keyspace should be start form 1 to less or equal MAX keyspace number!\n");
    return KVS_ERR_KS_INDEX;
  }
  if (!_device_opened(dev_hd)) {
    return KVS_ERR_DEV_NOT_EXIST;
  }

  *ks_cnt = 0;
  ks_list* kslist = (ks_list*)calloc(1, sizeof(ks_list));
  if(!kslist) {
    WRITE_ERR("Out of memory, calloc failed\n");
    return KVS_ERR_SYS_IO;
  }

  kslist->ks_num = 0;

  kvs_result ret = _retreive_key_space_list(dev_hd, kslist);
  if(ret != KVS_SUCCESS) {
    free(kslist);
    return ret;
  }
  if(index > kslist->ks_num && kslist->ks_num != 0) {
    WRITE_ERR("Index of container/keyspace inputted is too bigger.\n");
    free(kslist);
    return KVS_ERR_KS_INDEX;
  }

  uint32_t items_buff_cnt = buffer_size/sizeof(kvs_key_space_name);
   *ks_cnt = 0;
  for(uint8_t idx = index - 1; idx < kslist->ks_num; idx++) {//index start from 1
    if(*ks_cnt >= items_buff_cnt) {
      if(!items_buff_cnt) {
        WRITE_ERR("At least one container to read, buffer inputted is empty\n");
        ret = KVS_ERR_SYS_IO;
      }
      break; //buffer exhausted 
    }
    uint32_t len = strlen(kslist->entries[idx].names_buffer);
    if(len > names[idx].name_len) {
      WRITE_ERR("The buffer that used to store name is too small.\n");
      free(kslist);
      return KVS_ERR_SYS_IO;
    }
    snprintf(names[idx].name, names[idx].name_len + 1, "%s",
      kslist->entries[idx].names_buffer);
    names[idx].name_len = len; //include end '\0'
    *ks_cnt += 1;
  }

  free(kslist);
  return ret;
}

kvs_result kvs_open_key_space(kvs_device_handle dev_hd, char *name, kvs_key_space_handle *ks_hd) {
  if((dev_hd == NULL) || (name == NULL) || (ks_hd == NULL)) return KVS_ERR_PARAM_INVALID;
  if(*name == '\0') return KVS_ERR_KS_NAME;

  uint16_t ks_name_len = strlen(name);
  if (ks_name_len < KVS_MIN_KEY_LENGTH || ks_name_len > KVS_MAX_KEY_LENGTH) {
    fprintf(stderr, "key space name size is out of range, key space name size = %d\n", ks_name_len);
      return KVS_ERR_KS_NAME;
    }
  if (!_device_opened(dev_hd)) return KVS_ERR_DEV_NOT_EXIST;
  if (_key_space_opened(dev_hd, name)) return KVS_ERR_KS_OPEN;

  uint8_t exist = 0;
  kvs_result ret = _exist_key_space_entry(dev_hd, name, &exist);
  if (ret != KVS_SUCCESS) {
    fprintf(stderr, "Check key space exist failed. error code:0x%x.\n", ret);
    return ret;
  }
  if (!exist) return KVS_ERR_KS_NOT_EXIST;

  //open key space
  kvs_key_space_handle ks_handle = (kvs_key_space_handle)malloc(sizeof(struct _kvs_key_space_handle));
  if (!ks_handle) return KVS_ERR_SYS_IO;
  ks_handle->dev = dev_hd;
  snprintf(ks_handle->name, sizeof(ks_handle->name), "%s", name);

  ret = _open_key_space(ks_handle);
  if (ret != KVS_SUCCESS) {
    fprintf(stderr, "Update key space state failed. error code:0x%x,\n", ret);
    free(ks_handle);
    return ret;
  }

  dev_hd->open_ks_hds.push_back(ks_handle);
  g_env.list_open_ks.push_back(ks_handle);
  *ks_hd = ks_handle;
  return KVS_SUCCESS;
}

kvs_result _close_key_space(kvs_key_space_handle ks_hd) {
  kvs_result ret = KVS_SUCCESS;
  ks_metadata ks_meta = {0, 0, 0, 0, 0, 0, ks_hd->name};
  ret = _retrieve_key_space_metadata(ks_hd->dev, &ks_meta);
  if (ret != KVS_SUCCESS) return ret;

  ks_meta.opened = 0;
  return _store_key_space_metadata(ks_hd->dev, &ks_meta, KVS_STORE_POST);
}

kvs_result kvs_close_key_space(kvs_key_space_handle ks_hd) {
  kvs_result ret = _check_key_space_handle(ks_hd);
  if (ret != KVS_SUCCESS) return ret;

  ret = _close_key_space(ks_hd);
  if (ret != KVS_SUCCESS) {
    fprintf(stderr, "Close key space failed. error code:0x%x-%s.\n", ret,
        kvs_errstr(ret));
    return ret;
  }
  kvs_device_handle dev_hd = ks_hd->dev;
  dev_hd->open_ks_hds.remove(ks_hd);
  g_env.list_open_ks.remove(ks_hd);
  free(ks_hd);
  return ret;
}

kvs_result kvs_delete_key_group(kvs_key_space_handle ks_hd,
  kvs_key_group_filter *grp_fltr) {
  return KVS_ERR_SYS_IO;  // does not support delete key group for now
}

kvs_result kvs_delete_key_group_async(kvs_key_space_handle ks_hd,
      kvs_key_group_filter *grp_fltr, void *private1, void *private2, 
      kvs_postprocess_function post_fn) {
  return KVS_ERR_SYS_IO;  // does not support delete key group for now
}

kvs_result kvs_get_kvp_info(kvs_key_space_handle ks_hd, kvs_key *key, kvs_kvp_info *info) {
  kvs_result ret = _check_key_space_handle(ks_hd);
  if (ret != KVS_SUCCESS) return ret;

  if (key == NULL || info == NULL) return KVS_ERR_PARAM_INVALID;

  uint32_t vlen = KVS_MAX_VALUE_LENGTH;
  char *value = (char*)kvs_malloc(vlen, 4096);
  if (value == NULL) {
    fprintf(stderr, "malloc failed in kvs_get_kvp_info\n");
    return KVS_ERR_SYS_IO;
  }

  kvs_option_retrieve option;
  option.kvs_retrieve_delete = false;
  kvs_value kvsvalue = {value, vlen, 0, 0};
  ret = kvs_retrieve_kvp(ks_hd, key, &option, &kvsvalue);
  if (ret != KVS_SUCCESS)
    fprintf(stderr, "get_kvp_info failed: key= %s error= 0x%x - %s\n", (char*)key->key, ret, kvs_errstr(ret));
  else {
    info->key_len = key->length;
    info->value_len = kvsvalue.actual_value_size;
    memcpy(info->key, key->key, key->length);
  }
  if (value) kvs_free(value);

  return ret;
}

kvs_result kvs_get_key_space_info(kvs_key_space_handle ks_hd, kvs_key_space *ks) {  
  if(ks == NULL) {
    return KVS_ERR_PARAM_INVALID;
  }
  kvs_result ret = _check_key_space_handle(ks_hd);
  if (ret != KVS_SUCCESS) {
    return ret;
  }

  ks_metadata ks_meta = {0, 0, 0, 0, 0, 0, ks_hd->name};
  ret = _retrieve_key_space_metadata(ks_hd->dev, &ks_meta);
  if(ret != KVS_SUCCESS)
    return ret;
  ks->opened = ks_meta.opened;
  ks->capacity = ks_meta.capacity;
  ks->free_size = ks_meta.free_size;
  ks->count = ks_meta.kv_count;
  ks->name->name_len = strnlen(ks_hd->name,MAX_CONT_PATH_LEN);
  snprintf(ks->name->name, ks->name->name_len + 1, "%s", ks_hd->name);
  return ret;
}

kvs_result kvs_store_kvp(kvs_key_space_handle ks_hd, kvs_key *key, 
                      kvs_value *value, kvs_option_store *opt) {
  int ret = _check_key_space_handle(ks_hd);
  if (ret!=KVS_SUCCESS) {
    return (kvs_result)ret;
  }

  if((key == NULL) || (value == NULL) || (opt == NULL)) {
    return KVS_ERR_PARAM_INVALID;
  }

  ret = validate_request(key, value);
  if(ret)
    return (kvs_result)ret;

  ret = ks_hd->dev->driver->store_tuple(ks_hd, key, value,
    *opt, 0, 0, 1, 0);
  return (kvs_result)ret;
}

kvs_result kvs_store_kvp_async(kvs_key_space_handle ks_hd, kvs_key *key, kvs_value *value,
        kvs_option_store *opt, void *private1, void *private2, kvs_postprocess_function post_fn) {
  int ret = _check_key_space_handle(ks_hd);
  if (ret!=KVS_SUCCESS) {
    return (kvs_result)ret;
  }

  if(key == NULL || value == NULL || opt == NULL || post_fn == NULL)
    return KVS_ERR_PARAM_INVALID;

  ret = validate_request(key, value);
  if(ret)
    return (kvs_result)ret;

  ret = ks_hd->dev->driver->store_tuple(ks_hd, key, value,
    *opt, private1, private2, 0, post_fn);
  return (kvs_result)ret;
}

kvs_result kvs_retrieve_kvp(kvs_key_space_handle ks_hd, kvs_key *key,
                        kvs_option_retrieve *opt, kvs_value *value) {
  int ret = _check_key_space_handle(ks_hd);
  if (ret!=KVS_SUCCESS) {
    return (kvs_result)ret;
  }
  if((key == NULL) || (value == NULL) || (opt == NULL)) {
    return KVS_ERR_PARAM_INVALID;
  }
  ret = validate_request(key, value);
  if(ret)
    return (kvs_result)ret;
  if (value->length & (KVS_VALUE_LENGTH_ALIGNMENT_UNIT - 1))
      return KVS_ERR_PARAM_INVALID;

  ret = ks_hd->dev->driver->retrieve_tuple(ks_hd, key, value,
    *opt, 0, 0, 1, 0);
  return (kvs_result)ret;
}

kvs_result kvs_retrieve_kvp_async(kvs_key_space_handle ks_hd, kvs_key *key, 
      kvs_option_retrieve *opt, void *private1, void *private2, kvs_value *value, 
      kvs_postprocess_function post_fn) {
  int ret = _check_key_space_handle(ks_hd);
  if (ret!=KVS_SUCCESS) {
    return (kvs_result)ret;
  }
  if(key == NULL || value == NULL || opt == NULL || post_fn == NULL)
    return KVS_ERR_PARAM_INVALID;
  ret = validate_request(key, value);
  if(ret)
    return (kvs_result)ret;
  if (value->length & (KVS_VALUE_LENGTH_ALIGNMENT_UNIT - 1))
      return KVS_ERR_PARAM_INVALID;

  ret = ks_hd->dev->driver->retrieve_tuple(ks_hd, key, value,
    *opt, private1, private2, 0, post_fn);
  return (kvs_result)ret;
}

kvs_result kvs_exist_kv_pairs(kvs_key_space_handle ks_hd, uint32_t key_cnt, kvs_key *keys, kvs_exist_list *list) {
  int ret = KVS_SUCCESS;
  if (keys == NULL || list == NULL || (key_cnt <= 0) || (list->result_buffer == NULL))
    return KVS_ERR_PARAM_INVALID;
  for (unsigned int i = 0; i != key_cnt; ++i) {
    ret = validate_kv_pair_(keys + i, 0, 0);
    if (ret != KVS_SUCCESS) {
      return (kvs_result)ret;
    }
  }
  list->keys = keys;
  list->num_keys = key_cnt;
  
  ret = _check_key_space_handle(ks_hd);
  if (ret!=KVS_SUCCESS) {
    return (kvs_result)ret;
  }
  if(list->length <= 0)
      return KVS_ERR_BUFFER_SMALL;
  
  ret = ks_hd->dev->driver->exist_tuple(ks_hd, key_cnt, keys,
    list, NULL, NULL, 1, 0); 
  return (kvs_result)ret;
}

kvs_result kvs_exist_kv_pairs_async(kvs_key_space_handle ks_hd, uint32_t key_cnt, 
      kvs_key *keys, kvs_exist_list *list, void *private1, void *private2, 
      kvs_postprocess_function post_fn) {
  int ret = KVS_SUCCESS;    
  if (keys == NULL || list == NULL || post_fn == NULL || list->result_buffer == NULL || (key_cnt <= 0))
    return KVS_ERR_PARAM_INVALID;

  for (unsigned int i = 0; i != key_cnt; ++i) {
    ret = validate_kv_pair_(keys + i, 0, 0);
    if (ret != KVS_SUCCESS) {
      return (kvs_result)ret;
    }
  }
  list->keys = keys;
  list->num_keys = key_cnt;
  
  ret = _check_key_space_handle(ks_hd);
  if (ret!=KVS_SUCCESS) {
    return (kvs_result)ret;
  }
  if(list->length  <= 0)
    return KVS_ERR_BUFFER_SMALL;
  
  ret = ks_hd->dev->driver->exist_tuple(ks_hd, key_cnt, keys,
    list, private1, private2, 0, post_fn);

  return (kvs_result)ret;
}

kvs_result kvs_create_iterator(kvs_key_space_handle ks_hd, kvs_option_iterator *iter_op,
                      kvs_key_group_filter *iter_fltr, kvs_iterator_handle *iter_hd) {
  int ret = _check_key_space_handle(ks_hd);
  if (ret != KVS_SUCCESS) {
    return (kvs_result)ret;
  }
  if((iter_op == NULL) || (iter_fltr == NULL) || (iter_hd == NULL)) {
    return KVS_ERR_PARAM_INVALID;
  }
  uint32_t bitmask;
  uint32_t bit_pattern;
  filter2context(iter_fltr, &bitmask, &bit_pattern);
  if(!_is_valid_bitmask(bitmask))
    return KVS_ERR_ITERATOR_FILTER_INVALID;

  ret = ks_hd->dev->driver->create_iterator(ks_hd, *iter_op,
    bitmask, bit_pattern, iter_hd);
  return (kvs_result)ret;
}

kvs_result kvs_delete_iterator(kvs_key_space_handle ks_hd, kvs_iterator_handle iter_hd) {
  int ret = _check_key_space_handle(ks_hd);
  if (ret != KVS_SUCCESS) {
    return (kvs_result)ret;
  }

  ret =  ks_hd->dev->driver->delete_iterator(ks_hd, iter_hd);
  return (kvs_result)ret;
}

kvs_result kvs_delete_kvp(kvs_key_space_handle ks_hd, kvs_key *key, kvs_option_delete *opt) {
  kvs_result ret = _check_key_space_handle(ks_hd);
  if (ret != KVS_SUCCESS) {
    return ret;
  }
  
  if(key == NULL || opt == NULL)
    return KVS_ERR_PARAM_INVALID;

  ret = (kvs_result)validate_request(key, 0);
  if(ret != KVS_SUCCESS)
    return ret;

  ret = (kvs_result)ks_hd->dev->driver->delete_tuple(ks_hd, key, 
    *opt, NULL, NULL, 1, 0);
  return ret;
}

kvs_result kvs_delete_kvp_async(kvs_key_space_handle ks_hd, kvs_key* key, 
      kvs_option_delete *opt, void *private1, void *private2, 
      kvs_postprocess_function post_fn) {

  kvs_result ret = _check_key_space_handle(ks_hd);
  if (ret != KVS_SUCCESS) {
    return ret;
  }
  if((key == NULL) || (opt == NULL) || (post_fn == NULL))
    return KVS_ERR_PARAM_INVALID;

  ret = (kvs_result)validate_request(key, 0);
  if(ret != KVS_SUCCESS) 
    return ret;
  
  ret = (kvs_result)ks_hd->dev->driver->delete_tuple(ks_hd, key,
    *opt, private1, private2, 0, post_fn);
  return ret;
}

kvs_result kvs_iterate_next(kvs_key_space_handle ks_hd, kvs_iterator_handle iter_hd, 
    kvs_iterator_list *iter_list) {

  if(iter_list == NULL || iter_list->it_list == NULL)
    return KVS_ERR_PARAM_INVALID;

  kvs_result ret = _check_key_space_handle(ks_hd);
  if (ret != KVS_SUCCESS) {
    return ret;
  }

  if(iter_list->size != KVS_ITERATOR_BUFFER_SIZE){
    return KVS_ERR_SYS_IO;
  }

  ret = (kvs_result)ks_hd->dev->driver->iterator_next(ks_hd, iter_hd, iter_list, NULL, NULL, 1, 0);
  return ret;
}

kvs_result kvs_iterate_next_async(kvs_key_space_handle ks_hd, kvs_iterator_handle iter_hd , 
    kvs_iterator_list *iter_list, void *private1, void *private2, kvs_postprocess_function post_fn) {
  if (iter_list == NULL || iter_list->it_list == NULL || post_fn == NULL){
    return KVS_ERR_PARAM_INVALID;
  }

  kvs_result ret = _check_key_space_handle(ks_hd);
  if (ret != KVS_SUCCESS) {
    return ret;
  }

  if(iter_list->size != KVS_ITERATOR_BUFFER_SIZE){
    return KVS_ERR_SYS_IO;
  }

  ret = (kvs_result)ks_hd->dev->driver->iterator_next(ks_hd, iter_hd, iter_list, private1, 
    private2, 0, post_fn);
  return ret;
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
  if (g_env.use_spdk) _udd_free(buf);
  else free(buf);
#else
  free(buf);
#endif
}
