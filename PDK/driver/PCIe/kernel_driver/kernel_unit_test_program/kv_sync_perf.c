#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/eventfd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <math.h>
#include <time.h>
#include "linux_nvme_ioctl.h"
#include "kv_nvme.h"

//#define DUMP

struct kv_data {
    char *key_buf;
    char *val_buf;
};

void usage(void)
{
	printf("[Usage] kv_sync_perf -d device_path -l key_len [-s value_size -n num_io -t iothread -O<Overwrite> -R<Read> -D<Delete> ]\n");
}

#define PERF_WRITE      (1)
#define PERF_OVERWRITE  (2)
#define PERF_READ       (3)
#define PERF_DELETE     (4)
#define PERF_MAX        (5)


struct thread_info {
    pthread_t thread;
    struct kv_data *cmd_data;
    int cmd_type;
    int num_io;
    int start_num;
    int fd;
    int nsid;
    int key_len;
    int value_len;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int start;
    int cpu;
    unsigned long long execute_ns;
};

#define DEFAULT_BUF_SIZE		(4096)
#define DEFAULT_AIO_COUNT		(1024*1024)

static void *do_io_worker(void *ctx)
{
    struct thread_info *t_info = NULL;
	int fd = -1;
	unsigned int nsid = 0;
	int i = 0;
    struct kv_data *cmd_data = NULL;
	int num_io = 0, start_num = 0;	
	char *tmp_key = NULL, *tmp_value = NULL;
    struct timespec t1, t2;
    unsigned long long totalnanosecs = 0;
    int key_len = 0, value_len = 0;
    int cmd_type = PERF_WRITE;
	struct nvme_passthru_kv_cmd cmd;
    t_info = (struct thread_info *)ctx;
    num_io = t_info->num_io; 
    start_num = t_info->start_num;
    nsid = t_info->nsid;
    fd = t_info->fd;
    cmd_data = t_info->cmd_data;
    key_len = t_info->key_len;
    value_len = t_info->value_len;
    cmd_type = t_info->cmd_type;

    pthread_mutex_lock(&t_info->mutex);
    while(t_info->start == 0)
        pthread_cond_wait(&t_info->cond, &t_info->mutex);
    pthread_mutex_unlock(&t_info->mutex);

    clock_gettime(CLOCK_REALTIME, &t1);
	for (i = 0; i < num_io; i++) {
		tmp_key = cmd_data->key_buf;
		tmp_value = cmd_data->val_buf;

		memset(tmp_key, 0, key_len);
		snprintf(tmp_key, key_len, "key%012d", i + start_num);
        snprintf(tmp_value, value_len, "value%d", i + start_num);
		memset(&cmd, 0, sizeof(struct nvme_passthru_kv_cmd));
        switch(cmd_type) {
        case PERF_READ:
		    cmd.opcode = nvme_cmd_kv_retrieve;
            break;
        case PERF_DELETE:
		    cmd.opcode = nvme_cmd_kv_delete;
            break;
        case PERF_WRITE:
        case PERF_OVERWRITE:
		    cmd.opcode = nvme_cmd_kv_store;
            break;
        default:
            // temp use write
            cmd_type = PERF_WRITE;
		    cmd.opcode = nvme_cmd_kv_store;
            break;
        }
		cmd.nsid = nsid;
		if (key_len > KVCMD_INLINE_KEY_MAX) {
			cmd.key_addr = (__u64)tmp_key;
		} else {
			memcpy(cmd.key, tmp_key, key_len);
		}
		cmd.key_length = key_len;
		cmd.cdw11 = key_len -1;
        if (cmd_type  == PERF_WRITE || cmd_type == PERF_READ || cmd_type == PERF_OVERWRITE) {
    		cmd.data_addr = (__u64)tmp_value;
    		cmd.data_length = value_len;
    		cmd.cdw10 = value_len >>  2;
        }
		if (ioctl(fd, NVME_IOCTL_IO_KV_CMD, &cmd) < 0) {
			fprintf(stderr, "fail to submit AIO command index %d\n", i + 1);
			goto exit;
		}
	}
    clock_gettime(CLOCK_REALTIME, &t2);
    totalnanosecs = (t2.tv_sec - t1.tv_sec) * 1000000000;
    totalnanosecs += (t2.tv_nsec - t1.tv_nsec);
 
    t_info->execute_ns = totalnanosecs;
exit:
    return NULL;
}

static char * op_str (int op) {
    char *str = NULL;
    switch(op) {
        case PERF_WRITE:
            str = "Write test";
            break;
        case PERF_OVERWRITE:
            str = "Overwrite test";
            break;
        case PERF_READ:
            str = "Read test";
            break;
        case PERF_DELETE:
            str = "Delete test";
            break;
        default:
            str = "Unknow test";
            break;
    }
    return str;
}




