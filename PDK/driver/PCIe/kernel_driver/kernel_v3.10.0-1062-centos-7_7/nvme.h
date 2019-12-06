/*
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

#ifndef _NVME_H
#define _NVME_H

#if 1
//#define KV_NVME_DUMMY_OP
//#define KV_NVME_DUMMY_OP_REPORT
#define KSID_SUPPORT
#endif

#if 1
#include "linux_nvme.h"
#else
#include <linux/nvme.h>
#endif
#include <linux/cdev.h>
#include <linux/pci.h>
#include <linux/kref.h>
#include <linux/blk-mq.h>
#include <linux/idr.h>

#if 1
#define is_kv_append_cmd(opcode)	((opcode) == nvme_cmd_kv_append)
#define is_kv_store_cmd(opcode)	((opcode) == nvme_cmd_kv_store)
#define is_kv_retrieve_cmd(opcode)	((opcode) == nvme_cmd_kv_retrieve)
#define is_kv_delete_cmd(opcode)	((opcode) == nvme_cmd_kv_delete)
#define is_kv_iter_req_cmd(opcode)	((opcode) == nvme_cmd_kv_iter_req)
#define is_kv_iter_read_cmd(opcode)	((opcode) == nvme_cmd_kv_iter_read)
#define is_kv_exist_cmd(opcode)	((opcode) == nvme_cmd_kv_exist)
#define is_kv_cmd(opcode)	(is_kv_store_cmd(opcode) || is_kv_append_cmd(opcode) ||\
		is_kv_retrieve_cmd(opcode) || is_kv_delete_cmd(opcode) ||\
        is_kv_iter_req_cmd(opcode) || is_kv_iter_read_cmd(opcode) ||\
		is_kv_exist_cmd(opcode))
#endif

extern unsigned int nvme_io_timeout;
#define NVME_IO_TIMEOUT	(nvme_io_timeout * HZ)

extern unsigned int admin_timeout;
#define ADMIN_TIMEOUT	(admin_timeout * HZ)

#define NVME_DEFAULT_KATO	5
#define NVME_KATO_GRACE		10

extern struct workqueue_struct *nvme_wq;
extern struct workqueue_struct *nvme_reset_wq;
extern struct workqueue_struct *nvme_delete_wq;

/*
 * List of workarounds for devices that required behavior not specified in
 * the standard.
 */
enum nvme_quirks {
	/*
	 * Prefers I/O aligned to a stripe size specified in a vendor
	 * specific Identify field.
	 */
	NVME_QUIRK_STRIPE_SIZE			= (1 << 0),

	/*
	 * The controller doesn't handle Identify value others than 0 or 1
	 * correctly.
	 */
	NVME_QUIRK_IDENTIFY_CNS			= (1 << 1),

	/*
	 * The controller deterministically returns O's on reads to discarded
	 * logical blocks.
	 */
	NVME_QUIRK_DISCARD_ZEROES		= (1 << 2),

	/*
	 * The controller needs a delay before starts checking the device
	 * readiness, which is done by reading the NVME_CSTS_RDY bit.
	 */
	NVME_QUIRK_DELAY_BEFORE_CHK_RDY		= (1 << 3),

	/*
	 * APST should not be used.
	 */
	NVME_QUIRK_NO_APST			= (1 << 4),

	/*
	 * The deepest sleep state should not be used.
	 */
	NVME_QUIRK_NO_DEEPEST_PS		= (1 << 5),

	/*
	 * Set MEDIUM priority on SQ creation
	 */
	NVME_QUIRK_MEDIUM_PRIO_SQ		= (1 << 7),
};

/*
 * Common request structure for NVMe passthrough.  All drivers must have
 * this structure as the first member of their request-private data.
 */
struct nvme_request {
	struct nvme_command	*cmd;
	union nvme_result	result;
	u8			retries;
	u8			flags;
	u16			status;
	struct nvme_ctrl	*ctrl;
};

