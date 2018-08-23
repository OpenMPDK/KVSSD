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


#ifndef KVS_TYPES_H
#define KVS_TYPES_H

/*! \mainpage A library for Samsung KV Devices
 *
 * \section intro_sec Introduction
 *
 * This is the main page of Libsirius. 
 *
 * \section dep_sec Install Dependencies 
 *
 *  Ubuntu/Debian:  
 *  \code 
 *   sudo ./install_deps.sh 
 *  \endcode 
 *
 *  On other systems, install-deps.sh needs to be manually modified. 
 * 
 * \section build_user_sec Build Applications using Libsirius binary
 * 
 *  Add the ./lib and ./include directories of libsirius to GCC/G++ look up paths, 
 *  and add the following link flag. 
 *  \code
 *  -I<includedir> -L<libdir> -lsirius 
 *  \endcode 
 *
 *  The main header file of this library is "kvs_types.h". 
 *  
 * \section build_sec Build Libsirius from Source Code 
 *
 * \subsection b_step2 Step 2: Build SPDK and DPDK
 *   \code
 *   make
 *   \endcode 
 * 
 *   It will build SPDK and DPDK libraries and create a build directory.  
 *
 * \subsection b_step3 Step 3: Compile Libsirius 
 *   \code
 *   cd build 
 *   make debug or make release
 *   \endcode
 *
 * 
 * 
 */
/** 
    \defgroup KV_API Functions
    \defgroup EnumTypes Constants
*/
#include <memory.h>
#include <stdio.h>
#include <pthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KVS_MIN_KEY_LEN 16
#define KVS_MAX_KEY_LEN 255
#define KVS_MIN_VALUE_LEN 16//64
#define KVS_MAX_VALUE_LEN (2*1024*1024)

  
/** Possible return values of KV APIs
 * 
 * \ingroup EnumTypes
 */
enum kvs_result {
  // Generic command status                
  KVS_SUCCESS                         =    0x0,        /*!< success */
  
  // warnings                
  KVS_WRN_COMPRESS                    =  0x0B0,        /*! < compression is not support and ignored */
  KVS_WRN_MORE                        =  0x300,        /*! < more data is available, but buffer is not enough */
  
  // errors                  
  KVS_ERR_DEV_CAPACITY                =  0x012,        /*! < device does not have enough space */
  KVS_ERR_DEV_INIT                    =  0x070,        /*! < device initialization failed */
  KVS_ERR_DEV_INITIALIZED             =  0x071,        /*! < device was initialized already */
  KVS_ERR_DEV_NOT_EXIST               =  0x072,        /*! < no device exists  */
  KVS_ERR_DEV_SANITIZE_FAILED         =  0x01C,        /*! < the previous sanitize operation failed */
  KVS_ERR_DEV_SANITIZE_IN_PROGRESS    =  0x01D,        /*! < the sanitization operation is in progress */
  KVS_ERR_ITERATOR_IN_PROGRESS        =  0x090,        /*! < iterator is in progress and operation fails because it is not allowed during iterator is in progress. */
  
  KVS_ERR_ITERATOR_NOT_EXIST          =  0x394,        /*! < no iterator exists */
  KVS_ERR_KEY_INVALID                 =  0x395,        /*! < key invalid (value of key is NULL) */
  KVS_ERR_KEY_LENGTH_INVALID          =  0x003,        /*! < key length is out of range (unsupported key length) */
  KVS_ERR_KEY_NOT_EXIST               =  0x010,        /*! < given key doesn't exist */
  KVS_ERR_NS_DEFAULT                  =  0x095,        /*! < default namespace cannot be modified, deleted, attached, or detached */
  KVS_ERR_NS_INVALID                  =  0x00B,        /*! < namespace does not exist */
  KVS_ERR_OPTION_INVALID              =  0x004,        /*! < device does not support the specified options */
  KVS_ERR_PARAM_NULL                  =  0x101,        /*! < no input pointer can be NULL */
  KVS_ERR_PURGE_IN_PRGRESS            =  0x084,        /*! < purge operation is in progress */
  KVS_ERR_SYS_IO                      =  0x303,        /*! < host failed to communicate with the device */
  KVS_ERR_SYS_PERMISSION              =  0x415,        /*! < caller does not have a permission to call this interface */
  KVS_ERR_VALUE_LENGTH_INVALID        =  0x001,        /*! < value length is out of range  */
  KVS_ERR_VALUE_LENGTH_MISALIGNED     =  0x008,        /*! < value length is misaligned. Value length shall be multiples of 4 bytes.  */
  KVS_ERR_VALUE_OFFSET_INVALID        =  0x002,        /*! < value offset is invalid meaning that offset is out of bound. */
  KVS_ERR_VENDOR                      =  0x0F0,        /*! < vendor-specific error is returned, check the system log for more details */
  
