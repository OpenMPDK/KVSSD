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

#include <stdbool.h>

#include <aerospike/as_val.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 * TYPES
 ******************************************************************************/

typedef struct as_result_s {
    bool        is_malloc;
    cf_atomic32 count;
    bool is_success;
    as_val * value;
} as_result;

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/

as_result * as_result_init(as_result *r);
as_result * as_result_new();

as_result * as_result_reserve(as_result *r);

void as_result_destroy(as_result *);

// These functions new an as_result object - consume the as_val
as_result * as_success_new(as_val *);
as_result * as_failure_new(as_val *);

// These functions init an as_result object - consume the as_val
as_result * as_success_init(as_result *, as_val *);
as_result * as_failure_init(as_result *, as_val *);

// retrieves the value associated with success or failure
as_val * as_result_value(as_result *);

as_result * as_result_setsuccess(as_result * r, as_val * v);
as_result * as_result_setfailure(as_result * r, as_val * v);

#ifdef __cplusplus
} // end extern "C"
#endif
