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

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *	TYPES
 *****************************************************************************/

/**
 *	Log Level
 */
typedef enum as_log_level_e {
	AS_LOG_LEVEL_ERROR = 0,
	AS_LOG_LEVEL_WARN = 1,
	AS_LOG_LEVEL_INFO = 2,
	AS_LOG_LEVEL_DEBUG = 3,
	AS_LOG_LEVEL_TRACE = 4
} as_log_level;

/**
 *	Callback function for as_log related logging calls.
 *
 *	The following is a simple log callback:
 *	~~~~~~~~~~{.c}
 *	bool my_log_callback(
 *		as_log_level level, const char * func, const char * file, uint32_t line,
 *		const char * fmt, ...)
 *	{
 *		char msg[1024] = {0};
 *	
 *		va_list ap;
 *		va_start(ap, fmt);
 *		vsnprintf(msg, 1024, fmt, ap);
 *		msg[1023] = '\0';
 *		va_end(ap);
 *		
 *		fprintf(stderr, "[%s:%d][%s] %d - %s\n", file, line, func, level, msg);
 *	
 *		return true;
 *	}
 *	~~~~~~~~~~
 *
 *	The function should return true on success.
 *
 *
 *	@param level 		The log level of the message.
 *	@param func 		The function where the message was logged.
 *	@param file 		The file where the message was logged.
 *	@param line 		The line where the message was logged.
 *	@param fmt 			The format string used.
 *	@param ... 			The format argument.
 *
 *	@return true if the message was logged. Otherwise false.
 *
 *	@ingroup as_log_object
 */
typedef bool (* as_log_callback)(
	as_log_level level, const char * func, const char * file, uint32_t line,
	const char * fmt, ...);

/**
 *	Aerospike Client exposed logging functionality including:
 *	- Ability to control the verbosity of log messages.
 *	- Direct where log messages are sent to.
 *
 *	## Setting Log Level
 *
 *	To set the log level for the aerospike client, simply use 
 *	as_log_set_level() and pass in the client log to set.
 *
 *	~~~~~~~~~~{.c}
 *	as_log_set_level(AS_LOG_LEVEL_INFO);
 *	~~~~~~~~~~
 *
 *	## Redirecting Log Output
 *
 *	By default, the logger is not enabled.
 *
 *	To enable where log messages are sent, simply define a new @ref as_log_callback,
 *	and set it for the client using as_log_set_callback():
 *
 *	~~~~~~~~~~{.c}
 *	as_log_set_callback(my_log_callback);
 *	~~~~~~~~~~
 *
 *	Where the `my_log_callback` could be defined as 
 *
 *	~~~~~~~~~~{.c}
 *	bool my_log_callback(
 *	    as_log_level level, const char * func, const char * file, uint32_t line,
 *	    const char * fmt, ...)
 *	{
 *	    char msg[1024] = {0};
 *	    va_list ap;
 *	    va_start(ap, fmt);
 *	    vsnprintf(msg, 1024, fmt, ap);
 *	    msg[1023] = '\0';
 *	    va_end(ap);
 *	    fprintf(stderr, "[%s:%d][%s] %d - %s\n", file, line, func, level, msg);
 *	    return true;
 *	}
 *	~~~~~~~~~~
 *
 *	@ingroup client_objects
 */
typedef struct as_log_s {

	/**
	 *	Log Level
	 */
	as_log_level level;

	/**
	 *	Logging Callback
	 */
	as_log_callback callback;

} as_log;

/******************************************************************************
 *	GLOBAL VARIABLES
 *****************************************************************************/

extern as_log g_as_log;
extern const char* as_log_level_strings[];

/******************************************************************************
 *	FUNCTIONS
 *****************************************************************************/

/**
 *	Set logging level for the global client log.
 *
 *	@param level 	The log level.
 *
 *	@relates as_log
 */
static inline void
as_log_set_level(as_log_level level)
{
	g_as_log.level = level;
}

/**
 *	Set logging callback for the global client log.
 *
 *	@param callback 	The log callback.
 *
 *	@relates as_log
 */
static inline void
as_log_set_callback(as_log_callback callback)
{
	g_as_log.callback = callback;
}

/**
 *	Convert log level to a string.
 *
 *	@param level 	The log level.
 *
 *	@relates as_log
 */
static inline const char*
as_log_level_tostring(as_log_level level)
{
	return as_log_level_strings[level];
}

#ifdef __cplusplus
} // end extern "C"
#endif