  // command specific status(errors)                
  KVS_ERR_BUFFER_SMALL                =  0x301,        /*! < provided buffer size too small for iterator_next operation */
  KVS_ERR_DEV_MAX_NS                  =  0x191,        /*! < maximum number of namespaces was created */
  KVS_ERR_ITERATOR_COND_INVALID       =  0x192,        /*! < iterator condition is not valid */
  KVS_ERR_KEY_EXIST                   =  0x080,        /*! < given key already exists (with KV_STORE_IDEMPOTENT option) */
  KVS_ERR_NS_ATTAHED                  =  0x118,        /*! < namespace was alredy attached */
  KVS_ERR_NS_CAPACITY                 =  0x181,        /*! < namespace capacity limit exceeds */
  KVS_ERR_NS_NOT_ATTACHED             =  0x11A,        /*! < device cannot detach a namespace since it has not been attached yet */
  KVS_ERR_QUEUE_CQID_INVALID          =  0x401,        /*! < completion queue identifier is invalid */
  KVS_ERR_QUEUE_SQID_INVALID          =  0x402,        /*! < submission queue identifier is invalid */
  KVS_ERR_QUEUE_DELETION_INVALID      =  0x403,        /*! < cannot delete completion queue since submission queue has not been fully deleted */
  
  KVS_ERR_QUEUE_MAX_QUEUE             =  0x104,        /*! < maximum number of queues are already created */
  KVS_ERR_QUEUE_QID_INVALID           =  0x405,        /*! < queue identifier is invalid */
  KVS_ERR_QUEUE_QSIZE_INVALID         =  0x406,        /*! < queue size is invalid  */
  
  KVS_ERR_TIMEOUT                     =  0x195,        /*! < timer expired and no operation is completed yet.  */

  // media and data integratiy status (error)                
  KVS_ERR_UNCORRECTIBLE               =  0x781,        /*! < uncorrectable error occurs */
  
  // quue in shutdown
  KVS_ERR_QUEUE_IN_SHUTDOWN           =  0x900,        /*! < queue in shutdown mode */

  // queue is full, unable to accept more IO
  KVS_ERR_QUEUE_IS_FULL               =  0x901,        /*! < queue is full */

  // the beginning state after being accepted into a submission queue
  KVS_ERR_COMMAND_SUBMITTED           =  0x902,        /*! < accepted state when a command is submitted */   
  
  // too many iterators open that exceeded supported max number of iterators
  KVS_ERR_TOO_MANY_ITERATORS_OPEN     =  0x091,        /*! < Exceeded max number of opened iterators */
  
  KVS_ERR_ITERATOR_END                =  0x093,        /*! < Indicate end of iterator operation */

  // Added for iterator next call that can return empty results
  KVS_ERR_SYS_BUSY                    =  0x905,        /*! < Retry is recommended */
  
  // initialized by caller before submission
  KVS_ERR_COMMAND_INITIALIZED         =  0x999,        /*! < start state when a command is initialized  */

  //
  // from udd
  //
  KVS_ERR_MISALIGNED_VALUE_OFFSET     = 0x09,           /**<  misaligned value offset */
  KVS_ERR_MISALIGNED_KEY_SIZE         = 0x0A,           /**<  misaligned key length(size) */
  KVS_ERR_UNRECOVERED_ERROR           = 0x11,           /**<  internal I/O error */
  KVS_ERR_MAXIMUM_VALUE_SIZE_LIMIT_EXCEEDED = 0x81,     /**<  value of given key is already full(KV_MAX_TOTAL_VALUE_LEN) */
  KVS_ERR_ITERATE_HANDLE_ALREADY_OPENED = 0x92,         /**<  fail to open iterator with given prefix/bitmask as it is already opened */
  KVS_ERR_ITERATE_REQUEST_FAIL        = 0x94,           /**<  fail to process the iterate request due to FW internal status */
  KVS_ERR_DD_NO_DEVICE                = 0x100,
  KVS_ERR_DD_INVALID_QUEUE_TYPE       = 0x102,
  KVS_ERR_DD_NO_AVAILABLE_RESOURCE    = 0x103,
  KVS_ERR_DD_UNSUPPORTED_CMD          = 0x105,

