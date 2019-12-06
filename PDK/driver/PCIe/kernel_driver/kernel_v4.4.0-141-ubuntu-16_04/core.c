/*
 * NVM Express device driver
 * Copyright (c) 2011-2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/hdreg.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/list_sort.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/pr.h>
#include <linux/ptrace.h>
#if 0
#include <linux/nvme_ioctl.h>
#endif

#include <linux/t10-pi.h>
#include <linux/pm_qos.h>
#include <scsi/sg.h>
#include <asm/unaligned.h>

#include "nvme.h"

#if 1
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/eventfd.h>
#include <linux/kref.h>
#include <linux/freezer.h>
#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/task_io_accounting_ops.h>
#include "linux_nvme_ioctl.h"
#endif


#define NVME_MINORS		(1U << MINORBITS)

static int nvme_major;
module_param(nvme_major, int, 0);

static int nvme_char_major;
module_param(nvme_char_major, int, 0);

static unsigned long default_ps_max_latency_us = 100000;
module_param(default_ps_max_latency_us, ulong, 0644);
MODULE_PARM_DESC(default_ps_max_latency_us,
		 "max power saving latency for new devices; use PM QOS to change per device");

static LIST_HEAD(nvme_ctrl_list);
DEFINE_SPINLOCK(dev_list_lock);

static struct class *nvme_class;

#if 1
/*
 * PORT AIO Support logic from Kvepic.
 */

// AIO data structures
static struct kmem_cache        *kaioctx_cachep = 0;
static struct kmem_cache        *kaiocb_cachep = 0;
static mempool_t *kaiocb_mempool = 0;
static mempool_t *kaioctx_mempool = 0;

static __u32 aio_context_id;

#define AIOCTX_MAX 1024
#define AIOCB_MAX (1024*64)

static __u64 debug_completed  =0;
static int debug_outstanding  =0;

struct nvme_kaioctx
{
    struct nvme_aioctx uctx;
    struct eventfd_ctx *eventctx;
    struct list_head kaiocb_list;
    spinlock_t kaioctx_spinlock;
    struct kref ref;
};

static struct nvme_kaioctx **g_kaioctx_tb = NULL;
static spinlock_t g_kaioctx_tb_spinlock;

struct aio_user_ctx {
	int nents;
	int len;
	struct page ** pages;
	struct scatterlist *sg;
	char data[1];
};

struct nvme_kaiocb
{
	struct list_head aiocb_list;
	struct nvme_aioevent event;
	struct nvme_completion cqe;
    int opcode;
    struct nvme_command cmd;
	struct gendisk *disk;
    struct scatterlist meta_sg;
    bool need_to_copy;
    unsigned long start_time;
    bool use_meta;
	void *kv_data;
	void *meta;
	struct aio_user_ctx *user_ctx;
	struct aio_user_ctx *kernel_ctx;
    struct request *req;
};

/* context for aio worker.*/
struct aio_worker_ctx{
    int cpu;
    spinlock_t kaiocb_spinlock;
    struct list_head kaiocb_list;
    wait_queue_head_t aio_waitqueue;
};

/* percpu aio worker context pointer */
struct aio_worker_ctx * __percpu aio_w_ctx;
/* percpu aio worker pointer */
struct task_struct ** __percpu aio_worker;



static void remove_kaioctx(struct nvme_kaioctx * ctx)
{
	struct nvme_kaiocb *tmpcb;
    struct list_head *pos, *q;
	unsigned long flags;
    if (ctx) {
        spin_lock_irqsave(&ctx->kaioctx_spinlock, flags);
        list_for_each_safe(pos, q, &ctx->kaiocb_list){
    	    tmpcb = list_entry(pos, struct nvme_kaiocb, aiocb_list);
            list_del(pos);
			mempool_free(tmpcb, kaiocb_mempool);
   	    }
        spin_unlock_irqrestore(&ctx->kaioctx_spinlock, flags);
        eventfd_ctx_put(ctx->eventctx);	
    	mempool_free(ctx, kaioctx_mempool);
    }
}

static void cleanup_kaioctx(struct kref *kref) {
    struct nvme_kaioctx *ctx = container_of(kref, struct nvme_kaioctx, ref);
    remove_kaioctx(ctx);
}

static void ref_kaioctx(struct nvme_kaioctx *ctx) {
    kref_get(&ctx->ref);
}

static void deref_kaioctx(struct nvme_kaioctx *ctx) {
    kref_put(&ctx->ref, cleanup_kaioctx);
}


/* destroy memory pools
 */
static void destroy_aio_mempool(void)
{
    int i = 0;
    if (g_kaioctx_tb) {
        for (i = 0; i < AIOCTX_MAX; i++) {
            if (g_kaioctx_tb[i]) {
               remove_kaioctx(g_kaioctx_tb[i]);
               g_kaioctx_tb[i] = NULL;
            }
        }
        kfree(g_kaioctx_tb);
        g_kaioctx_tb = NULL;
    }
    if (kaiocb_mempool)
        mempool_destroy(kaiocb_mempool);
    if (kaioctx_mempool)
        mempool_destroy(kaioctx_mempool);
    if (kaioctx_cachep)
        kmem_cache_destroy(kaioctx_cachep);
    if (kaiocb_cachep)
        kmem_cache_destroy(kaiocb_cachep);
}

/* prepare basic data structures
 * to support aio context and requests
 */
static int aio_service_init(void)
{
    g_kaioctx_tb = (struct nvme_kaioctx **)kmalloc(sizeof(struct nvme_kaioctx *) * AIOCTX_MAX, GFP_KERNEL);
    if (!g_kaioctx_tb)
        goto fail;
    memset(g_kaioctx_tb, 0, sizeof(struct nvme_kaioctx *) * AIOCTX_MAX);

	// slap allocator and memory pool
    kaioctx_cachep = kmem_cache_create("nvme_kaioctx", sizeof(struct nvme_kaioctx), 0, 0, NULL);
    if (!kaioctx_cachep)
        goto fail;

    kaiocb_cachep = kmem_cache_create("nvme_kaiocb", sizeof(struct nvme_kaiocb), 0, 0, NULL);
    if (!kaiocb_cachep)
        goto fail;

    kaiocb_mempool = mempool_create_slab_pool(AIOCB_MAX, kaiocb_cachep);
    if (!kaiocb_mempool)
        goto fail;

    kaioctx_mempool = mempool_create_slab_pool(AIOCTX_MAX, kaioctx_cachep);
    if (!kaioctx_mempool)
        goto fail;

    // global variables
    // context id 0 is reserved for normal I/O operations (synchronous)
    aio_context_id = 1;
    spin_lock_init(&g_kaioctx_tb_spinlock);
    printk(KERN_DEBUG"nvme-aio: initialized\n");
    return 0;

fail:
    destroy_aio_mempool();
    return -ENOMEM;
}

/*
 * release memory before exit
 */
static int aio_service_exit(void)
{
    destroy_aio_mempool();
    printk(KERN_DEBUG"nvme-aio: unloaded\n");
    return 0;
}

static struct nvme_kaioctx * find_kaioctx(__u32 ctxid) {
    struct nvme_kaioctx * tmp = NULL;
    //unsigned long flags;
    //spin_lock_irqsave(&g_kaioctx_tb_spinlock, flags);
    tmp = g_kaioctx_tb[ctxid];
    if (tmp) ref_kaioctx(tmp);
    //spin_unlock_irqrestore(&g_kaioctx_tb_spinlock, flags);
    return tmp;
}


/*
 * find an aio context with a given id
 */
static int  set_aioctx_event(__u32 ctxid, struct nvme_kaiocb * kaiocb)
{
    struct nvme_kaioctx *tmp;
	unsigned long flags;
    tmp = find_kaioctx(ctxid);
    if (tmp) {
        spin_lock_irqsave(&tmp->kaioctx_spinlock, flags);
	    list_add_tail(&kaiocb->aiocb_list, &tmp->kaiocb_list);
		eventfd_signal(tmp->eventctx, 1);
#if 0
		pr_err("nvme_set_aioctx: success to signal event %d %llu\n", kaiocb->event.ctxid, kaiocb->event.reqid);
#endif
        spin_unlock_irqrestore(&tmp->kaioctx_spinlock, flags);
        deref_kaioctx(tmp);
        return 0;
    } else {
#if 0
		pr_err("nvme_set_aioctx: failed to signal event %d.\n", ctxid);
#endif
    }

    return -1;
}

/*
 * delete an aio context
 * it will release any resources allocated for this context
 */
static int nvme_del_aioctx(struct nvme_aioctx __user *uctx )
{
	struct nvme_kaioctx ctx;
	unsigned long flags;
    if (copy_from_user(&ctx, uctx, sizeof(struct nvme_aioctx)))
        return -EFAULT;

    spin_lock_irqsave(&g_kaioctx_tb_spinlock, flags);
    if (g_kaioctx_tb[ctx.uctx.ctxid]) {
        deref_kaioctx(g_kaioctx_tb[ctx.uctx.ctxid]);
        g_kaioctx_tb[ctx.uctx.ctxid] = NULL;
    }
    spin_unlock_irqrestore(&g_kaioctx_tb_spinlock, flags);
    return 0;
}

/*
 * set up an aio context
 * allocate a new context with given parameters and prepare a eventfd_context
 */
static int nvme_set_aioctx(struct nvme_aioctx __user *uctx )
{
    struct nvme_kaioctx *ctx;
	struct fd efile;
	struct eventfd_ctx *eventfd_ctx = NULL;
	unsigned long flags;
	int ret = 0;
    int i = 0;
    if (!capable(CAP_SYS_ADMIN))
        return -EACCES;

    ctx = mempool_alloc(kaioctx_mempool, GFP_NOIO); //ctx = kmem_cache_zalloc(kaioctx_cachep, GFP_KERNEL);
    if (!ctx)
        return -ENOMEM;

    if (copy_from_user(ctx, uctx, sizeof(struct nvme_aioctx)))
        return -EFAULT;

	efile = fdget(ctx->uctx.eventfd);
	if (!efile.file) {
		pr_err("nvme_set_aioctx: failed to get efile for efd %d.\n", ctx->uctx.eventfd);
		ret = -EBADF;
		goto exit;
	}

	eventfd_ctx = eventfd_ctx_fileget(efile.file);
	if (IS_ERR(eventfd_ctx)) {
		pr_err("nvme_set_aioctx: failed to get eventfd_ctx for efd %d.\n", ctx->uctx.eventfd);
		ret = PTR_ERR(eventfd_ctx);
		goto put_efile;
	}
    // set context id
    spin_lock_irqsave(&g_kaioctx_tb_spinlock, flags);
    if(g_kaioctx_tb[aio_context_id]) {
        for(i = 0; i < AIOCTX_MAX; i++) {
           if(g_kaioctx_tb[i] == NULL) {
               aio_context_id = i;
               break;
           }
        }
        if (i >= AIOCTX_MAX) {
	    	pr_err("nvme_set_aioctx: too many aioctx open.\n");
	    	ret = -EMFILE;
	    	goto put_event_fd;
        }
    }
    ctx->uctx.ctxid = aio_context_id++;
    if (aio_context_id == AIOCTX_MAX)
        aio_context_id = 0;
	ctx->eventctx = eventfd_ctx;
    spin_lock_init(&ctx->kaioctx_spinlock);
    INIT_LIST_HEAD(&ctx->kaiocb_list);
    kref_init(&ctx->ref);
    g_kaioctx_tb[ctx->uctx.ctxid] = ctx;
    spin_unlock_irqrestore(&g_kaioctx_tb_spinlock, flags);

    if (copy_to_user(&uctx->ctxid, &ctx->uctx.ctxid, sizeof(ctx->uctx.ctxid)))
    {
		pr_err("nvme_set_aioctx: failed to copy context id %d to user.\n", ctx->uctx.ctxid);
        ret =  -EINVAL;
		goto cleanup;
    }
	eventfd_ctx = NULL;
    debug_outstanding = 0;
    debug_completed = 0;
	fdput(efile);
    return 0;
cleanup:
    spin_lock_irqsave(&g_kaioctx_tb_spinlock, flags);
    g_kaioctx_tb[ctx->uctx.ctxid - 1] = NULL;
    spin_unlock_irqrestore(&g_kaioctx_tb_spinlock, flags);
	mempool_free(ctx, kaiocb_mempool);
put_event_fd:
	eventfd_ctx_put(eventfd_ctx);	
put_efile:
	fdput(efile);
exit:
	return ret;
}

