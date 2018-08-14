/**
 *   BSD LICENSE
 *
 *   Copyright (c) 2017 Samsung Electronics Co., Ltd.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Samsung Electronics Co., Ltd. nor the names of
 *       its contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** @file kv_trace.h
 *  @brief Defines macros for debug print functions
 *
 *  This file contains the macro definitions for the debug print functions used by the KV API.
 *  @bug No known bugs.
 */

#ifndef __KVNVME_TRACE_H__
#define __KVNVME_TRACE_H__

#include <sys/time.h>

#define	KVNVME_LOG_ERR			0x01
#define	KVNVME_LOG_WARN			0x02
#define	KVNVME_LOG_INFO			0x03
#define	KVNVME_LOG_DEBUG		0x04

#define KVNVME_TIMESTART(time)	\
	do {	\
		struct timezone tz; \
		gettimeofday(&time, &tz); \
	} while(0)

#define KVNVME_TIMESTOP(time)	\
	do	{	\
		struct timezone tz; \
		struct timeval tv; \
		gettimeofday(&tv, &tz); \
		time.tv_sec = tv.tv_sec - time.tv_sec; \
		time.tv_usec = tv.tv_usec- time.tv_usec; \
		if (time.tv_usec < 0) { time.tv_sec--; time.tv_usec+=1000000; } \
		printf("[%ld ms]\n", time.tv_sec*1000+time.tv_usec/1000); \
	} while(0)

#define KVNVME_TRACE(log_level, fmt, args...)	\
	(void)((log_level <= KVNVME_LOG_LEVEL) ?	\
	printf("%s:: %d:: %s:: "fmt "\n", __FILE__, __LINE__, __FUNCTION__, ## args) :	\
	0)

// Error Messages
#define KVNVME_ERR(fmt, args...)	\
	do {	\
		KVNVME_TRACE(KVNVME_LOG_ERR, fmt, ## args);	\
	} while(0)

// Warning Messages
#define KVNVME_WARN(fmt, args...)	\
	do {	\
		KVNVME_TRACE(KVNVME_LOG_WARN, fmt, ## args);	\
	} while(0)

// Info Messages
#define KVNVME_INFO(fmt, args...)	\
	do {	\
		KVNVME_TRACE(KVNVME_LOG_INFO, fmt, ## args);	\
	} while(0)

// Debug Messages
#define KVNVME_DEBUG(fmt, args...)	\
	do {	\
		KVNVME_TRACE(KVNVME_LOG_DEBUG, fmt, ## args);	\
	} while(0)

#define ENTER()	\
	do {	\
		KVNVME_DEBUG("Enter");	\
	} while(0)

#define LEAVE()	\
	do {	\
		KVNVME_DEBUG("Leave");	\
	} while(0)

#endif /* _KVNVME_TRACE_H_ */
