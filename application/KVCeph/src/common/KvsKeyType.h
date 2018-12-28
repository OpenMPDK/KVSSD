#ifndef KVSKEYTYPE_H
#define KVSKEYTYPE_H
#include <stdio.h>
#include <iostream>
#include <string.h>

#define USE_KVSKEYTYPE

// User key types

enum KvsKeyType
{
  KVSKEY_BENCH     = 0,
  KVSKEY_OSDMAP    = 1,
  KVSKEY_INCOSDMAP = 2,
  KVSKEY_SNAPMAPPER= 3,
  KVSKEY_PGLOG     = 4,
  KVSKEY_PGBIGINFO = 5,
  KVSKEY_INFOS     = 6,
  KVSKEY_7         = 7
};

union kvs_userkey
{
  struct  {
    uint32_t type:3;
    uint32_t user:29;
  } bits3;
  struct  {
    uint32_t type:3;
    uint32_t preferred:1;
    uint32_t shardid: 8;    // 12
    uint32_t seed: 16;      // 28
    uint32_t pool: 4;       // 32
  } spg;
  uint32_t userkey;
};



inline ghobject_t kvs_get_osdmap_pobject_name(epoch_t epoch) {
  union kvs_userkey key;
  // epoch - 4 byte
  key.bits3.type = KVSKEY_OSDMAP;
  key.bits3.user = epoch;

  return ghobject_t(hobject_t(sobject_t(object_t(std::string((const char*)&key.userkey, 4)), 0)));
}

inline ghobject_t kvs_get_inc_osdmap_pobject_name(epoch_t epoch) {
  union kvs_userkey key;
  // epoch - 4 byte
  key.bits3.type = KVSKEY_INCOSDMAP;
  key.bits3.user = epoch;

  return ghobject_t(hobject_t(sobject_t(object_t(std::string((const char*)&key.userkey, 4)), 0)));
}

inline ghobject_t kvs_make_snapmapper_oid() {
  union kvs_userkey key;
  // epoch - 4 byte
  key.bits3.type = KVSKEY_SNAPMAPPER;
  key.bits3.user = 0;

  return ghobject_t(hobject_t(sobject_t(object_t(std::string((const char*)&key.userkey, 4)),0)));
}

inline ghobject_t kvs_make_pg_log_oid(spg_t pg) {
  union kvs_userkey key;
  // epoch - 4 byte
  key.spg.type =  KVSKEY_PGLOG;
  key.spg.shardid   =  (!pg.is_no_shard())? (uint8_t)(pg.shard.id):0;
  key.spg.preferred =  (pg.pgid.m_preferred >= 0)? 1:0;
  key.spg.seed      =  pg.pgid.m_seed;
  key.spg.pool      =  pg.pgid.m_pool;

  return ghobject_t(hobject_t(sobject_t(object_t(std::string((const char*)&key.userkey, 4)),0)));
}

inline ghobject_t kvs_make_pg_biginfo_oid(spg_t pg) {
    union kvs_userkey key;
    // epoch - 4 byte
    key.bits3.type = KVSKEY_PGBIGINFO;
    key.bits3.user = 0;

    return ghobject_t(hobject_t(sobject_t(object_t(std::string((const char*)&key.userkey, 4)), 0)));
}

inline ghobject_t kvs_make_infos_oid() {
    union kvs_userkey key;
    // epoch - 4 byte
    key.bits3.type = KVSKEY_INFOS;
    key.bits3.user = 0;

    hobject_t oid(sobject_t(std::string((const char*)&key.userkey, 4), CEPH_NOSNAP));
    return ghobject_t(oid);

}

#endif
