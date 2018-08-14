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

/** 
 * @mainpage Aerospike C Client
 *
 * @section intro_sec Introduction
 *
 * The Aerospike C client allows you to build C/C++ applications to store and retrieve data from the
 * Aerospike cluster. The C client is a smart client that periodically pings nodes for cluster
 * status and manages interactions with the cluster.  The following functionality is supported.
 *
 * - Database commands
 *   - Key/Value
 *   - Map/List collections
 *   - Batch read
 *   - Scan
 *   - Secondary index query
 *   - User defined Lua functions
 * - Both synchronous and asynchronous command models
 * - Asynchronous model supports the following event frameworks.
 *   - libev
 *   - libevent
 *   - libuv
 * - Thread safe API
 * - Shared memory cluster tend state for multi-process applications
 * - TLS secure sockets
 *
 * See <a href="modules.html">Modules</a> for an API overview.
 *
 * See <a href="http://www.aerospike.com/docs/client/c">Developer Guide</a> for installation
 * instructions and example code.
 */
 
/**
 * @defgroup client_objects Client Objects
 */

/**
 * @defgroup client_operations Client Operations
 *
 * Client operations require an initialized @ref aerospike client.
 */

/**
 * @defgroup aerospike_t Client Types
 */

/**
 * @defgroup client_utilities Utilities
 * @{
 *   @defgroup stringmap_t StringMap
 * @}
 */

#include <aerospike/as_error.h>
#include <aerospike/as_config.h>
#include <aerospike/as_log.h>
#include <aerospike/as_status.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 * TYPES
 *****************************************************************************/

/**
 * @private
 * Forward declaration of a cluster object.
 */
struct as_cluster_s;

/**
 * 	An instance of @ref aerospike is required to connect to and execute 
 * operations against an Aerospike Database cluster.
 *
 * ## Configuration
 *
 * An initialized client configuration is required to initialize a 
 * @ref aerospike client. See as_config for details on configuration options.
 * 
 * At a minimum, a configuration needs to be initialized and have at least
 * one host defined:
 * 
 * ~~~~~~~~~~{.c}
 * as_config config;
 * as_config_init(&config);
 * as_config_add_host(&config, "127.0.0.1", 3000);
 * ~~~~~~~~~~
 *
 * Once connected to a host in the cluster, then client will gather information
 * about the cluster, including all other nodes in the cluster. So, all that
 * is needed is a single valid host.  Multiple hosts can still be provided in
 * case the first host is not currently active.
 * 
 * ## Initialization
 *
 * An initialized @ref aerospike object is required to connect to the 
 * database. Initialization requires a configuration to bind to the client
 * instance. 
 *
 * The @ref aerospike object can be initialized via either:
 *
 * 	- aerospike_init() — Initialize a stack allocated @ref aerospike.
 * - aerospike_new() — Create and initialize a heap allocated @ref aerospike.
 *
 * Both initialization functions require a configuration.  Once initialized, the ownership
 * of the as_config instance fields are transferred to @ref aerospike.  The user should never
 * call as_config_destroy() directly.
 *
 * The following uses a stack allocated @ref aerospike and initializes it
 * with aerospike_init():
 *
 * ~~~~~~~~~~{.c}
 * aerospike as;
 * aerospike_init(&as, &config);
 * ~~~~~~~~~~
 * 
 * ## Connecting
 *
 * An application can connect to the database with an initialized
 * @ref aerospike. The client will be connected if `aerospike_connect()` completes
 * successfully:
 * 
 * ~~~~~~~~~~{.c}
 * if (aerospike_connect(&as, &err) != AEROSPIKE_OK) {
 * 	fprintf(stderr, "error(%d) %s at [%s:%d]", err.code, err.message, err.file, err.line);
 * }
 * ~~~~~~~~~~
 *
 * The `err` parameter will be populated if an error while attempting to
 * connect to the database. See as_error, for more information on error 
 * handling.
 * 
 * An aerospike object internally keeps cluster state and maintains connection pools to the cluster. 
 * The same aerospike object should be reused by the application for database operations 
 * to a given cluster. 
 *
 * If the application requires connecting to multiple Aerospike clusters, the application must
 * create multiple aerospike objects, each connecting to a different cluster.
 * 
 * ## Disconnecting
 *
 * When the connection to the database is not longer required, then the 
 * connection to the cluster can be closed via `aerospike_close()`:
 *
 * ~~~~~~~~~~{.c}
 * aerospike_close(&as, &err);
 * ~~~~~~~~~~
 *
 * ## Destruction
 *
 * When the client is not longer required, the client and its resources should 
 * be releases via `aerospike_destroy()`:
 *
 * ~~~~~~~~~~{.c}
 * aerospike_destroy(&as);
 * ~~~~~~~~~~
 *
 * @ingroup client_objects
 */
typedef struct aerospike_s {

	/**
	 * @private
	 * If true, then aerospike_destroy() will free this instance.
	 */
	bool _free;

	/**
	 * @private
	 * Cluster state.
	 */
	struct as_cluster_s * cluster;

	/**
	 * Client configuration.
	 */
	as_config config;

} aerospike;

/******************************************************************************
 * FUNCTIONS
 *****************************************************************************/

