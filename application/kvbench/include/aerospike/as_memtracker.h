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

struct as_memtracker_hooks_s;
typedef struct as_memtracker_hooks_s as_memtracker_hooks;

struct as_memtracker_s;
typedef struct as_memtracker_s as_memtracker;

/**
 * The interface which all memtrackers should implement.
 */
struct as_memtracker_hooks_s {
    /**
     * The destroy should free resources associated with the memtracker's source.
     * The destroy should not free the memtracker itself.
     */
    int (* destroy)(as_memtracker *);

    bool (* reserve)(const as_memtracker *, const uint32_t);
    bool (* release)(const as_memtracker *, const uint32_t);
    bool (* reset)(const as_memtracker *);
};

/**
 * Logger handle
 */
struct as_memtracker_s {
    bool                    is_malloc;
    void *                  source;
    const as_memtracker_hooks * hooks;
};

/*****************************************************************************
 * FUNCTIONS
 *****************************************************************************/

/**
 * Initialize a stack allocated memtracker
 */
as_memtracker * as_memtracker_init(as_memtracker * memtracker, void * source, const as_memtracker_hooks * hooks);

/**
 * Heap allocate and initialize a memtracker
 */
as_memtracker * as_memtracker_new(void * source, const as_memtracker_hooks * hooks);


static inline void * as_memtracker_source(const as_memtracker * mt) {
    return (mt ? mt->source : NULL);
}

/**
 * Release resources associated with the memtracker.
 * Calls memtracker->destroy. If success and if this is a heap allocated
 * memtracker, then it will be freed.
 */
int as_memtracker_destroy(as_memtracker * memtracker);

/**
 * Reserve num_bytes bytes of memory
 */
bool as_memtracker_reserve(const as_memtracker * memtracker, const uint32_t num_bytes);

/**
 * Release num_bytes bytes of memory
 */
bool as_memtracker_release(const as_memtracker * memtracker, const uint32_t num_bytes);

/**
 * Release the entire reservation for the current thread
 */
bool as_memtracker_reset(const as_memtracker * memtracker);

#ifdef __cplusplus
} // end extern "C"
#endif
