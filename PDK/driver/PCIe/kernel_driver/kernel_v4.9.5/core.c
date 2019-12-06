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
#include <linux/vmalloc.h>
#include <linux/types.h>
#include <linux/pr.h>
#include <linux/ptrace.h>
#include <linux/t10-pi.h>
#include <scsi/sg.h>
#include <asm/unaligned.h>

#include "nvme.h"
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/eventfd.h>
#include <linux/kref.h>
#include <linux/freezer.h>
#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/task_io_accounting_ops.h>
#include "linux_nvme_ioctl.h"
#include "fabrics.h"

#define NVME_MINORS		(1U << MINORBITS)

unsigned char admin_timeout = 60;
module_param(admin_timeout, byte, 0644);
MODULE_PARM_DESC(admin_timeout, "timeout in seconds for admin commands");
EXPORT_SYMBOL_GPL(admin_timeout);

unsigned char nvme_io_timeout = 30;
module_param_named(io_timeout, nvme_io_timeout, byte, 0644);
MODULE_PARM_DESC(io_timeout, "timeout in seconds for I/O");
EXPORT_SYMBOL_GPL(nvme_io_timeout);

unsigned char shutdown_timeout = 5;
module_param(shutdown_timeout, byte, 0644);
MODULE_PARM_DESC(shutdown_timeout, "timeout in seconds for controller shutdown");

unsigned int nvme_max_retries = 5;
module_param_named(max_retries, nvme_max_retries, uint, 0644);
MODULE_PARM_DESC(max_retries, "max number of retries a command may have");
EXPORT_SYMBOL_GPL(nvme_max_retries);

static int nvme_char_major;
module_param(nvme_char_major, int, 0);

static LIST_HEAD(nvme_ctrl_list);
static DEFINE_SPINLOCK(dev_list_lock);

static struct class *nvme_class;


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
	aiocb->event.status = le16_to_cpu(cqe->status) >> 1;
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


void nvme_cancel_request(struct request *req, void *data, bool reserved)
{
	int status;

	if (!blk_mq_request_started(req))
		return;

	dev_dbg_ratelimited(((struct nvme_ctrl *) data)->device,
				"Cancelling I/O %d", req->tag);

	status = NVME_SC_ABORT_REQ;
	if (blk_queue_dying(req->q))
		status |= NVME_SC_DNR;
	blk_mq_complete_request(req, status);
}
EXPORT_SYMBOL_GPL(nvme_cancel_request);

bool nvme_change_ctrl_state(struct nvme_ctrl *ctrl,
		enum nvme_ctrl_state new_state)
{
	enum nvme_ctrl_state old_state;
	bool changed = false;

	spin_lock_irq(&ctrl->lock);

	old_state = ctrl->state;
	switch (new_state) {
	case NVME_CTRL_LIVE:
		switch (old_state) {
		case NVME_CTRL_NEW:
		case NVME_CTRL_RESETTING:
		case NVME_CTRL_RECONNECTING:
			changed = true;
			/* FALLTHRU */
		default:
			break;
		}
		break;
	case NVME_CTRL_RESETTING:
		switch (old_state) {
		case NVME_CTRL_NEW:
		case NVME_CTRL_LIVE:
		case NVME_CTRL_RECONNECTING:
			changed = true;
			/* FALLTHRU */
		default:
			break;
		}
		break;
	case NVME_CTRL_RECONNECTING:
		switch (old_state) {
		case NVME_CTRL_LIVE:
			changed = true;
			/* FALLTHRU */
		default:
			break;
		}
		break;
	case NVME_CTRL_DELETING:
		switch (old_state) {
		case NVME_CTRL_LIVE:
		case NVME_CTRL_RESETTING:
		case NVME_CTRL_RECONNECTING:
			changed = true;
			/* FALLTHRU */
		default:
			break;
		}
		break;
	case NVME_CTRL_DEAD:
		switch (old_state) {
		case NVME_CTRL_DELETING:
			changed = true;
			/* FALLTHRU */
		default:
			break;
		}
		break;
	default:
		break;
	}

	if (changed)
		ctrl->state = new_state;

	spin_unlock_irq(&ctrl->lock);

	return changed;
}
EXPORT_SYMBOL_GPL(nvme_change_ctrl_state);

static void nvme_free_ns(struct kref *kref)
{
	struct nvme_ns *ns = container_of(kref, struct nvme_ns, kref);

	if (ns->ndev)
		nvme_nvm_unregister(ns);

	if (ns->disk) {
		spin_lock(&dev_list_lock);
		ns->disk->private_data = NULL;
		spin_unlock(&dev_list_lock);
	}

	put_disk(ns->disk);
	ida_simple_remove(&ns->ctrl->ns_ida, ns->instance);
	nvme_put_ctrl(ns->ctrl);
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
	if (ns) {
		if (!kref_get_unless_zero(&ns->kref))
			goto fail;
		if (!try_module_get(ns->ctrl->ops->module))
			goto fail_put_ns;
	}
	spin_unlock(&dev_list_lock);

	return ns;

fail_put_ns:
	kref_put(&ns->kref, nvme_free_ns);
fail:
	spin_unlock(&dev_list_lock);
	return NULL;
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
EXPORT_SYMBOL_GPL(nvme_requeue_req);

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
	req->cmd = (unsigned char *)cmd;
	req->cmd_len = sizeof(struct nvme_command);

	return req;
}
EXPORT_SYMBOL_GPL(nvme_alloc_request);

