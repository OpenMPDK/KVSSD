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
 *	@defgroup udf_operations UDF Operations
 *	@ingroup client_operations
 *
 *	The UDF API provides the ability to manage UDFs in the cluster.
 *
 *	Management capabilities include:
 *	- aerospike_udf_list() - 	List the UDF modules in the cluster.
 *	- aerospike_udf_get() -		Download a UDF module.
 *	- aerospike_udf_put() -		Upload a UDF module.
 *	- aerospike_udf_remove() -	Remove a UDF module.
 *
 */

#include <aerospike/aerospike.h>
#include <aerospike/as_error.h>
#include <aerospike/as_policy.h>
#include <aerospike/as_status.h>
#include <aerospike/as_udf.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *	FUNCTIONS
 *****************************************************************************/

/**
 *	List the UDF files in the cluster.
 *
 *	~~~~~~~~~~{.c}
 *	as_udf_files files;
 *	as_udf_files_init(&files, 0);
 *	
 *	if ( aerospike_udf_list(&as, &err, NULL, &files) != AEROSPIKE_OK ) {
 *		fprintf(stderr, "error(%d) %s at [%s:%d]", err.code, err.message, err.file, err.line);
 *	}
 *	else {
 *		printf("files[%d]:\n", files.size);
 *		for( int i = 0; i < files.size; i++ ) {
 *			as_udf_file * file = &files.entries[i];
 *			printf("  - %s (%d) [%s]\n", file->name, file->type, file->hash);
 *		}
 *	}
 *	
 *	as_udf_files_destroy(&files);
 *	~~~~~~~~~~
 *
 *
 *	@param as			The aerospike instance to use for this operation.
 *	@param err			The as_error to be populated if an error occurs.
 *	@param policy		The policy to use for this operation. If NULL, then the default policy will be used.
 *	@param files 		The list to populate with the results from the request.
 *
 *	@return AEROSPIKE_OK if successful. Otherwise an error occurred.
 *
 *	@ingroup udf_operations
 */
as_status aerospike_udf_list(
	aerospike * as, as_error * err, const as_policy_info * policy, 
	as_udf_files * files
	);


/**
 *	Get specified UDF file from the cluster.
 *
 *	~~~~~~~~~~{.c}
 *	as_udf_file file;
 *	as_udf_file_init(&file);
 *	
 *	if ( aerospike_udf_get(&as, &err, NULL, "my.lua", AS_UDF_TYPE_LUA, &file) != AEROSPIKE_OK ) {
 *	    fprintf(stderr, "error(%d) %s at [%s:%d]", err.code, err.message, err.file, err.line);
 *	}
 *	else {
 *	    printf("%s type=%d hash=%s size=%d:\n", file.name, file.type. file.hash, file.content.size);
 *	    if ( file.type == AS_UDF_TYPE_UDF ) {
 *	        printf("%s", file.content.bytes)
 *	    }
 *	}
 *	
 *	as_udf_file_destroy(&file);
 *	~~~~~~~~~~
 *
 *
 *	@param as			The aerospike instance to use for this operation.
 *	@param err			The as_error to be populated if an error occurs.
 *	@param policy		The policy to use for this operation. If NULL, then the default policy will be used.
 *	@param filename		The name of the UDF file.
 *	@param type			The type of UDF file.
 *	@param file			The file from the cluster.
 *
 *	@return AEROSPIKE_OK if successful. Otherwise an error occurred.
 *
 *	@ingroup udf_operations
 */
as_status aerospike_udf_get(
	aerospike * as, as_error * err, const as_policy_info * policy, 
	const char * filename, as_udf_type type, as_udf_file * file
	);