enum {
	NVME_REQ_CANCELLED		= (1 << 0),
	NVME_REQ_USERCMD		= (1 << 1),
};

static inline struct nvme_request *nvme_req(struct request *req)
{
	return blk_mq_rq_to_pdu(req);
}

#if 1
struct nvme_io_param {
    struct scatterlist* kv_data_sg_ptr;
    struct scatterlist* kv_meta_sg_ptr;
    int kv_data_nents;
    int kv_data_len;
};

static inline struct nvme_io_param *nvme_io_param(struct request *req)
{
	return (struct nvme_io_param *)(blk_mq_rq_to_pdu(req) + sizeof(struct nvme_request));
}
#endif





/* The below value is the specific amount of delay needed before checking
 * readiness in case of the PCI_DEVICE(0x1c58, 0x0003), which needs the
 * NVME_QUIRK_DELAY_BEFORE_CHK_RDY quirk enabled. The value (in ms) was
 * found empirically.
 */
#define NVME_QUIRK_DELAY_AMOUNT		2300

enum nvme_ctrl_state {
	NVME_CTRL_NEW,
	NVME_CTRL_LIVE,
	NVME_CTRL_ADMIN_ONLY,    /* Only admin queue live */
	NVME_CTRL_RESETTING,
	NVME_CTRL_CONNECTING,
	NVME_CTRL_DELETING,
	NVME_CTRL_DEAD,
};

struct nvme_ctrl {
	enum nvme_ctrl_state state;
	bool identified;
	spinlock_t lock;
	struct mutex scan_lock;
	const struct nvme_ctrl_ops *ops;
	struct request_queue *admin_q;
	struct request_queue *connect_q;
	struct device *dev;
	int instance;
	struct blk_mq_tag_set *tagset;
	struct blk_mq_tag_set *admin_tagset;
	struct list_head namespaces;
	struct rw_semaphore namespaces_rwsem;
	struct device ctrl_device;
	struct device *device;	/* char device */
	struct cdev cdev;
	struct ida ns_ida;
	struct work_struct reset_work;
	struct work_struct delete_work;

	struct nvme_subsystem *subsys;
	struct list_head subsys_entry;

	char name[12];
	char serial[20];
	char model[40];
	char firmware_rev[8];
	u16 cntlid;

	u32 ctrl_config;
	u16 mtfa;
	u32 queue_count;

	u64 cap;
	u32 page_size;
	u32 max_hw_sectors;
	u32 max_segments;
	u16 oncs;
	u16 oacs;
	atomic_t abort_limit;
	u8 vwc;
	u32 vs;
	u32 sgls;
	u16 kas;
	u8 npss;
	u8 apsta;
	u32 oaes;
	u32 aen_result;
	unsigned int shutdown_timeout;
	unsigned int kato;
	bool subsystem;
	unsigned long quirks;
	struct nvme_id_power_state psd[32];
	struct nvme_effects_log *effects;
	struct work_struct scan_work;
	struct work_struct async_event_work;
	struct delayed_work ka_work;
	struct nvme_command ka_cmd;
	struct work_struct fw_act_work;
	unsigned long events;

	/* Power saving configuration */
	u64 ps_max_latency_us;
	bool apst_enabled;

	/* PCIe only: */
	u32 hmpre;
	u32 hmmin;
	u32 hmminds;
	u16 hmmaxd;

	/* Fabrics only */
	u16 sqsize;
	u32 ioccsz;
	u32 iorcsz;
	u16 icdoff;
	u16 maxcmd;
	int nr_reconnects;
	struct nvmf_ctrl_options *opts;
};

struct nvme_subsystem {
	int			instance;
	struct device		dev;
	/*
	 * Because we unregister the device on the last put we need
	 * a separate refcount.
	 */
	struct kref		ref;
	struct list_head	entry;
	struct mutex		lock;
	struct list_head	ctrls;
	char			subnqn[NVMF_NQN_SIZE];
	char			serial[20];
	char			model[40];
	char			firmware_rev[8];
	u8			cmic;
	u16			vendor_id;
};

