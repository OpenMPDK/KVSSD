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

int32_t kvs_exit_env() {

	std::list<kvs_device_handle > clone(g_env.open_devices.begin(),
			g_env.open_devices.end());

	//fprintf(stderr, "KVSSD: Close %d unclosed devices\n", (int) clone.size());

	for (kvs_device_handle t : clone) {

		kvs_close_device(t);

	}

	return 0;
}
/*
static const char *errstr[] = { "KVS_SUCCESS", "KVS_ERR_ALIGNMENT",
		"KVS_ERR_CAPAPCITY", "KVS_ERR_CLOSE", "KVS_ERR_CONT_EXIST",
		"KVS_ERR_CONT_NAME", "KVS_ERR_CONT_NOT_EXIST",
		"KVS_ERR_DEVICE_NOT_EXIST", "KVS_ERR_GROUP", "KVS_ERR_INDEX",
		"KVS_ERR_IO", "KVS_ERR_KEY", "KVS_ERR_KEY_TYPE", "KVS_ERR_MEMORY",
		"KVS_ERR_NULL_INPUT", "KVS_ERR_OFFSET", "KVS_ERR_OPEN",
		"KVS_ERR_OPTION_NOT_SUPPORTED", "KVS_ERR_PERMISSION", "KVS_ERR_SPACE",
		"KVS_ERR_TIMEOUT", "KVS_ERR_TUPLE_EXIST", "KVS_ERR_TUPLE_NOT_EXIST",
		"KVS_ERR_VALUE", "KVS_ERR_NOT_ENOUGH_CORES", "KVS_ERR_ITER_NOT_EXIST",
				"KVS_ERR_INVALID_CONFIGURATION", "KVS_ERR_KEY_MORE", "KVS_ERR_ITER_END"};

const char *kvs_errstr(int64_t errorno) {
	return errstr[abs((int) errorno)];
}
*/
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
	  g_env.queuedepth = options->aio.queuedepth;
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

inline bool is_emulator_path(const char *devpath) {
	return (devpath == 0 || strncmp(devpath, "/dev/kvemul", strlen("/dev/kvemul"))== 0);
}

inline bool is_spdk_path(const char *devpath) {
	return (strncmp(devpath, "trtype", 6) == 0);
}

kvs_device_handle kvs_open_device(const char *dev_path) {

	if (!g_env.initialized) {
		WRITE_WARNING(
				"the library is not properly configured: please run kvs_init_env() first\n");
		return 0;
	}
	
	kvs_device_handle user_dev = (kvs_device_handle)malloc(sizeof(struct _kvs_device_handle));

	kv_device_priv *dev  = _find_local_device_from_path(dev_path, g_env.list_devices);
	if (dev == 0) {
	  WRITE_ERR("can't find the device: %s\n", dev_path);
	  return 0;
	}
	
	dev->isopened = true;
	user_dev->dev = dev;
	user_dev->driver = _select_driver(dev);

#if defined WITH_SPDK
	if(dev->isspdkdev){
	  int curr_dev = g_env.udd_option.num_devices;
	  uint64_t sq_core = g_env.udd_option.core_masks[curr_dev];
	  uint64_t cq_core = g_env.udd_option.cq_masks[curr_dev];
	  user_dev->driver->init(dev_path, !g_env.use_async, sq_core, cq_core, g_env.udd_option.mem_size_mb);
	  g_env.udd_option.num_devices++;
	} else {
	  fprintf(stderr, "WRN: Please specify spdk device path properly\n");
	  exit(1);
	}
#else
	if(dev->isemul || dev->iskerneldev)
	  user_dev->driver->init(dev_path, g_env.configfile, g_env.queuedepth, g_env.is_polling);
#endif

	g_env.open_devices.push_back(user_dev);
	return user_dev;
}

int32_t kvs_close_device(kvs_device_handle user_dev) {

        delete user_dev->driver;
	g_env.open_devices.remove(user_dev);
	free(user_dev);
	return 0;
}


int32_t *kvs_create_container (kvs_device_handle dev_hd, const char *name, uint64_t sz_4kb, const kvs_container_context *ctx) {

  kvs_container *container = (kvs_container *)malloc(sizeof(kvs_container));

  container->name = (kvs_container_name *)malloc(sizeof(kvs_container_name));
  container->name->name = (char*)malloc(strlen(name));
  container->name->name_len = strlen(name);
  strcpy(container->name->name, name);

  dev_hd->list_containers.push_back(container);

  return 0;
  
}

int32_t kvs_delete_container (kvs_device_handle dev_hd, const char *cont_name) {

  for (const auto &t: dev_hd->list_containers) {
    if(strcmp(t->name->name, cont_name) == 0) {
      //fprintf(stdout, "KVSSD: Container %s is deleted\n", cont_name);
      if(t->name->name)
	free (t->name->name);
      if(t->name)
	free(t->name);
      free(t);
    }
  }

  return 0;
}

kvs_container_handle kvs_open_container (kvs_device_handle dev_hd, const char* name) {

  kvs_container_handle cont_handle = (kvs_container_handle)malloc(sizeof(struct _kvs_container_handle));
  cont_handle->dev = dev_hd;
  strcpy(cont_handle->name, name);

  dev_hd->open_containers.push_back(cont_handle);
  return cont_handle;

  return NULL;
}

int32_t kvs_close_container (kvs_container_handle cont_hd){
  
  
  //fprintf(stdout, "KVSSD: Conatainer %s is closed \n", cont_hd->name);
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

int32_t kvs_open_iterator(kvs_container_handle cont_hd,
			  const kvs_iterator_context *ctx) {

  const bool sync = (ctx->option & KVS_SYNC_IO) == KVS_SYNC_IO;
 
  if(sync) {
    fprintf(stderr, "WARN: Only support iterator under ASYNC mode\n");
    exit(1);
  }
  return cont_hd->dev->driver->open_iterator(0, ctx->option, ctx->bitmask, ctx->bit_pattern, ctx->private1, ctx->private2);
}

int32_t kvs_close_iterator(kvs_container_handle cont_hd, kvs_iterator_handle *hiter, const kvs_iterator_context *ctx) {
  const bool sync = (ctx->option & KVS_SYNC_IO) == KVS_SYNC_IO;
  if(sync) {
    fprintf(stderr, "WARN: Only support iterator under ASYNC mode\n");
    exit(1);
  }
  return cont_hd->dev->driver->close_iterator(0, hiter, ctx->private1, ctx->private2);
}

int32_t kvs_iterator_next(kvs_container_handle cont_hd, kvs_iterator_handle *hiter, kvs_iterator_list *iter_list, const kvs_iterator_context *ctx) {
  const bool sync = (ctx->option & KVS_SYNC_IO) == KVS_SYNC_IO;
  if(sync) {
    fprintf(stderr, "WARN: Only support iterator under ASYNC mode\n");
    exit(1);
  }
  return cont_hd->dev->driver->iterator_next(hiter, iter_list, ctx->private1, ctx->private2);
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
