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

#include <aerospike/as_error.h>
#include <aerospike/as_host.h>
#include <aerospike/as_policy.h>
#include <aerospike/as_password.h>
#include <aerospike/as_vector.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *	MACROS
 *****************************************************************************/

#ifdef __linux__
/**
 * Default path to the system UDF files.
 */
#define AS_CONFIG_LUA_SYSTEM_PATH "/opt/aerospike/client/sys/udf/lua"

/**
 * Default path to the user UDF files.
 */
#define AS_CONFIG_LUA_USER_PATH "/opt/aerospike/client/usr/udf/lua"
#endif

#ifdef __APPLE__
/**
 * Default path to the system UDF files.
 */
#define AS_CONFIG_LUA_SYSTEM_PATH "/usr/local/aerospike/client/sys/udf/lua"

/**
 * Default path to the user UDF files.
 */
#define AS_CONFIG_LUA_USER_PATH "/usr/local/aerospike/client/usr/udf/lua"
#endif

/**
 * The size of path strings
 */
#define AS_CONFIG_PATH_MAX_SIZE 256

/**
 * The maximum string length of path strings
 */
#define AS_CONFIG_PATH_MAX_LEN 	(AS_CONFIG_PATH_MAX_SIZE - 1)

/******************************************************************************
 *	TYPES
 *****************************************************************************/

/**
 *	IP translation table.
 *
 *	@ingroup as_config_object
 */
typedef struct as_addr_map_s {
	
	/**
	 *	Original hostname or IP address in string format.
	 */
    char* orig;
	
	/**
	 *	Use this IP address instead.
	 */
    char* alt;
	
} as_addr_map;

/**
 *	Cluster event notification type.
 *
 *	@ingroup as_config_object
 */
typedef enum as_cluster_event_type_e {
	/**
	 *	Node was added to cluster.
	 */
	AS_CLUSTER_ADD_NODE = 0,

	/**
	 *	Node was removed fron cluster.
	 */
	AS_CLUSTER_REMOVE_NODE = 1,

	/**
	 *	There are no active nodes in the cluster.
	 */
	AS_CLUSTER_DISCONNECTED = 2
} as_cluster_event_type;

/**
 *	Cluster event notification data.
 *
 *	@ingroup as_config_object
 */
typedef struct as_cluster_event_s {
	/**
	 *	Node name.
	 */
	const char* node_name;

	/**
	 *	Node IP address in string format.
	 */
	const char* node_address;

	/**
	 *	User defined data.
	 */
	void* udata;

	/**
	 *	Cluster event notification type.
	 */
	as_cluster_event_type type;
} as_cluster_event;

/**
 *	Cluster event notification callback function.
 *	as_cluster_event is placed on the stack before calling.
 *	Do not free node_name or node_address.
 *
 *	@ingroup as_config_object
 */
typedef void (*as_cluster_event_callback) (as_cluster_event* event);

/**
 *	lua module config
 *
 *	@ingroup as_config_object
 */
typedef struct as_config_lua_s {

	/**
	 *	Enable caching of UDF files in the client
	 *	application.
	 */
	bool cache_enabled;

	/**
	 *	The path to the system UDF files. These UDF files 
	 *	are installed with the aerospike client library.
	 *	Default location defined in: AS_CONFIG_LUA_SYSTEM_PATH
	 */
	char system_path[AS_CONFIG_PATH_MAX_SIZE];

	/**
	 *	The path to user's UDF files.
	 *	Default location defined in: AS_CONFIG_LUA_USER_PATH
	 */
	char user_path[AS_CONFIG_PATH_MAX_SIZE];

} as_config_lua;

/**
 *	TLS module config
 *
 *	@ingroup as_config_object
 */
