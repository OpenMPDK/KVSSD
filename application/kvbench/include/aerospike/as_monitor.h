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

#include <pthread.h>
#include <stdbool.h>

typedef struct {
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool complete;
} as_monitor;

static inline void
as_monitor_init(as_monitor* monitor)
{
	pthread_mutex_init(&monitor->lock, NULL);
	pthread_cond_init(&monitor->cond, NULL);
	monitor->complete = false;
}

static inline void
as_monitor_destroy(as_monitor* monitor)
{
	pthread_mutex_destroy(&monitor->lock);
	pthread_cond_destroy(&monitor->cond);
}

static inline void
as_monitor_begin(as_monitor* monitor)
{
	monitor->complete = false;
}

static inline void
as_monitor_notify(as_monitor* monitor)
{
	pthread_mutex_lock(&monitor->lock);
	monitor->complete = true;
	pthread_cond_signal(&monitor->cond);
	pthread_mutex_unlock(&monitor->lock);
}

static inline void
as_monitor_wait(as_monitor* monitor)
{
	pthread_mutex_lock(&monitor->lock);
	while (! monitor->complete) {
		pthread_cond_wait(&monitor->cond, &monitor->lock);
	}
	pthread_mutex_unlock(&monitor->lock);
}
