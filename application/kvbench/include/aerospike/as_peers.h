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

#include <aerospike/as_cluster.h>

/******************************************************************************
 * TYPES
 *****************************************************************************/

typedef struct as_peers_s {
	as_vector /* as_host */ hosts;
	as_vector /* as_node* */ nodes;
	bool use_peers;
	bool gen_changed;
} as_peers;

/******************************************************************************
 * FUNCTIONS
 *****************************************************************************/

as_node*
as_peers_find_local_node(as_vector* nodes, const char* name);

void
as_peers_parse_services(as_peers* peers, as_cluster* cluster, as_node* node, char* buf);

as_status
as_peers_parse_peers(as_peers* peers, as_error* err, as_cluster* cluster, as_node* node, char* buf);
