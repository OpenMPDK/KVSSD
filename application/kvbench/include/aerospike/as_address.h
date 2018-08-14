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

#include <citrusleaf/cf_byte_order.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 *	@private
 *	Convert socket address (including port) to a string.
 *
 *	Formats:
 *	~~~~~~~~~~{.c}
 *	IPv4: xxx.xxx.xxx.xxx:<port>
 *	IPv6: [xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx]:<port>
 *	~~~~~~~~~~
 */
void
as_address_name(struct sockaddr* addr, char* name, socklen_t size);

/**
 *	@private
 *	Convert socket address to a string without brackets or a port.
 *
 *	Formats:
 *	~~~~~~~~~~{.c}
 *	IPv4: xxx.xxx.xxx.xxx
 *	IPv6: xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx
 *	~~~~~~~~~~
 */
void
as_address_short_name(struct sockaddr* addr, char* name, socklen_t size);

/**
 *	@private
 *	Return port of address.
 */
static inline in_port_t
as_address_port(struct sockaddr* addr)
{
	in_port_t port = (addr->sa_family == AF_INET)?
		((struct sockaddr_in*)addr)->sin_port :
		((struct sockaddr_in6*)addr)->sin6_port;
	return cf_swap_from_be16(port);
}

/**
 *	@private
 *	Return size of socket address.
 */
static inline socklen_t
as_address_size(struct sockaddr* addr)
{
	return (addr->sa_family == AF_INET)? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
}

/**
 *	@private
 *	Copy socket address to storage.
 */
static inline void
as_address_copy_storage(struct sockaddr* src, struct sockaddr_storage* trg)
{
	size_t size = as_address_size(src);
	memcpy(trg, src, size);
}

#ifdef __cplusplus
} // end extern "C"
#endif