/**
 *	Put a UDF file into the cluster.  This function will return before the put is completed on
 *	all nodes.  Use aerospike_udf_put_wait() when need to wait for completion.
 *
 *	~~~~~~~~~~{.c}
 *	as_bytes content;
 *	as_bytes_init(&content);
 *	...
 *	
 *	if ( aerospike_udf_put(&as, &err, NULL, "my.lua", AS_UDF_TYPE_LUA, &content) != AEROSPIKE_OK ) {
 *	    fprintf(stderr, "error(%d) %s at [%s:%d]", err.code, err.message, err.file, err.line);
 *	}
 *	
 *	as_bytes_destroy(&content);
 *	~~~~~~~~~~
 *
 *
 *	@param as			The aerospike instance to use for this operation.
 *	@param err			The as_error to be populated if an error occurs.
 *	@param policy		The policy to use for this operation. If NULL, then the default policy will be used.
 *	@param filename		The name of the UDF file.
 *	@param type			The type of UDF file.
 *	@param content		The file of the UDF file.
 *
 *	@return AEROSPIKE_OK if UDF put was started. Otherwise an error occurred.
 *
 *	@ingroup udf_operations
 */
as_status aerospike_udf_put(
	aerospike * as, as_error * err, const as_policy_info * policy, 
	const char * filename, as_udf_type type, as_bytes * content
	);

/**
 *	Wait for asynchronous udf put to complete using given polling interval.
 *
 *	~~~~~~~~~~{.c}
 *	as_bytes content;
 *	as_bytes_init(&content);
 *
 *	if (aerospike_udf_put(&as, &err, NULL, "my.lua", AS_UDF_TYPE_LUA, &content) == AEROSPIKE_OK ) {
 *	    aerospike_udf_put_wait(&as, &err, NULL, "my.lua", 0);
 *	}
 *	as_bytes_destroy(&content);
 *	~~~~~~~~~~
 *
 *	@param as			The aerospike instance to use for this operation.
 *	@param err			The as_error to be populated if an error occurs.
 *	@param policy		The policy to use for this operation. If NULL, then the default policy will be used.
 *	@param filename		The name of the UDF file.
 *	@param interval_ms	The polling interval in milliseconds. If zero, 1000 ms is used.
 *
 *	@return AEROSPIKE_OK if successful. Otherwise an error occurred.
 *
 *	@ingroup udf_operations
 */
as_status aerospike_udf_put_wait(
	aerospike * as, as_error * err, const as_policy_info * policy,
	const char * filename, uint32_t interval_ms);

/**
 *	Remove a UDF file from the cluster. This function will return before the remove is completed on
 *	all nodes.  Use aerospike_udf_remove_wait() when need to wait for completion.
 *
 *	~~~~~~~~~~{.c}
 *	if ( aerospike_udf_remove(&as, &err, NULL, "my.lua") != AEROSPIKE_OK ) {
 *	    fprintf(stderr, "error(%d) %s at [%s:%d]", err.code, err.message, err.file, err.line);
 *	}
 *	~~~~~~~~~~
 *
 *	@param as			The aerospike instance to use for this operation.
 *	@param err			The as_error to be populated if an error occurs.
 *	@param policy		The policy to use for this operation. If NULL, then the default policy will be used.
 *	@param filename 	The name of the UDF file.
 *
 *	@return AEROSPIKE_OK if remove was started. Otherwise an error occurred.
 *
 *	@ingroup udf_operations
 */
as_status aerospike_udf_remove(
	aerospike * as, as_error * err, const as_policy_info * policy, const char * filename
	);

/**
 *	Wait for asynchronous udf remove to complete using given polling interval.
 *
 *	~~~~~~~~~~{.c} *
 *	if (aerospike_udf_remove(&as, &err, NULL, "my.lua") == AEROSPIKE_OK) {
 *	    aerospike_udf_remove_wait(&as, &err, NULL, "my.lua", 0);
 *	}
 *	~~~~~~~~~~
 *
 *	@param as			The aerospike instance to use for this operation.
 *	@param err			The as_error to be populated if an error occurs.
 *	@param policy		The policy to use for this operation. If NULL, then the default policy will be used.
 *	@param filename		The name of the UDF file.
 *	@param interval_ms	The polling interval in milliseconds. If zero, 1000 ms is used.
 *
 *	@return AEROSPIKE_OK if successful. Otherwise an error occurred.
 *
 *	@ingroup udf_operations
 */
as_status aerospike_udf_remove_wait(
	aerospike * as, as_error * err, const as_policy_info * policy,
	const char * filename, uint32_t interval_ms);

#ifdef __cplusplus
} // end extern "C"
#endif