typedef struct as_config_tls_s {

	/**
	 *	Enable TLS on connections.
     *  By default TLS is disabled.
	 */
	bool enable;

	/**
	 *	Only encrypt connections; do not verify certificates.
     *  By default TLS will verify certificates.
	 */
	bool encrypt_only;
	
	/**
	 *  Path to a trusted CA certificate file.
	 *  By default TLS will use system standard trusted CA certificates.
	 *	Use as_config_tls_set_cafile() to set this field.
	 */
	char* cafile;

	/**
	 *  Path to a directory of trusted certificates.
	 *  See the OpenSSL SSL_CTX_load_verify_locations manual page for
	 *  more information about the format of the directory.
	 *	Use as_config_tls_set_capath() to set this field.
	 */
	char* capath;

	/**
	 *  Specifies enabled protocols.
	 *
	 *  This format is the same as Apache's SSLProtocol documented
	 *  at https://httpd.apache.org/docs/current/mod/mod_ssl.html#sslprotocol
	 *
	 *  If not specified (NULL) the client will use "-all +TLSv1.2".
	 *
	 *  If you are not sure what protocols to select this option is
	 *  best left unspecified (NULL).
	 *
	 *	Use as_config_tls_set_protocols() to set this field.
	 */
	char* protocols;
	
	/**
	 *  Specifies enabled cipher suites.
	 *
	 *  The format is the same as OpenSSL's Cipher List Format documented
	 *  at https://www.openssl.org/docs/manmaster/apps/ciphers.html
	 *
	 *  If not specified the OpenSSL default cipher suite described in
	 *  the ciphers documentation will be used.
	 *
	 *  If you are not sure what cipher suite to select this option
	 *  is best left unspecified (NULL).
	 *
	 *	Use as_config_tls_set_cipher_suite() to set this field.
	 */
	char* cipher_suite;
	
	/**
	 *	Enable CRL checking for the certificate chain leaf certificate.
	 *  An error occurs if a suitable CRL cannot be found.
     *  By default CRL checking is disabled.
	 */
	bool crl_check;

	/**
	 *	Enable CRL checking for the entire certificate chain.
	 *  An error occurs if a suitable CRL cannot be found.
     *  By default CRL checking is disabled.
	 */
	bool crl_check_all;

	/**
	 *  Path to a certificate blacklist file.
	 *  The file should contain one line for each blacklisted certificate.
	 *  Each line starts with the certificate serial number expressed in hex.
	 *  Each entry may optionally specify the issuer name of the
	 *  certificate (serial numbers are only required to be unique per
	 *  issuer).  Example records:
	 *  867EC87482B2 /C=US/ST=CA/O=Acme/OU=Engineering/CN=Test Chain CA
	 *  E2D4B0E570F9EF8E885C065899886461
	 *
	 *	Use as_config_tls_set_cert_blacklist() to set this field.
	 */
	char* cert_blacklist;

	/**
	 *  Log session information for each connection.
	 */
	bool log_session_info;
	
	/**
	 *  Path to the client's key for mutual authentication.
	 *  By default mutual authentication is disabled.
	 *
	 *	Use as_config_tls_set_keyfile() to set this field.
	 */
	char* keyfile;

	/**
	 *  Path to the client's certificate chain file for mutual authentication.
	 *  By default mutual authentication is disabled.
	 *
	 *	Use as_config_tls_set_certfile() to set this field.
	 */
	char* certfile;

} as_config_tls;

