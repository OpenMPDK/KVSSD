#ifndef __WORKLOAD_H
#define __WORKLOAD_H

#ifdef __AS_BENCH
#include "aerospike/as_record.h"
#endif

#include "../bench/arch.h"

struct latency_stat {
  uint64_t cursor;
  uint64_t nsamples;
  uint32_t *samples;
  spin_t lock;
};
/*
enum Operations  {
  OP_INSERT,
  OP_UPDATE,
  OP_GET,
  OP_DEL,
  OP_ITER_OPEN,
  OP_ITER_CLOSE,
  OP_ITER_NEXT
};
*/
typedef struct IoContext {
  int tid;
  int op;
  void *key;
  uint8_t keylength;
  void *value;
  uint32_t valuelength;
  //int freebuf;
  int result;
  int status;
  void *private1;
  void *private2;
  struct IoContext *prev;
  struct IoContext *next;
#if defined __AS_BENCH 
  as_key akey;
  as_record arec;
  //struct IoContext *prev;
  //struct IoContext *next;
  uint64_t start;
  int monitor;
  latency_stat *l_stat;
#endif
}IoContext_t;




#endif
