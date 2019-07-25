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
	printf("[Usage] kv_delete -d device_path -k key_string -z space_id -i (idempotent)\n");
}


int main(int argc, char *argv[])
{
	int ret = 0;
	int fd = -1;
	int opt = 0;
	char *dev = NULL;
	char *key = NULL;
	int key_len = 0;
	unsigned int nsid = 0;
    enum nvme_kv_delete_option option = DELETE_OPTION_NOTHING;
	long tmp = 0;
    int space_id = 0;
	while((opt = getopt(argc, argv, "d:k:z:i")) != -1) {
		switch(opt) {
			case 'd':
				dev = optarg;
			break;
			case 'k':
				key = optarg;
				key_len = strlen(key);
			break;
			case 'i':
                option = DELETE_OPTION_CHECK_KEY_EXIST;
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

	ret = nvme_kv_delete(space_id, fd, nsid, key, key_len, option); 
	if (ret) {
		printf("fail to delete for %s\n", dev);
	}
exit:
	if (fd >= 0) {
		close(fd);
	}
	return ret;
}