/**
 *	The `as_config` contains the settings for the `aerospike` client. Including
 *	default policies, seed hosts in the cluster and other settings.
 *
 *	## Initialization
 *
 *	Before using as_config, you must first initialize it. This will setup the 
 *	default values.
 *
 *	~~~~~~~~~~{.c}
 *	as_config config;
 *	as_config_init(&config);
 *	~~~~~~~~~~
 *
 *	Once initialized, you can populate the values.
 *
 *	## Seed Hosts
 *	
 *	The client will require at least one seed host defined in the 
 *	configuration. The seed host is defined in `as_config.hosts`. 
 *
 *	~~~~~~~~~~{.c}
 *	as_config_add_host(&config, "127.0.0.1", 3000);
 *	~~~~~~~~~~
 *
 *	The client will iterate over the list until it connects with one of the hosts.
 *
 *	## Policies
 *
 *	The configuration also defines default policies for the application. The 
 *	`as_config_init()` function already presets default values for the policies.
 *	
 *	Policies define the behavior of the client, which can be global across
 *	operations, global to a single operation, or local to a single use of an
 *	operation.
 *	
 *	Each database operation accepts a policy for that operation as an a argument.
 *	This is considered a local policy, and is a single use policy. This policy
 *	supersedes any global policy defined.
 *	
 *	If a value of the policy is not defined, then the rule is to fallback to the
 *	global policy for that operation. If the global policy for that operation is
 *	undefined, then the global default value will be used.
 *
 *	If you find that you have behavior that you want every use of an operation
 *	to utilize, then you can specify the default policy in as_config.policies.
 *
 *	For example, the `aerospike_key_put()` operation takes an `as_policy_write`
 *	policy. If you find yourself setting the `key` policy value for every call 
 *	to `aerospike_key_put()`, then you may find it beneficial to set the global
 *	`as_policy_write` in `as_policies.write`, which all write operations will use.
 *
 *	~~~~~~~~~~{.c}
 *	config.policies.write.key = AS_POLICY_KEY_SEND;
 *	~~~~~~~~~~
 *
 *	If you find that you want to use a policy value across all operations, then 
 *	you may find it beneficial to set the default policy value for that policy 
 *	value.
 *
 *	For example, if you keep setting the key policy value to 
 *	`AS_POLICY_KEY_SEND`, then you may want to just set `as_policies.key`. This
 *	will set the global default value for the policy value. So, if an global
 *  operation policy or a local operation policy does not define a value, then
 *	this value will be used.
 *
 *	~~~~~~~~~~{.c}
 *	config.policies.key = AS_POLICY_KEY_SEND;
 *	~~~~~~~~~~
 *
 *	Global default policy values:
 *	-	as_policies.timeout
 *	-	as_policies.retry
 *	-	as_policies.key
 *	-	as_policies.gen
 *	-	as_policies.exists
 *
 *	Global operation policies:
 *	-	as_policies.read
 *	-	as_policies.write
 *	-	as_policies.operate
 *	-	as_policies.remove
 *	-	as_policies.query
 *	-	as_policies.scan
 *	-	as_policies.info
 *
 *
 *	## User-Defined Function Settings
 *	
 *	If you are using user-defined functions (UDF) for processing query 
 *	results (i.e aggregations), then you will find it useful to set the 
 *	`mod_lua` settings. Of particular importance is the `mod_lua.user_path`, 
 *	which allows you to define a path to where the client library will look for
 *	Lua files for processing.
 *	
 *	~~~~~~~~~~{.c}
 *	strcpy(config.mod_lua.user_path, "/home/me/lua");
 *	~~~~~~~~~~
 *
 *	Never call as_config_destroy() directly because ownership of config fields
 *	is transferred to aerospike in aerospike_init() or aerospike_new().
 *
 *	@ingroup client_objects
 */
