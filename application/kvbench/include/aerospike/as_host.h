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

#include <citrusleaf/alloc.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *	TYPES
 *****************************************************************************/

/**
 *	Host information.
 */
typedef struct as_host_s {
	/**
	 *	Host name or IP address of database server.
	 */
	char* name;
	
	/**
	 *	TLS certificate name for secure connections.
	 */
	char* tls_name;

	/**
	 *	Port of database server.
	 */
	uint16_t port;
} as_host;

/******************************************************************************
 *	FUNCTIONS
 *****************************************************************************/

/**
 * Deep copy host.
 */
static inline void
as_host_copy(const as_host* src, as_host* trg)
{
	trg->name = (char*)cf_strdup(src->name);
	trg->tls_name = src->tls_name ? (char*)cf_strdup(src->tls_name) : NULL;
	trg->port = src->port;
}

/**
 * Deep copy host from fields.
 */
static inline void
as_host_copy_fields(as_host* trg, const char* hostname, const char* tls_name, uint16_t port)
{
	trg->name = (char*)cf_strdup(hostname);
	trg->tls_name = tls_name ? (char*)cf_strdup(tls_name) : NULL;
	trg->port = port;
}

/**
 * Release memory associated with host.
 */
static inline void
as_host_destroy(as_host* host)
{
	cf_free(host->name);
	cf_free(host->tls_name);
}

#ifdef __cplusplus
} // end extern "C"
#endif
