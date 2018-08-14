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
 *	@defgroup info_operations Info Operations
 *	@ingroup client_operations
 *
 *	The Info API provides the ability to query server node(s) for statistics and set dynamically
 *	configurable variables.
 */

#include <aerospike/aerospike.h>
#include <aerospike/aerospike_scan.h>
#include <aerospike/as_error.h>
#include <aerospike/as_node.h>
#include <aerospike/as_policy.h>
#include <aerospike/as_status.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *	TYPES
 *****************************************************************************/

/**
 *	Callback for aerospike_info_foreach()
 *	
 *	@param err			The status and possible error information for the info request.
 *	@param node			The node which provided the response.
 *	@param req			The initial request.
 *	@param res			The response to the info request.
 *	@param udata		The udata provided to the aerospike_info_foreach()
 *
 *	@return TRUE to continue to the next info response. FALSE to stop processing.
 *
 *	@ingroup info_operations
 */
typedef bool (*aerospike_info_foreach_callback)(const as_error* err, const as_node* node, const char* req, char* res, void* udata);

/******************************************************************************
 *	FUNCTIONS
 *****************************************************************************/

/**
 *	Send an info request to a specific server node. The response must be freed by the caller on success.
 *
 *	~~~~~~~~~~{.c}
 *	char* res = NULL;
 *	if (aerospike_info_node(&as, &err, NULL, node, "info", &res) != AEROSPIKE_OK) {
 *		// handle error
 *	}
 *	else {
 *		// handle response
 *		free(res);
 *	}
 *	~~~~~~~~~~
 *
 *	@param as			The aerospike instance to use for this operation.
 *	@param err			The as_error to be populated if an error occurs.
 *	@param policy		The policy to use for this operation. If NULL, then the default policy will be used.
 *	@param node			The server node to send the request to.
 *	@param req			The info request to send.
 *	@param res			The response from the node. The response will be a NULL terminated string,
 *						allocated by the function, and must be freed by the caller.
 *
 *	@return AEROSPIKE_OK on success. Otherwise an error.
 *
 *	@ingroup info_operations
 */
as_status
aerospike_info_node(
	aerospike* as, as_error* err, const as_policy_info* policy, as_node* node,
	const char* req, char** res
	);

/**
 *	Send an info request to a specific host. The response must be freed by the caller on success.
 *
 *	~~~~~~~~~~{.c}
 *	char* res = NULL;
 *	if (aerospike_info_host(&as, &err, NULL, "127.0.0.1", 3000, "info", &res) != AEROSPIKE_OK) {
 *		// handle error
 *	}
 *	else {
 *		// handle response
 *		free(res);
 *		res = NULL;
 *	}
 *	~~~~~~~~~~
 *
 *	If TLS is enabled, this function will only work if the hostname is also the TLS certificate name.
 *
 *	@deprecated			Use aerospike_info_node() or aerospike_info_any() instead.
 *
 *	@param as			The aerospike instance to use for this operation.
 *	@param err			The as_error to be populated if an error occurs.
 *	@param policy		The policy to use for this operation. If NULL, then the default policy will be used.
 *	@param hostname		The IP address or hostname to send the request to.
 *	@param port			The port to send the request to.
 *	@param req			The info request to send.
 *	@param res			The response from the node. The response will be a NULL terminated string,
 *						allocated by the function, and must be freed by the caller.
 *
 *	@return AEROSPIKE_OK on success. Otherwise an error.
 *
 *	@ingroup info_operations
 */
as_status
aerospike_info_host(
	aerospike* as, as_error* err, const as_policy_info* policy, const char* hostname, uint16_t port,
	const char* req, char** res
	);

/**
 *	Send an info request to a specific socket address. The response must be freed by the caller on success.
 *	This function does not support TLS connections nor IPv6.
 *
 *	~~~~~~~~~~{.c}
 *	char* res = NULL;
 *	if (aerospike_info_socket_address(&as, &err, NULL, &sa_in, "info", &res) != AEROSPIKE_OK) {
 *		// handle error
 *	}
 *	else {
 *		// handle response
 *		free(res);
 *		res = NULL;
 *	}
 *	~~~~~~~~~~
 *
 *	@param as			The aerospike instance to use for this operation.
 *	@param err			The as_error to be populated if an error occurs.
 *	@param policy		The policy to use for this operation. If NULL, then the default policy will be used.
 *	@param sa_in		The IP address and port to send the request to.
 *	@param req			The info request to send.
 *	@param res			The response from the node. The response will be a NULL terminated string,
 *						allocated by the function, and must be freed by the caller.
 *
 *	@return AEROSPIKE_OK on success. Otherwise an error.
 *
 *	@ingroup info_operations
 */
as_status
aerospike_info_socket_address(
	aerospike* as, as_error* err, const as_policy_info* policy, struct sockaddr_in* sa_in,
	const char* req, char** res
	);

/**
 *	Send an info request to a node in the cluster.  If node request fails, send request to the next
 *	node in the cluster.  Repeat until the node request succeeds. The response must be freed by
 *	the caller on success.
 *
 *	~~~~~~~~~~{.c}
 *	char* res = NULL;
 *	if (aerospike_info_any(&as, &err, NULL, "info", &res) != AEROSPIKE_OK) {
 *		// handle error
 *	}
 *	else {
 *		// handle response
 *		free(res);
 *		res = NULL;
 *	}
 *	~~~~~~~~~~
 *
 *	@param as			The aerospike instance to use for this operation.
 *	@param err			The as_error to be populated if an error occurs.
 *	@param policy		The policy to use for this operation. If NULL, then the default policy will be used.
 *	@param req			The info request to send.
 *	@param res			The response from the node. The response will be a NULL terminated string,
 *						allocated by the function, and must be freed by the caller.
 *
 *	@return AEROSPIKE_OK on success. Otherwise an error.
 *
 *	@ingroup info_operations
 */
as_status
aerospike_info_any(
	aerospike* as, as_error* err, const as_policy_info* policy, const char* req, char** res
	);

/**
 *	Send an info request to the entire cluster.
 *
 *	~~~~~~~~~~{.c}
 *	if (aerospike_info_foreach(&as, &err, NULL, "info", callback, NULL) != AEROSPIKE_OK) {
 *		// handle error
 *	}
 *	~~~~~~~~~~
 *
 *	The callback takes a response string. The caller should not free this string.
 *
 *	~~~~~~~~~~{.c}
 *	bool callback(const as_error* err, const as_node * node, const char* req, char* res, void* udata) {
 *		// handle response
 *	}
 *	~~~~~~~~~~
 *
 *
 *	@param as			The aerospike instance to use for this operation.
 *	@param err			The as_error to be populated if an error occurs.
 *	@param policy		The policy to use for this operation. If NULL, then the default policy will be used.
 *	@param req			The info request to send.
 *	@param callback		The function to call when a response is received.
 *	@param udata		User-data to send to the callback.
 *
 *	@return AEROSPIKE_OK on success. Otherwise an error.
 *
 *	@ingroup info_operations
 */
as_status
aerospike_info_foreach(
	aerospike* as, as_error* err, const as_policy_info* policy, const char* req,
	aerospike_info_foreach_callback callback, void* udata
	);

#ifdef __cplusplus
} // end extern "C"
#endif
