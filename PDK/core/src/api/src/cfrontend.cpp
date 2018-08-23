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


#include "kvs_utils.h"
#include "uddenv.h"
#include "private_types.h"
#include "kvemul.hpp"
#include "udd.hpp"
#include "kvkdd.hpp"

#include <map>
#include <list>
#include <regex>
#include <string>

struct {
  bool initialized = false;
  bool use_spdk = false;
  bool use_async = false;
  int queuedepth;
  int is_polling = 1;
  _on_iocomplete iocomplete_fn;
  std::map<std::string, kv_device_priv *> list_devices;
  std::list<kvs_device_handle > open_devices;
  struct {
    uint64_t cq_masks[NR_MAX_SSD];
    uint64_t core_masks[NR_MAX_SSD];
    uint32_t mem_size_mb;
    int num_devices;
  } udd_option;
  char configfile[256];
} g_env;

std::map<int, std::string> errortable;

int32_t kvs_exit_env() {

  std::list<kvs_device_handle > clone(g_env.open_devices.begin(),
				      g_env.open_devices.end());
  
  //fprintf(stderr, "KVSSD: Close %d unclosed devices\n", (int) clone.size());
  for (kvs_device_handle t : clone) {
      kvs_close_device(t);
  }
  
  return 0;
}

int32_t kvs_init_env_opts(kvs_init_options* options) {
  memset((void*) options, 0, sizeof(kvs_init_options));
  options->memory.use_dpdk = 1;
  options->memory.dpdk_mastercoreid = -1;
  options->memory.nr_hugepages_per_socket = 1024;
  options->memory.socketmask = 0;
  return 0;
}

int kvs_list_kvdevices(kv_device_info **devs, int size) {
  int index = 0;
  int max = std::min((int) g_env.list_devices.size(), size);
  
  for (const auto &t : g_env.list_devices) {
    kv_device_priv* dev_i = t.second;
    kv_device_info* dev = (kv_device_info*) malloc(sizeof(kv_device_info));

    strcpy(dev->node, dev_i->node);
    strcpy(dev->spdkpath, dev_i->spdkpath);
    dev->nsid = dev_i->nsid;
    strcpy(dev->pci_slot_name, dev_i->pci_slot_name);
    dev->numanode = dev_i->numanode;
    dev->vendorid = dev_i->vendorid;
    dev->deviceid = dev_i->deviceid;
    strcpy(dev->ven_dev_id, dev_i->ven_dev_id);

    devs[index++] = dev;

    if (index == max)
      break;
  }
  return index;
}

int initialize_udd_options(kvs_init_options* options){
  int i = 0;
  std::string delim = ",";
  char *pt;
  pt = strtok(options->udd.core_mask_str, ",");
  while(pt != NULL) {
    g_env.udd_option.core_masks[i++] = std::stol(pt);
    pt = strtok(NULL, ",");
  }
  
  i = 0;
  pt = strtok(options->udd.cq_thread_mask, ",");
  while(pt != NULL) {
    g_env.udd_option.cq_masks[i++] = std::stol(pt);
    pt = strtok(NULL, ",");
  }
  
  g_env.udd_option.num_devices = 0;
  g_env.udd_option.mem_size_mb = options->udd.mem_size_mb;
  
  return 0;
}

int32_t kvs_init_env(kvs_init_options* options) {
  
  if (g_env.initialized)
    return 0;

  if (options) {
    g_env.queuedepth = options->aio.queuedepth > 0 ? options->aio.queuedepth : 64;
    strcpy(g_env.configfile, options->emul_config_file);
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
      // emulator or kdd
      //fprintf(stdout, "Using KV Emulator or Kernel\n");
      g_env.is_polling = options->aio.is_polling;
    }

    // initialize cache if needed
    if (options->memory.max_cachesize_mb > 0) {
      WRITE_WARNING("Key-value caching is not supported yet\n");
      return KVS_ERR_OPTION_INVALID;//KVS_ERR_OPTION_NOT_SUPPORTED;
    }
	  
    if (options->aio.iocomplete_fn != 0) {
      g_env.iocomplete_fn = options->aio.iocomplete_fn;
      g_env.use_async = true;
      
      // initialize aio threads if needed
      if (options->memory.use_dpdk == 0) {
	// create aio threads for non-spdk drivers
      }
    } else {
      g_env.is_polling = 1; // always polling for sync mode
    }
  }

  g_env.initialized = true;

  WRITE_LOG("INIT_ENV Finished: async? %d\n", g_env.use_async);
  return 0;
}


