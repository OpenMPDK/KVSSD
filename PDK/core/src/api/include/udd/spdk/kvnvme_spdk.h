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

#ifndef _KVNVME_SPDK_H_
#define _KVNVME_SPDK_H_

#include "spdk/nvme.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KV_MAX_KEY_SIZE (4096)
#define KV_MAX_EMBED_KEY_SIZE (16)
#define KV_MAX_VALUE_SIZE (4<<20)

enum spdk_nvme_samsung_nvm_opcode {

  SPDK_NVME_OPC_KV_STORE      = 0x81,
  SPDK_NVME_OPC_KV_RETRIEVE   = 0x90,
  SPDK_NVME_OPC_KV_DELETE     = 0xA1,
  SPDK_NVME_OPC_KV_ITERATE_REQUEST	= 0xB1,
  SPDK_NVME_OPC_KV_ITERATE_READ		= 0xB2,
  SPDK_NVME_OPC_KV_APPEND     = 0x83,
  SPDK_NVME_OPC_KV_EXIST	= 0xB3,

};


typedef void (*spdk_nvme_cmd_cb)(void *, const struct spdk_nvme_cpl *);
/**
 * \brief Submits a KV Store I/O to the specified NVMe namespace.
 *
 * \param ns NVMe namespace to submit the KV Store I/O
 * \param qpair I/O queue pair to submit the request
 * \param ns_id namespace id of key
 * \param key virtual address pointer to the value
 * \param key_length length (in bytes) of the key
 * \param buffer virtual address pointer to the value
 * \param buffer_length length (in bytes) of the value
 * \param offset offset of value (in bytes) 
 * \param cb_fn callback function to invoke when the I/O is completed
 * \param cb_arg argument to pass to the callback function
 * \param io_flags set flags, defined by the SPDK_NVME_IO_FLAGS_* entries
 *                      in spdk/nvme_spec.h, for this I/O.
 * \param option option to pass to NVMe command 
 *          0 - Idempotent; 1 - Post; 2 - Append 
 * \param is_store store or append
 *          1 = store, 0 = append
 * \return 0 if successfully submitted, ENOMEM if an nvme_request
 *           structure cannot be allocated for the I/O request, EINVAL if
 *           key_length or buffer_length is too large.
 *
 * The command is submitted to a qpair allocated by spdk_nvme_ctrlr_alloc_io_qpair().
 * The user must ensure that only one thread submits I/O on a given qpair at any given time.
 */
int
spdk_nvme_kv_cmd_store(struct spdk_nvme_ns *ns, struct spdk_nvme_qpair *qpair,
			      uint32_t ns_id, void *key, uint32_t key_length,
			      void *buffer, uint32_t buffer_length,
			      uint32_t offset,
			      spdk_nvme_cmd_cb cb_fn, void *cb_arg,
		              uint32_t io_flags,
			      uint8_t  option, uint8_t is_store);

/**
 * \brief Submits a KV Retrieve I/O to the specified NVMe namespace.
 *
 * \param ns NVMe namespace to submit the KV Retrieve I/O
 * \param qpair I/O queue pair to submit the request
 * \param ns_id namespace id of key
 * \param key virtual address pointer to the value
 * \param key_length length (in bytes) of the key
 * \param buffer virtual address pointer to the value
 * \param buffer_length length (in bytes) of the value
 * \param offset offset of value (in bytes) 
 * \param cb_fn callback function to invoke when the I/O is completed
 * \param cb_arg argument to pass to the callback function
 * \param io_flags set flags, defined by the SPDK_NVME_IO_FLAGS_* entries
 *                      in spdk/nvme_spec.h, for this I/O.
 * \param option option to pass to NVMe command 
 *     No option supported for retrieve I/O yet. 
 *
 * \return 0 if successfully submitted, ENOMEM if an nvme_request
 *           structure cannot be allocated for the I/O request, EINVAL if
 *           key_length or buffer_length is too large.
 *
 * The command is submitted to a qpair allocated by spdk_nvme_ctrlr_alloc_io_qpair().
 * The user must ensure that only one thread submits I/O on a given qpair at any given time.
 */
