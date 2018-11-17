/**
 *   BSD LICENSE
 *
 *   Copyright (c) 2017 Samsung Electronics Co., Ltd.
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
/**
 * @file     kv_types.h
 * @brief    constants, enum constants, data structures used on kvsdk
 */

/**
 * @mainpage Notice
 * @section A. Section 1
 * 	- TBD
 * @section B. Section 2	
 * 	- TBD
 */



#ifndef KV_TYPES_C_H
#define KV_TYPES_C_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

//Constants
#define KV_ALIGNMENT_UNIT 64
#define KV_MIN_VALUE_LEN 0
#define KV_MAX_IO_VALUE_LEN (2048*1024) //28KB -> 2048KB
#define LBA_MAX_IO_VALUE_LEN (2048*1024) //2024KB -> 2048KB
#define KV_MAX_TOTAL_VALUE_LEN (2ull*1024*1024*1024) //2GB
#define KV_MIN_KEY_LEN 4
#define KV_MAX_KEY_LEN 255
#define KV_IT_READ_BUFFER_META_LEN 4

#define KV_SDK_MAX_ITERATE_READ_LEN (32*1024) //32KB
#define KV_SDK_MIN_ITERATE_READ_LEN (32*1024) //32KB

#define KV_SSD_MAX_ITERATE_READ_LEN (32*1024) //32KB
#define KV_SSD_MIN_ITERATE_READ_LEN (32*1024) //32KB
#define KV_MAX_ITERATE_HANDLE 16	//maximum 4 handles

#define DEV_ID_LEN 32
#define NR_MAX_SSD 64
#define MAX_CPU_CORES 64

//Enum Constants
/**
* @brief options for initializing types
*/
enum kv_sdk_init_types {
	KV_SDK_INIT_FROM_JSON = 0x00,	/**< initialize sdk with json file */
	KV_SDK_INIT_FROM_STR = 0x01,	/**< initialize sdk with data structure ‘kv_sdk’ */
};

/**
* @brief options for SSD types
*/
enum kv_sdk_ssd_types {
	KV_TYPE_SSD  = 0x00,		/**< KV SSD */
	LBA_TYPE_SSD = 0x01,		/**< normal(legacy) SSD */
};

/**
* @brief options for cache algorithm
*/
enum kv_cache_algorithm {
	CACHE_ALGORITHM_RADIX = 0x00,	/**< radix tree based cache */
};

/**
* @brief options for cache reclaim policy
*/
enum kv_cache_reclaim {
	CACHE_RECLAIM_LRU = 0x00,	/**< lru based reclaim policy */
};
		
/**
* @brief options for slab memory sources
*/
enum kv_slab_mm_alloc_policy {
	SLAB_MM_ALLOC_POSIX = 0x10,	/**< slab allocator for using heap memory */
	SLAB_MM_ALLOC_HUGE =0x20,	/**< slab allocator for using hugepage memory */
};

/**
 * @brief options used for store operation
 */
enum kv_store_option {		
	KV_STORE_DEFAULT = 0x00,		/**< [DEFAULT] storing key value pair(or overwriting given value if key exists) */
	KV_STORE_COMPRESSION = 0x01,		/**< compressing value before writing it to the storage */
	KV_STORE_IDEMPOTENT = 0x02,		/**< storing KV pair only if the key in the pair does not exist already in the device */
};		
		
/**
 * @brief options used for retrieve operation
 */
enum kv_retrieve_option {
	KV_RETRIEVE_DEFAULT = 0x00,		/**< [DEFAULT] retrieving value as it is written(even compressed value is retrieved in its compressed form) */
	KV_RETRIEVE_DECOMPRESSION = 0x01,	/**< returning value after decompressing it */
//	KV_RETRIEVE_VALUE_SIZE = 0x02,          /**< [Suspended] get value size of the key stored (no data transfer) */
};		
		
/**
 * @brief options used for delete operation
 */
enum kv_delete_option {		
	KV_DELETE_DEFAULT = 0x00,		/**<  [DEFAULT] default operation for command */
	KV_DELETE_CHECK_IDEMPOTENT = 0x01,	/**<  check whether the key being deleted exists in KV SSD */
};		
		
/**
 * @brief options used for exist operation
 */
enum kv_exist_option {		
	KV_EXIST_DEFAULT = 0x00,		/**<  [DEFAULT] default operation for command */
};		

/**
 * @brief options format option (0=erase map only, 1=erase user data)
 */
enum kv_format_option {
	KV_FORMAT_MAPDATA = 0x00,
	KV_FORMAT_USERDATA = 0x01,		/**<  [DEFAULT] default operation for format */
};
		
/**
 * @brief options used for iterate_request operation
 */
enum kv_iterate_request_option {		
	KV_ITERATE_REQUEST_DEFAULT = 0x00,		/**<  [DEFAULT] default operation for command */
	KV_ITERATE_REQUEST_OPEN = 0x01,                 /**<  iterator open */
	KV_ITERATE_REQUEST_CLOSE = 0x02,                /**<  iterator close */

};

