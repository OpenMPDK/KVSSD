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

#include <aerospike/as_node.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 * MACROS
 *****************************************************************************/

/**
 * Maximum namespace size including null byte.  Effective maximum length is 31.
 */
#define AS_MAX_NAMESPACE_SIZE 32

/******************************************************************************
 * TYPES
 *****************************************************************************/

/**
 * @private
 * Map of namespace data partitions to nodes.
 */
typedef struct as_partition_s {
	/**
	 * @private
	 * Master node for this partition.
	 */
	as_node* master;
	
	/**
	 * @private
	 * Prole node for this partition.
	 * TODO - not ideal for replication factor > 2.
	 */
	as_node* prole;

	/**
	 * @private
	 * Currrent regime for CP mode.
	 */
	uint32_t regime;
} as_partition;

/**
 * @private
 * Map of namespace to data partitions.
 */
typedef struct as_partition_table_s {
	/**
	 * @private
	 * Namespace
	 */
	char ns[AS_MAX_NAMESPACE_SIZE];

	/**
	 * @private
	 * Is namespace running in CP mode.
	 */
	bool cp_mode;
	char pad[3];

	/**
	 * @private
	 * Fixed length of partition array.
	 */
	uint32_t size;

	/**
	 * @private
	 * Array of partitions for a given namespace.
	 */
	as_partition partitions[];
} as_partition_table;

/**
 * @private
 * Reference counted array of partition table pointers.
 */
typedef struct as_partition_tables_s {
	/**
	 * @private
	 * Reference count of partition table array.
	 */
	uint32_t ref_count;
	
	/**
	 * @private
	 * Length of partition table array.
	 */
	uint32_t size;

	/**
	 * @private
	 * Partition table array.
	 */
	as_partition_table* array[];
} as_partition_tables;

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/

/**
 * @private
 * Create reference counted structure containing partition tables.
 */
as_partition_tables*
as_partition_tables_create(uint32_t capacity);

/**
 * @private
 * Destroy and release memory for partition table.
 */
void
as_partition_table_destroy(as_partition_table* table);

/**
 * @private
 * Get partition table given namespace.
 */
as_partition_table*
as_partition_tables_get(as_partition_tables* tables, const char* ns);

/**
 * @private
 * Is node referenced in any partition table.
 */
bool
as_partition_tables_find_node(as_partition_tables* tables, as_node* node);
	
/**
 * @private
 * Return partition ID given digest.
 */
static inline uint32_t
as_partition_getid(const uint8_t* digest, uint32_t n_partitions)
{
	return (*(uint16_t*)digest) & (n_partitions - 1);
}

#ifdef __cplusplus
} // end extern "C"
#endif
