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

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct as_cluster_s;

/******************************************************************************
 * FUNCTIONS
 *****************************************************************************/

uint32_t
as_async_get_cluster_count();

uint32_t
as_async_get_pending(struct as_cluster_s* cluster);

uint32_t
as_async_get_connections(struct as_cluster_s* cluster);

void
as_async_update_max_idle(struct as_cluster_s* cluster, uint32_t max_idle);

void
as_async_update_max_conns(struct as_cluster_s* cluster, bool pipe, uint32_t max_conns);

#ifdef __cplusplus
} // end extern "C"
#endif
