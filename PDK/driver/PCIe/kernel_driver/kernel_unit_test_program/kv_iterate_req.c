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
	printf("[Usage] kv_iterate -d device_path -k key_prefix -z space_id -r (Remove Keys)\n");
}

#define MAX_ITERATOR_COUNT		(4)
int main(int argc, char *argv[])
{
	int ret = 0;
	int fd = -1;
	int opt = 0;
	char *dev = NULL;
	char *key_prefix = NULL;
	int key_prefix_len = 0;
	unsigned int nsid = 0;
	unsigned iterator = 0;
	unsigned bitmask = 0;
	int count = MAX_ITERATOR_COUNT, i = 0;
    unsigned char iter_handle = 0;
	long tmp = 0;
    int space_id = 0;
    bool b_delete = false;
    enum nvme_kv_iter_req_option iter_option;
	while((opt = getopt(argc, argv, "d:k:z:r")) != -1) {
		switch(opt) {
			case 'd':
				dev = optarg;
			break;
			case 'k':
				key_prefix = optarg;
				key_prefix_len = strlen(key_prefix); /* exclude null char */
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
            case 'r':
                b_delete = true;
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
#ifdef ITER_CLEANUP
	for(i = 0; i < 16; i++) {
        iter_handle = i;
	    ret = nvme_kv_iterate_req(space_id, fd, nsid, &iter_handle, bitmask, iterator, ITER_OPTION_CLOSE);
    }
    iter_handle = 0;
#endif


	for(i = 0; i < count; i++)
	{
		iterator |= (key_prefix[i] << i*8);
		bitmask |= (0xff << i*8);
	}
#ifdef ITER_EXT
    iter_option = ITER_OPTION_OPEN;
    if (b_delete) iter_option |= ITER_OPTION_DEL_KEY_VALUE;
    else iter_option |= ITER_OPTION_KEY_ONLY;
	ret = nvme_kv_iterate_req(space_id, fd, nsid, &iter_handle, bitmask, iterator, iter_option);
#else
    iter_option = ITER_OPTION_OPEN;
	ret = nvme_kv_iterate_req(space_id, fd, nsid, &iter_handle, bitmask, iterator, iter_option);
#endif 
	if (ret) {
		printf("fail to open iterate for %s\n", dev);
        goto exit;
	}
    	printf("iter_handle (0x%d)\n", iter_handle);

	ret = nvme_kv_iterate_req(space_id, fd, nsid, &iter_handle, bitmask, iterator, ITER_OPTION_CLOSE); 
	if (ret) {
		printf("fail to close iterate for %s\n", dev);
        goto exit;
	}

exit:
	if (fd >= 0) {
		close(fd);
	}
	return ret;
}

