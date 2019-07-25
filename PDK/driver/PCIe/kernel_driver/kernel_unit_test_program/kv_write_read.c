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
	printf("[Usage] kv_write_read -d device_path -n number_of_kvpairs -i(idempotent flag)]\n");
}

#define DEFAULT_BUF_SIZE		(4096)

// real nvme device info
typedef struct device_handle_t {
    char *devpath;

    // device fd
    int fd;
    // event fd
    int efd;
    // device ctx id
    uint32_t ctxid;
    // nsid
    uint32_t nsid;
} device_handle_t;

typedef struct nvme_iohdl {
    enum nvme_kv_opcode opcode;
    char *key;
    int key_len;

    char *value;
    char *value_save;

    int vblen; // value buffer len
    int value_size; // value data len
    int offset;     // value offset

    // 4k aligned buffer for nvme internal use
    char *aligned_buf;
    int blen;

    int reqid; 
    int retcode; // return result
} nvme_iohdl;


void create_iohdl(nvme_iohdl **iohdlr, int klen, int vlen) {
    nvme_iohdl *iohdl = (nvme_iohdl *) calloc(1, sizeof(nvme_iohdl));
    *iohdlr = iohdl;

    iohdl->key = (char *) calloc(klen, 1);
    iohdl->key_len = 16;

    iohdl->value = (char *) calloc(vlen, 1);
    iohdl->value_save = (char *) calloc(vlen, 1);
    iohdl->vblen = vlen;
    iohdl->value_size = vlen;
    iohdl->offset = 0;

    // 4k aligned buffer for nvme internal use
    // WORKING LINE
    //posix_memalign((void **)&(iohdl->aligned_buf), 4096, iohdl->value_size);
    //
    // BUG HERE
    iohdl->aligned_buf = malloc(4096);
    if (!iohdl->aligned_buf) {
        printf("fail to alloc aligned buf size %d\n", iohdl->value_size);
        exit(1);
    }
    memset(iohdl->aligned_buf, 0, iohdl->value_size);
    iohdl->blen = iohdl->value_size;

    iohdl->reqid = 0;
    iohdl->retcode = 0;
}

void free_iohdl(nvme_iohdl *iohdl) {
    free(iohdl->key);
    free(iohdl->value);
    free(iohdl->value_save);
    free(iohdl->aligned_buf);
    free(iohdl);
}

void populate_iohdl(nvme_iohdl *iohdl) {
    static uint32_t i = 0;
    i++;
	unsigned int k = 0;
    uint32_t blen = iohdl->vblen;
    snprintf(iohdl->key, iohdl->key_len, "key#%d#", i);

    char *buffer = (char *) iohdl->value;
    memcpy(buffer, iohdl->key, strlen(iohdl->key));
    blen -= strlen(iohdl->key);
    buffer += strlen(iohdl->key);

    for (k = 0; k < blen - 1; k++) {
        unsigned int j = '1' + random() % 30;
        buffer[k] = j;
    }

    memcpy(iohdl->value_save, iohdl->value, iohdl->value_size);
    memcpy(iohdl->aligned_buf, iohdl->value, iohdl->value_size);
    iohdl->reqid = i;
}

int create_device(const char *dev, device_handle_t *devhdl) {
    struct nvme_aioctx aioctx;

    // allocate fds
    int fd = open(dev, O_RDWR);
    if (fd < 0) {
    	printf("fail to open device %s\n", dev);
    	exit(1);
    }
    devhdl->fd = fd;
    
    unsigned int nsid = ioctl(fd, NVME_IOCTL_ID);
    if (nsid == (unsigned) -1) {
        close(fd);
    	printf("fail to get nsid for %s\n", dev);
    	exit(1);
    }
    devhdl->nsid = nsid;
    
    int efd = eventfd(0,0);
    if (efd < 0) {
        close(fd);
    	printf("fail to open eventfd %s\n", dev);
    	exit(1);
    }
    devhdl->efd = efd;

    aioctx.eventfd = efd;
    aioctx.ctxid = 0;
    if (ioctl(fd, NVME_IOCTL_SET_AIOCTX, &aioctx) < 0) {
        close(efd);
        close(fd);
    	printf("fail to open AIO context %s\n", dev);
        exit(1);
    }

    devhdl->ctxid = aioctx.ctxid;

    devhdl->devpath = strdup(dev);
    return 0;
}

int close_device(device_handle_t *devhdl) {
    if (devhdl->devpath) {
        free(devhdl->devpath);
    }

    if (devhdl->efd) {
        struct nvme_aioctx aioctx;
        aioctx.eventfd = devhdl->efd;
        aioctx.ctxid = devhdl->ctxid;
        ioctl(devhdl->fd, NVME_IOCTL_DEL_AIOCTX, &aioctx);
    }
    if (devhdl->fd) {
        close(devhdl->fd);
    }

    return 0;
}

