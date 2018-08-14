/*
 * Copyright 2008-2017 Aerospike, Inc.
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

#include <aerospike/as_admin.h>
#include <aerospike/as_cluster.h>
#include <aerospike/as_listener.h>
#include <aerospike/as_queue.h>
#include <aerospike/as_proto.h>
#include <aerospike/as_socket.h>
#include <citrusleaf/cf_ll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>

#if defined(AS_USE_LIBEV)
#include <ev.h>
#elif defined(AS_USE_LIBUV)
#include <uv.h>
#elif defined(AS_USE_LIBEVENT)
#include <event2/event.h>
#else
#endif

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 * TYPES
 *****************************************************************************/
	
#define AS_ASYNC_STATE_UNREGISTERED 0
#define AS_ASYNC_STATE_REGISTERED 1
#define AS_ASYNC_STATE_TLS_CONNECT 2
#define AS_ASYNC_STATE_AUTH_WRITE 3
#define AS_ASYNC_STATE_AUTH_READ_HEADER 4
#define AS_ASYNC_STATE_AUTH_READ_BODY 5
#define AS_ASYNC_STATE_COMMAND_WRITE 6
#define AS_ASYNC_STATE_COMMAND_READ_HEADER 7
#define AS_ASYNC_STATE_COMMAND_READ_BODY 8
#define AS_ASYNC_STATE_COMPLETE 9

#define AS_ASYNC_FLAGS_MASTER 1
#define AS_ASYNC_FLAGS_READ 2
#define AS_ASYNC_FLAGS_HAS_TIMER 4
#define AS_ASYNC_FLAGS_USING_SOCKET_TIMER 8
#define AS_ASYNC_FLAGS_EVENT_RECEIVED 16
#define AS_ASYNC_FLAGS_FREE_BUF 32
#define AS_ASYNC_FLAGS_CP_MODE 64

#define AS_ASYNC_AUTH_RETURN_CODE 1

#define AS_EVENT_CONNECTION_COMPLETE 0
#define AS_EVENT_CONNECTION_PENDING 1
#define AS_EVENT_CONNECTION_ERROR 2

#define AS_EVENT_QUEUE_INITIAL_CAPACITY 256
	
struct as_event_command;
struct as_event_executor;
	
typedef struct {
#if defined(AS_USE_LIBEV)
	struct ev_io watcher;
	as_socket socket;
#elif defined(AS_USE_LIBUV)
	uv_tcp_t socket;
	// Reuse memory for requests, because only one request is active at a time.
	union {
		uv_connect_t connect;
		uv_write_t write;
	} req;
#elif defined(AS_USE_LIBEVENT)
	struct event watcher;
	as_socket socket;
#else
#endif
	int watching;
	bool pipeline;
} as_event_connection;

typedef struct {
	as_event_connection base;
	struct as_event_command* cmd;
} as_async_connection;

typedef struct {
	as_pipe_listener listener;
	void* udata;
} as_queued_pipe_cb;

typedef void (*as_event_executable) (void* udata);
typedef bool (*as_event_parse_results_fn) (struct as_event_command* cmd);
typedef void (*as_event_executor_complete_fn) (struct as_event_executor* executor);
typedef void (*as_event_executor_destroy_fn) (struct as_event_executor* executor);

typedef struct as_event_command {
#if defined(AS_USE_LIBEV)
	struct ev_timer timer;
#elif defined(AS_USE_LIBUV)
	uv_timer_t timer;
#elif defined(AS_USE_LIBEVENT)
	struct event timer;
#else
#endif
	uint64_t total_deadline;
	uint32_t socket_timeout;
	uint32_t max_retries;
	uint32_t iteration;
	as_policy_replica replica;
	as_event_loop* event_loop;
	as_event_connection* conn;
	as_cluster* cluster;
	as_node* node;
	void* partition;  // as_partition* or as_partition_shm*
	void* udata;
	as_event_parse_results_fn parse_results;
	as_pipe_listener pipe_listener;
	cf_ll_element pipe_link;
	
	uint8_t* buf;
	uint32_t write_offset;
	uint32_t write_len;
	uint32_t read_capacity;
	uint32_t len;
	uint32_t pos;

	uint8_t type;
	uint8_t state;
	uint8_t flags;
	bool deserialize;
} as_event_command;

typedef struct {
	as_event_executable executable;
	void* udata;
} as_event_commander;

