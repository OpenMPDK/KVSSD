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

#ifndef _KVNVME_H_
#define _KVNVME_H_

#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/queue.h>
#include <fcntl.h>
#include "kv_types.h"

// NOTE: (0911)below constants have been moved to kv_types.h
//#define	LBA_TYPE_SSD	0
//#define	KV_TYPE_SSD	1
//#define	MAX_CPU_CORES	64

#define	SYNC_IO_QUEUE	1
#define	ASYNC_IO_QUEUE	2

#define	KV_SHM_ID	0x1234
#define	KV_MEM_SIZE	8192

#define	DEFAULT_IO_QUEUE_DEPTH		256

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*kv_cb_fn_t)(const kv_pair *kv, unsigned int result, unsigned int status);
typedef void (*kv_aer_cb_fn_t)(void *aer_cb_arg, unsigned int result, unsigned int status);

typedef enum raw_cmd_type {
	ADMIN_CMD_TYPE,

	IO_CMD_TYPE,
} raw_cmd_type_t;

typedef struct kv_nvme_cmd {
	/* Command Dword 0 */
	uint16_t opc	:  8;
	uint16_t fuse	:  2;
	uint16_t rsvd1	:  4;
	uint16_t psdt	:  2;
	uint16_t cid;

	/* Command Dword 1 */
	uint32_t nsid;

	/* Command Dword 2-3 */
	uint32_t rsvd2;
	uint32_t rsvd3;

	/* Command Dword 4-5 */
	uint64_t mptr;

	/* Command Dword 6-9 */
	uint64_t prp1;
	uint64_t prp2;

	/* Command Dword 10-15 */
	uint32_t cdw10;
	uint32_t cdw11;
	uint32_t cdw12;
	uint32_t cdw13;
	uint32_t cdw14;
	uint32_t cdw15;
} kv_nvme_cmd_t;

typedef struct kv_nvme_cpl {
	/* Result of the command */
	uint32_t result;
	/* Status of the command */
	uint32_t status;
} kv_nvme_cpl_t;

/**
 * @brief Initializes the underlying uNVMe Driver environment
 */
void kv_env_init(uint32_t process_mem_size_mb);

void kv_env_init_with_shmid(uint32_t process_mem_size_mb, int shmid);

/**
 * @brief Initialize a KV NVMe Device
 * @param bdf BDF of the device in a string format. Example: "0000:01:00.0"
 * @param options Pointer to a KV UDD I/O options structure
 * @param ssd_type LBA Type SSD (0) or KV Type SSD (1)
 * @return 0 : Success
 * @return < 0: Failure
 */
int kv_nvme_init(const char *bdf, kv_nvme_io_options *options, unsigned int ssd_type);



/**
 * @brief Return whether SDK is initialized.
 *         when not initialized(=return 0), most of SDK APIs will not work
 *  @return 1 = initialized , 0 = not initialized
 */
int kv_nvme_is_dd_initialized();

/**
 * @brief Open a KV NVMe Device
 * @param bdf BDF of the device in a string format. Example: "0000:01:00.0"
 * @return Handle to the KV NVMe Device
 * @return 0: Failure
 */
uint64_t kv_nvme_open(const char *bdf);

/**
 * @brief Display the WAF (Write Amplificaton Factor) in a KV NVMe Device
 * @param handle Handle to the KV NVMe Device
 */
uint64_t kv_nvme_get_waf(uint64_t handle);

/**
 * @brief Gmet Smart Log with given log id and buffer
 * @param handle Handle to the KV NVMe Device
 * @log_id log page identifier to read
 * @buffer buffer to store read log (Heap)
 * @buffer buffer size
**/
int kv_nvme_get_log_page(uint64_t handle, uint8_t log_id, void* buffer, uint32_t buffer_size);

/**
 * @brief Get the I/O Queue Type (Sync or Async) of an I/O Queue in a KV NVMe Device
 * @param handle Handle to the KV NVMe Device
 * @param core_id CPU Core ID of the current executing I/O thread
 * @return 1 : Sync I/O Queue
 * @return 2 : Async I/O Queue
 * @return < 0: Invalid I/O Queue ID for the corresponding CPU Core ID
 */
int kv_nvme_io_queue_type(uint64_t handle, int core_id);

/**
 * @brief return IO queue size
 * @param handle Handle to the KV NVMe Device
 */
uint16_t kv_nvme_get_io_queue_size(uint64_t handle);

/**
 * @brief return Sector size of the device
 * @param handle Handle to the KV NVMe Device
 */
uint32_t kv_nvme_get_sector_size(uint64_t handle);

/**
 * @brief return current QD being submitted
 * @param handle Handle to the KV NVMe Device
 */
