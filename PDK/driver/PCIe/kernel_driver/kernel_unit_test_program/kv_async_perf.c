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

struct aio_cmd_ctx {
	int index;
	struct nvme_passthru_kv_cmd cmd;
};


struct kv_data {
    char *key_buf;
    char *val_buf;
};

void usage(void)
{
	printf("[Usage] kv_async_perf -d device_path -l key_len [-s value_size -n numAio -q queue_depath -t iothread -O<Overwrite> -R<Read> -D<Delete> ]\n");
}

#define PERF_WRITE      (1)
#define PERF_OVERWRITE  (2)
#define PERF_READ       (3)
#define PERF_DELETE     (4)
#define PERF_MAX        (5)


struct thread_info {
    pthread_t thread; 
    struct aio_cmd_ctx *cmd_ctx;
    struct kv_data *cmd_data;
    int cmd_type;
    int num_io;
    int start_num;
    int fd;
    int efd;
    int nsid;
    int qdepth;
    int key_len;
    int value_len;
    struct nvme_aioctx aioctx;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int start;
    int cpu;
    unsigned long long execute_ns;
    int failed_io;
};

#define DEFAULT_BUF_SIZE		(4096)
#define DEFAULT_AIO_COUNT		(1024*1024)

static void *do_aio_worker(void *ctx)
{
    struct thread_info *t_info = NULL;
	int fd = -1, efd = -1;
	unsigned int nsid = 0;
	int i = 0;
    struct kv_data *cmd_data = NULL;
	int qdepth = 0;
	struct nvme_aioevents aioevents;
	struct nvme_aioctx aioctx;
	struct aio_cmd_ctx *cmd_ctx = NULL;
	int num_io = 0, num_on_air = 0, num_aio = 0, start_num = 0;	
	char *tmp_key = NULL, *tmp_value = NULL;
	fd_set rfds;
	struct timeval timeout;
	int nr_changedfds = 0;
	int read_s = 0;
    int failed_io = 0;
	unsigned long long eftd_ctx = 0;
	unsigned int reqid = 0;
	unsigned int check_nr = 0;
    struct timespec t1, t2;
    unsigned long long totalnanosecs = 0;
    int key_len = 0, value_len = 0;
    int cmd_type = PERF_WRITE;
    t_info = (struct thread_info *)ctx;
    qdepth = t_info->qdepth;
    num_aio = t_info->num_io; 
    start_num = t_info->start_num;
    aioctx = t_info->aioctx;
    nsid = t_info->nsid;
    fd = t_info->fd;
    efd = t_info->efd;
    cmd_data = t_info->cmd_data;
    cmd_ctx = t_info->cmd_ctx;
    key_len = t_info->key_len;
    value_len = t_info->value_len;
    cmd_type = t_info->cmd_type;

    pthread_mutex_lock(&t_info->mutex);
    while(t_info->start == 0)
        pthread_cond_wait(&t_info->cond, &t_info->mutex);
    pthread_mutex_unlock(&t_info->mutex);

    clock_gettime(CLOCK_REALTIME, &t1);
	for (i = 0; i < qdepth; i++) {
		tmp_key = cmd_data[i].key_buf;
		tmp_value = cmd_data[i].val_buf;

		memset(tmp_key, 0, key_len);
		snprintf(tmp_key, key_len, "keyi%d", num_io + start_num);
        snprintf(tmp_value, value_len, "value%d", num_io + start_num);
		memset(&cmd_ctx[i].cmd, 0, sizeof(struct nvme_passthru_kv_cmd));
        switch(cmd_type) {
        case PERF_READ:
		    cmd_ctx[i].cmd.opcode = nvme_cmd_kv_retrieve;
            break;
        case PERF_DELETE:
		    cmd_ctx[i].cmd.opcode = nvme_cmd_kv_delete;
            break;
        case PERF_WRITE:
        case PERF_OVERWRITE:
		    cmd_ctx[i].cmd.opcode = nvme_cmd_kv_store;
            break;
        default:
            // temp use write
            cmd_type = PERF_WRITE;
		    cmd_ctx[i].cmd.opcode = nvme_cmd_kv_store;
            break;
        }
		cmd_ctx[i].cmd.nsid = nsid;
		if (key_len > KVCMD_INLINE_KEY_MAX) {
			cmd_ctx[i].cmd.key_addr = (__u64)tmp_key;
		} else {
			memcpy(cmd_ctx[i].cmd.key, tmp_key, key_len);
		}
		cmd_ctx[i].cmd.key_length = key_len;
		cmd_ctx[i].cmd.cdw11 = key_len -1;
        if (cmd_type  == PERF_WRITE || cmd_type == PERF_READ || cmd_type == PERF_OVERWRITE) {
    		cmd_ctx[i].cmd.data_addr = (__u64)tmp_value;
    		cmd_ctx[i].cmd.data_length = value_len;
    		cmd_ctx[i].cmd.cdw10 = value_len >>  2;
        }
		cmd_ctx[i].cmd.ctxid = aioctx.ctxid;
		cmd_ctx[i].cmd.reqid = i;
		if (ioctl(fd, NVME_IOCTL_AIO_CMD, &cmd_ctx[i].cmd) < 0) {
			fprintf(stderr, "fail to submit AIO command index %d\n", num_io);
			goto exit;
		}
		num_io++;
		num_on_air++;
	}

	FD_ZERO(&rfds);
	FD_SET(efd, &rfds);
	memset(&timeout, 0, sizeof(timeout));
	timeout.tv_usec = 1000; //timeout after 1000 micro_sec
#ifdef DUMP
	fprintf(stderr, "[Send AIO until %d]\n", num_aio);
#endif
	while (num_on_air) {
		nr_changedfds = select(efd+1, &rfds, NULL, NULL, &timeout);
		if (nr_changedfds == 1 || nr_changedfds == 0) {
			read_s = read(efd, &eftd_ctx, sizeof(unsigned long long));
			if (read_s != sizeof(unsigned long long)) {
				fprintf(stderr, "failt to read from eventfd ..\n");
				goto exit;
			}
			while (eftd_ctx) {
				check_nr = eftd_ctx;
				if (check_nr > MAX_AIO_EVENTS) {
					check_nr = MAX_AIO_EVENTS;
				}
                if (check_nr > qdepth) {
                    check_nr = qdepth;
                }
				aioevents.nr = check_nr;
                aioevents.ctxid = aioctx.ctxid;
				if (ioctl(fd, NVME_IOCTL_GET_AIOEVENT, &aioevents) < 0) {
					fprintf(stderr, "fail to read IOEVETS \n");
					goto exit;
				}
				eftd_ctx -= check_nr;
				for (i = 0; i < aioevents.nr; i++) {
					/* found request */
					reqid = aioevents.events[i].reqid;
                    if (aioevents.events[i].status) failed_io++; 
					num_on_air--;
					assert(reqid < qdepth);
#ifdef DUMP
					fprintf(stderr, "cmd(0x%02x) reqid(%d) result(%d) status(%d) finished!  num IOs %d num on the Air %d\n",
							cmd_ctx[reqid].cmd.opcode, reqid, aioevents.events[i].result, aioevents.events[i].status,
							num_io, num_on_air);
#endif
					if (num_io < num_aio) {
						tmp_key = cmd_data[reqid].key_buf;
						tmp_value = cmd_data[reqid].val_buf;

						memset(tmp_key, 0, key_len);
		                snprintf(tmp_key, key_len, "keyi%d", num_io + start_num);
                        snprintf(tmp_value, value_len, "value%d", num_io + start_num);

						memset(&cmd_ctx[reqid].cmd, 0, sizeof(struct nvme_passthru_kv_cmd));
                        switch(cmd_type) {
                        case PERF_READ:
		                    cmd_ctx[reqid].cmd.opcode = nvme_cmd_kv_retrieve;
                            break;
                        case PERF_DELETE:
		                    cmd_ctx[reqid].cmd.opcode = nvme_cmd_kv_delete;
                            break;
                        case PERF_WRITE:
                        case PERF_OVERWRITE:
		                    cmd_ctx[reqid].cmd.opcode = nvme_cmd_kv_store;
                            break;
                        default:
                            // temp use write
                            cmd_type = PERF_WRITE;
		                    cmd_ctx[reqid].cmd.opcode = nvme_cmd_kv_store;
                            break;
                        }

						cmd_ctx[reqid].cmd.nsid = nsid;
						if (key_len > KVCMD_INLINE_KEY_MAX) {
							cmd_ctx[reqid].cmd.key_addr = (__u64)tmp_key;
						} else {
							memcpy(cmd_ctx[reqid].cmd.key, tmp_key, key_len);
						}
						cmd_ctx[reqid].cmd.key_length = key_len;
						cmd_ctx[reqid].cmd.cdw11 = key_len -1;
                        if (cmd_type  == PERF_WRITE || cmd_type == PERF_READ || cmd_type == PERF_OVERWRITE) {
						    cmd_ctx[reqid].cmd.data_addr = (__u64)tmp_value;
						    cmd_ctx[reqid].cmd.data_length = value_len;
						    cmd_ctx[reqid].cmd.cdw10 = value_len >> 2;
                        }
						cmd_ctx[reqid].cmd.ctxid = aioctx.ctxid;
						cmd_ctx[reqid].cmd.reqid = reqid;
						if (ioctl(fd, NVME_IOCTL_AIO_CMD, &cmd_ctx[reqid].cmd) < 0) {
							fprintf(stderr, "fail to submit AIO command index %d\n", num_io);
							goto exit;
						}
						num_io++;
						num_on_air++;	
					}
				}
			}
		}
	}
    clock_gettime(CLOCK_REALTIME, &t2);
    totalnanosecs = (t2.tv_sec - t1.tv_sec) * 1000000000;
    totalnanosecs += (t2.tv_nsec - t1.tv_nsec);
 
    t_info->execute_ns = totalnanosecs;
    t_info->failed_io = failed_io;
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
	int num_aio  = DEFAULT_AIO_COUNT;
	int key_len = 0;
	int num_count = 0;
	long tmp = 0;
	int value_len = 0;	
	unsigned int nsid = 0;
	int i = 0, k = 0;
    struct kv_data *cmd_data = NULL;
	int qdepth = MAX_AIO_EVENTS;
	struct aio_cmd_ctx *cmd_ctx = NULL;
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
    uint64_t total_failed_io = 0;
	while((opt = getopt(argc, argv, "d:l:s:n:q:t:ORD")) != -1) {
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
				num_aio = tmp;
			break;
			case 'q':
				tmp = strtol(optarg, NULL, 10);
				if (tmp == LONG_MIN || tmp == LONG_MAX || tmp > INT_MAX || tmp < INT_MIN) {
					fprintf(stderr, "invalid queue depth %ld\n", tmp);
					ret = -EINVAL;
					goto exit;
				}
				qdepth = tmp;
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

	num_count = log10((double)num_aio);

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

    cmd_data = valloc(sizeof(struct kv_data) * qdepth * iothread);
    if (!cmd_data) {
        fprintf(stderr, "fail to alloc cmd_data qdepth %d\n", qdepth * iothread);
        ret = -ENOMEM;
        goto exit;
    }
    memset(cmd_data, 0, sizeof(struct kv_data) * qdepth * iothread);

    for (i = 0; i < qdepth * iothread; i++) {
        //posix_memalign((void **)&cmd_data[i].key_buf, 4096, key_len);
        //posix_memalign((void **)&cmd_data[i].val_buf, 4096, value_len);
        cmd_data[i].key_buf = malloc(key_len);
        cmd_data[i].val_buf = malloc(value_len);
        if (!cmd_data[i].key_buf || !cmd_data[i].val_buf) {
            fprintf(stderr, "fail to alloc cmd_data buffer index %d\n", i);
            ret = -ENOMEM;
            goto exit;
        }
    }

	cmd_ctx = (struct aio_cmd_ctx *)valloc(sizeof(struct aio_cmd_ctx) * qdepth * iothread);
	if (!cmd_ctx) {
		fprintf(stderr, "fail to alloc cmd ctx size %d\n", (int)(sizeof(struct aio_cmd_ctx) * qdepth * iothread));
		ret = -ENOMEM;
		goto exit;
	}
	memset(cmd_ctx, 0, sizeof(struct aio_cmd_ctx) * qdepth * iothread);

    for (k = 0; k < iothread; k++) {
    	for (i = 0; i < qdepth; i++) {
	    	cmd_ctx[k*qdepth + i].index = i;
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
    if (num_aio < iothread) iothread = num_aio;
    remain_reqs = num_aio;
    reqs_count = 0;
    per_thread_reqs = num_aio / iothread;

    for (i = 0; i < iothread; i++)
    {
        threads[i].fd = fd;
        threads[i].nsid = nsid;
        threads[i].key_len = key_len;
        threads[i].value_len = value_len;
        threads[i].cmd_data = &cmd_data[i*qdepth];
        threads[i].cmd_ctx = &cmd_ctx[i*qdepth];
        threads[i].qdepth = qdepth;
        threads[i].efd = eventfd(0,0);
        if (threads[i].efd < 0) {
	        fprintf(stderr, "fail to create an event.\n");
    		goto exit;
    	}
#ifdef DUMP
    	fprintf(stderr, "[set AIOCTX with efd %d]\n", threads[i].efd);
#endif
    	threads[i].aioctx.eventfd = threads[i].efd;
	    if (ioctl(fd, NVME_IOCTL_SET_AIOCTX, &threads[i].aioctx) < 0) {
		    fprintf(stderr, "fail to set_aioctx.\n");
		    goto exit;
	    }
#ifdef DUMP
	    fprintf(stderr, "[set aioctx efd %d and ctxid %d]\n", threads[i].aioctx.eventfd, threads[i].aioctx.ctxid);
#endif 
        for (k = 0; k < qdepth; k++) {
            threads[i].cmd_ctx[k].index = k;
        }
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
        threads[i].failed_io = 0;
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
            if (pthread_create(&threads[i].thread, &attr, do_aio_worker, &threads[i])) {
                pthread_attr_destroy(&attr);
		        fprintf(stderr, "fail to create pthread %d.\n", i);
		        goto exit;
            }
            pthread_attr_destroy(&attr);
            fprintf(stderr, "create thread %d on cpu %d req start %d req count %d\n", i, threads[i].cpu,  threads[i].start_num, threads[i].num_io);
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
        total_failed_io = 0;
        for (i = 0; i < iothread; i++) {
            fprintf(stderr, "[Thread %d]\n", i);
            latency_ns = threads[i].execute_ns/threads[i].num_io;
            iops = 1000000000/latency_ns;
            fprintf(stderr, "\t\t\tiops = %lf latency_per_operation = %lf nanosecond failed req count %d.\n", iops, latency_ns, threads[i].failed_io);
            average_iops += iops;
            average_latency_ns +=latency_ns;
            total_failed_io += threads[i].failed_io;
        }
        average_iops = average_iops/iothread;
        average_latency_ns = average_latency_ns/iothread;
        fprintf(stderr, "\tAverage %s per thread iops = %lf latency_per_operation = %lf nanosecond total failed req %lu.\n",op_str(k),  average_iops, average_latency_ns, total_failed_io);

        totalnanosecs = (t2.tv_sec - t1.tv_sec) * 1000000000;
        totalnanosecs += (t2.tv_nsec - t1.tv_nsec);
        fprintf(stderr, "\n\n-------- TOTAL execution time -------------------\n");
        fprintf(stderr, "total execution nanosecond %lld\n", totalnanosecs);
        latency_ns = totalnanosecs /num_aio;
        iops = 1000000000/latency_ns;
        fprintf(stderr, "%s result  iops = %lf latency_per_operation = %lf nanosecond.\n", op_str(k), iops, latency_ns);
    }
exit:
    if (threads) {
        for (i = 0; i < iothread; i++) {
        	if (threads[i].efd > 0) {
		        if (ioctl(fd, NVME_IOCTL_DEL_AIOCTX, &threads[i].aioctx) < 0) {
			        fprintf(stderr, "fail to del aio ctx.\n");
		        }   
		        close(threads[i].efd);
	        }
        }
    }
	if (fd >= 0) {
		close(fd);
	}
    if (cmd_data) {
        for(i = 0; i < qdepth * iothread; i++) {
            if (cmd_data[i].key_buf) free(cmd_data[i].key_buf);
            if (cmd_data[i].val_buf) free(cmd_data[i].val_buf);
        }
        free(cmd_data);
    }
	if (cmd_ctx) free(cmd_ctx);
    if (threads) free(threads);
	return ret;
}

