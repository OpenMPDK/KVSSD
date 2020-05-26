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
#ifndef SAMSUNG_KVS_ADI_H
#define SAMSUNG_KVS_ADI_H

#include "stdint.h"

#ifdef __cplusplus
extern "C" {
#endif

// current implementation version
#define SAMSUNG_ADI_VERSION "0.5.0.0"

#define SAMSUNG_KV_MIN_KEY_LEN 4
#define SAMSUNG_KV_MAX_KEY_LEN 255
#define SAMSUNG_KV_MIN_VALUE_LEN 0
#define SAMSUNG_KV_MAX_VALUE_LEN (2*1024*1024)

#define SAMSUNG_MAX_ITERATORS 16

#define SAMSUNG_MAX_KEYSPACE_CNT 2
#define SAMSUNG_MIN_KEYSPACE_ID 0
#define KV_ALIGNMENT_UNIT 512


/**
 * return value from all interfaces
 */
typedef int32_t kv_result;

    // Generic command status                
#define    KV_SUCCESS                            0x0        ///< success

// warnings
#define    KV_WRN_MORE                          0xF000        ///< more data is available, but buffer is not enough

// errors                  
#define    KV_ERR_DEV_CAPACITY                  0x004     ///< device does not have enough space
#define    KV_ERR_DEV_INIT                      0x005     ///< device initialization failed
#define    KV_ERR_DEV_INITIALIZED               0x006     ///< device was initialized already
#define    KV_ERR_DEV_NOT_EXIST                 0x007     ///< no device exists 
#define    KV_ERR_DEV_SANITIZE_FAILED           0x008     ///< the previous sanitize operation failed

#define    KV_ERR_ITERATOR_NOT_EXIST            0x00C     ///< no iterator exists
#define    KV_ERR_ITERATOR_ALREADY_OPEN         0x00D     ///< terator is already open
#define    KV_ERR_KEY_INVALID                   0x00F     ///< key invalid (value of key is NULL)
#define    KV_ERR_KEY_LENGTH_INVALID            0x010     ///< key length is out of range (unsupported key length)
#define    KV_ERR_KEY_NOT_EXIST                 0x011     ///< given key doesn't exist
#define    KV_ERR_NS_DEFAULT                    0x302     ///< default namespace cannot be modified, deleted, attached, or detached
#define    KV_ERR_NS_INVALID                    0x303     ///< namespace does not exist
#define    KV_ERR_OPTION_INVALID                0x012     ///< device does not support the specified options
#define    KV_ERR_PARAM_INVALID                 0x013     ///< no input pointer can be NULL
#define    KV_ERR_PURGE_IN_PRGRESS              0x014     ///< purge operation is in progress
#define    KV_ERR_SYS_IO                        0x01E     ///< host failed to communicate with the device
#define    KV_ERR_VALUE_LENGTH_INVALID          0x021     ///< value length is out of range
#define    KV_ERR_VALUE_LENGTH_MISALIGNED       0x022     ///< value length is misaligned. Value length shall be multiples of 4 bytes.
#define    KV_ERR_VALUE_OFFSET_INVALID          0x023     ///< value offset is invalid meaning that offset is out of bound.
#define    KV_ERR_VENDOR                        0x025     ///< vendor-specific error is returned, check the system log for more details
#define    KV_ERR_PERMISSION                    0x026     ///< unable to open device due to permission error
#define    KV_ERR_MISALIGNED_VALUE_OFFSET       0x20C     ///< misaligned value offset


// command specific status(errors)               
#define    KV_ERR_BUFFER_SMALL                  0x001     ///< provided buffer size too small for iterator_next operation
#define    KV_ERR_DEV_MAX_NS                    0x304     ///< maximum number of namespaces was created
#define    KV_ERR_ITERATOR_COND_INVALID         0x00A     ///< iterator condition is not valid
#define    KV_ERR_KEY_EXIST                     0x00E     ///< given key already exists (with KV_STORE_IDEMPOTENT option)
#define    KV_ERR_NS_ATTAHED                    0x300     ///< namespace was alredy attached
#define    KV_ERR_NS_CAPACITY                   0x301     ///< namespace capacity limit exceeds
#define    KV_ERR_NS_NOT_ATTACHED               0x305     ///< device cannot detach a namespace since it has not been attached yet
#define    KV_ERR_QUEUE_CQID_INVALID            0x015     ///< completion queue identifier is invalid
#define    KV_ERR_QUEUE_SQID_INVALID            0x01C     ///< submission queue identifier is invalid
#define    KV_ERR_QUEUE_DELETION_INVALID        0x016     ///< cannot delete completion queue since submission queue has not been fully deleted

#define    KV_ERR_QUEUE_MAX_QUEUE               0x019     ///< maximum number of queues are already created
#define    KV_ERR_QUEUE_QID_INVALID             0x01A     ///< queue identifier is invalid
#define    KV_ERR_QUEUE_QSIZE_INVALID           0x01B     ///< queue size is invalid 

#define    KV_ERR_TIMEOUT                       0x01F     ///< timer expired and no operation is completed yet. 

// media and data integratiy status(error)                
#define    KV_ERR_UNCORRECTIBLE                 0x020     ///< uncorrectable error occurs

// quue in shutdown
#define    KV_ERR_QUEUE_IN_SHUTDOWN             0x017     ///< queue in shutdown mode

// queue is full, unable to accept more IO
#define    KV_ERR_QUEUE_IS_FULL                 0x018     ///< queue is full

// the beginning state after being accepted into a submission queue
#define    KV_ERR_COMMAND_SUBMITTED             0x003     ///< accepted state when a command is submitted   

// too many iterators open that exceeded supported max number of iterators
#define    KV_ERR_TOO_MANY_ITERATORS_OPEN       0x00B     ///< Exceeded max number of opened iterators

// Added for iterator next call that can return empty results
#define    KV_ERR_SYS_BUSY                      0x01D     ///< Retry is recommended

// initialized by caller before submission
#define    KV_ERR_COMMAND_INITIALIZED           0x002     ///< start state when a command is initialized 

#define    KV_ERR_DD_UNSUPPORTED_CMD            0x205     ///< invalid command or not yet supported

#define    KV_ERR_ITERATE_REQUEST_FAIL          0x209     ///< fail to process the iterate request

///< device driver does not support.
#define    KV_ERR_DD_UNSUPPORTED       0x02C 

//device does not support the specified keyspace
#define KV_ERR_KEYSPACE_INVALID        0x031

/**
 * \mainpage A libary for Samsung Key-Value Storage ADI
 */

// forward declaration of each handle
typedef uint8_t kv_iterator_handle;
struct kv_io_context;
struct _kv_device_handle;
typedef struct _kv_device_handle* kv_device_handle;
struct _kv_namespace_handle;
typedef struct _kv_namespace_handle* kv_namespace_handle;

struct _kv_queue_handle;
typedef struct _kv_queue_handle* kv_queue_handle;

typedef enum cmd_opcode_t {
    KV_OPC_FLUSH    = 0,

    KV_OPC_GET      = 1,
    KV_OPC_STORE    = 2,
    KV_OPC_DELETE   = 3,
    KV_OPC_PURGE    = 4,
    KV_OPC_CHECK_KEY_EXIST = 6,

    KV_OPC_OPEN_ITERATOR  = 7,
    KV_OPC_CLOSE_ITERATOR  = 8,
    KV_OPC_ITERATE_NEXT = 9,

    KV_OPC_SANITIZE_DEVICE = 10,

    KV_OPC_LIST_ITERATOR = 11,
    KV_OPC_DELETE_GROUP = 12,
    KV_OPC_ITERATE_NEXT_SINGLE_KV  = 13,
} cmd_opcode_t;

/** 
 * This type is used to represent a key length. Currently uint8_t is
 * used to represent a key which can grow up to KV_MAX_KEY_LEN bytes.
 */
typedef uint16_t kv_key_t;

/** 
 * This type is used to represent a value length. Currently uint32_t is
 * used to represent a value which can grow up to KV_MAX_VALUE_LEN bytes.
 */
typedef uint32_t kv_value_t;



/**
 * Linux interrupt handler definition
 */
typedef struct _kv_interrupt_handler {
  void (*handler)(void *, int); /**< interrupt handler */
  void *private_data;           /**< interrupt handler parameter */
  int number;
} interrupt_handler_t;

// struct _kv_interrupt_handler;
// forward declaration of _kv_interrupt_handler
typedef struct _kv_interrupt_handler* kv_interrupt_handler;

/**
  KV_MIN_KEY_LEN defines the minimum length of key in bytes that a key-value storage device shall support. The actual minimum key size for a device is vendor-specific and can be equal or larger than this value. The default value is 1 byte. The actual minimum key size of a device can be retrieved via kv_device.min_key_len returned by kv_get_device_info().  

 [SAMSUNG] 
 The minimum key size (kv_device.min_key_len) of Samsung PM983 is 4 bytes.
 */
#define KV_MIN_KEY_LEN SAMSUNG_KV_MIN_KEY_LEN // minimum key size 


/**
  The minimum length of value in bytes that a key-value storage device shall support. A key value device may support as small as this minimum value size. The actual minimum value size of device is vendor-specific and may be equal or larger than this value. The default value is 1 byte. The actual minimum value size of a device may be retrieved via kv_device.min_value_len returned by kv_get_device_info(). 

  [SAMSUNG]
  The minimum value size (kv_device.min_value_len) for Samsung PM983 is 0 bytes.
 */
#define KV_MIN_VALUE_LEN SAMSUNG_KV_MIN_VALUE_LEN // minimum value size 

/**
  KV_MAX_KEY_LEN defines the maximum length of key in bytes that a key-value storage device shall support. The actual maximum key size for a device is vendor-specific and can be equal or smaller than this value. The default value is 255 bytes. The actual value of a device can be retrieved via kv_device.max_key_len returned by kv_get_device_info().

  [SAMSUNG] 
  The maximum key size (kv_device.max_key_len) of Samsung PM983 is 255 bytes.
 */
#define KV_MAX_KEY_LEN SAMSUNG_KV_MAX_KEY_LEN // maximum key size 

/**
  The maximum length of value in bytes that a key-value storage device shall support. A device shall be able to handle the maximum size without any errors like timeout. The actual maximum value size for a device is vendor-specific and may be equal or smaller than this value. The default value is 2MB. The actual maximum value of a device may be retrieved via kv_device.max_value_len returned by kv_get_device_info().

  [SAMSUNG] 
  The maximum value size (kv_device.max_value_len) of Samsung PM983 is 2MB.
 */
#define KV_MAX_VALUE_LEN SAMSUNG_KV_MAX_VALUE_LEN // maximum value size (2MB) 

/**
 * This is used to apply an operation to all namespaces created in the device.
 */
#define KV_NAMESPACE_ALL (uint32_t)(-1) // all attached namespaces 

/**
 * The default namespace can be accessed without an actual handle.
 */
#define KV_NAMESPACE_DEFAULT 0 // default namespace
/**
 *  [SAMSUNG]
 */
#define SUBMISSION_Q_TYPE 0
/**
 *  [SAMSUNG]
 */
#define COMPLETION_Q_TYPE 1
/**
 * bool_t
 */
typedef enum {
  FALSE = 0x00,
  TRUE  = 0x01, 
} bool_t; 

/**
 * kv_device_type
 * [SAMSUNG] 
 *  Only KV_DEVICE_TYPE_KV is available for now.
 */
typedef enum {
  KV_DEVICE_TYPE_BLK = 0x00, ///< normal type of SSD
  KV_DEVICE_TYPE_KV  = 0x01, ///< [DEFAULT] KV type of SSD 
} kv_device_type; 


/**
 *  kv_delete_option
 */
typedef enum {
  KV_DELETE_OPT_DEFAULT = 0x00, ///< [DEFAULT] default operation for delete command(always return success)
  KV_DELETE_OPT_ERROR = 0x01, ///< [DEFAULT] return error when key does not exist
} kv_delete_option; 

/**
 * kv_exist_option
 */
typedef enum {
  KV_EXIST_OPT_DEFAULT = 0x00, ///< [DEFAULT] default operation for exist command
} kv_exist_option; 

/** 
 * kv_iterator_option
 */
typedef enum {
  KV_ITERATOR_OPT_KEY = 0x00, ///< [DEFAULT] iterator command gets only key entries without values
  KV_ITERATOR_OPT_KV  = 0x01, ///< iterator command gets key and value pairs
  KV_ITERATOR_OPT_KV_WITH_DELETE = 0x02, ///< iterator command gets key and value pairs
                                         ///< and delete the returned pairs
} kv_iterator_option; 

/**
 * kv_purge_option
 */
typedef enum {
  KV_PURGE_OPT_DEFAULT      = 0x00, ///< [DEFAULT] No secure erase option requested
  KV_PURGE_OPT_KV_ERASE     = 0x01, ///< All key-value pairs shalle be erased. Cryptographic erase may be performed if all data is encrypted
  KV_PURGE_OPT_CRYPTO_ERASE = 0x02, ///< Cryptographic Erase: all key-value pairs shall be erased cryptographically. This is accomplished by deleting the encryption key.
} kv_purge_option; 

/**
 * kv_retrieve_option
 * [OPTIONAL] decompress value before reading it from the storage 
 *   if a device has the capability. This is a hint to a device. 
 */
typedef enum {
  KV_RETRIEVE_OPT_DEFAULT    = 0x00, ///< [DEFAULT] retrieving value as it is written (even compressed value is also retrieved in its compressed form)
  KV_RETRIEVE_OPT_DELETE = 0x01,  
} kv_retrieve_option; 

// kv_sanitize_option
typedef enum {
  RESERVED                      = 0x00, ///< RESERVED
  RESERVED1                     = 0x01, ///< RESERVED
  KV_SANITIZE_OPT_KV_ERASE      = 0x02, ///< [DEFAULT] All key-value pairs shalle be erased. Cryptographic erase may be performed if all data is encrypted
  
  KV_SANITIZE_OPT_OVERWRITE     = 0x03, ///< alters all key-value pairs by writing a fixed data pattern defined in kv_sanitize_pattern.pattern to all location on the media 
  
  KV_SANITIZE_OPT_CRYPTO_ERASE  = 0x04, ///< Cryptographic Erase: all key-value pairs shall be erased cryptographically.This is accomplished by changing the media encryption keys for all locations on the media.
} kv_sanitize_option; 


// kv_store_option
typedef enum {
  KV_STORE_OPT_DEFAULT    = 0x00, ///< [DEFAULT] storing key value pair(or overwriting given value if key exists)
  KV_STORE_OPT_COMPRESS   = 0x01, ///< [OPTIONAL] compressing value before writing it to the storage if a device has the capability. This is a hint to a device, idempotent and compression can be simultaneously set if the device support compression.
  KV_STORE_OPT_IDEMPOTENT = 0x02, ///< [MANDATORY] storing KV pair only if the key in the pair does not exist already

  KV_STORE_OPT_UPDATE_ONLY = 0x03,
  KV_STORE_OPT_APPEND = 0x04,
} kv_store_option;

/**
 * kv_device structure represents a controller and has device-wide information.
 */
typedef struct {
  uint32_t version;         ///< KV spec version that this device is based on
  uint16_t max_namespaces;  ///< max # of namespaces that device can support
  uint16_t max_queues;      ///< max # of queues that device can support
  uint64_t capacity;       ///< device capacity in bytes
  kv_value_t min_value_len; ///< min length of value in bytes that device can support
  kv_value_t max_value_len; ///< max length of value in bytes that device can support
  kv_key_t min_key_len;     ///< min length of key in bytes that device can support
  kv_key_t max_key_len;     ///< max length of key in bytes that device can support
  void *extended_info;      ///< vendor specific extended device information.
} kv_device; 

/**
 * [SAMSUNG]
  Samsung PM983 provides additional device information which is specified in kv_samsung_device. This structure can be accessed via (kv_samsung_device *) kv_device.extended_info. This specifies key and value store units. Key store unit is 255 bytes and value store unit is 32 bytes inside PM983. It means that host could fully utilize device and achieve better performance when key size is 255 bytes and value length is multiples of 32 bytes even though the device can handle other key and value sizes that specified in KV_MIN_KEY_LEN, KV_MIN_VALUE_LEN, KV_MAX_KEY_LEN and KV_MAX_VALUE_LEN.
  */
typedef struct {
  kv_value_t key_unit;    ///< internal space management unit allocated for key in bytes
  kv_value_t value_unit;  ///< internal space management unit for value in bytes: 
} kv_samsung_device;      ///< optimization hint 

/**
 * kv_device_stat structure represents the device-wide statistics information.
 */
typedef struct {
  uint16_t namespace_count; ///< # of namespaces that the device currently maintains
  uint16_t queue_count;     ///< # of queues that the device currently maintains
  uint16_t utilization;     ///< device space utilization in an integer form of 0(0.00%) to 10000(100.00%).
  uint16_t waf;             ///< write amplification factor in an integer form of (xxx.xx)
  void *extended_info;      ///< vendor specific extended device information.  
} kv_device_stat; 

/**
 * kv_group_condition
  This structure defines information for kv_open_iterator() that will set up a group of keys matched with given bit_pattern within a range of bits masked by bitmask for iteration. For more details, \see kv_open_iterator
 */
typedef struct {
  uint32_t bitmask;     ///< bit mask for bit pattern to use
  uint32_t bit_pattern; ///< bit pattern for condition 
} kv_group_condition; 


/**
  kv_iterator
  kv_iterator represents a group of key and value pairs which is used for iteration. 
 */
typedef struct {
  uint8_t handle_id; ///< iterator identifier
  uint8_t status;
  uint8_t type; ///< iterator option
  uint8_t keyspace_id;
  uint32_t prefix; ///< iterator condition bit pattern
  uint32_t bitmask; ///< iterator condition bit mask
  uint8_t is_eof;
  uint8_t reserved[3];
} kv_iterator; 

/**
  kv_iterator_list
  kv_iterator_list represents an iterator group entries.  it is used for retrieved iterator entries as a return value for kv_interator_next() operation. nit specifies how many entries in the returned iterator list(it_list). it_list has the nit number of <key_length, key> entries when iterator is set with KV_ITERATOR_OPT_KEY and the nit number of <key_length, key, value_length, value> entries when iterator is set with KV_ITERATOR_OPT_KV.
  */
typedef struct {
  uint32_t num_entries;   ///< the number of iterator entries in the list
  bool   end;           ///< represent if there are more keys to iterate (end = 0) or not (end = 1)
  uint32_t size;      ///< buffer size
  uint8_t  *it_list;  ///< iterator list buffer
} kv_iterator_list;


/**
  kv_key
  A key consists of a pointer and its length. For a string type key, the key buffer holds a byte string without a null termination. The key field shall not be null. If the key buffer is NULL, an interface shall return an error, KV_ERR_KEY_INVALID.

  [SAMSUNG] 
  The valid key length ranges between 16 bytes and 255 bytes.
  */
typedef struct {
  void *key;        ///< a pointer to a key
  kv_key_t length;  ///< key length in byte unit, based on KV_MAX_KEY_LENGHT 
} kv_key; 

/**
  kv_value

  A value consists of a buffer pointer and its length. The memory allocated for the value buffer shall be physically contiguous. The value field shall not be null. The length field specifies the value buffer size, and its meanings depend on operation. For store operation, it is the actual data size stored in the value buffer in bytes. For retrieve operation, it is the size of value buffer in bytes to store data from device as an input, and it is set by the actual value size when the operation is successfully conducted. The offset field specifies the offset of the value and is used only for retrieve operation. For example, when a retrieve operation couldn't get a full value in any reason, then host can initiate a next retrieveal command with the offset meaning that the offset is just a starting point of the remaining value. The offset value is ignored in any other operation.


  [SAMSUNG]
  The valid value length for Samsung PM983 ranges between 64 bytes and 2M bytes. Value length shall be multiples of 4 bytes. Memory buffer shall be allocated in the multiples of 32 bytes (i.e., double words) for the value.
  */
typedef struct {
  void *value;        ///< buffer address for value

  kv_value_t length;           ///< value buffer size in byte unit for input and the retuned value length for output
  kv_value_t actual_value_size;        ///< actual value size that is stored in SSD
  kv_value_t offset;           ///< offset for value

} kv_value;

/**
  kv_namespace
  kv_namespace represents namespace information. 

  [SAMSUNG]
  \see kv_samsung_namespace for extended_info
  */
typedef struct {
  uint32_t nsid;        ///< namespace ID
  bool_t attached;      ///< is this namespace attached
  uint64_t capacity;   ///< storage space that may be allocated at one time as part of the namespace in bytes
  void *extended_info;  ///< vendor specific extended namespace information.
} kv_namespace; 
 
/**
  kv_namespace_stat
  kv_namespace_stat structure represents the namespace-wide statistics information.
  */
typedef struct {
  uint32_t nsid;                  ///< namespace identifier
  bool_t attached;                ///< is this namespace attached
  uint64_t capacity;             ///< storage space that may be allocated at one time as part of the namespace in bytes
  uint64_t unallocated_capacity; ///< unallocated capacity in bytes.
  void *extended_info;            ///< vendor specific extended namespace information.
} kv_namespace_stat; 
 

/**
  kv_queue
  Kv_queue structure represent queue information, and extended_info has vendor specific information. 
  */
typedef struct {
  uint16_t queue_id;            ///< queue identifier
  uint16_t queue_size;          ///< queue size
  uint16_t completion_queue_id; ///< completion queue identifier which used for submission queue creation
  uint16_t queue_type;          ///< queue type (0 for submission queue, 1 for completion queue)// vendor specific queue-related information
  void *extended_info;
} kv_queue; 
 
/**
  kv_queue_stat
  kv_queue_stat structure represents the queue statistics information.
 */
typedef struct {
  uint16_t queue_id;   ///< queue identifier
  void *extended_info;  ///< vendor specific extended device information.  
} kv_queue_stat; 

/**
  kv_sanitize_pattern
  This structure defines information for kv_sanitize() that erases all existing key-value pairs and overwrites whole storage media with the given pattern. For more details, \see kv_sanitize. 
 */
typedef struct {
  uint32_t pattern_length;  ///< sanitize overwrite pattern buffer size in byte unit
  void *pattern;            ///< buffer address for sanize overwrite pattern 
} kv_sanitize_pattern; 

/**
 * \defgroup device_interfaces
 */

/**
 * \ingroup device_interfaces
 *
  This interface initializes a device (controller) with a given information and returns a device handle (kv_device_handle *). The dev_init can be NULL. The implementation is vendor and device specific. If the initialization fails, it returns an error code, KV_ERR_DEVICE_INIT. For example, kv_initialize_device() used in a kernel driver can initialize HW queues, scan namespaces, and build internal data structures. If a device is initialized more than once, the subsequent calls fail and returns an error, KV_ERR_DEV_INITIALIZED.

  A device can be removed from a system by calling kv_cleanup_device().

  PARAMETERS
  IN dev_init 		implementation-specific device initialization parameter, NULL is allowed if no parameter is required. For Samsung, please use kvs_init_options
  OUT dev_hdl		device handle
  
  RETURNS
  KV_SUCCESS
  
  ERROR CODE
  KV_ERR_DEV_INIT			the device initialization failed, please check the dev_init parameter
  KV_ERR_DEV_INITIALIZED		this device has been initialized already
  KV_ERR_DEV_NOT_EXIST 		no device exists for the device handle
  KV_ERR_PARAM_INVALID 		dev_hdl cannot be NULL, a variable of kv_device_handle shall have been already allocated
  KV_ERR_SYS_IO 			the host failed to communicate with the device
  KV_ERR_SYS_PERMISSION 		this caller does not have a permission to call this interface
  KV_ERR_VENDOR			vendor-specific error is returned, check the system log for more details

 */
kv_result kv_initialize_device(void *dev_init, kv_device_handle *dev_hdl);


/**
  kv_cleanup_device
  \ingroup device_interfaces

  This interface cleanups internal data structures and frees resources of device. The device handle shall be invalidated once this interface is called. If this interface is called multiple times with the same device handle, only the first call shall be successful while the subsequent calls shall return an error, KV_ERR_DEV_NOT_EXIST.

  PARAMETERS
  IN dev_hdl		device handle
  
  RETURNS
  KV_SUCCESS	
  
  ERROR CODE
  KV_ERR_DEV_NOT_EXIST 		no device exists for the device handle
  KV_ERR_SYS_IO 			the host failed to communicate with the device
  KV_ERR_SYS_PERMISSION 		this caller does not have a permission to call this interface
  KV_ERR_VENDOR			vendor-specific error is returned, check the system log for more details
  */
kv_result kv_cleanup_device(kv_device_handle dev_hdl);

/**
  kv_get_device_info
  \ingroup device_interfaces

  This interface returns the device information referenced by the given device handle. To get Samsung vendor specific information, kv_samsung_device extended_info structure in kv_device should be allocated.
  
  PARAMETERS
  IN handle 	device handle
  OUT devinfo	caller allocated buffer for device info
  
  RETURNS
  KV_SUCCESS	
  
  ERROR CODE
  KV_ERR_DEV_NOT_EXIST 		no device exists for the device handle
  KV_ERR_PARAM_INVALID 		dev cannot be NULL, a variable of kv_device shall have been already allocated
  KV_ERR_SYS_IO 			the host failed to communicate with the device
  KV_ERR_SYS_PERMISSION 		this caller does not have a permission to call this interface
  KV_ERR_VENDOR			vendor-specific error is returned, check the system log for more details
  */ 
kv_result kv_get_device_info(const kv_device_handle dev_hdl, kv_device *devinfo);

/**
  kv_get_device_stat
  \ingroup device_interfaces

  This interface returns the device statistics referenced by the given device handle.  
  
  PARAMETERS
  IN handle 	device handle
  OUT dev		caller allocated buffer for device statistics
  
  RETURNS
  KV_SUCCESS	
  
  ERROR CODE
  KV_ERR_DEV_NOT_EXIST 		no device exists for the device handle
  KV_ERR_PARAM_INVALID 		dev_st cannot be NULL, a variable of kv_device_stat shall have been already allocated
  KV_ERR_SYS_IO 			the host failed to communicate with the device
  KV_ERR_SYS_PERMISSION 		this caller does not have a permission to call this interface
  KV_ERR_VENDOR			vendor-specific error is returned, check the system log for more details
  */
kv_result kv_get_device_stat(const kv_device_handle dev_hdl, kv_device_stat *dev_st);

kv_result kv_get_device_waf(const kv_device_handle dev_hdl, uint32_t *waf);


////////////////////////////////////////////////
// the following are Samsung ADI specific operation structure
// for io command

typedef struct {
    kv_retrieve_option option;
} op_get_struct_t;

typedef struct {
    kv_store_option option;
} op_store_struct_t;

typedef struct {
    kv_delete_option option;
} op_delete_struct_t;

typedef struct {
    kv_purge_option option;
} op_purge_struct_t;

typedef struct {
    kv_exist_option option;
    uint32_t keycount;
    uint8_t *result;
    uint32_t result_size;
} op_key_exist_struct_t;

typedef struct {
    kv_iterator_option it_op;
    kv_group_condition it_cond;
} op_iterator_open_struct_t;

typedef struct {
    kv_iterator_handle iter_hdl;
    kv_iterator_list *iter_list;
    kv_key *key;
    kv_value *value;
} op_iterator_next_struct_t;

typedef struct {
    kv_sanitize_option option;
    kv_sanitize_pattern *pattern;
} op_sanitize_struct_t;

typedef struct {
    kv_iterator_handle iter_hdl;
} op_close_iterator_struct_t;

typedef struct {
    kv_iterator *kv_iters;
    uint32_t *iter_cnt;
} op_list_iterator_struct_t;

typedef struct {
    kv_group_condition *grp_cond;
} op_delete_group_struct_t;

////////////////////////////////
// this part must be the same as the public portion of 
// io_ctx_t
struct kv_io_context{
    int opcode;
    const kv_key *key;
    kv_value *value;
    void *private_data;
    kv_result retcode;


    struct {
        uint8_t *buffer;
        uint32_t buffer_size ;
        uint32_t buffer_count;

        kv_iterator_handle hiter;
    } result;
    struct {
        int id;
        bool end;
        void *buf;
        int buflength;
    } hiter;
};


// general data structure for async IO
// contains all info for cmd execution
typedef struct { 
    cmd_opcode_t opcode;
    const kv_key *key;
    kv_value *value;
    void *private_data;
    kv_result retcode;
    


    struct {
        uint8_t *buffer;
        uint32_t buffer_size ;
        uint32_t buffer_count;

        kv_iterator_handle hiter;
    } result;

    struct {
        int id;
        bool end;
        void *buf;
        int buflength;
    } hiter;

//private
    void (*post_fn)(kv_io_context *op);   ///< asynchronous notification callback (valid only for async I/O)
    uint32_t timeout_usec;
    uint16_t qid;
    uint32_t nsid;
    int8_t ks_id;

    // command specific structure
    // see structures defined above
    union {
        op_sanitize_struct_t sanitize_info;
        op_get_struct_t get_info;
        op_store_struct_t store_info;
        op_delete_struct_t delete_info;
        op_purge_struct_t purge_info;
        op_key_exist_struct_t key_exist_info;
        op_iterator_open_struct_t iterator_open_info;
        op_iterator_next_struct_t iterator_next_info;
        op_close_iterator_struct_t iterator_close_info;
        op_list_iterator_struct_t iterator_list_info;
        op_delete_group_struct_t delete_group_info;
    } command;

} io_ctx_t;

////////////////////////////////////////////////

/**
  kv_postprocess_function
  kv_postprocess_function can be called and is used to specify the tasks that need to be done once an IO operation complete. Typical post-processing tasks are to send a signal to a thread to wake it up to implement synchronous IO semantics and/or to call a user-defined notification function to implement asynchronous IO semantics.
  */
typedef struct {
  void (*post_fn)(kv_io_context *op);   ///< asynchronous notification callback (valid only for async I/O)
  void *private_data;       ///< private data address which can be used in callback (valid only for async I/O)
} kv_postprocess_function; 
 

/**
  kv_sanitize
  \ingroup device_interfaces

  This interface shall post an operation to a submission queue to start a sanitize operation for the device. The sanitize operation types that may be supported are KV_ERASE, CRYPTO_ERASE, and Overwrite. This routines are processed in the background (i.e., completion of the kv_sanitize command does not indicate completion of the sanitize operation).
  While a sanitize operation is in progress, all controllers in the device shall abort any command not allowed during a sanitize operation with a status of Sanitize In Progress. After a sanitize operation fails, all controllers in the device shall abort any command not allowed during a sanitize operation with a status of Sanitize FAILED until a subsequent sanitize operation is started or successful recovery from the failed sanitize operation occurs.
  
  The precise behavior of this interface is determined by the options set by users. 
  * KV_SANITIZE_OPT_KV_ERASE [DEFAULT, MANDATORY]: this alters all user data (i.e., key-value pairs) with a low-level block erase method that is specific to the media within the device in which user data may be stored. 
  * KV_SANITIZE_OPT_CRYPTO_ERASE: this sanitize operation alters user data by changing the media encryption keys for all user data on the media within the device in which user data may be stored.
  * KV_SANITIZE_OPT_OVERWRITE: The Overwrite sanitize operation alters all user data by writing a fixed data pattern specified in kv_sanitize_pattern.pattern to all locations on the media within the device in which user data may be stored one or more times. 
  
  Overwrite patten is defined in kv_sanitize_pattern data structure that includes patten length and pattern (section5.4.12).
  
  If a user defines an interrupt handler (i.e., kv_set_interrupt_handler()) and a postprocess function (i.e., post_fn), the interrupt handler will call the postprocess function when the device triggers an interrupt to notify the completion of the operation. If no postprocess function is defined, the interrupt handler just finishes its operation. If no interrupt handler is defined but a postprocess function is defined, the function is ignored.
  
  If a postprocess function (i.e., post_fn) is defined, the interrupt handler (an interrupt handler defined by kv_set_interrupt_handler()) or the poller (kv_poll_completion()) will call the postprocess function when the device triggers an interrupt to notify the completion of the operation or when the poller detects that the device finished the specified operation.
  
  PARAMETERS
  IN que_hdl	queue handle
  IN ns_hdl		namespace handle, or KV_NAMESPACE_DEFAULT
  IN option		options defined in KV_PURGE_OPTION
  IN post_fn	a postprocess function which is called when the operation completes
  
  RETURNS
  KV_SUCCESS 
  
  ERROR CODE
  KV_ERR_DEV_NOT_EXIST 		no device exists
  KV_ERR_OPTION_INVALID		the device does not support the specified options
  KV_ERR_QUEUE_QID_INVALID	submission queue identifier is invalid
  KV_ERR_SANITIZE_FAILED		the previous sanitize operation failed
  KV_ERR_SANITIZE_IN_PROGRESS	the sanitization operation is in progress
  KV_ERR_SYS_IO 			the host failed to communicate with the device
  KV_ERR_SYS_PERMISSION 		this caller does not have a permission to call this interface
  KV_ERR_VENDOR			vendor-specific error is returned, check the system log for more details
  */
kv_result kv_sanitize (kv_queue_handle que_hdl, kv_device_handle dev_hdl, kv_sanitize_option option, kv_sanitize_pattern *pattern, kv_postprocess_function *post_fn);

/**
  \ingroup Queue_Interfaces
  kv_create_queue

  This interface creates a new queue with the given queue parameter and returns a queue handle.
  
  PARAMETERS
  IN dev_hdl 	device handle
  IN queinfo    queue parameter 
  IN/OUT que_hdl	queue handle
  
  RETURNS
  KV_SUCCESS	
  
  ERROR CODE
  KV_ERR_DEV_MAX_QUEUE		it reaches the maximum number of queues that a device can create
  KV_ERR_QUEUE_QID_INVALID	QID is invalid
  KV_ERR_QUEUE_QSIZE_INVALID	Queue size is invalid
  KV_ERR_QUEUE_CQID_INVALID	completion queue identifier is invalid
  KV_ERR_DEV_NOT_EXIST 		no device exists for the device handle
  KV_ERR_QUEUE_CREATE		the device cannot create a queue
  KV_ERR_PARAM_INVALID 		que and que_hdl cannot be NULL, both shall have been already allocated
  KV_ERR_SYS_IO 			the host failed to communicate with the device
  KV_ERR_SYS_PERMISSION 		this caller does not have a permission to call this interface
  KV_ERR_VENDOR			vendor-specific error is returned, check the system log for more details
  */
kv_result kv_create_queue(kv_device_handle dev_hdl, const kv_queue *queinfo, kv_queue_handle *que_hdl);

/**
  kv_delete_queue

  This interface deletes given queue. Host software needs to delete all corresponding submission queue prior to deleting a completion queue.
  
  PARAMETERS
  IN dev_hdl 	device handle
  IN que_hdl	queue handle
  
  RETURNS
  KV_SUCCESS	
  
  ERROR CODE
  KV_ERR_DEV_NOT_EXIST 		no device exists for the device handle
  KV_ERR_QUEUE_QID_INVALID	QID is invalid
  KV_ERR_QUEUE_DELETION_INVALID  Completion queue deletion is invalid. Submission queque has not been fully deleted.
  KV_ERR_SYS_IO 			the host failed to communicate with the device
  KV_ERR_SYS_PERMISSION 		this caller does not have a permission to call this interface
  KV_ERR_VENDOR			vendor-specific error is returned, check the system log for more details
  */
kv_result kv_delete_queue(kv_device_handle dev_hdl, kv_queue_handle que_hdl);


/**
  kv_get_queue_handles

  This interface retrieves all queue handles created. 
  
  PARAMETERS
  IN dev_hdl 		device handle
  OUT que_hdls		a caller allocated array of queue handle buffer as an input and filled queue handles as an output
  IN/OUT que_cnt		a caller allocated variable, allocated que_hdls size as an input, and filled que_hdls size as an output
  
  RETURNS
  KV_SUCCESS	
  
  ERROR CODE
  KV_ERR_DEV_NOT_EXIST 		no device exists for the device handle
  KV_ERR_PARAM_INVALID 		que_hdls and que_cnt cannot be NULL, both shall have been already allocated
  KV_ERR_SYS_IO 			the host failed to communicate with the device
  KV_ERR_SYS_PERMISSION 		this caller does not have a permission to call this interface
  KV_ERR_VENDOR			vendor-specific error is returned, check the system log for more details
  */
kv_result kv_get_queue_handles(const kv_device_handle dev_hdl, kv_queue_handle *que_hdls, uint16_t *que_cnt);


/**
  kv_get_queue_info

  This interface returns the queue information referenced by the given queue handle.  
  
  PARAMETERS
  IN dev_hdl	device handle
  IN que_hdl	queue handle
  OUT queinfo		caller allocated buffer for queue info
  
  RETURNS
  KV_SUCCESS	
  
  ERROR CODE
  KV_ERR_DEV_NOT_EXIST 		no device exists for the device handle
  KV_ERR_PARAM_INVALID 		que cannot be NULL, a variable of kv_queue shall have been already allocated
  KV_ERR_QUEUE_QID_INVALID	submission queue identifier is invalid
  KV_ERR_SYS_IO 			the host failed to communicate with the device
  KV_ERR_SYS_PERMISSION 		this caller does not have a permission to call this interface
  KV_ERR_VENDOR			vendor-specific error is returned, check the system log for more details
  */
kv_result kv_get_queue_info(const kv_device_handle dev_hdl, const kv_queue_handle que_hdl, kv_queue *queinfo);


/**
  kv_get_queue_stat

  This interface returns the queue statistics referenced by the given queue handle.  
  
  PARAMETERS
  IN dev_hdl	device handle
  IN que_hdl	queue handle
  OUT que_st	caller allocated buffer for queue statistics
  
  RETURNS
  KV_SUCCESS	
  
  ERROR CODE
  KV_ERR_DEV_NOT_EXIST 		no device exists for the device handle
  KV_ERR_PARAM_INVALID 		que cannot be NULL, a variable of kv_queue shall have been already allocated
  KV_ERR_QUEUE_QID_INVALID	submission queue identifier is invalid
  KV_ERR_SYS_IO 			the host failed to communicate with the device
  KV_ERR_SYS_PERMISSION 		this caller does not have a permission to call this interface
  KV_ERR_VENDOR			vendor-specific error is returned, check the system log for more details
  */
kv_result kv_get_queue_stat(const kv_device_handle dev_hdl, const kv_queue_handle que_hdl, kv_queue_stat *que_st);

/**
  \ingroup Namespace_Interfaces
  kv_create_namespace

  This interface creates a new namespace with the given namespace parameter (ns) and returns a namespace handle.
  
  [SAMSUNG]
  Samsung PM983 does not support multiple namespaces. If this interface is called, ADI returns KV_ERR_DEV_MAX_NS.
  
  PARAMETERS
  IN dev_hdl 	device handle
  IN ns		namespace parameter 
  IN/OUT ns_hdl	namespace handle
  
  RETURNS
  KV_SUCCESS	
  
  ERROR CODE
  KV_ERR_DEV_CAPACITY		device does not have enough space to create a namespace.
  KV_ERR_DEV_MAX_NS		it reaches the maximum number of namespaces that a device can create
  KV_ERR_DEV_NOT_EXIST 		no device exists for the device handle
  KV_ERR_NS_CAPACITY		a namespace capacity limit exceeds
  KV_ERR_PARAM_INVALID 		ns and ns_hdl cannot be NULL, both shall have been already allocated
  KV_ERR_SYS_IO 			the host failed to communicate with the device
  KV_ERR_SYS_PERMISSION 		this caller does not have a permission to call this interface
  KV_ERR_VENDOR			vendor-specific error is returned, check the system log for more details
  */
kv_result kv_create_namespace(kv_device_handle dev_hdl, const kv_namespace *ns, kv_namespace_handle *ns_hdl);

/**
  kv_delete_namespace
  
  This interface deletes given namespace. As a side effect of the delete operation, the namespace is detached from device as the namespace is no longer present in the system. It is recommended that host software detach device from a namespace prior to deleting the namespace.If this interface is called multiple times with the same handle, the first call is successful and deletes the namespace. However, the subsequent calls will fail and return an error, KV_ERR_NS_NOT_EXIST.
  
  [SAMSUNG]
  Samsung PM983 does not support multiple namespaces. A default namespace cannot be deleted. If this interface is called, this interface returns KV_ERR_NS_DEFAULT.
  
  PARAMETERS
  IN dev_hdl 	device handle
  IN ns_hdl		namespace handle, or KV_NAMESPACE_DEFAULT
  
  RETURNS
  KV_SUCCESS	
  
  ERROR CODE
  KV_ERR_DEV_NOT_EXIST 		no device exists for the device handle
  KV_ERR_NS_DEFAULT		default namespace cannot be modified, deleted, attached, or detached
  KV_ERR_NS_NOT_EXIST		the namespace does not exist
  KV_ERR_SYS_IO 			the host failed to communicate with the device
  KV_ERR_SYS_PERMISSION 		this caller does not have a permission to call this interface
  KV_ERR_VENDOR			vendor-specific error is returned, check the system log for more details
  */
kv_result kv_delete_namespace(kv_device_handle dev_hdl, kv_namespace_handle ns_hdl);


/**
  kv_attach_namespace
  
  This interface attaches a namespace to a device. A namespace shall be accessible only after the namespace is attached. Any access to a namespace without attachment shall return an error, KV_ERR_NS_NOT_ATTACHED. A namespace can be attached to only one controller.
  
  [SAMSUNG]
  Samsung PM983 does not support multiple namespaces. A default namespace is attached automatically when the device is initialized. If this interface is called, ADI returns KV_ERR_NS_DEFAULT.
  
  PARAMETERS
  IN dev_hdl 	device handle
  IN ns_hdl		namespace handle, or KV_NAMESPACE_DEFAULT
  
  RETURNS
  KV_SUCCESS	
  
  ERROR CODE
  KV_ERR_DEV_NOT_EXIST 		no device exists for the device handle
  KV_ERR_NS_ATTAHED		this namespace was attached already
  KV_ERR_NS_DEFAULT		default namespace cannot be modified, deleted, attached, or detached
  KV_ERR_NS_NOT_EXIST		the namespace does not exist
  KV_ERR_SYS_IO 			the host failed to communicate with the device
  KV_ERR_SYS_PERMISSION 		this caller does not have a permission to call this interface
  KV_ERR_VENDOR			vendor-specific error is returned, check the system log for more details
  */
kv_result kv_attach_namespace(kv_device_handle dev_hdl, kv_namespace_handle ns_hdl);

/**
  kv_detach_namespace
  This interface detaches a namespace from a device. If the namespace is not attached yet, it returns an error, KV_ERR_NS_NOT_ATTACHED. Once a namespace is detached, it is not accessible. Any access to the namespace shall return an error, KV_ERR_NOT_ATTACHED. 
  
  [SAMSUNG]
  Samsung PM983 does not support multiple namespaces. A default namespace is detached automatically when the device is cleaned up. If this interface is called, ADI returns KV_ERR_NS_DEFAULT.
  
  PARAMETERS
  IN dev_hdl 	device handle
  IN ns_hdl		namespace handle, or KV_NAMESPACE_DEFAULT
  
  RETURNS
  KV_SUCCESS	
  
  ERROR CODE
  KV_ERR_DEV_NOT_EXIST 		no device exists for the device handle
  		
  KV_ERR_NS_DEFAULT		default namespace cannot be modified, deleted, attached, or detached
  KV_ERR_NS_NOT_ATTACHED		the device cannot detach a namespace since it has not been attached yet
  KV_ERR_NS_NOT_EXIST		the namespace does not exist
  KV_ERR_SYS_IO 			the host failed to communicate with the device
  KV_ERR_SYS_PERMISSION 		this caller does not have a permission to call this interface
  KV_ERR_VENDOR			a vendor-specific error is returned, please check the system log for more details
  */
kv_result kv_detach_namespace(kv_device_handle dev_hdl, kv_namespace_handle ns_hdl);

/**
  kv_list_namespaces

  This interface retrieves a list of namespaces created in this device and returns them in the given namespace array, ns_hdls. The number of namespace handles are set to ns_cnt. ns_cnt has the buffer size for kv_namespace_handle as an input and it is set to the number of namespaces stored in the buffer as an output.
  
  [SAMSUNG]
  Samsung PM983 does not support multiple namespaces. It always returns the default namespace.
  
  PARAMETERS
  IN dev_hdl	device handle
  OUT ns_hdls	namespace array
  IN/OUT ns_cnt	buffer size in kv_namespace_handle count as an input and number of namespace handles stored in the buffer as an 
                  output
  
  RETURNS
  KV_SUCCESS	
  
  ERROR CODE
  KV_ERR_DEV_NOT_EXIST 		no device exists for the device handle
  KV_ERR_PARAM_INVALID 		ns_hdls and ns_cnt cannot be NULL, both shall have been already allocated
  KV_ERR_SYS_IO 			the host failed to communicate with the device
  KV_ERR_SYS_PERMISSION 		this caller does not have a permission to call this interface
  KV_ERR_VENDOR			vendor-specific error is returned, check the system log for more details
  */
kv_result kv_list_namespaces(const kv_device_handle dev_hdl, kv_namespace_handle *ns_hdls, uint32_t *ns_cnt);

/**
  kv_get_namespace_info

  This interface returns the namespace information.
  
  PARAMETERS
  IN dev_hdl 	device handle
  IN ns_hdl		namespace handle, or KV_NAMESPACE_DEFAULT
  OUT nsinfo	namespace information
  
  RETURNS
  KV_SUCCESS	
  
  ERROR CODE
  KV_ERR_DEV_NOT_EXIST 		no device exists for the device handle
  KV_ERR_NS_NOT_EXIST		the namespace does not exist
  KV_ERR_PARAM_INVALID 		ns cannot be NULL, it shall have been already allocated
  KV_ERR_SYS_IO 			the host failed to communicate with the device
  KV_ERR_SYS_PERMISSION 		this caller does not have a permission to call this interface
  KV_ERR_VENDOR			vendor-specific error is returned, check the system log for more details
  */
kv_result kv_get_namespace_info(const kv_device_handle dev_hdl, const kv_namespace_handle ns_hdl, kv_namespace *nsinfo);

/**
  kv_get_namespace_stat

  This interface returns the namespace statistics.
  
  PARAMETERS
  IN dev_hdl 	device handle
  IN ns_hdl		namespace handle, or KV_NAMESPACE_DEFAULT
  OUT ns_st	namespace statistics
  
  RETURNS
  KV_SUCCESS	
  
  ERROR CODE
  KV_ERR_DEV_NOT_EXIST 		no device exists for the device handle
  KV_ERR_NS_NOT_EXIST		the namespace does not exist
  KV_ERR_PARAM_INVALID 		ns_st cannot be NULL, it shall have been already allocated
  KV_ERR_SYS_IO 			the host failed to communicate with the device
  KV_ERR_SYS_PERMISSION 		this caller does not have a permission to call this interface
  KV_ERR_VENDOR			vendor-specific error is returned, check the system log for more details
  */
kv_result kv_get_namespace_stat(const kv_device_handle dev_hdl, const kv_namespace_handle ns_hdl, kv_namespace_stat *ns_st);

/**
  kv_purge
  
  This interface shall post an operation to a submission queue of device to delete all key-value pairs in the namespace. This routine works asynchronously and returns immediately regardless of whether the pairs are actually deleted from a device or not. As a part of kv_purge operation, the host may request a secure erase of the contents of the namespace. There are two types of secure erase. The KV_ERASE erases all key-value pairs in the namespace. The CRYPTO_ERASE erases all key-value pairs in the namespace by deleting the encryption key with which the user data was previously encrypted.
  The kv_purge command may fail if there are outstanding key-value commands to the namespace specified with ns_hdl. Any key-value commands for the namespace that has a kv_purge command in progress may fail with the KV_PURGE_IN_PROGRESS error.
  
  The precise behavior of this interface is determined by the option set by users. 
  * KV_PURGE_OPT_DEFAULT [MANDATORY]: this removes all key-value pairs. No secure erase is required
  * KV_PURGE_OPT_KV_ERASE: this removes all key-value pairs and includes secure erase. Cryptographic erase may be performed if all data is encrypted
  * KV_PURGE_OPT_CRYPT_ERASE: this cryptographically erases all key-value pairs. This is accomplished by deleting the encryption key
  
  This operation is not atomic.
  
  If a user defines an interrupt handler (i.e., kv_set_interrupt_handler()) and a postprocess function (i.e., post_fn), the interrupt handler will call the postprocess function when the device triggers an interrupt to notify the completion of the operation. If no postprocess function is defined, the interrupt handler just finishes its operation. If no interrupt handler is defined but a postprocess function is defined, the function is ignored.
  
  If a postprocess function (i.e., post_fn) is defined, the interrupt handler (an interrupt handler defined by kv_set_interrupt_handler()) or the poller (kv_poll_completion()) will call the postprocess function when the device triggers an interrupt to notify the completion of the operation or when the poller detects that the device finished the specified operation.
  
  
  [SAMSUNG]
  Samsung PM983 does not support cryptographic erase. If this interface is called, ADI returns KV_ERR_OPTION_INVALID.
  
  PARAMETERS
  IN que_hdl	queue handle
  IN ns_hdl		namespace handle, or KV_NAMESPACE_DEFAULT
  IN option		options defined in KV_PURGE_OPTION
  IN post_fn	a postprocess function which is called when the operation completes
  
  
  RETURNS
  KV_SUCCESS 
  
  ERROR CODE
  KV_ERR_NS_NOT_EXIST		the namespace does not exist
  KV_ERR_OPTION_INVALID		the device does not support the specified options
  
  KV_ERR_PURGE_IN_PROGRESS	purge operation in progress
  KV_ERR_QUEUE_QID_INVALID	submission queue identifier is invalid
  KV_ERR_SYS_IO 			the host failed to communicate with the device
  KV_ERR_SYS_PERMISSION 		this caller does not have a permission to call this interface
  KV_ERR_VENDOR			vendor-specific error is returned, check the system log for more details 
  */
kv_result kv_purge(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, uint8_t ks_id, kv_purge_option option, kv_postprocess_function *post_fn);

/**
  \ingroup Iterator_Interfaces
  kv_open_iterator

  This interface enables users to set up a group of keys such that they can be iterated within a namespace. In other words, kv_open_iterator() enables a device to prepare a group of keys for iteration by matching given bit pattern (kv_group_condition.bit_pattern) to all keys in the namespace considering bits indicated by kv_group_condition.bitmask. It also sets up the iterator option; kv_iterator_next() will retrieve only keys when kv_iterator_option is KV_ITERATOR_OPT_KEY while kv_iterator_next() will retrieve key and value pairs when kv_iterator_option is KV_ITERATOR_OPT_KV.
  
  If a user defines an interrupt handler (i.e., kv_set_interrupt_handler()) and a postprocess function (i.e., post_fn), the interrupt handler will call the postprocess function when the device triggers an interrupt to notify the completion of the operation. If no postprocess function is defined, the interrupt handler just finishes its operation. If no interrupt handler is defined but a postprocess function is defined, the function is ignored.
  
  If a postprocess function (i.e., post_fn) is defined, the interrupt handler (an interrupt handler defined by kv_set_interrupt_handler()) or the poller (kv_poll_completion()) will call the postprocess function when the device triggers an interrupt to notify the completion of the operation or when the poller detects that the device finished the specified operation.
  
  Once an IO successfully completes, kv_iterator_handle is returned. 
  
  [SAMSUNG]
  Samsung PM983 does support only one iterator at a specific time. Samsung PM983 supports only two bytes of keys from MSB for iterator condition as using 2 MSB fro a key.
  
  PARAMETERS
  IN que_hdl    queue handle
  IN ns_hdl             namespace handle, or KV_NAMESPACE_DEFAULT
  IN it_op              iterator option which defines the kv_iterator_next() return values
  IN it_cond    condition structure which contains bit pattern, bit mask, etc.
  IN post_fn    a postprocess function which is called when the operation completes

  OUTPUT:
    The result iter_hdl is obtained through callback function by accessing result.hiter from kv_io_context.
    Please see struction definition of kv_io_context for result.hiter. 

  RETURNS
  KV_SUCCESS
  
  ERROR CODE
  KV_ERR_COND_INVALID		iterator condition(match bitmask and pattern) is not valid
  KV_ERR_DEV_NOT_EXIST 		no device exists
  KV_ERR_ITER_EXIST		iterator is already open. No more iterator can be opened.
  KV_ERR_ITER_OP_INVALID 	iterator option is not valid
  KV_ERR_NS_NOT_EXIST		the namespace does not exist
  
  KV_ERR_QUEUE_QID_INVALID	submission queue identifier is invalid
  KV_ERR_SYS_IO 		the host failed to communicate with the device
  KV_ERR_SYS_PERMISSION 	this caller does not have a permission to call this interface
  KV_ERR_VENDOR			vendor-specific error is returned, check the system log for more details
 
  */
kv_result kv_open_iterator(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, uint8_t ks_id, const kv_iterator_option it_op, const kv_group_condition *it_cond, kv_postprocess_function *post_fn);

kv_result kv_open_iterator_sync(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, uint8_t ks_id, const kv_iterator_option it_op, const kv_group_condition *it_cond, uint8_t *iterhandle);
kv_result kv_close_iterator_sync(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, kv_iterator_handle iter_hdl);
kv_result kv_list_iterators_sync(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, kv_iterator *kv_iters, uint32_t *iter_cnt);
kv_result kv_iterator_next_sync(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, kv_iterator_handle iter_hdl, kv_iterator_list *iter_list);
/**
  kv_close_iterator

  This interface releases the given iterator group of iter_hdl in the given namespace, so the iterator operation ends. 
  
  PARAMETERS
  IN que_hdl	queue handle
  IN ns_hdl 	namespace handle
  IN iter_hdl	iterator handle
  
  RETURNS
  KV_SUCCESS	
  
  ERROR CODE
  
  KV_ERR_ITER_NOT_EXIST		the iterator group does not exist
  KV_ERR_NS_NOT_EXIST		the namespace does not exist/
  KV_ERR_PARAM_INVALID 		opt cannot be NULL
  KV_ERR_SYS_IO 		the host failed to communicate with the device
  KV_ERR_SYS_PERMISSION 	this caller does not have a permission to call this interface
  KV_ERR_VENDOR			vendor-specific error is returned, check the system log for more details
  */
kv_result kv_close_iterator(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, kv_iterator_handle iter_hdl, kv_postprocess_function  *post_fn);

/**
  kv_iterator_next

  This interface obtains a subset of keys or key-value pairs from an iterator group of iter_hdl within a namespace. In other words, kv_iterator_next() shall retrieve a next group of keys or key-value pairs in the iterator group(iter_hdl) that is set with kv_open_iterator() command. The retrieved values(iter_list) are either keys or key-value pairs based on the iterator option which is set by kv_open_iterator().
  
  When kv_store() or kv_delete() command whose key matches with an existing iterator group is received, the KV_ITERATOR_IN_PROGRESS error shall be returned meaning that host cannot change existing keys in an iterator group.
  
  In the output of this operation, iter_list.nkeys represents the number of iterator elements in iter_list.it_list and iter_list.end indicates if there are more elemements in iterator group after this operation. If iter_list.end is 0, there are more iterator group elements and host may run kv_iterator_next() again to retrieve those elements. If iter_list.end is 1, there is no more iterator group elements meaning that iterator reaches the end. 
  
  Output values(iter_list.it_list) are determined by the iterator option set by a user.
  * KV_ITERATOR_OPT_KEY [MANDATORY]: a subset of keys are returned in iter_list.it_list data structure
  * KV_ITERATOR_OPT_KV; a subset of key-value pairs are returned in iter_list.it_list data structure
  
  If a user defines an interrupt handler (i.e., kv_set_interrupt_handler()) and a postprocess function (i.e., post_fn), the interrupt handler will call the postprocess function when the device triggers an interrupt to notify the completion of the operation. If no postprocess function is defined, the interrupt handler just finishes its operation. If no interrupt handler is defined but a postprocess function is defined, the function is ignored.
  
  If a postprocess function (i.e., post_fn) is defined, the interrupt handler (an interrupt handler defined by kv_set_interrupt_handler()) or the poller (kv_poll_completion()) will call the postprocess function when the device triggers an interrupt to notify the completion of the operation or when the poller detects that the device finished the specified operation.
  
  PARAMETERS
  IN que_hdl    queue handle
  IN ns_hdl     namespace handle, or KV_NAMESPACE_DEFAULT
  IN iter_hdl   iterator handle
  IN post_fn    a postprocess function which is called when the operation completes
  OUT iter_list output buffer for a set of keys or key-value pairs
  
  RETURNS
  KV_SUCCESS
  
  KV_WRN_MORE                   More keys are available, but buffer is not enough
  
  ERROR CODE
  KV_ERR_DEV_NOT_EXIST          no device exists
  KV_ERR_ITER_INVALID           no iterator group exists
  KV_ERR_NS_NOT_EXIST           the namespace does not exist
  
  KV_ERR_QUEUE_QID_INVALID      submission queue identifier is invalid
  KV_ERR_SYS_IO                 the host failed to communicate with the device
  KV_ERR_SYS_PERMISSION         this caller does not have a permission to call this interface
  KV_ERR_VENDOR                 vendor-specific error is returned, check the system log for more details
  */
kv_result kv_iterator_next(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, kv_iterator_handle iter_hdl, kv_iterator_list *iter_list, kv_postprocess_function *post_fn);

/**
 *
 * kv_list_iterators

  This interface retrieves a list of all iterators (open or closed) in this device and returns them in the given iterator array, kv_iters. The number of iterator handles are set to iter_cnt. As an input, iter_cnt has the buffer size for kv_iterator. As an output, iter_cnt is set to the number of iterators stored in the buffer.
  
  If a user defines an interrupt handler (i.e., kv_set_interrupt_handler()) and a postprocess function (i.e., post_fn), the interrupt handler will call the postprocess function when the device triggers an interrupt to notify operation completion. If no postprocess function is defined, the interrupt handler just finishes its operation. If no interrupt handler is defined but a postprocess function is defined, the function is ignored.
  
  If a postprocess function (i.e., post_fn) is defined, the interrupt handler (an interrupt handler defined by kv_set_interrupt_handler()) or the poller (kv_poll_completion()) will call the postprocess function when the device triggers an interrupt to notify the completion of the operation or when the poller detects that the device finished the specified operation.
  
  PARAMETERS
  INPUT: que_hdl          queue handle
  INPUT: ns_hdl       namespace handle, or KV_NAMESPACE_DEFAULT
  INPUT: post_fn          a postprocess function which is called when the operation completes
  OUTPUT: kv_iters        iterator array
  IN/OUTPUT: iter_cnt input: buffer size in kv_iterator_handle count
  output: number of iterator handles stored in the buffer
  IN/OUTPUT: op_hdl        an operation handle for this operation, a buffer for the handle shall be created before calling this routine
  
  RETURNS
  KV_SUCCESS  
  
  ERROR CODE for command submission
  KV_ERR_NS_NOT_EXIST     the namespace does not exist
  KV_ERR_QUEUE_QID_INVALID    submission queue identifier is invalid
  KV_ERR_PARAM_INVALID       iter_hdls and iter_cnt cannot be NULL, both shall have been already allocated
  KV_ERR_SYS_IO           the host failed to communicate with the device
  
  ERROR CODE for operation completion
  KV_ERR_VENDOR           vendor-specific error is returned, check the system log for more details
*/
kv_result kv_list_iterators(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, kv_iterator *kv_iters, uint32_t *iter_cnt, kv_postprocess_function  *post_fn);

/**
  \ingroup Key-value Pair Interfaces

  kv_delete

  This interface shall post an operation to a submission queue of device to delete a key-value pair. This routine works asynchronously and returns immediately regardless of whether the pair is actually deleted from a device or not. 
  
  The precise behavior of this interface is determined by the options set by users. 
  * KV_DELETE_OPT_DEFAULT [MANDATORY]: this removes a key-value pair exactly matching to the given key. 
  
  All delete operations are atomic. If it completes and succeeds, it guarantees a pair is deleted wholly.
  
  If a user defines an interrupt handler (i.e., kv_set_interrupt_handler()) and a postprocess function (i.e., post_fn), the interrupt handler will call the postprocess function when the device triggers an interrupt to notify the completion of the operation. If no postprocess function is defined, the interrupt handler just finishes its operation. If no interrupt handler is defined but a postprocess function is defined, the function is ignored.
  
  If a postprocess function (i.e., post_fn) is defined, the interrupt handler (an interrupt handler defined by kv_set_interrupt_handler()) or the poller (kv_poll_completion()) will call the postprocess function when the device triggers an interrupt to notify the completion of the operation or when the poller detects that the device finished the specified operation.
  
  
  
  PARAMETERS
  IN que_hdl	queue handle
  IN ns_hdl		namespace handle, or KV_NAMESPACE_DEFAULT
  IN key		key
  IN option		options defined in kv_delete_option
  IN post_fn	a postprocess function which is called when the operation completes
  
  
  RETURNS
  KV_SUCCESS 
  
  ERROR CODE
  KV_ERR_DEV_CAPACITY		device does not have enough space to create a namespace.
  KV_ERR_DEV_NOT_EXIST 		no device exists
  KV_ERR_KEY_EXIST			the given key is already exists (with KV_STORE_IDEMPOTENT option)
  KV_ERR_KEY_INVALID		key format is invalid, or kv_key->key buffer is null
  KV_ERR_KEY_LENGTH_INVALID	the key length is out of range of kv_device.min_key_len and kv_device.max_key_len
  KV_ERR_KEY_NOT_EXIST		given key doesn't exist (unmapped-read error)
  KV_ERR_NS_NOT_EXIST		the namespace does not exist
  KV_ERR_OPTION_INVALID		the device does not support the specified options
  
  KV_ERR_QUEUE_QID_INVALID	submission queue identifier is invalid
  KV_ERR_SYS_IO 			the host failed to communicate with the device
  KV_ERR_SYS_PERMISSION 		this caller does not have a permission to call this interface
  KV_ERR_VENDOR			vendor-specific error is returned, check the system log for more details
  */
kv_result kv_delete(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, uint8_t ks_id, const kv_key *key, kv_delete_option option, kv_postprocess_function *post_fn);


/**
 * kv_delete_group

  This interface shall post an operation to a device submission queue to delete a group of key-value pairs that matches with grp_cond. This routine works asynchronously and returns immediately regardless of whether the group of key-value pairs is actually deleted from a device. 
  
  If a user defines an interrupt handler (i.e., kv_set_interrupt_handler()) and a postprocess function (i.e., post_fn), the interrupt handler will call the postprocess function when the device triggers an interrupt to notify the completion of the operation. If no postprocess function is defined, the interrupt handler just finishes its operation. If no interrupt handler is defined but a postprocess function is defined, the function is ignored.
  
  If a postprocess function (i.e., post_fn) is defined, the interrupt handler (an interrupt handler defined by kv_set_interrupt_handler()) or the poller (kv_poll_completion()) will call the postprocess function when the device triggers an interrupt to notify the completion of the operation or when the poller detects that the device finished the specified operation.
  
  PARAMETERS
  INPUT: que_hdl     queue handle
  INPUT: ns_hdl      namespace handle, or KV_NAMESPACE_DEFAULT
  INPUT: grop_cond       group condition(key bit pattern and bitmask) for a group of kyes
  INPUT: post_fn     a postprocess function which is called when the operation completes
  IN/OUTPUT: op_hdl   an operation handle for this operation, a buffer for the handle shall be created before calling this routine
  
  RETURNS
  KV_SUCCESS 
  
  ERROR CODE for command submission
  KV_ERR_NS_NOT_EXIST     the namespace does not exist
  KV_ERR_QUEUE_QID_INVALID    submission queue identifier is invalid
  KV_ERR_SYS_IO           the host failed to communicate with the device
  KV_ERR_SYS_PERMISSION       this caller does not have a permission to call this interface
  
  ERROR CODE for operation completion
  KV_ERR_PARAM_INVALID       grp_cond or op_hdl cannot be NULL, both shall have been already allocated
  KV_ERR_VENDOR           vendor-specific error is returned, check the system log for more details
 */
kv_result kv_delete_group(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, uint8_t ks_id, kv_group_condition *grp_cond, kv_postprocess_function *post_fn);

/**
  kv_exist

  This interface shall check if keys already exist and return their status. This routine works asynchronously and returns immediately regardless of whether the key is actually checked in the device. The caller is responsible to allocate a buffer for the result (buffer). One bit is used for each key. Therefore when 32 keys are intended to be checked, a caller shall allocate 32 bits (i.e., 4 bytes) of memory buffer and the existence information will be filled by callee. The LSB (Least Significant Bit) of the buffer indicates if the first key exist or not.
  
  The precise behavior of this interface is determined by the options set by users.
  * KV_EXIST_OPT_DEFAULT [MANDATORY]:
  
  If a user defines an interrupt handler (i.e., kv_set_interrupt_handler()) and a postprocess function (i.e., post_fn), the interrupt handler will call the postprocess function when the device triggers an interrupt to notify the completion of the operation. If no postprocess function is defined, the interrupt handler just finishes its operation. If no interrupt handler is defined but a postprocess function is defined, the function is ignored.
  
  If a postprocess function (i.e., post_fn) is defined, the interrupt handler (an interrupt handler defined by kv_set_interrupt_handler()) or the poller (kv_poll_completion()) will call the postprocess function when the device triggers an interrupt to notify the completion of the operation or when the poller detects that the device finished the specified operation.
  
  
  PARAMETERS
  IN que_hdl	queue handle
  IN ns_hdl	namespace handle, or KV_NAMESPACE_DEFAULT
  IN keys       an array of key of type kv_key

  INPUT: key_cnt        the number of keys
  INPUT: post_fn        a postprocess function which is called when the operation completes
  INPUT: buffer_size    buffer size in bytes
  OUTPUT: buffer        output buffer. Each key takes one bit for the result. (TRUE means existing, while FALSE is not existing)
  
  RETURNS 
  KV_SUCCESS 
  
  ERROR CODE
  KV_ERR_DEV_NOT_EXIST 		no device exists
  KV_ERR_KEY_INVALID		key format is invalid, or kv_key->key buffer is null
  KV_ERR_KEY_LENGTH_INVALID	the key length is out of range of kv_device.min_key_len and kv_device.max_key_len
  KV_ERR_KEY_NOT_EXIST		given key doesn't exist (unmapped-read error)
  KV_ERR_NS_NOT_EXIST		the namespace does not exist
  KV_ERR_OPTION_INVALID		the device does not support the specified options
  KV_ERR_PARAM_INVALID 		key or result cannot be NULL, both shall have been already allocated
  KV_ERR_QUEUE_QID_INVALID	submission queue identifier is invalid
  KV_ERR_SYS_IO 			the host failed to communicate with the device
  KV_ERR_SYS_PERMISSION 		this caller does not have a permission to call this interface
  KV_ERR_VENDOR			vendor-specific error is returned, check the system log for more details
  */
kv_result kv_exist(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, uint8_t ks_id, const kv_key *keys, uint32_t keycount, uint32_t buffer_size, uint8_t *buffer, kv_postprocess_function *post_fn);

/**
  kv_retrieve 

  This interface shall post an operation to a submission queue of device to retrieve a key-value pair. This routine works asynchronously and returns immediately regardless of whether the pair is actually retrieved from a device or not. 
  
  The value buffer should be big enough to store the data of a pair and a buffer overflow shall not happen. The kv_value.length indicates the given buffer's size. On return, kv_value.length is set to the actual size of value stored in the value buffer. 
  	
  If users set kv_value.offset and kv_value.length to positive numbers, this interface retrieves length-byte data starting from the position of offset-byte (see Figure 7).
  
  The precise behavior of this interface is determined by the options set by users. 
  * KV_RETRIEVE_OPT_DEFAULT [MANDATORY]: the default retrieve operation shall retrieve data as it is
  * KV_RETRIEVE_OPT_DECOMPRESS [OPTIONAL]: returns a value after it may decompress the value if this device has the feature and the data was compressed when it was stored with the KV_STORE_OPT_COMPRESS option. 
  
  Note that retrieving uncompressed value with the KV_RETRIEVE_OPT_DECOMPRESS option returns a warning of KV_WRN_DECOMPRESS and returns the stored data without decompression. A hint option like KV_STORE_OPT_DECOMPRESS can be set together with other operational options. Regardless of options, all retrieve operations are atomic. If it completes and succeeds, it guarantees a pair is read wholly.
  
  If a user defines an interrupt handler (i.e., kv_set_interrupt_handler()) and a postprocess function (i.e., post_fn), the interrupt handler will call the postprocess function when the device triggers an interrupt to notify the completion of the operation. If no postprocess function is defined, the interrupt handler just finishes its operation. If no interrupt handler is defined but a postprocess function is defined, the function is ignored.
  
  If a postprocess function (i.e., post_fn) is defined, the interrupt handler (an interrupt handler defined by kv_set_interrupt_handler()) or the poller (kv_poll_completion()) will call the postprocess function when the device triggers an interrupt to notify the completion of the operation or when the poller detects that the device finished the specified operation.
  
  [SAMSUNG] 
  The valid key size for Samsung PM983 is between 16 and 255 bytes, and the valid value size is between 64 bytes and 2MB. KV_STORE_OPT_DECOMPRESS is supported. 
  
  PARAMETERS
  IN que_hdl	queue handle
  IN ns_hdl		namespace handle, or KV_NAMESPACE_DEFAULT
  IN key		key
  IN option		options defined in kv_retrieve_option
  IN post_fn	a postprocess function which is called when the operation completes
  OUT value	value available at the time of IO complettion
  
  RETURNS 
  KV_SUCCESS 
  
  ERROR CODE
  KV_ERR_KEY_INVALID		key format is invalid, or kv_key->key buffer is null
  KV_ERR_KEY_LENGTH_INVALID	the key length is out of range of kv_device.min_key_len and kv_device.max_key_len
  KV_ERR_KEY_NOT_EXIST		given key does not exist (unmapped-read error)
  KV_ERR_NS_NOT_EXIST		the namespace does not exist
  KV_ERR_OFFSET_INVALID		the offset is out of the value range 
  KV_ERR_OPTION_INVALID		the device does not support the specified options
  KV_ERR_PARAM_INVALID 		key or value cannot be NULL, both shall have been already allocated
  KV_ERR_QUEUE_QID_INVALID	submission queue identifier is invalid
  KV_ERR_SYS_IO 			the host failed to communicate with the device
  KV_ERR_SYS_PERMISSION 		this caller does not have a permission to call this interface
  KV_ERR_VALUE_LENGTH_INVALID	the value length is out of range of kv_device.min_value_len and kv_device.max_value_len
  KV_ERR_VENDOR			vendor-specific error is returned, check the system log for more details
  */
kv_result kv_retrieve(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, uint8_t ks_id, const kv_key *key, kv_retrieve_option option, kv_value *value,  const kv_postprocess_function *post_fn);

/**
  kv_store

  This interface shall post an operation to a submission queue of device to store a key-value pair. This routine works asynchronously and returns immediately regardless of whether the pair is actually stored to a device or not. Since one of the benefits of using key value interface is to avoid unnecessary read-modify-write, partial update is not allowed, and the kv_value.offset parameter is ignored.
  
  The precise behavior of this interface is determined by the options set by users. 
  * KV_STORE_OPT_DEFAULT [MANDATORY]: if a key exists, this operation shall overwrite existing key-value pair with a given value. If the key does not exist, this will store a new key-value pair with a given value. 
  * KV_STORE_OPT_COMPRESS [OPTIONAL]: the data in the value buffer may be compressed before it is written to device if the device can compress data. The component that performs a compression operation is implementation-specific. This is optional, and a device may use this option as a hint.
  * KV_STORE_OPT_IDEMPOTENT [MANDATORY]: users can store a value only once. If a key does not exist, this operation shall succeed. If a key already exists, this operation returns an error, KV_ERR_EXIST, without affecting the existing pair. 
  
  A hint option like KV_STORE_OPT_COMPRESS can be set together with other operational options like KV_STORE_OPT_IDEMPOTENT.
  Regardless of options, all store operations are atomic. If it completes and succeeds, it guarantees a pair is written wholly.
  
  If a user defines an interrupt handler (i.e., kv_set_interrupt_handler()) and a postprocess function (i.e., post_fn), the interrupt handler will call the postprocess function when the device triggers an interrupt to notify the completion of the operation. If no postprocess function is defined, the interrupt handler just finishes its operation. If no interrupt handler is defined but a postprocess function is defined, the function is ignored.
  
  If a postprocess function (i.e., post_fn) is defined, the interrupt handler (an interrupt handler defined by kv_set_interrupt_handler()) or the poller (kv_poll_completion()) will call the postprocess function when the device triggers an interrupt to notify the completion of the operation or when the poller detects that the device finished the specified operation.
  
  
  [SAMSUNG] 
  The valid key size for Samsung PM983 is between 16 and 255 bytes, and the valid value size is between 64 bytes and 28 kilobytes. KV_STORE_OPT_COMPRESS is supported.
  
  PARAMETERS
  IN que_hdl	queue handle
  IN ns_hdl		namespace handle, or KV_NAMESPACE_DEFAULT
  IN key		key
  IN value		value
  IN option		options defined in KV_STORE_OPTION
  IN post_fn	a postprocess function which is called when the operation completes
  
  RETURNS
  KV_SUCCESS 
  
  KV_WRN_OVERWRITTEN		this store operation overwrote a mutable object created by kv_append()
  
  ERROR CODE
  KV_ERR_DEV_CAPACITY		device does not have enough space to create a namespace.
  KV_ERR_DEV_NOT_EXIST 		no device exists
  KV_ERR_KEY_EXIST			the given key is already exists (with KV_STORE_IDEMPOTENT option)
  KV_ERR_KEY_INVALID		key format is invalid, or kv_key->key buffer is null
  KV_ERR_KEY_LENGTH_INVALID	the key length is out of range of kv_device.min_key_len and kv_device.max_key_len
  KV_ERR_NS_CAPACITY		a namespace capacity limit exceeds
  KV_ERR_NS_NOT_EXIST		the namespace does not exist
  KV_ERR_OPTION_INVALID		the device does not support the specified options
  KV_ERR_PARAM_INVALID 		key or value cannot be NULL, both shall have been already allocated
  KV_ERR_QUEUE_QID_INVALID	submission queue identifier is invalid
  KV_ERR_SYS_IO 			the host failed to communicate with the device
  KV_ERR_SYS_PERMISSION 		this caller does not have a permission to call this interface
  KV_ERR_VALUE_LENGTH_INVALID	the value length is out of range of kv_device.min_value_len and kv_device.max_value_len
  KV_ERR_VENDOR			vendor-specific error is returned, check the system log for more details

  */
kv_result kv_store(kv_queue_handle que_hdl, kv_namespace_handle ns_hdl, uint8_t ks_id, const kv_key *key, const kv_value *value, kv_store_option option, const kv_postprocess_function *post_fn);

/**
 \ingroup Completion Interfaces
  kv_poll_completion

  This interface actively checks if any posted operations completes for a given timeout period in a caller thread context. If no operation is ready for the period, it returns with an error, KV_ERR_TIMEOUT. If there are operations completed, the completed handles are returned via op_hdls and the number of handles are set to hdl_cnt. If more operations than hdl_cnt are ready, it returns KV_WRN_MORE. If timeout_usec is set zero, it returns immediately.
  
  If a postprocess function (i.e., post_fn) is defined, the poller (kv_poll_completion()) may call the postprocess function when the poller detects that the device finished the specified operation.
  
  PARAMETERS
  IN que_hdl	queue handle
  IN timeout_usec	timeout in micro-seconds
  IN/OUT num_events	the maxiumum number of I/O completion events to retrieve needs to be set in an input and the number of completed I/O events is returned as an output
  
  RETURNS
  KV_SUCCESS
  
  KV_WRN_MORE		there are more completed operations
  KV_WRN_OVERWRITTEN		this store operation overwrote a mutable object created by kv_append()
  
  ERROR CODE
  KV_ERR_DEV_CAPACITY		device does not have enough space to create a namespace.
  KV_ERR_DEV_NOT_EXIST 		no device exists
  KV_ERR_KEY_COUNT_MISMATCH	key count specified does not match the number of keys in the key list
  KV_ERR_KEY_EXIST			the given key is already exists (with KV_STORE_IDEMPOTENT option)
  KV_ERR_KEY_INVALID		key format is invalid, or kv_key->key buffer is null
  KV_ERR_KEY_NOT_EXIST		given key does not exist (unmapped-read error)
  KV_ERR_KEY_LENGTH_INVALID	the key length is out of range of kv_device.min_key_len and kv_device.max_key_len
  KV_ERR_NS_NOT_EXIST		the namespace does not exist
  KV_ERR_OFFSET_INVALID		the offset is out of the value range 
  KV_ERR_OPTION_INVALID		the device does not support the specified options
  KV_ERR_PAIR_ATOMIC		this object is not created by a kv_append operation and cannot be modified
  KV_ERR_QUEUE_QID_INVALID	submission queue identifier is invalid
  KV_ERR_SYS_IO 			the host failed to communicate with the device
  KV_ERR_SYS_PERMISSION 		this caller does not have a permission to call this interface
  KV_ERR_TIMEOUT			timer expired and no operation is completed yet.
  KV_ERR_VALUE_LENGTH_INVALID	the value length is out of range of kv_device.min_value_len and kv_device.max_value_len
  KV_ERR_VENDOR			vendor-specific error is returned, check the system log for more details
  
  [SAMSUNG]
  KV_ERR_SEC_MISALIGNMENT	kv_samsung_device.append_unit is defined, and the size of value is not aligned
  */
kv_result kv_poll_completion(kv_queue_handle que_hdl, uint32_t timeout_usec, uint32_t *num_events);


/**
  kv_set_interrupt_handler

  This interface is called by an interrupt process thread when an interrupt is triggered by a device when any posted operations complete. Its implementation is vendor-specific.
  
  If a user defines an interrupt handler (i.e., kv_set_interrupt_handler()) and a postprocess function (i.e., post_fn), the interrupt handler will call the postprocess function when the device triggers an interrupt to notify the completion of the operation. If no postprocess function is defined, the interrupt handler just finishes its operation. If no interrupt handler is defined but a postprocess function is defined, the function is ignored.
  
  If a postprocess function (i.e., post_fn) is defined, the interrupt handler (an interrupt handler defined by kv_set_interrupt_handler()) may call the postprocess function when the device triggers an interrupt to notify the completion of the operation.
  
  PARAMETERS
  IN que_hdl	queue handle
  IN int_hdl		an interrupt handler
  
  RETURNS
  KV_SUCCESS
  
  ERROR CODE
  KV_ERR_DEV_NOT_EXIST 		no device exists
  KV_ERR_QUEUE_QID_INVALID	submission queue identifier is invalid
  KV_ERR_SYS_IO 			the host failed to communicate with the device
  KV_ERR_SYS_PERMISSION 		this caller does not have a permission to call this interface
  KV_ERR_VENDOR			vendor-specific error is returned, check the system log for more details
  */
kv_result kv_set_interrupt_handler(kv_queue_handle que_hdl, const kv_interrupt_handler int_hdl);

// get the number commands still in queue
uint32_t get_queued_commands_count(kv_queue_handle que_hdl);

// use this to get default namespace for Samsung device
kv_result get_namespace_default(kv_device_handle dev_hdl, kv_namespace_handle *ns_hdl);

// internal API, added for an emulator
extern uint64_t _kv_emul_queue_latency;
kv_result _kv_bypass_namespace(const kv_device_handle dev_hdl, const kv_namespace_handle ns_hdl, bool_t bypass);



// added as a temporary init struct
// this is subject to change
typedef struct {
    // emulator default path must be: /dev/kvemul
    // or an actual physical device path
    const char *devpath;

    // only good for emulator
    // default is ./kvssd_emul.conf
    const char *configfile;

    // no effect currently, only good for emulator
    bool_t need_persistency;

    // operate in polling mode or not
    // default is polling
    // for emulator, it can be overriden by configuration file above
    bool_t is_polling;

    int queuedepth;

} kv_device_init_t;

// for returned key value from iterator
typedef struct {
    kv_key *key;
    kv_value *value;
} kv_pair_t;

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* SAMSUNG_KVS_ADI_H */