  //0x200 ~ 0x2FF for SDK Error
  KVS_ERR_SDK_OPEN                    = 0x200,          /**<  device(sdk) open failed */
  KVS_ERR_SDK_CLOSE                   = 0x201,          /**<  device(sdk) close failed */
  KVS_ERR_CACHE_NO_CACHED_KEY         = 0x202,          /**<  (kv cache) cache miss */
  KVS_ERR_CACHE_INVALID_PARAM         = 0x203,          /**<  (kv cache) invalid parameters */
  KVS_ERR_HEAP_ALLOC_FAILURE          = 0x204,          /**<  heap allocation fail for sdk operations */
  KVS_ERR_SLAB_ALLOC_FAILURE          = 0x205,          /**<  slab allocation fail for sdk operations */
  KVS_ERR_SDK_INVALID_PARAM           = 0x206,          /**<  invalid parameters for sdk operations */

  KVS_ERR_DECOMPRESSION               = 0x302,          /**<  retrieveing uncompressed value with KV_RETRIEVE_DECOMPRESSION option */
};    
  
/** Key-Value I/O options, used in
 *  \ref kvs_store_context, \ref kvs_retrieve_context, \ref kvs_delete_context
 * \ingroup EnumTypes
 */
enum kvs_io_options {
  KVS_STORE_POST 	    = 0b00000000, /*!< allows overwrites */
  KVS_STORE_IDEMPOTENT 	    = 0b00000001, /*!< do not overwrite */
  KVS_STORE_APPEND     	    = 0b00000010, /*!< append value */
  KVS_RETRIEVE_IDEMPOTENT   = 0b00000100, /*!< read a matching key-value pair*/
  KVS_RETRIEVE_DELETE       = 0b00001000, /*!< destructive read */
  KVS_DELETE_TUPLE          = 0b00010000, /*!< delete a key-value pair  */
  KVS_DELETE_GROUP          = 0b00100000, /*!< delete a group */
  KVS_ITER_DEFAULT          = 0b10000000, /*!< default iterator operation on key only*/ 
  KVS_SYNC_IO 		    = 0b01000000, /*!< convert ASYNC I/O to SYNC I/O */
};

/**
 * kv_iterator_option
 */
/* TODO: revisit this with other io options */
typedef enum {
  KVS_ITERATOR_OPT_KEY = 0x00, /*! < [DEFAULT] iterator command gets only key entries without values */
  KVS_ITERATOR_OPT_KV  = 0x01, /*! < iterator command gets key and value pairs */
} kvs_iterator_option;

/** Operation code 
 *
 * Specify the type of I/O operation. It will be used in the opcode field in \ref kv_iocb. 
 * \ingroup EnumTypes
 */

enum kvs_op {
    IOCB_ASYNC_PUT_CMD=1,
    IOCB_ASYNC_GET_CMD=2,
    IOCB_ASYNC_DEL_CMD=3,
    IOCB_ASYNC_ITER_OPEN_CMD=4,
    IOCB_ASYNC_ITER_CLOSE_CMD=5,
    IOCB_ASYNC_ITER_NEXT_CMD=6
};

/**
 * @brief options for SSD driver types
 */
  
enum kv_driver_types {
  KV_UDD  = 0x00,            /*!< KV spdk driver */
  KV_KDD  = 0x01,            /*!< KV kernel driver */
  KV_EMUL = 0x02,            /*!< KV emulator */
};

typedef uint8_t kvs_key_t;
typedef uint32_t kvs_value_t;
  
/** represents a key 
 *
 */
typedef struct {
  void *key;          /*!< a pointer to a key string */
  uint16_t length;    /*!< a length of a key */
} kvs_key;

/** represents a value 
 *
 */
typedef struct {
  void *value;        /*!< a pointer to a value */
  uint32_t length;    /*!< a length of a value */
  //uint32_t value_size;
  uint32_t offset;     /*!< an offset within a value */
} kvs_value;

#define G_ITER_KEY_SIZE_FIXED 16;

