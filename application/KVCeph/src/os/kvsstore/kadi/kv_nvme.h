#ifndef _KV_NVME_H_
#define _KV_NVME_H_
#include <stdbool.h>
#define DUMP_ISSUE_CMD
#define ITER_EXT
#define KV_SPACE
//#define ITER_CLEANUP
//#define OLD_KV_FORMAT


int nvme_kv_store(int space_id, int fd, unsigned int nsid,
		const char *key, int key_len,
		const char *value, int value_len,
		int offset, enum nvme_kv_store_option option);

int nvme_kv_retrieve(int space_id, int fd, unsigned int nsid,
		const char *key, int key_len,
		const char *value, int *value_len,
		int offset, enum nvme_kv_retrieve_option);

int nvme_kv_delete(int space_id, int fd, unsigned int nsid,
		const char *key, int key_len, enum nvme_kv_delete_option);

int nvme_kv_iterate_req(int space_id, int fd, unsigned int nsid,
		unsigned char* iter_handle,
        unsigned iter_mask, unsigned iter_value,
		enum nvme_kv_iter_req_option option);

int nvme_kv_iterate_read(int space_id, int fd, unsigned int nsid,
		unsigned char iter_handle,
		const char *result, int* result_len);

int nvme_kv_iterate_read_one(int sapce_id, int fd, unsigned int nsid,
		unsigned char iter_handle,
		const char *result, int buff_len,
        int *key_len, int *value_len);

int nvme_kv_exist(int sapce_id, int fd, unsigned int nsid,
		const char *key, int key_len);

int nvme_kv_get_log(int fd, unsigned int nsid, char page_num, char *buffer, int bufferlen);
#endif /* #ifndef _KV_NVME_H_ */
