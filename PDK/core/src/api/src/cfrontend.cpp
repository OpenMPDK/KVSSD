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
#ifndef WITH_SPDK
#include "kvemul.hpp"
#include "kvkdd.hpp"
#else
#include "udd.hpp"
#endif

#include <map>
#include <list>
//#include <regex>
#include <string>

struct {
  bool initialized = false;
  bool use_spdk = false;
  int queuedepth;
  int is_polling = 0;
  std::map<std::string, kv_device_priv *> list_devices;
  std::list<kvs_device_handle > open_devices;
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

kvs_result kvs_exit_env() {

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

#if defined WITH_SPDK
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
  g_env.udd_option.syncio = options->udd.syncio;
  
  return 0;
}
#endif

kvs_result kvs_init_env(kvs_init_options* options) {
  
  if (g_env.initialized)
    return KVS_SUCCESS;

  if (options) {
    g_env.queuedepth = options->aio.queuedepth > 0 ? options->aio.queuedepth : 256;
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
		std::map<std::string, kv_device_priv *> &list_devices) {

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
    //return new KUDDriver(dev, g_env.iocomplete_fn);
    return new KUDDriver(dev, 0);
  }
#else
    if (dev->isemul) {
      return new KvEmulator(dev, 0);
      //return new KvEmulator(dev, g_env.iocomplete_fn );
    } else {
      return new KDDriver(dev, 0); 
      //return new KDDriver(dev, g_env.iocomplete_fn);
    }
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
}

const char *kvs_errstr(int32_t errorno) {
  return errortable[errorno].c_str();
}

kvs_result kvs_open_device(const char *dev_path, kvs_device_handle *dev_hd) {
  int ret = 0;

  if(dev_path == NULL)
  {
    fprintf(stderr, "Please specify a valid device path\n");
    return KVS_ERR_PARAM_INVALID;
  }
  
  build_error_table();
  
  if (!g_env.initialized) {
    WRITE_WARNING(
		  "the library is not properly configured: please run kvs_init_env() first\n");
    return (kvs_result)ret;
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
    ret = user_dev->driver->init(dev_path, g_env.udd_option.syncio/*!g_env.use_async*/, sq_core, cq_core, g_env.udd_option.mem_size_mb, g_env.queuedepth);
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

  return (kvs_result)ret;
}

kvs_result kvs_close_device(kvs_device_handle user_dev) {
  if(!user_dev)
    return KVS_ERR_PARAM_INVALID;
      
  auto t  = find(g_env.open_devices.begin(), g_env.open_devices.end(), user_dev);
  if (t == g_env.open_devices.end())
    return KVS_ERR_DEV_NOT_EXIST;
  
  delete user_dev->driver;
  delete user_dev->dev;
  g_env.open_devices.remove(user_dev);
  free(user_dev);
  return KVS_SUCCESS;
}


kvs_result kvs_create_container (kvs_device_handle dev_hd, const char *name, uint64_t size, const kvs_container_context *ctx) {
  /*
  kvs_container *container = (kvs_container *)malloc(sizeof(kvs_container));

  container->name = (kvs_container_name *)malloc(sizeof(kvs_container_name));
  container->name->name = (char*)malloc(strlen(name));
  container->name->name_len = strlen(name);
  strcpy(container->name->name, name);

  dev_hd->list_containers.push_back(container);
  */
  return KVS_SUCCESS;
  
}

kvs_result kvs_delete_container (kvs_device_handle dev_hd, const char *cont_name) {
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
  return KVS_SUCCESS;
}

kvs_result kvs_open_container (kvs_device_handle dev_hd, const char* name, kvs_container_handle *cont_hd) {
  kvs_container_handle cont_handle = (kvs_container_handle)malloc(sizeof(struct _kvs_container_handle));
  cont_handle->dev = dev_hd;
  strcpy(cont_handle->name, name);

  dev_hd->driver->open_containers.push_back(cont_handle);

  *cont_hd = cont_handle;

  return KVS_SUCCESS;
}

kvs_result kvs_close_container (kvs_container_handle cont_hd){

  cont_hd->dev->driver->open_containers.remove(cont_hd);
  if(cont_hd)
    free(cont_hd);
  
  return KVS_SUCCESS;

}

kvs_result kvs_get_container_info (kvs_container_handle cont_hd, kvs_container *cont) {

  fprintf(stdout, "WARN: not implemented yet\n");
  return KVS_SUCCESS;
}

kvs_result kvs_get_tuple_info (kvs_container_handle cont_hd, const kvs_key *key, kvs_tuple_info *info) {

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

  int ret = kvs_retrieve_tuple(cont_hd, key, &kvsvalue, &ret_ctx);
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

  int ret;

  if(key == NULL || value == NULL )
    return KVS_ERR_PARAM_INVALID;

  ret = validate_request(key, value);
  if(ret)
    return (kvs_result)ret;
  
  ret =  cont_hd->dev->driver->store_tuple(0, key, value, ctx->option,
					   ctx->private1, ctx->private2, 1, 0);
  return (kvs_result)ret;
}

kvs_result kvs_store_tuple_async(kvs_container_handle cont_hd, const kvs_key *key,
				 const kvs_value *value, const kvs_store_context *ctx,
				 kvs_callback_function cbfn) {
  int ret;
  if(key == NULL || value == NULL)
    return KVS_ERR_PARAM_INVALID;

  ret = validate_request(key, value);
  if(ret)
    return (kvs_result)ret;
  
  ret = cont_hd->dev->driver->store_tuple(0, key, value, ctx->option,
					   ctx->private1, ctx->private2, 0, cbfn);
  return (kvs_result)ret;
}


kvs_result kvs_retrieve_tuple(kvs_container_handle cont_hd, const kvs_key *key,
			      kvs_value *value, const kvs_retrieve_context *ctx) {

  int ret;
  if(key == NULL || value == NULL)
    return KVS_ERR_PARAM_INVALID;
  ret = validate_request(key, value);
  if(ret)
    return (kvs_result)ret;
  
  ret = cont_hd->dev->driver->retrieve_tuple(0, key, value, ctx->option,
					     ctx->private1, ctx->private2, 1, 0);
  return (kvs_result)ret;
}

kvs_result kvs_retrieve_tuple_async(kvs_container_handle cont_hd, const kvs_key *key,
				    kvs_value *value, const kvs_retrieve_context *ctx,
				    kvs_callback_function cbfn) {
  int ret;
  if(key == NULL || value == NULL)
    return KVS_ERR_PARAM_INVALID;

  ret = validate_request(key, value);
  if(ret)
    return (kvs_result)ret;
  
  ret = cont_hd->dev->driver->retrieve_tuple(0, key, value, ctx->option,
					      ctx->private1, ctx->private2, 0, cbfn);
  return (kvs_result)ret;
}


kvs_result kvs_delete_tuple(kvs_container_handle cont_hd, const kvs_key *key,
		const kvs_delete_context *ctx) {

  int ret;
  if(key == NULL)
    return KVS_ERR_PARAM_INVALID;
  ret = validate_request(key, 0);
  if(ret)
    return (kvs_result)ret;
  
  ret = cont_hd->dev->driver->delete_tuple(0, key, ctx->option, ctx->private1,
					    ctx->private2, 1, 0);
  return (kvs_result)ret;
}

kvs_result kvs_delete_tuple_async(kvs_container_handle cont_hd, const kvs_key* key,
				  const kvs_delete_context* ctx, kvs_callback_function cbfn) {
  int ret;
  if(key == NULL)
    return KVS_ERR_PARAM_INVALID;

  ret = validate_request(key, 0);
  if(ret) return (kvs_result)ret;
  
  ret = cont_hd->dev->driver->delete_tuple(0, key, ctx->option, ctx->private1,
					    ctx->private2, 0, cbfn);
  return (kvs_result)ret;
}

kvs_result kvs_exist_tuples(kvs_container_handle cont_hd, uint32_t key_cnt, const kvs_key *keys, uint32_t buffer_size, uint8_t *result_buffer, const kvs_exist_context *ctx) {
  int ret;
  if(keys == NULL || result_buffer == NULL)
    return KVS_ERR_PARAM_INVALID;

  ret = validate_request(keys, 0);
  if(ret) return (kvs_result)ret;
  
  ret = cont_hd->dev->driver->exist_tuple(0, key_cnt, keys, buffer_size, result_buffer, ctx->private1, ctx->private2, 1, 0); 
  return (kvs_result)ret;

}

kvs_result kvs_exist_tuples_async(kvs_container_handle cont_hd, uint32_t key_cnt, const kvs_key *keys, uint32_t buffer_size, uint8_t *result_buffer, const kvs_exist_context *ctx, kvs_callback_function cbfn) {
  int ret;
  if(keys == NULL || result_buffer == NULL)
    return KVS_ERR_PARAM_INVALID;
  ret = validate_request(keys, 0);
  if(ret) return (kvs_result)ret;
  
  ret = cont_hd->dev->driver->exist_tuple(0, key_cnt, keys, buffer_size, result_buffer, ctx->private1, ctx->private2, 0, cbfn);
  
  return (kvs_result)ret;
}

kvs_result kvs_open_iterator(kvs_container_handle cont_hd, const kvs_iterator_context *ctx,
			  kvs_iterator_handle *iter_hd) {
  int ret;
  ret = cont_hd->dev->driver->open_iterator(0, ctx->option, ctx->bitmask,
					     ctx->bit_pattern, iter_hd);
  return (kvs_result)ret;
}

kvs_result kvs_close_iterator(kvs_container_handle cont_hd, kvs_iterator_handle hiter,
			   const kvs_iterator_context *ctx) {
  int ret;
  ret =  cont_hd->dev->driver->close_iterator(0, hiter);
  return (kvs_result)ret;
}


kvs_result kvs_close_iterator_all(kvs_container_handle cont_hd) {

  int ret;
  ret = cont_hd->dev->driver->close_iterator_all(0);
  return (kvs_result)ret;
}

kvs_result kvs_list_iterators(kvs_container_handle cont_hd, kvs_iterator_info *kvs_iters, int count) {
  int ret;
  if(kvs_iters == NULL)
    return KVS_ERR_PARAM_INVALID;

  ret = cont_hd->dev->driver->list_iterators(0, kvs_iters, count);
  return (kvs_result)ret;
}

kvs_result kvs_iterator_next(kvs_container_handle cont_hd, kvs_iterator_handle hiter,
			  kvs_iterator_list *iter_list, const kvs_iterator_context *ctx) {

  int ret;
  if(iter_list == NULL)
    return KVS_ERR_PARAM_INVALID;
  ret = cont_hd->dev->driver->iterator_next(hiter, iter_list, ctx->private1, ctx->private2, 1, 0);
  return (kvs_result)ret;
}

kvs_result kvs_iterator_next_async(kvs_container_handle cont_hd, kvs_iterator_handle hiter,
				   kvs_iterator_list *iter_list, const kvs_iterator_context *ctx,
				   kvs_callback_function cbfn) {
  int ret;
  if(iter_list == NULL)
    return KVS_ERR_PARAM_INVALID;
  if(iter_list->it_list == NULL)
    return KVS_ERR_PARAM_INVALID; 
  
  ret = cont_hd->dev->driver->iterator_next(hiter, iter_list, ctx->private1, ctx->private2, 0, cbfn);
  return (kvs_result)ret;
}

float kvs_get_waf(kvs_device_handle dev) {
  return dev->driver->get_waf();
}

kvs_result kvs_get_device_info(kvs_device_handle dev_hd, kvs_device *dev_info) {

  int ret;

  auto t = find(g_env.open_devices.begin(), g_env.open_devices.end(), dev_hd);
  if(t == g_env.open_devices.end()) {
    ret = KVS_ERR_DEV_NOT_EXIST;
  } else {  
    ret = dev_hd->driver->get_total_size(&dev_info->capacity);
    dev_info->max_value_len = KVS_MAX_VALUE_LENGTH;
    dev_info->max_key_len = KVS_MAX_KEY_LENGTH;
    dev_info->optimal_value_len = KVS_OPTIMAL_VALUE_LENGTH;
  }
  return (kvs_result)ret;
}

kvs_result kvs_get_device_utilization(kvs_device_handle dev_hd, int32_t *dev_util) {
  int ret;
  auto t = find(g_env.open_devices.begin(), g_env.open_devices.end(), dev_hd);
  if(t == g_env.open_devices.end()) {
    ret = KVS_ERR_DEV_NOT_EXIST;
  } else
    ret = dev_hd->driver->get_used_size(dev_util);
  
  return (kvs_result)ret;
}

kvs_result kvs_get_device_capacity(kvs_device_handle dev_hd, int64_t *dev_capa) {
  int ret;
  auto t = find(g_env.open_devices.begin(), g_env.open_devices.end(), dev_hd);
  if(t == g_env.open_devices.end()) {
    ret = KVS_ERR_DEV_NOT_EXIST;
  } else
    ret = dev_hd->driver->get_total_size(dev_capa);
  return (kvs_result)ret;
}

kvs_result kvs_get_min_key_length (kvs_device_handle dev_hd, int32_t *min_key_length){
  *min_key_length = KVS_MIN_KEY_LENGTH;
  return KVS_SUCCESS;
}

kvs_result kvs_get_max_key_length (kvs_device_handle dev_hd, int32_t *max_key_length){
  *max_key_length = KVS_MAX_KEY_LENGTH;
  return KVS_SUCCESS;
}

kvs_result kvs_get_min_value_length (kvs_device_handle dev_hd, int32_t *min_value_length){
  *min_value_length = KVS_MIN_VALUE_LENGTH;
  return KVS_SUCCESS;
}

kvs_result kvs_get_max_value_length (kvs_device_handle dev_hd, int32_t *max_value_length){
  *max_value_length = KVS_MAX_VALUE_LENGTH;
  return KVS_SUCCESS;
}

kvs_result kvs_get_optimal_value_length (kvs_device_handle dev_hd, int32_t *opt_value_length){
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