struct _kvs_iterator_handle;
typedef struct _kvs_iterator_handle * kvs_iterator_handle;

  /**
     kvs_iterator_list
     kvs_iterator_list represents an iterator group entries.  it is used for retrieved iterator entries as a return value for  kvs_interator_next() operation. nit specifies how many entries in the returned iterator list(it_list). it_list has the nit number of <key_length, key> entries when iterator is set with KV_ITERATOR_OPT_KEY and the nit number of <key_length, key, value_length, value> entries when iterator is set with KV_ITERATOR_OPT_KV.
  */  
typedef struct {
  uint32_t num_entries;   /*!< the number of iterator entries in the list */
  uint32_t size;          /*!< buffer size */
  int      end;           /*!< represent if there are more keys to iterate (end = 0) or not (end = 1) */
  void    *it_list;       /*!< iterator list buffer */
} kvs_iterator_list;

/** Result of an asynchronous IO operation 
 *
 *  This structure is passed to an IO callback function, 
 *  containing an information about the original request,
 *  and its result.
 */
typedef struct {
  uint8_t opcode;          /*!< an operation code (see \ref kvs_op) */
  uint32_t contid;         /*!< container ID */
  void *key;               /*!< key buffer */
  uint32_t keysize;        /*!< key size */
  void *value;             /*!< value buffer */
  uint32_t value_offset;   /*!< value offset */
  uint32_t valuesize;      /*!< valuesize */
  void *private1;          /*!< a pointer passed from a user */
  void *private2;          /*!< a pointer passed from a user */
  uint8_t option;          /*!< option */
  int64_t result;          /*!< I/O result (see \ref kvs_result) */
  kvs_iterator_handle *iter_handle;  /*!< iterator handler  */
} kv_iocb;

typedef pthread_t kvs_thread_t;

/** A function prototype for I/O callback function 
 *
 */
typedef void (*_on_iocomplete)(kv_iocb* ioctx);
typedef void (*_on_iothreadinit)(kvs_thread_t id);

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
    int is_polling;               /*!< polling or interrupt mode */
    _on_iocomplete iocomplete_fn; /*!< a pointer to an I/O callback function (e.g. void iocomplete(kv_iocb*)) */
  } aio;

  int ssd_type;
  struct {
    char core_mask_str[256];
    char cq_thread_mask[256];
    uint32_t mem_size_mb;
  } udd;
  const char *emul_config_file;
} kvs_init_options;

/** options for \ref kvs_delete_tuple()
*/
typedef struct {
  uint32_t option :8;             /*!< an option for a delete operation. one of the Delete options specified in \ref kvs_io_options */
  uint32_t reserved :24;          /*!< reserved */
  void *private1;                 /*!< a pointer to a user's I/O context information, which will be delivered to a callback function, unmodified */
  void *private2;                 /*!< the second pointer to a user's I/O context information */

} kvs_delete_context;

/** options for \ref kvs_store_tuple()
*/
typedef struct {
  uint32_t option :8;		/*!< an option for a store operation. one of the Delete options specified in \ref kvs_io_options */
  uint32_t reserved :24;        /*!< reserved */
  void *private1;		/*!< a pointer to a user's I/O context information, which will be delivered to a callback function, unmodified */
  void *private2;		/*!< the second pointer to a user's I/O context information */

} kvs_store_context;

/** options for \ref kvs_retrieve_tuple()
*/
typedef struct {
  uint32_t option :8;           /*!< an option for a retrieve operation. one of the Delete options specified in \ref kvs_io_options */
  uint32_t reserved :24;        /*!< reserved */
  void *private1;		/*!< a pointer to a user's I/O context information, which will be delivered to a callback function, unmodified */
  void *private2;		/*!< the second pointer to a user's I/O context information */

} kvs_retrieve_context;

/** options for \ref kvs_open_iterator()
 */
typedef struct {
  uint32_t option :8;           /*!< an option for a iterator operation. one of the iterator options specified in \ref kv_iterator_option */
  //kvs_group_condition *grp_cond;
  uint32_t bitmask;             /*!< bit mask for bit pattern to use */
  uint32_t bit_pattern;         /*!< bit pattern for condition */
  void *private1;               /*!< a pointer to a user's I/O context information, which will be delivered to a callback function, unmodified , only used for iterator_next call */
  void *private2;               /*!< the second pointer to a user's I/O context information */  
} kvs_iterator_context;


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

/*! KV Device 
 * 
 * It represents an opened KV device. 
 *
 * \see kvs_open_device
 * \struct kv_device
 */
struct _kvs_device_handle;
typedef struct _kvs_device_handle *kvs_device_handle;

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
  uint32_t option : 8;
  uint32_t reserved : 24;
} kvs_container_context;
  