kv_device_priv *_find_local_device_from_path(const std::string &devpath,
		std::map<std::string, kv_device_priv *> &list_devices) {

  static std::regex emu_pattern("/dev/kvemul*");
  kv_device_priv *dev = 0;
  std::smatch matches;

#if defined WITH_SPDK
  static std::regex pciaddr_pattern("[^:]*:(.*)$");
  if (std::regex_search(devpath, matches, pciaddr_pattern)) {
    kv_device_priv *udd = new kv_device_priv();
    static int uddnsid = 0;
    udd->nsid = uddnsid++;
    udd->isemul = false;
    udd->iskerneldev = false;
    udd->isspdkdev = true;
    return udd;
  } else {
    fprintf(stderr, "WRN: Please specify spdk device path properly\n");
    exit(1);
  }
#else
  static const char *emulpath = "/dev/kvemul";
  if (devpath == emulpath) {
    //if(std::regex_search(devpath, matches, emu_pattern)){
    static int emulnsid = 0;
    kv_device_priv *emul = new kv_device_priv();
    sprintf(emul->node, "%s", devpath.c_str());
    emul->nsid = emulnsid++;
    emul->isemul = true;
    emul->iskerneldev = false;
    return emul;
  } else {
    kv_device_priv *kdd = new kv_device_priv();
    static int kddnsid = 0;
    kdd->nsid = kddnsid++;
    kdd->isemul = false;
    kdd->isspdkdev = false;
    kdd->iskerneldev = true;
    return kdd;
  }
  
  for (const auto &t : g_env.list_devices) {
    if (devpath == t.second->node) {
      dev = t.second;
    }
  }
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
    return new KUDDriver(dev, g_env.iocomplete_fn);
  }
#else
    if (dev->isemul) {
      return new KvEmulator(dev, g_env.iocomplete_fn );
    } else {
      return new KDDriver(dev, g_env.iocomplete_fn);
    }
#endif
    return nullptr;
}
/*
inline bool is_emulator_path(const char *devpath) {
  return (devpath == 0 || strncmp(devpath, "/dev/kvemul", strlen("/dev/kvemul"))== 0);
}

inline bool is_spdk_path(const char *devpath) {
  return (strncmp(devpath, "trtype", 6) == 0);
  }
*/

