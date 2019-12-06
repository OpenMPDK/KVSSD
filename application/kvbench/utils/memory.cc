#include <stdlib.h>
#include <stdio.h>

#include "memory.h"
#if defined __KV_BENCH
#include "kvs_api.h"
#endif

FILE *cpu_fp = NULL;
std::string read_singleline_sysfile(const char *path) {
  std::string val;
  std::ifstream sysfile (path);
  if (sysfile.is_open() && !sysfile.eof()) {
    sysfile >> val;
    sysfile.close();
    return val;
  }
  return "";
}

int parse_cpuids(std::string &cpustr, int &first_cpuid, int &second_cpuid) {
  // parse e.g. '0' or '0-3'
  std::string item;
  std::stringstream ss(cpustr);
  if (std::getline(ss, item, '-')) {
    first_cpuid = std::atoi(item.c_str());
    if (std::getline(ss, item, '-')) {
      second_cpuid = std::atoi(item.c_str());
    } else {
      second_cpuid = first_cpuid;
    }
    return 0;
  }

  return -1;
}

int find_cpulist(int nodeid, cpu_info *cpuinfo, int config_only) {
  int num_cpus = 0, counters = 0;
  char path[512];
  int i;
  sprintf(path, "/sys/devices/system/node/node%d/cpulist", nodeid);

  std::string cpulist = read_singleline_sysfile(path);
  if (cpulist.length() <= 0) return -1;
  
  // parse e.g. '0,1,2,3... 5-8, 9'
  std::string item;
  std::stringstream ss(cpulist);

  while (std::getline(ss, item, ',')) {

    int first_cpuid, second_cpuid;

    int ret = parse_cpuids(item, first_cpuid, second_cpuid);
    if (ret < 0) return -1;

    num_cpus = second_cpuid - first_cpuid + 1;
    for (i = 0; i < num_cpus; i++) {
      if(config_only && cpu_fp) 
	fprintf(cpu_fp, "%d,%d,-1,-1\n",nodeid,first_cpuid + i);
      counters++;
    }
  }
  
  //return cpuinfo->cpulist[cpuid].size();
  return counters;
}

int get_cpuinfo(cpu_info *cpuinfo, int config_only) {

  int first_cpuid = 0, last_cpuid = numa_max_node();
  int i, j;  
  cpuinfo->num_numanodes = last_cpuid - first_cpuid + 1;
  if(config_only) {
    cpu_fp = fopen("cpu.txt", "w");
    fprintf(cpu_fp, "nodeid,coreid,dbid_load,dbid_perf\n");
  }
  
  for (i = 0; i < cpuinfo->num_numanodes; i++) {
    cpuinfo->num_cores_per_numanodes = find_cpulist(first_cpuid + i, cpuinfo, config_only);
    if (cpuinfo->num_cores_per_numanodes< 0) return -1;
  }

  if(cpu_fp) fclose(cpu_fp);

  return 0;
}

unsigned long get_curcpu(int *chip, int *core)
{
  unsigned long a,d,c;
  __asm__ volatile("rdtscp" : "=a" (a), "=d" (d), "=c" (c));
  *chip = (c & 0xFFF000)>>12;
  *core = c & 0xFFF;
  return ((unsigned long)a) | (((unsigned long)d) << 32);;
}

void initialize(void *memory, const pool_info *info, mempool_t *pool)
{
  pool->base = (unsigned char*)memory;
  pool->block_size = info->unitsize;
  pool->num_blocks = info->numunits;
  pool->num_freeblocks =  pool->num_blocks;
  pool->nextfreeblock  = pool->base;
  pool->num_initializedblocks = 0;
  pool->next_free_idx = 0;

}

void destroy(int allocatortype, mempool_t *pool) {

#if !defined __KV_BENCH
  if(allocatortype == NUMA_ALLOCATOR){
    free_mem_numa(pool->base);  
  }else if (allocatortype == POSIX_ALLOCATOR){
    free_mem_posix(pool->base);
  }
#else
  free_mem_spdk(pool->base);
#endif
  
  pool->base = NULL;
}

unsigned char* AddrFromIndex(uint32_t  i, mempool_t *pool)
{
  return pool->base + ( i * pool->block_size );
}

uint32_t IndexFromAddr(const unsigned char* p, mempool_t *pool) 
{
  return (((uint32_t )(p - pool->base)) / pool->block_size);
}

