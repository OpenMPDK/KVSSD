#ifndef __MEMORY_H
#define __MEMORY_H

#include <numa.h>
#include <stdint.h>
#include <map>
#include <set>
#include <sstream>
#include <fstream>
#include <mutex>

#include "workload.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct instance_info {
  int nodeid_load;
  int nodeid_perf;
  int *coreids_pop;
  int *coreids_bench;
}instance_info_t;
    
typedef struct {
  int num_numanodes;
  int num_cores_per_numanodes;
  int **cpulist;
} cpu_info;
  
typedef enum {
  NO_POOL = 0,
  NUMA_ALLOCATOR = 1,
  POSIX_ALLOCATOR = 2,
  SPDK_ALLOCATOR = 3,
} allocator_t;

typedef struct pool_info {
  uint64_t unitsize;
  uint64_t numunits;
  int alignment;
  int allocatortype;
} pool_info_t;
  
typedef struct mempool {
  int32_t socketid;
  uint32_t num_blocks;
  uint32_t block_size;
  uint32_t num_freeblocks;
  uint32_t num_initializedblocks;
  unsigned char *base;
  unsigned char *nextfreeblock;
  uint32_t next_free_idx;
}mempool_t;

int get_cpuinfo(cpu_info *cpuinfo, int config_only);
unsigned long get_curcpu(int *chip, int *core);

  
  //void initialize(void *memory, const pool_info_t *info, mempool_t *pool);
void destroy(int allocatortype, mempool_t *pool);
void *Allocate(mempool_t *pool);
void DeAllocate(void* p, mempool_t *pool);  
void pool_setup(pool_info_t *info, mempool_t *pool, int nodeid);

void *allocate_mem_numa(int alignment, uint64_t size, int nodeid);
void *allocate_mem_posix(int alignment, uint64_t size, int nodeid);
void *allocate_mem_spdk(int alignment, uint64_t size, int nodeid);
void free_mem_numa(void *ptr);
void free_mem_posix(void *ptr);
void free_mem_spdk(void *ptr);
int get_nvme_numa_node(char *devname, instance_info_t *info);

inline void notify(IoContext *context, mempool_t *keypool, mempool_t *valuepool){
  //static std::mutex mu;
  //std::unique_lock<std::mutex> lock(mu);
  if(context->key) { DeAllocate(context->key, keypool);}
  if(context->value) { DeAllocate(context->value, valuepool);}
}
  
#ifdef __cplusplus
}
#endif

#endif
