/*
 * Copyright 2015 Aerospike, Inc.
 *
 * Portions may be licensed to Aerospike, Inc. under one or more contributor
 * license agreements.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy of
 * the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations under
 * the License.
 */

#pragma once

#include <aerospike/as_async.h>
#include <aerospike/as_log.h>
#include <aerospike/as_log_macros.h>
#include <aerospike/as_node.h>
#include <aerospike/as_socket.h>

#include <citrusleaf/alloc.h>
#include <citrusleaf/cf_ll.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

typedef struct as_pipe_connection {
	as_event_connection base;
	as_event_command* writer;
	cf_ll readers;
	bool canceling;
	bool canceled;
	bool in_pool;
} as_pipe_connection;

extern int
as_pipe_get_send_buffer_size();

extern int
as_pipe_get_recv_buffer_size();

extern void
as_pipe_get_connection(as_event_command* cmd);

extern bool
as_pipe_modify_fd(int fd);

extern void
as_pipe_socket_error(as_event_command* cmd, as_error* err, bool retry);

extern void
as_pipe_timeout(as_event_command* cmd, bool retry);

extern void
as_pipe_response_error(as_event_command* cmd, as_error* err);

extern void
as_pipe_response_complete(as_event_command* cmd);

extern void
as_pipe_write_start(as_event_command* cmd);

extern void
as_pipe_read_start(as_event_command* cmd);

static inline as_event_command*
as_pipe_link_to_command(cf_ll_element* link)
{
	return (as_event_command*)((uint8_t*)link - offsetof(as_event_command, pipe_link));
}
