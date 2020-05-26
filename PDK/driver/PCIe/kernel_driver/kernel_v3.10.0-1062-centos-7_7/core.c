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
#include <linux/idr.h>
#include <linux/pm_qos.h>
#include <scsi/sg.h>
#include <asm/unaligned.h>

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


#include "nvme.h"
#include "fabrics.h"

#define NVME_MINORS		(1U << MINORBITS)

unsigned int admin_timeout = 60;
module_param(admin_timeout, uint, 0644);
MODULE_PARM_DESC(admin_timeout, "timeout in seconds for admin commands");
EXPORT_SYMBOL_GPL(admin_timeout);

unsigned int nvme_io_timeout = 30;
module_param_named(io_timeout, nvme_io_timeout, uint, 0644);
MODULE_PARM_DESC(io_timeout, "timeout in seconds for I/O");
EXPORT_SYMBOL_GPL(nvme_io_timeout);

static unsigned char shutdown_timeout = 5;
module_param(shutdown_timeout, byte, 0644);
MODULE_PARM_DESC(shutdown_timeout, "timeout in seconds for controller shutdown");

static u8 nvme_max_retries = 5;
module_param_named(max_retries, nvme_max_retries, byte, 0644);
MODULE_PARM_DESC(max_retries, "max number of retries a command may have");

static unsigned long default_ps_max_latency_us = 100000;
module_param(default_ps_max_latency_us, ulong, 0644);
MODULE_PARM_DESC(default_ps_max_latency_us,
		 "max power saving latency for new devices; use PM QOS to change per device");

static bool force_apst;
module_param(force_apst, bool, 0644);
MODULE_PARM_DESC(force_apst, "allow APST for newly enumerated devices even if quirked off");

/*
 * nvme_wq - hosts nvme related works that are not reset or delete
 * nvme_reset_wq - hosts nvme reset works
 * nvme_delete_wq - hosts nvme delete works
 *
 * nvme_wq will host works such are scan, aen handling, fw activation,
 * keep-alive error recovery, periodic reconnects etc. nvme_reset_wq
 * runs reset works which also flush works hosted on nvme_wq for
 * serialization purposes. nvme_delete_wq host controller deletion
 * works which flush reset works for serialization.
 */
struct workqueue_struct *nvme_wq;
EXPORT_SYMBOL_GPL(nvme_wq);

struct workqueue_struct *nvme_reset_wq;
EXPORT_SYMBOL_GPL(nvme_reset_wq);

struct workqueue_struct *nvme_delete_wq;
EXPORT_SYMBOL_GPL(nvme_delete_wq);

static DEFINE_IDA(nvme_subsystems_ida);
static LIST_HEAD(nvme_subsystems);
static DEFINE_MUTEX(nvme_subsystems_lock);

static DEFINE_IDA(nvme_instance_ida);
static dev_t nvme_chr_devt;
static struct class *nvme_class;
static struct class *nvme_subsys_class;