uint16_t kv_nvme_get_current_qd(uint64_t handle, int core_id);

/**
 * @brief increase current QD being submitted
 * @param handle Handle to the KV NVMe Device
 */
uint16_t kv_nvme_increase_current_qd(uint64_t handle, int core_id);

/**
 * @brief decrease current QD being submitted
 * @param handle Handle to the KV NVMe Device
 */
uint16_t kv_nvme_decrease_current_qd(uint64_t handle, int core_id);

/**
 * @brief Register AER Callback for a KV NVMe Device
 * @param handle Handle to the KV NVMe Device
 * @param aer_cb_fn Pointer to the AER Callback Function
 * @param aer_cb_arg Parameter to the AER Callback Function
 * @return 0 : Success
 * @return < 0: Failure
 */
int kv_nvme_register_aer_callback(uint64_t handle, kv_aer_cb_fn_t aer_cb_fn, void *aer_cb_arg);

/**
 * @brief Submit Raw Admin command for a KV NVMe Device
 * @param handle Handle to the KV NVMe Device
 * @param cmd Raw Command Structure
 * @param buf Virtual address of the contiguous memory buffer
 * @param buf_len Length of the buffer
 * @return ptr: Valid pointer to completion structure
 * @return NULL: Failure
 */
kv_nvme_cpl_t *kv_nvme_submit_raw_cmd(uint64_t handle, kv_nvme_cmd_t cmd, void *buf, uint32_t buf_len, raw_cmd_type_t type);

/**
 * @brief Store Key-Value pair to a KV NVMe Device
 * @param handle Handle to the KV NVMe Device
 * @param kv_pair kv_pair that is to be stored to the KV NVMe Device
 * @return 0 : Success
 * @return > 0: Status of the Store command
 */
int kv_nvme_write(uint64_t handle, const kv_pair* kv);

/**
 * @brief Append Key-Value pair to a KV NVMe Device
 * @param handle Handle to the KV NVMe Device
 * @param kv_pair kv_pair that is to be stored to the KV NVMe Device
 * @return 0 : Success
 * @return > 0: Status of the Store command
 */
int kv_nvme_append(uint64_t handle, const kv_pair* kv);

/**
 * @brief Store Key-Value pair to a KV NVMe Device Asynchronously
 * @param handle Handle to the KV NVMe Device
 * @param kv_pair kv_pair that is to be stored to the KV NVMe Device
 * @return 0 : Success
 * @return > 0: Status of the Store command
 */
int kv_nvme_write_async(uint64_t handle, const kv_pair* kv);

/**
 * @brief Retrieve Value for a particular Key, from a KV NVMe Device
 * @param handle Handle to the KV NVMe Device
 * @param kv_pair kv_pair that is to be passed to the KV NVMe Device
 * @return 0 : Success
 * @return > 0: Status of the Retrieve command
 */
int kv_nvme_read(uint64_t handle, kv_pair* pair);

/**
 * @brief Retrieve Value for a particular Key, from a KV NVMe Device Asynchronously
 * @param handle Handle to the KV NVMe Device
 * @param kv_pair kv_pair that is to be passed to the KV NVMe Device
 * @return 0 : Success
 * @return > 0: Status of the Retrieve command
 */
int kv_nvme_read_async(uint64_t handle, kv_pair* pair);

/**
 * @brief Delete a particular Key, from a KV NVMe Device
 * @param handle Handle to the KV NVMe Device
 * @param kv_pair kv_pair that is to be deleted from the KV NVMe Device
 * @return 0 : Success
 * @return > 0: Status of the Delete command
 */
int kv_nvme_delete(uint64_t handle, const kv_pair *key);

/**
 * @brief Delete a particular Key, from a KV NVMe Device Asynchronously
 * @param handle Handle to the KV NVMe Device
 * @param kv_pair kv_pair that is to be deleted from the KV NVMe Device
 * @return 0 : Success
 * @return > 0: Status of the Delete command
 */
int kv_nvme_delete_async(uint64_t handle, const kv_pair *key);

/**
 * @brief Check if existence of a particular Key set, from a KV NVMe Device Asynchronously
 * @param handle Handle to the KV NVMe Device
 * @param key_list Key list that is going to check whether those exist in the KV NVMe Device
 * @param result Result bytes of each key existenc e(Exist=1|Not Exist=0)
 * @return 0 : Success
 * @return > 0: Status of the Exist command
 */
int kv_nvme_exist(uint64_t handle, const kv_key_list *key_list, kv_value *result);

/**
 * @brief open iterate handle
 * @param handle Handle to the KV NVMe Device
 * @param bitmask  bitmask
 * @param prefix  key prefix of matching key set
 * @param iterate_type  one of four types (key-only, key-value, key-only with delete, key-value with delete )
 * @param result result buffer containing results
 * @return > 0 : iterator id opened
 * @return = UINT8_MAX: fail to open iterator
 */