/* get an aiocb, which represents a single I/O request.
 */
static struct nvme_kaiocb *get_aiocb(__u64 reqid) {
	struct nvme_kaiocb *req;
    req = mempool_alloc(kaiocb_mempool, GFP_NOIO); //ctx = kmem_cache_zalloc(kaioctx_cachep, GFP_KERNEL);
    if (!req) return 0;

    memset(req, 0, sizeof(*req));

    INIT_LIST_HEAD(&req->aiocb_list);

    req->event.reqid = reqid;

    return req;
}

/* returns the completed events to users
 */
static int nvme_get_ioevents(struct nvme_aioevents __user *uevents)
{
	struct list_head *pos, *q;
	struct nvme_kaiocb *tmp;
	struct nvme_kaioctx *tmp_ctx;
	unsigned long flags;
    LIST_HEAD(tmp_head);
	__u16 count =0;
	__u16 nr = 0;
    __u32 ctxid = 0;

    if (!capable(CAP_SYS_ADMIN))
        return -EACCES;

    if (get_user(nr, &uevents->nr) < 0) { 	return -EINVAL;    }

    if (nr == 0 || nr > 128) return -EINVAL;

    if (get_user(ctxid, &uevents->ctxid) < 0) { return -EINVAL; }

    tmp_ctx = find_kaioctx(ctxid);
    if (tmp_ctx) {
        spin_lock_irqsave(&tmp_ctx->kaioctx_spinlock, flags);
        list_for_each_safe(pos, q, &tmp_ctx->kaiocb_list){
    	    list_del_init(pos);
            list_add(pos, &tmp_head);
            count++;
            if (nr == count) break;
        }
        spin_unlock_irqrestore(&tmp_ctx->kaioctx_spinlock, flags);
        deref_kaioctx(tmp_ctx);
        count = 0; 
        list_for_each_safe(pos, q, &tmp_head){
    	    list_del(pos);
    	    tmp = list_entry(pos, struct nvme_kaiocb, aiocb_list);
            copy_to_user(&uevents->events[count], &tmp->event, sizeof(struct nvme_aioevent));
	        mempool_free(tmp, kaiocb_mempool);
            count++;
        }
   	}
    if (put_user(count, &uevents->nr)  < 0) { return -EINVAL; }

    return 0;
}

static void kv_complete_aio_fn(struct nvme_kaiocb *cmdinfo) { 
    struct request *req =  cmdinfo->req;
	int i = 0;

	if (cmdinfo->need_to_copy) {
        /*
           unaligned user buffer, copy back to user if this request is for read.
         */
		if (is_kv_retrieve_cmd(cmdinfo->opcode) || is_kv_iter_read_cmd(cmdinfo->opcode)) {
#if 0
            char *data = cmdinfo->kv_data;
            pr_err("kv_async_completion: data %c%c%c%c.\n", data[0], data[1], data[2], data[3]);
#endif
			(void)sg_copy_from_buffer(cmdinfo->user_ctx->sg, cmdinfo->user_ctx->nents,
					cmdinfo->kv_data, cmdinfo->user_ctx->len);
        }
		for (i = 0; i < cmdinfo->kernel_ctx->nents; i++) {
			put_page(sg_page(&cmdinfo->kernel_ctx->sg[i]));
        }
	}
    if (cmdinfo->user_ctx) {
        if (is_kv_store_cmd(cmdinfo->opcode) || is_kv_append_cmd(cmdinfo->opcode)) {
            generic_end_io_acct(WRITE, &cmdinfo->disk->part0, cmdinfo->start_time);
        } else {
            generic_end_io_acct(READ, &cmdinfo->disk->part0, cmdinfo->start_time);
        }
        for (i = 0; i < cmdinfo->user_ctx->nents; i++) {
            put_page(sg_page(&cmdinfo->user_ctx->sg[i]));
        }
	}
	blk_mq_free_request(req);

    if (cmdinfo->use_meta) {
        put_page(sg_page(&cmdinfo->meta_sg));
    }

    if (cmdinfo->need_to_copy) { 
        kfree(cmdinfo->kernel_ctx);
        kfree(cmdinfo->kv_data);
    }
    if (cmdinfo->user_ctx) kfree(cmdinfo->user_ctx);
    if (cmdinfo->meta) kfree(cmdinfo->meta);

	if (set_aioctx_event(cmdinfo->event.ctxid, cmdinfo)) {
		mempool_free(cmdinfo, kaiocb_mempool);
	}
}

static void wake_up_aio_worker(struct aio_worker_ctx *aio_ctx) {
    wake_up(&aio_ctx->aio_waitqueue);
}

static void insert_aiocb_to_worker(struct nvme_kaiocb *aiocb) {
        struct aio_worker_ctx *ctx = NULL;
        unsigned long flags;
        int cpu = smp_processor_id();
        INIT_LIST_HEAD(&aiocb->aiocb_list);
		//pr_err("insert aiocb(%p) to aio_worker(%d)!\n", aiocb, cpu);
        ctx = per_cpu_ptr(aio_w_ctx, cpu);
        spin_lock_irqsave(&ctx->kaiocb_spinlock, flags);
        list_add_tail(&aiocb->aiocb_list, &ctx->kaiocb_list);
        spin_unlock_irqrestore(&ctx->kaiocb_spinlock, flags);
        wake_up_aio_worker(ctx);
        return;
}

static void kv_async_completion(struct request *req, int error){
    struct nvme_kaiocb *aiocb = req->end_io_data;
    struct nvme_completion *cqe = req->special;
	//pr_err("complition handler for aiocb(%p)!\n", aiocb);
    aiocb->req = req;
	aiocb->event.result = le32_to_cpu(cqe->result);
	aiocb->event.status = le32_to_cpu(cqe->status) >> 1;
    insert_aiocb_to_worker(aiocb);
    return;
}

static int kvaio_percpu_worker_fn(void *arg) {
    struct aio_worker_ctx *ctx = (struct aio_worker_ctx *)arg;
    struct list_head *pos, *next;
    unsigned long flags;
    LIST_HEAD(tmp_list);
	pr_err("start aio worker %u\n", ctx->cpu);
    while(!kthread_should_stop() || !list_empty(&ctx->kaiocb_list)) {
        if (list_empty(&ctx->kaiocb_list)) {
            wait_event_interruptible_timeout(ctx->aio_waitqueue,
                    !list_empty(&ctx->kaiocb_list), HZ/10);
            continue;
        }
        INIT_LIST_HEAD(&tmp_list);
        spin_lock_irqsave(&ctx->kaiocb_spinlock, flags);
        list_splice(&ctx->kaiocb_list, &tmp_list);
        INIT_LIST_HEAD(&ctx->kaiocb_list);
        spin_unlock_irqrestore(&ctx->kaiocb_spinlock, flags);
        if (!list_empty(&tmp_list)) {
            list_for_each_safe(pos, next, &tmp_list) {
                struct nvme_kaiocb *aiocb = list_entry(pos, struct nvme_kaiocb, aiocb_list);
                list_del_init(pos);
                kv_complete_aio_fn(aiocb);
            }
        }
    }
    return 0;
}

static int aio_worker_init(void) {
    struct task_struct **p = NULL;
    struct aio_worker_ctx *ctx = NULL;
    int i = 0;

    aio_worker = alloc_percpu(struct task_struct *);
    if (!aio_worker) {
		pr_err("fail to alloc percpu worker task_struct!\n");
        return -ENOMEM;
    }
    aio_w_ctx = alloc_percpu(struct aio_worker_ctx);
    if (!aio_w_ctx) {
		pr_err("fail to alloc percpu aio context!\n");
        goto out_free;
    }

    for_each_online_cpu(i) {
        ctx = per_cpu_ptr(aio_w_ctx, i);
        ctx->cpu = i;
        spin_lock_init(&ctx->kaiocb_spinlock);
        INIT_LIST_HEAD(&ctx->kaiocb_list);
        init_waitqueue_head(&ctx->aio_waitqueue);
        p = per_cpu_ptr(aio_worker, i);
        *p = kthread_create_on_node(kvaio_percpu_worker_fn, ctx, cpu_to_node(i), "aio_completion_worker/%u", i);
        if (!(*p))
            goto reset_pthread;
        kthread_bind(*p, i);
        wake_up_process(*p);
    }
    
    return 0;
reset_pthread:
    for_each_online_cpu(i) {
        p = per_cpu_ptr(aio_worker, i);
        if (*p) kthread_stop(*p);
    }
out_free:
    if (aio_worker)
        free_percpu(aio_worker);
    return -ENOMEM;
}

static void aio_worker_exit(void) {
    struct task_struct **p = NULL;
    int i = 0;
    for_each_online_cpu(i) {
        p = per_cpu_ptr(aio_worker, i);
        if (*p) kthread_stop(*p);
    }
    free_percpu(aio_worker);
    free_percpu(aio_w_ctx);
    return;
}

#endif




static void nvme_free_ns(struct kref *kref)
{
	struct nvme_ns *ns = container_of(kref, struct nvme_ns, kref);

	if (ns->type == NVME_NS_LIGHTNVM)
		nvme_nvm_unregister(ns->queue, ns->disk->disk_name);

	spin_lock(&dev_list_lock);
	ns->disk->private_data = NULL;
	spin_unlock(&dev_list_lock);

	nvme_put_ctrl(ns->ctrl);
	put_disk(ns->disk);
	kfree(ns);
}

static void nvme_put_ns(struct nvme_ns *ns)
{
	kref_put(&ns->kref, nvme_free_ns);
}

static struct nvme_ns *nvme_get_ns_from_disk(struct gendisk *disk)
{
	struct nvme_ns *ns;

	spin_lock(&dev_list_lock);
	ns = disk->private_data;
	if (ns && !kref_get_unless_zero(&ns->kref))
		ns = NULL;
	spin_unlock(&dev_list_lock);

	return ns;
}

void nvme_requeue_req(struct request *req)
{
	unsigned long flags;

	blk_mq_requeue_request(req);
	spin_lock_irqsave(req->q->queue_lock, flags);
	if (!blk_queue_stopped(req->q))
		blk_mq_kick_requeue_list(req->q);
	spin_unlock_irqrestore(req->q->queue_lock, flags);
}

struct request *nvme_alloc_request(struct request_queue *q,
		struct nvme_command *cmd, unsigned int flags, int qid)
{
	struct request *req;

	if (qid == NVME_QID_ANY) {
		req = blk_mq_alloc_request(q, nvme_is_write(cmd), flags);
	} else {
		req = blk_mq_alloc_request_hctx(q, nvme_is_write(cmd), flags,
				qid ? qid - 1 : 0);
	}
	if (IS_ERR(req))
		return req;

	req->cmd_type = REQ_TYPE_DRV_PRIV;
	req->cmd_flags |= REQ_FAILFAST_DRIVER;
	req->__data_len = 0;
	req->__sector = (sector_t) -1;
	req->bio = req->biotail = NULL;

	req->cmd = (unsigned char *)cmd;
	req->cmd_len = sizeof(struct nvme_command);

	return req;
}

/*
 * Returns 0 on success.  If the result is negative, it's a Linux error code;
 * if the result is positive, it's an NVM Express status code
 */
int __nvme_submit_sync_cmd(struct request_queue *q, struct nvme_command *cmd,
		struct nvme_completion *cqe, void *buffer, unsigned bufflen,
		unsigned timeout, int qid, int at_head, int flags)
{
	struct request *req;
	int ret;

	req = nvme_alloc_request(q, cmd, flags, qid);
	if (IS_ERR(req))
		return PTR_ERR(req);

	req->timeout = timeout ? timeout : ADMIN_TIMEOUT;
	req->special = cqe;

	if (buffer && bufflen) {
		ret = blk_rq_map_kern(q, req, buffer, bufflen, GFP_KERNEL);
		if (ret)
			goto out;
	}

	blk_execute_rq(req->q, NULL, req, at_head);
	ret = req->errors;
 out:
	blk_mq_free_request(req);
	return ret;
}
EXPORT_SYMBOL_GPL(__nvme_submit_sync_cmd);

int nvme_submit_sync_cmd(struct request_queue *q, struct nvme_command *cmd,
		void *buffer, unsigned bufflen)
{
	return __nvme_submit_sync_cmd(q, cmd, NULL, buffer, bufflen, 0,
			NVME_QID_ANY, 0, 0);
}
#if 1
/*
 * There was two types of operation need to map single continueous physical continues physical address.
 * 1. kv_exist and kv_iterate's buffer.
 * 2. kv_store, kv_retrieve, and kv_delete's key buffer.
 * Note.
 * - check it's address 4byte word algined. If not malloc and copy data.
 */