static void nvme_ns_remove(struct nvme_ns *ns);
static int nvme_revalidate_disk(struct gendisk *disk);
static void nvme_remove_invalid_namespaces(struct nvme_ctrl *ctrl,
					   unsigned nsid);


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
			generic_end_io_acct(req->q, WRITE, &cmdinfo->disk->part0, cmdinfo->start_time);
		} else {
			generic_end_io_acct(req->q, READ, &cmdinfo->disk->part0, cmdinfo->start_time);
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
	//pr_err("complition handler for aiocb(%p)!\n", aiocb);
	aiocb->req = req;
	aiocb->event.result = le32_to_cpu(nvme_req(req)->result.u32);
	aiocb->event.status = error;
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



static void nvme_set_queue_dying(struct nvme_ns *ns)
{
	/*
	 * Revalidating a dead namespace sets capacity to 0. This will end
	 * buffered writers dirtying pages that can't be synced.
	 */
	if (!ns->disk || test_and_set_bit(NVME_NS_DEAD, &ns->flags))
		return;
	revalidate_disk(ns->disk);
	blk_set_queue_dying(ns->queue);
	/* Forcibly unquiesce queues to avoid blocking dispatch */
	blk_mq_unquiesce_queue(ns->queue);
}

static void nvme_queue_scan(struct nvme_ctrl *ctrl)
{
	/*
	 * Only new queue scan work when admin and IO queues are both alive
	 */
	if (ctrl->state == NVME_CTRL_LIVE)
		queue_work(nvme_wq, &ctrl->scan_work);
}

int nvme_reset_ctrl(struct nvme_ctrl *ctrl)
{
	if (!nvme_change_ctrl_state(ctrl, NVME_CTRL_RESETTING))
		return -EBUSY;
	if (!queue_work(nvme_reset_wq, &ctrl->reset_work))
		return -EBUSY;
	return 0;
}
EXPORT_SYMBOL_GPL(nvme_reset_ctrl);

int nvme_reset_ctrl_sync(struct nvme_ctrl *ctrl)
{
	int ret;

	ret = nvme_reset_ctrl(ctrl);
	if (!ret) {
		flush_work(&ctrl->reset_work);
		if (ctrl->state != NVME_CTRL_LIVE &&
		    ctrl->state != NVME_CTRL_ADMIN_ONLY)
			ret = -ENETRESET;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(nvme_reset_ctrl_sync);

static void nvme_delete_ctrl_work(struct work_struct *work)
{
	struct nvme_ctrl *ctrl =
		container_of(work, struct nvme_ctrl, delete_work);

	dev_info(ctrl->device,
		 "Removing ctrl: NQN \"%s\"\n", ctrl->opts->subsysnqn);

	flush_work(&ctrl->reset_work);
	nvme_stop_ctrl(ctrl);
	nvme_remove_namespaces(ctrl);
	ctrl->ops->delete_ctrl(ctrl);
	nvme_uninit_ctrl(ctrl);
	nvme_put_ctrl(ctrl);
}

int nvme_delete_ctrl(struct nvme_ctrl *ctrl)
{
	if (!nvme_change_ctrl_state(ctrl, NVME_CTRL_DELETING))
		return -EBUSY;
	if (!queue_work(nvme_delete_wq, &ctrl->delete_work))
		return -EBUSY;
	return 0;
}
EXPORT_SYMBOL_GPL(nvme_delete_ctrl);

int nvme_delete_ctrl_sync(struct nvme_ctrl *ctrl)
{
	int ret = 0;

	/*
	 * Keep a reference until the work is flushed since ->delete_ctrl
	 * can free the controller.
	 */
	nvme_get_ctrl(ctrl);
	ret = nvme_delete_ctrl(ctrl);
	if (!ret)
		flush_work(&ctrl->delete_work);
	nvme_put_ctrl(ctrl);
	return ret;
}
EXPORT_SYMBOL_GPL(nvme_delete_ctrl_sync);

static int nvme_error_status(struct request *req)
{
	if(nvme_req(req)->cmd) {
		// return status directly if kv command
		if(is_kv_cmd(nvme_req(req)->cmd->common.opcode))
			return nvme_req(req)->status;
	}

	switch (nvme_req(req)->status & 0x7ff) {
	case NVME_SC_SUCCESS:
		return 0;
	case NVME_SC_CAP_EXCEEDED:
		return -ENOSPC;
	case NVME_SC_LBA_RANGE:
		return -EREMOTEIO;
	case NVME_SC_BAD_ATTRIBUTES:
	case NVME_SC_INVALID_OPCODE:
	case NVME_SC_INVALID_FIELD:
	case NVME_SC_INVALID_NS:
		return -EOPNOTSUPP;
	case NVME_SC_WRITE_FAULT:
	case NVME_SC_READ_ERROR:
	case NVME_SC_UNWRITTEN_BLOCK:
	case NVME_SC_ACCESS_DENIED:
	case NVME_SC_READ_ONLY:
	case NVME_SC_COMPARE_FAILED:
		return -ENODATA;
	case NVME_SC_GUARD_CHECK:
	case NVME_SC_APPTAG_CHECK:
	case NVME_SC_REFTAG_CHECK:
	case NVME_SC_INVALID_PI:
		return -EILSEQ;
	case NVME_SC_RESERVATION_CONFLICT:
		return -EBADE;
	default:
		return -EIO;
	}
}

static inline bool nvme_req_needs_retry(struct request *req)
{
	if (blk_noretry_request(req))
		return false;
	if (nvme_req(req)->status & NVME_SC_DNR)
		return false;
	if (nvme_req(req)->retries >= nvme_max_retries)
		return false;
	return true;
}

void nvme_complete_rq(struct request *req)
{
	if (unlikely(nvme_req(req)->status && nvme_req_needs_retry(req))) {
		nvme_req(req)->retries++;
		blk_mq_requeue_request(req, true);
		return;
	}

	blk_mq_end_request(req, nvme_error_status(req));
}
EXPORT_SYMBOL_GPL(nvme_complete_rq);

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
	nvme_req(req)->status = status;
	blk_mq_complete_request(req, 0);

}
EXPORT_SYMBOL_GPL(nvme_cancel_request);

bool nvme_change_ctrl_state(struct nvme_ctrl *ctrl,
		enum nvme_ctrl_state new_state)
{
	enum nvme_ctrl_state old_state;
	unsigned long flags;
	bool changed = false;

	spin_lock_irqsave(&ctrl->lock, flags);

	old_state = ctrl->state;
	switch (new_state) {
	case NVME_CTRL_ADMIN_ONLY:
		switch (old_state) {
		case NVME_CTRL_CONNECTING:
			changed = true;
			/* FALLTHRU */
		default:
			break;
		}
		break;
	case NVME_CTRL_LIVE:
		switch (old_state) {
		case NVME_CTRL_NEW:
		case NVME_CTRL_RESETTING:
		case NVME_CTRL_CONNECTING:
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
		case NVME_CTRL_ADMIN_ONLY:
			changed = true;
			/* FALLTHRU */
		default:
			break;
		}
		break;
	case NVME_CTRL_CONNECTING:
		switch (old_state) {
		case NVME_CTRL_NEW:
		case NVME_CTRL_RESETTING:
			changed = true;
			/* FALLTHRU */
		default:
			break;
		}
		break;
	case NVME_CTRL_DELETING:
		switch (old_state) {
		case NVME_CTRL_LIVE:
		case NVME_CTRL_ADMIN_ONLY:
		case NVME_CTRL_RESETTING:
		case NVME_CTRL_CONNECTING:
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

	spin_unlock_irqrestore(&ctrl->lock, flags);

	return changed;
}
EXPORT_SYMBOL_GPL(nvme_change_ctrl_state);

static void nvme_free_ns(struct kref *kref)
{
	struct nvme_ns *ns = container_of(kref, struct nvme_ns, kref);

	put_disk(ns->disk);
	ida_simple_remove(&ns->ctrl->ns_ida, ns->instance);
	nvme_put_ctrl(ns->ctrl);
	kfree(ns);
}

static void nvme_put_ns(struct nvme_ns *ns)
{
	kref_put(&ns->kref, nvme_free_ns);
}

static inline void nvme_clear_nvme_request(struct request *req)
{
	if (!(req->cmd_flags & REQ_DONTPREP)) {
		nvme_req(req)->retries = 0;
		nvme_req(req)->flags = 0;
		req->cmd_flags |= REQ_DONTPREP;
	}
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
	nvme_clear_nvme_request(req);
	req->__data_len = 0;
	req->__sector = (sector_t) -1;
	req->bio = req->biotail = NULL;

	nvme_req(req)->cmd = cmd;

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

	return BLK_MQ_RQ_QUEUE_OK;
}

static inline int nvme_setup_rw(struct nvme_ns *ns, struct request *req,
		struct nvme_command *cmnd)
{
	u16 control = 0;
	u32 dsmgmt = 0;

	/*
	 * If formated with metadata, require the block layer provide a buffer
	 * unless this namespace is formated such that the metadata can be
	 * stripped/generated by the controller with PRACT=1.
	 */
	if (ns && ns->ms && !blk_integrity_rq(req)) {
		if (!(ns->pi_type && ns->ms == 8) &&
		    req->cmd_type != REQ_TYPE_DRV_PRIV) {
			blk_mq_end_request(req, -EFAULT);
			return BLK_MQ_RQ_QUEUE_OK;
		}
	}

	if (req->cmd_flags & REQ_FUA)
		control |= NVME_RW_FUA;
	if (req->cmd_flags & (REQ_FAILFAST_DEV | REQ_RAHEAD))
		control |= NVME_RW_LR;

	if (req->cmd_flags & REQ_RAHEAD)
		dsmgmt |= NVME_RW_DSM_FREQ_PREFETCH;

	memset(cmnd, 0, sizeof(*cmnd));
	cmnd->rw.opcode = (rq_data_dir(req) ? nvme_cmd_write : nvme_cmd_read);
	cmnd->rw.nsid = cpu_to_le32(ns->ns_id);
	cmnd->rw.slba = cpu_to_le64(nvme_block_nr(ns, blk_rq_pos(req)));
	cmnd->rw.length = cpu_to_le16((blk_rq_bytes(req) >> ns->lba_shift) - 1);

	if (ns->ms) {
		if (!blk_integrity_rq(req))
			control |= NVME_RW_PRINFO_PRACT;
	}

	cmnd->rw.control = cpu_to_le16(control);
	cmnd->rw.dsmgmt = cpu_to_le32(dsmgmt);
	return 0;
}

int nvme_setup_cmd(struct nvme_ns *ns, struct request *req,
		struct nvme_command *cmd)
{
	int ret = BLK_MQ_RQ_QUEUE_OK;

	nvme_clear_nvme_request(req);

	if (req->cmd_type == REQ_TYPE_DRV_PRIV)
		memcpy(cmd, nvme_req(req)->cmd, sizeof(*cmd));
	else if (req->cmd_flags & REQ_FLUSH)
		nvme_setup_flush(ns, cmd);
	else if (req->cmd_flags & REQ_DISCARD)
		ret = nvme_setup_discard(ns, req, cmd);
	else
		ret = nvme_setup_rw(ns, req, cmd);

	cmd->common.command_id = req->tag;

	return ret;
}
EXPORT_SYMBOL_GPL(nvme_setup_cmd);

/*
 * Returns 0 on success.  If the result is negative, it's a Linux error code;
 * if the result is positive, it's an NVM Express status code
 */
int __nvme_submit_sync_cmd(struct request_queue *q, struct nvme_command *cmd,
		union nvme_result *result, void *buffer, unsigned bufflen,
		unsigned timeout, int qid, int at_head, int flags)
{
	struct request *req;
	int ret;

	req = nvme_alloc_request(q, cmd, flags, qid);
	if (IS_ERR(req))
		return PTR_ERR(req);

	req->timeout = timeout ? timeout : ADMIN_TIMEOUT;

	if (buffer && bufflen) {
		ret = blk_rq_map_kern(q, req, buffer, bufflen, __GFP_WAIT);
		if (ret)
			goto out;
	}

	blk_execute_rq(req->q, NULL, req, at_head);
	if (result)
		*result = nvme_req(req)->result;
	if (nvme_req(req)->flags & NVME_REQ_CANCELLED)
		ret = -EINTR;
	else
		ret = nvme_req(req)->status;
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
			generic_start_io_acct(req->q, WRITE, (bufflen >> 9 ? bufflen >>9 : 1), &disk->part0);
		} else {
			generic_start_io_acct(req->q, READ, (bufflen >> 9 ? bufflen >>9 : 1), &disk->part0);
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
		blk_execute_rq_nowait(req->q, disk, req, 0, kv_async_completion);
		return 0;
	} else {
		blk_execute_rq(req->q, disk, req, 0);
		//It's different in this kernel version, req->errors does not equal to status
		ret = nvme_req(req)->status;
		if (result)
			*result = le32_to_cpu(nvme_req(req)->result.u32);
		if (status)
			*status = ret;
		if (ret && !is_kv_iter_req_cmd(cmd->common.opcode) && !is_kv_iter_read_cmd(cmd->common.opcode)) {
#if 0
			pr_err("__nvme_submit_user_cmd failed!!!!!: opcode(%02x)\n", cmd->common.opcode);
#endif
		}
	}
	if (need_to_copy) {
		if ((is_kv_retrieve_cmd(cmd->common.opcode) && !ret) ||
				(is_kv_iter_read_cmd(cmd->common.opcode) && (!ret || ((ret & 0x00ff) == 0x0093)))) {
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

	if (is_kv_store_cmd(cmd->common.opcode) || is_kv_append_cmd(cmd->common.opcode)) {
		generic_end_io_acct(req->q, WRITE, &disk->part0, start_time);
	} else {
		generic_end_io_acct(req->q, READ, &disk->part0, start_time);
	}
	blk_mq_free_request(req);
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







static void *nvme_add_user_metadata(struct bio *bio, void __user *ubuf,
		unsigned len, u32 seed, bool write)
{
	struct bio_integrity_payload *bip;
	int ret = -ENOMEM;
	void *buf;

	buf = kmalloc(len, GFP_KERNEL);
	if (!buf)
		goto out;

	ret = -EFAULT;
	if (write && copy_from_user(buf, ubuf, len))
		goto out_free_meta;

	bip = bio_integrity_alloc(bio, GFP_KERNEL, 1);
	if (IS_ERR(bip)) {
		ret = PTR_ERR(bip);
		goto out_free_meta;
	}

	bip->bip_size = len;
	bip->bip_sector = seed;
	ret = bio_integrity_add_page(bio, virt_to_page(buf), len,
			offset_in_page(buf));
	if (ret == len)
		return buf;
	ret = -ENOMEM;
out_free_meta:
	kfree(buf);
out:
	return ERR_PTR(ret);
}

int __nvme_submit_user_cmd(struct request_queue *q, struct nvme_command *cmd,
		void __user *ubuffer, unsigned bufflen,
		void __user *meta_buffer, unsigned meta_len, u32 meta_seed,
		u32 *result, unsigned timeout)
{
	bool write = nvme_is_write(cmd);
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
	nvme_req(req)->flags |= NVME_REQ_USERCMD;

	if (ubuffer && bufflen) {
		ret = blk_rq_map_user(q, req, NULL, ubuffer, bufflen, __GFP_WAIT);
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
		if (disk && meta_buffer && meta_len) {
			meta = nvme_add_user_metadata(bio, meta_buffer, meta_len,
					meta_seed, write);
			if (IS_ERR(meta)) {
				ret = PTR_ERR(meta);
				goto out_unmap;
			}
		}
	}

 submit:
	blk_execute_rq(req->q, disk, req, 0);
	if (nvme_req(req)->flags & NVME_REQ_CANCELLED)
		ret = -EINTR;
	else
		ret = nvme_req(req)->status;
	if (result)
		*result = le32_to_cpu(nvme_req(req)->result.u32);
	if (meta && !ret && !write) {
		if (copy_to_user(meta_buffer, meta, meta_len))
			ret = -EFAULT;
	}
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
	unsigned long flags;
	bool startka = false;

	blk_mq_free_request(rq);

	if (error) {
		dev_err(ctrl->device,
			"failed nvme_keep_alive_end_io error=%d\n", error);
		return;
	}

	spin_lock_irqsave(&ctrl->lock, flags);
	if (ctrl->state == NVME_CTRL_LIVE ||
	    ctrl->state == NVME_CTRL_CONNECTING)
		startka = true;
	spin_unlock_irqrestore(&ctrl->lock, flags);
	if (startka)
		schedule_delayed_work(&ctrl->ka_work, ctrl->kato * HZ);
}

static int nvme_keep_alive(struct nvme_ctrl *ctrl)
{
	struct request *rq;

	rq = nvme_alloc_request(ctrl->admin_q, &ctrl->ka_cmd, BLK_MQ_REQ_RESERVED,
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
		nvme_reset_ctrl(ctrl);
		return;
	}
}

static void nvme_start_keep_alive(struct nvme_ctrl *ctrl)
{
	if (unlikely(ctrl->kato == 0))
		return;

	schedule_delayed_work(&ctrl->ka_work, ctrl->kato * HZ);
}

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
	c.identify.cns = NVME_ID_CNS_CTRL;

	*id = kmalloc(sizeof(struct nvme_id_ctrl), GFP_KERNEL);
	if (!*id)
		return -ENOMEM;

	error = nvme_submit_sync_cmd(dev->admin_q, &c, *id,
			sizeof(struct nvme_id_ctrl));
	if (error)
		kfree(*id);
	return error;
}

static int nvme_identify_ns_descs(struct nvme_ns *ns, unsigned nsid)
{
	struct nvme_command c = { };
	int status;
	void *data;
	int pos;
	int len;

	c.identify.opcode = nvme_admin_identify;
	c.identify.nsid = cpu_to_le32(nsid);
	c.identify.cns = NVME_ID_CNS_NS_DESC_LIST;

	data = kzalloc(NVME_IDENTIFY_DATA_SIZE, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	status = nvme_submit_sync_cmd(ns->ctrl->admin_q, &c, data,
				      NVME_IDENTIFY_DATA_SIZE);
	if (status)
		goto free_data;

	for (pos = 0; pos < NVME_IDENTIFY_DATA_SIZE; pos += len) {
		struct nvme_ns_id_desc *cur = data + pos;

		if (cur->nidl == 0)
			break;

		switch (cur->nidt) {
		case NVME_NIDT_EUI64:
			if (cur->nidl != NVME_NIDT_EUI64_LEN) {
				dev_warn(ns->ctrl->device,
					 "ctrl returned bogus length: %d for NVME_NIDT_EUI64\n",
					 cur->nidl);
				goto free_data;
			}
			len = NVME_NIDT_EUI64_LEN;
			memcpy(ns->eui, data + pos + sizeof(*cur), len);
			break;
		case NVME_NIDT_NGUID:
			if (cur->nidl != NVME_NIDT_NGUID_LEN) {
				dev_warn(ns->ctrl->device,
					 "ctrl returned bogus length: %d for NVME_NIDT_NGUID\n",
					 cur->nidl);
				goto free_data;
			}
			len = NVME_NIDT_NGUID_LEN;
			memcpy(ns->nguid, data + pos + sizeof(*cur), len);
			break;
		case NVME_NIDT_UUID:
			if (cur->nidl != NVME_NIDT_UUID_LEN) {
				dev_warn(ns->ctrl->device,
					 "ctrl returned bogus length: %d for NVME_NIDT_UUID\n",
					 cur->nidl);
				goto free_data;
			}
			len = NVME_NIDT_UUID_LEN;
			memcpy(ns->uuid, data + pos + sizeof(*cur), len);
			break;
		default:
			/* Skip unknown types */
			len = cur->nidl;
			break;
		}

		len += sizeof(*cur);
	}
free_data:
	kfree(data);
	return status;
}

static int nvme_identify_ns_list(struct nvme_ctrl *dev, unsigned nsid, __le32 *ns_list)
{
	struct nvme_command c = { };

	c.identify.opcode = nvme_admin_identify;
	c.identify.cns = NVME_ID_CNS_NS_ACTIVE_LIST;
	c.identify.nsid = cpu_to_le32(nsid);
	return nvme_submit_sync_cmd(dev->admin_q, &c, ns_list,
				    NVME_IDENTIFY_DATA_SIZE);
}

int nvme_identify_ns(struct nvme_ctrl *dev, unsigned nsid,
		struct nvme_id_ns **id)
{
	struct nvme_command c = { };
	int error;

	/* gcc-4.4.4 (at least) has issues with initializers and anon unions */
	c.identify.opcode = nvme_admin_identify;
	c.identify.nsid = cpu_to_le32(nsid);
	c.identify.cns = NVME_ID_CNS_NS;

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
	union nvme_result res;
	int ret;

	memset(&c, 0, sizeof(c));
	c.features.opcode = nvme_admin_get_features;
	c.features.nsid = cpu_to_le32(nsid);
	c.features.fid = cpu_to_le32(fid);

	ret = __nvme_submit_sync_cmd(dev->admin_q, &c, &res, buffer, buflen, 0,
			NVME_QID_ANY, 0, 0);
	if (ret >= 0 && result)
		*result = le32_to_cpu(res.u32);
	return ret;
}

int nvme_set_features(struct nvme_ctrl *dev, unsigned fid, unsigned dword11,
		      void *buffer, size_t buflen, u32 *result)
{
	struct nvme_command c;
	union nvme_result res;
	int ret;

	memset(&c, 0, sizeof(c));
	c.features.opcode = nvme_admin_set_features;
	c.features.fid = cpu_to_le32(fid);
	c.features.dword11 = cpu_to_le32(dword11);

	ret = __nvme_submit_sync_cmd(dev->admin_q, &c, &res,
			buffer, buflen, 0, NVME_QID_ANY, 0, 0);
	if (ret >= 0 && result)
		*result = le32_to_cpu(res.u32);
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
		dev_err(ctrl->device, "Could not set queue count (%d)\n", status);
		*count = 0;
	} else {
		nr_io_queues = min(result & 0xffff, result >> 16) + 1;
		*count = min(*count, nr_io_queues);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(nvme_set_queue_count);

#define NVME_AEN_SUPPORTED \
	(NVME_AEN_CFG_NS_ATTR | NVME_AEN_CFG_FW_ACT)

static void nvme_enable_aen(struct nvme_ctrl *ctrl)
{
	u32 result, supported_aens = ctrl->oaes & NVME_AEN_SUPPORTED;
	int status;

	if (!supported_aens)
		return;

	status = nvme_set_features(ctrl, NVME_FEAT_ASYNC_EVENT, supported_aens,
			NULL, 0, &result);
	if (status)
		dev_warn(ctrl->device, "Failed to configure AEN (cfg %x)\n",
			 supported_aens);
}

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
			metadata, meta_len, lower_32_bits(io.slba), NULL, 0);
}

static u32 nvme_known_admin_effects(u8 opcode)
{
	switch (opcode) {
	case nvme_admin_format_nvm:
		return NVME_CMD_EFFECTS_CSUPP | NVME_CMD_EFFECTS_LBCC |
					NVME_CMD_EFFECTS_CSE_MASK;
	case nvme_admin_sanitize_nvm:
		return NVME_CMD_EFFECTS_CSE_MASK;
	default:
		break;
	}
	return 0;
}

static u32 nvme_passthru_start(struct nvme_ctrl *ctrl, struct nvme_ns *ns,
								u8 opcode)
{
	u32 effects = 0;

	if (ns) {
		if (ctrl->effects)
			effects = le32_to_cpu(ctrl->effects->iocs[opcode]);
		if (effects & ~NVME_CMD_EFFECTS_CSUPP)
			dev_warn(ctrl->device,
				 "IO command:%02x has unhandled effects:%08x\n",
				 opcode, effects);
		return 0;
	}

	if (ctrl->effects)
		effects = le32_to_cpu(ctrl->effects->acs[opcode]);
	else
		effects = nvme_known_admin_effects(opcode);

	/*
	 * For simplicity, IO to all namespaces is quiesced even if the command
	 * effects say only one namespace is affected.
	 */
	if (effects & (NVME_CMD_EFFECTS_LBCC | NVME_CMD_EFFECTS_CSE_MASK)) {
		mutex_lock(&ctrl->scan_lock);
		nvme_start_freeze(ctrl);
		nvme_wait_freeze(ctrl);
	}
	return effects;
}

static void nvme_update_formats(struct nvme_ctrl *ctrl)
{
	struct nvme_ns *ns;

	down_read(&ctrl->namespaces_rwsem);
	list_for_each_entry(ns, &ctrl->namespaces, list)
		if (ns->disk && nvme_revalidate_disk(ns->disk))
			nvme_set_queue_dying(ns);
	up_read(&ctrl->namespaces_rwsem);

	nvme_remove_invalid_namespaces(ctrl, NVME_NSID_ALL);
}

static void nvme_passthru_end(struct nvme_ctrl *ctrl, u32 effects)
{
	/*
	 * Revalidate LBA changes prior to unfreezing. This is necessary to
	 * prevent memory corruption if a logical block size was changed by
	 * this command.
	 */
	if (effects & NVME_CMD_EFFECTS_LBCC)
		nvme_update_formats(ctrl);
	if (effects & (NVME_CMD_EFFECTS_LBCC | NVME_CMD_EFFECTS_CSE_MASK)) {
		nvme_unfreeze(ctrl);
		mutex_unlock(&ctrl->scan_lock);
	}
	if (effects & NVME_CMD_EFFECTS_CCC)
		nvme_init_identify(ctrl);
	if (effects & (NVME_CMD_EFFECTS_NIC | NVME_CMD_EFFECTS_NCC))
		nvme_queue_scan(ctrl);
}

static int nvme_user_cmd(struct nvme_ctrl *ctrl, struct nvme_ns *ns,
			struct nvme_passthru_cmd __user *ucmd)
{
	struct nvme_passthru_cmd cmd;
	struct nvme_command c;
	unsigned timeout = 0;
	u32 effects;
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


	effects = nvme_passthru_start(ctrl, ns, cmd.opcode);
	status = nvme_submit_user_cmd(ns ? ns->queue : ctrl->admin_q, &c,
			(void __user *)(uintptr_t)cmd.addr, cmd.data_len,
			&cmd.result, timeout);
	nvme_passthru_end(ctrl, effects);

	if (status >= 0) {
		if (put_user(cmd.result, &ucmd->result))
			return -EFAULT;
	}

	return status;
}

static int nvme_ioctl(struct block_device *bdev, fmode_t mode, unsigned int cmd,
							unsigned long arg)
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
	struct nvme_ns *ns = bdev->bd_disk->private_data;

	if (!kref_get_unless_zero(&ns->kref))
		goto fail;
	if (!try_module_get(ns->ctrl->ops->module))
		goto fail_put_ns;

	return 0;

fail_put_ns:
	nvme_put_ns(ns);
fail:
	return -ENXIO;
}

static void nvme_release(struct gendisk *disk, fmode_t mode)
{
	struct nvme_ns *ns = disk->private_data;

	module_put(ns->ctrl->ops->module);
	nvme_put_ns(ns);
}

static int nvme_getgeo(struct block_device *bd, struct hd_geometry *geo)
{
	/* some standard values */
	geo->heads = 1 << 6;
	geo->sectors = 1 << 5;
	geo->cylinders = get_capacity(bd->bd_disk) >> 11;
	return 0;
}

#ifdef CONFIG_BLK_DEV_INTEGRITY
static int nvme_noop_verify(struct blk_integrity_exchg *exg)
{
	return 0;
}

static void nvme_noop_generate(struct blk_integrity_exchg *exg)
{
}

struct blk_integrity nvme_meta_noop = {
	.name            = "NVME_META_NOOP",
	.generate_fn        = nvme_noop_generate,
	.verify_fn        = nvme_noop_verify,
};
static void nvme_init_integrity(struct nvme_ns *ns)
{
	nvme_meta_noop.tuple_size = ns->ms;
	blk_integrity_register(ns->disk, &nvme_meta_noop);
	blk_queue_max_integrity_segments(ns->queue, 1);
}
#else /* CONFIG_BLK_DEV_INTEGRITY */
static void nvme_init_integrity(struct nvme_ns *ns)
{
}
#endif /* CONFIG_BLK_DEV_INTEGRITY */

static void nvme_set_chunk_size(struct nvme_ns *ns)
{
	u32 chunk_size = (((u32)ns->noiob) << (ns->lba_shift - 9));
	blk_queue_chunk_sectors(ns->queue, rounddown_pow_of_two(chunk_size));
}

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
	ns->queue->limits.max_discard_sectors = 0xffffffff;
	queue_flag_set_unlocked(QUEUE_FLAG_DISCARD, ns->queue);
}

static int nvme_revalidate_disk(struct gendisk *disk)
{
	struct nvme_ns *ns = disk->private_data;
	struct nvme_id_ns *id;
	u8 lbaf;
	u16 old_ms;
	unsigned short bs;

	if (test_bit(NVME_NS_DEAD, &ns->flags)) {
		set_capacity(disk, 0);
		return -ENODEV;
	}
	if (nvme_identify_ns(ns->ctrl, ns->ns_id, &id)) {
		dev_warn(ns->ctrl->device, "Identify namespace failed\n");
		return -ENODEV;
	}
	if (id->ncap == 0) {
		kfree(id);
		return -ENODEV;
	}

	if (ns->ctrl->vs >= NVME_VS(1, 1, 0))
		memcpy(ns->eui, id->eui64, sizeof(ns->eui));
	if (ns->ctrl->vs >= NVME_VS(1, 2, 0))
		memcpy(ns->nguid, id->nguid, sizeof(ns->nguid));
	if (ns->ctrl->vs >= NVME_VS(1, 3, 0)) {
		 /* Don't treat error as fatal we potentially
		  * already have a NGUID or EUI-64
		  */
		if (nvme_identify_ns_descs(ns, ns->ns_id))
			dev_warn(ns->ctrl->device,
				 "%s: Identify Descriptors failed\n", __func__);
	}

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
	ns->noiob = le16_to_cpu(id->noiob);

	blk_mq_freeze_queue(disk->queue);
	if (blk_get_integrity(disk) && (ns->ms != old_ms ||
				bs != queue_logical_block_size(disk->queue) ||
				(ns->ms && ns->ext)))
		blk_integrity_unregister(disk);

	ns->pi_type = ns->ms == 8 ? id->dps & NVME_NS_DPS_PI_MASK : 0;
	blk_queue_logical_block_size(ns->queue, bs);
	if (ns->noiob)
		nvme_set_chunk_size(ns);

	if (ns->ms && !blk_get_integrity(disk) && (disk->flags & GENHD_FL_UP) &&
								!ns->ext)
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
	u32 cdw10 = nvme_pr_type(type) << 8 | (abort ? 2 : 1);
	return nvme_pr_command(bdev, cdw10, old, new, nvme_cmd_resv_acquire);
}

static int nvme_pr_clear(struct block_device *bdev, u64 key)
{
	u32 cdw10 = 1 | (key ? 1 << 3 : 0);
	return nvme_pr_command(bdev, cdw10, key, 0, nvme_cmd_resv_register);
}

static int nvme_pr_release(struct block_device *bdev, u64 key, enum pr_type type)
{
	u32 cdw10 = nvme_pr_type(type) << 8 | (key ? 1 << 3 : 0);
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
	ctrl->ctrl_config |= NVME_CC_AMS_RR | NVME_CC_SHN_NONE;
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
	unsigned long timeout = jiffies + (ctrl->shutdown_timeout * HZ);
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
	if (ctrl->max_hw_sectors) {
		u32 max_segments =
			(ctrl->max_hw_sectors / (ctrl->page_size >> 9)) + 1;

		max_segments = min_not_zero(max_segments, ctrl->max_segments);
		blk_queue_max_hw_sectors(q, ctrl->max_hw_sectors);
		blk_queue_max_segments(q, min_t(u32, max_segments, USHRT_MAX));
	}
	if ((ctrl->quirks & NVME_QUIRK_STRIPE_SIZE) &&
	    is_power_of_2(ctrl->max_hw_sectors))
		blk_queue_chunk_sectors(q, ctrl->max_hw_sectors);
	if (ctrl->vwc & NVME_CTRL_VWC_PRESENT)
		blk_queue_flush(q, REQ_FLUSH | REQ_FUA);
	blk_queue_virt_boundary(q, ctrl->page_size - 1);
}

static int nvme_configure_timestamp(struct nvme_ctrl *ctrl)
{
	__le64 ts;
	int ret;

	if (!(ctrl->oncs & NVME_CTRL_ONCS_TIMESTAMP))
		return 0;

	ts = cpu_to_le64(ktime_to_ms(ktime_get_real()));
	ret = nvme_set_features(ctrl, NVME_FEAT_TIMESTAMP, 0, &ts, sizeof(ts),
			NULL);
	if (ret)
		dev_warn_once(ctrl->device,
			"could not set timestamp (%d)\n", ret);
	return ret;
}

static int nvme_configure_apst(struct nvme_ctrl *ctrl)
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
	u64 max_lat_us = 0;
	int max_ps = -1;
	int ret;

	/*
	 * If APST isn't supported or if we haven't been initialized yet,
	 * then don't do anything.
	 */
	if (!ctrl->apsta)
		return 0;

	if (ctrl->npss > 31) {
		dev_warn(ctrl->device, "NPSS is invalid; not using APST\n");
		return 0;
	}

	table = kzalloc(sizeof(*table), GFP_KERNEL);
	if (!table)
		return 0;

	if (!ctrl->apst_enabled || ctrl->ps_max_latency_us == 0) {
		/* Turn off APST. */
		apste = 0;
		dev_dbg(ctrl->device, "APST disabled\n");
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

			if (max_ps == -1)
				max_ps = state;

			if (total_latency_us > max_lat_us)
				max_lat_us = total_latency_us;
		}

		apste = 1;

		if (max_ps == -1) {
			dev_dbg(ctrl->device, "APST enabled but no non-operational states are available\n");
		} else {
			dev_dbg(ctrl->device, "APST enabled: max PS = %d, max round-trip latency = %lluus, table = %*phN\n",
				max_ps, max_lat_us, (int)sizeof(*table), table);
		}
	}

	ret = nvme_set_features(ctrl, NVME_FEAT_AUTO_PST, apste,
				table, sizeof(*table), NULL);
	if (ret)
		dev_err(ctrl->device, "failed to set APST feature (%d)\n", ret);

	kfree(table);
	return ret;
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

static void nvme_init_subnqn(struct nvme_subsystem *subsys, struct nvme_ctrl *ctrl,
		struct nvme_id_ctrl *id)
{
	size_t nqnlen;
	int off;

	nqnlen = strnlen(id->subnqn, NVMF_NQN_SIZE);
	if (nqnlen > 0 && nqnlen < NVMF_NQN_SIZE) {
		strlcpy(subsys->subnqn, id->subnqn, NVMF_NQN_SIZE);
		return;
	}

	if (ctrl->vs >= NVME_VS(1, 2, 1))
		dev_warn(ctrl->device, "missing or invalid SUBNQN field.\n");

	/* Generate a "fake" NQN per Figure 254 in NVMe 1.3 + ECN 001 */
	off = snprintf(subsys->subnqn, NVMF_NQN_SIZE,
			"nqn.2014.08.org.nvmexpress:%4x%4x",
			le16_to_cpu(id->vid), le16_to_cpu(id->ssvid));
	memcpy(subsys->subnqn + off, id->sn, sizeof(id->sn));
	off += sizeof(id->sn);
	memcpy(subsys->subnqn + off, id->mn, sizeof(id->mn));
	off += sizeof(id->mn);
	memset(subsys->subnqn + off, 0, sizeof(subsys->subnqn) - off);
}

static void __nvme_release_subsystem(struct nvme_subsystem *subsys)
{
	ida_simple_remove(&nvme_subsystems_ida, subsys->instance);
	kfree(subsys);
}

static void nvme_release_subsystem(struct device *dev)
{
	__nvme_release_subsystem(container_of(dev, struct nvme_subsystem, dev));
}

static void nvme_destroy_subsystem(struct kref *ref)
{
	struct nvme_subsystem *subsys =
			container_of(ref, struct nvme_subsystem, ref);

	mutex_lock(&nvme_subsystems_lock);
	list_del(&subsys->entry);
	mutex_unlock(&nvme_subsystems_lock);

	device_del(&subsys->dev);
	put_device(&subsys->dev);
}

static void nvme_put_subsystem(struct nvme_subsystem *subsys)
{
	kref_put(&subsys->ref, nvme_destroy_subsystem);
}

static struct nvme_subsystem *__nvme_find_get_subsystem(const char *subsysnqn)
{
	struct nvme_subsystem *subsys;

	lockdep_assert_held(&nvme_subsystems_lock);

	list_for_each_entry(subsys, &nvme_subsystems, entry) {
		if (strcmp(subsys->subnqn, subsysnqn))
			continue;
		if (!kref_get_unless_zero(&subsys->ref))
			continue;
		return subsys;
	}

	return NULL;
}

#define SUBSYS_ATTR_RO(_name, _mode, _show)			\
	struct device_attribute subsys_attr_##_name = \
		__ATTR(_name, _mode, _show, NULL)

static ssize_t nvme_subsys_show_nqn(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct nvme_subsystem *subsys =
		container_of(dev, struct nvme_subsystem, dev);

	return snprintf(buf, PAGE_SIZE, "%s\n", subsys->subnqn);
}
static SUBSYS_ATTR_RO(subsysnqn, S_IRUGO, nvme_subsys_show_nqn);

#define nvme_subsys_show_str_function(field)				\
static ssize_t subsys_##field##_show(struct device *dev,		\
			    struct device_attribute *attr, char *buf)	\
{									\
	struct nvme_subsystem *subsys =					\
		container_of(dev, struct nvme_subsystem, dev);		\
	return sprintf(buf, "%.*s\n",					\
		       (int)sizeof(subsys->field), subsys->field);	\
}									\
static SUBSYS_ATTR_RO(field, S_IRUGO, subsys_##field##_show);

nvme_subsys_show_str_function(model);
nvme_subsys_show_str_function(serial);
nvme_subsys_show_str_function(firmware_rev);

static struct attribute *nvme_subsys_attrs[] = {
	&subsys_attr_model.attr,
	&subsys_attr_serial.attr,
	&subsys_attr_firmware_rev.attr,
	&subsys_attr_subsysnqn.attr,
	NULL,
};

static struct attribute_group nvme_subsys_attrs_group = {
	.attrs = nvme_subsys_attrs,
};

static const struct attribute_group *nvme_subsys_attrs_groups[] = {
	&nvme_subsys_attrs_group,
	NULL,
};

static int nvme_active_ctrls(struct nvme_subsystem *subsys)
{
	int count = 0;
	struct nvme_ctrl *ctrl;

	mutex_lock(&subsys->lock);
	list_for_each_entry(ctrl, &subsys->ctrls, subsys_entry) {
		if (ctrl->state != NVME_CTRL_DELETING &&
		    ctrl->state != NVME_CTRL_DEAD)
			count++;
	}
	mutex_unlock(&subsys->lock);

	return count;
}

static int nvme_init_subsystem(struct nvme_ctrl *ctrl, struct nvme_id_ctrl *id)
{
	struct nvme_subsystem *subsys, *found;
	int ret;

	subsys = kzalloc(sizeof(*subsys), GFP_KERNEL);
	if (!subsys)
		return -ENOMEM;
	ret = ida_simple_get(&nvme_subsystems_ida, 0, 0, GFP_KERNEL);
	if (ret < 0) {
		kfree(subsys);
		return ret;
	}
	subsys->instance = ret;
	mutex_init(&subsys->lock);
	kref_init(&subsys->ref);
	INIT_LIST_HEAD(&subsys->ctrls);
	nvme_init_subnqn(subsys, ctrl, id);
	memcpy(subsys->serial, id->sn, sizeof(subsys->serial));
	memcpy(subsys->model, id->mn, sizeof(subsys->model));
	memcpy(subsys->firmware_rev, id->fr, sizeof(subsys->firmware_rev));
	subsys->vendor_id = le16_to_cpu(id->vid);
	subsys->cmic = id->cmic;

	subsys->dev.class = nvme_subsys_class;
	subsys->dev.release = nvme_release_subsystem;
	subsys->dev.groups = nvme_subsys_attrs_groups;
	dev_set_name(&subsys->dev, "nvme-subsys%d", subsys->instance);
	device_initialize(&subsys->dev);

	mutex_lock(&nvme_subsystems_lock);
	found = __nvme_find_get_subsystem(subsys->subnqn);
	if (found) {
		/*
		 * Verify that the subsystem actually supports multiple
		 * controllers, else bail out.
		 */
		if (nvme_active_ctrls(found) && !(id->cmic & (1 << 1))) {
			dev_err(ctrl->device,
				"ignoring ctrl due to duplicate subnqn (%s).\n",
				found->subnqn);
			nvme_put_subsystem(found);
			ret = -EINVAL;
			goto out_unlock;
		}

		__nvme_release_subsystem(subsys);
		subsys = found;
	} else {
		ret = device_add(&subsys->dev);
		if (ret) {
			dev_err(ctrl->device,
				"failed to register subsystem device.\n");
			goto out_unlock;
		}
		list_add_tail(&subsys->entry, &nvme_subsystems);
	}

	ctrl->subsys = subsys;
	mutex_unlock(&nvme_subsystems_lock);

	if (sysfs_create_link(&subsys->dev.kobj, &ctrl->device->kobj,
			dev_name(ctrl->device))) {
		dev_err(ctrl->device,
			"failed to create sysfs link from subsystem.\n");
		/* the transport driver will eventually put the subsystem */
		return -EINVAL;
	}

	mutex_lock(&subsys->lock);
	list_add_tail(&ctrl->subsys_entry, &subsys->ctrls);
	mutex_unlock(&subsys->lock);

	return 0;

out_unlock:
	mutex_unlock(&nvme_subsystems_lock);
	put_device(&subsys->dev);
	return ret;
}

int nvme_get_log(struct nvme_ctrl *ctrl, u32 nsid, u8 log_page, u8 lsp,
		void *log, size_t size, u64 offset)
{
	struct nvme_command c = { };
	unsigned long dwlen = size / 4 - 1;

	c.get_log_page.opcode = nvme_admin_get_log_page;
	c.get_log_page.nsid = cpu_to_le32(nsid);
	c.get_log_page.lid = log_page;
	c.get_log_page.lsp = lsp;
	c.get_log_page.numdl = cpu_to_le16(dwlen & ((1 << 16) - 1));
	c.get_log_page.numdu = cpu_to_le16(dwlen >> 16);
	c.get_log_page.lpol = cpu_to_le32(lower_32_bits(offset));
	c.get_log_page.lpou = cpu_to_le32(upper_32_bits(offset));

	return nvme_submit_sync_cmd(ctrl->admin_q, &c, log, size);
}

static int nvme_get_effects_log(struct nvme_ctrl *ctrl)
{
	int ret;

	if (!ctrl->effects)
		ctrl->effects = kzalloc(sizeof(*ctrl->effects), GFP_KERNEL);

	if (!ctrl->effects)
		return 0;

	ret = nvme_get_log(ctrl, NVME_NSID_ALL, NVME_LOG_CMD_EFFECTS, 0,
			ctrl->effects, sizeof(*ctrl->effects), 0);
	if (ret) {
		kfree(ctrl->effects);
		ctrl->effects = NULL;
	}
	return ret;
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
	bool prev_apst_enabled;

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

	if (id->lpa & NVME_CTRL_LPA_CMD_EFFECTS_LOG) {
		ret = nvme_get_effects_log(ctrl);
		if (ret < 0)
			goto out_free;
	}

	if (!ctrl->identified) {
		int i;

		ret = nvme_init_subsystem(ctrl, id);
		if (ret)
			goto out_free;

		/*
		 * Check for quirks.  Quirk can depend on firmware version,
		 * so, in principle, the set of quirks present can change
		 * across a reset.  As a possible future enhancement, we
		 * could re-scan for quirks every time we reinitialize
		 * the device, but we'd have to make sure that the driver
		 * behaves intelligently if the quirks change.
		 */
		for (i = 0; i < ARRAY_SIZE(core_quirks); i++) {
			if (quirk_matches(id, &core_quirks[i]))
				ctrl->quirks |= core_quirks[i].quirks;
		}
	}

	if (force_apst && (ctrl->quirks & NVME_QUIRK_NO_DEEPEST_PS)) {
		dev_warn(ctrl->device, "forcibly allowing all power states due to nvme_core.force_apst -- use at your own risk\n");
		ctrl->quirks &= ~NVME_QUIRK_NO_DEEPEST_PS;
	}

	ctrl->oacs = le16_to_cpu(id->oacs);
	ctrl->oncs = le16_to_cpup(&id->oncs);
	ctrl->oaes = le32_to_cpu(id->oaes);
	atomic_set(&ctrl->abort_limit, id->acl + 1);
	ctrl->vwc = id->vwc;
	ctrl->cntlid = le16_to_cpup(&id->cntlid);
	if (id->mdts)
		max_hw_sectors = 1 << (id->mdts + page_shift - 9);
	else
		max_hw_sectors = UINT_MAX;
	ctrl->max_hw_sectors =
		min_not_zero(ctrl->max_hw_sectors, max_hw_sectors);

	nvme_set_queue_limits(ctrl, ctrl->admin_q);
	ctrl->sgls = le32_to_cpu(id->sgls);
	ctrl->kas = le16_to_cpu(id->kas);

	if (id->rtd3e) {
		/* us -> s */
		u32 transition_time = le32_to_cpu(id->rtd3e) / 1000000;

		ctrl->shutdown_timeout = clamp_t(unsigned int, transition_time,
						 shutdown_timeout, 60);

		if (ctrl->shutdown_timeout != shutdown_timeout)
			dev_info(ctrl->device,
				 "Shutdown timeout set to %u seconds\n",
				 ctrl->shutdown_timeout);
	} else
		ctrl->shutdown_timeout = shutdown_timeout;

	ctrl->npss = id->npss;
	ctrl->apsta = id->apsta;
	prev_apst_enabled = ctrl->apst_enabled;
	if (ctrl->quirks & NVME_QUIRK_NO_APST) {
		if (force_apst && id->apsta) {
			dev_warn(ctrl->device, "forcibly allowing APST due to nvme_core.force_apst -- use at your own risk\n");
			ctrl->apst_enabled = true;
		} else {
			ctrl->apst_enabled = false;
		}
	} else {
		ctrl->apst_enabled = id->apsta;
	}
	memcpy(ctrl->psd, id->psd, sizeof(ctrl->psd));

	if (ctrl->ops->flags & NVME_F_FABRICS) {
		ctrl->icdoff = le16_to_cpu(id->icdoff);
		ctrl->ioccsz = le32_to_cpu(id->ioccsz);
		ctrl->iorcsz = le32_to_cpu(id->iorcsz);
		ctrl->maxcmd = le16_to_cpu(id->maxcmd);

		/*
		 * In fabrics we need to verify the cntlid matches the
		 * admin connect
		 */
		if (ctrl->cntlid != le16_to_cpu(id->cntlid)) {
			ret = -EINVAL;
			goto out_free;
		}

		if (!ctrl->opts->discovery_nqn && !ctrl->kas) {
			dev_err(ctrl->device,
				"keep-alive support is mandatory for fabrics\n");
			ret = -EINVAL;
			goto out_free;
		}
	} else {
		ctrl->cntlid = le16_to_cpu(id->cntlid);
		ctrl->hmpre = le32_to_cpu(id->hmpre);
		ctrl->hmmin = le32_to_cpu(id->hmmin);
		ctrl->hmminds = le32_to_cpu(id->hmminds);
		ctrl->hmmaxd = le16_to_cpu(id->hmmaxd);
	}

	kfree(id);

	if (ctrl->apst_enabled && !prev_apst_enabled)
		dev_pm_qos_expose_latency_tolerance(ctrl->device);
	else if (!ctrl->apst_enabled && prev_apst_enabled)
		dev_pm_qos_hide_latency_tolerance(ctrl->device);

	ret = nvme_configure_apst(ctrl);
	if (ret < 0)
		return ret;
	
	ret = nvme_configure_timestamp(ctrl);
	if (ret < 0)
		return ret;

	ctrl->identified = true;

	return 0;

out_free:
	kfree(id);
	return ret;
}
EXPORT_SYMBOL_GPL(nvme_init_identify);

static int nvme_dev_open(struct inode *inode, struct file *file)
{
	struct nvme_ctrl *ctrl =
		container_of(inode->i_cdev, struct nvme_ctrl, cdev);

	switch (ctrl->state) {
	case NVME_CTRL_LIVE:
	case NVME_CTRL_ADMIN_ONLY:
		break;
	default:
		return -EWOULDBLOCK;
	}

	file->private_data = ctrl;
	return 0;
}

static int nvme_dev_user_cmd(struct nvme_ctrl *ctrl, void __user *argp)
{
	struct nvme_ns *ns;
	int ret;

	down_read(&ctrl->namespaces_rwsem);
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
	up_read(&ctrl->namespaces_rwsem);

	ret = nvme_user_cmd(ctrl, ns, argp);
	nvme_put_ns(ns);
	return ret;

out_unlock:
	up_read(&ctrl->namespaces_rwsem);
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
		return nvme_reset_ctrl_sync(ctrl);
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
	.unlocked_ioctl	= nvme_dev_ioctl,
	.compat_ioctl	= nvme_dev_ioctl,
};

static ssize_t nvme_sysfs_reset(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);
	int ret;

	ret = nvme_reset_ctrl_sync(ctrl);
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
	struct nvme_ns *ns = dev_to_disk(dev)->private_data;
	struct nvme_subsystem *subsys = ns->ctrl->subsys;
	int serial_len = sizeof(subsys->serial);
	int model_len = sizeof(subsys->model);
	static const u8 null_uuid[16];

	if (memcmp(ns->uuid, null_uuid, 16))
		return sprintf(buf, "uuid.%pU\n", &ns->uuid);

	if (memchr_inv(ns->nguid, 0, sizeof(ns->nguid)))
		return sprintf(buf, "eui.%16phN\n", ns->nguid);

	if (memchr_inv(ns->eui, 0, sizeof(ns->eui)))
		return sprintf(buf, "eui.%8phN\n", ns->eui);

	while (serial_len > 0 && (subsys->serial[serial_len - 1] == ' ' ||
				  subsys->serial[serial_len - 1] == '\0'))
		serial_len--;
	while (model_len > 0 && (subsys->model[model_len - 1] == ' ' ||
				 subsys->model[model_len - 1] == '\0'))
		model_len--;

	return sprintf(buf, "nvme.%04x-%*phN-%*phN-%08x\n", subsys->vendor_id,
		serial_len, subsys->serial, model_len, subsys->model,
		ns->ns_id);
}
static DEVICE_ATTR(wwid, S_IRUGO, wwid_show, NULL);

static ssize_t nguid_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct nvme_ns *ns = dev_to_disk(dev)->private_data;
	return sprintf(buf, "%pU\n", ns->nguid);
}
static DEVICE_ATTR(nguid, S_IRUGO, nguid_show, NULL);