void build_error_table() {
  errortable[0x0]="KVS_SUCCESS";
  errortable[0x0B0]="KVS_WRN_COMPRESS";
  errortable[0x300]="KVS_WRN_MORE";
  errortable[0x012]="KVS_ERR_DEV_CAPACITY";
  errortable[0x070]="KVS_ERR_DEV_INIT";
  errortable[0x071]="KVS_ERR_DEV_INITIALIZED";
  errortable[0x072]="KVS_ERR_DEV_NOT_EXIST";
  errortable[0x01C]="KVS_ERR_DEV_SANITIZE_FAILED";
  errortable[0x01D]="KVS_ERR_DEV_SANITIZE_IN_PROGRESS";
  errortable[0x090]="KVS_ERR_ITERATOR_IN_PROGRESS";
  errortable[0x394]="KVS_ERR_ITERATOR_NOT_EXIST";
  errortable[0x395]="KVS_ERR_KEY_INVALID";
  errortable[0x003]="KVS_ERR_KEY_LENGTH_INVALID";
  errortable[0x010]="KVS_ERR_KEY_NOT_EXIST";
  errortable[0x095]="KVS_ERR_NS_DEFAULT";
  errortable[0x00B]="KVS_ERR_NS_INVALID";
  errortable[0x004]="KVS_ERR_OPTION_INVALID";
  errortable[0x101]="KVS_ERR_PARAM_NULL";
  errortable[0x084]="KVS_ERR_PURGE_IN_PRGRESS";
  errortable[0x303]="KVS_ERR_SYS_IO";
  errortable[0x415]="KVS_ERR_SYS_PERMISSION";
  errortable[0x001]="KVS_ERR_VALUE_LENGTH_INVALID";
  errortable[0x008]="KVS_ERR_VALUE_LENGTH_MISALIGNED";
  errortable[0x002]="KVS_ERR_VALUE_OFFSET_INVALID";
  errortable[0x0F0]="KVS_ERR_VENDOR";
  errortable[0x301]="KVS_ERR_BUFFER_SMALL";
  errortable[0x191]="KVS_ERR_DEV_MAX_NS";
  errortable[0x192]="KVS_ERR_ITERATOR_COND_INVALID";
  errortable[0x080]="KVS_ERR_KEY_EXIST";
  errortable[0x118]="KVS_ERR_NS_ATTAHED";
  errortable[0x181]="KVS_ERR_NS_CAPACITY";
  errortable[0x11A]="KVS_ERR_NS_NOT_ATTACHED";
  errortable[0x401]="KVS_ERR_QUEUE_CQID_INVALID";
  errortable[0x402]="KVS_ERR_QUEUE_SQID_INVALID";
  errortable[0x403]="KVS_ERR_QUEUE_DELETION_INVALID";
  errortable[0x104]="KVS_ERR_QUEUE_MAX_QUEUE";
  errortable[0x405]="KVS_ERR_QUEUE_QID_INVALID";
  errortable[0x406]="KVS_ERR_QUEUE_QSIZE_INVALID";
  errortable[0x195]="KVS_ERR_TIMEOUT";
  errortable[0x781]="KVS_ERR_UNCORRECTIBLE";
  errortable[0x900]="KVS_ERR_QUEUE_IN_SHUTDOWN";
  errortable[0x901]="KVS_ERR_QUEUE_IS_FULL";
  errortable[0x902]="KVS_ERR_COMMAND_SUBMITTED";
  errortable[0x091]="KVS_ERR_TOO_MANY_ITERATORS_OPEN";
  errortable[0x093]="KVS_ERR_ITERATOR_END";
  errortable[0x905]="KVS_ERR_SYS_BUSY";
  errortable[0x999]="KVS_ERR_COMMAND_INITIALIZED";
  errortable[0x09]="KVS_ERR_MISALIGNED_VALUE_OFFSET";
  errortable[0x0A]="KVS_ERR_MISALIGNED_KEY_SIZE";
  errortable[0x11]="KVS_ERR_UNRECOVERED_ERROR";
  errortable[0x81]="KVS_ERR_MAXIMUM_VALUE_SIZE_LIMIT_EXCEEDED";
  errortable[0x92]="KVS_ERR_ITERATE_HANDLE_ALREADY_OPENED";
  errortable[0x94]="KVS_ERR_ITERATE_REQUEST_FAIL";
  errortable[0x100]="KVS_ERR_DD_NO_DEVICE";
  errortable[0x102]="KVS_ERR_DD_INVALID_QUEUE_TYPE";
  errortable[0x103]="KVS_ERR_DD_NO_AVAILABLE_RESOURCE";
  errortable[0x105]="KVS_ERR_DD_UNSUPPORTED_CMD";
  errortable[0x200]="KVS_ERR_SDK_OPEN";
  errortable[0x201]="KVS_ERR_SDK_CLOSE";
  errortable[0x202]="KVS_ERR_CACHE_NO_CACHED_KEY";
  errortable[0x203]="KVS_ERR_CACHE_INVALID_PARAM";
  errortable[0x204]="KVS_ERR_HEAP_ALLOC_FAILURE";
  errortable[0x205]="KVS_ERR_SLAB_ALLOC_FAILURE";
  errortable[0x206]="KVS_ERR_SDK_INVALID_PARAM";
  errortable[0x302]="KVS_ERR_DECOMPRESSION";
  
}

const char *kvs_errstr(int32_t errorno) {
  return errortable[errorno].c_str();
}

int32_t kvs_open_device(const char *dev_path, kvs_device_handle *dev_hd) {
  int ret = 0;

  build_error_table();
  
  if (!g_env.initialized) {
    WRITE_WARNING(
		  "the library is not properly configured: please run kvs_init_env() first\n");
    return 0;
  }
	
  kvs_device_handle user_dev = (kvs_device_handle)malloc(sizeof(struct _kvs_device_handle));
  
  kv_device_priv *dev  = _find_local_device_from_path(dev_path, g_env.list_devices);
  if (dev == 0) {
    WRITE_ERR("can't find the device: %s\n", dev_path);
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
    ret = user_dev->driver->init(dev_path, !g_env.use_async, sq_core, cq_core, g_env.udd_option.mem_size_mb);
    g_env.udd_option.num_devices++;
  } else {
    fprintf(stderr, "WRN: Please specify spdk device path properly\n");
    exit(1);
  }
#else
  if(dev->isemul || dev->iskerneldev)
    ret = user_dev->driver->init(dev_path, g_env.configfile, g_env.queuedepth, g_env.is_polling);
#endif
  
  g_env.open_devices.push_back(user_dev);

  *dev_hd = user_dev;

  return ret;
}

int32_t kvs_close_device(kvs_device_handle user_dev) {
  
  delete user_dev->driver;
  g_env.open_devices.remove(user_dev);
  free(user_dev);
  return 0;
}