void *Allocate(mempool_t *pool)
{

  if (pool->num_initializedblocks < pool->num_blocks)
  {
    uint32_t *p = (uint32_t *)AddrFromIndex(pool->num_initializedblocks, pool);
    *p = pool->num_initializedblocks + 1;
    pool->num_initializedblocks++;
  }
  void *ret = NULL;
  if (pool->num_freeblocks > 0)
  {
    ret = (void *)pool->nextfreeblock;

    --(pool->num_freeblocks);
    if (pool->num_freeblocks != 0)
    {
      pool->nextfreeblock = AddrFromIndex(*((uint32_t *)pool->nextfreeblock), pool);
    }
    else
    {
      pool->nextfreeblock = NULL;
    }
  }

  return ret;
}

void DeAllocate(void* p, mempool_t *pool)
{
  if (p == NULL)
    return;

  if (pool->nextfreeblock != NULL)
  {
    (*(uint32_t *)p) = IndexFromAddr(pool->nextfreeblock, pool);
    pool->nextfreeblock = (unsigned char *)p;
  }
  else
  {
    *((uint32_t *)p) = pool->num_blocks;
    pool->nextfreeblock = (unsigned char *)p;
  }

  ++(pool->num_freeblocks);
  
}

void *allocate_mem_numa(int alignment, uint64_t size, int nodeid);
void *allocate_mem_posix(int alignment, uint64_t size, int nodeid);
#if defined __KV_BENCH 
void *allocate_mem_spdk(int alignment, uint64_t size, int nodeid);
#endif
void pool_setup(pool_info_t *info, mempool_t *pool, int nodeid)
{
  void *memory;
  uint64_t size = info->unitsize * info->numunits;

  if (size > 0) {

#if !defined __KV_BENCH
    if (info->allocatortype == NUMA_ALLOCATOR) {
      memory = allocate_mem_numa(info->alignment, size, nodeid);
    } else if(info->allocatortype == POSIX_ALLOCATOR) {
      memory = allocate_mem_posix(info->alignment, size, nodeid);
    } else {
      fprintf(stderr, "ERR: The application should use numa/posix as memory allocator\n");
      exit(0);
    }
#else
      memory = allocate_mem_spdk(info->alignment, size, nodeid);
#endif

    if (memory == NULL) {
      fprintf(stderr,
      "Allocate memory failed. "
      "Pool alignment is invalid: pool alignment = %d "
      "or Not enough memory on Socket %d : request size = %ld\n",
      info->alignment, nodeid, size );
      exit(0);
    }

    initialize(memory, info, pool);
  } else {
    fprintf(stderr, "ERR: The size of memory pool must be greater than 0\n");
    exit(0);
  }
}

void *numa_aligned_alloc(int alignment, uint64_t size, int nodeid)
{
  void *base;
  void **aligned;
  uint64_t total = size + alignment - 1 + sizeof(void*) + sizeof(uint64_t);
  if(nodeid != -1)
    base = numa_alloc_onnode(total, nodeid);
  else
    base= numa_alloc_local(total);

  aligned = (void**)((((uint64_t)base) + alignment - 1 + sizeof(void*) + sizeof(uint64_t)) & ~(alignment - 1));
  aligned[-1]  = base;
  aligned[-2]  = (void*)total;
  
  return aligned;
}

void *allocate_mem_numa(int alignment, uint64_t size, int nodeid)
{
  void *ptr = numa_aligned_alloc(alignment, size, nodeid);
  return ptr;

}
void *allocate_mem_posix(int alignment, uint64_t size, int nodeid)
{
  void *ptr;
  if(posix_memalign(&ptr, alignment, size) == 0) {
    return ptr;
  }
  
  return NULL;
}

#if defined __KV_BENCH 
void *allocate_mem_spdk(int alignment, uint64_t size, int nodeid){
  void *ptr = kvs_malloc(size, alignment);
  return ptr;
}

void free_mem_spdk(void *ptr)
{
  kvs_free(ptr);
}
#endif

void free_mem_numa(void *p)
{
  void *ptr = (((void**)p)[-1]);
  uint64_t size = (uint64_t)(((void**)p)[-2]);

  numa_free(ptr, size);
  
}
void free_mem_posix(void *ptr)
{
  free(ptr);
}

template<typename T>
static int get_value_from_sysfile(char *path, T &result) {
  std::ifstream sysdev_info (path);
  if (sysdev_info.is_open() && !sysdev_info.eof()) {
    sysdev_info >> result;
    return 0;
  }
  return -1;
}

int get_nvme_numa_node(char *devname, instance_info_t *info) {
  char sysdev_path[1024];
  sprintf(sysdev_path, "/sys/block/%s/device/device/numa_node", devname);

  get_value_from_sysfile(sysdev_path, info->nodeid_load);
  get_value_from_sysfile(sysdev_path, info->nodeid_perf);

  return 0;
}

