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
 *	@defgroup query_operations Query Operations
 *	@ingroup client_operations
 *
 *	The Aerospike Query Operations provide the ability to query data in the 
 *	Aerospike database. The queries can only be performed on secondary indexes, 
 *	which have been created in the database. To scan all the records in the
 *	database, then you must use the @ref scan_operations.
 *	
 *	## Usage
 *
 *	Before you can execute a query, you first need to build a query using 
 *	as_query. See as_query for details on building queries.
 *
 *	Once you have a query defined, then you can execute the query :
 *
 *	-	aerospike_query_foreach() -	Executes a query and invokes a callback
 *		function for each result returned.
 *	
 *	When aerospike_query_foreach() is executed, it will process the results
 *	and create records on the stack. Because the records are on the stack, 
 *	they will only be available within the context of the callback function.
 *
 *
 *	## Walk-through
 *	
 *	First, we define a query using as_query. The query will be for the "test"
 *	namespace and "demo" set. We will add a where predicate on "bin2", on which
 *	we have already created a secondary index. 
 *	
 *	~~~~~~~~~~{.c}
 *	as_query query;
 *	as_query_init(&query, "test", "demo");
 *
 *	as_query_where_init(&query, 1);
 *	as_query_where(&query, "bin2", as_integer_equals(100));
 *	~~~~~~~~~~
 *
 *	Now that we have a query defined, we want to execute it using 
 *	aerospike_query_foreach().
 *	
 *	~~~~~~~~~~{.c}
 *	if ( aerospike_query_foreach(&as, &err, NULL, &query, callback, NULL) != AEROSPIKE_OK ) {
 *		fprintf(stderr, "error(%d) %s at [%s:%d]", err.code, err.message, err.file, err.line);
 *	}
 *	~~~~~~~~~~
 *
 *	The callback provided to the function above is implemented as:
 *	
 *	~~~~~~~~~~{.c}
 *	bool callback(const as_val * val, void * udata) {
 *		as_record * rec = as_record_fromval(val);
 *		if ( !rec ) return false;
 *		fprintf("record contains %d bins", as_record_numbins(rec));
 *		return true;
 *	}
 *	~~~~~~~~~~
 *
 *	An as_query is simply a query definition, so it does not contain any state,
 *	allowing it to be reused for multiple query operations. 
 *	
 *	When you are finished with the query, you should destroy the resources 
 *	allocated to it:
 *
 *	~~~~~~~~~~{.c}
 *	as_query_destroy(&query);
 *	~~~~~~~~~~
 *
 */

#include <aerospike/aerospike.h>
#include <aerospike/as_error.h>
#include <aerospike/as_event.h>
#include <aerospike/as_job.h>
#include <aerospike/as_policy.h>
#include <aerospike/as_query.h>
#include <aerospike/as_record.h>
#include <aerospike/as_status.h>
#include <aerospike/as_stream.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *	TYPES
 *****************************************************************************/

/**
 *	This callback will be called for each value or record returned from a query.
 *	Multiple threads will likely be calling this callback in parallel.  Therefore,
 *	your callback implementation should be thread safe.
 *
 *	The aerospike_query_foreach() function accepts this callback.
 *
 *	~~~~~~~~~~{.c}
 *	bool my_callback(as_val * val, void * udata) {
 *		return true;
 *	}
 *	~~~~~~~~~~
 *
 *	@param val 			The value received from the query.
 *	@param udata 		User-data provided to the calling function.
 *
 *	@return `true` to continue to the next value. Otherwise, iteration will end.
 *
 *	@ingroup query_operations
 */
typedef bool (*aerospike_query_foreach_callback)(const as_val* val, void* udata);

/**
 *	Asynchronous query user callback.  This function is called for each record returned.
 *	This function is also called once when the query completes or an error has occurred.
 *
 *	@param err			This error structure is only populated when the command fails. Null on success.
 *	@param record 		Returned record.  Use as_val_reserve() on record to prevent calling function from destroying.
 *						The record will be null on final query completion or query error.
 *	@param udata 		User data that is forwarded from asynchronous command function.
 *	@param event_loop 	Event loop that this command was executed on.  Use this event loop when running
 *						nested asynchronous commands when single threaded behavior is desired for the
 *						group of commands.
 *
 *	@return `true` to continue to the next value. Otherwise, the query will end.
 *
 *	@ingroup query_operations
 */
typedef bool (*as_async_query_record_listener)(as_error* err, as_record* record, void* udata, as_event_loop* event_loop);

/******************************************************************************
 *	FUNCTIONS
 *****************************************************************************/

/**
 *	Execute a query and call the callback function for each result item.
 *	Multiple threads will likely be calling the callback in parallel.  Therefore,
 *	your callback implementation should be thread safe.
 *
 *	~~~~~~~~~~{.c}
 *	as_query query;
 *	as_query_init(&query, "test", "demo");
 *	as_query_select(&query, "bin1");
 *	as_query_where(&query, "bin2", as_integer_equals(100));
 *	
 *	if ( aerospike_query_foreach(&as, &err, NULL, &query, callback, NULL) != AEROSPIKE_OK ) {
 *		fprintf(stderr, "error(%d) %s at [%s:%d]", err.code, err.message, err.file, err.line);
 *	}
 *	
 *	as_query_destroy(&query);
 *	~~~~~~~~~~
 *
 *	@param as			The aerospike instance to use for this operation.
 *	@param err			The as_error to be populated if an error occurs.
 *	@param policy		The policy to use for this operation. If NULL, then the default policy will be used.
 *	@param query		The query to execute against the cluster.
 *	@param callback		The callback function to call for each result value.
 *	@param udata		User-data to be passed to the callback.
 *
 *	@return AEROSPIKE_OK on success, otherwise an error.
 *
 *	@ingroup query_operations
 */