int32_t *kvs_create_container (kvs_device_handle dev_hd, const char *name, uint64_t sz_4kb, const kvs_container_context *ctx) {
  /*
  kvs_container *container = (kvs_container *)malloc(sizeof(kvs_container));

  container->name = (kvs_container_name *)malloc(sizeof(kvs_container_name));
  container->name->name = (char*)malloc(strlen(name));
  container->name->name_len = strlen(name);
  strcpy(container->name->name, name);

  dev_hd->list_containers.push_back(container);
  */
  return 0;
  
}

int32_t kvs_delete_container (kvs_device_handle dev_hd, const char *cont_name) {
  /*
  for (const auto &t: dev_hd->list_containers) {
    if(strcmp(t->name->name, cont_name) == 0) {
      fprintf(stdout, "KVSSD: Container %s is deleted\n", cont_name);
      if(t->name->name)
	free (t->name->name);
      if(t->name)
	free(t->name);
      free(t);
    }
  }
  */
  return 0;
}

int32_t kvs_open_container (kvs_device_handle dev_hd, const char* name, kvs_container_handle *cont_hd) {
  kvs_container_handle cont_handle = (kvs_container_handle)malloc(sizeof(struct _kvs_container_handle));
  cont_handle->dev = dev_hd;
  strcpy(cont_handle->name, name);

  dev_hd->driver->open_containers.push_back(cont_handle);

  *cont_hd = cont_handle;

  return 0;
}

int32_t kvs_close_container (kvs_container_handle cont_hd){
  
  //fprintf(stdout, "KVSSD: Container %s is closed \n", cont_hd->name);
  cont_hd->dev->driver->open_containers.remove(cont_hd);
  if(cont_hd)
    free(cont_hd);
  
  return 0;

}

int32_t kvs_store_tuple(kvs_container_handle cont_hd, const kvs_key *key,
		const kvs_value *value, const kvs_store_context *ctx) {
  const bool sync = (!g_env.use_async)
    || (ctx->option & KVS_SYNC_IO) == KVS_SYNC_IO;
  return cont_hd->dev->driver->store_tuple(0, key, value, 0/*ctx->option*/,
					   ctx->private1, ctx->private2, sync);
}
int32_t kvs_retrieve_tuple(kvs_container_handle cont_hd, const kvs_key *key,
		kvs_value *value, const kvs_retrieve_context *ctx) {
  const bool sync = (!g_env.use_async)
    || (ctx->option & KVS_SYNC_IO) == KVS_SYNC_IO;
  return cont_hd->dev->driver->retrieve_tuple(0, key, value, 0/*ctx->option*/,
					      ctx->private1, ctx->private2, sync);
}

int32_t kvs_delete_tuple(kvs_container_handle cont_hd, const kvs_key *key,
		const kvs_delete_context *ctx) {
  const bool sync = (!g_env.use_async)
    || (ctx->option & KVS_SYNC_IO) == KVS_SYNC_IO;
  return cont_hd->dev->driver->delete_tuple(0, key, 0/*ctx->option*/, ctx->private1,
					    ctx->private2, sync);
}

int32_t kvs_open_iterator(kvs_container_handle cont_hd, const kvs_iterator_context *ctx,
			  kvs_iterator_handle *iter_hd) {

  return cont_hd->dev->driver->open_iterator(0, ctx->option, ctx->bitmask,
					     ctx->bit_pattern, iter_hd);
}

int32_t kvs_close_iterator(kvs_container_handle cont_hd, kvs_iterator_handle hiter,
			   const kvs_iterator_context *ctx) {
  
  return cont_hd->dev->driver->close_iterator(0, hiter);
}

int32_t kvs_iterator_next(kvs_container_handle cont_hd, kvs_iterator_handle hiter,
			  kvs_iterator_list *iter_list, const kvs_iterator_context *ctx) {
  const bool sync = (!g_env.use_async)  ||
    (ctx->option & KVS_SYNC_IO) == KVS_SYNC_IO;
  /*
  if(sync) {
    fprintf(stderr, "WARN: Only support iterator under ASYNC mode\n");
    //exit(1);
  }
  */
  return cont_hd->dev->driver->iterator_next(hiter, iter_list, ctx->private1, ctx->private2, sync);
}

float kvs_get_waf(kvs_device_handle dev) {
  return dev->driver->get_waf();
}

int32_t kvs_get_device_utilization(kvs_device_handle dev) {
  return dev->driver->get_used_size();
}

int64_t kvs_get_device_capacity(kvs_device_handle dev) {
  return dev->driver->get_total_size();
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
