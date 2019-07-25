#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/eventfd.h>
#include <sys/select.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include "linux_nvme_ioctl.h"
#include "kv_nvme.h"

void usage(void)
{
	printf("[Usage] kv_retrieve_aio -d device_path -k key_string [-l offset -s value_len -z space_id]\n");
}

#define DEFAULT_BUF_SIZE		(4096)

int main(int argc, char *argv[])
{
	int ret = 0;
	int fd = -1, efd = -1;
	int opt = 0;
	char *dev = NULL;
	char *key = NULL;
	int key_len = 0;
	long tmp = 0;
	int offset = 0;
	int value_len = DEFAULT_BUF_SIZE;
	unsigned int nsid = 0;
	char *buf = NULL;
	struct nvme_aioevents aioevents;
	struct nvme_aioctx aioctx;
	struct nvme_passthru_kv_cmd cmd;
	fd_set rfds;
	int read_s = 0;
	unsigned long long efd_ctx = 0;
	struct timeval timeout;
    int space_id = 0;
	while((opt = getopt(argc, argv, "d:k:l:s:z:")) != -1) {
		switch(opt) {
			case 'd':
				dev = optarg;
			break;
			case 'k':
				key = optarg;
				key_len = strlen(key);
			break;
			case 'l':
				tmp = strtol(optarg, NULL, 10);
				if (tmp == LONG_MIN || tmp == LONG_MAX || tmp > INT_MAX || tmp < INT_MIN) {
					printf("invalid offset %ld\n", tmp);
					ret = -EINVAL;
					goto exit;
				}
				offset = tmp;
			break;
			case 's':
				tmp = strtol(optarg, NULL, 10);
				if (tmp == LONG_MIN || tmp == LONG_MAX || tmp > INT_MAX || tmp < INT_MIN) {
					printf("invalid offset %ld\n", tmp);
					ret = -EINVAL;
					goto exit;
				}
				value_len = tmp;
			break;
			case 'z':
				tmp = strtol(optarg, NULL, 10);
				if (tmp == LONG_MIN || tmp == LONG_MAX || tmp > INT_MAX || tmp < INT_MIN) {
					printf("invalid offset %ld\n", tmp);
					ret = -EINVAL;
					goto exit;
				}
				space_id = tmp;
			break;

			case '?':
			default:
				usage();
				ret = -EINVAL;
				goto exit;
			break;
		}
	}

	if (!dev || !key) {
		usage();
		ret = -EINVAL;
		goto exit;
	}

	if (value_len) {
		//posix_memalign((void **)&buf, 4096, value_len);
		buf = (char *)calloc(1, value_len);
		if (!buf) {
			printf("fail to alloc buf size %d\n", value_len);
			ret = -ENOMEM;
			goto exit;
		}
		memset(buf, 0, value_len);
	}

	fd = open(dev, O_RDWR);
	if (fd < 0) {
		printf("fail to open device %s\n", dev);
		goto exit;
	}

	nsid = ioctl(fd, NVME_IOCTL_ID);
	if (nsid == (unsigned) -1) {
		printf("fail to get nsid for %s\n", dev);
		goto exit;
	}

	efd = eventfd(0,0);
	if (efd < 0) {
		printf("fail to open eventfd %s\n", dev);
		goto exit;
	}

	memset(&aioevents, 0, sizeof(aioevents));
	memset(&aioctx, 0, sizeof(aioctx));
	memset(&cmd, 0, sizeof(cmd));

	aioctx.eventfd = efd;
	if (ioctl(fd, NVME_IOCTL_SET_AIOCTX, &aioctx) < 0) {
		printf("fail to set aioctx %s\n", dev);
		goto exit;
	}

	cmd.opcode = nvme_cmd_kv_retrieve;
	cmd.nsid = nsid;
    cmd.cdw3 = space_id;
    if (offset) {
		cmd.cdw5 = offset;
    }
	cmd.data_addr = (__u64)buf;
	cmd.data_length = value_len;
    if (key_len <= KVCMD_INLINE_KEY_MAX) {
        memcpy(cmd.key, key, key_len);
    } else {
	    cmd.key_addr = (__u64)key;
    }
	cmd.key_length = key_len;
	cmd.reqid = 1;
	cmd.ctxid = aioctx.ctxid;


	if (ioctl(fd, NVME_IOCTL_AIO_CMD, &cmd) < 0) {
		printf("fail to send aio command %s\n", dev);
		goto exit;
	}

    FD_ZERO(&rfds);
	FD_SET(efd, &rfds);
	memset(&timeout, 0, sizeof(timeout));
	timeout.tv_usec = 1000;
    int check = 0;
	while(1) {
        check = select(efd+1, &rfds, NULL, NULL, &timeout);
		if (check == 1 || check == 0) {
			read_s = read(efd, &efd_ctx, sizeof(unsigned long long));
			if (read_s != sizeof(unsigned long long)) {
				printf("fail to read from efd %s\n", dev);
				goto exit;
			}
			if (efd_ctx) {
				aioevents.nr = efd_ctx;
	            aioevents.ctxid = aioctx.ctxid;
				if (ioctl(fd, NVME_IOCTL_GET_AIOEVENT, &aioevents) < 0) {
					printf("fail to get aioevents %s\n", dev);
					goto exit;
				}
				printf("get request result for cmd(0x%02x) reqid(%lld) result(%d) status(%d)!\n",
						cmd.opcode, aioevents.events[0].reqid, aioevents.events[0].result, aioevents.events[0].status);
#if 1
    buf[value_len -1] = 0;
    printf("returned value size %d\n", value_len);
    printf("retrive data %s\n", buf);
#endif


				break;
			}
		}
	}

exit:
	if (efd > 0) {
		if (ioctl(fd, NVME_IOCTL_DEL_AIOCTX, &aioctx) < 0) {
			printf("fail to del aioctx %s\n", dev);
		}
	}
	if (buf) free(buf);
	if (fd >= 0) {
		close(fd);
	}
	return ret;
}