as_status
aerospike_query_foreach(
	aerospike* as, as_error* err, const as_policy_query* policy, const as_query* query,
	aerospike_query_foreach_callback callback, void* udata
	);

/**
 *	Asynchronously execute a query and call the listener function for each result item.
 *	Standard secondary index queries are supported, but aggregation queries are not supported
 *	in async mode.
 *
 *	~~~~~~~~~~{.c}
 *	bool my_listener(as_error* err, as_record* record, void* udata, as_event_loop* event_loop)
 *	{
 *		if (err) {
 *			printf("Query failed: %d %s\n", err->code, err->message);
 *			return false;
 *		}
 *
 *		if (! record) {
 *			printf("Query ended\n");
 *			return false;
 *		}
 *
 *		// Process record
 *		// Do not call as_record_destroy() because the calling function will do that for you.
 *		return true;
 *	}
 *	as_query query;
 *	as_query_init(&query, "test", "demo");
 *	as_query_select(&query, "bin1");
 *	as_query_where(&query, "bin2", as_integer_equals(100));
 *
 *	if ( aerospike_query_foreach(&as, &err, NULL, &query, callback, NULL) != AEROSPIKE_OK ) {
 *		fprintf(stderr, "error(%d) %s at [%s:%d]", err.code, err.message, err.file, err.line);
 *	}
 *
 *	as_query_destroy(&query);
 *	~~~~~~~~~~
 *
 *	@param as			The aerospike instance to use for this operation.
 *	@param err			The as_error to be populated if an error occurs.
 *	@param policy		The policy to use for this operation. If NULL, then the default policy will be used.
 *	@param query		The query to execute against the cluster.
 *	@param listener		The function to be called for each returned value.
 *	@param udata		User-data to be passed to the callback.
 *	@param event_loop 	Event loop assigned to run this command. If NULL, an event loop will be choosen by round-robin.
 *
 *	@return AEROSPIKE_OK if async query succesfully queued. Otherwise an error.
 *
 *	@ingroup query_operations
 */
as_status
aerospike_query_async(
	aerospike* as, as_error* err, const as_policy_query* policy, const as_query* query,
	as_async_query_record_listener listener, void* udata, as_event_loop* event_loop
	);
	
/**
 *	Apply user defined function on records that match the query filter.
 *	Records are not returned to the client.
 *	This asynchronous server call will return before command is complete.
 *	The user can optionally wait for command completion.
 *
 *	~~~~~~~~~~{.c}
 *	as_query query;
 *	as_query_init(&query, "test", "demo");
 *	as_query_select(&query, "bin1");
 *	as_query_where(&query, "bin2", as_integer_equals(100));
 *	as_query_apply(&query, "my_lua.lua", "my_lua_function", NULL);
 *	uint64_t query_id = 0;
 *
 *	if (aerospike_query_background(&as, &err, NULL, &query, &query_id) == AEROSPIKE_OK) {
 *		aerospike_query_wait(as, &err, NULL, &query, query_id, 0);
 *	}
 *	else {
 *		fprintf(stderr, "error(%d) %s at [%s:%d]", err.code, err.message, err.file, err.line);
 *	}
 *
 *	as_query_destroy(&query);
 *	~~~~~~~~~~
 *
 *	@param as			The aerospike instance to use for this operation.
 *	@param err			The as_error to be populated if an error occurs.
 *	@param policy		The policy to use for this operation. If NULL, then the default policy will be used.
 *	@param query		The query to execute against the cluster.
 *	@param query_id		The id for the query job, which can be used for querying the status of the query.
 *
 *	@return AEROSPIKE_OK on success, otherwise an error.
 *
 *	@ingroup query_operations
 */
as_status
aerospike_query_background(
	aerospike* as, as_error* err, const as_policy_write* policy,
	const as_query* query, uint64_t* query_id
	);

/**
 *	Wait for a background query to be completed by servers.
 *
 *	@param as			The aerospike instance to use for this operation.
 *	@param err			The as_error to be populated if an error occurs.
 *	@param policy		The info policy to use for this operation. If NULL, then the default policy will be used.
 *	@param query		The query that was executed against the cluster.
 *	@param query_id		The id for the query job, which can be used for querying the status of the query.
 *	@param interval_ms	Polling interval in milliseconds. If zero, 1000 ms is used.
 *
 *	@return AEROSPIKE_OK on success, otherwise an error.
 *
 *	@ingroup query_operations
 */
static inline as_status
aerospike_query_wait(
   aerospike* as, as_error* err, const as_policy_info* policy,
   const as_query* query, uint64_t query_id, uint32_t interval_ms
   )
{
	const char* module = (query->where.size > 0)? "query" : "scan";
	return aerospike_job_wait(as, err, policy, module, query_id, interval_ms);
}

/**
 *	Check the progress of a background query running on the database.
 *
 *	@param as			The aerospike instance to use for this operation.
 *	@param err			The as_error to be populated if an error occurs.
 *	@param policy		The info policy to use for this operation. If NULL, then the default policy will be used.
 *	@param query		The query that was executed against the cluster.
 *	@param query_id		The id for the query job, which can be used for querying the status of the query.
 *	@param info			Information about this background query, to be populated by this operation.
 *
 *	@return AEROSPIKE_OK on success, otherwise an error.
 *
 *	@ingroup query_operations
 */
static inline as_status
aerospike_query_info(
	aerospike* as, as_error* err, const as_policy_info* policy,
	const as_query* query, uint64_t query_id, as_job_info* info
	)
{
	const char* module = (query->where.size > 0)? "query" : "scan";
	return aerospike_job_info(as, err, policy, module, query_id, false, info);
}

#ifdef __cplusplus
} // end extern "C"
#endif
