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

#include <aerospike/aerospike.h>
#include <aerospike/as_cluster.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *	TYPES
 *****************************************************************************/

/**
 *	@private
 *	Name value pair.
 */
typedef struct as_name_value_s {
	char* name;
	char* value;
} as_name_value;

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/

/**
 *	@private
 *	Send info command to specific node. The values must be freed by the caller on success.
 */
as_status
as_info_command_node(
	as_error* err, as_node* node, char* command, bool send_asis, uint64_t deadline_ms,
	char** response
	);

/**
 *	@private
 *	Send info command to random node. The values must be freed by the caller on success.
 */
as_status
as_info_command_random_node(aerospike* as, as_error* err, as_policy_info* policy, char* command);

/**
 *	@private
 *	Send info command to specific host. The values must be freed by the caller on success.
 */
as_status
as_info_command_host(
	as_cluster* cluster, as_error* err, struct sockaddr* addr, char* command,
	bool send_asis, uint64_t deadline_ms, char** response, const char* tls_name
	);

/**
 *	@private
 *	Send info command to specific socket. The values must be freed by the caller on success.
 *	Set max_response_length to zero if response size should not be bounded.
 */
as_status
as_info_command(
	as_error* err, as_socket* sock, as_node* node, char* names, bool send_asis, uint64_t deadline_ms,
	uint64_t max_response_length, char** values
	);

/**
 *	@private
 *	Create and authenticate socket for info requests.
 */
as_status
as_info_create_socket(
	as_cluster* cluster, as_error* err, struct sockaddr* addr, uint64_t deadline_ms,
	const char* tls_name, as_socket* sock
	);

/**
 *	@private
 *	Return the single command's info response buffer value.
 *	The original buffer will be modified with the null termination character.
 */
as_status
as_info_parse_single_response(char *values, char **value);

/**
 *	@private
 *	Parse info response buffer into name/value pairs, one for each command.
 *	The original buffer will be modified with null termination characters to
 *	delimit each command name and value referenced by the name/value pairs.
 */
void
as_info_parse_multi_response(char* buf, as_vector* /* <as_name_value> */ values);

#ifdef __cplusplus
} // end extern "C"
#endif
