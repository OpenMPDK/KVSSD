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
#include <aerospike/as_config.h>
#include <aerospike/as_key.h>
#include <aerospike/as_socket.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *	MACROS
 *****************************************************************************/

/**
 *	Maximum size of role string including null byte.
 */
#define AS_ROLE_SIZE 32

/******************************************************************************
 *	TYPES
 *****************************************************************************/

/**
 *	Permission codes define the type of permission granted for a user's role.
 */
typedef enum as_privilege_code_e {
	/**
	 *	User can edit/remove other users.  Global scope only.
	 */
	AS_PRIVILEGE_USER_ADMIN = 0,
	
	/**
	 *	User can perform systems administration functions on a database that do not involve user
	 *	administration.  Examples include setting dynamic server configuration.
	 *	Global scope only.
	 */
	AS_PRIVILEGE_SYS_ADMIN = 1,
	
	/**
	 *	User can perform data administration functions on a database that do not involve user
	 *	administration.  Examples include create/drop index and user defined functions. 
	 *	Global scope only.
	 */
	AS_PRIVILEGE_DATA_ADMIN = 2,

	/**
	 *	User can read data only.
	 */
	AS_PRIVILEGE_READ = 10,
	
	/**
	 *	User can read and write data.
	 */
	AS_PRIVILEGE_READ_WRITE = 11,
	
	/**
	 *	User can read and write data through user defined functions.
	 */
	AS_PRIVILEGE_READ_WRITE_UDF = 12
} as_privilege_code;

/**
 *	User privilege.
 */
typedef struct as_privilege_s {
	/**
	 *	Namespace scope.  Apply permission to this null terminated namespace only.
	 *	If string length is zero, the privilege applies to all namespaces.
	 */
	as_namespace ns;
	
	/**
	 *	Set name scope.  Apply permission to this null terminated set within namespace only.
	 *	If string length is zero, the privilege applies to all sets within namespace.
	 */
	as_set set;
	
	/**
	 *	Privilege code.
	 */
	as_privilege_code code;
} as_privilege;

/**
 *	Role definition.
 */
typedef struct as_role_s {
	/**
	 *	Role name.
	 */
	char name[AS_ROLE_SIZE];
	
	/**
	 *	Length of privileges array.
	 */
	int privileges_size;
	
	/**
	 *	Array of assigned privileges.
	 */
	as_privilege privileges[];
} as_role;

/**
 *	User and assigned roles.
 */
typedef struct as_user_s {
	/**
	 *	User name.
	 */
	char name[AS_USER_SIZE];
	
	/**
	 *	Length of roles array.
	 */
	int roles_size;

	/**
	 *	Array of assigned role names.
	 */
	char roles[][AS_ROLE_SIZE];
} as_user;

struct as_node_s;

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/

/**
 *	Create user with password and roles.  Clear-text password will be hashed using bcrypt before 
 *	sending to server.
 */
as_status
aerospike_create_user(aerospike* as, as_error* err, const as_policy_admin* policy, const char* user_name, const char* password, const char** roles, int roles_size);

/**
 *	Remove user from cluster.
 */
as_status
aerospike_drop_user(aerospike* as, as_error* err, const as_policy_admin* policy, const char* user_name);

/**
 *	Set user's password by user administrator.  Clear-text password will be hashed using bcrypt before sending to server.
 */
as_status
aerospike_set_password(aerospike* as, as_error* err, const as_policy_admin* policy, const char* user_name, const char* password);

/**
 *	Change user's password by user.  Clear-text password will be hashed using bcrypt before sending to server.
 */
as_status
aerospike_change_password(aerospike* as, as_error* err, const as_policy_admin* policy, const char* user_name, const char* password);

/**
 *	Add role to user's list of roles.
 */
as_status
aerospike_grant_roles(aerospike* as, as_error* err, const as_policy_admin* policy, const char* user_name, const char** roles, int roles_size);

/**
 *	Remove role from user's list of roles.
 */
as_status
aerospike_revoke_roles(aerospike* as, as_error* err, const as_policy_admin* policy, const char* user_name, const char** roles, int roles_size);

/**
 *	Create user defined role.
 */
as_status
aerospike_create_role(aerospike* as, as_error* err, const as_policy_admin* policy, const char* role, as_privilege** privileges, int privileges_size);

/**
 *	Delete user defined role.
 */
as_status
aerospike_drop_role(aerospike* as, as_error* err, const as_policy_admin* policy, const char* role);

/**
 *	Add specified privileges to user.
 */
as_status
aerospike_grant_privileges(aerospike* as, as_error* err, const as_policy_admin* policy, const char* role, as_privilege** privileges, int privileges_size);

/**
 *	Remove specified privileges from user.
 */
as_status
aerospike_revoke_privileges(aerospike* as, as_error* err, const as_policy_admin* policy, const char* role, as_privilege** privileges, int privileges_size);

/**
 *	Retrieve roles for a given user.
 *	When successful, as_user_destroy() must be called to free resources.
 */
as_status
aerospike_query_user(aerospike* as, as_error* err, const as_policy_admin* policy, const char* user_name, as_user** user);

/**
 *	Release as_user_roles memory.
 */
void
as_user_destroy(as_user* user);

/**
 *	Retrieve all users and their roles.
 *	When successful, as_users_destroy() must be called to free resources.
 */
as_status
aerospike_query_users(aerospike* as, as_error* err, const as_policy_admin* policy, as_user*** users, int* users_size);

/**
 *	Release memory for as_user_roles array.
 */
void
as_users_destroy(as_user** users, int users_size);

/**
 *	Retrieve role definition for a given role name.
 *	When successful, as_role_destroy() must be called to free resources.
 */
as_status
aerospike_query_role(aerospike* as, as_error* err, const as_policy_admin* policy, const char* role_name, as_role** role);

/**
 *	Release as_role memory.
 */
void
as_role_destroy(as_role* role);

/**
 *	Retrieve all roles and their privileges.
 *	When successful, as_roles_destroy() must be called to free resources.
 */
as_status
aerospike_query_roles(aerospike* as, as_error* err, const as_policy_admin* policy, as_role*** roles, int* roles_size);

/**
 *	Release memory for as_role array.
 */
void
as_roles_destroy(as_role** roles, int roles_size);

/**
 *	@private
 *	Authenticate user with a server node.  This is done automatically after socket open.
 *  Do not use this method directly.
 */
as_status
as_authenticate(as_error* err, as_socket* sock, struct as_node_s* node, const char* user, const char* credential, uint32_t socket_timeout, uint64_t deadline_ms);
	
/**
 *	@private
 *	Write authentication command to buffer.  Return buffer length.
 */
uint32_t
as_authenticate_set(const char* user, const char* credential, uint8_t* buffer);

#ifdef __cplusplus
} // end extern "C"
#endif
