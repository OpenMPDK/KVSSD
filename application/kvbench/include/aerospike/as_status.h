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

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 *	TYPES
 ******************************************************************************/

/**
 *	Status codes used as return values as as_error.code values.
 */
typedef enum as_status_e {

	/***************************************************************************
	 *	Client Errors
	 **************************************************************************/
	/**
	 *	Synchronous connection error.
	 */
	AEROSPIKE_ERR_CONNECTION = -10,

	/**
	 *	Node invalid or could not be found.
	 */
	AEROSPIKE_ERR_TLS_ERROR = -9,
	
	/**
	 *	Node invalid or could not be found.
	 */
	AEROSPIKE_ERR_INVALID_NODE = -8,
	
	/**
	 *	Asynchronous connection error.
	 */
	AEROSPIKE_ERR_NO_MORE_CONNECTIONS = -7,

	/**
	 *	Asynchronous connection error.
	 */
	AEROSPIKE_ERR_ASYNC_CONNECTION = -6,
	
	/**
	 *	Query or scan was aborted in user's callback.
	 */
	AEROSPIKE_ERR_CLIENT_ABORT = -5,

	/**
	 *	Host name could not be found in DNS lookup.
	 */
	AEROSPIKE_ERR_INVALID_HOST = -4,

	/**
	 *	No more records available when parsing batch, scan or query records.
	 */
	AEROSPIKE_NO_MORE_RECORDS = -3,
	
	/**
	 *	Invalid client API parameter.
	 */
	AEROSPIKE_ERR_PARAM	= -2,
	
	/**
	 *	Generic client API usage error.
	 */
	AEROSPIKE_ERR_CLIENT = -1,
	
	/**
	 *	Deprecated.  Generic client error.  Keep for legacy reasons.
	 */
	AEROSPIKE_ERR = -1,

	/***************************************************************************
	 *	Success
	 **************************************************************************/

	/**
	 *	Generic success.
	 */
	AEROSPIKE_OK = 0,
	
	/***************************************************************************
	 *	Server Errors
	 **************************************************************************/
	
	/**
	 *	Generic error returned by server.
	 */
	AEROSPIKE_ERR_SERVER = 1,
	
	/**
	 *	Record does not exist in database. May be returned by read, or write
	 *	with policy AS_POLICY_EXISTS_UPDATE.
	 */
	AEROSPIKE_ERR_RECORD_NOT_FOUND = 2,
	
	/**
	 *	Generation of record in database does not satisfy write policy.
	 */
	AEROSPIKE_ERR_RECORD_GENERATION = 3,
	
	/**
	 *	Request protocol invalid, or invalid protocol field.
	 */
	AEROSPIKE_ERR_REQUEST_INVALID = 4,
	
	/**
	 *	Record already exists. May be returned by write with policy
	 *	AS_POLICY_EXISTS_CREATE.
	 */
	AEROSPIKE_ERR_RECORD_EXISTS = 5,
	
	/**
	 *	Bin already exists.
	 */
	AEROSPIKE_ERR_BIN_EXISTS = 6,

	/**
	 *	A cluster state change occurred during the request. This may also be
	 *	returned by scan operations with the fail_on_cluster_change flag set.
	 */
	AEROSPIKE_ERR_CLUSTER_CHANGE = 7,
	
	/**
	 *	The server node is running out of memory and/or storage device space
	 *	reserved for the specified namespace.
	 */
	AEROSPIKE_ERR_SERVER_FULL = 8,
	
	/**
	 *	Request timed out.  Can be triggered by client or server.
	 */
	AEROSPIKE_ERR_TIMEOUT = 9,
	
	/**
	 *	XDR is not available for the cluster.
	 */
	AEROSPIKE_ERR_NO_XDR = 10,
	
	/**
	 *	Generic cluster discovery & connection error.
	 */
	AEROSPIKE_ERR_CLUSTER = 11,
	
	/**
	 *	Bin modification operation can't be done on an existing bin due to its
	 *	value type.
	 */
	AEROSPIKE_ERR_BIN_INCOMPATIBLE_TYPE = 12,
	
	/**
	 *	Record being (re-)written can't fit in a storage write block.
	 */
	AEROSPIKE_ERR_RECORD_TOO_BIG = 13,
	
	/**
	 *	Too may concurrent requests for one record - a "hot-key" situation.
	 */
	AEROSPIKE_ERR_RECORD_BUSY = 14,
	
	/**
	 *	Scan aborted by user.
	 */
	AEROSPIKE_ERR_SCAN_ABORTED = 15,
	
	/**
	 *	Sometimes our doc, or our customers wishes, get ahead of us.  We may have
	 *	processed something that the server is not ready for (unsupported feature).
	 */
	AEROSPIKE_ERR_UNSUPPORTED_FEATURE = 16,
	
	/**
	 *	Bin-level replace-only supported on server but not on client.
	 */
    AEROSPIKE_ERR_BIN_NOT_FOUND = 17,
	
	/**
	 *	The server node's storage device(s) can't keep up with the write load.
	 */
	AEROSPIKE_ERR_DEVICE_OVERLOAD = 18,
	
	/**
	 *	Record key sent with transaction did not match key stored on server.
	 */
	AEROSPIKE_ERR_RECORD_KEY_MISMATCH = 19,
	
	/**
	 *	Namespace in request not found on server.
	 */
	AEROSPIKE_ERR_NAMESPACE_NOT_FOUND = 20,
	
	/**
	 *	Sent too-long bin name (should be impossible in this client) or exceeded
	 *	namespace's bin name quota.
	 */
	AEROSPIKE_ERR_BIN_NAME = 21,
	
	/**
	 *	Operation not allowed at this time.
	 */
	AEROSPIKE_ERR_FAIL_FORBIDDEN = 22,

	AEROSPIKE_ERR_FAIL_ELEMENT_NOT_FOUND = 23,

	AEROSPIKE_ERR_FAIL_ELEMENT_EXISTS = 24,

	/**
	 *	There are no more records left for query.
	 */
	AEROSPIKE_QUERY_END = 50,
	
	/**
	 *	Security functionality not supported by connected server.
	 */
	AEROSPIKE_SECURITY_NOT_SUPPORTED = 51,
	
	/**
	 *	Security functionality not enabled by connected server.
	 */
	AEROSPIKE_SECURITY_NOT_ENABLED = 52,
		
	/**
	 *	Security type not supported by connected server.
	 */
	AEROSPIKE_SECURITY_SCHEME_NOT_SUPPORTED = 53,
	
	/**
	 *	Administration command is invalid.
	 */
	AEROSPIKE_INVALID_COMMAND = 54,
	
	/**
	 *	Administration field is invalid.
	 */
	AEROSPIKE_INVALID_FIELD = 55,
	
	/**
	 *	Security protocol not followed.
	 */
	AEROSPIKE_ILLEGAL_STATE = 56,
	
	/**
	 *	User name is invalid.
	 */
	AEROSPIKE_INVALID_USER = 60,
	
	/**
	 *	User was previously created.
	 */
	AEROSPIKE_USER_ALREADY_EXISTS = 61,
	
	/**
	 *	Password is invalid.
	 */
	AEROSPIKE_INVALID_PASSWORD = 62,
	
	/**
	 *	Password has expired.
	 */
	AEROSPIKE_EXPIRED_PASSWORD = 63,
	
	/**
	 *	Forbidden password (e.g. recently used)
	 */
	AEROSPIKE_FORBIDDEN_PASSWORD = 64,
	
	/**
	 *	Security credential is invalid.
	 */
	AEROSPIKE_INVALID_CREDENTIAL = 65,

	/**
	 *	Role name is invalid.
	 */
	AEROSPIKE_INVALID_ROLE = 70,
	
	/**
	 *	Role already exists.
	 */
	AEROSPIKE_ROLE_ALREADY_EXISTS = 71,

	/**
	 *	Privilege is invalid.
	 */
	AEROSPIKE_INVALID_PRIVILEGE = 72,
	
	/**
	 *	User must be authentication before performing database operations.
	 */
	AEROSPIKE_NOT_AUTHENTICATED = 80,
	
	/**
	 *	User does not possess the required role to perform the database operation.
	 */
	AEROSPIKE_ROLE_VIOLATION = 81,
	
	/**
	 *	Generic UDF error.
	 */
	AEROSPIKE_ERR_UDF = 100,
	
	/**
	 *	The requested item in a large collection was not found.
	 */
	AEROSPIKE_ERR_LARGE_ITEM_NOT_FOUND = 125,

	/**
	 * Batch functionality has been disabled.
	 */
	AEROSPIKE_ERR_BATCH_DISABLED = 150,
	
	/**
	 * Batch max requests have been exceeded.
	 */
	AEROSPIKE_ERR_BATCH_MAX_REQUESTS_EXCEEDED = 151,
	
	/**
	 * All batch queues are full.
	 */
	AEROSPIKE_ERR_BATCH_QUEUES_FULL = 152,

	/**
	 * Invalid/Unsupported GeoJSON
	 */
	AEROSPIKE_ERR_GEO_INVALID_GEOJSON = 160,

	/**
	 *	Index found.
	 */
	AEROSPIKE_ERR_INDEX_FOUND = 200,
	
	/**
	 *	Index not found
	 */
	AEROSPIKE_ERR_INDEX_NOT_FOUND = 201,
	
	/**
	 *	Index is out of memory
	 */
	AEROSPIKE_ERR_INDEX_OOM = 202,
	
	/**
	 *	Unable to read the index.
	 */
	AEROSPIKE_ERR_INDEX_NOT_READABLE = 203,
	
	/**
	 *	Generic secondary index error.
	 */
	AEROSPIKE_ERR_INDEX = 204,
	
	/**
	 *	Index name is too long.
	 */
	AEROSPIKE_ERR_INDEX_NAME_MAXLEN = 205,
	
	/**
	 *	System already has maximum allowed indices.
	 */
	AEROSPIKE_ERR_INDEX_MAXCOUNT = 206,
	
	/**
	 *	Query was aborted.
	 */
	AEROSPIKE_ERR_QUERY_ABORTED = 210,
	
	/**
	 *	Query processing queue is full.
	 */
	AEROSPIKE_ERR_QUERY_QUEUE_FULL = 211,

	/**
	 *	Secondary index query timed out on server.
	 */
	AEROSPIKE_ERR_QUERY_TIMEOUT = 212,

	/**
	 *	Generic query error.
	 */
	AEROSPIKE_ERR_QUERY = 213,
	
	/***************************************************************************
	 *	UDF OPERATIONS
	 **************************************************************************/

	/**
	 *	UDF does not exist.
	 */
	AEROSPIKE_ERR_UDF_NOT_FOUND				= 1301,
	/**
	 *	LUA file does not exist.
	 */
	AEROSPIKE_ERR_LUA_FILE_NOT_FOUND		= 1302,

	/***************************************************************************
	 *	Large Data Type (LDT) OPERATIONS
	 **************************************************************************/

	/** Internal LDT error. */
	AEROSPIKE_ERR_LDT_INTERNAL                    = 1400,

	/** LDT item not found */
	AEROSPIKE_ERR_LDT_NOT_FOUND                   = 1401,

	/** Unique key violation: Duplicated item inserted when 'unique key" was set.*/
	AEROSPIKE_ERR_LDT_UNIQUE_KEY                  = 1402,

	/** General error during insert operation. */
	AEROSPIKE_ERR_LDT_INSERT                      = 1403,

	/** General error during search operation. */
	AEROSPIKE_ERR_LDT_SEARCH                      = 1404,

	/** General error during delete operation. */
	AEROSPIKE_ERR_LDT_DELETE                      = 1405,


	/** General input parameter error. */
	AEROSPIKE_ERR_LDT_INPUT_PARM                  = 1409,

    // -------------------------------------------------

	/** LDT Type mismatch for this bin.  */
	AEROSPIKE_ERR_LDT_TYPE_MISMATCH               = 1410,

	/** The supplied LDT bin name is null. */
	AEROSPIKE_ERR_LDT_NULL_BIN_NAME               = 1411,

	/** The supplied LDT bin name must be a string. */
	AEROSPIKE_ERR_LDT_BIN_NAME_NOT_STRING         = 1412,

	/** The supplied LDT bin name exceeded the 14 char limit. */
	AEROSPIKE_ERR_LDT_BIN_NAME_TOO_LONG           = 1413,

	/** Internal Error: too many open records at one time. */
	AEROSPIKE_ERR_LDT_TOO_MANY_OPEN_SUBRECS       = 1414,

	/** Internal Error: Top Record not found.  */
	AEROSPIKE_ERR_LDT_TOP_REC_NOT_FOUND           = 1415,

	/** Internal Error: Sub Record not found. */
	AEROSPIKE_ERR_LDT_SUB_REC_NOT_FOUND           = 1416,

	/** LDT Bin does not exist. */
	AEROSPIKE_ERR_LDT_BIN_DOES_NOT_EXIST          = 1417,

	/** Collision: LDT Bin already exists. */
	AEROSPIKE_ERR_LDT_BIN_ALREADY_EXISTS          = 1418,

	/** LDT control structures in the Top Record are damaged. Cannot proceed. */
	AEROSPIKE_ERR_LDT_BIN_DAMAGED                 = 1419,

    // -------------------------------------------------

	/** Internal Error: LDT Subrecord pool is damaged. */
	AEROSPIKE_ERR_LDT_SUBREC_POOL_DAMAGED         = 1420,

	/** LDT control structures in the Sub Record are damaged. Cannot proceed. */
	AEROSPIKE_ERR_LDT_SUBREC_DAMAGED              = 1421,

	/** Error encountered while opening a Sub Record. */
	AEROSPIKE_ERR_LDT_SUBREC_OPEN                 = 1422,

	/** Error encountered while updating a Sub Record. */
	AEROSPIKE_ERR_LDT_SUBREC_UPDATE               = 1423,

	/** Error encountered while creating a Sub Record. */
	AEROSPIKE_ERR_LDT_SUBREC_CREATE               = 1424,

	/** Error encountered while deleting a Sub Record. */
	AEROSPIKE_ERR_LDT_SUBREC_DELETE               = 1425, 

	/** Error encountered while closing a Sub Record. */
	AEROSPIKE_ERR_LDT_SUBREC_CLOSE                = 1426,

	/** Error encountered while updating a TOP Record. */
	AEROSPIKE_ERR_LDT_TOPREC_UPDATE               = 1427,

	/** Error encountered while creating a TOP Record. */
	AEROSPIKE_ERR_LDT_TOPREC_CREATE               = 1428,

    // -------------------------------------------------

	/** The filter function name was invalid. */
	AEROSPIKE_ERR_LDT_FILTER_FUNCTION_BAD         = 1430,

	/** The filter function was not found. */
	AEROSPIKE_ERR_LDT_FILTER_FUNCTION_NOT_FOUND   = 1431,

	/** The function to extract the Unique Value from a complex object was invalid. */
	AEROSPIKE_ERR_LDT_KEY_FUNCTION_BAD            = 1432,

	/** The function to extract the Unique Value from a complex object was not found. */
	AEROSPIKE_ERR_LDT_KEY_FUNCTION_NOT_FOUND      = 1433,

	/** The function to transform an object into a binary form was invalid. */
	AEROSPIKE_ERR_LDT_TRANS_FUNCTION_BAD          = 1434,

	/** The function to transform an object into a binary form was not found. */
	AEROSPIKE_ERR_LDT_TRANS_FUNCTION_NOT_FOUND    = 1435,

	/** The function to untransform an object from binary form to live form was invalid. */
	AEROSPIKE_ERR_LDT_UNTRANS_FUNCTION_BAD        = 1436,

	/** The function to untransform an object from binary form to live form not found. */
	AEROSPIKE_ERR_LDT_UNTRANS_FUNCTION_NOT_FOUND  = 1437,

	/** The UDF user module name for LDT Overrides was invalid */
	AEROSPIKE_ERR_LDT_USER_MODULE_BAD             = 1438,

	/** The UDF user module name for LDT Overrides was not found */
	AEROSPIKE_ERR_LDT_USER_MODULE_NOT_FOUND       = 1439

} as_status;

#ifdef __cplusplus
} // end extern "C"
#endif