static ssize_t uuid_show(struct device *dev, struct device_attribute *attr,
								char *buf)
{
	struct nvme_ns *ns = dev_to_disk(dev)->private_data;
	static const u8 null_uuid[16];

	/* For backward compatibility expose the NGUID to userspace if
	 * we have no UUID set
	 */
	if (!memcmp(ns->uuid, null_uuid, 16)) {
		printk_ratelimited(KERN_WARNING
				   "No UUID available providing old NGUID\n");
		return sprintf(buf, "%pU\n", ns->nguid);
	}
	return sprintf(buf, "%pU\n", &ns->uuid);
}
static DEVICE_ATTR(uuid, S_IRUGO, uuid_show, NULL);

static ssize_t eui_show(struct device *dev, struct device_attribute *attr,
								char *buf)
{
	struct nvme_ns *ns = dev_to_disk(dev)->private_data;
	return sprintf(buf, "%8ph\n", ns->eui);
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
	&dev_attr_wwid.attr,
	&dev_attr_uuid.attr,
	&dev_attr_nguid.attr,
	&dev_attr_eui.attr,
	&dev_attr_nsid.attr,
	NULL,
};

static umode_t nvme_ns_attrs_are_visible(struct kobject *kobj,
		struct attribute *a, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct nvme_ns *ns = dev_to_disk(dev)->private_data;
	static const u8 null_uuid[16];

	if (a == &dev_attr_uuid.attr) {
	  if (!memcmp(ns->uuid, null_uuid, 16) &&
		    !memchr_inv(ns->nguid, 0, sizeof(ns->nguid)))
			return 0;
	}
	if (a == &dev_attr_nguid.attr) {
		if (!memchr_inv(ns->nguid, 0, sizeof(ns->nguid)))
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

const struct attribute_group *nvme_ns_id_attr_groups[] = {
	&nvme_ns_attr_group,
	NULL,
};

#define nvme_show_str_function(field)						\
static ssize_t  field##_show(struct device *dev,				\
			    struct device_attribute *attr, char *buf)		\
{										\
        struct nvme_ctrl *ctrl = dev_get_drvdata(dev);				\
        return sprintf(buf, "%.*s\n",						\
		(int)sizeof(ctrl->subsys->field), ctrl->subsys->field);		\
}										\
static DEVICE_ATTR(field, S_IRUGO, field##_show, NULL);

nvme_show_str_function(model);
nvme_show_str_function(serial);
nvme_show_str_function(firmware_rev);

#define nvme_show_int_function(field)						\
static ssize_t  field##_show(struct device *dev,				\
			    struct device_attribute *attr, char *buf)		\
{										\
        struct nvme_ctrl *ctrl = dev_get_drvdata(dev);				\
        return sprintf(buf, "%d\n", ctrl->field);	\
}										\
static DEVICE_ATTR(field, S_IRUGO, field##_show, NULL);

nvme_show_int_function(cntlid);

static ssize_t nvme_sysfs_delete(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);

	if (device_remove_file_self(dev, attr))
		nvme_delete_ctrl_sync(ctrl);
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

static ssize_t nvme_sysfs_show_state(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);
	static const char *const state_name[] = {
		[NVME_CTRL_NEW]		= "new",
		[NVME_CTRL_LIVE]	= "live",
		[NVME_CTRL_ADMIN_ONLY]	= "only-admin",
		[NVME_CTRL_RESETTING]	= "resetting",
		[NVME_CTRL_CONNECTING]	= "connecting",
		[NVME_CTRL_DELETING]	= "deleting",
		[NVME_CTRL_DEAD]	= "dead",
	};

	if ((unsigned)ctrl->state < ARRAY_SIZE(state_name) &&
	    state_name[ctrl->state])
		return sprintf(buf, "%s\n", state_name[ctrl->state]);

	return sprintf(buf, "unknown state\n");
}

static DEVICE_ATTR(state, S_IRUGO, nvme_sysfs_show_state, NULL);

static ssize_t nvme_sysfs_show_subsysnqn(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%s\n", ctrl->subsys->subnqn);
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
	&dev_attr_state.attr,
	NULL
};

static umode_t nvme_dev_attrs_are_visible(struct kobject *kobj,
		struct attribute *a, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);

	if (a == &dev_attr_delete_controller.attr && !ctrl->ops->delete_ctrl)
		return 0;
	if (a == &dev_attr_address.attr && !ctrl->ops->get_address)
		return 0;

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

	down_read(&ctrl->namespaces_rwsem);
	list_for_each_entry(ns, &ctrl->namespaces, list) {
		if (ns->ns_id == nsid) {
			if (!kref_get_unless_zero(&ns->kref))
				continue;
			ret = ns;
			break;
		}
		if (ns->ns_id > nsid)
			break;
	}
	up_read(&ctrl->namespaces_rwsem);
	return ret;
}

