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
	printf("[Usage] kv_store -d device_path -k key_string [-v value_pattern -l offset -s value_size -z space_id -i(idempotent flag)]\n");
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
	char *value_pattern = NULL, *value_pos = NULL;
	int value_pattern_len = 0;
	enum nvme_kv_store_option option = STORE_OPTION_NOTHING;
	long tmp = 0;
	int offset = 0;
	int value_len = DEFAULT_BUF_SIZE;	
	unsigned int nsid = 0;
	int count = 0, cp_len = 0;
	char *buf = NULL;
    char key_16_buff[32];
    int space_id = 0;
	while((opt = getopt(argc, argv, "d:k:v:l:s:z:i")) != -1) {
		switch(opt) {
			case 'd':
				dev = optarg;
			break;
			case 'k':
				key = optarg;
				key_len = strlen(key);
			break;
			case 'v':
				value_pattern = optarg;
				value_pattern_len = strlen(value_pattern);
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
			case 'i':
	            option = STORE_OPTION_IDEMPOTENT;
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

	if (!value_pattern) {
		/* use key string */
		value_pattern = key;
		value_pattern_len = key_len;
	}

	if (value_len) {
		//posix_memalign((void **)&buf, 4096, value_len);
        buf = (char*)calloc(1, value_len);
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

	nsid = ioctl(fd, NVME_IOCTL_ID);
	if (nsid == (unsigned) -1) {
		printf("fail to get nsid for %s\n", dev);
		goto exit;
	}

	/* fill 4096B buffer with given input value string */
	if (value_len) {
		count = value_len;
		value_pos = buf;
		cp_len = ((count > value_pattern_len) ? value_pattern_len : count);
		while(count > cp_len) {
			memcpy(value_pos, value_pattern, cp_len);
			count -= cp_len;
			value_pos += cp_len;
		}
	}
    memset(key_16_buff, 0, sizeof(key_16_buff));
    memcpy(key_16_buff, key, ((key_len < 16) ? key_len : 16));
	ret = nvme_kv_store(space_id, fd, nsid, key_16_buff, 16, buf, (int)value_len, (int)offset, option); 
	if (ret) {
		printf("fail to store for %s\n", dev);
	}
exit:
	if (buf) free(buf);
	if (fd >= 0) {
		close(fd);
	}
	return ret;
}

