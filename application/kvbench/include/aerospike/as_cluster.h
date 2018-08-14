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

#include <aerospike/as_config.h>
#include <aerospike/as_node.h>
#include <aerospike/as_partition.h>
#include <aerospike/as_policy.h>
#include <aerospike/as_thread_pool.h>

#ifdef __cplusplus
extern "C" {
#endif
	
// Concurrency kit needs to be under extern "C" when compiling C++.
#include <aerospike/ck/ck_pr.h>

/******************************************************************************
 * TYPES
 *****************************************************************************/

/**
 * @private
 * Reference counted array of server node pointers.
 */
typedef struct as_nodes_s {
	/**
	 * @private
	 * Reference count of node array.
	 */
	uint32_t ref_count;
	
	/**
	 * @private
	 * Length of node array.
	 */
	uint32_t size;
	
	/**
	 * @private
	 * Server node array.
	 */
	as_node* array[];
} as_nodes;

/**
 * @private
 * Reference counted release function definition.
 */
typedef void (*as_release_fn) (void* value);

/**
 * @private
 * Reference counted data to be garbage collected.
 */
typedef struct as_gc_item_s {
	/**
	 * @private
	 * Reference counted data to be garbage collected.
	 */
	void* data;
	
	/**
	 * @private
	 * Release function.
	 */
	as_release_fn release_fn;
} as_gc_item;

/**
 * Cluster of server nodes.
 */
typedef struct as_cluster_s {
	/**
	 * @private
	 * Active nodes in cluster.
	 */
	as_nodes* nodes;
	
	/**
	 * @private
	 * Hints for best node for a partition.
	 */
	as_partition_tables* partition_tables;
		
	/**
	 * @private
	 * Nodes to be garbage collected.
	 */
	as_vector* /* <as_gc_item> */ gc;
	
	/**
	 * @private
	 * Shared memory implementation of cluster.
	 */
	struct as_shm_info_s* shm_info;
	
	/**
	 * @private
	 * User name in UTF-8 encoded bytes.
	 */
	char* user;
	
	/**
	 * @private
	 * Password in hashed format in bytes.
	 */
	char* password;
	
	/**
	 * @private
	 * Expected cluster name for all nodes.  May be null.
	 */
	char* cluster_name;
	
	/**
	 * Cluster event function that will be called when nodes are added/removed from the cluster.
	 */
	as_cluster_event_callback event_callback;

	/**
	 * Cluster event user data that will be passed back to event_callback.
	 */
	void* event_callback_udata;

	/**
	 * Pending async commands counter array for all event loops.
	 */
	int* pending;

	/**
	 * @private
	 * Initial seed hosts specified by user.
	 */
	as_vector* /* <as_host> */ seeds;

	/**
	 * @private
	 * A IP translation table is used in cases where different clients use different server
	 * IP addresses.  This may be necessary when using clients from both inside and outside
	 * a local area network.  Default is no translation.
	 *
	 * The key is the IP address returned from friend info requests to other servers.  The
	 * value is the real IP address used to connect to the server.
	 */
	as_vector* /* <as_addr_map> */ ip_map;

	/**
	 * @private
	 * TLS parameters
	 */
	as_tls_context tls_ctx;
	
	/**
	 * @private
	 * Pool of threads used to query server nodes in parallel for batch, scan and query.
	 */
	as_thread_pool thread_pool;
		
	/**
	 * @private
	 * Cluster tend thread.
	 */
	pthread_t tend_thread;
	
	/**
	 * @private
	 * Lock for adding/removing seeds.
	 */
	pthread_mutex_t seed_lock;

	/**
	 * @private
	 * Lock for the tend thread to wait on with the tend interval as timeout.
	 * Normally locked, resulting in waiting a full interval between
	 * tend iterations.  Upon cluster shutdown, unlocked by the main
	 * thread, allowing a fast termination of the tend thread.
	 */
	pthread_mutex_t tend_lock;
	
	/**
	 * @private
	 * Tend thread identifier to be used with tend_lock.
	 */
	pthread_cond_t tend_cond;

	/**
	 * @private
	 * Milliseconds between cluster tends.
	 */
	uint32_t tend_interval;

	/**
	 * @private
	 * Maximum number of synchronous connections allowed per server node.
	 */
	uint32_t max_conns_per_node;
	
	/**
	 * @private
	 * Maximum number of asynchronous (non-pipeline) connections allowed for each node.
	 * Async transactions will be rejected if the maximum async node connections would be exceeded.
	 * This variable is ignored if asynchronous event loops are not created.
	 */
	uint32_t async_max_conns_per_node;
	
	/**
	 * @private
	 * Maximum number of pipeline connections allowed for each node.
	 * Pipeline transactions will be rejected if the maximum pipeline node connections would be exceeded.
	 * This variable is ignored if asynchronous event loops are not created.
	 */
	uint32_t pipe_max_conns_per_node;
	
	/**
	 * @private
	 * Number of synchronous connection pools used for each node.
	 */
	uint32_t conn_pools_per_node;

	/**
	 * @private
	 * Initial connection timeout in milliseconds.
	 */
	uint32_t conn_timeout_ms;
	
	/**
	 * @private
	 * Maximum socket idle in seconds.
	 */
	uint32_t max_socket_idle;
	
	/**
	 * @private
	 * Random node index.
	 */
	uint32_t node_index;

	/**
	 * @private
	 * Total number of data partitions used by cluster.
	 */
	uint16_t n_partitions;
	
	/**
	 * @private
	 * If "services-alternate" should be used instead of "services"
	 */
	bool use_services_alternate;
	
	/**
	 * @private
	 * Should continue to tend cluster.
	 */
	volatile bool valid;
} as_cluster;

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/

/**
 * Create and initialize cluster.
 */
as_status
as_cluster_create(as_config* config, as_error* err, as_cluster** cluster);

/**
 * Close all connections and release memory associated with cluster.
 */
void
as_cluster_destroy(as_cluster* cluster);

/**
 * Is cluster connected to any server nodes.
 */
bool
as_cluster_is_connected(as_cluster* cluster);

/**
 * Get all node names in cluster.
 */
void
as_cluster_get_node_names(as_cluster* cluster, int* n_nodes, char** node_names);

/**
 * Reserve reference counted access to cluster nodes.
 */
static inline as_nodes*
as_nodes_reserve(as_cluster* cluster)
{
	as_nodes* nodes = (as_nodes *)ck_pr_load_ptr(&cluster->nodes);
	//ck_pr_fence_acquire();
	ck_pr_inc_32(&nodes->ref_count);
	return nodes;
}

/**
 * Release reference counted access to cluster nodes.
 */
static inline void
as_nodes_release(as_nodes* nodes)
{
	//ck_pr_fence_release();
	
	bool destroy;
	ck_pr_dec_32_zero(&nodes->ref_count, &destroy);
	
	if (destroy) {
		cf_free(nodes);
	}
}

/**
 * 	Add seed to cluster.
 */
void
as_cluster_add_seed(as_cluster* cluster, const char* hostname, const char* tls_name, uint16_t port);

/**
 * 	Remove seed from cluster.
 */
void
as_cluster_remove_seed(as_cluster* cluster, const char* hostname, uint16_t port);

/**
 * @private
 * Change user and password that is used to authenticate with cluster servers.
 */
void
as_cluster_change_password(as_cluster* cluster, const char* user, const char* password);

/**
 * @private
 * Get random node in the cluster.
 * as_nodes_release() must be called when done with node.
 */
as_node*
as_node_get_random(as_cluster* cluster);

/**
 * @private
 * Get node given node name.
 * as_nodes_release() must be called when done with node.
 */
as_node*
as_node_get_by_name(as_cluster* cluster, const char* name);

/**
 * @private
 * Reserve reference counted access to partition tables.
 * as_partition_tables_release() must be called when done with tables.
 */
static inline as_partition_tables*
as_partition_tables_reserve(as_cluster* cluster)
{
	as_partition_tables* tables = (as_partition_tables *)ck_pr_load_ptr(&cluster->partition_tables);
	ck_pr_inc_32(&tables->ref_count);
	return tables;
}

/**
 * @private
 * Release reference counted access to partition tables.
 */
static inline void
as_partition_tables_release(as_partition_tables* tables)
{
	bool destroy;
	ck_pr_dec_32_zero(&tables->ref_count, &destroy);
	
	if (destroy) {
		cf_free(tables);
	}
}

/**
 * @private
 * Get partition table given namespace.
 */
static inline as_partition_table*
as_cluster_get_partition_table(as_cluster* cluster, const char* ns)
{
	// Partition tables array size does not currently change after first cluster tend.
	// Also, there is a one second delayed garbage collection coupled with as_partition_tables_get()
	// being very fast.  Reference counting the tables array is not currently necessary, but do it
	// anyway in case the server starts supporting dynamic namespaces.
	as_partition_tables* tables = as_partition_tables_reserve(cluster);
	as_partition_table* table = as_partition_tables_get(tables, ns);
	as_partition_tables_release(tables);
	return table;
}

/**
 * @private
 * Get mapped node given partition.  
 * as_nodes_release() must be called when done with node.
 */
as_node*
as_partition_get_node(as_cluster* cluster, as_partition* p, as_policy_replica replica, bool use_master, bool cp_mode);

/**
 * @private
 * Get mapped node given digest key.  If there is no mapped node, another node is used based on replica.
 * If successful, as_nodes_release() must be called when done with node.
 */
as_status
as_cluster_get_node(
	struct as_cluster_s* cluster, as_error* err, const char* ns, const uint8_t* digest,
	as_policy_replica replica, bool master, as_node** node
	);

#ifdef __cplusplus
} // end extern "C"
#endif
