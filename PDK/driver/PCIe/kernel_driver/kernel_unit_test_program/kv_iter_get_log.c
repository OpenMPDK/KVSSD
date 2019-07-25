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
	printf("[Usage] kv_iter_get_log -d device_path [-s value_size]\n");
}

#define DEFAULT_BUF_SIZE		(4096)

int main(int argc, char *argv[])
{
	int ret = 0;
	int fd = -1;
	int opt = 0;
	char *dev = NULL;
	long tmp = 0;
	int value_len = DEFAULT_BUF_SIZE;	
	unsigned int nsid = 0xffffffff;
	char *buf = NULL, *data = NULL;
    int i = 0;
	while((opt = getopt(argc, argv, "d:s:")) != -1) {
		switch(opt) {
			case 'd':
				dev = optarg;
			break;
			case 's':
				tmp = strtol(optarg, NULL, 10);
				if (tmp == LONG_MIN || tmp == LONG_MAX || tmp > INT_MAX || tmp < INT_MIN) {
					printf("invalid length %ld\n", tmp);
					ret = -EINVAL;
					goto exit;
				}
				value_len = tmp;
			break;
			case '?':
			default:
				usage();
				ret = -EINVAL;
				goto exit;
			break;
		}
	}

	if (!dev) {
		usage();
		ret = -EINVAL;
		goto exit;
	}

	if (value_len) {
        int alloc_len = ((value_len % 4) ? value_len + ( 4 - (value_len % 4)) : value_len);
		posix_memalign((void **)&buf, 4096, alloc_len);
        //buf = (char*)calloc(1, value_len);
		if (!buf) {
			printf("fail to alloc buf size %d\n", value_len);
			ret = -ENOMEM;
			goto exit;
		}
	}

	fd = open(dev, O_RDWR);
	if (fd < 0) {
		printf("fail to open device %s\n", dev);
		goto exit;
	}
 
	ret = nvme_kv_get_log(fd, nsid, 0xD0, buf, (int)value_len); 
	if (ret) {
		printf("fail to get log for %s\n", dev);
	}
    data = buf;
    for (i = 0; i < 16; i++) {
        printf("nHandle(%02x) bOpened(%02x) nIterTyep(%02x) nKeySpaceId(%02x) niterValue(%08x) niterMaks(%08x) nfinished(%02x) \n",
                data[0], data[1], data[2], data[3],
                *((unsigned int *)&data[4]), *((unsigned int *)&data[8]), data[12]);
        data += 16;
    }

exit:
	if (buf) free(buf);
	if (fd >= 0) {
		close(fd);
	}
	return ret;
}

