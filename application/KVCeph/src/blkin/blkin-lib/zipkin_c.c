/*
 * Copyright 2014 Marios Kogias <marioskogias@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 *   1. Redistributions of source code must retain the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer.
 *   2. Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials
 *      provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY COPYRIGHT HOLDERS ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include "zipkin_c.h"
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#define TRACEPOINT_DEFINE
#include <zipkin_trace.h>

const char *default_ip = "NaN";
const char *default_name = "NoName";

const char* const CLIENT_SEND = "cs";
const char* const CLIENT_RECV = "cr";
const char* const SERVER_SEND = "ss";
const char* const SERVER_RECV = "sr";
const char* const WIRE_SEND = "ws";
const char* const WIRE_RECV = "wr";
const char* const CLIENT_SEND_FRAGMENT = "csf";
const char* const CLIENT_RECV_FRAGMENT = "crf";
const char* const SERVER_SEND_FRAGMENT = "ssf";
const char* const SERVER_RECV_FRAGMENT = "srf";

static int64_t random_big()
{
	int64_t a;
	a = rand();
	a = a << 32;
	int b = rand();
	a = a + b;
	if (a<0)
		a = !a;
	return a;
}

int blkin_init()
{
	static pthread_mutex_t blkin_init_mutex = PTHREAD_MUTEX_INITIALIZER;
	static int initialized = 0;

	/*
	 * Initialize srand with sth appropriete
	 * time is not good for archipelago: several deamons -> same timstamp
	 */
	pthread_mutex_lock(&blkin_init_mutex);
	if (!initialized) {
		int inf, seed;
		inf = open("/dev/urandom", O_RDONLY); //file descriptor 1
		read(inf, &seed, sizeof(int));
		close(inf);
		srand(seed);
		initialized = 1;
	}
	pthread_mutex_unlock(&blkin_init_mutex);
	return 0;
}

int blkin_init_new_trace(struct blkin_trace *new_trace, const char *service,
			 const struct blkin_endpoint *endpoint)
{
	int res;
	if (!new_trace) {
		res = -EINVAL;
		goto OUT;
	}
	new_trace->name = service;
	blkin_init_trace_info(&(new_trace->info));
	new_trace->endpoint = endpoint;
	res = 0;

OUT:
	return res;
}

void blkin_init_trace_info(struct blkin_trace_info *trace_info)
{
	trace_info->span_id = trace_info->trace_id = random_big();
	trace_info->parent_span_id = 0;
}

int blkin_init_child_info(struct blkin_trace *child,
			  const struct blkin_trace_info *parent_info,
			  const struct blkin_endpoint *endpoint,
			  const char *child_name)
{
	int res;
	if ((!child) || (!parent_info) || (!endpoint)){
		res = -EINVAL;
		goto OUT;
	}
	child->info.trace_id = parent_info->trace_id;
	child->info.span_id = random_big();
	child->info.parent_span_id = parent_info->span_id;
	child->name = child_name;
	child->endpoint = endpoint;
	res = 0;

OUT:
	return res;
}

int blkin_init_child(struct blkin_trace *child,
		     const struct blkin_trace *parent,
		     const struct blkin_endpoint *endpoint,
		     const char *child_name)
{
	int res;
	if (!parent) {
		res = -EINVAL;
		goto OUT;
	}
	if (!endpoint)
		endpoint = parent->endpoint;
	if (blkin_init_child_info(child, &parent->info, endpoint, child_name) != 0){
		res = -EINVAL;
		goto OUT;
	}
	res = 0;

OUT:
	return res;
}

int blkin_init_endpoint(struct blkin_endpoint *endp, const char *ip,
			int16_t port, const char *name)
{
	int res;
	if (!endp){
		res = -EINVAL;
		goto OUT;
	}
	if (!ip)
		ip = default_ip;

	endp->ip = ip;
	endp->port = port;
	endp->name = name;
	res = 0;

OUT:
	return res;
}

int blkin_init_string_annotation(struct blkin_annotation *annotation,
				 const char *key, const char *val, const struct blkin_endpoint *endpoint)
{
	int res;
	if ((!annotation) || (!key) || (!val)){
		res = -EINVAL;
		goto OUT;
	}
	annotation->type = ANNOT_STRING;
	annotation->key = key;
	annotation->val_str = val;
	annotation->endpoint = endpoint;
	res = 0;

OUT:
	return res;
}

