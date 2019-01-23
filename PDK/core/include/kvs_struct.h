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


#ifndef KVS_STRUCT_H
#define KVS_STRUCT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool kvs_delete_error;          // [OPTION] delete a tuple if a key exists otherwise return KVS_ERR_KEY_NOT_EXIST error
} kvs_delete_option;

typedef struct {
  bool kvs_retrieve_delete;       // [OPTION] read the value of tuple and delete the tuple
  bool kvs_retrieve_decompress;   // [OPTION] decompress value before retrieval
} kvs_retrieve_option;

typedef enum {
  KVS_STORE_POST=0,            // [DEFAULT] if the key exists, overwrite it. if the key does not exist, insert it.
  KVS_STORE_UPDATE_ONLY,       // if the key exists, overwrite it. if the key does not exist, return error.
  KVS_STORE_NOOVERWRITE,       // if the key exists, return error. if the key does not exist, insert it. (=IDEMPOTENT)
  KVS_STORE_APPEND,            // if the key exists, append value. if the key does not exist, insert it.
} kvs_store_type;
  
typedef struct {
  kvs_store_type  st_type;     // store type
  bool kvs_store_compress;     // compress value before storing it to media
} kvs_store_option;


typedef enum {
  KVS_ITERATOR_KEY = 0,    // [DEFAULT] iterator command gets only key entries without value
  KVS_ITERATOR_KEY_VALUE,  // [OPTION] iterator command gets key and value pairs
  KVS_ITERATOR_WITH_DELETE, // [OPTION] iterator command gets key and delete
} kvs_iterator_type;
  
  
typedef struct {
  kvs_iterator_type  iter_type;
} kvs_iterator_option;

typedef enum {
  KVS_KEY_ORDER_NONE = 0,      // [DEFAULT] key ordering is not defined in a container
  KVS_KEY_ORDER_ASCEND,                // [OPTION] tuples are sorted in ascending key order in a container
  KVS_KEY_ORDER_DESCEND,       // [OPTION] tupleas are sorted in descending key order in a container
} kvs_key_order;
  
typedef struct {
  kvs_key_order ordering;
} kvs_container_option;

/** Operation code
 *
 * Specify the type of I/O operation. It will be used in the opcode field in \ref kv_iocb.
 * \ingroup EnumTypes
 */
enum kvs_op {
  IOCB_ASYNC_PUT_CMD=1,
  IOCB_ASYNC_GET_CMD=2,
  IOCB_ASYNC_DEL_CMD=3,
  IOCB_ASYNC_CHECK_KEY_EXIST_CMD=4,
  IOCB_ASYNC_ITER_OPEN_CMD=5,
  IOCB_ASYNC_ITER_CLOSE_CMD=6,
  IOCB_ASYNC_ITER_NEXT_CMD=7
};

/**
 * @brief options for SSD driver types
 */
#if 0
  enum kv_driver_types {
    KV_UDD  = 0x00,            /*!< KV spdk driver */
    KV_KDD  = 0x01,            /*!< KV kernel driver */
    KV_EMUL = 0x02,            /*!< KV emulator */
  };
#endif
  

typedef uint8_t kvs_key_t;
typedef uint32_t kvs_value_t;
typedef unsigned __int128 uint128_t;

struct _kvs_device_handle;
typedef struct _kvs_device_handle *kvs_device_handle;

/*! KV container
 *
 * It represents a container inside a KV device
 */
struct _kvs_container_handle;
typedef struct _kvs_container_handle* kvs_container_handle;
  /*
struct _kvs_iterator_handle;
typedef struct _kvs_iterator_handle * kvs_iterator_handle;
  */
typedef uint8_t kvs_iterator_handle;  

/**
   kvs_iterator_list
   kvs_iterator_list represents an iterator group entries.  it is used for retrieved iterator entries as a return value for  kvs_interator_next() operation. nit specifies how many entries in the returned iterator list(it_list). it_ \
   list has the nit number of <key_length, key> entries when iterator is set with KV_ITERATOR_OPT_KEY and the nit number of <key_length, key, value_length, value> entries when iterator is set with KV_ITERATOR_OPT_KV.
*/
typedef struct {
  uint32_t num_entries;   /*!< the number of iterator entries in the list */
  uint32_t size;          /*!< buffer size */
  int      end;           /*!< represent if there are more keys to iterate (end = 0) or not (end = 1) */
  void    *it_list;       /*!< iterator list buffer */
} kvs_iterator_list;

  
  /**
   * kvs_iterator_info
   * kvs_iterator_info contains iterator metadata associated with an iterator. 
   */
typedef struct {
  kvs_iterator_handle iter_handle;                /*!< iterator */
  uint8_t status;                     /*!< iterator status: 1(opened), 0(closed) */
  uint8_t type;                       /*!< iterator type */
  uint8_t keyspace_id;                /*!< KSID that the iterate handle deals with */
  uint32_t bit_pattern;               /*!< bit pattern for condition */
  uint32_t bitmask;                   /*!< bit mask for bit pattern to use */
  uint8_t is_eof;                     /*!< 1 (The iterate is finished), 0 (The iterate is not finished) */ 
  uint8_t reserved[3];
} kvs_iterator_info;
  
  
typedef struct {
  uint32_t name_len;
  char *name;
} kvs_container_name;
  