#define KV_QUEUE_DAM_ALIGNMENT (0x03)
static bool check_add_for_single_cont_phyaddress(void __user *address, unsigned length, struct request_queue *q)
{
	unsigned offset = 0;
	unsigned count = 0;

	offset = offset_in_page(address);
	count = DIV_ROUND_UP(offset + length, PAGE_SIZE);
	if ((count > 1) || ((unsigned long)address & KV_QUEUE_DAM_ALIGNMENT)) {
		/* addr does not aligned as 4 bytes or addr needs more than one page */
		return false;
	}
	return true;
}

static int user_addr_npages(int offset, int size)
{
	unsigned count = DIV_ROUND_UP(offset + size, PAGE_SIZE);
	return count;
}

static struct aio_user_ctx *get_aio_user_ctx(void __user *addr, unsigned len, bool b_kernel)
{
	int offset = offset_in_page(addr);
	int datalen = len;
	int num_page = user_addr_npages(offset,len);
	int size = 0;
	struct aio_user_ctx  *user_ctx = NULL;
	int mapped_pages = 0;
	int i = 0;

	size = sizeof (struct aio_user_ctx) + sizeof(__le64 *) * num_page
			+ sizeof(struct scatterlist) * num_page -1;
	/* need to keep user address to map to copy when complete request */
	user_ctx = (struct aio_user_ctx *)kmalloc(size, GFP_KERNEL);
	if (!user_ctx) {
		return NULL;
    }

    user_ctx->nents = 0;
	user_ctx->pages =(struct page **)user_ctx->data;
	user_ctx->sg = (struct scatterlist *)(user_ctx->data + sizeof(__le64 *) * num_page);
    if (b_kernel) {
        struct page *page = NULL;
        char *src_data = addr;
        for(i = 0; i < num_page; i++) {
            page = virt_to_page(src_data);
            get_page(page);
            user_ctx->pages[i] = page;
            src_data += PAGE_SIZE;
        }

    } else {
        mapped_pages = get_user_pages_fast((unsigned long)addr, num_page,
                1, user_ctx->pages);
        if (mapped_pages != num_page) {
            user_ctx->nents = mapped_pages;
            goto exit;
        }
    }
    user_ctx->nents = num_page;
    user_ctx->len = datalen;
    sg_init_table(user_ctx->sg, num_page);
    for(i = 0; i < num_page; i++) {
        sg_set_page(&user_ctx->sg[i], user_ctx->pages[i],
                min_t(unsigned, datalen, PAGE_SIZE - offset),
                offset);
        datalen -= (PAGE_SIZE - offset);
        offset = 0;
    }
    sg_mark_end(&user_ctx->sg[i -1]);
	return user_ctx;
exit:
	if (user_ctx) {
		for (i = 0; i < user_ctx->nents; i++)
			put_page(user_ctx->pages[i]);
		kfree(user_ctx);
	}
	return NULL;
}
int __nvme_submit_kv_user_cmd(struct request_queue *q, struct nvme_command *cmd,
		struct nvme_passthru_kv_cmd *pthr_cmd,
		void __user *ubuffer, unsigned bufflen,
		void __user *meta_buffer, unsigned meta_len, u32 meta_seed,
		u32 *result, u32 *status, unsigned timeout, bool aio)
{
	struct nvme_completion cqe;
	struct nvme_ns *ns = q->queuedata;
	struct gendisk *disk = ns ? ns->disk : NULL;
	struct request *req;
	int ret = 0;
	struct nvme_kaiocb *aiocb = NULL;
    struct aio_user_ctx  *user_ctx = NULL;
	struct aio_user_ctx  *kernel_ctx = NULL;
    struct scatterlist* meta_sg_ptr;
    struct scatterlist meta_sg;
    struct page* p_page = NULL;
    struct nvme_io_param* param = NULL;
    char *kv_data = NULL;
    char *kv_meta = NULL;
    bool need_to_copy = false; 
	int i = 0, offset = 0;
    unsigned len = 0;
    unsigned long start_time = jiffies;
	if (!disk)
			return -EFAULT;

    if (aio) {
   		aiocb = get_aiocb(pthr_cmd->reqid);
		if (!aiocb) {
			ret = -ENOMEM;
			goto out_end;
        }
        aiocb->cmd = *cmd;
        aiocb->disk = disk;
        cmd = &aiocb->cmd;
    }
	req = nvme_alloc_request(q, cmd, 0, NVME_QID_ANY);
	if (IS_ERR(req)) {
        if (aiocb) mempool_free(aiocb, kaiocb_mempool);
		return PTR_ERR(req);
    }

	req->timeout = timeout ? timeout : ADMIN_TIMEOUT;
    if (!aio)
        req->special = &cqe;
    param = nvme_io_param(req);
    param->kv_meta_sg_ptr = NULL;
    param->kv_data_sg_ptr = NULL;
    param->kv_data_nents = 0;
    param->kv_data_len = 0;

    if (ubuffer && bufflen) {
        if ((unsigned long)ubuffer & KV_QUEUE_DAM_ALIGNMENT) {
            need_to_copy = true; 
            len = DIV_ROUND_UP(bufflen, PAGE_SIZE)*PAGE_SIZE;
            kv_data = kmalloc(len, GFP_KERNEL);
            if (kv_data == NULL) {
			    ret = -ENOMEM;
                goto out_req_end;
            }
        }
        user_ctx = get_aio_user_ctx(ubuffer, bufflen, false);
        if (need_to_copy) {
            kernel_ctx = get_aio_user_ctx(kv_data, bufflen, true);
            if (kernel_ctx) {
                if (is_kv_store_cmd(cmd->common.opcode) || is_kv_append_cmd(cmd->common.opcode)) {
			        (void)sg_copy_to_buffer(user_ctx->sg, user_ctx->nents,
					    kv_data, user_ctx->len);
#if 0
                    pr_err("copied data %c:%c:%c:%c: %c:%c:%c:%c.\n",
                            kv_data[0], kv_data[1], kv_data[2], kv_data[3],
                            kv_data[4], kv_data[5], kv_data[6], kv_data[7]);
#endif
                }
            }

        } else {
            kernel_ctx = user_ctx;
        }
        if (user_ctx == NULL || kernel_ctx == NULL) {
            ret = -ENOMEM;
            goto out_unmap;
        }
        param->kv_data_sg_ptr = kernel_ctx->sg;
        param->kv_data_nents = kernel_ctx->nents;
        param->kv_data_len = kernel_ctx->len;
        if (aio) {
            aiocb->need_to_copy = need_to_copy;
            aiocb->kv_data = kv_data;
            aiocb->kernel_ctx = kernel_ctx;
            aiocb->user_ctx = user_ctx;
            aiocb->start_time = jiffies;
        }
        if (is_kv_store_cmd(cmd->common.opcode) || is_kv_append_cmd(cmd->common.opcode)) {
            generic_start_io_acct(WRITE, (bufflen >> 9 ? bufflen >>9 : 1), &disk->part0);
        } else {
            generic_start_io_acct(READ, (bufflen >> 9 ? bufflen >>9 : 1), &disk->part0);
        }
    }

    if (meta_buffer && meta_len) {
        if (check_add_for_single_cont_phyaddress(meta_buffer, meta_len, q)) {
            ret = get_user_pages_fast((unsigned long)meta_buffer, 1, 0, &p_page);
            if (ret != 1) {
                ret = -ENOMEM;
                goto out_unmap;
            }
            offset = offset_in_page(meta_buffer);
        } else {
            len = DIV_ROUND_UP(meta_len, 256) * 256;
            kv_meta = kmalloc(len, GFP_KERNEL);
            if (!kv_meta) {
                ret = -ENOMEM;
                goto out_unmap;
            }
            if (copy_from_user(kv_meta, meta_buffer, meta_len)) {
                ret = -EFAULT;
                goto out_free_meta;
            }
            offset = offset_in_page(kv_meta);
            p_page = virt_to_page(kv_meta);
            get_page(p_page);
        }
        if (aio) {
            aiocb->use_meta = true;
            aiocb->meta = kv_meta;
            meta_sg_ptr = &aiocb->meta_sg;
        } else {
            meta_sg_ptr = &meta_sg;
        }
        sg_init_table(meta_sg_ptr, 1);
        sg_set_page(meta_sg_ptr, p_page, meta_len, offset);
        sg_mark_end(meta_sg_ptr);
        param->kv_meta_sg_ptr = meta_sg_ptr;
    } else {
        param->kv_meta_sg_ptr = NULL;
    }

	if (aio) {
		aiocb->event.ctxid = pthr_cmd->ctxid;
		aiocb->event.reqid = pthr_cmd->reqid;
		aiocb->opcode = cmd->common.opcode;
		req->end_io_data = aiocb;
        req->special = &aiocb->cqe;
		blk_execute_rq_nowait(req->q, disk, req, 0, kv_async_completion);
		return 0;
	} else {
		blk_execute_rq(req->q, disk, req, 0);
		ret = req->errors;
		if (result)
		    *result = le32_to_cpu(cqe.result);
		if (status)
			*status = le32_to_cpu(cqe.status) >> 1;
		if (ret && !is_kv_iter_req_cmd(cmd->common.opcode) && !is_kv_iter_read_cmd(cmd->common.opcode)) {
#if 0
			pr_err("__nvme_submit_user_cmd failed!!!!!: opcode(%02x)\n", cmd->common.opcode);
#endif
		}
    }
    if (need_to_copy) {
		if ((is_kv_retrieve_cmd(cmd->common.opcode) && !ret) ||
              (is_kv_iter_read_cmd(cmd->common.opcode) && (!ret || (((le16_to_cpu(cqe.status) >>1) & 0x00ff) == 0x0093)))) {
#if 0
            char *data = kv_data;
            pr_err("recevied data %c:%c:%c:%c: %c:%c:%c:%c.\n",
                    data[0], data[1], data[2], data[3],
                    data[4], data[5], data[6], data[7]);
#endif
			(void)sg_copy_from_buffer(user_ctx->sg, user_ctx->nents,
					kv_data, user_ctx->len);
        }
    }

 out_free_meta:
    if (p_page) put_page(p_page);
	if (kv_meta) kfree(kv_meta);
 out_unmap:
    if (user_ctx) {
        for (i = 0; i < user_ctx->nents; i++) {
            put_page(sg_page(&user_ctx->sg[i]));
        }
        kfree(user_ctx);
    }
    if (need_to_copy) {
        if (kernel_ctx) {
            for (i = 0; i < kernel_ctx->nents; i++) {
                put_page(sg_page(&kernel_ctx->sg[i]));
            }
            kfree(kernel_ctx);
        }
        if (kv_data) kfree(kv_data);
    }
out_req_end:
    if (aio && aiocb) mempool_free(aiocb, kaiocb_mempool);
	blk_mq_free_request(req);

    if (is_kv_store_cmd(cmd->common.opcode) || is_kv_append_cmd(cmd->common.opcode)) {
        generic_end_io_acct(WRITE, &disk->part0, start_time);
    } else {
        generic_end_io_acct(READ, &disk->part0, start_time);
    }
out_end:
	return ret;
}

static int nvme_user_kv_cmd(struct nvme_ctrl *ctrl,
			struct nvme_ns *ns,
			struct nvme_passthru_kv_cmd __user *ucmd, bool aio)
{
	struct nvme_passthru_kv_cmd cmd;
	struct nvme_command c;
	unsigned timeout = 0;
	int status;
	void __user *metadata = NULL;
	unsigned meta_len = 0;
    unsigned option = 0;
    unsigned iter_handle = 0;
	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	if (copy_from_user(&cmd, ucmd, sizeof(cmd)))
		return -EFAULT;
	if (cmd.flags)
		return -EINVAL;


	/* filter out non kv command */
	if (!is_kv_cmd(cmd.opcode)) {
		return -EINVAL;
	}
	memset(&c, 0, sizeof(c));
	c.common.opcode = cmd.opcode;
	c.common.flags = cmd.flags;
#ifdef KSID_SUPPORT
	c.common.nsid = cmd.cdw3;
#else
	c.common.nsid = cpu_to_le32(cmd.nsid);
#endif
	if (cmd.timeout_ms) 
		timeout = msecs_to_jiffies(cmd.timeout_ms);

//    if (cmd.data_addr) {
//		pr_err("nvme_user_kv_cmd: kv cmd opcode(%02x) info: key_length(%d) cdw11(%d) addr(%p) data_len(%d) cdw10(%d) data(%p).\n",
//				cmd.opcode, cmd.key_length, cmd.cdw11, (void *)cmd.key_addr, cmd.data_length, cmd.cdw10, (void *)cmd.data_addr);
//    }

