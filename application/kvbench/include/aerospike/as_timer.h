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

#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*****************************************************************************
 * TYPES
 *****************************************************************************/

struct as_timer_s;
/**
 * The interface which all timer should implement.
 */
typedef struct as_timer_hooks_s {
    /**
     * The destroy should free resources associated with the timer's source.
     * The destroy should not free the timer itself.
     */
    int      (* destroy)(struct as_timer_s *);
    bool     (* timedout)(const struct as_timer_s *);
	uint64_t (* timeslice)(const struct as_timer_s *);
} as_timer_hooks;

/**
 * Timer handle
 */
typedef struct as_timer_s {
    bool                    is_malloc;
    void *                  source;
    const as_timer_hooks  * hooks;
} as_timer;

/*****************************************************************************
 * FUNCTIONS
 *****************************************************************************/

/**
 * Initialize a stack allocated timer
 */
as_timer * as_timer_init(as_timer * timer, void * source, const as_timer_hooks * hooks);

/**
 * Heap allocate and initialize a timer
 */
as_timer * as_timer_new(void * source, const as_timer_hooks * hooks);


static inline void * as_timer_source(const as_timer * tt) {
    return (tt ? tt->source : NULL);
}

/**
 * Release resources associated with the timer.
 * Calls timer->destroy. If success and if this is a heap allocated
 * timer, then it will be freed.
 */
int as_timer_destroy(as_timer * timer);

/**
 * true if timer has timedout
 */
bool as_timer_timedout(const as_timer * timer);

/**
 * returns timeslice assigned for this timer
 */
uint64_t as_timer_timeslice(const as_timer * timer);

#ifdef __cplusplus
} // end extern "C"
#endif
