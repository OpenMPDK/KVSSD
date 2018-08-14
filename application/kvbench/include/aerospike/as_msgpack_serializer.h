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

#include <aerospike/as_msgpack.h>
#include <aerospike/as_serializer.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/

/**
 * Creates a new serializer for msgpack.
 */
as_serializer *as_msgpack_new();

/**
 * Initializes a serializer for msgpack.
 */
as_serializer *as_msgpack_init(as_serializer *);

#ifdef __cplusplus
} // end extern "C"
#endif