	switch(cmd.opcode) {
		case nvme_cmd_kv_store:
        case nvme_cmd_kv_append:
            option = cpu_to_le32(cmd.cdw4);
			c.kv_store.offset = cpu_to_le32(cmd.cdw5);
			/* validate key length */
            if (cmd.key_length >  KVCMD_MAX_KEY_SIZE ||
                cmd.key_length < KVCMD_MIN_KEY_SIZE) {
				cmd.result = KVS_ERR_VALUE;
				status = -EINVAL;
				goto exit;
            }
			c.kv_store.key_len = cpu_to_le32(cmd.key_length -1); /* key len -1 */
            c.kv_store.option = (option & 0xff);
            /* set value size */
            if (cmd.data_length % 4) {
			    c.kv_store.value_len = cpu_to_le32((cmd.data_length >> 2) + 1);
                c.kv_store.invalid_byte = 4 - (cmd.data_length % 4);
            } else {
			    c.kv_store.value_len = cpu_to_le32((cmd.data_length >> 2));
            }
			if (cmd.key_length > KVCMD_INLINE_KEY_MAX) {
				metadata = (void __user*)cmd.key_addr;
				meta_len = cmd.key_length;
			} else {
				memcpy(c.kv_store.key, cmd.key, cmd.key_length);
			}
		break;
		case nvme_cmd_kv_retrieve:
            option = cpu_to_le32(cmd.cdw4);
			c.kv_retrieve.offset = cpu_to_le32(cmd.cdw5);
			/* validate key length */
            if (cmd.key_length >  KVCMD_MAX_KEY_SIZE ||
                cmd.key_length < KVCMD_MIN_KEY_SIZE) {
				cmd.result = KVS_ERR_VALUE;
				status = -EINVAL;
				goto exit;
            }
			c.kv_retrieve.key_len = cpu_to_le32(cmd.key_length -1); /* key len - 1 */
            c.kv_retrieve.option = (option & 0xff);
			c.kv_retrieve.value_len = cpu_to_le32((cmd.data_length >> 2));

			if (cmd.key_length > KVCMD_INLINE_KEY_MAX) {
				metadata = (void __user*)cmd.key_addr;
				meta_len = cmd.key_length;
			} else {
				memcpy(c.kv_retrieve.key, cmd.key, cmd.key_length);
			}
#if 0
            if (aio && offset_in_page(cmd.data_addr)) {
                /* aio does not support unaligned memory*/
				status = -EINVAL;
				goto exit;
            }
#endif
		break;
		case nvme_cmd_kv_delete:
            option = cpu_to_le32(cmd.cdw4);
            if (cmd.key_length >  KVCMD_MAX_KEY_SIZE ||
                cmd.key_length < KVCMD_MIN_KEY_SIZE) {
				cmd.result = KVS_ERR_VALUE;
				status = -EINVAL;
				goto exit;
            }
			c.kv_delete.key_len = cpu_to_le32(cmd.key_length -1);
		    c.kv_delete.option = (option & 0xff);	
			if (cmd.key_length > KVCMD_INLINE_KEY_MAX) {
				metadata = (void __user *)cmd.key_addr;
				meta_len = cmd.key_length;
			} else {
				memcpy(c.kv_delete.key, cmd.key, cmd.key_length);
			}
			break;
		case nvme_cmd_kv_exist:
            option = cpu_to_le32(cmd.cdw4);
            if (cmd.key_length >  KVCMD_MAX_KEY_SIZE ||
                cmd.key_length < KVCMD_MIN_KEY_SIZE) {
				cmd.result = KVS_ERR_VALUE;
				status = -EINVAL;
				goto exit;
            }
			c.kv_exist.key_len = cpu_to_le32(cmd.key_length -1);
		    c.kv_exist.option = (option & 0xff);	
			if (cmd.key_length > KVCMD_INLINE_KEY_MAX) {
				metadata = (void __user *)cmd.key_addr;
				meta_len = cmd.key_length;
			} else {
				memcpy(c.kv_exist.key, cmd.key, cmd.key_length);
			}
			break;

		case nvme_cmd_kv_iter_req:
            option = cpu_to_le32(cmd.cdw4);
            iter_handle = cpu_to_le32(cmd.cdw5);
            c.kv_iter_req.iter_handle = iter_handle & 0xff;
            c.kv_iter_req.option = option & 0xff;
            c.kv_iter_req.iter_val = cpu_to_le32(cmd.cdw12);
            c.kv_iter_req.iter_bitmask = cpu_to_le32(cmd.cdw13);
			break;
		case nvme_cmd_kv_iter_read:
            option = cpu_to_le32(cmd.cdw4);
            iter_handle = cpu_to_le32(cmd.cdw5);
            c.kv_iter_read.iter_handle = iter_handle & 0xff;
            c.kv_iter_read.option = option & 0xff;
			c.kv_iter_read.value_len = cpu_to_le32((cmd.data_length >> 2));
			break;
		default:
			cmd.result = KVS_ERR_IO;
			status = -EINVAL;
			goto exit;
	}

//    if (cmd.data_addr) {
//        u32 *c_data = c.common.cdw2;
//		pr_err("nvme_user_kv_cmd: kv cmd opcode(%02x) info: key_length(%d) cdw11(%d) addr(%p) data_len(%d) cdw10(%d) data(%p) cdw5(%d) cdw6(%d) cdw7(%d) cdw8(%d) cdw9(%d) cdw10(%d) cdw11(%d) cdw12(%d) cdw13(%d) cdw14(%d) cdw15(%d).\n",
//				cmd.opcode, cmd.key_length, cmd.cdw11, (void *)cmd.key_addr, cmd.data_length, cmd.cdw10, (void *)cmd.data_addr,
//                c_data[3], c_data[4], c_data[5], c_data[6], c_data[7], c_data[8], c_data[9], c_data[10], c_data[11], c_data[12], c_data[13]);
//    }
	status = __nvme_submit_kv_user_cmd(ns ? ns->queue : ctrl->admin_q, &c, &cmd,
					(void __user *)(uintptr_t)cmd.data_addr, cmd.data_length, metadata, meta_len, 0,
					&cmd.result, &cmd.status, timeout, aio);
exit:
	if (!aio) {
		if (put_user(cmd.result, &ucmd->result))
			return -EFAULT;
		if (put_user(cmd.status, &ucmd->status))
			return -EFAULT;
	}
	return status;

}

static int nvme_user_kv_aio_cmd(struct nvme_ctrl *ctrl, struct nvme_ns *ns,
			struct nvme_passthru_kv_cmd __user *ucmd)
{
	return nvme_user_kv_cmd(ctrl, ns, ucmd, true);
}

#endif

int __nvme_submit_user_cmd(struct request_queue *q, struct nvme_command *cmd,
		void __user *ubuffer, unsigned bufflen,
		void __user *meta_buffer, unsigned meta_len, u32 meta_seed,
		u32 *result, unsigned timeout)
{
	bool write = nvme_is_write(cmd);
	struct nvme_completion cqe;
	struct nvme_ns *ns = q->queuedata;
	struct gendisk *disk = ns ? ns->disk : NULL;
	struct request *req;
	struct bio *bio = NULL;
	void *meta = NULL;
	int ret;

	req = nvme_alloc_request(q, cmd, 0, NVME_QID_ANY);
	if (IS_ERR(req))
		return PTR_ERR(req);

	req->timeout = timeout ? timeout : ADMIN_TIMEOUT;
	req->special = &cqe;

	if (ubuffer && bufflen) {
		ret = blk_rq_map_user(q, req, NULL, ubuffer, bufflen,
				GFP_KERNEL);
		if (ret)
			goto out;
		bio = req->bio;

		if (!disk)
			goto submit;
		bio->bi_bdev = bdget_disk(disk, 0);
		if (!bio->bi_bdev) {
			ret = -ENODEV;
			goto out_unmap;
		}

		if (meta_buffer) {
			struct bio_integrity_payload *bip;

			meta = kmalloc(meta_len, GFP_KERNEL);
			if (!meta) {
				ret = -ENOMEM;
				goto out_unmap;
			}

			if (write) {
				if (copy_from_user(meta, meta_buffer,
						meta_len)) {
					ret = -EFAULT;
					goto out_free_meta;
				}
			}

			bip = bio_integrity_alloc(bio, GFP_KERNEL, 1);
			if (IS_ERR(bip)) {
				ret = PTR_ERR(bip);
				goto out_free_meta;
			}

			bip->bip_iter.bi_size = meta_len;
			bip->bip_iter.bi_sector = meta_seed;

			ret = bio_integrity_add_page(bio, virt_to_page(meta),
					meta_len, offset_in_page(meta));
			if (ret != meta_len) {
				ret = -ENOMEM;
				goto out_free_meta;
			}
		}
	}
 submit:
	blk_execute_rq(req->q, disk, req, 0);
	ret = req->errors;
	if (result)
		*result = le32_to_cpu(cqe.result);
	if (meta && !ret && !write) {
		if (copy_to_user(meta_buffer, meta, meta_len))
			ret = -EFAULT;
	}
 out_free_meta:
	kfree(meta);
 out_unmap:
	if (bio) {
		if (disk && bio->bi_bdev)
			bdput(bio->bi_bdev);
		blk_rq_unmap_user(bio);
	}
 out:
	blk_mq_free_request(req);
	return ret;
}

int nvme_submit_user_cmd(struct request_queue *q, struct nvme_command *cmd,
		void __user *ubuffer, unsigned bufflen, u32 *result,
		unsigned timeout)
{
	return __nvme_submit_user_cmd(q, cmd, ubuffer, bufflen, NULL, 0, 0,
			result, timeout);
}

int nvme_identify_ctrl(struct nvme_ctrl *dev, struct nvme_id_ctrl **id)
{
	struct nvme_command c = { };
	int error;

	/* gcc-4.4.4 (at least) has issues with initializers and anon unions */
	c.identify.opcode = nvme_admin_identify;
	c.identify.cns = cpu_to_le32(1);

	*id = kmalloc(sizeof(struct nvme_id_ctrl), GFP_KERNEL);
	if (!*id)
		return -ENOMEM;

	error = nvme_submit_sync_cmd(dev->admin_q, &c, *id,
			sizeof(struct nvme_id_ctrl));
	if (error)
		kfree(*id);
	return error;
}

static int nvme_identify_ns_list(struct nvme_ctrl *dev, unsigned nsid, __le32 *ns_list)
{
	struct nvme_command c = { };

	c.identify.opcode = nvme_admin_identify;
	c.identify.cns = cpu_to_le32(2);
	c.identify.nsid = cpu_to_le32(nsid);
	return nvme_submit_sync_cmd(dev->admin_q, &c, ns_list, 0x1000);
}

int nvme_identify_ns(struct nvme_ctrl *dev, unsigned nsid,
		struct nvme_id_ns **id)
{
	struct nvme_command c = { };
	int error;

	/* gcc-4.4.4 (at least) has issues with initializers and anon unions */
	c.identify.opcode = nvme_admin_identify,
	c.identify.nsid = cpu_to_le32(nsid),

	*id = kmalloc(sizeof(struct nvme_id_ns), GFP_KERNEL);
	if (!*id)
		return -ENOMEM;

	error = nvme_submit_sync_cmd(dev->admin_q, &c, *id,
			sizeof(struct nvme_id_ns));
	if (error)
		kfree(*id);
	return error;
}

int nvme_get_features(struct nvme_ctrl *dev, unsigned fid, unsigned nsid,
		      void *buffer, size_t buflen, u32 *result)
{
	struct nvme_command c;
	struct nvme_completion cqe;
	int ret;

	memset(&c, 0, sizeof(c));
	c.features.opcode = nvme_admin_get_features;
	c.features.nsid = cpu_to_le32(nsid);
	c.features.fid = cpu_to_le32(fid);

	ret = __nvme_submit_sync_cmd(dev->admin_q, &c, &cqe, buffer, buflen, 0,
			NVME_QID_ANY, 0, 0);
	if (ret >= 0 && result)
		*result = le32_to_cpu(cqe.result);
	return ret;
}

