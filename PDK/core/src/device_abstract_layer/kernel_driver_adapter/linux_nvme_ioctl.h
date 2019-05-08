/*
 * Definitions for the NVM Express ioctl interface
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

#ifndef _UAPI_LINUX_NVME_IOCTL_H
#define _UAPI_LINUX_NVME_IOCTL_H

#include <linux/types.h>

struct nvme_user_io {
	__u8	opcode;
	__u8	flags;
	__u16	control;
	__u16	nblocks;
	__u16	rsvd;
	__u64	metadata;
	__u64	addr;
	__u64	slba;
	__u32	dsmgmt;
	__u32	reftag;
	__u16	apptag;
	__u16	appmask;
};

struct nvme_passthru_cmd {
	__u8	opcode;
	__u8	flags;
	__u16	rsvd1;
	__u32	nsid;
	__u32	cdw2;
	__u32	cdw3;
	__u64	metadata;
	__u64	addr;
	__u32	metadata_len;
	__u32	data_len;
	__u32	cdw10;
	__u32	cdw11;
	__u32	cdw12;
	__u32	cdw13;
	__u32	cdw14;
	__u32	cdw15;
	__u32	timeout_ms;
	__u32	result;
};

struct nvme_passthru_kv_cmd {
	__u8	opcode;
	__u8	flags;
	__u16	rsvd1;
	__u32	nsid;
	__u32	cdw2;
	__u32	cdw3;
	__u32	cdw4;
	__u32	cdw5;
	__u64	data_addr;
	__u32	data_length;
	__u32	key_length;
	__u32	cdw10;
	__u32	cdw11;
	union {
		struct {
			__u64 key_addr;
			__u32 rsvd5;
			__u32 rsvd6;
		};
		__u8 key[16];
		struct {
			__u32 cdw12;
			__u32 cdw13;
			__u32 cdw14;
			__u32 cdw15;
		};
	};
	__u32	timeout_ms;
	__u32	result;
	__u32	status;
	__u32	ctxid;
	__u64	reqid;
};

struct nvme_aioctx {
	__u32	ctxid;
	__u32	eventfd;
};


struct nvme_aioevent {
	__u64	reqid;
	__u32	ctxid;
	__u32	result;
	__u16	status;
};

#define MAX_AIO_EVENTS	128
struct nvme_aioevents {
	__u16	nr;
    __u32   ctxid;
	struct nvme_aioevent events[MAX_AIO_EVENTS];
};


#define nvme_admin_cmd nvme_passthru_cmd

#define NVME_IOCTL_ID		_IO('N', 0x40)
#define NVME_IOCTL_ADMIN_CMD	_IOWR('N', 0x41, struct nvme_admin_cmd)
#define NVME_IOCTL_SUBMIT_IO	_IOW('N', 0x42, struct nvme_user_io)
#define NVME_IOCTL_IO_CMD	_IOWR('N', 0x43, struct nvme_passthru_cmd)
#define NVME_IOCTL_RESET	_IO('N', 0x44)
#define NVME_IOCTL_SUBSYS_RESET	_IO('N', 0x45)
#define NVME_IOCTL_RESCAN	_IO('N', 0x46)

#define NVME_IOCTL_AIO_CMD		_IOWR('N', 0x47, struct nvme_passthru_kv_cmd)
#define NVME_IOCTL_SET_AIOCTX	_IOWR('N', 0x48, struct nvme_aioctx)
#define NVME_IOCTL_DEL_AIOCTX	_IOWR('N', 0x49, struct nvme_aioctx)
#define NVME_IOCTL_GET_AIOEVENT	_IOWR('N', 0x50, struct nvme_aioevents)
#define NVME_IOCTL_IO_KV_CMD	_IOWR('N', 0x51, struct nvme_passthru_kv_cmd)
#endif /* _UAPI_LINUX_NVME_IOCTL_H */
