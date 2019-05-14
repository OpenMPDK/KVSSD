/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
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

#define	KVS_SUCCESS		0
#define KVS_ERR_ALIGNMENT	(-1)
#define KVS_ERR_CAPAPCITY	(-2)
#define KVS_ERR_CLOSE	(-3)
#define KVS_ERR_CONT_EXIST	(-4)
#define KVS_ERR_CONT_NAME	(-5)
#define KVS_ERR_CONT_NOT_EXIST	(-6)
#define KVS_ERR_DEVICE_NOT_EXIST (-7)
#define KVS_ERR_GROUP	(-8)
#define KVS_ERR_INDEX	(-9)
#define KVS_ERR_IO	(-10)
#define KVS_ERR_KEY	(-11)
#define KVS_ERR_KEY_TYPE	(-12)
#define KVS_ERR_MEMORY	(-13)
#define KVS_ERR_NULL_INPUT	(-14)
#define KVS_ERR_OFFSET	(-15)
#define KVS_ERR_OPEN	(-16)
#define KVS_ERR_OPTION_NOT_SUPPORTED	(-17)
#define KVS_ERR_PERMISSION	(-18)
#define KVS_ERR_SPACE	(-19)
#define KVS_ERR_TIMEOUT	(-20)
#define KVS_ERR_TUPLE_EXIST	(-21)
#define KVS_ERR_TUPLE_NOT_EXIST	(-22)
#define KVS_ERR_VALUE	(-23)


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