uint32_t kv_nvme_iterate_open(uint64_t handle, const uint32_t bitmask, const uint32_t prefix, const uint8_t iterate_type);

/**
 * @brief close iterate handle
 * @param handle Handle to the KV NVMe Device
 * @param iterator  iterator handle being closed
 * @param result result buffer containing results
 * @return = 0 : Success
 * @return != 0 : Fail to Close iterator
 */
int kv_nvme_iterate_close(uint64_t handle, const uint8_t iterator);

/**
 * @brief read matching key set from given iterator
 * @param handle Handle to the KV NVMe Device
 * @param it kv_iterate structure including iterator id and result buffer 
 * @return = 0 : Success
 * @return != 0 : Fail to Close iterator
 */
int kv_nvme_iterate_read(uint64_t handle, kv_iterate* it);

/**
 * @brief read matching key set from given iterator
 * @param handle Handle to the KV NVMe Device
 * @param it kv_iterate structure including iterator id and result buffer 
 * @return = 0 : Success
 * @return != 0 : Fail to Close iterator
 */
int kv_nvme_iterate_read_async(uint64_t handle, kv_iterate* it);

/**
 * @brief return array describing iterate handle(s)
 * @param handle Handle to the KV NVMe Device
 * @param info iterate handle information array (OUT)
 * @param nr_handle number of iterate handles to get information (IN)
 * @return = 0 : Success
 * @return != 0 : Fail to Close iterator
 */
int kv_nvme_iterate_info(uint64_t handle, kv_iterate_handle_info* info, int nr_handle);

/**
 * @brief read matching key set from given iterator asynchronously
 * @param handle Handle to the KV NVMe Device
 * @param it kv_iterate structure including iterator id and result buffer 
 * @return = 0 : Success
 * @return != 0 : Fail to Close iterator
 */
int kv_nvme_iterate_read_async(uint64_t handle, kv_iterate* it);


/**
 * @brief Format the KV NVMe Device 
 * @param handle Handle to the KV NVMe Device
 * @return 0 : Success
 * @return > 0: Status of the Delete command
 */
int kv_nvme_format(uint64_t handle);

/**
 * @brief Get the Total Size of the KV NVMe Device (In Bytes)
 * @param handle Handle to the KV NVMe Device
 * @return > 0 : Total Size of the KV NVMe Device (Success)
 * @return 0: Failure
 */
uint64_t kv_nvme_get_total_size(uint64_t handle);

/**
 * @brief Get the Total Size of the KV NVMe Device (In Bytes)
 * @param handle Handle to the KV NVMe Device
 * @return > 0 : Total Size of the KV NVMe Device (Success). In case of KV SSD, % unit is used instread of bytes unit
 * @return 0: Failure
 */
uint64_t kv_nvme_get_used_size(uint64_t handle);

/**
 * @brief Close a KV NVMe Device
 * @param handle Handle to the KV NVMe Device
 * @return 0: Success
 * @return < 0: Failure
 */
int kv_nvme_close(uint64_t handle);

/**
 * @brief De-Initialize a KV NVMe Device
 * @param bdf BDF of the device in a string format. Example: "0000:01:00.0"
 * @return 0 : Success
 * @return < 0: Failure
 */
int kv_nvme_finalize(char *bdf);

/**
 * @brief Allocate Physically Contiguous Memory
 * @param size Size of the physically contiguous memory to allocate (in bytes)
 * @return ptr : Valid pointer to the physically contiguous memory if succcess, else NULL
 */
void *kv_alloc(unsigned long long size);

/**
 * @brief Allocate Physically Contiguous Memory and Fill it with Zeroes
 * @param size Size of the physically contiguous memory to allocate (in bytes)
 * @return ptr : Valid pointer to the physically contiguous memory if succcess, else NULL
 */
void *kv_zalloc(unsigned long long size);

/**
 * @brief Allocate Physically Contiguous Memory and Fill it with Zeroes
 * @param size Size of the physically contiguous memory to allocate (in bytes)
 * @param socket_id NUMA Socket ID (-1 = SPDK_ENV_SOCKET_ID_ANY)
 * @return ptr : Valid pointer to the physically contiguous memory if succcess, else NULL
 */
void *kv_zalloc_socket(unsigned long long size, int socket_id);

/**
 * @brief Free the Allocated Physically Contiguous Memory
 * @param ptr Pointer to the physically contiguous memory that is to be freed
 */
void kv_free(void *ptr);

/**
 * @brief Show API Info (buildtime / system info)
 */
void kv_nvme_sdk_info(void);

#ifdef __cplusplus
}

#endif

#endif