struct nvme_ns {
	struct list_head list;

	struct nvme_ctrl *ctrl;
	struct request_queue *queue;
	struct gendisk *disk;
	struct kref kref;
	int instance;

	u8 eui[8];
	u8 nguid[16];
	u8 uuid[16];

	unsigned ns_id;
	int lba_shift;
	u16 ms;
	bool ext;
	u8 pi_type;
	unsigned long flags;
	u16 noiob;

#define NVME_NS_REMOVING 0
#define NVME_NS_DEAD     1

	u64 mode_select_num_blocks;
	u32 mode_select_block_len;
};

struct nvme_ctrl_ops {
	const char *name;
	struct module *module;
	unsigned int flags;
#define NVME_F_FABRICS			(1 << 0)
	int (*reg_read32)(struct nvme_ctrl *ctrl, u32 off, u32 *val);
	int (*reg_write32)(struct nvme_ctrl *ctrl, u32 off, u32 val);
	int (*reg_read64)(struct nvme_ctrl *ctrl, u32 off, u64 *val);
	void (*free_ctrl)(struct nvme_ctrl *ctrl);
	void (*submit_async_event)(struct nvme_ctrl *ctrl);
	void (*delete_ctrl)(struct nvme_ctrl *ctrl);
	int (*get_address)(struct nvme_ctrl *ctrl, char *buf, int size);
	void (*stop_ctrl)(struct nvme_ctrl *ctrl);
};

static inline bool nvme_ctrl_ready(struct nvme_ctrl *ctrl)
{
	u32 val = 0;

	if (ctrl->ops->reg_read32(ctrl, NVME_REG_CSTS, &val))
		return false;
	return val & NVME_CSTS_RDY;
}

static inline int nvme_reset_subsystem(struct nvme_ctrl *ctrl)
{
	if (!ctrl->subsystem)
		return -ENOTTY;
	return ctrl->ops->reg_write32(ctrl, NVME_REG_NSSR, 0x4E564D65);
}

static inline u64 nvme_block_nr(struct nvme_ns *ns, sector_t sector)
{
	return (sector >> (ns->lba_shift - 9));
}

static inline unsigned nvme_map_len(struct request *rq)
{
	if (rq->cmd_flags & REQ_DISCARD)
		return sizeof(struct nvme_dsm_range);
	else
		return blk_rq_bytes(rq);
}

static inline void nvme_cleanup_cmd(struct request *req)
{
	if (req->cmd_flags & REQ_DISCARD)
		kfree(req->completion_data);
}

static inline void nvme_end_request(struct request *req, __le16 status,
		union nvme_result result)
{
	struct nvme_request *rq = nvme_req(req);

	rq->status = le16_to_cpu(status) >> 1;
	rq->result = result;
	blk_mq_complete_request(req, 0);
}

static inline void nvme_get_ctrl(struct nvme_ctrl *ctrl)
{
	get_device(ctrl->device);
}

static inline void nvme_put_ctrl(struct nvme_ctrl *ctrl)
{
	put_device(ctrl->device);
}

void nvme_complete_rq(struct request *req);
void nvme_cancel_request(struct request *req, void *data, bool reserved);
bool nvme_change_ctrl_state(struct nvme_ctrl *ctrl,
		enum nvme_ctrl_state new_state);
int nvme_disable_ctrl(struct nvme_ctrl *ctrl, u64 cap);
int nvme_enable_ctrl(struct nvme_ctrl *ctrl, u64 cap);
int nvme_shutdown_ctrl(struct nvme_ctrl *ctrl);
int nvme_init_ctrl(struct nvme_ctrl *ctrl, struct device *dev,
		const struct nvme_ctrl_ops *ops, unsigned long quirks);