typedef struct as_event_executor {
	pthread_mutex_t lock;
	struct as_event_command** commands;
	as_event_loop* event_loop;
	as_event_executor_complete_fn complete_fn;
	void* udata;
	as_error* err;
	uint32_t max_concurrent;
	uint32_t max;
	uint32_t count;
	bool notify;
	bool valid;
} as_event_executor;

/******************************************************************************
 * COMMON FUNCTIONS
 *****************************************************************************/

as_status
as_event_command_execute(as_event_command* cmd, as_error* err);

void
as_event_socket_timeout(as_event_command* cmd);

void
as_event_total_timeout(as_event_command* cmd);

bool
as_event_command_retry(as_event_command* cmd, bool alternate);
	
void
as_event_executor_complete(as_event_command* cmd);

void
as_event_executor_cancel(as_event_executor* executor, int queued_count);

void
as_event_error_callback(as_event_command* cmd, as_error* err);

void
as_event_parse_error(as_event_command* cmd, as_error* err);

void
as_event_socket_error(as_event_command* cmd, as_error* err);

void
as_event_response_error(as_event_command* cmd, as_error* err);

bool
as_event_command_parse_result(as_event_command* cmd);
	
bool
as_event_command_parse_header(as_event_command* cmd);

bool
as_event_command_parse_success_failure(as_event_command* cmd);

void
as_event_command_free(as_event_command* cmd);

void
as_event_close_cluster(as_cluster* cluster);

/******************************************************************************
 * IMPLEMENTATION SPECIFIC FUNCTIONS
 *****************************************************************************/

bool
as_event_create_loop(as_event_loop* event_loop);

void
as_event_register_external_loop(as_event_loop* event_loop);

/**
 * Schedule execution of function on specified event loop.
 * Command is placed on event loop queue and is never executed directly.
 */
bool
as_event_execute(as_event_loop* event_loop, as_event_executable executable, void* udata);

void
as_event_command_write_start(as_event_command* cmd);

void
as_event_connect(as_event_command* cmd);

void
as_event_close_connection(as_event_connection* conn);

void
as_event_node_destroy(as_node* node);

/******************************************************************************
 * LIBEV INLINE FUNCTIONS
 *****************************************************************************/

#if defined(AS_USE_LIBEV)

void as_ev_socket_timeout(struct ev_loop* loop, ev_timer* timer, int revents);
void as_ev_total_timeout(struct ev_loop* loop, ev_timer* timer, int revents);

static inline int
as_event_validate_connection(as_event_connection* conn)
{
	return as_socket_validate(&conn->socket);
}

static inline void
as_event_set_conn_last_used(as_event_connection* conn, uint32_t max_socket_idle)
{
	// TLS connections default to 55 seconds.
	if (max_socket_idle == 0 && conn->socket.ctx) {
		max_socket_idle = 55;
	}

	if (max_socket_idle > 0) {
		conn->socket.idle_check.max_socket_idle = max_socket_idle;
		conn->socket.idle_check.last_used = (uint32_t)cf_get_seconds();
	}
	else {
		conn->socket.idle_check.max_socket_idle = conn->socket.idle_check.last_used = 0;
	}
}

static inline void
as_event_init_total_timer(as_event_command* cmd, uint64_t timeout)
{
	ev_timer_init(&cmd->timer, as_ev_total_timeout, (double)timeout / 1000.0, 0.0);
	cmd->timer.data = cmd;
	ev_timer_start(cmd->event_loop->loop, &cmd->timer);
}
	
static inline void
as_event_set_total_timer(as_event_command* cmd, uint64_t timeout)
{
	ev_timer_start(cmd->event_loop->loop, &cmd->timer);
}

static inline void
as_event_init_socket_timer(as_event_command* cmd)
{
	ev_init(&cmd->timer, as_ev_socket_timeout);
	cmd->timer.repeat = ((double)cmd->socket_timeout) / 1000.0;
	cmd->timer.data = cmd;
	ev_timer_again(cmd->event_loop->loop, &cmd->timer);
}

static inline void
as_event_repeat_socket_timer(as_event_command* cmd)
{
	cmd->timer.repeat = (double)cmd->socket_timeout/ 1000.0;
	ev_timer_again(cmd->event_loop->loop, &cmd->timer);
}

static inline void
as_event_stop_timer(as_event_command* cmd)
{
	ev_timer_stop(cmd->event_loop->loop, &cmd->timer);
}

static inline void
as_event_stop_watcher(as_event_command* cmd, as_event_connection* conn)
{
	ev_io_stop(cmd->event_loop->loop, &conn->watcher);
}

static inline void
as_event_command_release(as_event_command* cmd)
{
	as_event_command_free(cmd);
}