int nvme_set_features(struct nvme_ctrl *dev, unsigned fid, unsigned dword11,
		      void *buffer, size_t buflen, u32 *result)
{
	struct nvme_command c;
	struct nvme_completion cqe;
	int ret;

	memset(&c, 0, sizeof(c));
	c.features.opcode = nvme_admin_set_features;
	c.features.fid = cpu_to_le32(fid);
	c.features.dword11 = cpu_to_le32(dword11);

	ret = __nvme_submit_sync_cmd(dev->admin_q, &c, &cqe,
			buffer, buflen, 0, NVME_QID_ANY, 0, 0);
	if (ret >= 0 && result)
		*result = le32_to_cpu(cqe.result);
	return ret;
}

int nvme_get_log_page(struct nvme_ctrl *dev, struct nvme_smart_log **log)
{
	struct nvme_command c = { };
	int error;

	c.common.opcode = nvme_admin_get_log_page,
	c.common.nsid = cpu_to_le32(0xFFFFFFFF),
	c.common.cdw10[0] = cpu_to_le32(
			(((sizeof(struct nvme_smart_log) / 4) - 1) << 16) |
			 NVME_LOG_SMART),

	*log = kmalloc(sizeof(struct nvme_smart_log), GFP_KERNEL);
	if (!*log)
		return -ENOMEM;

	error = nvme_submit_sync_cmd(dev->admin_q, &c, *log,
			sizeof(struct nvme_smart_log));
	if (error)
		kfree(*log);
	return error;
}

int nvme_set_queue_count(struct nvme_ctrl *ctrl, int *count)
{
	u32 q_count = (*count - 1) | ((*count - 1) << 16);
	u32 result;
	int status, nr_io_queues;

	status = nvme_set_features(ctrl, NVME_FEAT_NUM_QUEUES, q_count, NULL, 0,
			&result);
	if (status)
		return status;

	nr_io_queues = min(result & 0xffff, result >> 16) + 1;
	*count = min(*count, nr_io_queues);
	return 0;
}

static int nvme_submit_io(struct nvme_ns *ns, struct nvme_user_io __user *uio)
{
	struct nvme_user_io io;
	struct nvme_command c;
	unsigned length, meta_len;
	void __user *metadata;

	if (copy_from_user(&io, uio, sizeof(io)))
		return -EFAULT;

	switch (io.opcode) {
	case nvme_cmd_write:
	case nvme_cmd_read:
	case nvme_cmd_compare:
		break;
	default:
		return -EINVAL;
	}

	length = (io.nblocks + 1) << ns->lba_shift;
	meta_len = (io.nblocks + 1) * ns->ms;
	metadata = (void __user *)(uintptr_t)io.metadata;

	if (ns->ext) {
		length += meta_len;
		meta_len = 0;
	} else if (meta_len) {
		if ((io.metadata & 3) || !io.metadata)
			return -EINVAL;
	}

	memset(&c, 0, sizeof(c));
	c.rw.opcode = io.opcode;
	c.rw.flags = io.flags;
	c.rw.nsid = cpu_to_le32(ns->ns_id);
	c.rw.slba = cpu_to_le64(io.slba);
	c.rw.length = cpu_to_le16(io.nblocks);
	c.rw.control = cpu_to_le16(io.control);
	c.rw.dsmgmt = cpu_to_le32(io.dsmgmt);
	c.rw.reftag = cpu_to_le32(io.reftag);
	c.rw.apptag = cpu_to_le16(io.apptag);
	c.rw.appmask = cpu_to_le16(io.appmask);

	return __nvme_submit_user_cmd(ns->queue, &c,
			(void __user *)(uintptr_t)io.addr, length,
			metadata, meta_len, io.slba, NULL, 0);
}

static int nvme_user_cmd(struct nvme_ctrl *ctrl, struct nvme_ns *ns,
			struct nvme_passthru_cmd __user *ucmd)
{
	struct nvme_passthru_cmd cmd;
	struct nvme_command c;
	unsigned timeout = 0;
	int status;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	if (copy_from_user(&cmd, ucmd, sizeof(cmd)))
		return -EFAULT;

    memset(&c, 0, sizeof(c));
	c.common.opcode = cmd.opcode;
	c.common.flags = cmd.flags;
	c.common.nsid = cpu_to_le32(cmd.nsid);
	c.common.cdw2[0] = cpu_to_le32(cmd.cdw2);
	c.common.cdw2[1] = cpu_to_le32(cmd.cdw3);
	c.common.cdw10[0] = cpu_to_le32(cmd.cdw10);
	c.common.cdw10[1] = cpu_to_le32(cmd.cdw11);
	c.common.cdw10[2] = cpu_to_le32(cmd.cdw12);
	c.common.cdw10[3] = cpu_to_le32(cmd.cdw13);
	c.common.cdw10[4] = cpu_to_le32(cmd.cdw14);
	c.common.cdw10[5] = cpu_to_le32(cmd.cdw15);

    if (cmd.timeout_ms)
        timeout = msecs_to_jiffies(cmd.timeout_ms);

#if 1
    if (ns == NULL && cmd.opcode == nvme_admin_format_nvm)
        timeout = (120 * HZ);
#endif
	status = nvme_submit_user_cmd(ns ? ns->queue : ctrl->admin_q, &c,
			(void __user *)(uintptr_t)cmd.addr, cmd.data_len,
			&cmd.result, timeout);
	if (status >= 0) {
		if (put_user(cmd.result, &ucmd->result))
			return -EFAULT;
	}

	return status;
}

static int nvme_ioctl(struct block_device *bdev, fmode_t mode,
		unsigned int cmd, unsigned long arg)
{
	struct nvme_ns *ns = bdev->bd_disk->private_data;

	switch (cmd) {
	case NVME_IOCTL_ID:
		force_successful_syscall_return();
		return ns->ns_id;
	case NVME_IOCTL_ADMIN_CMD:
		return nvme_user_cmd(ns->ctrl, NULL, (void __user *)arg);
	case NVME_IOCTL_IO_CMD:
		return nvme_user_cmd(ns->ctrl, ns, (void __user *)arg);
	case NVME_IOCTL_SUBMIT_IO:
		return nvme_submit_io(ns, (void __user *)arg);
#if 1
	case NVME_IOCTL_IO_KV_CMD:
		return nvme_user_kv_cmd(ns->ctrl, ns, (void __user *)arg, false);
	case NVME_IOCTL_AIO_CMD:
		return nvme_user_kv_aio_cmd(ns->ctrl, ns, (void __user *)arg);
	case NVME_IOCTL_SET_AIOCTX:
		return nvme_set_aioctx((void __user*)arg);
	case NVME_IOCTL_DEL_AIOCTX:
		return nvme_del_aioctx((void __user*)arg);
	case NVME_IOCTL_GET_AIOEVENT:
		return nvme_get_ioevents((void __user*)arg);
#endif
#ifdef CONFIG_BLK_DEV_NVME_SCSI
	case SG_GET_VERSION_NUM:
		return nvme_sg_get_version_num((void __user *)arg);
	case SG_IO:
		return nvme_sg_io(ns, (void __user *)arg);
#endif
	default:
		return -ENOTTY;
	}
}

#ifdef CONFIG_COMPAT
static int nvme_compat_ioctl(struct block_device *bdev, fmode_t mode,
			unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case SG_IO:
		return -ENOIOCTLCMD;
	}
	return nvme_ioctl(bdev, mode, cmd, arg);
}
#else
#define nvme_compat_ioctl	NULL
#endif

static int nvme_open(struct block_device *bdev, fmode_t mode)
{
	return nvme_get_ns_from_disk(bdev->bd_disk) ? 0 : -ENXIO;
}

static void nvme_release(struct gendisk *disk, fmode_t mode)
{
	nvme_put_ns(disk->private_data);
}

static int nvme_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	/* some standard values */
	geo->heads = 1 << 6;
	geo->sectors = 1 << 5;
	geo->cylinders = get_capacity(bdev->bd_disk) >> 11;
	return 0;
}

#ifdef CONFIG_BLK_DEV_INTEGRITY
static void nvme_init_integrity(struct nvme_ns *ns)
{
	struct blk_integrity integrity;

	switch (ns->pi_type) {
	case NVME_NS_DPS_PI_TYPE3:
		integrity.profile = &t10_pi_type3_crc;
		break;
	case NVME_NS_DPS_PI_TYPE1:
	case NVME_NS_DPS_PI_TYPE2:
		integrity.profile = &t10_pi_type1_crc;
		break;
	default:
		integrity.profile = NULL;
		break;
	}
	integrity.tuple_size = ns->ms;
	blk_integrity_register(ns->disk, &integrity);
	blk_queue_max_integrity_segments(ns->queue, 1);
}
#else
static void nvme_init_integrity(struct nvme_ns *ns)
{
}
#endif /* CONFIG_BLK_DEV_INTEGRITY */

static void nvme_config_discard(struct nvme_ns *ns)
{
	struct nvme_ctrl *ctrl = ns->ctrl;
	u32 logical_block_size = queue_logical_block_size(ns->queue);

	if (ctrl->quirks & NVME_QUIRK_DISCARD_ZEROES)
		ns->queue->limits.discard_zeroes_data = 1;
	else
		ns->queue->limits.discard_zeroes_data = 0;

	ns->queue->limits.discard_alignment = logical_block_size;
	ns->queue->limits.discard_granularity = logical_block_size;
	blk_queue_max_discard_sectors(ns->queue, 0xffffffff);
	queue_flag_set_unlocked(QUEUE_FLAG_DISCARD, ns->queue);
}

static int nvme_revalidate_disk(struct gendisk *disk)
{
	struct nvme_ns *ns = disk->private_data;
	struct nvme_id_ns *id;
	u8 lbaf, pi_type;
	u16 old_ms;
	unsigned short bs;

	if (test_bit(NVME_NS_DEAD, &ns->flags)) {
		set_capacity(disk, 0);
		return -ENODEV;
	}
	if (nvme_identify_ns(ns->ctrl, ns->ns_id, &id)) {
		dev_warn(ns->ctrl->dev, "%s: Identify failure nvme%dn%d\n",
				__func__, ns->ctrl->instance, ns->ns_id);
		return -ENODEV;
	}
	if (id->ncap == 0) {
		kfree(id);
		return -ENODEV;
	}

	if (nvme_nvm_ns_supported(ns, id) && ns->type != NVME_NS_LIGHTNVM) {
		if (nvme_nvm_register(ns->queue, disk->disk_name)) {
			dev_warn(ns->ctrl->dev,
				"%s: LightNVM init failure\n", __func__);
			kfree(id);
			return -ENODEV;
		}
		ns->type = NVME_NS_LIGHTNVM;
	}

	if (ns->ctrl->vs >= NVME_VS(1, 1))
		memcpy(ns->eui, id->eui64, sizeof(ns->eui));
	if (ns->ctrl->vs >= NVME_VS(1, 2))
		memcpy(ns->uuid, id->nguid, sizeof(ns->uuid));

	old_ms = ns->ms;
	lbaf = id->flbas & NVME_NS_FLBAS_LBA_MASK;
	ns->lba_shift = id->lbaf[lbaf].ds;
	ns->ms = le16_to_cpu(id->lbaf[lbaf].ms);
	ns->ext = ns->ms && (id->flbas & NVME_NS_FLBAS_META_EXT);

	/*
	 * If identify namespace failed, use default 512 byte block size so
	 * block layer can use before failing read/write for 0 capacity.
	 */
	if (ns->lba_shift == 0)
		ns->lba_shift = 9;
	bs = 1 << ns->lba_shift;
	/* XXX: PI implementation requires metadata equal t10 pi tuple size */
	pi_type = ns->ms == sizeof(struct t10_pi_tuple) ?
					id->dps & NVME_NS_DPS_PI_MASK : 0;

	blk_mq_freeze_queue(disk->queue);
	if (blk_get_integrity(disk) && (ns->pi_type != pi_type ||
				ns->ms != old_ms ||
				bs != queue_logical_block_size(disk->queue) ||
				(ns->ms && ns->ext)))
		blk_integrity_unregister(disk);

	ns->pi_type = pi_type;
	blk_queue_logical_block_size(ns->queue, bs);

	if (ns->ms && !blk_get_integrity(disk) && !ns->ext)
		nvme_init_integrity(ns);
	if (ns->ms && !(ns->ms == 8 && ns->pi_type) && !blk_get_integrity(disk))
		set_capacity(disk, 0);
	else
		set_capacity(disk, le64_to_cpup(&id->nsze) << (ns->lba_shift - 9));

	if (ns->ctrl->oncs & NVME_CTRL_ONCS_DSM)
		nvme_config_discard(ns);
	blk_mq_unfreeze_queue(disk->queue);

	kfree(id);
	return 0;
}