static void nvme_alloc_ns(struct nvme_ctrl *ctrl, unsigned nsid)
{
	struct nvme_ns *ns;
	struct gendisk *disk;
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

	disk = alloc_disk_node(0, node);
	if (!disk)
		goto out_free_queue;

	kref_init(&ns->kref);
	ns->ns_id = nsid;
	ns->disk = disk;
	ns->lba_shift = 9; /* set to a default value for 512 until disk is validated */


	blk_queue_logical_block_size(ns->queue, 1 << ns->lba_shift);
	nvme_set_queue_limits(ctrl, ns->queue);

	disk->fops = &nvme_fops;
	disk->private_data = ns;
	disk->queue = ns->queue;
	disk->driverfs_dev = ctrl->device;
	disk->flags = GENHD_FL_EXT_DEVT;
	sprintf(disk->disk_name, "nvme%dn%d", ctrl->instance, ns->instance);

	if (nvme_revalidate_disk(ns->disk))
		goto out_free_disk;

	down_write(&ctrl->namespaces_rwsem);
	list_add_tail(&ns->list, &ctrl->namespaces);
	up_write(&ctrl->namespaces_rwsem);

	nvme_get_ctrl(ctrl);

	add_disk_with_attributes(ns->disk, nvme_ns_id_attr_groups);
	return;
 out_free_disk:
	kfree(disk);
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

	if (ns->disk->flags & GENHD_FL_UP) {
		del_gendisk(ns->disk);
		blk_cleanup_queue(ns->queue);
		if (blk_get_integrity(ns->disk))
			blk_integrity_unregister(ns->disk);
	}

	down_write(&ns->ctrl->namespaces_rwsem);
	list_del_init(&ns->list);
	up_write(&ns->ctrl->namespaces_rwsem);

	nvme_put_ns(ns);
}

