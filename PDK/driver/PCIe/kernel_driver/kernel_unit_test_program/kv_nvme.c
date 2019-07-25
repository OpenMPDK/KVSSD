#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include "linux_nvme_ioctl.h"
#include "kv_nvme.h"

#ifdef DUMP_ISSUE_CMD
static void dump_cmd(struct nvme_passthru_kv_cmd *cmd)
{
	printf("[dump issued cmd opcode (%02x)]\n", cmd->opcode);
	printf("\t opcode(%02x)\n", cmd->opcode);
	printf("\t flags(%02x)\n", cmd->flags);
	printf("\t rsvd1(%04d)\n", cmd->rsvd1);
	printf("\t nsid(%08x)\n", cmd->nsid);
	printf("\t cdw2(%08x)\n", cmd->cdw2);
	printf("\t cdw3(%08x)\n", cmd->cdw3);
	printf("\t rsvd2(%08x)\n", cmd->cdw4);
	printf("\t cdw5(%08x)\n", cmd->cdw5);
	printf("\t data_addr(%p)\n",(void *)cmd->data_addr);
	printf("\t data_length(%08x)\n", cmd->data_length);
	printf("\t key_length(%08x)\n", cmd->key_length);
	printf("\t cdw10(%08x)\n", cmd->cdw10);
	printf("\t cdw11(%08x)\n", cmd->cdw11);
	printf("\t cdw12(%08x)\n", cmd->cdw12);
	printf("\t cdw13(%08x)\n", cmd->cdw13);
	printf("\t cdw14(%08x)\n", cmd->cdw14);
	printf("\t cdw15(%08x)\n", cmd->cdw15);
	printf("\t timeout_ms(%08x)\n", cmd->timeout_ms);
	printf("\t result(%08x)\n", cmd->result);
	printf("\n\n\n");
}

static void dump_cmd_orig(struct nvme_passthru_cmd *cmd)
{
	printf("[dump issued cmd opcode (%02x)]\n", cmd->opcode);
	printf("\t opcode(%02x)\n", cmd->opcode);
	printf("\t flags(%02x)\n", cmd->flags);
	printf("\t rsvd1(%04d)\n", cmd->rsvd1);
	printf("\t nsid(%08x)\n", cmd->nsid);
	printf("\t cdw2(%08x)\n", cmd->cdw2);
	printf("\t cdw3(%08x)\n", cmd->cdw3);
	printf("\t metadata(%p)\n", (void *)cmd->metadata);
	printf("\t addr(%p)\n",(void *)cmd->addr);
	printf("\t metadata_length(%08x)\n", cmd->metadata_len);
	printf("\t data_length(%08x)\n", cmd->data_len);
	printf("\t cdw10(%08x)\n", cmd->cdw10);
	printf("\t cdw11(%08x)\n", cmd->cdw11);
	printf("\t cdw12(%08x)\n", cmd->cdw12);
	printf("\t cdw13(%08x)\n", cmd->cdw13);
	printf("\t cdw14(%08x)\n", cmd->cdw14);
	printf("\t cdw15(%08x)\n", cmd->cdw15);
	printf("\t timeout_ms(%08x)\n", cmd->timeout_ms);
	printf("\t result(%08x)\n", cmd->result);
	printf("\n\n\n");
}


#endif


int nvme_kv_store(int space_id, int fd, unsigned int nsid,
		const char *key, int key_len,
		const char *value, int value_len,
		int offset, enum nvme_kv_store_option option)
{
	int ret = 0;
	struct nvme_passthru_kv_cmd cmd;
	memset(&cmd, 0, sizeof (struct nvme_passthru_kv_cmd));
	cmd.opcode = nvme_cmd_kv_store;
	cmd.nsid = nsid;
    cmd.cdw3 = space_id;
    cmd.cdw4 = option;
	cmd.cdw5 = offset;
	cmd.data_addr = (__u64)value;
	cmd.data_length = value_len;
	cmd.key_length = key_len;
	if (key_len > KVCMD_INLINE_KEY_MAX) {
		cmd.key_addr = (__u64)key;
	} else {
		memcpy(cmd.key, key, key_len);
	}

#ifdef DUMP_ISSUE_CMD
	dump_cmd(&cmd);
#endif
	ret = ioctl(fd, NVME_IOCTL_IO_KV_CMD, &cmd);

#ifdef DUMP_ISSUE_CMD
	if (ret) {
		printf("opcode(%02x) error(%d) and cmd.result(%d) cmd.status(%d).\n",cmd.opcode, ret, cmd.result, cmd.status);
	} else {
		printf("opcode(%02x) ret (%d) and cmd.result(%d) cmd.status (%d).\n",cmd.opcode, ret, cmd.result, cmd.status);
	}
#endif
	return ret;
}