/**
 * @brief iterate_handle_types : key-only, key-value, and key-only+delete iteration, respectively
 */
enum kv_iterate_handle_type {
	KV_KEY_ITERATE = 0x01,
	KV_KEY_ITERATE_WITH_RETRIEVE = 0x02,
	KV_KEY_ITERATE_WITH_DELETE = 0x03,
};


/**
 * @brief keyspace_id
 */
enum kv_keyspace_id {
	KV_KEYSPACE_IODATA = 0x00,
	KV_KEYSPACE_METADATA = 0x01,
};

/**
 * @brief options used for iterate_read operation
 */
enum kv_iterate_read_option {		
	KV_ITERATE_READ_DEFAULT = 0x00,			/**<  [DEFAULT] default operation for command */
};
		
/**
 * @brief options used for store operation
 */
enum kv_result {
	KV_SUCCESS = 0,						/**<  successful */

        //0x00 ~ 0xFF for Device error
        KV_ERR_INVALID_VALUE_SIZE = 0x01,                       /**<  invalid value length(size) */
        KV_ERR_INVALID_VALUE_OFFSET = 0x02,                     /**<  invalid value offset */
        KV_ERR_INVALID_KEY_SIZE = 0x03,                         /**<  invalid key length(size) */
        KV_ERR_INVALID_OPTION = 0x04,                           /**<  invalid I/O option */
        KV_ERR_INVALID_KEYSPACE_ID = 0x05,                      /**<  invalid keyspace ID (should be 0 or 1. 2018-08027) */
        //0x06 ~ 0x07 are reserved
        KV_ERR_MISALIGNED_VALUE_SIZE = 0x08,                    /**<  misaligned value length(size) */
        KV_ERR_MISALIGNED_VALUE_OFFSET = 0x09,                  /**<  misaligned value offset */
        KV_ERR_MISALIGNED_KEY_SIZE = 0x0A,                      /**<  misaligned key length(size) */
        //0x0B ~ 0x0F are reserved
        KV_ERR_NOT_EXIST_KEY = 0x10,                            /**<  not existing key (unmapped key) */
        KV_ERR_UNRECOVERED_ERROR = 0x11,                        /**<  internal I/O error */
        KV_ERR_CAPACITY_EXCEEDED = 0x12,                        /**<  capacity limit */
        //0x13 ~ 0x7F are reserved
        KV_ERR_IDEMPOTENT_STORE_FAIL = 0x80,                    /**<  overwrite fail when given key is already written with IDEMPOTENT option */
        KV_ERR_MAXIMUM_VALUE_SIZE_LIMIT_EXCEEDED = 0x81,        /**<  value of given key is already full(KV_MAX_TOTAL_VALUE_LEN) */

	KV_ERR_ITERATE_FAIL_TO_PROCESS_REQUEST = 0x90, 	        	/**<  fail to read/close handle with given handle id */ 
	KV_ERR_ITERATE_NO_AVAILABLE_HANDLE = 0x91, 	        	/**<  no more available handle */ 
	KV_ERR_ITERATE_HANDLE_ALREADY_OPENED = 0x92,  	        /**<  fail to open iterator with given prefix/bitmask as it is already opened */
	KV_ERR_ITERATE_READ_EOF = 0x93,     			/**<  end-of-file for iterate_read with given iterator */
	KV_ERR_ITERATE_REQUEST_FAIL = 0x94,     		/**<  fail to process the iterate request due to FW internal status */ 
	KV_ERR_ITERATE_TCG_LOCKED = 0x95,     			/**<  iterate TCG locked */
	KV_ERR_ITERATE_ERROR = 0x96,     			/**<  an error while iterate, closing the iterate handle is recommended */

        //0x100 ~ 0x1FF for DD Error
	KV_ERR_DD_NO_DEVICE = 0x100,
	KV_ERR_DD_INVALID_PARAM = 0x101,
	KV_ERR_DD_INVALID_QUEUE_TYPE = 0x102,
	KV_ERR_DD_NO_AVAILABLE_RESOURCE = 0x103,
	KV_ERR_DD_NO_AVAILABLE_QUEUE = 0x104,
	KV_ERR_DD_UNSUPPORTED_CMD = 0x105,

        //0x200 ~ 0x2FF for SDK Error
        KV_ERR_SDK_OPEN = 0x200,                                /**<  device(sdk) open failed */
        KV_ERR_SDK_CLOSE = 0x201,                               /**<  device(sdk) close failed */
        KV_ERR_CACHE_NO_CACHED_KEY = 0x202,                     /**<  (kv cache) cache miss */
        KV_ERR_CACHE_INVALID_PARAM = 0x203,                     /**<  (kv cache) invalid parameters */
        KV_ERR_HEAP_ALLOC_FAILURE = 0x204,                      /**<  heap allocation fail for sdk operations */
        KV_ERR_SLAB_ALLOC_FAILURE = 0x205,                      /**<  slab allocation fail for sdk operations */
        KV_ERR_SDK_INVALID_PARAM = 0x206,                       /**<  invalid parameters for sdk operations */

