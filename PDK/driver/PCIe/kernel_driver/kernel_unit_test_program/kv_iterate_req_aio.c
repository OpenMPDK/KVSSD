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
	printf("[Usage] kv_iterate_aio -d device_path -k key_prefix -z space_id\n");
}

#define MAX_ITERATOR_COUNT		(3)
int main(int argc, char *argv[])
{
	int ret = 0;
	int fd = -1, efd = -1;
	int opt = 0;
	char *dev = NULL;
	char *key_prefix = NULL;
	int key_prefix_len = 0;
	unsigned int nsid = 0;
	unsigned int iterator = 0;
	unsigned int bitmask = 0;
	int count = MAX_ITERATOR_COUNT, i = 0;
	//int start  = 0;
	struct nvme_aioevents aioevents;
	struct nvme_aioctx aioctx;
	struct nvme_passthru_kv_cmd cmd;
	fd_set rfds;
	int read_s = 0;
	unsigned long long efd_ctx = 0;
	struct timeval timeout;
    bool stop = false;
    unsigned char handle = 0;
    int nr_changedfds = 0;
	long tmp = 0;
    int space_id = 0;
	while((opt = getopt(argc, argv, "d:k:z:")) != -1) {
		switch(opt) {
			case 'd':
				dev = optarg;
			break;
			case 'k':
				key_prefix = optarg;
				key_prefix_len = strlen(key_prefix);
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

	if (!dev || !key_prefix) {
		usage();
		ret = -EINVAL;
		goto exit;
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
	
	if (key_prefix_len < count) {
		count = key_prefix_len;
	}
	
	for(i = 0; i < count; i++)
	{
		iterator |= (key_prefix[i] << i*8);
		bitmask |= (0xff << i*8);
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
	cmd.opcode = nvme_cmd_kv_iter_req;
	cmd.nsid = nsid;
    cmd.cdw3 = space_id;
#ifdef ITER_EXT
    cmd.cdw4 = (ITER_OPTION_OPEN | ITER_OPTION_KEY_ONLY);
#else
    cmd.cdw4 = ITER_OPTION_OPEN;
#endif
	cmd.cdw12 = iterator;
	cmd.cdw13 = bitmask;
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

	while(1) {
        nr_changedfds = select(efd+1, &rfds, NULL, NULL, &timeout);
        if (nr_changedfds == 1 || nr_changedfds == 0) {
			read_s = read(efd, &efd_ctx, sizeof(unsigned long long));
			if (read_s != sizeof(unsigned long long)) {
				printf("fail to read from efd %s\n", dev);
				goto exit;
			}
			if (efd_ctx) {
	            memset(&aioevents, 0, sizeof(aioevents));
				aioevents.nr = efd_ctx;
	            aioevents.ctxid = aioctx.ctxid;
				if (ioctl(fd, NVME_IOCTL_GET_AIOEVENT, &aioevents) < 0) {
					printf("fail to get aioevents %s\n", dev);
					goto exit;
				}
                handle = aioevents.events[0].result & 0xff;
				printf("get request result for cmd(0x%02x) reqid(%lld) result(%d) status(%d)!\n",
						cmd.opcode, aioevents.events[0].reqid, aioevents.events[0].result, aioevents.events[0].status);
                if (stop) break;
                printf("iterator handle 0x%02x\n", handle);
	            memset(&cmd, 0, sizeof(cmd));
		        cmd.opcode = nvme_cmd_kv_iter_req;
	            cmd.nsid = nsid;
                cmd.cdw3 = space_id;
                cmd.cdw4 = ITER_OPTION_CLOSE;
                cmd.cdw5 = handle;
	            cmd.reqid = 2;
	            cmd.ctxid = aioctx.ctxid;
                stop = true;
	            if (ioctl(fd, NVME_IOCTL_AIO_CMD, &cmd) < 0) {
		            printf("fail to send aio command %s\n", dev);
		            goto exit;
			    }
            }
		}
	}
exit:
	if (efd > 0) {
		if (ioctl(fd, NVME_IOCTL_DEL_AIOCTX, &aioctx) < 0) {
			printf("fail to del aioctx %s\n", dev);
		}
	}
	if (fd >= 0) {
		close(fd);
	}
	return ret;
}