int nvme_kv_retrieve(int space_id, int fd, unsigned int nsid,
		const char *key, int key_len,
		const char *value, int *value_len,
		int offset, enum nvme_kv_retrieve_option option)
{
	int ret = 0;
	struct nvme_passthru_kv_cmd cmd;
	memset(&cmd, 0, sizeof (struct nvme_passthru_kv_cmd));
	cmd.opcode = nvme_cmd_kv_retrieve;
	cmd.nsid = nsid;
    cmd.cdw3 = space_id;
    cmd.cdw4 = option;
	cmd.cdw5 = offset;
	cmd.data_addr = (__u64)value;
	cmd.data_length = *value_len;
	cmd.key_length = key_len;
	if (key_len > KVCMD_INLINE_KEY_MAX) {
		cmd.key_addr = (__u64)key;
	} else {
		memcpy(cmd.key, key, key_len);
	}

#ifdef DUMP_ISSUE_CMD
	dump_cmd(&cmd);
#endif
	ret = ioctl(fd, NVME_IOCTL_IO_KV_CMD, &cmd);
    *value_len = cmd.result;
#ifdef DUMP_ISSUE_CMD
	if (ret) {
		printf("opcode(%02x) error(%d) and cmd.result(%d) cmd.status(%d).\n",cmd.opcode, ret, cmd.result, cmd.status);
	} else {
		printf("opcode(%02x) ret (%d) and cmd.result(%d) cmd.status (%d).\n",cmd.opcode, ret, cmd.result, cmd.status);
	}
#endif
	return ret;
}


int nvme_kv_delete(int space_id, int fd, unsigned int nsid,
		const char *key, int key_len, enum nvme_kv_delete_option option)
{
	int ret = 0;
	struct nvme_passthru_kv_cmd cmd;
	memset(&cmd, 0, sizeof (struct nvme_passthru_kv_cmd));
	cmd.opcode = nvme_cmd_kv_delete;
    cmd.cdw3 = space_id;
	cmd.nsid = nsid;
    cmd.cdw4 = option;
	cmd.key_length = key_len;
	if (key_len > KVCMD_INLINE_KEY_MAX) {
		cmd.key_addr = (__u64)key;
	} else {
		memcpy(cmd.key, key, key_len);
	}

#ifdef DUMP_ISSUE_CMD
	dump_cmd(&cmd);
#endif
	ret = ioctl(fd, NVME_IOCTL_IO_KV_CMD, &cmd);
#ifdef DUMP_ISSUE_CMD
	if (ret) {
		printf("opcode(%02x) error(%d) and cmd.result(%d) cmd.status(%d).\n",cmd.opcode, ret, cmd.result, cmd.status);
	} else {
		printf("opcode(%02x) ret (%d) and cmd.result(%d) cmd.status (%d).\n",cmd.opcode, ret, cmd.result, cmd.status);
	}
#endif
	return ret;
}


int nvme_kv_iterate_req(int space_id, int fd, unsigned int nsid,
		unsigned char *iter_handle,
        unsigned iter_mask, unsigned iter_value,
		enum nvme_kv_iter_req_option option)
{
	int ret = 0;
	struct nvme_passthru_kv_cmd cmd;
	memset(&cmd, 0, sizeof (struct nvme_passthru_kv_cmd));
	cmd.opcode = nvme_cmd_kv_iter_req;
    cmd.cdw3 = space_id;
	cmd.nsid = nsid;
    cmd.cdw4 = option;
    if (option == ITER_OPTION_CLOSE) {
        cmd.cdw5 = *iter_handle;
    }
    if (option & ITER_OPTION_OPEN) {
        cmd.cdw12 = iter_value;
        cmd.cdw13 = iter_mask;
    }
#ifdef DUMP_ISSUE_CMD
	dump_cmd(&cmd);
#endif
	ret = ioctl(fd, NVME_IOCTL_IO_KV_CMD, &cmd);
    *iter_handle = cmd.result & 0xff;
#ifdef DUMP_ISSUE_CMD
	if (ret) {
		printf("opcode(%02x) error(%d) and cmd.result(%d) cmd.status(%d).\n",cmd.opcode, ret, cmd.result, cmd.status);
	} else {
		printf("opcode(%02x) ret (%d) and cmd.result(%d) cmd.status (%d).\n",cmd.opcode, ret, cmd.result, cmd.status);
	}
#endif
	return ret;
}

