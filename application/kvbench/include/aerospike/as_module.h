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

#include <stdlib.h>

#include <aerospike/as_aerospike.h>
#include <aerospike/as_stream.h>
#include <aerospike/as_result.h>
#include <aerospike/as_types.h>
#include <aerospike/as_udf_context.h>

#ifdef __cplusplus
extern "C" {
#endif

/*****************************************************************************
 * TYPES
 *****************************************************************************/

struct as_module_s;

/**
 * Module events.
 *
 * as_module_event e;
 * e.type = AS_MODULE_CONFIGURE;
 * e.data.config = my_config;
 */

typedef enum as_module_event_type_e {
    AS_MODULE_EVENT_CONFIGURE     = 0,
    AS_MODULE_EVENT_FILE_SCAN     = 1,
    AS_MODULE_EVENT_FILE_ADD      = 2,
    AS_MODULE_EVENT_FILE_REMOVE   = 3,
    AS_MODULE_EVENT_CLEAR_CACHE   = 4
} as_module_event_type;

typedef struct as_module_event_data_s {
    void * 			config;
    const char * 	filename;
} as_module_event_data;

typedef struct as_module_event_s {
    as_module_event_type     type;
    as_module_event_data     data;
} as_module_event;

typedef struct as_module_error_s {
	uint8_t		scope;
	uint32_t 	code;
	char 		message[1024];
	char 		file[256];
	uint32_t	line;
	char 		func[256];
} as_module_error;

/**
 * Module Interface 
 * Provide functions which interface with a module.
 */
typedef struct as_module_hooks_s {

    /**
     * Free resources used by the module.
     */
    int (* destroy)(struct as_module_s * m);

    /**
     * Dispatch an event to the module.
     */
    int (* update)(struct as_module_s * m, as_module_event * e);

    /**
     * Apply a functio to a record
     */
    int (* validate)(struct as_module_s * m, as_aerospike * as, const char * filename, const char * content, uint32_t size, as_module_error * err);

    /**
     * Apply a function to a record
     */
    int (* apply_record)(struct as_module_s * m, as_udf_context *ctx, const char * filename, const char * function, as_rec * rec, as_list * args, as_result * res);

    /**
     * Apply a function to a stream.
     */
    int (* apply_stream)(struct as_module_s * m, as_udf_context *ctx, const char * filename, const char * function, as_stream * istream, as_list * args, as_stream * ostream, as_result *res);

} as_module_hooks;

/**
 * Module Structure.
 * Contains pointer to module specific data and a pointer to the
 * hooks that interface with the module.
 *
 * @field source contains module specific data.
 * @field hooks contains functions that can be applied to the module.
 */
typedef struct as_module_s {
    const void *            source;
    const as_module_hooks * hooks;
} as_module;


/*****************************************************************************
 * INLINE FUNCTIONS
 *****************************************************************************/

/**
 * Get the source of the module.
 *
 * @param m the module to get the source from.
 */
void * as_module_source(as_module * m);

/**
 * Module Destroyer.
 * This frees up the resources used by the module.
 *
 * Proxies to `m->hooks->destroy(m, ...)`
 *
 * @param m the module being initialized.
 * @return 0 on success, otherwise 1
 */
int as_module_destroy(as_module * m);

/**
 * Module Configurator. 
 * This configures and reconfigures the module. This can be called an
 * arbitrary number of times during the lifetime of the server.
 *
 * Proxies to `m->hooks->configure(m, ...)`
 *
 * @param m the module being configured.
 * @return 0 on success, otherwhise 1
 */
int as_module_configure(as_module * m, void * c);

/**
 * Update a Module.
 *
 * Proxies to `m->hooks->update(m, ...)`
 *
 * @param m the module being initialized.
 * @return 0 on success, otherwise 1
 */
int as_module_update(as_module * m, as_module_event * e);

/**
 * Validates a UDF provided by a string.
 *
 * @param m				Module from which the fqn will be resolved.
 * @param as 			aerospike object to be used.
 * @param filename 		The name of the udf module to be validated.
 * @param content 		The content of the udf module to be validated.
 * @param error 		The error string (1024 bytes). Should be preallocated. Will be an empty string if no error occurred.
 *
 * @return 0 on success, otherwise 1 on error.
 */
int as_module_validate(as_module * m, as_aerospike * as, const char * filename, const char * content, uint32_t size, as_module_error * error);

/**
 * Applies a UDF to a stream with provided arguments.
 *
 * @param m 			Module from which the fqn will be resolved.
 * @param ctx 			aerospike udf execution context
 * @param filename		The name of the udf module containing the function to be executed.
 * @param function		The name of the udf function to be executed.
 * @param r 			record to apply to the function.
 * @param args 			list of arguments for the function represented as vals 
 * @param result 		pointer to a val that will be populated with the result.
 *
 * @return 0 on success, otherwise 1
 */
int as_module_apply_record(as_module * m, as_udf_context * ctx, const char * filename, const char * function, as_rec * r, as_list * args, as_result * res);

/**
 * Applies function to a stream and set of arguments. Pushes the results into an output stream.
 *
 * Proxies to `m->hooks->apply_stream(m, ...)`
 *
 * @param m 			Module from which the fqn will be resolved.
 * @param ctx			aerospike udf execution context
 * @param filename		The name of the udf module containing the function to be executed.
 * @param function		The name of the udf function to be executed.
 * @param istream 		pointer to a readable stream, that will provides values.
 * @param args 			list of arguments for the function represented as vals 
 * @param ostream 		pointer to a writable stream, that will be populated with results.
 * @param result 		pointer to a val that will be populated with the result.
 *
 * @return 0 on success, otherwise 1
 */
int as_module_apply_stream(as_module * m, as_udf_context * ctx, const char * filename, const char * function, as_stream * istream, as_list * args, as_stream * ostream, as_result *res);

/**
 * Return lua error in string format when error code is passed in
 *
 * @param errno        The error code
 */
char *as_module_err_string(int);

#ifdef __cplusplus
} // end extern "C"
#endif
