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

#include <inttypes.h>

#ifndef CF_WINDOWS
/******************************************************************************
 * LINUX
 *****************************************************************************/

#include <stdbool.h>    // A real pity that Linux requires this for bool, true & false:
#include <alloca.h>     // Use alloca() instead of variable-sized stack arrays for non-gcc portability.

#else // CF_WINDOWS
/******************************************************************************
 * WINDOWS
 *****************************************************************************/

#include <malloc.h>     // for alloca()

#define PRIu64 "I64u"
#define PRId64 "I64d"

#endif // CF_WINDOWS