int blkin_init_integer_annotation(struct blkin_annotation *annotation,
				  const char *key, int64_t val, const struct blkin_endpoint *endpoint)
{
	int res;
	if ((!annotation) || (!key)) {
		res = -EINVAL;
		goto OUT;
	}
	annotation->type = ANNOT_INTEGER;
	annotation->key = key;
	annotation->val_int = val;
	annotation->endpoint = endpoint;
	res = 0;

OUT:
	return res;
}

int blkin_init_timestamp_annotation(struct blkin_annotation *annotation,
				    const char *event, const struct blkin_endpoint *endpoint)
{
	int res;
	if ((!annotation) || (!event)){
		res = -EINVAL;
		goto OUT;
	}
	annotation->type = ANNOT_TIMESTAMP;
	annotation->val_str = event;
	annotation->endpoint = endpoint;
	res = 0;

OUT:
	return res;
}

int blkin_record(const struct blkin_trace *trace,
		 const struct blkin_annotation *annotation)
{
	int res;
	if (!annotation || !trace || !trace->name) {
		res = -EINVAL;
		goto OUT;
	}

	const struct blkin_endpoint *endpoint =
		annotation->endpoint ? : trace->endpoint;
	if (!endpoint || !endpoint->ip || !endpoint->name) {
		res = -EINVAL;
		goto OUT;
	}

	if (annotation->type == ANNOT_STRING) {
		if ((!annotation->key) || (!annotation->val_str)) {
			res = -EINVAL;
			goto OUT;
		}
		tracepoint(zipkin, keyval_string, trace->name,
			   endpoint->name, endpoint->port, endpoint->ip,
			   trace->info.trace_id, trace->info.span_id,
			   trace->info.parent_span_id,
			   annotation->key, annotation->val_str);
	}
	else if (annotation->type == ANNOT_INTEGER) {
		if (!annotation->key) {
			res = -EINVAL;
			goto OUT;
		}
		tracepoint(zipkin, keyval_integer, trace->name,
			   endpoint->name, endpoint->port, endpoint->ip,
			   trace->info.trace_id, trace->info.span_id,
			   trace->info.parent_span_id,
			   annotation->key, annotation->val_int);
	}
	else {
		if (!annotation->val_str) {
			res = -EINVAL;
			goto OUT;
		}
		tracepoint(zipkin, timestamp , trace->name,
			   endpoint->name, endpoint->port, endpoint->ip,
			   trace->info.trace_id, trace->info.span_id,
			   trace->info.parent_span_id,
			   annotation->val_str);
	}
	res = 0;
OUT:
	return res;
}

int blkin_get_trace_info(const struct blkin_trace *trace,
			 struct blkin_trace_info *info)
{
	int res;
	if ((!trace) || (!info)){
		res = -EINVAL;
		goto OUT;
	}

	res = 0;
	*info = trace->info;
OUT:
	return res;
}

int blkin_set_trace_info(struct blkin_trace *trace,
			 const struct blkin_trace_info *info)
{
	int res;
	if ((!trace) || (!info)){
		res = -EINVAL;
		goto OUT;
	}

	res = 0;
	trace->info = *info;
OUT:
	return res;
}

int blkin_set_trace_properties(struct blkin_trace *trace,
			 const struct blkin_trace_info *info,
			 const char *name,
			 const struct blkin_endpoint *endpoint)
{
	int res;
	if ((!trace) || (!info) || (!endpoint) || (!name)){
		res = -EINVAL;
		goto OUT;
	}

	res = 0;
	trace->info = *info;
	trace->name = name;
	trace->endpoint = endpoint;

OUT:
	return res;
}

int blkin_pack_trace_info(const struct blkin_trace_info *info,
			  struct blkin_trace_info_packed *pinfo)
{
	if (!info || !pinfo) {
		return -EINVAL;
	}

	pinfo->trace_id = __cpu_to_be64(info->trace_id);
	pinfo->span_id = __cpu_to_be64(info->span_id);
	pinfo->parent_span_id = __cpu_to_be64(info->parent_span_id);

	return 0;
}

int blkin_unpack_trace_info(const struct blkin_trace_info_packed *pinfo,
			    struct blkin_trace_info *info)
{
	if (!info || !pinfo) {
		return -EINVAL;
	}

	info->trace_id = __be64_to_cpu(pinfo->trace_id);
	info->span_id = __be64_to_cpu(pinfo->span_id);
	info->parent_span_id = __be64_to_cpu(pinfo->parent_span_id);

	return 0;
}