static char nvme_pr_type(enum pr_type type)
{
	switch (type) {
	case PR_WRITE_EXCLUSIVE:
		return 1;
	case PR_EXCLUSIVE_ACCESS:
		return 2;
	case PR_WRITE_EXCLUSIVE_REG_ONLY:
		return 3;
	case PR_EXCLUSIVE_ACCESS_REG_ONLY:
		return 4;
	case PR_WRITE_EXCLUSIVE_ALL_REGS:
		return 5;
	case PR_EXCLUSIVE_ACCESS_ALL_REGS:
		return 6;
	default:
		return 0;
	}
};

static int nvme_pr_command(struct block_device *bdev, u32 cdw10,
				u64 key, u64 sa_key, u8 op)
{
	struct nvme_ns *ns = bdev->bd_disk->private_data;
	struct nvme_command c;
	u8 data[16] = { 0, };

	put_unaligned_le64(key, &data[0]);
	put_unaligned_le64(sa_key, &data[8]);

	memset(&c, 0, sizeof(c));
	c.common.opcode = op;
	c.common.nsid = cpu_to_le32(ns->ns_id);
	c.common.cdw10[0] = cpu_to_le32(cdw10);

	return nvme_submit_sync_cmd(ns->queue, &c, data, 16);
}

static int nvme_pr_register(struct block_device *bdev, u64 old,
		u64 new, unsigned flags)
{
	u32 cdw10;

	if (flags & ~PR_FL_IGNORE_KEY)
		return -EOPNOTSUPP;

	cdw10 = old ? 2 : 0;
	cdw10 |= (flags & PR_FL_IGNORE_KEY) ? 1 << 3 : 0;
	cdw10 |= (1 << 30) | (1 << 31); /* PTPL=1 */
	return nvme_pr_command(bdev, cdw10, old, new, nvme_cmd_resv_register);
}

static int nvme_pr_reserve(struct block_device *bdev, u64 key,
		enum pr_type type, unsigned flags)
{
	u32 cdw10;

	if (flags & ~PR_FL_IGNORE_KEY)
		return -EOPNOTSUPP;

	cdw10 = nvme_pr_type(type) << 8;
	cdw10 |= ((flags & PR_FL_IGNORE_KEY) ? 1 << 3 : 0);
	return nvme_pr_command(bdev, cdw10, key, 0, nvme_cmd_resv_acquire);
}

static int nvme_pr_preempt(struct block_device *bdev, u64 old, u64 new,
		enum pr_type type, bool abort)
{
	u32 cdw10 = nvme_pr_type(type) << 8 | abort ? 2 : 1;
	return nvme_pr_command(bdev, cdw10, old, new, nvme_cmd_resv_acquire);
}

static int nvme_pr_clear(struct block_device *bdev, u64 key)
{
	u32 cdw10 = 1 | (key ? 1 << 3 : 0);
	return nvme_pr_command(bdev, cdw10, key, 0, nvme_cmd_resv_register);
}

static int nvme_pr_release(struct block_device *bdev, u64 key, enum pr_type type)
{
	u32 cdw10 = nvme_pr_type(type) << 8 | key ? 1 << 3 : 0;
	return nvme_pr_command(bdev, cdw10, key, 0, nvme_cmd_resv_release);
}

static const struct pr_ops nvme_pr_ops = {
	.pr_register	= nvme_pr_register,
	.pr_reserve	= nvme_pr_reserve,
	.pr_release	= nvme_pr_release,
	.pr_preempt	= nvme_pr_preempt,
	.pr_clear	= nvme_pr_clear,
};

static const struct block_device_operations nvme_fops = {
	.owner		= THIS_MODULE,
	.ioctl		= nvme_ioctl,
	.compat_ioctl	= nvme_compat_ioctl,
	.open		= nvme_open,
	.release	= nvme_release,
	.getgeo		= nvme_getgeo,
	.revalidate_disk= nvme_revalidate_disk,
	.pr_ops		= &nvme_pr_ops,
};

static int nvme_wait_ready(struct nvme_ctrl *ctrl, u64 cap, bool enabled)
{
	unsigned long timeout =
		((NVME_CAP_TIMEOUT(cap) + 1) * HZ / 2) + jiffies;
	u32 csts, bit = enabled ? NVME_CSTS_RDY : 0;
	int ret;

	while ((ret = ctrl->ops->reg_read32(ctrl, NVME_REG_CSTS, &csts)) == 0) {
		if ((csts & NVME_CSTS_RDY) == bit)
			break;

		msleep(100);
		if (fatal_signal_pending(current))
			return -EINTR;
		if (time_after(jiffies, timeout)) {
			dev_err(ctrl->dev,
				"Device not ready; aborting %s\n", enabled ?
						"initialisation" : "reset");
			return -ENODEV;
		}
	}

	return ret;
}

/*
 * If the device has been passed off to us in an enabled state, just clear
 * the enabled bit.  The spec says we should set the 'shutdown notification
 * bits', but doing so may cause the device to complete commands to the
 * admin queue ... and we don't know what memory that might be pointing at!
 */
int nvme_disable_ctrl(struct nvme_ctrl *ctrl, u64 cap)
{
	int ret;

	ctrl->ctrl_config &= ~NVME_CC_SHN_MASK;
	ctrl->ctrl_config &= ~NVME_CC_ENABLE;

	ret = ctrl->ops->reg_write32(ctrl, NVME_REG_CC, ctrl->ctrl_config);
	if (ret)
		return ret;

	if (ctrl->quirks & NVME_QUIRK_DELAY_BEFORE_CHK_RDY)
		msleep(NVME_QUIRK_DELAY_AMOUNT);

	return nvme_wait_ready(ctrl, cap, false);
}

int nvme_enable_ctrl(struct nvme_ctrl *ctrl, u64 cap)
{
	/*
	 * Default to a 4K page size, with the intention to update this
	 * path in the future to accomodate architectures with differing
	 * kernel and IO page sizes.
	 */
	unsigned dev_page_min = NVME_CAP_MPSMIN(cap) + 12, page_shift = 12;
	int ret;

	if (page_shift < dev_page_min) {
		dev_err(ctrl->dev,
			"Minimum device page size %u too large for host (%u)\n",
			1 << dev_page_min, 1 << page_shift);
		return -ENODEV;
	}

	ctrl->page_size = 1 << page_shift;

	ctrl->ctrl_config = NVME_CC_CSS_NVM;
	ctrl->ctrl_config |= (page_shift - 12) << NVME_CC_MPS_SHIFT;
	ctrl->ctrl_config |= NVME_CC_ARB_RR | NVME_CC_SHN_NONE;
	ctrl->ctrl_config |= NVME_CC_IOSQES | NVME_CC_IOCQES;
	ctrl->ctrl_config |= NVME_CC_ENABLE;

	ret = ctrl->ops->reg_write32(ctrl, NVME_REG_CC, ctrl->ctrl_config);
	if (ret)
		return ret;
	return nvme_wait_ready(ctrl, cap, true);
}

int nvme_shutdown_ctrl(struct nvme_ctrl *ctrl)
{
	unsigned long timeout = SHUTDOWN_TIMEOUT + jiffies;
	u32 csts;
	int ret;

	ctrl->ctrl_config &= ~NVME_CC_SHN_MASK;
	ctrl->ctrl_config |= NVME_CC_SHN_NORMAL;

	ret = ctrl->ops->reg_write32(ctrl, NVME_REG_CC, ctrl->ctrl_config);
	if (ret)
		return ret;

	while ((ret = ctrl->ops->reg_read32(ctrl, NVME_REG_CSTS, &csts)) == 0) {
		if ((csts & NVME_CSTS_SHST_MASK) == NVME_CSTS_SHST_CMPLT)
			break;

		msleep(100);
		if (fatal_signal_pending(current))
			return -EINTR;
		if (time_after(jiffies, timeout)) {
			dev_err(ctrl->dev,
				"Device shutdown incomplete; abort shutdown\n");
			return -ENODEV;
		}
	}

	return ret;
}

static void nvme_set_queue_limits(struct nvme_ctrl *ctrl,
		struct request_queue *q)
{
	if (ctrl->max_hw_sectors) {
		u32 max_segments =
			(ctrl->max_hw_sectors / (ctrl->page_size >> 9)) + 1;

		blk_queue_max_hw_sectors(q, ctrl->max_hw_sectors);
		blk_queue_max_segments(q, min_t(u32, max_segments, USHRT_MAX));
	}
	if (ctrl->stripe_size)
		blk_queue_chunk_sectors(q, ctrl->stripe_size >> 9);
	if (ctrl->vwc & NVME_CTRL_VWC_PRESENT)
		blk_queue_flush(q, REQ_FLUSH | REQ_FUA);
	blk_queue_virt_boundary(q, ctrl->page_size - 1);
}

static void nvme_configure_apst(struct nvme_ctrl *ctrl)
{
	/*
	 * APST (Autonomous Power State Transition) lets us program a
	 * table of power state transitions that the controller will
	 * perform automatically.  We configure it with a simple
	 * heuristic: we are willing to spend at most 2% of the time
	 * transitioning between power states.  Therefore, when running
	 * in any given state, we will enter the next lower-power
	 * non-operational state after waiting 50 * (enlat + exlat)
	 * microseconds, as long as that state's exit latency is under
	 * the requested maximum latency.
	 *
	 * We will not autonomously enter any non-operational state for
	 * which the total latency exceeds ps_max_latency_us.  Users
	 * can set ps_max_latency_us to zero to turn off APST.
	 */

	unsigned apste;
	struct nvme_feat_auto_pst *table;
	int ret;

	/*
	 * If APST isn't supported or if we haven't been initialized yet,
	 * then don't do anything.
	 */
	if (!ctrl->apsta)
		return;

	if (ctrl->npss > 31) {
		dev_warn(ctrl->device, "NPSS is invalid; not using APST\n");
		return;
	}

	table = kzalloc(sizeof(*table), GFP_KERNEL);
	if (!table)
		return;

	if (ctrl->ps_max_latency_us == 0) {
		/* Turn off APST. */
		apste = 0;
	} else {
		__le64 target = cpu_to_le64(0);
		int state;

		/*
		 * Walk through all states from lowest- to highest-power.
		 * According to the spec, lower-numbered states use more
		 * power.  NPSS, despite the name, is the index of the
		 * lowest-power state, not the number of states.
		 */
		for (state = (int)ctrl->npss; state >= 0; state--) {
			u64 total_latency_us, exit_latency_us, transition_ms;

			if (target)
				table->entries[state] = target;

			/*
			 * Don't allow transitions to the deepest state
			 * if it's quirked off.
			 */
			if (state == ctrl->npss &&
			    (ctrl->quirks & NVME_QUIRK_NO_DEEPEST_PS))
				continue;

			/*
			 * Is this state a useful non-operational state for
			 * higher-power states to autonomously transition to?
			 */
			if (!(ctrl->psd[state].flags &
			      NVME_PS_FLAGS_NON_OP_STATE))
				continue;

			exit_latency_us =
				(u64)le32_to_cpu(ctrl->psd[state].exit_lat);
			if (exit_latency_us > ctrl->ps_max_latency_us)
				continue;

			total_latency_us =
				exit_latency_us +
				le32_to_cpu(ctrl->psd[state].entry_lat);

			/*
			 * This state is good.  Use it as the APST idle
			 * target for higher power states.
			 */
			transition_ms = total_latency_us + 19;
			do_div(transition_ms, 20);
			if (transition_ms > (1 << 24) - 1)
				transition_ms = (1 << 24) - 1;

			target = cpu_to_le64((state << 3) |
					     (transition_ms << 8));
		}

		apste = 1;
	}

	ret = nvme_set_features(ctrl, NVME_FEAT_AUTO_PST, apste,
				table, sizeof(*table), NULL);
	if (ret)
		dev_err(ctrl->device, "failed to set APST feature (%d)\n", ret);

	kfree(table);
}