static inline void nvme_setup_flush(struct nvme_ns *ns,
		struct nvme_command *cmnd)
{
	memset(cmnd, 0, sizeof(*cmnd));
	cmnd->common.opcode = nvme_cmd_flush;
	cmnd->common.nsid = cpu_to_le32(ns->ns_id);
}

static inline int nvme_setup_discard(struct nvme_ns *ns, struct request *req,
		struct nvme_command *cmnd)
{
	struct nvme_dsm_range *range;
	struct page *page;
	int offset;
	unsigned int nr_bytes = blk_rq_bytes(req);

	range = kmalloc(sizeof(*range), GFP_ATOMIC);
	if (!range)
		return BLK_MQ_RQ_QUEUE_BUSY;

	range->cattr = cpu_to_le32(0);
	range->nlb = cpu_to_le32(nr_bytes >> ns->lba_shift);
	range->slba = cpu_to_le64(nvme_block_nr(ns, blk_rq_pos(req)));

	memset(cmnd, 0, sizeof(*cmnd));
	cmnd->dsm.opcode = nvme_cmd_dsm;
	cmnd->dsm.nsid = cpu_to_le32(ns->ns_id);
	cmnd->dsm.nr = 0;
	cmnd->dsm.attributes = cpu_to_le32(NVME_DSMGMT_AD);

	req->completion_data = range;
	page = virt_to_page(range);
	offset = offset_in_page(range);
	blk_add_request_payload(req, page, offset, sizeof(*range));

	/*
	 * we set __data_len back to the size of the area to be discarded
	 * on disk. This allows us to report completion on the full amount
	 * of blocks described by the request.
	 */
	req->__data_len = nr_bytes;

	return 0;
}

static inline void nvme_setup_rw(struct nvme_ns *ns, struct request *req,
		struct nvme_command *cmnd)
{
	u16 control = 0;
	u32 dsmgmt = 0;

	if (req->cmd_flags & REQ_FUA)
		control |= NVME_RW_FUA;
	if (req->cmd_flags & (REQ_FAILFAST_DEV | REQ_RAHEAD))
		control |= NVME_RW_LR;

	if (req->cmd_flags & REQ_RAHEAD)
		dsmgmt |= NVME_RW_DSM_FREQ_PREFETCH;

	memset(cmnd, 0, sizeof(*cmnd));
	cmnd->rw.opcode = (rq_data_dir(req) ? nvme_cmd_write : nvme_cmd_read);
	cmnd->rw.command_id = req->tag;
	cmnd->rw.nsid = cpu_to_le32(ns->ns_id);
	cmnd->rw.slba = cpu_to_le64(nvme_block_nr(ns, blk_rq_pos(req)));
	cmnd->rw.length = cpu_to_le16((blk_rq_bytes(req) >> ns->lba_shift) - 1);

	if (ns->ms) {
		switch (ns->pi_type) {
		case NVME_NS_DPS_PI_TYPE3:
			control |= NVME_RW_PRINFO_PRCHK_GUARD;
			break;
		case NVME_NS_DPS_PI_TYPE1:
		case NVME_NS_DPS_PI_TYPE2:
			control |= NVME_RW_PRINFO_PRCHK_GUARD |
					NVME_RW_PRINFO_PRCHK_REF;
			cmnd->rw.reftag = cpu_to_le32(
					nvme_block_nr(ns, blk_rq_pos(req)));
			break;
		}
		if (!blk_integrity_rq(req))
			control |= NVME_RW_PRINFO_PRACT;
	}

	cmnd->rw.control = cpu_to_le16(control);
	cmnd->rw.dsmgmt = cpu_to_le32(dsmgmt);
}

int nvme_setup_cmd(struct nvme_ns *ns, struct request *req,
		struct nvme_command *cmd)
{
	int ret = 0;

	if (req->cmd_type == REQ_TYPE_DRV_PRIV)
		memcpy(cmd, req->cmd, sizeof(*cmd));
	else if (req_op(req) == REQ_OP_FLUSH)
		nvme_setup_flush(ns, cmd);
	else if (req_op(req) == REQ_OP_DISCARD)
		ret = nvme_setup_discard(ns, req, cmd);
	else
		nvme_setup_rw(ns, req, cmd);

