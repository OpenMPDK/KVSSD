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

#include <aerospike/as_integer.h>
#include <aerospike/as_string.h>
#include <aerospike/as_geojson.h>
#include <aerospike/as_bytes.h>
#include <aerospike/as_list.h>
#include <aerospike/as_map.h>
#include <aerospike/as_val.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *	MACROS
 *****************************************************************************/

/**
 *	Maximum bin name size
 */
#define AS_BIN_NAME_MAX_SIZE 15

/**
 *	Maximum bin name length
 */
#define AS_BIN_NAME_MAX_LEN (AS_BIN_NAME_MAX_SIZE - 1)

/******************************************************************************
 *	TYPES
 *****************************************************************************/

/**
 *	Bin Name
 */
typedef char as_bin_name[AS_BIN_NAME_MAX_SIZE];

/**
 *	Bin Value
 */
typedef union as_bin_value_s {
	as_val nil;
	as_integer integer;
	as_double dbl;
	as_string string;
	as_bytes bytes;
	as_list list;
	as_map map;
} as_bin_value;

/**
 *	Represents a bin of a record. Each bin is a (name,value) pair. 
 *
 *	Bins of a record should never be directly accessed. The bins should only
 *	be modified via as_record functions. The only time an as_bin is directly 
 *	accessible is during iteration via as_record_iterator, but the 
 *	as_bin functions should be used to read the values.
 *
 *	@ingroup client_objects
 */
typedef struct as_bin_s {

	/**
	 *	Bin name.
	 */
	as_bin_name name;

	/**
	 *	Bin value.
	 */
	as_bin_value value;

	/**
	 *	Bin value pointer.
	 *	If NULL, then there is no value.
	 *	It can point to as_bin.value or a different value.
	 */
	as_bin_value * valuep;
	
} as_bin;

/**
 * Sequence of bins.
 */
typedef struct as_bins_s {

	/**
	 *	@private
	 *	If true, then as_record_destroy() will free data
	 */
	bool _free;

	/**
	 *	Number of entries allocated to data.
	 */
	uint16_t capacity;

	/**
	 *	Number of entries currently holding data.
	 */
	uint16_t size;

	/**
	 *	Storage for bins
	 */
	as_bin * entries;

} as_bins;

/******************************************************************************
 *	INLINE FUNCTIONS
 *****************************************************************************/

/**
 *	Get the name of the bin.
 *
 *	~~~~~~~~~~{.c}
 *	char * name = as_bin_get_name(bin);
 *	~~~~~~~~~~
 *
 *	@param bin 	The bin to get the name of.
 *
 *	@return The name of the bin.
 *
 *	@relates as_bin
 */
static inline char * as_bin_get_name(const as_bin * bin) {
	return (char *) bin->name;
}


/**
 *	Get the value of the bin.
 *
 *	~~~~~~~~~~{.c}
 *	as_bin_value val = as_bin_get_value(bin);
 *	~~~~~~~~~~
 *
 *	@param bin 	The bin to get the value of.
 *
 *	@return The value of the bin.
 *
 *	@relates as_bin
 */
static inline as_bin_value * as_bin_get_value(const as_bin * bin) {
	return bin->valuep;
}


/**
 *	Get the type for the value of the bin.
 *
 *	~~~~~~~~~~{.c}
 *	as_val_t type = as_bin_get_type(bin);
 *	~~~~~~~~~~
 *
 *	@param bin 	The bin to get value's type.
 *
 *	@return The type of the bin's value
 *
 *	@relates as_bin
 */
static inline as_val_t as_bin_get_type(const as_bin * bin) {
	return as_val_type(bin->valuep);
}

#ifdef __cplusplus
} // end extern "C"
#endif