int nvme_kv_iterate_read(int space_id, int fd, unsigned int nsid,
		unsigned char iter_handle,
		const char *result, int *result_len)
{
	int ret = 0;
#ifndef ITER_EXT
    unsigned char report_handle = 0;
#endif
	struct nvme_passthru_kv_cmd cmd;
	memset(&cmd, 0, sizeof (struct nvme_passthru_kv_cmd));
	cmd.opcode = nvme_cmd_kv_iter_read;
	cmd.nsid = nsid;
    cmd.cdw3 = space_id;
    cmd.cdw5 = iter_handle;
	cmd.data_addr = (__u64)result;
	cmd.data_length = *result_len;

#ifdef DUMP_ISSUE_CMD
	dump_cmd(&cmd);
#endif
	ret = ioctl(fd, NVME_IOCTL_IO_KV_CMD, &cmd);
    *result_len = cmd.result & 0xffff;
#ifndef ITER_EXT
    report_handle = (cmd.result >> 16) & 0xff;
    if (report_handle != iter_handle) {
        printf("iterater handle mismaptch orig (0x%d) : report (0x%d).\n", iter_handle, report_handle);
    }
    if (cmd.result & 0x01000000) {
        printf("handle expired %d\n", report_handle);
    }
#endif
    if (cmd.status == 0x0393 && *result_len) { /* scan finished, but data is valid */
        printf("scan finished ... buf need to read valid buffer\n");
        ret = 0;
    }

#ifdef DUMP_ISSUE_CMD
	if (ret) {
		printf("opcode(%02x) error(%d) and cmd.result(%d) cmd.status(%d).\n",cmd.opcode, ret, cmd.result, cmd.status);
	} else {
		// *start = cmd.result;
		printf("opcode(%02x) ret (%d) and cmd.result(%d) cmd.status (%d).\n",cmd.opcode, ret, cmd.result, cmd.status);
	}
#endif
	return ret;
}

int nvme_kv_iterate_read_one(int space_id, int fd, unsigned int nsid,
		unsigned char iter_handle,
		const char *result, int buff_len,
        int *key_len, int *value_len)
{
    *key_len = *value_len = 0;
	int ret = 0;
	struct nvme_passthru_kv_cmd cmd;
	memset(&cmd, 0, sizeof (struct nvme_passthru_kv_cmd));
	cmd.opcode = nvme_cmd_kv_iter_read;
	cmd.nsid = nsid;
    cmd.cdw3 = space_id;
    cmd.cdw5 = iter_handle;
	cmd.data_addr = (__u64)result;
	cmd.data_length = buff_len;

#ifdef DUMP_ISSUE_CMD
	dump_cmd(&cmd);
#endif
	ret = ioctl(fd, NVME_IOCTL_IO_KV_CMD, &cmd);
    *value_len = cmd.result & 0x00ffffff;
    *key_len = (cmd.result >> 24) & 0xff;

    if (cmd.status == 0x0393 && (*key_len || *value_len)) { /* scan finished, but data is valid */
        printf("scan finished ... buf need to read valid buffer\n");
        ret = 0;
    }

#ifdef DUMP_ISSUE_CMD
	if (ret) {
		printf("opcode(%02x) error(%d) and cmd.result(%d) cmd.status(%d).\n",cmd.opcode, ret, cmd.result, cmd.status);
	} else {
		// *start = cmd.result;
		printf("opcode(%02x) ret (%d) and cmd.result(%d) cmd.status (%d).\n",cmd.opcode, ret, cmd.result, cmd.status);
	}
#endif
	return ret;
}

int nvme_kv_exist(int space_id, int fd, unsigned int nsid,
		const char *key, int key_len)
{
	int ret = 0;
	struct nvme_passthru_kv_cmd cmd;
	memset(&cmd, 0, sizeof (struct nvme_passthru_kv_cmd));
	cmd.opcode = nvme_cmd_kv_exist;
	cmd.nsid = nsid;
    cmd.cdw3 = space_id;
	cmd.key_length = key_len;
	if (key_len > KVCMD_INLINE_KEY_MAX) {
		cmd.key_addr = (__u64)key;
	} else {
		memcpy(cmd.key, key, key_len);
	}

#ifdef DUMP_ISSUE_CMD
	dump_cmd(&cmd);
#endif
	ret = ioctl(fd, NVME_IOCTL_IO_KV_CMD, &cmd);
#ifdef DUMP_ISSUE_CMD
	if (ret) {
		printf("opcode(%02x) error(%d) and cmd.result(%d) cmd.status(%d).\n",cmd.opcode, ret, cmd.result, cmd.status);
	} else {
		printf("opcode(%02x) ret (%d) and cmd.result(%d) cmd.status (%d).\n",cmd.opcode, ret, cmd.result, cmd.status);
	}
#endif
	return ret;
}


int nvme_kv_get_log(int fd, unsigned int nsid, char page_num, char *buffer, int bufferlen) {
    int ret = 0;
    struct nvme_passthru_cmd cmd;
    memset(&cmd, 0, sizeof(struct nvme_passthru_cmd));
    cmd.opcode = 0x02;
    cmd.nsid = nsid;
    cmd.cdw10 = (__u32)(((bufferlen >> 2) -1) << 16) | ((__u32)page_num & 0x000000ff);
    cmd.addr = (__u64)buffer;
    cmd.data_len = bufferlen;
#ifdef DUMP_ISSUE_CMD
	dump_cmd_orig(&cmd);
#endif
	ret = ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd);
#ifdef DUMP_ISSUE_CMD
	if (ret) {
		printf("opcode(%02x) error(%d) and cmd.result(%d).\n",cmd.opcode, ret, cmd.result);
	} else {
		printf("opcode(%02x) ret (%d) and cmd.result(%d).\n",cmd.opcode, ret, cmd.result);
	}
#endif
    return ret;
}
