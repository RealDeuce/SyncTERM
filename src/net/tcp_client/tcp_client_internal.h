/* Private resolved-address interface for TCP client connection racing. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef SYNCTERM_TCP_CLIENT_INTERNAL_H_
#define SYNCTERM_TCP_CLIENT_INTERNAL_H_

#include "tcp_client.h"

#include <stddef.h>

#if !SYNCTERM_PLATFORM_WINDOWS
	#include <sys/socket.h>
#endif

struct tcp_client_address {
	enum tcp_client_address_family family;
	int native_family;
	size_t length;
	union {
		struct sockaddr_storage alignment;
		unsigned char storage[sizeof(struct sockaddr_storage)];
	};
};

struct tcp_client_address_list {
	struct tcp_client_address *items;
	size_t count;
};

int tcp_client_resolve(struct tcp_client_address_list *result,
    const char *host, uint16_t port,
    enum tcp_client_address_family family);
void tcp_client_free_addresses(struct tcp_client_address_list *addresses);

#endif
