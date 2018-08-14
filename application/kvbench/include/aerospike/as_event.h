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

#include <aerospike/as_queue.h>
#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>

/**
 *	@defgroup async_events Event Framework Abstraction
 *
 *  Generic asynchronous events abstraction.  Designed to support multiple event libraries.
 *	Only one library is supported per build.
 */
#define AS_EVENT_LIB_DEFINED (defined(AS_USE_LIBEV) || defined(AS_USE_LIBUV) || defined(AS_USE_LIBEVENT))

#if defined(AS_USE_LIBEV)
#include <ev.h>
#elif defined(AS_USE_LIBUV)
#include <uv.h>
#elif defined(AS_USE_LIBEVENT)
#include <event2/event_struct.h>
#else
#endif

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 * TYPES
 *****************************************************************************/
	
/**
 *	Generic asynchronous event loop abstraction.  There is one event loop per thread.
 *	Event loops can be created by the client, or be referenced to externally created event loops.
 *
 *	@ingroup async_events
 */
typedef struct as_event_loop {
#if defined(AS_USE_LIBEV)
	struct ev_loop* loop;
	struct ev_async wakeup;
#elif defined(AS_USE_LIBUV)
	uv_loop_t* loop;
	uv_async_t* wakeup;
#elif defined(AS_USE_LIBEVENT)
	struct event_base* loop;
	struct event wakeup;
#else
	void* loop;
#endif
		
	struct as_event_loop* next;
	pthread_mutex_t lock;
	as_queue queue;
	as_queue pipe_cb_queue;
	pthread_t thread;
	uint32_t index;
	// Count of consecutive errors occurring before event loop registration.
	// Used to prevent deep recursion.
	uint32_t errors;
	bool pipe_cb_calling;
} as_event_loop;

/******************************************************************************
 * GLOBAL VARIABLES
 *****************************************************************************/

extern as_event_loop* as_event_loops;
extern as_event_loop* as_event_loop_current;
extern uint32_t as_event_loop_size;

/******************************************************************************
 * PUBLIC FUNCTIONS
 *****************************************************************************/

/**
 *	Create new event loops. This method should only be called when asynchronous client commands 
 *	will be used and the calling program itself is not asynchronous.  If this method is used,
 *	it must be called before aerospike_connect().
 *
 *	@param capacity	Number of event loops to create.
 *	@return			Event loop array.
 *
 *	@ingroup async_events
 */
as_event_loop*
as_event_create_loops(uint32_t capacity);

/**
 *	Set the number of externally created event loops.  This method should be called when the 
 *	calling program wants to share event loops with the client.  This reduces resource usage and
 *	can increase performance.
 *
 *	This method is used in conjunction with as_event_set_external_loop() to fully define the
 *	the external loop to the client and obtain a reference the client's event loop abstraction.
 *
 *	~~~~~~~~~~{.c}
 *	struct {
 *		pthread_t thread;
 *		struct ev_loop* loop;
 *		as_event_loop* as_loop;
 *	} my_loop;
 *
 *  static void* my_loop_worker_thread(void* udata) {
 *		struct my_loop* myloop = udata;
 *		myloop->loop = ev_loop_new(EVFLAG_AUTO);
 *		myloop->as_loop = as_event_set_external_loop(myloop->loop);
 *		ev_loop(myloop->loop, 0);
 *		ev_loop_destroy(myloop->loop);
 *		return NULL;
 *	}
 *
 *	int capacity = 8;
 *	struct my_loop* loops = malloc(sizeof(struct my_loop) * capacity);
 *	as_event_set_external_loop_capacity(capacity);
 *
 *	for (int i = 0; i < capacity; i++) {
 *		struct my_loop* myloop = &loops[i];
 *		return pthread_create(&myloop->thread, NULL, my_loop_worker_thread, myloop) == 0;
 *	}
 *	~~~~~~~~~~
 *
 *	@param capacity	Number of externally created event loops.
 *	@return			True if all external loops were initialized.
 *
 *	@ingroup async_events
 */
bool
as_event_set_external_loop_capacity(uint32_t capacity);