/*! KV container
 *
 * It represents a container inside a KV device
 */
struct _kvs_container_handle;
typedef struct _kvs_container_handle* kvs_container_handle;
  
/*! Initialize the library
 * 
 *  This function must be called once to initialize the environment for the library.
 *  \ref kvs_init_options includes all the available options.
 *  \ingroup KV_API
 */

int32_t kvs_init_env(kvs_init_options* options);

/*! Set default options to \ref kvs_init_options 
 * 
 * \ingroup KV_API
 */
int32_t kvs_init_env_opts(kvs_init_options* options);

/*! Deinitialize the library
 * 
 * It closes all opened devices and releases any the system resources assigned by the library.
 * 
 * \ingroup KV_API
 */
int32_t kvs_exit_env();

/*! List KV devices 
 * 
 * It returns the list of KV devices in the system
 * 
 * \see kv_device_info for each information retrieved by this function
 * \ingroup KV_API
 */
//int32_t kvs_list_kvdevices(kv_device_info **devs, int size);

/*! Open a KV device 
 * 
 *  The device path can be either a kernel device path or a SPDK device path that 
 *  can be retrieved from \ref kv_device_info. The corresponding device driver will 
 *  be loaded internally. 
 * 
 *  TO open a KV emulator, please use the pseudo device path of "/dev/kvemul"
 *
 * \return \ref kv_device_handle : a device handle
 * 
 *\ingroup KV_API
 */
int32_t kvs_open_device(const char *dev_path, kvs_device_handle *dev_hd);

/*! Close a KV device 
 * 
 *  \ingroup KV_API
 */
int32_t kvs_close_device(kvs_device_handle user_dev);


/*! This API creates a new contrainer in a device. 
 * User needs to specify a unique container name as a null terminated string, and its capacity. 
 * The capacity is defined in 4KB units. 
 * A 0 (numeric zero) capacity of means no limitation where 
 * device capacity limits actual container capacity. 
 * The device assigns a unique id while a user assigns a unique name. 
 *
 * \param dev_hd kvs_device_handle data structure that includes unique device id 
 * \param name name of container
 * \param sz_4kb capacity of a container with respect to tuple size (key size + value size) in 4KB units 
 * \param ctx: group information to create container groups
 */

  /* Container related features are not supported yet 
   * A dummy container will be created after the call
   */
int32_t *kvs_create_container (kvs_device_handle dev_hd, const char *name, uint64_t sz_4kb, const kvs_container_context *ctx);
  
int32_t kvs_delete_container (kvs_device_handle dev_hd, const char *cont_name);

int32_t kvs_open_container (kvs_device_handle dev_hd, const char* name, kvs_container_handle *cont_hd);
  
int32_t kvs_close_container (kvs_container_handle cont_hd);
    
  
/*! Check and Process completed asynchronous I/Os
 * 
 *  It checks if there is any completed asynchronous I/Os and
 *  calls the user's callback function specified in \ref kvs_init_options.
 * 
 *  It should be called within the same thread that issued the I/O. 
 *
 *  \param cont_hd container handle  
 *  \param maxevents the maximum number of I/O events to process
 *  \return the number of events processed
 *  \ingroup KV_API
 */
int32_t kvs_get_ioevents(kvs_container_handle cont_hd, int maxevents);

/*! Store a KV pair 
 *
 * It stores a KV pair to a device. Depending on the type of KV driver, different memory contraints can be applied.
 * 
 * e.g. when using SPDK drivers, the buffers in \ref kvs_key and \ref kvs_value need to be allocated using 
 * \ref kvs_malloc or \ref kvs_zalloc.
 * 
 * Currently no option flags specified in \ref kvs_io_options are supported. \ref KVS_SYNC_IO flag is supported to 
 * enable synchronous I/O while asynchrnous I/O is being used. 
 * 
 * \param cont_hd container handle
 * \param key key to retrieve
 * \param value value to store
 * \param ctx options 
 *\ingroup KV_API
 */
int32_t kvs_store_tuple(kvs_container_handle cont_hd, const kvs_key *key, const kvs_value *value, const kvs_store_context *ctx);