/******************************************************************************
 * LIBUV INLINE FUNCTIONS
 *****************************************************************************/

#elif defined(AS_USE_LIBUV)

void as_uv_total_timeout(uv_timer_t* timer);
void as_uv_socket_timeout(uv_timer_t* timer);

static inline int
as_event_validate_connection(as_event_connection* conn)
{
	// Libuv does not have a peek function, so use fd directly.
	uv_os_fd_t fd;
	
	if (uv_fileno((uv_handle_t*)&conn->socket, &fd) == 0) {
		return as_socket_validate_fd(fd);
	}
	return false;
}
	
static inline void
as_event_set_conn_last_used(as_event_connection* conn, uint32_t max_socket_idle)
{
}

static inline void
as_event_init_total_timer(as_event_command* cmd, uint64_t timeout)
{
	uv_timer_init(cmd->event_loop->loop, &cmd->timer);
	cmd->timer.data = cmd;
	uv_timer_start(&cmd->timer, as_uv_total_timeout, timeout, 0);
}

static inline void
as_event_set_total_timer(as_event_command* cmd, uint64_t timeout)
{
	uv_timer_start(&cmd->timer, as_uv_total_timeout, timeout, 0);
}

static inline void
as_event_init_socket_timer(as_event_command* cmd)
{
	uv_timer_init(cmd->event_loop->loop, &cmd->timer);
	cmd->timer.data = cmd;
	uv_timer_start(&cmd->timer, as_uv_socket_timeout, cmd->socket_timeout, cmd->socket_timeout);
}

static inline void
as_event_repeat_socket_timer(as_event_command* cmd)
{
	uv_timer_again(&cmd->timer);
}

static inline void
as_event_stop_timer(as_event_command* cmd)
{
	uv_timer_stop(&cmd->timer);
}

static inline void
as_event_stop_watcher(as_event_command* cmd, as_event_connection* conn)
{
	// Watcher already stopped by design in libuv.
}

void
as_uv_timer_closed(uv_handle_t* handle);

static inline void
as_event_command_release(as_event_command* cmd)
{
	if (cmd->flags & AS_ASYNC_FLAGS_HAS_TIMER) {
		// libuv requires that cmd can't be freed until timer is closed.
		uv_close((uv_handle_t*)&cmd->timer, as_uv_timer_closed);
	}
	else {
		as_event_command_free(cmd);
	}
}

/******************************************************************************
 * LIBEVENT INLINE FUNCTIONS
 *****************************************************************************/

#elif defined(AS_USE_LIBEVENT)

void as_libevent_socket_timeout(evutil_socket_t sock, short events, void* udata);
void as_libevent_total_timeout(evutil_socket_t sock, short events, void* udata);

static inline int
as_event_validate_connection(as_event_connection* conn)
{
	return as_socket_validate(&conn->socket);
}

static inline void
as_event_set_conn_last_used(as_event_connection* conn, uint32_t max_socket_idle)
{
	// TLS connections default to 55 seconds.
	if (max_socket_idle == 0 && conn->socket.ctx) {
		max_socket_idle = 55;
	}

	if (max_socket_idle > 0) {
		conn->socket.idle_check.max_socket_idle = max_socket_idle;
		conn->socket.idle_check.last_used = (uint32_t)cf_get_seconds();
	}
	else {
		conn->socket.idle_check.max_socket_idle = conn->socket.idle_check.last_used = 0;
	}
}

static inline void
as_event_init_total_timer(as_event_command* cmd, uint64_t timeout)
{
	evtimer_assign(&cmd->timer, cmd->event_loop->loop, as_libevent_total_timeout, cmd);

	struct timeval tv;
	tv.tv_sec = timeout / 1000;
	tv.tv_usec = (timeout % 1000) * 1000;

	evtimer_add(&cmd->timer, &tv);
}

static inline void
as_event_set_total_timer(as_event_command* cmd, uint64_t timeout)
{
	struct timeval tv;
	tv.tv_sec = timeout / 1000;
	tv.tv_usec = (timeout % 1000) * 1000;

	evtimer_add(&cmd->timer, &tv);
}

static inline void
as_event_init_socket_timer(as_event_command* cmd)
{
	event_assign(&cmd->timer, cmd->event_loop->loop, -1, EV_PERSIST, as_libevent_socket_timeout, cmd);

	struct timeval tv;
	tv.tv_sec = cmd->socket_timeout / 1000;
	tv.tv_usec = (cmd->socket_timeout % 1000) * 1000;

	evtimer_add(&cmd->timer, &tv);
}