int
spdk_nvme_kv_cmd_retrieve(struct spdk_nvme_ns *ns, struct spdk_nvme_qpair *qpair,
			      uint32_t ns_id, void *key, uint32_t key_length,
			      void *buffer, uint32_t buffer_length,
			      uint32_t offset,
			      spdk_nvme_cmd_cb cb_fn, void *cb_arg,
			      uint32_t io_flags, uint32_t option);

/**
 * \brief Submits a KV Delete I/O to the specified NVMe namespace.
 *
 * \param ns NVMe namespace to submit the KV DeleteI/O
 * \param qpair I/O queue pair to submit the request
 * \param ns_id namespace id of key
 * \param key virtual address pointer to the value
 * \param key_length length (in bytes) of the key
 * \param buffer_length length (in bytes) of the value
 * \param offset offset of value (in bytes)
 * \param cb_fn callback function to invoke when the I/O is completed
 * \param cb_arg argument to pass to the callback function
 * \param io_flags set flags, defined by the SPDK_NVME_IO_FLAGS_* entries
 *                      in spdk/nvme_spec.h, for this I/O.
 * \param option option to pass to NVMe command 
 *     No option supported for retrieve I/O yet. 
 *
 * \return 0 if successfully submitted, ENOMEM if an nvme_request
 *           structure cannot be allocated for the I/O request, EINVAL if
 *           key_length or buffer_length is too large.
 *
 * The command is submitted to a qpair allocated by spdk_nvme_ctrlr_alloc_io_qpair().
 * The user must ensure that only one thread submits I/O on a given qpair at any given time.
 */
int
spdk_nvme_kv_cmd_delete (struct spdk_nvme_ns *ns, struct spdk_nvme_qpair *qpair,
			      uint32_t ns_id, void *key, uint32_t key_length, uint32_t buffer_length, uint32_t offset,
			      spdk_nvme_cmd_cb cb_fn, void *cb_arg,
		              uint32_t io_flags, uint8_t  option);

/**
 * \brief Submits a KV Exist I/O to the specified NVMe namespace.
 *
 * \param ns NVMe namespace to submit the KV Exist I/O
 * \param qpair I/O queue pair to submit the request
 * \param ns_id namespace id of key
 * \param key virtual address pointer to the value
 * \param key_length length (in bytes) of the key
 * \param cb_fn callback function to invoke when the I/O is completed
 * \param cb_arg argument to pass to the callback function
 * \param io_flags set flags, defined by the SPDK_NVME_IO_FLAGS_* entries
 *                      in spdk/nvme_spec.h, for this I/O.
 * \param option option to pass to NVMe command 
 *       0 - Fixed size; 1 - Variable size
 *
 * \return 0 if successfully submitted, ENOMEM if an nvme_request
 *           structure cannot be allocated for the I/O request, EINVAL if
 *           key_length or buffer_length is too large.
 *
 * The command is submitted to a qpair allocated by spdk_nvme_ctrlr_alloc_io_qpair().
 * The user must ensure that only one thread submits I/O on a given qpair at any given time.
 */
int
spdk_nvme_kv_cmd_exist (struct spdk_nvme_ns *ns, struct spdk_nvme_qpair *qpair,
			      uint32_t ns_id, void *key, uint32_t key_length,
			      spdk_nvme_cmd_cb cb_fn, void *cb_arg,
		              uint32_t io_flags, uint8_t  option);

/**
 * \brief Submits a KV Iterate Open Request to the specified NVMe namespace.
 *
 * \param ns NVMe namespace to submit the KV Iterate I/O
 * \param qpair I/O queue pair to submit the request
 * \param keyspace_id keyspace_id ( KV_KEYSPACE_IODATA = 0, KV_KEYSPACE_IODATA
 * \param bitmask Iterator bitmask (4 bytes) 
 * \param prefix Iterator prefix (4 bytes) 
 * \param cb_fn callback function to invoke when the I/O is completed
 * \param cb_arg argument to pass to the callback function
 * \param io_flags set flags, defined by the SPDK_NVME_IO_FLAGS_* entries
 *                      in spdk/nvme_spec.h, for this I/O.
 * \param option option to pass to NVMe command 
 *       0 - Fixed size; 1 - Variable size
 *
 * \return 0 if successfully submitted, ENOMEM if an nvme_request
 *           structure cannot be allocated for the I/O request, EINVAL if
 *           key_length or buffer_length is too large.
 *
 * The command is submitted to a qpair allocated by spdk_nvme_ctrlr_alloc_io_qpair().
 * The user must ensure that only one thread submits I/O on a given qpair at any given time.
 */
