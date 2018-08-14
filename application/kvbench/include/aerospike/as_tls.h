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

#include <openssl/ssl.h>

#include <aerospike/as_config.h>
#include <aerospike/as_status.h>
#include <aerospike/as_socket.h>


void as_tls_check_init();

void as_tls_cleanup();

void as_tls_thread_cleanup();

as_status as_tls_context_setup(as_config_tls* tlscfg,
							   as_tls_context* octx,
							   as_error* err);

void as_tls_context_destroy(as_tls_context* ctx);

as_status as_tls_config_reload(as_config_tls* tlscfg, as_tls_context* ctx, as_error *err);

int as_tls_wrap(as_tls_context* ctx, as_socket* sock, const char* tls_name);

void as_tls_set_name(as_socket* sock, const char* tls_name);

int as_tls_connect_once(as_socket* sock);

int as_tls_connect(as_socket* sock);

// int as_tls_peek(as_socket* sock, void* buf, int num);

int as_tls_read_pending(as_socket* sock);

int as_tls_read_once(as_socket* sock, void* buf, size_t num);

int as_tls_read(as_socket* sock, void* buf, size_t num, uint32_t socket_timeout, uint64_t deadline);

int as_tls_write_once(as_socket* sock, void* buf, size_t num);

int as_tls_write(as_socket* sock, void* buf, size_t num, uint32_t socket_timeout, uint64_t deadline);
