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
#include <limits.h>
#include "linux_nvme_ioctl.h"
#include "kv_nvme.h"

void usage(void)
{
	printf("[Usage] kv_retrieve_unalign -d device_path -k key_string [-l offset -s value_len -z space_id]\n");
}

#define DEFAULT_BUF_SIZE		(4096)

int main(int argc, char *argv[])
{
	int ret = 0;
	int fd = -1;
	int opt = 0;
	char *dev = NULL;
	char *key = NULL;
	int key_len = 0;
	long tmp = 0;
	int offset = 0;
	int value_len = DEFAULT_BUF_SIZE;
	unsigned int nsid = 0;
	char *buf = NULL;
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
		buf = (char *)calloc(1, value_len + 3);
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

	ret = nvme_kv_retrieve(space_id, fd, nsid, key, key_len, buf + 1, &value_len, offset, RETRIEVE_OPTION_NOTHING); 
	if (ret) {
		printf("fail to retrieve for %s\n", dev);
	}
#if 1
    buf[value_len -1] = 0;
    printf("returned value size %d\n", value_len);
    printf("retrive data\n%s\n", buf + 1);
    printf("retrive last data %d\n", buf[value_len - 2]);
#endif

exit:
	if (buf) free(buf);
	if (fd >= 0) {
		close(fd);
	}
	return ret;
}