int
spdk_nvme_kv_cmd_iterate_open (struct spdk_nvme_ns *ns, struct spdk_nvme_qpair *qpair,
			      uint8_t keyspace_id, uint32_t bitmask, uint32_t prefix, 
			      spdk_nvme_cmd_cb cb_fn, void *cb_arg,
		              uint32_t io_flags, uint8_t  option);

/**
 * \brief Submits a KV Iterate Read to the specified NVMe namespace.
 *
 * \param ns NVMe namespace to submit the KV Iterate I/O
 * \param qpair I/O queue pair to submit the request
 * \param iterator pointer to Iterator (3 bytes) 
 * \param buffer virtual address pointer to the return buffer 
 * \param buffer_length length (in bytes) of the return buffer 
 * \param buffer_offset offset (in bytes) of the return buffer 
 * \param cb_fn callback function to invoke when the I/O is completed
 * \param cb_arg argument to pass to the callback function
 * \param io_flags set flags, defined by the SPDK_NVME_IO_FLAGS_* entries
 *                      in spdk/nvme_spec.h, for this I/O.
 * \param option option to pass to NVMe command 
 *       0 - Fixed size; 1 - Variable size
 *
 * \return 0 if successfully submitted, ENOMEM if an nvme_request
 *           structure cannot be allocated for the I/O request, EINVAL if
 *           key_length or buffer_length is too large.
 *
 * The command is submitted to a qpair allocated by spdk_nvme_ctrlr_alloc_io_qpair().
 * The user must ensure that only one thread submits I/O on a given qpair at any given time.
 */
int
spdk_nvme_kv_cmd_iterate_read (struct spdk_nvme_ns *ns, struct spdk_nvme_qpair *qpair,
			      uint8_t iterator, void* buffer, uint32_t buffer_length, uint32_t buffer_offset,
			      spdk_nvme_cmd_cb cb_fn, void *cb_arg,
		              uint32_t io_flags, uint8_t  option);
 
/**
 * \brief Submits a KV Iterate I/O to the specified NVMe namespace.
 *
 * \param ns NVMe namespace to submit the KV Iterate I/O
 * \param qpair I/O queue pair to submit the request
 * \param iterator pointer to Iterator (3 bytes) 
 * \param cb_fn callback function to invoke when the I/O is completed
 * \param cb_arg argument to pass to the callback function
 * \param io_flags set flags, defined by the SPDK_NVME_IO_FLAGS_* entries
 *                      in spdk/nvme_spec.h, for this I/O.
 * \param option option to pass to NVMe command 
 *       0 - Fixed size; 1 - Variable size
 *
 * \return 0 if successfully submitted, ENOMEM if an nvme_request
 *           structure cannot be allocated for the I/O request, EINVAL if
 *           key_length or buffer_length is too large.
 *
 * The command is submitted to a qpair allocated by spdk_nvme_ctrlr_alloc_io_qpair().
 * The user must ensure that only one thread submits I/O on a given qpair at any given time.
 */
int
spdk_nvme_kv_cmd_iterate_close (struct spdk_nvme_ns *ns, struct spdk_nvme_qpair *qpair,
			      uint8_t iterator,
			      spdk_nvme_cmd_cb cb_fn, void *cb_arg,
		              uint32_t io_flags, uint8_t  option);
 
/**
 * \brief Submits a KV Iterate I/O to the specified NVMe namespace.
 * \param ns NVMe namespace to submit the KV Iterate I/O
 *
 * \return  uint32_t the maximum io queue size of the nvme namespace
 *
 */
uint16_t spdk_nvme_ns_get_max_io_queue_size(struct spdk_nvme_ns *ns);

#ifdef __cplusplus
}
#endif

#endif