static void nvme_validate_ns(struct nvme_ctrl *ctrl, unsigned nsid)
{
	struct nvme_ns *ns;

	ns = nvme_find_get_ns(ctrl, nsid);
	if (ns) {
		if (revalidate_disk(ns->disk))
			nvme_ns_remove(ns);
		nvme_put_ns(ns);
	} else
		nvme_alloc_ns(ctrl, nsid);
}

static void nvme_remove_invalid_namespaces(struct nvme_ctrl *ctrl,
					unsigned nsid)
{
	struct nvme_ns *ns, *next;
	LIST_HEAD(rm_list);

	down_write(&ctrl->namespaces_rwsem);
	list_for_each_entry_safe(ns, next, &ctrl->namespaces, list) {
		if (ns->ns_id > nsid || test_bit(NVME_NS_DEAD, &ns->flags))
			list_move_tail(&ns->list, &rm_list);
	}
	up_write(&ctrl->namespaces_rwsem);

	list_for_each_entry_safe(ns, next, &rm_list, list)
		nvme_ns_remove(ns);

}

static int nvme_scan_ns_list(struct nvme_ctrl *ctrl, unsigned nn)
{
	struct nvme_ns *ns;
	__le32 *ns_list;
	unsigned i, j, nsid, prev = 0, num_lists = DIV_ROUND_UP(nn, 1024);
	int ret = 0;

	ns_list = kzalloc(NVME_IDENTIFY_DATA_SIZE, GFP_KERNEL);
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

static void nvme_clear_changed_ns_log(struct nvme_ctrl *ctrl)
{
	size_t log_size = NVME_MAX_CHANGED_NAMESPACES * sizeof(__le32);
	__le32 *log;
	int error;

	log = kzalloc(log_size, GFP_KERNEL);
	if (!log)
		return;

	/*
	 * We need to read the log to clear the AEN, but we don't want to rely
	 * on it for the changed namespace information as userspace could have
	 * raced with us in reading the log page, which could cause us to miss
	 * updates.
	 */
	error = nvme_get_log(ctrl, NVME_NSID_ALL, NVME_LOG_CHANGED_NS, 0, log,
			log_size, 0);
	if (error)
		dev_warn(ctrl->device,
			"reading changed ns log failed: %d\n", error);

	kfree(log);
}

static void nvme_scan_work(struct work_struct *work)
{
	struct nvme_ctrl *ctrl =
		container_of(work, struct nvme_ctrl, scan_work);
	struct nvme_id_ctrl *id;
	unsigned nn;

	if (ctrl->state != NVME_CTRL_LIVE)
		return;

	WARN_ON_ONCE(!ctrl->tagset);

	if (test_and_clear_bit(NVME_AER_NOTICE_NS_CHANGED, &ctrl->events)) {
		dev_info(ctrl->device, "rescanning namespaces.\n");
		nvme_clear_changed_ns_log(ctrl);
	}

	if (nvme_identify_ctrl(ctrl, &id))
		return;

	mutex_lock(&ctrl->scan_lock);
	nn = le32_to_cpu(id->nn);
	if (ctrl->vs >= NVME_VS(1, 1, 0) &&
	    !(ctrl->quirks & NVME_QUIRK_IDENTIFY_CNS)) {
		if (!nvme_scan_ns_list(ctrl, nn))
			goto out_free_id;
	}
	nvme_scan_ns_sequential(ctrl, nn);
out_free_id:
	mutex_unlock(&ctrl->scan_lock);
	kfree(id);
	down_write(&ctrl->namespaces_rwsem);
	list_sort(NULL, &ctrl->namespaces, ns_cmp);
	up_write(&ctrl->namespaces_rwsem);
}

/*
 * This function iterates the namespace list unlocked to allow recovery from
 * controller failure. It is up to the caller to ensure the namespace list is
 * not modified by scan work while this function is executing.
 */
void nvme_remove_namespaces(struct nvme_ctrl *ctrl)
{
	struct nvme_ns *ns, *next;
	LIST_HEAD(ns_list);

	/* prevent racing with ns scanning */
	flush_work(&ctrl->scan_work);

	/*
	 * The dead states indicates the controller was not gracefully
	 * disconnected. In that case, we won't be able to flush any data while
	 * removing the namespaces' disks; fail all the queues now to avoid
	 * potentially having to clean up the failed sync later.
	 */
	if (ctrl->state == NVME_CTRL_DEAD)
		nvme_kill_queues(ctrl);

	down_write(&ctrl->namespaces_rwsem);
	list_splice_init(&ctrl->namespaces, &ns_list);
	up_write(&ctrl->namespaces_rwsem);

	list_for_each_entry_safe(ns, next, &ns_list, list)
		nvme_ns_remove(ns);
}
EXPORT_SYMBOL_GPL(nvme_remove_namespaces);

static void nvme_aen_uevent(struct nvme_ctrl *ctrl)
{
	char *envp[2] = { NULL, NULL };
	u32 aen_result = ctrl->aen_result;

	ctrl->aen_result = 0;
	if (!aen_result)
		return;

	envp[0] = kasprintf(GFP_KERNEL, "NVME_AEN=%#08x", aen_result);
	if (!envp[0])
		return;
	kobject_uevent_env(&ctrl->device->kobj, KOBJ_CHANGE, envp);
	kfree(envp[0]);
}

static void nvme_async_event_work(struct work_struct *work)
{
	struct nvme_ctrl *ctrl =
		container_of(work, struct nvme_ctrl, async_event_work);

	nvme_aen_uevent(ctrl);
	ctrl->ops->submit_async_event(ctrl);
}

static bool nvme_ctrl_pp_status(struct nvme_ctrl *ctrl)
{

	u32 csts;

	if (ctrl->ops->reg_read32(ctrl, NVME_REG_CSTS, &csts))
		return false;

	if (csts == ~0)
		return false;

	return ((ctrl->ctrl_config & NVME_CC_ENABLE) && (csts & NVME_CSTS_PP));
}

static void nvme_get_fw_slot_info(struct nvme_ctrl *ctrl)
{
	struct nvme_fw_slot_info_log *log;

	log = kmalloc(sizeof(*log), GFP_KERNEL);
	if (!log)
		return;

	if (nvme_get_log(ctrl, NVME_NSID_ALL, 0, NVME_LOG_FW_SLOT, log,
			sizeof(*log), 0))
		dev_warn(ctrl->device, "Get FW SLOT INFO log error\n");
	kfree(log);
}

static void nvme_fw_act_work(struct work_struct *work)
{
	struct nvme_ctrl *ctrl = container_of(work,
				struct nvme_ctrl, fw_act_work);
	unsigned long fw_act_timeout;

	if (ctrl->mtfa)
		fw_act_timeout = jiffies +
				msecs_to_jiffies(ctrl->mtfa * 100);
	else
		fw_act_timeout = jiffies +
				msecs_to_jiffies(admin_timeout * 1000);

	nvme_stop_queues(ctrl);
	while (nvme_ctrl_pp_status(ctrl)) {
		if (time_after(jiffies, fw_act_timeout)) {
			dev_warn(ctrl->device,
				"Fw activation timeout, reset controller\n");
			nvme_reset_ctrl(ctrl);
			break;
		}
		msleep(100);
	}

	if (ctrl->state != NVME_CTRL_LIVE)
		return;

	nvme_start_queues(ctrl);
	/* read FW slot information to clear the AER */
	nvme_get_fw_slot_info(ctrl);
}

static void nvme_handle_aen_notice(struct nvme_ctrl *ctrl, u32 result)
{
	switch ((result & 0xff00) >> 8) {
	case NVME_AER_NOTICE_NS_CHANGED:
		set_bit(NVME_AER_NOTICE_NS_CHANGED, &ctrl->events);
		nvme_queue_scan(ctrl);
		break;
	case NVME_AER_NOTICE_FW_ACT_STARTING:
		queue_work(nvme_wq, &ctrl->fw_act_work);
		break;
	default:
		dev_warn(ctrl->device, "async event result %08x\n", result);
	}
}

void nvme_complete_async_event(struct nvme_ctrl *ctrl, __le16 status,
		volatile union nvme_result *res)
{
	u32 result = le32_to_cpu(res->u32);

	if (le16_to_cpu(status) >> 1 != NVME_SC_SUCCESS)
		return;

	switch (result & 0x7) {
	case NVME_AER_NOTICE:
		nvme_handle_aen_notice(ctrl, result);
		break;
	case NVME_AER_ERROR:
	case NVME_AER_SMART:
	case NVME_AER_CSS:
	case NVME_AER_VS:
		ctrl->aen_result = result;
		break;
	default:
		break;
	}
	queue_work(nvme_wq, &ctrl->async_event_work);
}
EXPORT_SYMBOL_GPL(nvme_complete_async_event);

void nvme_stop_ctrl(struct nvme_ctrl *ctrl)
{
	nvme_stop_keep_alive(ctrl);
	flush_work(&ctrl->async_event_work);
	cancel_work_sync(&ctrl->fw_act_work);
	if (ctrl->ops->stop_ctrl)
		ctrl->ops->stop_ctrl(ctrl);
}
EXPORT_SYMBOL_GPL(nvme_stop_ctrl);

void nvme_start_ctrl(struct nvme_ctrl *ctrl)
{
	if (ctrl->kato)
		nvme_start_keep_alive(ctrl);

	if (ctrl->queue_count > 1) {
		nvme_queue_scan(ctrl);
		nvme_enable_aen(ctrl);
		queue_work(nvme_wq, &ctrl->async_event_work);
		nvme_start_queues(ctrl);
	}
}
EXPORT_SYMBOL_GPL(nvme_start_ctrl);

void nvme_uninit_ctrl(struct nvme_ctrl *ctrl)
{
	dev_pm_qos_hide_latency_tolerance(ctrl->device);
	cdev_device_del(&ctrl->cdev, ctrl->device);
}
EXPORT_SYMBOL_GPL(nvme_uninit_ctrl);

static void nvme_free_ctrl(struct device *dev)
{
	struct nvme_ctrl *ctrl =
		container_of(dev, struct nvme_ctrl, ctrl_device);
	struct nvme_subsystem *subsys = ctrl->subsys;

	ida_simple_remove(&nvme_instance_ida, ctrl->instance);
	ida_destroy(&ctrl->ns_ida);
	kfree(ctrl->effects);

	if (subsys) {
		mutex_lock(&subsys->lock);
		list_del(&ctrl->subsys_entry);
		mutex_unlock(&subsys->lock);
		sysfs_remove_link(&subsys->dev.kobj, dev_name(ctrl->device));
	}

	ctrl->ops->free_ctrl(ctrl);

	if (subsys)
		nvme_put_subsystem(subsys);
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

	ctrl->state = NVME_CTRL_NEW;
	spin_lock_init(&ctrl->lock);
	mutex_init(&ctrl->scan_lock);
	INIT_LIST_HEAD(&ctrl->namespaces);
	init_rwsem(&ctrl->namespaces_rwsem);
	ctrl->dev = dev;
	ctrl->ops = ops;
	ctrl->quirks = quirks;
	INIT_WORK(&ctrl->scan_work, nvme_scan_work);
	INIT_WORK(&ctrl->async_event_work, nvme_async_event_work);
	INIT_WORK(&ctrl->fw_act_work, nvme_fw_act_work);
	INIT_WORK(&ctrl->delete_work, nvme_delete_ctrl_work);

	INIT_DELAYED_WORK(&ctrl->ka_work, nvme_keep_alive_work);
	memset(&ctrl->ka_cmd, 0, sizeof(ctrl->ka_cmd));
	ctrl->ka_cmd.common.opcode = nvme_admin_keep_alive;

	ret = ida_simple_get(&nvme_instance_ida, 0, 0, GFP_KERNEL);
	if (ret < 0)
		goto out;
	ctrl->instance = ret;

	device_initialize(&ctrl->ctrl_device);
	ctrl->device = &ctrl->ctrl_device;
	ctrl->device->devt = MKDEV(MAJOR(nvme_chr_devt), ctrl->instance);
	ctrl->device->class = nvme_class;
	ctrl->device->parent = ctrl->dev;
	ctrl->device->groups = nvme_dev_attr_groups;
	ctrl->device->release = nvme_free_ctrl;
	dev_set_drvdata(ctrl->device, ctrl);
	ret = dev_set_name(ctrl->device, "nvme%d", ctrl->instance);
	if (ret)
		goto out_release_instance;

	cdev_init(&ctrl->cdev, &nvme_dev_fops);
	ctrl->cdev.owner = ops->module;
	ret = cdev_device_add(&ctrl->cdev, ctrl->device);
	if (ret)
		goto out_free_name;

	ida_init(&ctrl->ns_ida);

	/*
	 * Initialize latency tolerance controls.  The sysfs files won't
	 * be visible to userspace unless the device actually supports APST.
	 */
	ctrl->device->device_rh->power.set_latency_tolerance = nvme_set_latency_tolerance;
	dev_pm_qos_update_user_latency_tolerance(ctrl->device,
		min(default_ps_max_latency_us, (unsigned long)S32_MAX));

	return 0;
out_free_name:
	kfree_const(ctrl->device->kobj.name);
out_release_instance:
	ida_simple_remove(&nvme_instance_ida, ctrl->instance);
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

	down_read(&ctrl->namespaces_rwsem);

	/* Forcibly unquiesce queues to avoid blocking dispatch */
	if (ctrl->admin_q && !blk_queue_dying(ctrl->admin_q))
 		blk_mq_unquiesce_queue(ctrl->admin_q);

	list_for_each_entry(ns, &ctrl->namespaces, list)
		nvme_set_queue_dying(ns);

	up_read(&ctrl->namespaces_rwsem);
}
EXPORT_SYMBOL_GPL(nvme_kill_queues);

void nvme_unfreeze(struct nvme_ctrl *ctrl)
{
	struct nvme_ns *ns;

	down_read(&ctrl->namespaces_rwsem);
	list_for_each_entry(ns, &ctrl->namespaces, list)
		blk_mq_unfreeze_queue(ns->queue);
	up_read(&ctrl->namespaces_rwsem);
}
EXPORT_SYMBOL_GPL(nvme_unfreeze);

void nvme_wait_freeze_timeout(struct nvme_ctrl *ctrl, long timeout)
{
	struct nvme_ns *ns;

	down_read(&ctrl->namespaces_rwsem);
	list_for_each_entry(ns, &ctrl->namespaces, list) {
		timeout = blk_mq_freeze_queue_wait_timeout(ns->queue, timeout);
		if (timeout <= 0)
			break;
	}
	up_read(&ctrl->namespaces_rwsem);
}
EXPORT_SYMBOL_GPL(nvme_wait_freeze_timeout);

void nvme_wait_freeze(struct nvme_ctrl *ctrl)
{
	struct nvme_ns *ns;

	down_read(&ctrl->namespaces_rwsem);
	list_for_each_entry(ns, &ctrl->namespaces, list)
		blk_mq_freeze_queue_wait(ns->queue);
	up_read(&ctrl->namespaces_rwsem);
}
EXPORT_SYMBOL_GPL(nvme_wait_freeze);

void nvme_start_freeze(struct nvme_ctrl *ctrl)
{
	struct nvme_ns *ns;

	down_read(&ctrl->namespaces_rwsem);
	list_for_each_entry(ns, &ctrl->namespaces, list)
		blk_freeze_queue_start(ns->queue);
	up_read(&ctrl->namespaces_rwsem);
}
EXPORT_SYMBOL_GPL(nvme_start_freeze);

void nvme_stop_queues(struct nvme_ctrl *ctrl)
{
	struct nvme_ns *ns;

	down_read(&ctrl->namespaces_rwsem);
	list_for_each_entry(ns, &ctrl->namespaces, list) {
		spin_lock_irq(ns->queue->queue_lock);
		queue_flag_set(QUEUE_FLAG_STOPPED, ns->queue);
		spin_unlock_irq(ns->queue->queue_lock);

		blk_mq_quiesce_queue(ns->queue);
	}
	up_read(&ctrl->namespaces_rwsem);
}
EXPORT_SYMBOL_GPL(nvme_stop_queues);

void nvme_start_queues(struct nvme_ctrl *ctrl)
{
	struct nvme_ns *ns;

	down_read(&ctrl->namespaces_rwsem);
	list_for_each_entry(ns, &ctrl->namespaces, list) {
		queue_flag_clear_unlocked(QUEUE_FLAG_STOPPED, ns->queue);
		blk_mq_unquiesce_queue(ns->queue);
	}
	up_read(&ctrl->namespaces_rwsem);
}
EXPORT_SYMBOL_GPL(nvme_start_queues);

int __init nvme_core_init(void)
{
	int result = -ENOMEM;

	nvme_wq = alloc_workqueue("nvme-wq",
			WQ_UNBOUND | WQ_MEM_RECLAIM | WQ_SYSFS, 0);
	if (!nvme_wq)
		goto out;

	nvme_reset_wq = alloc_workqueue("nvme-reset-wq",
			WQ_UNBOUND | WQ_MEM_RECLAIM | WQ_SYSFS, 0);
	if (!nvme_reset_wq)
		goto destroy_wq;

	nvme_delete_wq = alloc_workqueue("nvme-delete-wq",
			WQ_UNBOUND | WQ_MEM_RECLAIM | WQ_SYSFS, 0);
	if (!nvme_delete_wq)
		goto destroy_reset_wq;

	result = alloc_chrdev_region(&nvme_chr_devt, 0, NVME_MINORS, "nvme");
	if (result < 0)
		goto destroy_delete_wq;

	nvme_class = class_create(THIS_MODULE, "nvme");
	if (IS_ERR(nvme_class)) {
		result = PTR_ERR(nvme_class);
		goto unregister_chrdev;
	}

	nvme_subsys_class = class_create(THIS_MODULE, "nvme-subsystem");
	if (IS_ERR(nvme_subsys_class)) {
		result = PTR_ERR(nvme_subsys_class);
		goto destroy_class;
	}

#if 1
	result = aio_service_init();
	if (result)
		goto destroy_class;

	result = aio_worker_init();
	if (result)
		goto unregister_aio_service;
#endif
	return 0;
#if 1
unregister_aio_service:
	aio_service_exit();
#endif
destroy_class:
	class_destroy(nvme_class);
unregister_chrdev:
	unregister_chrdev_region(nvme_chr_devt, NVME_MINORS);
destroy_delete_wq:
	destroy_workqueue(nvme_delete_wq);
destroy_reset_wq:
	destroy_workqueue(nvme_reset_wq);
destroy_wq:
	destroy_workqueue(nvme_wq);
out:
	return result;
}

void nvme_core_exit(void)
{
	ida_destroy(&nvme_subsystems_ida);
	class_destroy(nvme_subsys_class);
	class_destroy(nvme_class);
	unregister_chrdev_region(nvme_chr_devt, NVME_MINORS);
	destroy_workqueue(nvme_delete_wq);
	destroy_workqueue(nvme_reset_wq);
	destroy_workqueue(nvme_wq);
#if 1
	aio_worker_exit();
	aio_service_exit();
#endif

}

MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
module_init(nvme_core_init);
module_exit(nvme_core_exit);