typedef struct as_config_s {
	/**
	 *	Seed hosts. Populate with one or more hosts in the cluster that you intend to connect with.
	 *	Do not set directly.  Use as_config_add_hosts() or as_config_add_host() to add seed hosts.
	 */
	as_vector* hosts;
	
	/**
	 *	User authentication to cluster.  Leave empty for clusters running without restricted access.
	 */
	char user[AS_USER_SIZE];
	
	/**
	 *	Password authentication to cluster.  The hashed value of password will be stored by the client 
	 *	and sent to server in same format.  Leave empty for clusters running without restricted access.
	 */
	char password[AS_PASSWORD_HASH_SIZE];
	
	/**
	 *	Expected cluster name.  If not null, server nodes must return this cluster name in order to
	 *	join the client's view of the cluster. Should only be set when connecting to servers that
	 *	support the "cluster-name" info command.  Use as_config_set_cluster_name() to set this field.
	 *	Default: NULL
	 */
	char* cluster_name;
	
	/**
	 *	Cluster event function that will be called when nodes are added/removed from the cluster.
	 *
	 *	Default: NULL (no callback will be made)
	 */
	as_cluster_event_callback event_callback;

	/**
	 *	Cluster event user data that will be passed back to event_callback.
	 *
	 *	Default: NULL
	 */
	void* event_callback_udata;

	/**
	 *	A IP translation table is used in cases where different clients use different server
	 *	IP addresses.  This may be necessary when using clients from both inside and outside
	 *	a local area network.  Default is no translation.
	 *
	 *	The key is the IP address returned from friend info requests to other servers.  The
	 *	value is the real IP address used to connect to the server.
	 *
	 *	A deep copy of ip_map is performed in aerospike_connect().  The caller is
	 *	responsible for memory deallocation of the original data structure.
	 */
	as_addr_map* ip_map;
	
	/**
	 *	Length of ip_map array.
	 *  Default: 0
	 */
	uint32_t ip_map_size;
	
	/**
	 * Maximum number of synchronous connections allowed per server node.  Synchronous transactions
	 * will go through retry logic and potentially fail with error code "AEROSPIKE_ERR_NO_MORE_CONNECTIONS"
	 * if the maximum number of connections would be exceeded.
	 * 
	 * The number of connections used per node depends on how many concurrent threads issue
	 * database commands plus sub-threads used for parallel multi-node commands (batch, scan,
	 * and query). One connection will be used for each thread.
	 *
	 * Default: 300
	 */
	uint32_t max_conns_per_node;
	
	/**
	 *	Maximum number of asynchronous (non-pipeline) connections allowed for each node.
	 *	This limit will be enforced at the node/event loop level.  If the value is 100 and 2 event
	 *	loops are created, then each node/event loop asynchronous (non-pipeline) connection pool 
	 *	will have a limit of 50. Async transactions will be rejected if the limit would be exceeded.
	 *	This variable is ignored if asynchronous event loops are not created.
	 *	Default: 300
	 */
	uint32_t async_max_conns_per_node;

	/**
	 *	Maximum number of pipeline connections allowed for each node.
	 *	This limit will be enforced at the node/event loop level.  If the value is 100 and 2 event
	 *	loops are created, then each node/event loop pipeline connection pool will have a limit of 50. 
	 *	Async transactions will be rejected if the limit would be exceeded.
	 *	This variable is ignored if asynchronous event loops are not created.
	 *	Default: 64
	 */
	uint32_t pipe_max_conns_per_node;
	
	/**
	 *	Number of synchronous connection pools used for each node.  Machines with 8 cpu cores or
	 *	less usually need just one connection pool per node.  Machines with a large number of cpu
	 *	cores may have their synchronous performance limited by contention for pooled connections.
	 *	Contention for pooled connections can be reduced by creating multiple mini connection pools
	 *	per node.
	 *
	 *	Default: 1
	 */
	uint32_t conn_pools_per_node;

	/**
	 *	Initial host connection timeout in milliseconds.  The timeout when opening a connection
	 *	to the server host for the first time.
	 *	Default: 1000
	 */
	uint32_t conn_timeout_ms;

	/**
	 *	Maximum socket idle time in seconds.  Connection pools will discard sockets that have
	 *	been idle longer than the maximum.  The value is limited to 24 hours (86400).
	 *
	 *	It's important to set this value to a few seconds less than the server's proto-fd-idle-ms
	 *	(default 60000 milliseconds or 1 minute), so the client does not attempt to use a socket
	 *	that has already been reaped by the server.
	 *
	 *	Default: 0 seconds (disabled) for non-TLS connections, 55 seconds for TLS connections.
	 */
	uint32_t max_socket_idle;

	/**
	 *	Polling interval in milliseconds for cluster tender
	 *	Default: 1000
	 */
	uint32_t tender_interval;

	/**
	 *	Number of threads stored in underlying thread pool used by synchronous batch/scan/query commands.
	 *	These commands are often sent to multiple server nodes in parallel threads.  A thread pool 
	 *	improves performance because threads do not have to be created/destroyed for each command.
	 *	Calculate your value using the following formula:
	 *
	 *	thread_pool_size = (concurrent synchronous batch/scan/query commands) * (server nodes)
	 *
	 *	If your application only uses async commands, this field can be set to zero.
	 *	Default: 16
	 */
	uint32_t thread_pool_size;
	
	/**
	 *	Client policies
	 */
	as_policies policies;

	/**
	 *	lua config.  This is a global config even though it's located here in cluster config.
	 *	This config has been left here to avoid breaking the API.
	 *
	 *	The global lua config will only be changed once on first cluster initialization.
	 *	A better method for initializing lua configuration is to leave this field alone and
	 *	instead call aerospike_init_lua():
	 *
	 *	~~~~~~~~~~{.c}
	 *	// Get default global lua configuration.
	 *	as_config_lua lua;
	 *	as_config_lua_init(&lua);
	 *
	 *	// Optionally modify lua defaults.
	 *	lua.cache_enabled = <enable lua cache>;
	 *	strcpy(lua.system_path, <lua system directory>);
	 *	strcpy(lua.user_path, <lua user directory>);
	 *
	 *	// Initialize global lua configuration.
	 *	aerospike_init_lua(&lua);
	 *	~~~~~~~~~~
	 */
	as_config_lua lua;

	/*
	 * TLS configuration parameters.
	 */
	as_config_tls tls;
	
	/**
	 *	Action to perform if client fails to connect to seed hosts.
	 *
	 *	If fail_if_not_connected is true (default), the cluster creation will fail
	 *	when all seed hosts are not reachable.
	 *
	 *	If fail_if_not_connected is false, an empty cluster will be created and the 
	 *	client will automatically connect when Aerospike server becomes available.
	 */
	bool fail_if_not_connected;
	
	/**
	 *	Flag to signify if "services-alternate" should be used instead of "services"
	 *	Default : false
	 */
	bool use_services_alternate;

	/**
	 *	Indicates if shared memory should be used for cluster tending.  Shared memory
	 *	is useful when operating in single threaded mode with multiple client processes.
	 *	This model is used by wrapper languages such as PHP and Python.  When enabled, 
	 *	the data partition maps are maintained by only one process and all other processes 
	 *	use these shared memory maps.
	 *
	 *	Shared memory should not be enabled for multi-threaded programs.
	 *	Default: false
	 */
	bool use_shm;

	/**
	 *	Identifier for the shared memory segment associated with the target Aerospike cluster.
	 *	Each shared memory segment contains state for one Aerospike cluster.  If there are
	 *	multiple Aerospike clusters, a different shm_key must be defined for each cluster.
	 *	
	 *	Default: 0xA7000000
	 */
	int shm_key;
	
	/**
	 *	Shared memory maximum number of server nodes allowed.  This value is used to size
	 *	the fixed shared memory segment.  Leave a cushion between actual server node
	 *	count and shm_max_nodes so new nodes can be added without having to reboot the client.
	 *	Default: 16
	 */
	uint32_t shm_max_nodes;
	
	/**
	 *	Shared memory maximum number of namespaces allowed.  This value is used to size
	 *	the fixed shared memory segment.  Leave a cushion between actual namespaces
	 *	and shm_max_namespaces so new namespaces can be added without having to reboot the client.
	 *	Default: 8
	 */
	uint32_t shm_max_namespaces;
	
	/**
	 *	Take over shared memory cluster tending if the cluster hasn't been tended by this
	 *	threshold in seconds.
	 *	Default: 30
	 */
	uint32_t shm_takeover_threshold_sec;
} as_config;