        //0x300 ~ 0x3FF for uncertain error types
        KV_WRN_MORE = 0x300,                                    /**<  more results are available(for iterate) */
        KV_ERR_BUFFER = 0x301,                                  /**<  not enough buffer(for retrieve, exist, iterate) */
        KV_ERR_DECOMPRESSION = 0x302,                           /**<  retrieving uncompressed value with KV_RETRIEVE_DECOMPRESSION option */
	KV_ERR_IO = 0x303,					/**<  SDK operation error (remained type for compatibility) */
};		
#define KV_ERR_INVALID_VALUE (UINT64_MAX)			/**<  error in total size / waf / used size */
#define KV_INVALID_ITERATE_HANDLE (0)			/**<  error in invalid iterate handle */

//Data Structures

/**
* @brief A structure which contains device i/o information (NOTE: (0911)moved from kvnvme.h at KVDD)
*/
typedef struct {
        /** Core Mask describing which CPU Cores are to be used to run the I/O threads */
        uint64_t core_mask;
        /** Mask specifying whether the Thread performs Sync / Async I/O. If a bit is set, the corresponding thread performs Sync I/O */
        uint64_t sync_mask;
        /** Number of CQ Processing Threads to be created. Either this should be 1 or should be equal to the number of Async I/O Threads */
        uint64_t num_cq_threads;
        /** Core Mask describing which CPU Cores are to be used to run the CQ threads */
        uint64_t cq_thread_mask;
        /** Queue Depth of the NVMe device's I/O Queues */
        uint32_t queue_depth;
        /** Shared memory size in MB */
	uint32_t mem_size_mb;
} kv_nvme_io_options;

/**
* @brief A structure which contains configuration and options for kv_sdk_init
*/
typedef struct {
        bool use_cache;				/**< read cache enable/disable */
        int cache_algorithm;			/**< cache indexing algorithms (radix only) */
        int cache_reclaim_policy;		/**< cache eviction and reclaim policies (lru only) */
        uint64_t slab_size;			/**< size of slab memory used for cache and I/O buffer(B) */
        int slab_alloc_policy;			/**< slab memory allocation source (hugepage only) */
        int ssd_type;				/**< type of ssds. (KV SSD only) */
	int submit_retry_interval;              /**< submit retry interval (us unit,
						when -1, no retry on LBA/KV SSD, otherwise, retry with usleep for given interval on KV SSD, retry without usleep on LBA SSD */


        int nr_ssd;				/**< number of SSDs */
        char dev_id[NR_MAX_SSD][DEV_ID_LEN];		/**< PCI devices’ address */
        kv_nvme_io_options dd_options[NR_MAX_SSD];	/**< structure about description for devices */
        uint64_t dev_handle[NR_MAX_SSD];	/**< device handle */

        int log_level;				/**< logging level for KV sdk operations(from 0 to 3) */
        char log_file[1024];			/**< path of log file */
        pthread_mutex_t cb_cnt_mutex;		/**< mutex for callback counter */

        uint64_t app_hugemem_size;		/**< size of additional hugepage memory set by user app */
}kv_sdk;

/**
 * @brief A key consists of a pointer and its length
 */
typedef struct {
	void *key;			/**< a pointer to a key */
	uint16_t length;		/**< key length in byte unit */
} kv_key;

/**
 * @brief A value consists of a buffer pointer and its length
 */
typedef struct {
	void *value;			/**< buffer address for value */
	uint32_t length;		/**< value buffer size in byte unit for input and the retuned value length for output*/
	uint32_t actual_value_size;		/* full value size that is stored in disk (only applied on KV SSD, not on LBA SSD) */
	uint32_t offset; 		/**< offset for value */
} kv_value;

/**
 * @brief A structure which contains I/O option, and callback function(for async I/O)
 */
typedef struct {	
	void (*async_cb)();		/**< async notification callback (valid only for async I/O) */
	void* private_data;		/**< private data address used in callback (valid only for async I/O) */
	union {				
		int store_option;	
		int retrieve_option;           
		int delete_option; 	
		int iterate_request_option;
		int iterate_read_option;
		int exist_option;
	}io_option;			/**< options for operations */	
} kv_param;	


/**
 * @brief A pair of structures of key, value, and kv_param. 
 */
typedef struct {
	uint8_t keyspace_id;
	kv_key key;
	kv_value value;
	kv_param param;
} kv_pair;


/**
 * @brief A pair of structures of iterator, value, and kv_param. 
 */
typedef struct {
	uint32_t iterator;
	kv_pair kv;
} kv_iterate;

enum kv_sdk_iterate_status {
	ITERATE_HANDLE_OPENED = 0x01,		/**< iterator handle opened */
	ITERATE_HANDLE_CLOSED  = 0x00,		/**< iterator handle closed */
};

typedef struct {
	uint8_t handle_id;
	uint8_t status;
	uint8_t type;
	uint8_t keyspace_id;
	uint32_t prefix;
	uint32_t bitmask;
	uint8_t is_eof;
	uint8_t reserved[3];
}  kv_iterate_handle_info;

#endif /* KV_TYPES_C_H */

