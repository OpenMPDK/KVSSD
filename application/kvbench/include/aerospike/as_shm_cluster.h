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
#include <aerospike/as_partition.h>
#include <citrusleaf/cf_queue.h>

#ifdef __cplusplus
extern "C" {
#endif

// Concurrency kit needs to be under extern "C" when compiling C++.
#include <aerospike/ck/ck_spinlock.h>
#include <aerospike/ck/ck_swlock.h>

/******************************************************************************
 *	TYPES
 *****************************************************************************/

/**
 *	@private
 *	Shared memory representation of node. 48 bytes.
 */
typedef struct as_node_shm_s {
	/**
	 *	@private
	 *	Node name.
	 */
	char name[AS_NODE_NAME_SIZE];
		
	/**
	 *	@private
	 *	Lightweight node read/write lock.
	 */
	ck_swlock_t lock;
	
	/**
	 *	@private
	 *	Socket address.
	 */
	struct sockaddr_storage addr;

	/**
	 *	@private
	 *	TLS certificate name (needed for TLS only).
	 */
	char tls_name[AS_HOSTNAME_SIZE];
	
	/**
	 *	@private
	 *	Features supported by server.  Stored in bitmap.
	 */
	uint32_t features;

	/**
	 *	@private
	 *	Is node currently active.
	 */
	uint8_t active;
	
	/**
	 *	@private
	 *	Pad to 8 byte boundary.
	 */
	char pad[3];
} as_node_shm;

/**
 *	@private
 *  Shared memory representation of map of namespace data partitions to nodes. 8 bytes.
 */
typedef struct as_partition_shm_s {
	/**
	 *	@private
	 *	Master node index offset.
	 */
	uint32_t master;

	/**
	 *	@private
	 *	Prole node index offset.
	 */
	uint32_t prole;

	/**
	 *	@private
	 *	Current regime for CP mode.
	 */
	uint32_t regime;

	/**
	 *	@private
	 *	Pad to 8 byte boundary.
	 */
	uint32_t pad;
} as_partition_shm;

/**
 *	@private
 *	Shared memory representation of map of namespace to data partitions. 32 bytes + partitions size.
 */
typedef struct as_partition_table_shm_s {
	/**
	 *	@private
	 *	Namespace name.
	 */
	char ns[AS_MAX_NAMESPACE_SIZE];
	
	/**
	 *	@private
	 *	Is namespace running in CP mode.
	 */
	uint8_t cp_mode;

	/**
	 *	@private
	 *	Pad to 8 byte boundary.
	 */
	char pad[7];

	/**
	 *	@private
	 *	Array of partitions for a given namespace.
	 */
	as_partition_shm partitions[];
} as_partition_table_shm;

/**
 *	@private
 *	Shared memory cluster map. The map contains fixed arrays of nodes and partition tables.
 *	Each partition table contains a fixed array of partitions.  The shared memory segment will be 
 *	sized on startup and never change afterwards.  If the max nodes or max namespaces are reached, 
 *	the tender client will ignore additional nodes/namespaces and log an error message that the
 *	corresponding array is full.
 */
typedef struct as_cluster_shm_s {
	/**
	 *	@private
	 *	Last time cluster was tended in milliseconds since epoch.
	 */
	uint64_t timestamp;

	/**
	 *	@private
	 *	Cluster tend owner process id.
	 */
	uint32_t owner_pid;
	
	/**
	 *	@private
	 *	Current size of nodes array.
	 */
	uint32_t nodes_size;
	
	/**
	 *	@private
	 *	Maximum size of nodes array.
	 */
	uint32_t nodes_capacity;
	
	/**
	 *	@private
	 *	Nodes generation count.  Incremented whenever a node is added or removed from cluster.
	 */
	uint32_t nodes_gen;
	
	/**
	 *	@private
	 *	Total number of data partitions used by cluster.
	 */
	uint32_t n_partitions;

	/**
	 *	@private
	 *	Current size of partition tables array.
	 */
	uint32_t partition_tables_size;
	
	/**
	 *	@private
	 *	Maximum size of partition tables array.
	 */
	uint32_t partition_tables_capacity;

	/**
	 *	@private
	 *	Cluster offset to partition tables at the end of this structure.
	 */
	uint32_t partition_tables_offset;
	
	/**
	 *	@private
	 *	Bytes required to hold one partition_table.
	 */
	uint32_t partition_table_byte_size;

	/**
	 *	@private
	 *	Spin lock for taking over from a dead cluster tender.
	 */
	ck_spinlock_t take_over_lock;
	
	/**
	 *	@private
	 *	Shared memory master mutex lock.  Used to determine cluster tend owner.
	 */
	uint8_t lock;
	
	/**
	 *	@private
	 *	Has shared memory been fully initialized and populated.
	 */
	uint8_t ready;
	
	/**
	 *	@private
	 *	Pad to 8 byte boundary.
	 */
	char pad[6];

	/*
	 *	@private
	 *	Dynamically allocated node array.
	 */
	as_node_shm nodes[];
	
	// This is where the dynamically allocated partition tables are located.
} as_cluster_shm;

/**
 *	@private
 *	Local data related to shared memory implementation.
 */
typedef struct as_shm_info_s {
	/**
	 *	@private
	 *	Pointer to cluster shared memory.
	 */
	as_cluster_shm* cluster_shm;
	
	/**
	 *	@private
	 *	Array of pointers to local nodes.  
	 *	Array index offsets are synchronized with shared memory node offsets.
	 */
	as_node** local_nodes;
	
	/**
	 *	@private
	 *	Shared memory identifier.
	 */
	int shm_id;
	
	/**
	 *	@private
	 *	Take over shared memory cluster tending if the cluster hasn't been tended by this
	 *	millisecond threshold.
	 */
	uint32_t takeover_threshold_ms;
	
	/**
	 *	@private
	 *	Is this process responsible for performing cluster tending.
	 */
	volatile bool is_tend_master;
} as_shm_info;

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/

/**
 *	@private
 *	Create shared memory implementation of cluster.
 */
as_status
as_shm_create(struct as_cluster_s* cluster, as_error* err, as_config* config);

/**
 *	@private
 *	Destroy shared memory components.
 */
void
as_shm_destroy(struct as_cluster_s* cluster);

/**
 *	@private
 *	Add nodes to shared memory.
 */
void
as_shm_add_nodes(struct as_cluster_s* cluster, as_vector* /* <as_node*> */ nodes_to_add);

/**
 *	@private
 *	Remove nodes from shared memory.
 */
void
as_shm_remove_nodes(struct as_cluster_s* cluster, as_vector* /* <as_node*> */ nodes_to_remove);

/**
 *	@private
 *	Determine if node exists in shared memory partition map.
 */
bool
as_shm_partition_tables_find_node(as_cluster_shm* cluster_shm, as_node* node);

/**
 *	@private
 *	Find partition table for namespace in shared memory.
 */
as_partition_table_shm*
as_shm_find_partition_table(as_cluster_shm* cluster_shm, const char* ns);

/**
 *	@private
 *	Update shared memory partition tables for given namespace.
 */
void
as_shm_update_partitions(as_shm_info* shm_info, const char* ns, char* bitmap_b64, int64_t len, as_node* node, bool master, uint32_t regime);

/**
 *	@private
 *	Get shared memory mapped node given digest key. If there is no mapped node, another node is used based on replica.
 *	If successful, as_nodes_release() must be called when done with node.
 */
as_status
as_shm_cluster_get_node(struct as_cluster_s* cluster, as_error* err, const char* ns, const uint8_t* digest, as_policy_replica replica, bool use_master, as_node** node_pp);

/**
 *	@private
 *	Get shared memory mapped node given partition.
 *	as_nodes_release() must be called when done with node.
 */
as_node*
as_partition_shm_get_node(struct as_cluster_s* cluster, as_partition_shm* p, as_policy_replica replica, bool use_master, bool cp_mode);

/**
 *	@private
 *	Get shared memory partition tables array.
 */
static inline as_partition_table_shm*
as_shm_get_partition_tables(as_cluster_shm* cluster_shm)
{
	return (as_partition_table_shm*) ((char*)cluster_shm + cluster_shm->partition_tables_offset);
}

/**
 *	@private
 *	Get partition table identified by index.
 */
static inline as_partition_table_shm*
as_shm_get_partition_table(as_cluster_shm* cluster_shm, as_partition_table_shm* tables, uint32_t index)
{
	return (as_partition_table_shm*) ((char*)tables + (cluster_shm->partition_table_byte_size * index));
}

/**
 *	@private
 *	Get next partition table in array.
 */
static inline as_partition_table_shm*
as_shm_next_partition_table(as_cluster_shm* cluster_shm, as_partition_table_shm* table)
{
	return (as_partition_table_shm*) ((char*)table + cluster_shm->partition_table_byte_size);
}

#ifdef __cplusplus
} // end extern "C"
#endif
