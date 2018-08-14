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

/*
 * A simple priority queue implementation, which is simply a set of queues underneath.
 * This currently doesn't support 'delete' and 'reduce' functionality
 */
#include "cf_queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 * CONSTANTS
 ******************************************************************************/

#define CF_QUEUE_PRIORITY_LOW 1
#define CF_QUEUE_PRIORITY_MEDIUM 2
#define CF_QUEUE_PRIORITY_HIGH 3

/******************************************************************************
 * TYPES
 ******************************************************************************/

typedef struct cf_queue_priority_s {
	bool            threadsafe;
	cf_queue *      low_q;
	cf_queue *      medium_q;
	cf_queue *      high_q;
	pthread_mutex_t LOCK;
	pthread_cond_t  CV;
} cf_queue_priority;

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/

cf_queue_priority *cf_queue_priority_create(size_t element_sz, bool threadsafe);
void cf_queue_priority_destroy(cf_queue_priority *q);
int cf_queue_priority_sz(cf_queue_priority *q);
int cf_queue_priority_push(cf_queue_priority *q, const void *ptr, int pri);
int cf_queue_priority_pop(cf_queue_priority *q, void *buf, int mswait);
int cf_queue_priority_reduce_pop(cf_queue_priority *priority_q, void *buf, cf_queue_reduce_fn cb, void *udata);
int cf_queue_priority_change(cf_queue_priority *priority_q, const void *ptr, int new_pri);
int cf_queue_priority_reduce_change(cf_queue_priority *priority_q, int new_pri, cf_queue_reduce_fn cb, void *udata);

/******************************************************************************
 * MACROS
 ******************************************************************************/

#define CF_Q_PRI_EMPTY(__q) (CF_Q_EMPTY(__q->low_q) && CF_Q_EMPTY(__q->medium_q) && CF_Q_EMPTY(__q->high_q))

/******************************************************************************/

#ifdef __cplusplus
} // end extern "C"
#endif