static void nvme_set_latency_tolerance(struct device *dev, s32 val)
{
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);
	u64 latency;

	switch (val) {
	case PM_QOS_LATENCY_TOLERANCE_NO_CONSTRAINT:
	case PM_QOS_LATENCY_ANY:
		latency = U64_MAX;
		break;

	default:
		latency = val;
	}

	if (ctrl->ps_max_latency_us != latency) {
		ctrl->ps_max_latency_us = latency;
		nvme_configure_apst(ctrl);
	}
}

struct nvme_core_quirk_entry {
	/*
	 * NVMe model and firmware strings are padded with spaces.  For
	 * simplicity, strings in the quirk table are padded with NULLs
	 * instead.
	 */
	u16 vid;
	const char *mn;
	const char *fr;
	unsigned long quirks;
};

static const struct nvme_core_quirk_entry core_quirks[] = {
	{
		/*
		 * This Toshiba device seems to die using any APST states.  See:
		 * https://bugs.launchpad.net/ubuntu/+source/linux/+bug/1678184/comments/11
		 */
		.vid = 0x1179,
		.mn = "THNSF5256GPUK TOSHIBA",
		.quirks = NVME_QUIRK_NO_APST,
	}
};

/* match is null-terminated but idstr is space-padded. */
static bool string_matches(const char *idstr, const char *match, size_t len)
{
	size_t matchlen;

	if (!match)
		return true;

	matchlen = strlen(match);
	WARN_ON_ONCE(matchlen > len);

	if (memcmp(idstr, match, matchlen))
		return false;

	for (; matchlen < len; matchlen++)
		if (idstr[matchlen] != ' ')
			return false;

	return true;
}

static bool quirk_matches(const struct nvme_id_ctrl *id,
			  const struct nvme_core_quirk_entry *q)
{
	return q->vid == le16_to_cpu(id->vid) &&
		string_matches(id->mn, q->mn, sizeof(id->mn)) &&
		string_matches(id->fr, q->fr, sizeof(id->fr));
}

/*
 * Initialize the cached copies of the Identify data and various controller
 * register in our nvme_ctrl structure.  This should be called as soon as
 * the admin queue is fully up and running.
 */
int nvme_init_identify(struct nvme_ctrl *ctrl)
{
	struct nvme_id_ctrl *id;
	u64 cap;
	int ret, page_shift;
	u8 prev_apsta;

	ret = ctrl->ops->reg_read32(ctrl, NVME_REG_VS, &ctrl->vs);
	if (ret) {
		dev_err(ctrl->dev, "Reading VS failed (%d)\n", ret);
		return ret;
	}

	ret = ctrl->ops->reg_read64(ctrl, NVME_REG_CAP, &cap);
	if (ret) {
		dev_err(ctrl->dev, "Reading CAP failed (%d)\n", ret);
		return ret;
	}
	page_shift = NVME_CAP_MPSMIN(cap) + 12;

	if (ctrl->vs >= NVME_VS(1, 1))
		ctrl->subsystem = NVME_CAP_NSSRC(cap);

	ret = nvme_identify_ctrl(ctrl, &id);
	if (ret) {
		dev_err(ctrl->dev, "Identify Controller failed (%d)\n", ret);
		return -EIO;
	}

	if (!ctrl->identified) {
		/*
		 * Check for quirks.  Quirk can depend on firmware version,
		 * so, in principle, the set of quirks present can change
		 * across a reset.  As a possible future enhancement, we
		 * could re-scan for quirks every time we reinitialize
		 * the device, but we'd have to make sure that the driver
		 * behaves intelligently if the quirks change.
		 */

		int i;

		for (i = 0; i < ARRAY_SIZE(core_quirks); i++) {
			if (quirk_matches(id, &core_quirks[i]))
				ctrl->quirks |= core_quirks[i].quirks;
		}
	}

	ctrl->oncs = le16_to_cpup(&id->oncs);
	atomic_set(&ctrl->abort_limit, id->acl + 1);
	ctrl->vwc = id->vwc;
	memcpy(ctrl->serial, id->sn, sizeof(id->sn));
	memcpy(ctrl->model, id->mn, sizeof(id->mn));
	memcpy(ctrl->firmware_rev, id->fr, sizeof(id->fr));
	if (id->mdts)
		ctrl->max_hw_sectors = 1 << (id->mdts + page_shift - 9);
	else
		ctrl->max_hw_sectors = UINT_MAX;

	if ((ctrl->quirks & NVME_QUIRK_STRIPE_SIZE) && id->vs[3]) {
		unsigned int max_hw_sectors;

		ctrl->stripe_size = 1 << (id->vs[3] + page_shift);
		max_hw_sectors = ctrl->stripe_size >> (page_shift - 9);
		if (ctrl->max_hw_sectors) {
			ctrl->max_hw_sectors = min(max_hw_sectors,
							ctrl->max_hw_sectors);
		} else {
			ctrl->max_hw_sectors = max_hw_sectors;
		}
	}

	nvme_set_queue_limits(ctrl, ctrl->admin_q);

	ctrl->npss = id->npss;
	prev_apsta = ctrl->apsta;
	ctrl->apsta = (ctrl->quirks & NVME_QUIRK_NO_APST) ? 0 : id->apsta;
	memcpy(ctrl->psd, id->psd, sizeof(ctrl->psd));

	kfree(id);

	if (ctrl->apsta && !prev_apsta)
		dev_pm_qos_expose_latency_tolerance(ctrl->device);
	else if (!ctrl->apsta && prev_apsta)
		dev_pm_qos_hide_latency_tolerance(ctrl->device);

	nvme_configure_apst(ctrl);

	ctrl->identified = true;

	return ret;
}

static int nvme_dev_open(struct inode *inode, struct file *file)
{
	struct nvme_ctrl *ctrl;
	int instance = iminor(inode);
	int ret = -ENODEV;

	spin_lock(&dev_list_lock);
	list_for_each_entry(ctrl, &nvme_ctrl_list, node) {
		if (ctrl->instance != instance)
			continue;

		if (!ctrl->admin_q) {
			ret = -EWOULDBLOCK;
			break;
		}
		if (!kref_get_unless_zero(&ctrl->kref))
			break;
		file->private_data = ctrl;
		ret = 0;
		break;
	}
	spin_unlock(&dev_list_lock);

	return ret;
}

static int nvme_dev_release(struct inode *inode, struct file *file)
{
	nvme_put_ctrl(file->private_data);
	return 0;
}

static int nvme_dev_user_cmd(struct nvme_ctrl *ctrl, void __user *argp)
{
	struct nvme_ns *ns;
	int ret;

	mutex_lock(&ctrl->namespaces_mutex);
	if (list_empty(&ctrl->namespaces)) {
		ret = -ENOTTY;
		goto out_unlock;
	}

	ns = list_first_entry(&ctrl->namespaces, struct nvme_ns, list);
	if (ns != list_last_entry(&ctrl->namespaces, struct nvme_ns, list)) {
		dev_warn(ctrl->dev,
			"NVME_IOCTL_IO_CMD not supported when multiple namespaces present!\n");
		ret = -EINVAL;
		goto out_unlock;
	}

	dev_warn(ctrl->dev,
		"using deprecated NVME_IOCTL_IO_CMD ioctl on the char device!\n");
	kref_get(&ns->kref);
	mutex_unlock(&ctrl->namespaces_mutex);

	ret = nvme_user_cmd(ctrl, ns, argp);
	nvme_put_ns(ns);
	return ret;

out_unlock:
	mutex_unlock(&ctrl->namespaces_mutex);
	return ret;
}

static long nvme_dev_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg)
{
	struct nvme_ctrl *ctrl = file->private_data;
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case NVME_IOCTL_ADMIN_CMD:
		return nvme_user_cmd(ctrl, NULL, argp);
	case NVME_IOCTL_IO_CMD:
		return nvme_dev_user_cmd(ctrl, argp);
	case NVME_IOCTL_RESET:
		dev_warn(ctrl->dev, "resetting controller\n");
		return ctrl->ops->reset_ctrl(ctrl);
	case NVME_IOCTL_SUBSYS_RESET:
		return nvme_reset_subsystem(ctrl);
	default:
		return -ENOTTY;
	}
}

static const struct file_operations nvme_dev_fops = {
	.owner		= THIS_MODULE,
	.open		= nvme_dev_open,
	.release	= nvme_dev_release,
	.unlocked_ioctl	= nvme_dev_ioctl,
	.compat_ioctl	= nvme_dev_ioctl,
};

static ssize_t nvme_sysfs_reset(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);
	int ret;

	ret = ctrl->ops->reset_ctrl(ctrl);
	if (ret < 0)
		return ret;
	return count;
}
static DEVICE_ATTR(reset_controller, S_IWUSR, NULL, nvme_sysfs_reset);

static ssize_t uuid_show(struct device *dev, struct device_attribute *attr,
								char *buf)
{
	struct nvme_ns *ns = dev_to_disk(dev)->private_data;
	return sprintf(buf, "%pU\n", ns->uuid);
}
static DEVICE_ATTR(uuid, S_IRUGO, uuid_show, NULL);

static ssize_t eui_show(struct device *dev, struct device_attribute *attr,
								char *buf)
{
	struct nvme_ns *ns = dev_to_disk(dev)->private_data;
	return sprintf(buf, "%8phd\n", ns->eui);
}
static DEVICE_ATTR(eui, S_IRUGO, eui_show, NULL);

static ssize_t nsid_show(struct device *dev, struct device_attribute *attr,
								char *buf)
{
	struct nvme_ns *ns = dev_to_disk(dev)->private_data;
	return sprintf(buf, "%d\n", ns->ns_id);
}
static DEVICE_ATTR(nsid, S_IRUGO, nsid_show, NULL);

static struct attribute *nvme_ns_attrs[] = {
	&dev_attr_uuid.attr,
	&dev_attr_eui.attr,
	&dev_attr_nsid.attr,
	NULL,
};

static umode_t nvme_attrs_are_visible(struct kobject *kobj,
		struct attribute *a, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct nvme_ns *ns = dev_to_disk(dev)->private_data;

	if (a == &dev_attr_uuid.attr) {
		if (!memchr_inv(ns->uuid, 0, sizeof(ns->uuid)))
			return 0;
	}
	if (a == &dev_attr_eui.attr) {
		if (!memchr_inv(ns->eui, 0, sizeof(ns->eui)))
			return 0;
	}
	return a->mode;
}

static const struct attribute_group nvme_ns_attr_group = {
	.attrs		= nvme_ns_attrs,
	.is_visible	= nvme_attrs_are_visible,
};

