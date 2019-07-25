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
	printf("[Usage] kv_iterate -d device_path -k key_prefix [-s buffer_size -z space_id]\n");
}

#define DEFAULT_BUF_SIZE		(1024*32)
#define MAX_ITERATOR_COUNT		(4)
int main(int argc, char *argv[])
{
	int ret = 0;
	int fd = -1;
	int opt = 0;
	char *dev = NULL;
	char *key_prefix = NULL;
	int key_prefix_len = 0;
	long tmp = 0;
	int value_len = 0;	
	unsigned int nsid = 0;
	char *buf = NULL;
	unsigned iterator = 0;
	unsigned bitmask = 0;
    unsigned char iter_handle = 0;
    int loop = 0;
    int buf_size = 0;
	int count = MAX_ITERATOR_COUNT;
    int i = 0;
    int key_size = 0;
    int value_size = 0;
    int key_count = 0;
    int space_id = 0;
	while((opt = getopt(argc, argv, "d:k:s:z:")) != -1) {
		switch(opt) {
			case 'd':
				dev = optarg;
			break;
			case 'k':
				key_prefix = optarg;
				key_prefix_len = strlen(key_prefix);
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

	if (value_len == 0) {
		value_len = DEFAULT_BUF_SIZE;
	}

	posix_memalign((void **)&buf, 4096, value_len);
	if (!buf) {
		printf("fail to alloc buf size %d\n", value_len);
		ret = -ENOMEM;
		goto exit;
	}
	memset(buf, 0, value_len);

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
	ret = nvme_kv_iterate_req(space_id, fd, nsid, &iter_handle, bitmask, iterator, (ITER_OPTION_OPEN | ITER_OPTION_KEY_VALUE));

	if (ret) {
		printf("fail to open iterate for %s\n", dev);
        goto exit;
	}
    printf("iter_handle (0x%d)\n", iter_handle);


    while (1) {
        loop++;
        buf_size = value_len;
	    ret = nvme_kv_iterate_read_one(space_id, fd, nsid, iter_handle, buf, buf_size, &key_size, &value_size );
	    if (ret) {
		    printf("fail to iterate read for %s loop %d\n", dev, loop);
            break;
	    }
        if (key_size) {
            key_count++;
            printf("%dth key : key size %d\n", key_count, key_size);
            assert(key_size < 512);
            buf[key_size] = 0;
            printf("key --> %s\n", buf);
        }
        if (value_size) {
            printf("value_size %d\n", value_size);
            buf[512 + value_size] = 0;
            printf("key --> %s\n", buf + 512);
        }
        if (key_size == 0 && value_size == 0) {
            printf("!!!WARN -- return OK but key-value information is not set: Last known key_count is %d.\n", key_count);
        }
    }

	ret = nvme_kv_iterate_req(space_id, fd, nsid, &iter_handle, bitmask, iterator, ITER_OPTION_CLOSE); 
	if (ret) {
		printf("fail to close iterate for %s\n", dev);
        goto exit;
	}

exit:
	if (buf) free(buf);
	if (fd >= 0) {
		close(fd);
	}
	return ret;
}