/******************************************************************************
 *	FUNCTIONS
 *****************************************************************************/

/**
 *	Initialize the configuration to default values.
 *
 *	You should do this to ensure the configuration has valid values, before 
 *	populating it with custom options.
 *
 *	~~~~~~~~~~{.c}
 *	as_config config;
 *	as_config_init(&config);
 *	as_config_add_host(&config, "127.0.0.1", 3000);
 *	~~~~~~~~~~
 *	
 *	@relates as_config
 */
as_config*
as_config_init(as_config* config);

/**
 *	Add seed host(s) from a string with format: hostname1[:tlsname1][:port1],...
 *	Hostname may also be an IP address in the following formats.
 *
 *	~~~~~~~~~~{.c}
 *	IPv4: xxx.xxx.xxx.xxx
 *	IPv6: [xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx]
 *	IPv6: [xxxx::xxxx]
 *	~~~~~~~~~~
 *
 *	The host addresses will be copied.
 *	The caller is responsible for the original string.
 *
 *	~~~~~~~~~~{.c}
 *	as_config config;
 *	as_config_init(&config);
 *	as_config_add_hosts(&config, "host1,host2:3010,192.168.20.1:3020,[2001::1000]:3030", 3000);
 *	~~~~~~~~~~
 *
 *	@relates as_config
 */
bool
as_config_add_hosts(as_config* config, const char* string, uint16_t default_port);
	
/**
 *	Add host to seed the cluster.
 *	The host address will be copied.
 *	The caller is responsible for the original address string.
 *
 *	~~~~~~~~~~{.c}
 *	as_config config;
 *	as_config_init(&config);
 *	as_config_add_host(&config, "127.0.0.1", 3000);
 *	~~~~~~~~~~
 *
 *	@relates as_config
 */