#define nvme_show_function(field)						\
static ssize_t  field##_show(struct device *dev,				\
			    struct device_attribute *attr, char *buf)		\
{										\
        struct nvme_ctrl *ctrl = dev_get_drvdata(dev);				\
        return sprintf(buf, "%.*s\n", (int)sizeof(ctrl->field), ctrl->field);	\
}										\
static DEVICE_ATTR(field, S_IRUGO, field##_show, NULL);

nvme_show_function(model);
nvme_show_function(serial);
nvme_show_function(firmware_rev);

static struct attribute *nvme_dev_attrs[] = {
	&dev_attr_reset_controller.attr,
	&dev_attr_model.attr,
	&dev_attr_serial.attr,
	&dev_attr_firmware_rev.attr,
	NULL
};

static struct attribute_group nvme_dev_attrs_group = {
	.attrs = nvme_dev_attrs,
};

static const struct attribute_group *nvme_dev_attr_groups[] = {
	&nvme_dev_attrs_group,
	NULL,
};

static int ns_cmp(void *priv, struct list_head *a, struct list_head *b)
{
	struct nvme_ns *nsa = container_of(a, struct nvme_ns, list);
	struct nvme_ns *nsb = container_of(b, struct nvme_ns, list);

	return nsa->ns_id - nsb->ns_id;
}

static struct nvme_ns *nvme_find_ns(struct nvme_ctrl *ctrl, unsigned nsid)
{
	struct nvme_ns *ns;

	lockdep_assert_held(&ctrl->namespaces_mutex);

	list_for_each_entry(ns, &ctrl->namespaces, list) {
		if (ns->ns_id == nsid)
			return ns;
		if (ns->ns_id > nsid)
			break;
	}
	return NULL;
}

static void nvme_alloc_ns(struct nvme_ctrl *ctrl, unsigned nsid)
{
	struct nvme_ns *ns;
	struct gendisk *disk;
	int node = dev_to_node(ctrl->dev);

	lockdep_assert_held(&ctrl->namespaces_mutex);

	ns = kzalloc_node(sizeof(*ns), GFP_KERNEL, node);
	if (!ns)
		return;

	ns->queue = blk_mq_init_queue(ctrl->tagset);
	if (IS_ERR(ns->queue))
		goto out_free_ns;
	queue_flag_set_unlocked(QUEUE_FLAG_NOMERGES, ns->queue);
	queue_flag_set_unlocked(QUEUE_FLAG_NONROT, ns->queue);
	ns->queue->queuedata = ns;
	ns->ctrl = ctrl;

	disk = alloc_disk_node(0, node);
	if (!disk)
		goto out_free_queue;

	kref_init(&ns->kref);
	ns->ns_id = nsid;
	ns->disk = disk;
	ns->lba_shift = 9; /* set to a default value for 512 until disk is validated */


	blk_queue_logical_block_size(ns->queue, 1 << ns->lba_shift);
	nvme_set_queue_limits(ctrl, ns->queue);

	disk->major = nvme_major;
	disk->first_minor = 0;
	disk->fops = &nvme_fops;
	disk->private_data = ns;
	disk->queue = ns->queue;
	disk->driverfs_dev = ctrl->device;
	disk->flags = GENHD_FL_EXT_DEVT;
	sprintf(disk->disk_name, "nvme%dn%d", ctrl->instance, nsid);

	if (nvme_revalidate_disk(ns->disk))
		goto out_free_disk;

	list_add_tail(&ns->list, &ctrl->namespaces);
	kref_get(&ctrl->kref);
	if (ns->type == NVME_NS_LIGHTNVM)
		return;

	add_disk(ns->disk);
	if (sysfs_create_group(&disk_to_dev(ns->disk)->kobj,
					&nvme_ns_attr_group))
		pr_warn("%s: failed to create sysfs group for identification\n",
			ns->disk->disk_name);
	return;
 out_free_disk:
	kfree(disk);
 out_free_queue:
	blk_cleanup_queue(ns->queue);
 out_free_ns:
	kfree(ns);
}

static void nvme_ns_remove(struct nvme_ns *ns)
{
	if (test_and_set_bit(NVME_NS_REMOVING, &ns->flags))
		return;

	if (ns->disk->flags & GENHD_FL_UP) {
		if (blk_get_integrity(ns->disk))
			blk_integrity_unregister(ns->disk);
		sysfs_remove_group(&disk_to_dev(ns->disk)->kobj,
					&nvme_ns_attr_group);
		del_gendisk(ns->disk);
		blk_mq_abort_requeue_list(ns->queue);
		blk_cleanup_queue(ns->queue);
	}
	mutex_lock(&ns->ctrl->namespaces_mutex);
	list_del_init(&ns->list);
	mutex_unlock(&ns->ctrl->namespaces_mutex);
	nvme_put_ns(ns);
}

static void nvme_validate_ns(struct nvme_ctrl *ctrl, unsigned nsid)
{
	struct nvme_ns *ns;

	ns = nvme_find_ns(ctrl, nsid);
	if (ns) {
		if (revalidate_disk(ns->disk))
			nvme_ns_remove(ns);
	} else
		nvme_alloc_ns(ctrl, nsid);
}

static int nvme_scan_ns_list(struct nvme_ctrl *ctrl, unsigned nn)
{
	struct nvme_ns *ns;
	__le32 *ns_list;
	unsigned i, j, nsid, prev = 0, num_lists = DIV_ROUND_UP(nn, 1024);
	int ret = 0;

	ns_list = kzalloc(0x1000, GFP_KERNEL);
	if (!ns_list)
		return -ENOMEM;

	for (i = 0; i < num_lists; i++) {
		ret = nvme_identify_ns_list(ctrl, prev, ns_list);
		if (ret)
			goto out;

		for (j = 0; j < min(nn, 1024U); j++) {
			nsid = le32_to_cpu(ns_list[j]);
			if (!nsid)
				goto out;

			nvme_validate_ns(ctrl, nsid);

			while (++prev < nsid) {
				ns = nvme_find_ns(ctrl, prev);
				if (ns)
					nvme_ns_remove(ns);
			}
		}
		nn -= j;
	}
 out:
	kfree(ns_list);
	return ret;
}

static void __nvme_scan_namespaces(struct nvme_ctrl *ctrl, unsigned nn)
{
	struct nvme_ns *ns, *next;
	unsigned i;

	lockdep_assert_held(&ctrl->namespaces_mutex);

	for (i = 1; i <= nn; i++)
		nvme_validate_ns(ctrl, i);

	list_for_each_entry_safe(ns, next, &ctrl->namespaces, list) {
		if (ns->ns_id > nn)
			nvme_ns_remove(ns);
	}
}

void nvme_scan_namespaces(struct nvme_ctrl *ctrl)
{
	struct nvme_id_ctrl *id;
	unsigned nn;

	if (nvme_identify_ctrl(ctrl, &id))
		return;

	mutex_lock(&ctrl->namespaces_mutex);
	nn = le32_to_cpu(id->nn);
	if (ctrl->vs >= NVME_VS(1, 1) &&
	    !(ctrl->quirks & NVME_QUIRK_IDENTIFY_CNS)) {
		if (!nvme_scan_ns_list(ctrl, nn))
			goto done;
	}
	__nvme_scan_namespaces(ctrl, le32_to_cpup(&id->nn));
 done:
	list_sort(NULL, &ctrl->namespaces, ns_cmp);
	mutex_unlock(&ctrl->namespaces_mutex);
	kfree(id);
}

void nvme_remove_namespaces(struct nvme_ctrl *ctrl)
{
	struct nvme_ns *ns, *next;

	list_for_each_entry_safe(ns, next, &ctrl->namespaces, list)
		nvme_ns_remove(ns);
}

static DEFINE_IDA(nvme_instance_ida);

static int nvme_set_instance(struct nvme_ctrl *ctrl)
{
	int instance, error;

	do {
		if (!ida_pre_get(&nvme_instance_ida, GFP_KERNEL))
			return -ENODEV;

		spin_lock(&dev_list_lock);
		error = ida_get_new(&nvme_instance_ida, &instance);
		spin_unlock(&dev_list_lock);
	} while (error == -EAGAIN);

	if (error)
		return -ENODEV;

	ctrl->instance = instance;
	return 0;
}

static void nvme_release_instance(struct nvme_ctrl *ctrl)
{
	spin_lock(&dev_list_lock);
	ida_remove(&nvme_instance_ida, ctrl->instance);
	spin_unlock(&dev_list_lock);
}

void nvme_uninit_ctrl(struct nvme_ctrl *ctrl)
 {
	device_destroy(nvme_class, MKDEV(nvme_char_major, ctrl->instance));

	spin_lock(&dev_list_lock);
	list_del(&ctrl->node);
	spin_unlock(&dev_list_lock);
}

static void nvme_free_ctrl(struct kref *kref)
{
	struct nvme_ctrl *ctrl = container_of(kref, struct nvme_ctrl, kref);

	put_device(ctrl->device);
	nvme_release_instance(ctrl);

	ctrl->ops->free_ctrl(ctrl);
}

void nvme_put_ctrl(struct nvme_ctrl *ctrl)
{
	kref_put(&ctrl->kref, nvme_free_ctrl);
}

/*
 * Initialize a NVMe controller structures.  This needs to be called during
 * earliest initialization so that we have the initialized structured around
 * during probing.
 */
int nvme_init_ctrl(struct nvme_ctrl *ctrl, struct device *dev,
		const struct nvme_ctrl_ops *ops, unsigned long quirks)
{
	int ret;

	INIT_LIST_HEAD(&ctrl->namespaces);
	mutex_init(&ctrl->namespaces_mutex);
	kref_init(&ctrl->kref);
	ctrl->dev = dev;
	ctrl->ops = ops;
	ctrl->quirks = quirks;

	ret = nvme_set_instance(ctrl);
	if (ret)
		goto out;

	ctrl->device = device_create_with_groups(nvme_class, ctrl->dev,
				MKDEV(nvme_char_major, ctrl->instance),
				dev, nvme_dev_attr_groups,
				"nvme%d", ctrl->instance);
	if (IS_ERR(ctrl->device)) {
		ret = PTR_ERR(ctrl->device);
		goto out_release_instance;
	}
	get_device(ctrl->device);
	dev_set_drvdata(ctrl->device, ctrl);

	spin_lock(&dev_list_lock);
	list_add_tail(&ctrl->node, &nvme_ctrl_list);
	spin_unlock(&dev_list_lock);

	/*
	 * Initialize latency tolerance controls.  The sysfs files won't
	 * be visible to userspace unless the device actually supports APST.
	 */
	ctrl->device->power.set_latency_tolerance = nvme_set_latency_tolerance;
	dev_pm_qos_update_user_latency_tolerance(ctrl->device,
		min(default_ps_max_latency_us, (unsigned long)S32_MAX));

	return 0;
out_release_instance:
	nvme_release_instance(ctrl);
out:
	return ret;
}

/**
 * nvme_kill_queues(): Ends all namespace queues
 * @ctrl: the dead controller that needs to end
 *
 * Call this function when the driver determines it is unable to get the
 * controller in a state capable of servicing IO.
 */
void nvme_kill_queues(struct nvme_ctrl *ctrl)
{
	struct nvme_ns *ns;

	mutex_lock(&ctrl->namespaces_mutex);
	list_for_each_entry(ns, &ctrl->namespaces, list) {
		if (!kref_get_unless_zero(&ns->kref))
			continue;

		/*
		 * Revalidating a dead namespace sets capacity to 0. This will
		 * end buffered writers dirtying pages that can't be synced.
		 */
		if (!test_and_set_bit(NVME_NS_DEAD, &ns->flags))
			revalidate_disk(ns->disk);

		blk_set_queue_dying(ns->queue);
		blk_mq_abort_requeue_list(ns->queue);
		blk_mq_start_stopped_hw_queues(ns->queue, true);

		nvme_put_ns(ns);
	}
	mutex_unlock(&ctrl->namespaces_mutex);
}

void nvme_stop_queues(struct nvme_ctrl *ctrl)
{
	struct nvme_ns *ns;

	mutex_lock(&ctrl->namespaces_mutex);
	list_for_each_entry(ns, &ctrl->namespaces, list) {
		spin_lock_irq(ns->queue->queue_lock);
		queue_flag_set(QUEUE_FLAG_STOPPED, ns->queue);
		spin_unlock_irq(ns->queue->queue_lock);

		blk_mq_cancel_requeue_work(ns->queue);
		blk_mq_stop_hw_queues(ns->queue);
	}
	mutex_unlock(&ctrl->namespaces_mutex);
}

void nvme_start_queues(struct nvme_ctrl *ctrl)
{
	struct nvme_ns *ns;

	mutex_lock(&ctrl->namespaces_mutex);
	list_for_each_entry(ns, &ctrl->namespaces, list) {
		queue_flag_clear_unlocked(QUEUE_FLAG_STOPPED, ns->queue);
		blk_mq_start_stopped_hw_queues(ns->queue, true);
		blk_mq_kick_requeue_list(ns->queue);
	}
	mutex_unlock(&ctrl->namespaces_mutex);
}

int __init nvme_core_init(void)
{
	int result;

	result = register_blkdev(nvme_major, "nvme");
	if (result < 0)
		return result;
	else if (result > 0)
		nvme_major = result;

	result = __register_chrdev(nvme_char_major, 0, NVME_MINORS, "nvme",
							&nvme_dev_fops);
	if (result < 0)
		goto unregister_blkdev;
	else if (result > 0)
		nvme_char_major = result;

#if 1
	result = aio_service_init();
	if (result)
		goto unregister_chrdev;
    
    result = aio_worker_init();
    if (result)
		goto unregister_aio_service;
#endif

	nvme_class = class_create(THIS_MODULE, "nvme");
	if (IS_ERR(nvme_class)) {
		result = PTR_ERR(nvme_class);
		goto unregister_aio_worker;
	}
	return 0;
#if 1
unregister_aio_worker:
	aio_worker_exit();
unregister_aio_service:
    aio_service_exit();
#endif
unregister_chrdev:
	__unregister_chrdev(nvme_char_major, 0, NVME_MINORS, "nvme");
unregister_blkdev:
	unregister_blkdev(nvme_major, "nvme");
	return result;
}

void nvme_core_exit(void)
{
	unregister_blkdev(nvme_major, "nvme");
	class_destroy(nvme_class);
	__unregister_chrdev(nvme_char_major, 0, NVME_MINORS, "nvme");
#if 1
    aio_worker_exit();
    aio_service_exit();
#endif
}