/*! Retrieve a KV pair 
 *
 * Finds and returns the key-value pair. The value is an input/output parameter. It needs to provide an address of 
 * the value buffer and its size. The retreived data will be copied to the buffer. If the buffer size is not enough 
 * to store the results, it will return \ref KVS_ERR_VALUE.
 * 
 * Some memory constraints can be applied as described in \ref kvs_store_tuple. 
 * 
 * No retrieve options are supported yet except KVS_SYNC_IO. 
 * 
 * \param cont_hd container handle
 * \param key key to retrieve
 * \param value a value buffer where the output will be stored  [in/out]
 * \param ctx options 
 * \ingroup KV_API
 */
int32_t kvs_retrieve_tuple(kvs_container_handle cont_hd, const kvs_key *key, kvs_value *value, const kvs_retrieve_context *ctx);

/*! Delete a KV pair 
 * 
 * Deletes a key-value pair.
 * 
 * No delete options are supported yet except KVS_SYNC_IO. 
 * 
 * \param cont_hd container handle
 * \param key key to delete
 * \param ctx options 
 * \ingroup KV_API
 */
int32_t kvs_delete_tuple(kvs_container_handle cont_hd, const kvs_key *key, const kvs_delete_context *ctx);

/*! Open an iterator
 *
 * \param cont_hd container handle
 * \param ctx options
 * \param iter_hd : a pointer to iterator handler
 * \ingroup KV_API
 */
int32_t kvs_open_iterator(kvs_container_handle cont_hd, const kvs_iterator_context *ctx, kvs_iterator_handle *iter_hd);
/*! close an iterator
 * 
 * \param cont_hd container handle
 * \param hiter the iterator handler
 * \param ctx options
 * \ingroup KV_API
 */
int32_t kvs_close_iterator(kvs_container_handle cont_hd, kvs_iterator_handle hiter, const kvs_iterator_context *ctx);

/*! iterator next
 *
 * retrieve a next group of keys or key-value pairs in the iterator group 
 *
 * \param cont_hd container handle
 * \param hiter the iterator handler
 * \param iter_list output buffer for a set of keys or key-value pairs
 * \param ctx options
 * \ingroup KV_API
 */
int32_t kvs_iterator_next(kvs_container_handle cont_hd, kvs_iterator_handle hiter, kvs_iterator_list *iter_list, const kvs_iterator_context *ctx);

/*! Get WAF (Write Amplificaton Factor) in a KV NMVe Device 
 * \param dev device handle
 * \return \ref float : WAF 
 * \ingroup KV_API 
 */
//float kvs_get_waf(struct kv_device_api *dev);

/*! Get device used size in percentage in a KV NMVe Device
 * \param dev device handle
 * \return \ref int32_t : used size in % 
 * \ingroup KV_API
 */
int32_t kvs_get_device_utilization(kvs_device_handle dev);

int64_t kvs_get_device_capacity(kvs_device_handle dev);

/*! Returns an error string
 *
 *  It interpretes the return value of the functions listed here.
 *
 *\ingroup KV_API
 */
const char *kvs_errstr(int32_t errorno);
  
// memory
#define _FUNCTIONIZE(a,b)  a(b)
#define _STRINGIZE(a)      #a
#define _INT2STRING(i)     _FUNCTIONIZE(_STRINGIZE,i)
#define _LOCATION       __FILE__ ":" _INT2STRING(__LINE__)

void *_kvs_zalloc(size_t size_bytes, size_t alignment, const char *file);
void *_kvs_malloc(size_t size_bytes, size_t alignment, const char *file);
void  _kvs_free(void * buf, const char *file);

/*! Allocate aligened memory 
 * 
 * It allocates from hugepages when DPDK is enabled, 
 * otherwise allocate from the memory in the current NUMA node.
 *
 * \return a memory pointer if succeeded, 0 otherwise.
 *\ingroup KV_API
 */
#define kvs_malloc(size, alignment) _kvs_malloc(size, alignment, _LOCATION)
/*! Allocate zeroed aligned memory 
 *
 * It allocates from hugepages when DPDK is enabled, 
 * otherwise allocate from the memory in the current NUMA node.
 *
 * \return a memory pointer if succeeded, 0 otherwise.
 *\ingroup KV_API
 */
#define kvs_zalloc(size, alignment) _kvs_zalloc(size, alignment, _LOCATION)

/*! Free memory 
 *\ingroup KV_API
 */
#define kvs_free(buf) _kvs_free(buf, _LOCATION)
#define kvs_memstat() _kvs_report_memstat()

#ifdef __cplusplus
} // extern "C"
#endif

#endif