// fd, efd, 4k aligned_buf should have been set up already
// read and write using the same buffer
int kv_operation(device_handle_t *devhdl, nvme_iohdl *iohdl, int idempotent) {
    struct nvme_aioevents aioevents;
    struct nvme_aioctx aioctx;
    struct nvme_passthru_kv_cmd cmd;
    fd_set rfds;
    int read_s = 0;
    int nr_change = 0;
    unsigned long long efd_ctx = 0;
    struct timeval timeout;
    int status = -1;

    memset(&aioevents, 0, sizeof(aioevents));
    memset(&aioctx, 0, sizeof(aioctx));
    memset(&cmd, 0, sizeof(cmd));

    int efd = devhdl->efd;
    int fd = devhdl->fd;
    int nsid = devhdl->nsid;
    uint32_t ctxid = devhdl->ctxid;

    // value already copied to aligned buffer for write
    char *aligned_buf = iohdl->aligned_buf;
    unsigned int blen = iohdl->blen;

    // cmd.opcode = nvme_cmd_kv_store;
    // nvme_cmd_kv_retrieve
    cmd.opcode = iohdl->opcode;
    cmd.nsid = nsid;
    cmd.ctxid = ctxid;
    if (iohdl->opcode == nvme_cmd_kv_store && idempotent) {
            // don't overite
    	cmd.cdw4 = 2;
    }

    if (iohdl->offset) {
        cmd.cdw5 = iohdl->offset;
    }

    // use aligned data buffer
    cmd.data_addr = (__u64)aligned_buf;
    cmd.data_length = iohdl->value_size;

    // if store key value, copy value to aligned buffer first
    if (cmd.opcode == nvme_cmd_kv_store) {
        assert(iohdl->value_size > 0);
        // ensure buffer size are good
        assert(iohdl->vblen == blen);
        memcpy(aligned_buf, iohdl->value, iohdl->value_size);
    }

    if (iohdl->key_len < KVCMD_INLINE_KEY_MAX) {
        cmd.key_addr = (__u64) (iohdl->key);
    } else {
        memcpy(cmd.key, iohdl->key, iohdl->key_len);
    }
    cmd.key_length = iohdl->key_len;
    cmd.cdw11 = iohdl->key_len - 1;
    cmd.cdw10 = iohdl->value_size >> 2;
    cmd.reqid = iohdl->reqid;

    if (ioctl(fd, NVME_IOCTL_AIO_CMD, &cmd) < 0) {
    	printf("fail to send aio command %s\n", devhdl->devpath);
    	return status;
    }

    FD_ZERO(&rfds);
    FD_SET(efd, &rfds);
    memset(&timeout, 0, sizeof(timeout));
    timeout.tv_usec = 1000;

    while(1) {
    	nr_change = select(devhdl->efd+1, &rfds, NULL, NULL, &timeout);
        if (nr_change == 1 || nr_change == 0){
    		read_s = read(efd, &efd_ctx, sizeof(unsigned long long));
    		if (read_s != sizeof(unsigned long long)) {
    			printf("fail to read from efd %s\n", devhdl->devpath);
    			break;
    		}
    		if (efd_ctx) {
    			aioevents.nr = efd_ctx;
                            aioevents.ctxid = ctxid;
    			if (ioctl(fd, NVME_IOCTL_GET_AIOEVENT, &aioevents) < 0) {
    				printf("fail to get aioevents %s\n", devhdl->devpath);
    				break;
    			} 
                if (aioevents.events[0].status)
    			    printf("get request result for cmd(0x%02x) reqid(%lld) result(%d) status(%d)\n", cmd.opcode, aioevents.events[0].reqid, aioevents.events[0].result, aioevents.events[0].status);
                status = aioevents.events[0].status;
    			break;
    		}
    	}
    }

    return status;
}


int main(int argc, char *argv[])
{
    int ret = 0;
    int opt = 0;
    char *buf = NULL;
    char *dev = NULL;
    // int key_len = 0;
    // char *value_pattern = NULL, *value_pos = NULL;
    // int value_pattern_len = 0;
    unsigned char idempotent  = 0;
    int nvals = 5000;
    int i = 0; 
    while((opt = getopt(argc, argv, "d:n:i:")) != -1) {
    	switch(opt) {
    		case 'd':
    			dev = optarg;
    		break;
    		case 'n':
    			nvals = strtol(optarg, NULL, 10);
    		break;
    		case 'i':
    			idempotent = 1;
    		break;
    		case '?':
    		default:
    			usage();
                        exit(1);
    		break;
    	}
    }
    
    device_handle_t devhdl;
    // device handle
    create_device(dev, &devhdl);
    
    // XXX init iohdl
    nvme_iohdl *iohdl;

    create_iohdl(&iohdl, 16, 4096);

    for (i = 0; i < nvals; i++) {
        // each time will generate a different reqid
        populate_iohdl(iohdl);
        iohdl->opcode = nvme_cmd_kv_store;
        kv_operation(&devhdl, iohdl, idempotent);

        // read it back, first reset buffer
        memset(iohdl->aligned_buf, 0, iohdl->blen);
        iohdl->opcode = nvme_cmd_kv_retrieve;
        kv_operation(&devhdl, iohdl, idempotent);
 
        // printf("value written %s\n", iohdl->value_save);
        if (memcmp(iohdl->aligned_buf, iohdl->value_save, 4096)) {
            printf("value read doesn't match value written\n");
            exit(1);
        } else {
            // printf("success\n");
        }
    }

    printf("success\n");
    free_iohdl(iohdl);
    close_device(&devhdl); 
    if (buf) free(buf);
    return ret;
}