/**
 * Initialize a stack allocated aerospike instance. 
 *
 * The config parameter can be an instance of `as_config` or `NULL`. If `NULL`,
 * then the default configuration will be used.
 *
 * Ownership of the as_config instance fields are transferred to @ref aerospike.  
 * The user should never call as_config_destroy() directly.
 *
 * ~~~~~~~~~~{.c}
 * aerospike as;
 * aerospike_init(&as, &config);
 * ~~~~~~~~~~
 *
 * Once you are finished using the instance, then you should destroy it via the 
 * `aerospike_destroy()` function.
 *
 * @param as 		The aerospike instance to initialize.
 * @param config 	The configuration to use for the instance.
 *
 * @returns the initialized aerospike instance
 *
 * @see config for information on configuring the client.
 *
 * @relates aerospike
 */
aerospike*
aerospike_init(aerospike* as, as_config* config);

/**
 * Creates a new heap allocated aerospike instance.
 *
 * Ownership of the as_config instance fields are transferred to @ref aerospike.
 * The user should never call as_config_destroy() directly.
 *
 * ~~~~~~~~~~{.c}
 * aerospike* as = aerospike_new(&config);
 * ~~~~~~~~~~
 *
 * Once you are finished using the instance, then you should destroy it via the 
 * `aerospike_destroy()` function.
 *
 * @param config	The configuration to use for the instance.
 *
 * @returns a new aerospike instance
 *
 * @see config for information on configuring the client.
 *
 * @relates aerospike
 */
aerospike*
aerospike_new(as_config* config);

/**
 * Initialize global lua configuration.
 *
 * @param config 	The lua configuration to use for all cluster instances.
 */
void
aerospike_init_lua(as_config_lua* config);	

/**
 * Destroy the aerospike instance and associated resources.
 *
 * ~~~~~~~~~~{.c}
 * aerospike_destroy(&as);
 * ~~~~~~~~~~
 *
 * @param as 		The aerospike instance to destroy
 *
 * @relates aerospike
 */
void
aerospike_destroy(aerospike* as);

/**
 * Connect an aerospike instance to the cluster.
 *
 * ~~~~~~~~~~{.c}
 * aerospike_connect(&as, &err);
 * ~~~~~~~~~~
 *
 * Once you are finished using the connection, then you must close it via
 * the `aerospike_close()` function.
 *
 * If connect fails, then you do not need to call `aerospike_close()`.
 *
 * @param as 		The aerospike instance to connect to a cluster.
 * @param err 		If an error occurs, the err will be populated.
 *
 * @returns AEROSPIKE_OK on success. Otherwise an error occurred.
 *
 * @relates aerospike
 */
as_status
aerospike_connect(aerospike* as, as_error* err);

/**
 * Close connections to the cluster.
 *
 * ~~~~~~~~~~{.c}
 * aerospike_close(&as, &err);
 * ~~~~~~~~~~
 *
 * @param as 		The aerospike instance to disconnect from a cluster.
 * @param err 		If an error occurs, the err will be populated.
 *
 * @returns AEROSPIKE_OK on success. Otherwise an error occurred. 
 *
 * @relates aerospike
 */
as_status
aerospike_close(aerospike* as, as_error* err);

/**
 * Is cluster connected to any server nodes.
 *
 * ~~~~~~~~~~{.c}
 * bool connected = aerospike_cluster_is_connected(&as);
 * ~~~~~~~~~~
 *
 * @param as 		The aerospike instance to check.
 *
 * @returns true when cluster is connected.
 *
 * @relates aerospike
 */
bool
aerospike_cluster_is_connected(aerospike* as);

/**
 * Do all server nodes in the cluster support async pipelining.
 *
 * @param as 		The aerospike instance to check.
 *
 * @returns true when all server nodes support pipelining.
 *
 * @relates aerospike
 */
bool
aerospike_has_pipelining(aerospike* as);

/**
 * Should stop socket operation if interrupted by a signal.  Default is false which means
 * the socket operation will be retried until timeout.
 *
 * @relates aerospike
 */
void
aerospike_stop_on_interrupt(bool stop);

/**
 * Remove records in specified namespace/set efficiently.  This method is many orders of magnitude
 * faster than deleting records one at a time.  Works with Aerospike Server versions >= 3.12.
 *
 * This asynchronous server call may return before the truncation is complete.  The user can still
 * write new records after the server returns because new records will have last update times
 * greater than the truncate cutoff (set at the time of truncate call).
 *
 * @param as			Aerospike instance.
 * @param err			If an error occurs, the err will be populated.
 * @param policy		The policy to use for this operation. If NULL, then the default policy will be used.
 * @param ns			Required namespace.
 * @param set			Optional set name.  Pass in NULL to delete all sets in namespace.
 * @param before_nanos	Optionally delete records before record last update time.
 *						Units are in nanoseconds since unix epoch (1970-01-01).
 *						If specified, value must be before the current time.
 *						Pass in 0 to delete all records in namespace/set regardless of last update time.
 * @returns AEROSPIKE_OK on success. Otherwise an error occurred.
 *
 * @relates aerospike
 */
as_status
aerospike_truncate(aerospike* as, as_error* err, as_policy_info* policy, const char* ns, const char* set, uint64_t before_nanos);

/**
 * Refresh the current TLS configuration by reloading its certificate, key, and blacklist files.
 *
 * @param as 		Aerospike instance whose TLS configuration to refresh.
 * @param err		If an error occurs, this will be populated.
 *
 * @returns AEROSPIKE_OK on success. Otherwise an error occurred.
 *
 * @relates aerospike
 */
as_status
aerospike_reload_tls_config(aerospike* as, as_error* err);

#ifdef __cplusplus
} // end extern "C"
#endif
