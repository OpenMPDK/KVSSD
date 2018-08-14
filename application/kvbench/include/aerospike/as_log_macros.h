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

#include <aerospike/as_log.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 * as_log.h MACROS
 *****************************************************************************/

#define as_log_error_enabled() (g_as_log.callback && AS_LOG_LEVEL_ERROR <= g_as_log.level)
#define as_log_warn_enabled() (g_as_log.callback && AS_LOG_LEVEL_WARN <= g_as_log.level)
#define as_log_info_enabled() (g_as_log.callback && AS_LOG_LEVEL_INFO <= g_as_log.level)
#define as_log_debug_enabled() (g_as_log.callback && AS_LOG_LEVEL_DEBUG <= g_as_log.level)
#define as_log_trace_enabled() (g_as_log.callback && AS_LOG_LEVEL_TRACE <= g_as_log.level)

#define as_log_error(__fmt, ... ) \
	if (g_as_log.callback) {\
		(g_as_log.callback) (AS_LOG_LEVEL_ERROR, __func__, __FILE__, __LINE__, __fmt, ##__VA_ARGS__);\
	}

#define as_log_warn(__fmt, ... ) \
	if (g_as_log.callback && AS_LOG_LEVEL_WARN <= g_as_log.level) {\
		(g_as_log.callback) (AS_LOG_LEVEL_WARN, __func__, __FILE__, __LINE__, __fmt, ##__VA_ARGS__);\
	}

#define as_log_info(__fmt, ... ) \
	if (g_as_log.callback && AS_LOG_LEVEL_INFO <= g_as_log.level) {\
		(g_as_log.callback) (AS_LOG_LEVEL_INFO, __func__, __FILE__, __LINE__, __fmt, ##__VA_ARGS__);\
	}

#define as_log_debug(__fmt, ... ) \
	if (g_as_log.callback && AS_LOG_LEVEL_DEBUG <= g_as_log.level) {\
		(g_as_log.callback) (AS_LOG_LEVEL_DEBUG, __func__, __FILE__, __LINE__, __fmt, ##__VA_ARGS__);\
	}

#define as_log_trace(__fmt, ... ) \
	if (g_as_log.callback && AS_LOG_LEVEL_TRACE <= g_as_log.level) {\
		(g_as_log.callback) (AS_LOG_LEVEL_TRACE, __func__, __FILE__, __LINE__, __fmt, ##__VA_ARGS__);\
	}

#ifdef __cplusplus
} // end extern "C"
#endif