/**
 *	Register an external event loop with the client. This method should be called when the 
 *	calling program wants to share event loops with the client.  This reduces resource usage and
 *	can increase performance.
 *
 *	This method must be called in the same thread as the event loop that is being registered.
 *
 *	This method is used in conjunction with as_event_set_external_loop_capacity() to fully define
 *	the external loop to the client and obtain a reference the client's event loop abstraction.
 *
 *	~~~~~~~~~~{.c}
 *	struct {
 *		pthread_t thread;
 *		struct ev_loop* loop;
 *		as_event_loop* as_loop;
 *	} my_loop;
 *
 *  static void* my_loop_worker_thread(void* udata) {
 *		struct my_loop* myloop = udata;
 *		myloop->loop = ev_loop_new(EVFLAG_AUTO);
 *		myloop->as_loop = as_event_set_external_loop(myloop->loop);
 *		ev_loop(myloop->loop, 0);
 *		ev_loop_destroy(myloop->loop);
 *		return NULL;
 *	}
 *
 *	int capacity = 8;
 *	struct my_loop* loops = malloc(sizeof(struct my_loop) * capacity);
 *	as_event_set_external_loop_capacity(capacity);
 *
 *	for (int i = 0; i < capacity; i++) {
 *		struct my_loop* myloop = &loops[i];
 *		return pthread_create(&myloop->thread, NULL, my_loop_worker_thread, myloop) == 0;
 *	}
 *	~~~~~~~~~~
 *
 *	@param loop		External event loop.
 *	@return			Client's generic event loop abstraction that is used in client async commands.
 *					Returns NULL if external loop capacity would be exceeded.
 *
 *	@ingroup async_events
 */
as_event_loop*
as_event_set_external_loop(void* loop);

/**
 *	Find client's event loop abstraction given the external event loop.
 *
 *	@param loop		External event loop.
 *	@return			Client's generic event loop abstraction that is used in client async commands.
 *					Returns NULL if loop not found.
 *
 *	@ingroup async_events
 */
as_event_loop*
as_event_loop_find(void* loop);

/**
 *	Retrieve event loop by array index.
 *
 *	@param index	Event loop array index.
 *	@return			Client's generic event loop abstraction that is used in client async commands.
 *
 *	@ingroup async_events
 */
static inline as_event_loop*
as_event_loop_get_by_index(uint32_t index)
{
	return index < as_event_loop_size ? &as_event_loops[index] : NULL;
}

/**
 *	Retrieve a random event loop using round robin distribution.
 *
 *	@return			Client's generic event loop abstraction that is used in client async commands.
 *
 *	@ingroup async_events
 */
static inline as_event_loop*
as_event_loop_get()
{
	// The last event loop points to the first event loop to create a circular linked list.
	// Not atomic because doesn't need to be exactly accurate.
	as_event_loop* event_loop = as_event_loop_current;
	as_event_loop_current = event_loop->next;
	return event_loop;
}
	
/**
 *	Close internal event loops and release watchers for internal and external event loops.
 *	The global event loop array will also be destroyed for internal event loops.
 *
 *	This method should be called once on program shutdown if as_event_create_loops() or
 *	as_event_set_external_loop_capacity() was called.
 *
 *	The shutdown sequence is slightly different for internal and external event loops.
 *
 *	Internal:
 *	~~~~~~~~~~{.c}
 *	as_event_close_loops();
 *	~~~~~~~~~~
 *
 *	External:
 *	~~~~~~~~~~{.c}
 *	as_event_close_loops();
 *	Join on external loop threads.
 *	as_event_destroy_loops();
 *	~~~~~~~~~~
 *
 *	@return		True if event loop close was successful.  If false, as_event_destroy_loops() should
 *				not be called.
 *
 *	@ingroup async_events
 */
bool
as_event_close_loops();

/**
 *	Destroy global event loop array.  This function only needs to be called for external
 *	event loops.
 *
 *	@ingroup async_events
 */
void
as_event_destroy_loops();

#ifdef __cplusplus
} // end extern "C"
#endif