	return ret;
}
EXPORT_SYMBOL_GPL(nvme_setup_cmd);

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
EXPORT_SYMBOL_GPL(nvme_submit_sync_cmd);


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
			*status = le16_to_cpu(cqe.status) >> 1;
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

		if (meta_buffer && meta_len) {
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

static void nvme_keep_alive_end_io(struct request *rq, int error)
{
	struct nvme_ctrl *ctrl = rq->end_io_data;

	blk_mq_free_request(rq);

	if (error) {
		dev_err(ctrl->device,
			"failed nvme_keep_alive_end_io error=%d\n", error);
		return;
	}

	schedule_delayed_work(&ctrl->ka_work, ctrl->kato * HZ);
}

static int nvme_keep_alive(struct nvme_ctrl *ctrl)
{
	struct nvme_command c;
	struct request *rq;

	memset(&c, 0, sizeof(c));
	c.common.opcode = nvme_admin_keep_alive;

	rq = nvme_alloc_request(ctrl->admin_q, &c, BLK_MQ_REQ_RESERVED,
			NVME_QID_ANY);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	rq->timeout = ctrl->kato * HZ;
	rq->end_io_data = ctrl;

	blk_execute_rq_nowait(rq->q, NULL, rq, 0, nvme_keep_alive_end_io);

	return 0;
}

static void nvme_keep_alive_work(struct work_struct *work)
{
	struct nvme_ctrl *ctrl = container_of(to_delayed_work(work),
			struct nvme_ctrl, ka_work);

	if (nvme_keep_alive(ctrl)) {
		/* allocation failure, reset the controller */
		dev_err(ctrl->device, "keep-alive failed\n");
		ctrl->ops->reset_ctrl(ctrl);
		return;
	}
}

void nvme_start_keep_alive(struct nvme_ctrl *ctrl)
{
	if (unlikely(ctrl->kato == 0))
		return;

	INIT_DELAYED_WORK(&ctrl->ka_work, nvme_keep_alive_work);
	schedule_delayed_work(&ctrl->ka_work, ctrl->kato * HZ);
}
EXPORT_SYMBOL_GPL(nvme_start_keep_alive);

void nvme_stop_keep_alive(struct nvme_ctrl *ctrl)
{
	if (unlikely(ctrl->kato == 0))
		return;

	cancel_delayed_work_sync(&ctrl->ka_work);
}
EXPORT_SYMBOL_GPL(nvme_stop_keep_alive);

int nvme_identify_ctrl(struct nvme_ctrl *dev, struct nvme_id_ctrl **id)
{
	struct nvme_command c = { };
	int error;

	/* gcc-4.4.4 (at least) has issues with initializers and anon unions */
	c.identify.opcode = nvme_admin_identify;
	c.identify.cns = cpu_to_le32(NVME_ID_CNS_CTRL);

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
	c.identify.cns = cpu_to_le32(NVME_ID_CNS_NS_ACTIVE_LIST);
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
	if (status < 0)
		return status;

	/*
	 * Degraded controllers might return an error when setting the queue
	 * count.  We still want to be able to bring them online and offer
	 * access to the admin queue, as that might be only way to fix them up.
	 */
	if (status > 0) {
		dev_err(ctrl->dev, "Could not set queue count (%d)\n", status);
		*count = 0;
	} else {
		nr_io_queues = min(result & 0xffff, result >> 16) + 1;
		*count = min(*count, nr_io_queues);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(nvme_set_queue_count);

static int nvme_submit_io(struct nvme_ns *ns, struct nvme_user_io __user *uio)
{
	struct nvme_user_io io;
	struct nvme_command c;
	unsigned length, meta_len;
	void __user *metadata;

	if (copy_from_user(&io, uio, sizeof(io)))
		return -EFAULT;
	if (io.flags)
		return -EINVAL;

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
            if (cmd.key_length > KVCMD_MAX_KEY_SIZE ||
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
            if (cmd.key_length > KVCMD_MAX_KEY_SIZE ||
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
	if (cmd.flags)
		return -EINVAL;

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
#ifdef CONFIG_BLK_DEV_NVME_SCSI
	case SG_GET_VERSION_NUM:
		return nvme_sg_get_version_num((void __user *)arg);
	case SG_IO:
		return nvme_sg_io(ns, (void __user *)arg);
#endif
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
	struct nvme_ns *ns = disk->private_data;

	module_put(ns->ctrl->ops->module);
	nvme_put_ns(ns);
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

	memset(&integrity, 0, sizeof(integrity));
	switch (ns->pi_type) {
	case NVME_NS_DPS_PI_TYPE3:
		integrity.profile = &t10_pi_type3_crc;
		integrity.tag_size = sizeof(u16) + sizeof(u32);
		integrity.flags |= BLK_INTEGRITY_DEVICE_CAPABLE;
		break;
	case NVME_NS_DPS_PI_TYPE1:
	case NVME_NS_DPS_PI_TYPE2:
		integrity.profile = &t10_pi_type1_crc;
		integrity.tag_size = sizeof(u16);
		integrity.flags |= BLK_INTEGRITY_DEVICE_CAPABLE;
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
	blk_queue_max_discard_sectors(ns->queue, UINT_MAX);
	queue_flag_set_unlocked(QUEUE_FLAG_DISCARD, ns->queue);
}

static int nvme_revalidate_ns(struct nvme_ns *ns, struct nvme_id_ns **id)
{
	if (nvme_identify_ns(ns->ctrl, ns->ns_id, id)) {
		dev_warn(ns->ctrl->dev, "%s: Identify failure\n", __func__);
		return -ENODEV;
	}

	if ((*id)->ncap == 0) {
		kfree(*id);
		return -ENODEV;
	}

	if (ns->ctrl->vs >= NVME_VS(1, 1, 0))
		memcpy(ns->eui, (*id)->eui64, sizeof(ns->eui));
	if (ns->ctrl->vs >= NVME_VS(1, 2, 0))
		memcpy(ns->uuid, (*id)->nguid, sizeof(ns->uuid));

	return 0;
}

static void __nvme_revalidate_disk(struct gendisk *disk, struct nvme_id_ns *id)
{
	struct nvme_ns *ns = disk->private_data;
	u8 lbaf, pi_type;
	u16 old_ms;
	unsigned short bs;

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
}

static int nvme_revalidate_disk(struct gendisk *disk)
{
	struct nvme_ns *ns = disk->private_data;
	struct nvme_id_ns *id = NULL;
	int ret;

	if (test_bit(NVME_NS_DEAD, &ns->flags)) {
		set_capacity(disk, 0);
		return -ENODEV;
	}

	ret = nvme_revalidate_ns(ns, &id);
	if (ret)
		return ret;

	__nvme_revalidate_disk(disk, id);
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
		if (csts == ~0)
			return -ENODEV;
		if ((csts & NVME_CSTS_RDY) == bit)
			break;

		msleep(100);
		if (fatal_signal_pending(current))
			return -EINTR;
		if (time_after(jiffies, timeout)) {
			dev_err(ctrl->device,
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
EXPORT_SYMBOL_GPL(nvme_disable_ctrl);

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
		dev_err(ctrl->device,
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
EXPORT_SYMBOL_GPL(nvme_enable_ctrl);

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
			dev_err(ctrl->device,
				"Device shutdown incomplete; abort shutdown\n");
			return -ENODEV;
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(nvme_shutdown_ctrl);

static void nvme_set_queue_limits(struct nvme_ctrl *ctrl,
		struct request_queue *q)
{
	bool vwc = false;

	if (ctrl->max_hw_sectors) {
		u32 max_segments =
			(ctrl->max_hw_sectors / (ctrl->page_size >> 9)) + 1;

		blk_queue_max_hw_sectors(q, ctrl->max_hw_sectors);
		blk_queue_max_segments(q, min_t(u32, max_segments, USHRT_MAX));
	}
	if (ctrl->stripe_size)
		blk_queue_chunk_sectors(q, ctrl->stripe_size >> 9);
	blk_queue_virt_boundary(q, ctrl->page_size - 1);
	if (ctrl->vwc & NVME_CTRL_VWC_PRESENT)
		vwc = true;
	blk_queue_write_cache(q, vwc, vwc);
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
	u32 max_hw_sectors;

	ret = ctrl->ops->reg_read32(ctrl, NVME_REG_VS, &ctrl->vs);
	if (ret) {
		dev_err(ctrl->device, "Reading VS failed (%d)\n", ret);
		return ret;
	}

	ret = ctrl->ops->reg_read64(ctrl, NVME_REG_CAP, &cap);
	if (ret) {
		dev_err(ctrl->device, "Reading CAP failed (%d)\n", ret);
		return ret;
	}
	page_shift = NVME_CAP_MPSMIN(cap) + 12;

	if (ctrl->vs >= NVME_VS(1, 1, 0))
		ctrl->subsystem = NVME_CAP_NSSRC(cap);

	ret = nvme_identify_ctrl(ctrl, &id);
	if (ret) {
		dev_err(ctrl->device, "Identify Controller failed (%d)\n", ret);
		return -EIO;
	}

	ctrl->vid = le16_to_cpu(id->vid);
	ctrl->oncs = le16_to_cpup(&id->oncs);
	atomic_set(&ctrl->abort_limit, id->acl + 1);
	ctrl->vwc = id->vwc;
	ctrl->cntlid = le16_to_cpup(&id->cntlid);
	memcpy(ctrl->serial, id->sn, sizeof(id->sn));
	memcpy(ctrl->model, id->mn, sizeof(id->mn));
	memcpy(ctrl->firmware_rev, id->fr, sizeof(id->fr));
	if (id->mdts)
		max_hw_sectors = 1 << (id->mdts + page_shift - 9);
	else
		max_hw_sectors = UINT_MAX;
	ctrl->max_hw_sectors =
		min_not_zero(ctrl->max_hw_sectors, max_hw_sectors);

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
	ctrl->sgls = le32_to_cpu(id->sgls);
	ctrl->kas = le16_to_cpu(id->kas);

	if (ctrl->ops->is_fabrics) {
		ctrl->icdoff = le16_to_cpu(id->icdoff);
		ctrl->ioccsz = le32_to_cpu(id->ioccsz);
		ctrl->iorcsz = le32_to_cpu(id->iorcsz);
		ctrl->maxcmd = le16_to_cpu(id->maxcmd);

		/*
		 * In fabrics we need to verify the cntlid matches the
		 * admin connect
		 */
		if (ctrl->cntlid != le16_to_cpu(id->cntlid))
			ret = -EINVAL;

		if (!ctrl->opts->discovery_nqn && !ctrl->kas) {
			dev_err(ctrl->dev,
				"keep-alive support is mandatory for fabrics\n");
			ret = -EINVAL;
		}
	} else {
		ctrl->cntlid = le16_to_cpu(id->cntlid);
	}

	kfree(id);
	return ret;
}
EXPORT_SYMBOL_GPL(nvme_init_identify);

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
		dev_warn(ctrl->device,
			"NVME_IOCTL_IO_CMD not supported when multiple namespaces present!\n");
		ret = -EINVAL;
		goto out_unlock;
	}

	dev_warn(ctrl->device,
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
		dev_warn(ctrl->device, "resetting controller\n");
		return ctrl->ops->reset_ctrl(ctrl);
	case NVME_IOCTL_SUBSYS_RESET:
		return nvme_reset_subsystem(ctrl);
	case NVME_IOCTL_RESCAN:
		nvme_queue_scan(ctrl);
		return 0;
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

static ssize_t nvme_sysfs_rescan(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);

	nvme_queue_scan(ctrl);
	return count;
}
static DEVICE_ATTR(rescan_controller, S_IWUSR, NULL, nvme_sysfs_rescan);

static ssize_t wwid_show(struct device *dev, struct device_attribute *attr,
								char *buf)
{
	struct nvme_ns *ns = nvme_get_ns_from_dev(dev);
	struct nvme_ctrl *ctrl = ns->ctrl;
	int serial_len = sizeof(ctrl->serial);
	int model_len = sizeof(ctrl->model);

	if (memchr_inv(ns->uuid, 0, sizeof(ns->uuid)))
		return sprintf(buf, "eui.%16phN\n", ns->uuid);

	if (memchr_inv(ns->eui, 0, sizeof(ns->eui)))
		return sprintf(buf, "eui.%8phN\n", ns->eui);

	while (ctrl->serial[serial_len - 1] == ' ')
		serial_len--;
	while (ctrl->model[model_len - 1] == ' ')
		model_len--;

	return sprintf(buf, "nvme.%04x-%*phN-%*phN-%08x\n", ctrl->vid,
		serial_len, ctrl->serial, model_len, ctrl->model, ns->ns_id);
}
static DEVICE_ATTR(wwid, S_IRUGO, wwid_show, NULL);

static ssize_t uuid_show(struct device *dev, struct device_attribute *attr,
								char *buf)
{
	struct nvme_ns *ns = nvme_get_ns_from_dev(dev);
	return sprintf(buf, "%pU\n", ns->uuid);
}
static DEVICE_ATTR(uuid, S_IRUGO, uuid_show, NULL);

static ssize_t eui_show(struct device *dev, struct device_attribute *attr,
								char *buf)
{
	struct nvme_ns *ns = nvme_get_ns_from_dev(dev);
	return sprintf(buf, "%8phd\n", ns->eui);
}
static DEVICE_ATTR(eui, S_IRUGO, eui_show, NULL);

static ssize_t nsid_show(struct device *dev, struct device_attribute *attr,
								char *buf)
{
	struct nvme_ns *ns = nvme_get_ns_from_dev(dev);
	return sprintf(buf, "%d\n", ns->ns_id);
}
static DEVICE_ATTR(nsid, S_IRUGO, nsid_show, NULL);

static struct attribute *nvme_ns_attrs[] = {
	&dev_attr_wwid.attr,
	&dev_attr_uuid.attr,
	&dev_attr_eui.attr,
	&dev_attr_nsid.attr,
	NULL,
};

static umode_t nvme_ns_attrs_are_visible(struct kobject *kobj,
		struct attribute *a, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct nvme_ns *ns = nvme_get_ns_from_dev(dev);

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
	.is_visible	= nvme_ns_attrs_are_visible,
};

#define nvme_show_str_function(field)						\
static ssize_t  field##_show(struct device *dev,				\
			    struct device_attribute *attr, char *buf)		\
{										\
        struct nvme_ctrl *ctrl = dev_get_drvdata(dev);				\
        return sprintf(buf, "%.*s\n", (int)sizeof(ctrl->field), ctrl->field);	\
}										\
static DEVICE_ATTR(field, S_IRUGO, field##_show, NULL);

#define nvme_show_int_function(field)						\
static ssize_t  field##_show(struct device *dev,				\
			    struct device_attribute *attr, char *buf)		\
{										\
        struct nvme_ctrl *ctrl = dev_get_drvdata(dev);				\
        return sprintf(buf, "%d\n", ctrl->field);	\
}										\
static DEVICE_ATTR(field, S_IRUGO, field##_show, NULL);

nvme_show_str_function(model);
nvme_show_str_function(serial);
nvme_show_str_function(firmware_rev);
nvme_show_int_function(cntlid);

static ssize_t nvme_sysfs_delete(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);

	if (device_remove_file_self(dev, attr))
		ctrl->ops->delete_ctrl(ctrl);
	return count;
}
static DEVICE_ATTR(delete_controller, S_IWUSR, NULL, nvme_sysfs_delete);

static ssize_t nvme_sysfs_show_transport(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%s\n", ctrl->ops->name);
}
static DEVICE_ATTR(transport, S_IRUGO, nvme_sysfs_show_transport, NULL);

static ssize_t nvme_sysfs_show_subsysnqn(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%s\n",
			ctrl->ops->get_subsysnqn(ctrl));
}
static DEVICE_ATTR(subsysnqn, S_IRUGO, nvme_sysfs_show_subsysnqn, NULL);

static ssize_t nvme_sysfs_show_address(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);

	return ctrl->ops->get_address(ctrl, buf, PAGE_SIZE);
}
static DEVICE_ATTR(address, S_IRUGO, nvme_sysfs_show_address, NULL);

static struct attribute *nvme_dev_attrs[] = {
	&dev_attr_reset_controller.attr,
	&dev_attr_rescan_controller.attr,
	&dev_attr_model.attr,
	&dev_attr_serial.attr,
	&dev_attr_firmware_rev.attr,
	&dev_attr_cntlid.attr,
	&dev_attr_delete_controller.attr,
	&dev_attr_transport.attr,
	&dev_attr_subsysnqn.attr,
	&dev_attr_address.attr,
	NULL
};

#define CHECK_ATTR(ctrl, a, name)		\
	if ((a) == &dev_attr_##name.attr &&	\
	    !(ctrl)->ops->get_##name)		\
		return 0

static umode_t nvme_dev_attrs_are_visible(struct kobject *kobj,
		struct attribute *a, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);

	if (a == &dev_attr_delete_controller.attr) {
		if (!ctrl->ops->delete_ctrl)
			return 0;
	}

	CHECK_ATTR(ctrl, a, subsysnqn);
	CHECK_ATTR(ctrl, a, address);

	return a->mode;
}

static struct attribute_group nvme_dev_attrs_group = {
	.attrs		= nvme_dev_attrs,
	.is_visible	= nvme_dev_attrs_are_visible,
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

static struct nvme_ns *nvme_find_get_ns(struct nvme_ctrl *ctrl, unsigned nsid)
{
	struct nvme_ns *ns, *ret = NULL;

	mutex_lock(&ctrl->namespaces_mutex);
	list_for_each_entry(ns, &ctrl->namespaces, list) {
		if (ns->ns_id == nsid) {
			kref_get(&ns->kref);
			ret = ns;
			break;
		}
		if (ns->ns_id > nsid)
			break;
	}
	mutex_unlock(&ctrl->namespaces_mutex);
	return ret;
}

static void nvme_alloc_ns(struct nvme_ctrl *ctrl, unsigned nsid)
{
	struct nvme_ns *ns;
	struct gendisk *disk;
	struct nvme_id_ns *id;
	char disk_name[DISK_NAME_LEN];
	int node = dev_to_node(ctrl->dev);

	ns = kzalloc_node(sizeof(*ns), GFP_KERNEL, node);
	if (!ns)
		return;

	ns->instance = ida_simple_get(&ctrl->ns_ida, 1, 0, GFP_KERNEL);
	if (ns->instance < 0)
		goto out_free_ns;

	ns->queue = blk_mq_init_queue(ctrl->tagset);
	if (IS_ERR(ns->queue))
		goto out_release_instance;
	queue_flag_set_unlocked(QUEUE_FLAG_NONROT, ns->queue);
	ns->queue->queuedata = ns;
	ns->ctrl = ctrl;

	kref_init(&ns->kref);
	ns->ns_id = nsid;
	ns->lba_shift = 9; /* set to a default value for 512 until disk is validated */

	blk_queue_logical_block_size(ns->queue, 1 << ns->lba_shift);
	nvme_set_queue_limits(ctrl, ns->queue);

	sprintf(disk_name, "nvme%dn%d", ctrl->instance, ns->instance);

	if (nvme_revalidate_ns(ns, &id))
		goto out_free_queue;

	if (nvme_nvm_ns_supported(ns, id)) {
		if (nvme_nvm_register(ns, disk_name, node,
							&nvme_ns_attr_group)) {
			dev_warn(ctrl->dev, "%s: LightNVM init failure\n",
								__func__);
			goto out_free_id;
		}
	} else {
		disk = alloc_disk_node(0, node);
		if (!disk)
			goto out_free_id;

		disk->fops = &nvme_fops;
		disk->private_data = ns;
		disk->queue = ns->queue;
		disk->flags = GENHD_FL_EXT_DEVT;
		memcpy(disk->disk_name, disk_name, DISK_NAME_LEN);
		ns->disk = disk;

		__nvme_revalidate_disk(disk, id);
	}

	mutex_lock(&ctrl->namespaces_mutex);
	list_add_tail(&ns->list, &ctrl->namespaces);
	mutex_unlock(&ctrl->namespaces_mutex);

	kref_get(&ctrl->kref);

	kfree(id);

	if (ns->ndev)
		return;

	device_add_disk(ctrl->device, ns->disk);
	if (sysfs_create_group(&disk_to_dev(ns->disk)->kobj,
					&nvme_ns_attr_group))
		pr_warn("%s: failed to create sysfs group for identification\n",
			ns->disk->disk_name);
	return;
 out_free_id:
	kfree(id);
 out_free_queue:
	blk_cleanup_queue(ns->queue);
 out_release_instance:
	ida_simple_remove(&ctrl->ns_ida, ns->instance);
 out_free_ns:
	kfree(ns);
}

static void nvme_ns_remove(struct nvme_ns *ns)
{
	if (test_and_set_bit(NVME_NS_REMOVING, &ns->flags))
		return;

	if (ns->disk && ns->disk->flags & GENHD_FL_UP) {
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

	ns = nvme_find_get_ns(ctrl, nsid);
	if (ns) {
		if (ns->disk && revalidate_disk(ns->disk))
			nvme_ns_remove(ns);
		nvme_put_ns(ns);
	} else
		nvme_alloc_ns(ctrl, nsid);
}

static void nvme_remove_invalid_namespaces(struct nvme_ctrl *ctrl,
					unsigned nsid)
{
	struct nvme_ns *ns, *next;

	list_for_each_entry_safe(ns, next, &ctrl->namespaces, list) {
		if (ns->ns_id > nsid)
			nvme_ns_remove(ns);
	}
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
			goto free;

		for (j = 0; j < min(nn, 1024U); j++) {
			nsid = le32_to_cpu(ns_list[j]);
			if (!nsid)
				goto out;

			nvme_validate_ns(ctrl, nsid);

			while (++prev < nsid) {
				ns = nvme_find_get_ns(ctrl, prev);
				if (ns) {
					nvme_ns_remove(ns);
					nvme_put_ns(ns);
				}
			}
		}
		nn -= j;
	}
 out:
	nvme_remove_invalid_namespaces(ctrl, prev);
 free:
	kfree(ns_list);
	return ret;
}

static void nvme_scan_ns_sequential(struct nvme_ctrl *ctrl, unsigned nn)
{
	unsigned i;

	for (i = 1; i <= nn; i++)
		nvme_validate_ns(ctrl, i);

	nvme_remove_invalid_namespaces(ctrl, nn);
}

static void nvme_scan_work(struct work_struct *work)
{
	struct nvme_ctrl *ctrl =
		container_of(work, struct nvme_ctrl, scan_work);
	struct nvme_id_ctrl *id;
	unsigned nn;

	if (ctrl->state != NVME_CTRL_LIVE)
		return;

	if (nvme_identify_ctrl(ctrl, &id))
		return;

	nn = le32_to_cpu(id->nn);
	if (ctrl->vs >= NVME_VS(1, 1, 0) &&
	    !(ctrl->quirks & NVME_QUIRK_IDENTIFY_CNS)) {
		if (!nvme_scan_ns_list(ctrl, nn))
			goto done;
	}
	nvme_scan_ns_sequential(ctrl, nn);
 done:
	mutex_lock(&ctrl->namespaces_mutex);
	list_sort(NULL, &ctrl->namespaces, ns_cmp);
	mutex_unlock(&ctrl->namespaces_mutex);
	kfree(id);
}

void nvme_queue_scan(struct nvme_ctrl *ctrl)
{
	/*
	 * Do not queue new scan work when a controller is reset during
	 * removal.
	 */
	if (ctrl->state == NVME_CTRL_LIVE)
		schedule_work(&ctrl->scan_work);
}
EXPORT_SYMBOL_GPL(nvme_queue_scan);

/*
 * This function iterates the namespace list unlocked to allow recovery from
 * controller failure. It is up to the caller to ensure the namespace list is
 * not modified by scan work while this function is executing.
 */
void nvme_remove_namespaces(struct nvme_ctrl *ctrl)
{
	struct nvme_ns *ns, *next;

	/*
	 * The dead states indicates the controller was not gracefully
	 * disconnected. In that case, we won't be able to flush any data while
	 * removing the namespaces' disks; fail all the queues now to avoid
	 * potentially having to clean up the failed sync later.
	 */
	if (ctrl->state == NVME_CTRL_DEAD)
		nvme_kill_queues(ctrl);

	list_for_each_entry_safe(ns, next, &ctrl->namespaces, list)
		nvme_ns_remove(ns);
}
EXPORT_SYMBOL_GPL(nvme_remove_namespaces);

static void nvme_async_event_work(struct work_struct *work)
{
	struct nvme_ctrl *ctrl =
		container_of(work, struct nvme_ctrl, async_event_work);

	spin_lock_irq(&ctrl->lock);
	while (ctrl->event_limit > 0) {
		int aer_idx = --ctrl->event_limit;

		spin_unlock_irq(&ctrl->lock);
		ctrl->ops->submit_async_event(ctrl, aer_idx);
		spin_lock_irq(&ctrl->lock);
	}
	spin_unlock_irq(&ctrl->lock);
}

void nvme_complete_async_event(struct nvme_ctrl *ctrl,
		struct nvme_completion *cqe)
{
	u16 status = le16_to_cpu(cqe->status) >> 1;
	u32 result = le32_to_cpu(cqe->result);

	if (status == NVME_SC_SUCCESS || status == NVME_SC_ABORT_REQ) {
		++ctrl->event_limit;
		schedule_work(&ctrl->async_event_work);
	}

	if (status != NVME_SC_SUCCESS)
		return;

	switch (result & 0xff07) {
	case NVME_AER_NOTICE_NS_CHANGED:
		dev_info(ctrl->device, "rescanning\n");
		nvme_queue_scan(ctrl);
		break;
	default:
		dev_warn(ctrl->device, "async event result %08x\n", result);
	}
}
EXPORT_SYMBOL_GPL(nvme_complete_async_event);

void nvme_queue_async_events(struct nvme_ctrl *ctrl)
{
	ctrl->event_limit = NVME_NR_AERS;
	schedule_work(&ctrl->async_event_work);
}
EXPORT_SYMBOL_GPL(nvme_queue_async_events);

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
	flush_work(&ctrl->async_event_work);
	flush_work(&ctrl->scan_work);
	nvme_remove_namespaces(ctrl);

	device_destroy(nvme_class, MKDEV(nvme_char_major, ctrl->instance));

	spin_lock(&dev_list_lock);
	list_del(&ctrl->node);
	spin_unlock(&dev_list_lock);
}
EXPORT_SYMBOL_GPL(nvme_uninit_ctrl);

static void nvme_free_ctrl(struct kref *kref)
{
	struct nvme_ctrl *ctrl = container_of(kref, struct nvme_ctrl, kref);

	put_device(ctrl->device);
	nvme_release_instance(ctrl);
	ida_destroy(&ctrl->ns_ida);

	ctrl->ops->free_ctrl(ctrl);
}

void nvme_put_ctrl(struct nvme_ctrl *ctrl)
{
	kref_put(&ctrl->kref, nvme_free_ctrl);
}
EXPORT_SYMBOL_GPL(nvme_put_ctrl);

/*
 * Initialize a NVMe controller structures.  This needs to be called during
 * earliest initialization so that we have the initialized structured around
 * during probing.
 */
int nvme_init_ctrl(struct nvme_ctrl *ctrl, struct device *dev,
		const struct nvme_ctrl_ops *ops, unsigned long quirks)
{
	int ret;

	ctrl->state = NVME_CTRL_NEW;
	spin_lock_init(&ctrl->lock);
	INIT_LIST_HEAD(&ctrl->namespaces);
	mutex_init(&ctrl->namespaces_mutex);
	kref_init(&ctrl->kref);
	ctrl->dev = dev;
	ctrl->ops = ops;
	ctrl->quirks = quirks;
	INIT_WORK(&ctrl->scan_work, nvme_scan_work);
	INIT_WORK(&ctrl->async_event_work, nvme_async_event_work);

	ret = nvme_set_instance(ctrl);
	if (ret)
		goto out;

	ctrl->device = device_create_with_groups(nvme_class, ctrl->dev,
				MKDEV(nvme_char_major, ctrl->instance),
				ctrl, nvme_dev_attr_groups,
				"nvme%d", ctrl->instance);
	if (IS_ERR(ctrl->device)) {
		ret = PTR_ERR(ctrl->device);
		goto out_release_instance;
	}
	get_device(ctrl->device);
	ida_init(&ctrl->ns_ida);

	spin_lock(&dev_list_lock);
	list_add_tail(&ctrl->node, &nvme_ctrl_list);
	spin_unlock(&dev_list_lock);

	return 0;
out_release_instance:
	nvme_release_instance(ctrl);
out:
	return ret;
}
EXPORT_SYMBOL_GPL(nvme_init_ctrl);

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
		/*
		 * Revalidating a dead namespace sets capacity to 0. This will
		 * end buffered writers dirtying pages that can't be synced.
		 */
		if (ns->disk && !test_and_set_bit(NVME_NS_DEAD, &ns->flags))
			revalidate_disk(ns->disk);

		blk_set_queue_dying(ns->queue);
		blk_mq_abort_requeue_list(ns->queue);
		blk_mq_start_stopped_hw_queues(ns->queue, true);
	}
	mutex_unlock(&ctrl->namespaces_mutex);
}
EXPORT_SYMBOL_GPL(nvme_kill_queues);

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
EXPORT_SYMBOL_GPL(nvme_stop_queues);

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
EXPORT_SYMBOL_GPL(nvme_start_queues);

int __init nvme_core_init(void)
{
	int result;

	result = __register_chrdev(nvme_char_major, 0, NVME_MINORS, "nvme",
							&nvme_dev_fops);
	if (result < 0)
		return result;
	else if (result > 0)
		nvme_char_major = result;

	nvme_class = class_create(THIS_MODULE, "nvme");
	if (IS_ERR(nvme_class)) {
		result = PTR_ERR(nvme_class);
		goto unregister_chrdev;
	}
	result = aio_service_init();
	if (result)
		goto unregister_chrdev;
    
    result = aio_worker_init();
    if (result)
		goto unregister_chrdev;
	return 0;

 unregister_chrdev:
	__unregister_chrdev(nvme_char_major, 0, NVME_MINORS, "nvme");
	return result;
}

void nvme_core_exit(void)
{
	class_destroy(nvme_class);
	__unregister_chrdev(nvme_char_major, 0, NVME_MINORS, "nvme");
    aio_worker_exit();
	aio_service_exit();
}

MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
module_init(nvme_core_init);
module_exit(nvme_core_exit);
