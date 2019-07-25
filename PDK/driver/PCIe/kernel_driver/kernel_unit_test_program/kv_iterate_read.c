#include <unistd.h>
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
#define DUMP
void usage(void)
{
	printf("[Usage] kv_iterate -d device_path -k key_prefix [-s buffer_size -z space_id -r (Remove keys)]\n");
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
    char data[256];
    int space_id = 0;
    bool b_delete = false;
    enum nvme_kv_iter_req_option iter_option;
	while((opt = getopt(argc, argv, "d:k:s:z:r")) != -1) {
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

    if (b_delete) {
        char *log_buff = NULL, *data = NULL;
        int index = 0;
        bool b_bound = false;
        bool b_finished = false;
        log_buff = (char *)malloc(4096);
        if (!log_buff) {
            printf("fail to alloc log buffer for iterhandle(0x%02x)\n", iter_handle);
            goto exit;
        }
#ifdef DUMP
        printf("Start checking iterhandle (0x%02x) Status with get log\n", iter_handle);
#endif
        /*
           get log D0 page code format.
           handle(1B) : Opened(1B): Type(1B): SpaceId(1B): iterValue(4B): iterMask(4B): Finshed(1B): Reserved(3B).
         */
        while (1) {
            b_bound = false;
            ret = nvme_kv_get_log(fd, 0xffffffff, 0xD0, log_buff, 4096);
            if (ret) { 
                printf("fail to get log for %02x\n", iter_handle);
                break;
            }
#ifdef DUMP
            data = log_buff;
            for (index = 0; index < 16; index++) {
                printf("nHandle(%02x) bOpened(%02x) nIterTyep(%02x) nKeySpaceId(%02x) niterValue(%08x) niterMaks(%08x) nfinished(%02x) \n",
                        data[0], data[1], data[2], data[3],
                        *((unsigned int *)&data[4]), *((unsigned int *)&data[8]), data[12]);
                data += 16;
            }
#endif
            data = log_buff;
            for (index = 0; index < 16; index++) {
                if (data[index*16] == iter_handle) { /* check handle */
                    b_bound = true;
                    if (data[index*16 + 12] == 0x01 /*check finished */) {
                        /* finished */
                        b_finished = true;
                        break;
                    }
                }
            }
            if (b_bound != true) {
                printf("iterate handle not exist %02x\n", iter_handle);
                ret = -EIO;
                goto exit;
            }
            if (b_finished) {
#ifdef DUMP
                printf("End checking iterhandle (0x%02x) Status with get log\n", iter_handle);
#endif
                goto exit;
            }
            sleep(1);
        }
    } else {
        while (1) {
            loop++;
            buf_size = value_len;
            ret = nvme_kv_iterate_read(space_id, fd, nsid, iter_handle, buf, &buf_size);
            if (ret) {
                printf("fail to iterate read for %s loop %d\n", dev, loop);
                break;
            }
#ifdef OLD_KV_FORMAT
            if (buf_size) {
                printf("buffer size %d\n", buf_size);
                for (i = 0; i < buf_size/16; i++) {
                    memcpy(data, buf + i*16, 16);
                    data[16] = 0;
                    printf("%dth entry --> %s\n", i, data);
                }
            } else  {
                break;
            }
#else
            if (buf_size) {
                unsigned int key_count = 0;
                unsigned int key_size = 0;
                int tap = 0;
                char* data_buf = buf;
                printf("buffer size %d\n", buf_size);
                if (buf_size < sizeof(unsigned int)) {
                    printf("check FIRMWARE : unexpected iteration buffer size %s:%d\n", __FILE__, __LINE__);
                    goto close_req;
                }
                key_count = *((unsigned int*)data_buf);
                buf_size -= sizeof(unsigned int);
                data_buf += sizeof(unsigned int);
                for (i = 0; i < key_count && buf_size > 0; i++) {
                    if (buf_size < sizeof(unsigned int)) {
                        printf("check FIRMWARE : unexpected iteration buffer size %s:%d\n", __FILE__, __LINE__);
                        goto close_req;
                    }
                    key_size = *((unsigned int*)data_buf);
                    buf_size -= sizeof(unsigned int);
                    data_buf += sizeof(unsigned int);
                    if (key_size > buf_size) {
                        printf("check FIRMWARE : unexpected iteration key size(%u) : buf size(%u)  %s:%d\n", key_size, buf_size,  __FILE__, __LINE__);
                        goto close_req;
                    }
                    if (key_size >= 256) {
                        printf("check FIRMWARE : unexpected iteration key size(%u)  %s:%d\n", key_size,  __FILE__, __LINE__);
                        goto close_req;
                    }
                    assert(key_size <= buf_size);
                    assert(key_size < 256);
                    memcpy(data, data_buf, key_size);
                    data[key_size] = 0;
                    printf("%dth entry --> %s\n", i, data);
                    tap = (((key_size + 3) >> 2) << 2);
                    buf_size -= tap;
                    data_buf += tap;
                }
            } else  {
                break;
            }
#endif
        }
#ifndef OLD_KV_FORMAT
close_req:
#endif
        iter_option = ITER_OPTION_CLOSE;
        ret = nvme_kv_iterate_req(space_id, fd, nsid, &iter_handle, bitmask, iterator, iter_option); 
        if (ret) {
            printf("fail to close iterate for %s\n", dev);
            goto exit;
        }
    }
exit:
	if (buf) free(buf);
	if (fd >= 0) {
		close(fd);
	}
	return ret;
}