static inline void
as_event_repeat_socket_timer(as_event_command* cmd)
{
	// libevent socket timers automatically repeat.
}

static inline void
as_event_stop_timer(as_event_command* cmd)
{
	evtimer_del(&cmd->timer);
}

static inline void
as_event_stop_watcher(as_event_command* cmd, as_event_connection* conn)
{
	event_del(&conn->watcher);
}

static inline void
as_event_command_release(as_event_command* cmd)
{
	as_event_command_free(cmd);
}

/******************************************************************************
 * EVENT_LIB NOT DEFINED INLINE FUNCTIONS
 *****************************************************************************/

#else

static inline int
as_event_validate_connection(as_event_connection* conn)
{
	return -1;
}

static inline void
as_event_set_conn_last_used(as_event_connection* conn, uint32_t max_socket_idle)
{
}

static inline void
as_event_init_total_timer(as_event_command* cmd, uint64_t timeout)
{
}

static inline void
as_event_set_total_timer(as_event_command* cmd, uint64_t timeout)
{
}

static inline void
as_event_init_socket_timer(as_event_command* cmd)
{
}

static inline void
as_event_repeat_socket_timer(as_event_command* cmd)
{
}

static inline void
as_event_stop_timer(as_event_command* cmd)
{
}

static inline void
as_event_stop_watcher(as_event_command* cmd, as_event_connection* conn)
{
}

static inline void
as_event_command_release(as_event_command* cmd)
{
}

#endif
	
/******************************************************************************
 * COMMON INLINE FUNCTIONS
 *****************************************************************************/

static inline as_event_loop*
as_event_assign(as_event_loop* event_loop)
{
	// Assign event loop using round robin distribution if not specified.
	return event_loop ? event_loop : as_event_loop_get();
}

static inline void
as_event_set_auth_write(as_event_command* cmd)
{
	// Authentication write buffer is always located after command write buffer.
	uint8_t* buf = (uint8_t*)cmd + cmd->write_offset + cmd->write_len;
	uint32_t len = as_authenticate_set(cmd->cluster->user, cmd->cluster->password, buf);
	cmd->len = cmd->write_len + len;
	cmd->pos = cmd->write_len;
}

static inline void
as_event_set_auth_read_header(as_event_command* cmd)
{
	// Authenticate read buffer uses the standard read buffer (buf).
	cmd->len = sizeof(as_proto);
	cmd->pos = 0;
	cmd->state = AS_ASYNC_STATE_AUTH_READ_HEADER;
}
	
static inline void
as_event_set_auth_parse_header(as_event_command* cmd)
{
	// Authenticate read buffer uses the standard read buffer (buf).
	as_proto* proto = (as_proto*)cmd->buf;
	as_proto_swap_from_be(proto);
	cmd->len = (uint32_t)proto->sz;
	cmd->pos = 0;
	cmd->state = AS_ASYNC_STATE_AUTH_READ_BODY;
}

static inline void
as_event_set_write(as_event_command* cmd)
{
	cmd->len = cmd->write_len;
	cmd->pos = 0;
}

static inline void
as_event_release_connection(as_event_connection* conn, as_conn_pool* pool)
{
	as_event_close_connection(conn);
	as_conn_pool_dec(pool);
}

static inline void
as_event_release_async_connection(as_event_command* cmd)
{
	as_conn_pool* pool = &cmd->node->async_conn_pools[cmd->event_loop->index];
	as_event_release_connection(cmd->conn, pool);
}

static inline void
as_event_decr_conn(as_event_command* cmd)
{
	as_conn_pool* pool = cmd->pipe_listener != NULL ?
		&cmd->node->pipe_conn_pools[cmd->event_loop->index] :
		&cmd->node->async_conn_pools[cmd->event_loop->index];
	
	as_conn_pool_dec(pool);
}

static inline void
as_event_connection_timeout(as_event_command* cmd, as_conn_pool* pool)
{
	as_event_connection* conn = cmd->conn;

	if (conn->watching > 0) {
		as_event_stop_watcher(cmd, conn);
		as_event_release_connection(conn, pool);
	}
	else {
		cf_free(conn);
		as_conn_pool_dec(pool);
	}
}

static inline bool
as_event_socket_retry(as_event_command* cmd)
{
	if (cmd->pipe_listener) {
		return false;
	}

	as_event_stop_watcher(cmd, cmd->conn);
	as_event_release_async_connection(cmd);
	return as_event_command_retry(cmd, true);
}

#ifdef __cplusplus
} // end extern "C"
#endif