void nvme_uninit_ctrl(struct nvme_ctrl *ctrl);
void nvme_start_ctrl(struct nvme_ctrl *ctrl);
void nvme_stop_ctrl(struct nvme_ctrl *ctrl);
void nvme_put_ctrl(struct nvme_ctrl *ctrl);
int nvme_init_identify(struct nvme_ctrl *ctrl);

void nvme_remove_namespaces(struct nvme_ctrl *ctrl);

void nvme_complete_async_event(struct nvme_ctrl *ctrl, __le16 status,
		volatile union nvme_result *res);

void nvme_stop_queues(struct nvme_ctrl *ctrl);
void nvme_start_queues(struct nvme_ctrl *ctrl);
void nvme_kill_queues(struct nvme_ctrl *ctrl);
void nvme_unfreeze(struct nvme_ctrl *ctrl);
void nvme_wait_freeze(struct nvme_ctrl *ctrl);
void nvme_wait_freeze_timeout(struct nvme_ctrl *ctrl, long timeout);
void nvme_start_freeze(struct nvme_ctrl *ctrl);

#define NVME_QID_ANY -1
struct request *nvme_alloc_request(struct request_queue *q,
		struct nvme_command *cmd, unsigned int flags, int qid);
int nvme_setup_cmd(struct nvme_ns *ns, struct request *req,
		struct nvme_command *cmd);
int nvme_submit_sync_cmd(struct request_queue *q, struct nvme_command *cmd,
		void *buf, unsigned bufflen);
int __nvme_submit_sync_cmd(struct request_queue *q, struct nvme_command *cmd,
		union nvme_result *result, void *buffer, unsigned bufflen,
		unsigned timeout, int qid, int at_head, int flags);
int nvme_submit_user_cmd(struct request_queue *q, struct nvme_command *cmd,
		void __user *ubuffer, unsigned bufflen, u32 *result,
		unsigned timeout);
int __nvme_submit_user_cmd(struct request_queue *q, struct nvme_command *cmd,
		void __user *ubuffer, unsigned bufflen,
		void __user *meta_buffer, unsigned meta_len, u32 meta_seed,
		u32 *result, unsigned timeout);
int nvme_identify_ctrl(struct nvme_ctrl *dev, struct nvme_id_ctrl **id);
int nvme_identify_ns(struct nvme_ctrl *dev, unsigned nsid,
		struct nvme_id_ns **id);
int nvme_get_log_page(struct nvme_ctrl *dev, struct nvme_smart_log **log);
int nvme_get_features(struct nvme_ctrl *dev, unsigned fid, unsigned nsid,
		      void *buffer, size_t buflen, u32 *result);
int nvme_set_features(struct nvme_ctrl *dev, unsigned fid, unsigned dword11,
		      void *buffer, size_t buflen, u32 *result);
int nvme_set_queue_count(struct nvme_ctrl *ctrl, int *count);
void nvme_stop_keep_alive(struct nvme_ctrl *ctrl);
int nvme_reset_ctrl(struct nvme_ctrl *ctrl);
int nvme_reset_ctrl_sync(struct nvme_ctrl *ctrl);
int nvme_delete_ctrl(struct nvme_ctrl *ctrl);
int nvme_delete_ctrl_sync(struct nvme_ctrl *ctrl);

int nvme_get_log(struct nvme_ctrl *ctrl, u32 nsid, u8 log_page, u8 lsp,
		void *log, size_t size, u64 offset);

struct sg_io_hdr;

int nvme_sg_io(struct nvme_ns *ns, struct sg_io_hdr __user *u_hdr);
int nvme_sg_io32(struct nvme_ns *ns, unsigned long arg);
int nvme_sg_get_version_num(int __user *ip);

int __init nvme_core_init(void);
void nvme_core_exit(void);

#endif /* _NVME_H */