void
as_config_add_host(as_config* config, const char* address, uint16_t port);

/**
 *	Remove all hosts.
 *
 *	@relates as_config
 */
void
as_config_clear_hosts(as_config* config);

/**
 *	User authentication for servers with restricted access.  The password will be stored by the
 *	client and sent to server in hashed format.
 *
 *	~~~~~~~~~~{.c}
 *		as_config config;
 *		as_config_init(&config);
 *		as_config_set_user(&config, "charlie", "mypassword");
 *	~~~~~~~~~~
 *
 *	@relates as_config
 */
bool
as_config_set_user(as_config* config, const char* user, const char* password);

/**
 *	Free existing string if not null and copy value to string.
 */
void
as_config_set_string(char** str, const char* value);

/**
 *	Set expected cluster name.
 *
 *	@relates as_config
 */
static inline void
as_config_set_cluster_name(as_config* config, const char* cluster_name)
{
	as_config_set_string(&config->cluster_name, cluster_name);
}

/**
 *	Set cluster event callback and user data.
 *
 *	@relates as_config
 */
static inline void
as_config_set_cluster_event_callback(as_config* config, as_cluster_event_callback callback, void* udata)
{
	config->event_callback = callback;
	config->event_callback_udata = udata;
}

/**
 *	Initialize global lua configuration to defaults.
 *
 *	@relates as_config
 */
static inline void
as_config_lua_init(as_config_lua* lua)
{
	lua->cache_enabled = false;
	strcpy(lua->system_path, AS_CONFIG_LUA_SYSTEM_PATH);
	strcpy(lua->user_path, AS_CONFIG_LUA_USER_PATH);
}

/**
 *	Set TLS path to a trusted CA certificate file.
 *
 *	@relates as_config
 */
static inline void
as_config_tls_set_cafile(as_config* config, const char* cafile)
{
	as_config_set_string(&config->tls.cafile, cafile);
}

/**
 *	Set TLS path to a directory of trusted certificates.
 *
 *	@relates as_config
 */
static inline void
as_config_tls_set_capath(as_config* config, const char* capath)
{
	as_config_set_string(&config->tls.capath, capath);
}

/**
 *	Set TLS enabled protocols.
 *
 *	@relates as_config
 */
static inline void
as_config_tls_set_protocols(as_config* config, const char* protocols)
{
	as_config_set_string(&config->tls.protocols, protocols);
}

/**
 *	Set TLS enabled cipher suites.
 *
 *	@relates as_config
 */
static inline void
as_config_tls_set_cipher_suite(as_config* config, const char* cipher_suite)
{
	as_config_set_string(&config->tls.cipher_suite, cipher_suite);
}

/**
 *	Set TLS path to a certificate blacklist file.
 *
 *	@relates as_config
 */
static inline void
as_config_tls_set_cert_blacklist(as_config* config, const char* cert_blacklist)
{
	as_config_set_string(&config->tls.cert_blacklist, cert_blacklist);
}

/**
 *	Set TLS path to the client's key for mutual authentication.
 *
 *	@relates as_config
 */
static inline void
as_config_tls_set_keyfile(as_config* config, const char* keyfile)
{
	as_config_set_string(&config->tls.keyfile, keyfile);
}

/**
 *	Set TLS path to the client's certificate chain file for mutual authentication.
 *
 *	@relates as_config
 */
static inline void
as_config_tls_set_certfile(as_config* config, const char* certfile)
{
	as_config_set_string(&config->tls.certfile, certfile);
}

/**
 *	Add TLS host to seed the cluster.
 *	The host address and TLS name will be copied.
 *	The caller is responsible for the original address string.
 *
 *	~~~~~~~~~~{.c}
 *	as_config config;
 *	as_config_init(&config);
 *	as_config_tls_add_host(&config, "127.0.0.1", "node1.test.org", 3000);
 *	~~~~~~~~~~
 *
 *	@relates as_config
 */
void
as_config_tls_add_host(as_config* config, const char* address, const char* tls_name, uint16_t port);

#ifdef __cplusplus
} // end extern "C"
#endif