typedef struct {
  bool opened;                   // is this container opened
  uint8_t scale;                 // indicates the scale of the capacity and free space
  uint64_t capacity;             // container capacity in scale units
  uint64_t free_size;            // available space of container in scale units
  uint64_t count;                // # of Key Value tuples that exist in this container
  kvs_container_name *name;      // container name
} kvs_container;

typedef struct {
  int64_t capacity;           /*!< device capacity in bytes */
  uint128_t unalloc_capacity;   /*!< device capacity in bytes that has not been allocated to any container */
  uint32_t  max_value_len;      /*!< max length of value in bytes that device is able to support */
  uint32_t  max_key_len;        /*!< max length of key in bytes that device is able to support */
  uint32_t  optimal_value_len;  /*!< optimal value size */
  uint32_t  optimal_value_granularity;/*!< optimal value granularity */
  void      *extended_info;     /*!< vendor specific extended device information */
} kvs_device;

  /** represents a key
   *
   */
typedef struct {
  void *key;          /*!< a pointer to a key string */
  kvs_key_t length;    /*!< a length of a key */
} kvs_key;

  /** represents a value
   *
   */
typedef struct {
  void *value;        /*!< a pointer to a value */
  uint32_t length;    /*!< a length of a value */
  uint32_t actual_value_size;
  uint32_t offset;     /*!< an offset within a value */
} kvs_value;

typedef struct {
  uint32_t key_length : 16; /*!< key length in bytes */
  uint32_t reserved : 16;
  uint32_t value_length;    /*!< value length in bytes */
  uint8_t key[KVS_MAX_KEY_LENGTH];  /*!< key */
} kvs_tuple_info;

typedef pthread_t kvs_thread_t;

  /** options for \ref kvs_delete_tuple()
   */
typedef struct {
  kvs_delete_option option;       /*!< an option for a delete operation */
  void *private1;                 /*!< a pointer to a user's I/O context information, which will be delivered to a callback function, unmodified */
  void *private2;                 /*!< the second pointer to a user's I/O context information */
} kvs_delete_context;

  /** options for \ref kvs_store_tuple()
   */
typedef struct {
  kvs_store_option option;      /*!< an option for a store operation. */
  void *private1;               /*!< a pointer to a user's I/O context information, which will be delivered to a callback function, unmodified */
  void *private2;               /*!< the second pointer to a user's I/O context information */
} kvs_store_context;

  /** options for \ref kvs_retrieve_tuple()
   */
typedef struct {
  kvs_retrieve_option option;     /*!< an option for a retrieve operation. */
  void *private1;               /*!< a pointer to a user's I/O context information, which will be delivered to a callback function, unmodified */
  void *private2;               /*!< the second pointer to a user's I/O context information */
} kvs_retrieve_context;

  /** options for \ref kvs_open_iterator()
   */
typedef struct {
  kvs_iterator_option option;    /*!< an option for a iterator operation. */
  uint32_t bitmask;             /*!< bit mask for bit pattern to use */
  uint32_t bit_pattern;         /*!< bit pattern for condition */
  void *private1;               /*!< a pointer to a user's I/O context information, which will be delivered to a callback function, unmodified , only used for iterator_next call */
  void *private2;               /*!< the second pointer to a user's I/O context information */
} kvs_iterator_context;

typedef struct {
  void *private1;
  void *private2;
} kvs_exist_context;
  
typedef struct {
  kvs_container_option option;
} kvs_container_context;

typedef struct {
  uint8_t  opcode;                /*!< operation opcode */
  kvs_container_handle *cont_hd;  /*!< container handle */
  kvs_key *key;                   /*!< key data structure */
  kvs_value *value;               /*!< value data structure */
  uint32_t key_cnt;               /*!< kvs_exist_tuple_async */
  uint8_t *result_buffer;         /*!< kvs_exist_tuple_async result buffer */
  //uint8_t option;               /*!< operation option */
  void *private1;                 /*!< a pointer passed from a user */
  void *private2;                 /*!< a pointer passed from a user */
  kvs_result result;              /*!< IO result */
  kvs_iterator_handle *iter_hd;   /*!< iterator handle */
} kvs_callback_context;


  /** A function prototype for I/O callback function
   *
   */
typedef void (*_on_iothreadinit)(kvs_thread_t id);
typedef void (*kvs_callback_function)(kvs_callback_context* ioctx);

  /** options for the library
   *
   *  This structure contains all available options
   *  in the library.
   *
   * \see kvs_init_env_opts() that initializes this with default values
   * \see kvs_init_env() that initializes the library
   */
  
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

  int ssd_type;
  struct {
    char core_mask_str[256];
    char cq_thread_mask[256];
    uint32_t mem_size_mb;
    int syncio;
  } udd;
    const char *emul_config_file;
  } kvs_init_options;
  
  /** Device information
   *
   * This structure contains the information of a KV device.
   */
typedef struct {
  char node[1024];                  /*!< device path that is recognized by OS   */
  char spdkpath[1024];              /*!< device path that is recognized by our KV driver for SPDK */
  int  nsid;                        /*!< namespace ID */
  char pci_slot_name[1024];         /*!< PCI address to a device */
  int  numanode;                    /*!< numa node ID that this device is connected to */
  int  vendorid;                    /*!< vendor ID */
  int  deviceid;                    /*!< device ID */
  char ven_dev_id[128];             /*!< vendor + device ID */
} kv_device_info;

#ifdef __cplusplus
} // extern "C"
#endif

#endif