int main(int argc, char *argv[])
{
	int ret = 0;
	int fd = -1;
	int opt = 0;
	char *dev = NULL;
	int num_io  = DEFAULT_AIO_COUNT;
	int key_len = 0;
	int num_count = 0;
	long tmp = 0;
	int value_len = 0;	
	unsigned int nsid = 0;
	int i = 0, k = 0;
    struct kv_data *cmd_data = NULL;
    struct timespec t1, t2;
    unsigned long long totalnanosecs = 0;
    double iops = 0, latency_ns = 0;
    double average_iops = 0, average_latency_ns = 0;
    int iothread = 1;
    struct thread_info *threads = NULL;
    int remain_reqs = 0;
    int reqs_count = 0;
    int per_thread_reqs = 0;
    pthread_attr_t attr;
    cpu_set_t cpu_set;
    int num_cpu = sysconf(_SC_NPROCESSORS_ONLN);
    int current_cpu = -1;
    void *status = NULL;
    int do_overwrite = 0, do_read = 0, do_delete = 0;
	while((opt = getopt(argc, argv, "d:l:s:n:t:ORD")) != -1) {
		switch(opt) {
			case 'd':
				dev = optarg;
			break;
			case 'l':
				tmp = strtol(optarg, NULL, 10);
				if (tmp == LONG_MIN || tmp == LONG_MAX || tmp > INT_MAX || tmp < INT_MIN) {
					fprintf(stderr, "invalid offset %ld\n", tmp);
					ret = -EINVAL;
					goto exit;
				}
				key_len = tmp;
			break;
			case 's':
				tmp = strtol(optarg, NULL, 10);
				if (tmp == LONG_MIN || tmp == LONG_MAX || tmp > INT_MAX || tmp < INT_MIN) {
					fprintf(stderr, "invalid offset %ld\n", tmp);
					ret = -EINVAL;
					goto exit;
				}
				value_len = tmp;
			break;
			case 'n':
				tmp = strtol(optarg, NULL, 10);
				if (tmp == LONG_MIN || tmp == LONG_MAX || tmp > INT_MAX || tmp < INT_MIN) {
					fprintf(stderr, "invalid offset %ld\n", tmp);
					ret = -EINVAL;
					goto exit;
				}
				num_io = tmp;
			break;
			case 't':
				tmp = strtol(optarg, NULL, 10);
				if (tmp <= 0 || tmp > 256) {
					fprintf(stderr, "invalid iothread %ld\n", tmp);
					ret = -EINVAL;
					goto exit;
				}
				iothread = tmp;
			break;
			case 'O':
                do_overwrite = 1;
			break;
            case 'R':
                do_read = 1;
            break;
            case 'D':
                do_delete =1;
            break;
			case '?':
			default:
				usage();
				ret = -EINVAL;
				goto exit;
			break;
		}
	}

	if (!dev || key_len == 0) {
		usage();
		ret = -EINVAL;
		goto exit;
	}

	if (value_len == 0) {
		value_len = DEFAULT_BUF_SIZE;
	}

	num_count = log10((double)num_io);

	if (3 + (int)num_count  > key_len) {
		fprintf(stderr, "prefix size (%d) is bigger than key len(%d)\n", 3, key_len);
		ret = -EINVAL;
		goto exit;
	}

    threads = (struct thread_info *) malloc(sizeof(struct thread_info) * iothread);
    if (!threads) {
        fprintf(stderr, "fail to alloc thread info %d\n", iothread);
        ret = -ENOMEM;
        goto exit;
    }
    memset(threads, 0, sizeof(struct thread_info) * iothread);

    cmd_data = valloc(sizeof(struct kv_data) * iothread);
    if (!cmd_data) {
        fprintf(stderr, "fail to alloc cmd_data qdepth %d\n", iothread);
        ret = -ENOMEM;
        goto exit;
    }
    memset(cmd_data, 0, sizeof(struct kv_data) * iothread);

    for (i = 0; i < iothread; i++) {
        posix_memalign((void **)&cmd_data[i].key_buf, 4096, key_len);
        posix_memalign((void **)&cmd_data[i].val_buf, 4096, value_len);
        if (!cmd_data[i].key_buf || !cmd_data[i].val_buf) {
            fprintf(stderr, "fail to alloc cmd_data buffer index %d\n", i);
            ret = -ENOMEM;
            goto exit;
        }
    }

    current_cpu = sched_getcpu();
    if (current_cpu == -1) {
        current_cpu = 0;
    }
	fd = open(dev, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "fail to open device %s.\n", dev);
		goto exit;
	}

	nsid = ioctl(fd, NVME_IOCTL_ID);
	if (nsid == (unsigned) -1) {
		fprintf(stderr, "fail to get nsid for %s.\n", dev);
		goto exit;
	}
    if (num_io < iothread) iothread = num_io;
    remain_reqs = num_io;
    reqs_count = 0;
    per_thread_reqs = num_io / iothread;

    for (i = 0; i < iothread; i++)
    {
        threads[i].fd = fd;
        threads[i].nsid = nsid;
        threads[i].key_len = key_len;
        threads[i].value_len = value_len;
        threads[i].cmd_data = &cmd_data[i];
        threads[i].start_num = reqs_count;
        if (remain_reqs > per_thread_reqs && (i+1) != iothread) {
            threads[i].num_io = per_thread_reqs;
        } else {
            threads[i].num_io = remain_reqs;
        }
        remain_reqs -= threads[i].num_io;
        reqs_count += threads[i].num_io;

        if (pthread_mutex_init(&threads[i].mutex, NULL)) {
		    fprintf(stderr, "fail to init mutext for thread %d.\n", i);
		    goto exit;
        }

        if (pthread_cond_init(&threads[i].cond, NULL)) {
		    fprintf(stderr, "fail to init cond for thread %d.\n", i);
		    goto exit;
        }
        threads[i].start = 0;
    }
    for (k = PERF_WRITE; k < PERF_MAX; k++) {
        if (k == PERF_OVERWRITE && do_overwrite == 0) continue;
        if (k == PERF_READ && do_read ==0) continue;
        if (k == PERF_DELETE && do_delete == 0) continue;

        for (i = 0; i < iothread; i++)
        {
            threads[i].cmd_type = k;
            threads[i].start = 0;
            if (pthread_attr_init(&attr)) {
		        fprintf(stderr, "fail to init pthread attr for thread %d.\n", i);
		        goto exit;
            }
            threads[i].cpu = (current_cpu + i) % num_cpu;
            CPU_ZERO(&cpu_set);
            CPU_SET(threads[i].cpu, &cpu_set);
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
            pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpu_set);
            if (pthread_create(&threads[i].thread, &attr, do_io_worker, &threads[i])) {
                pthread_attr_destroy(&attr);
		        fprintf(stderr, "fail to create pthread %d.\n", i);
		        goto exit;
            }
            pthread_attr_destroy(&attr);
            fprintf(stderr, "create thread %d on cpu %d req start %d req count %d\n", i, threads[i].cpu,  threads[i].start_num, threads[i].num_io);\
        }

        clock_gettime(CLOCK_REALTIME, &t1);
        for (i = 0; i < iothread; i++) {
            threads[i].start = 1;
            pthread_mutex_lock(&threads[i].mutex);
            pthread_cond_signal(&threads[i].cond);
            pthread_mutex_unlock(&threads[i].mutex);
        }

        for (i = 0; i < iothread; i++) {
            ret = pthread_join(threads[i].thread, &status);
            if (ret) {
                fprintf(stderr, "pthread %d return err %d\n", i, ret);
                goto exit;
            }
            fprintf(stderr, "pthread %d complete with status %ld\n", i, (long)status);
        }
        clock_gettime(CLOCK_REALTIME, &t2);

        fprintf(stderr, "\n\n\n<--- %s performance --->\n", op_str(k));
        average_iops = average_latency_ns = 0;
        fprintf(stderr, "-------- Per Threaad execution time -------------------\n");
        for (i = 0; i < iothread; i++) {
            fprintf(stderr, "[Thread %d]\n", i);
            latency_ns = threads[i].execute_ns/threads[i].num_io;
            iops = 1000000000/latency_ns;
            fprintf(stderr, "\t\t\tiops = %lf latency_per_operation = %lf nanosecond.\n", iops, latency_ns);
            average_iops += iops;
            average_latency_ns +=latency_ns;
        }
        average_iops = average_iops/iothread;
        average_latency_ns = average_latency_ns/iothread;
        fprintf(stderr, "\tAverage %s per thread iops = %lf latency_per_operation = %lf nanosecond.\n",op_str(k),  average_iops, average_latency_ns);

        totalnanosecs = (t2.tv_sec - t1.tv_sec) * 1000000000;
        totalnanosecs += (t2.tv_nsec - t1.tv_nsec);
        fprintf(stderr, "\n\n-------- TOTAL execution time -------------------\n");
        fprintf(stderr, "total execution nanosecond %lld\n", totalnanosecs);
        latency_ns = totalnanosecs /num_io;
        iops = 1000000000/latency_ns;
        fprintf(stderr, "%s result  iops = %lf latency_per_operation = %lf nanosecond.\n", op_str(k), iops, latency_ns);
    }
exit:
	if (fd >= 0) {
		close(fd);
	}
    if (cmd_data) {
        for(i = 0; i < iothread; i++) {
            if (cmd_data[i].key_buf) free(cmd_data[i].key_buf);
            if (cmd_data[i].val_buf) free(cmd_data[i].val_buf);
        }
        free(cmd_data);
    }
    if (threads) free(threads);
	return ret;
}

