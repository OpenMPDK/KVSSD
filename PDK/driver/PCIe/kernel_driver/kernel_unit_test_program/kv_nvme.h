#ifndef _KV_NVME_H_
#define _KV_NVME_H_
#include <stdbool.h>
#define DUMP_ISSUE_CMD
#define ITER_EXT
#define KV_SPACE
//#define ITER_CLEANUP
//#define OLD_KV_FORMAT
enum nvme_kv_store_option {
   STORE_OPTION_NOTHING = 0,
   STORE_OPTION_COMP = 1,
   STORE_OPTION_IDEMPOTENT = 2,
   STORE_OPTION_BGCOMP = 4
};


enum nvme_kv_retrieve_option {
    RETRIEVE_OPTION_NOTHING = 0,
    RETRIEVE_OPTION_DECOMP = 1,
    RETRIEVE_OPTION_ONLY_VALSIZE = 2
};

enum nvme_kv_delete_option {
    DELETE_OPTION_NOTHING = 0,
    DELETE_OPTION_CHECK_KEY_EXIST = 1
};

enum nvme_kv_iter_req_option {
    ITER_OPTION_NOTHING = 0x0,
    ITER_OPTION_OPEN = 0x01,
    ITER_OPTION_CLOSE = 0x02,
#ifdef ITER_EXT
    ITER_OPTION_KEY_ONLY = 0x04,
	ITER_OPTION_KEY_VALUE = 0x08,
	ITER_OPTION_DEL_KEY_VALUE = 0x10,
#endif
};

enum nvme_kv_opcode {
    nvme_cmd_kv_store	= 0x81,
	nvme_cmd_kv_append	= 0x83,
	nvme_cmd_kv_retrieve	= 0x90,
	nvme_cmd_kv_delete	= 0xA1,
	nvme_cmd_kv_iter_req	= 0xB1,
	nvme_cmd_kv_iter_read	= 0xB2,
	nvme_cmd_kv_exist	= 0xB3,

};

#define KVCMD_INLINE_KEY_MAX	(16)
#define KVCMD_MAX_KEY_SIZE		(255)
#define KVCMD_MIN_KEY_SIZE		(255)

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

int nvme_kv_get_log(int fd, unsigned int nsid, char page_num,
                    char *buffer, int bufferlen);


#endif /* #ifndef _KV_NVME_H_ */
