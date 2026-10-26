/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "tcp_client_internal.h"

#include "io_error.h"
#include "socket_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ws2tcpip.h>

static int
tcp_client_gai_error(int error)
{
	switch (error) {
		case EAI_MEMORY:
			return IO_ERROR_OUT_OF_MEMORY;
		case EAI_NONAME:
			return IO_ERROR_NOT_FOUND;
		case EAI_FAMILY:
		case EAI_SOCKTYPE:
		case EAI_SERVICE:
			return IO_ERROR_NOT_SUPPORTED;
		default:
			return IO_ERROR_NAME_RESOLUTION;
	}
}

int
tcp_client_resolve(struct tcp_client_address_list *result,
    const char *host, uint16_t port,
    enum tcp_client_address_family family)
{
	struct addrinfo hints = {0};
	struct addrinfo *addresses = NULL;
	struct addrinfo *current;
	char service[6];
	size_t count = 0;
	int error;

	if (result == NULL || host == NULL || *host == '\0')
		return IO_ERROR_INVALID_ARGUMENT;
	result->items = NULL;
	result->count = 0;
	switch (family) {
		case TCP_CLIENT_ADDRESS_ANY:
			hints.ai_family = AF_UNSPEC;
			break;
		case TCP_CLIENT_ADDRESS_IPV4:
			hints.ai_family = AF_INET;
			break;
		case TCP_CLIENT_ADDRESS_IPV6:
			hints.ai_family = AF_INET6;
			break;
		default:
			return IO_ERROR_INVALID_ARGUMENT;
	}
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_NUMERICSERV;
	(void)snprintf(service, sizeof(service), "%u", (unsigned)port);
	error = getaddrinfo(host, service, &hints, &addresses);
	if (error != 0)
		return tcp_client_gai_error(error);
	for (current = addresses; current != NULL; current = current->ai_next) {
		if ((current->ai_family == AF_INET || current->ai_family == AF_INET6) &&
		    current->ai_addrlen <= sizeof(result->items[0].storage))
			count++;
	}
	if (count == 0) {
		freeaddrinfo(addresses);
		return IO_ERROR_NOT_FOUND;
	}
	if (count > SIZE_MAX / sizeof(*result->items)) {
		freeaddrinfo(addresses);
		return IO_ERROR_OUT_OF_RANGE;
	}
	result->items = calloc(count, sizeof(*result->items));
	if (result->items == NULL) {
		freeaddrinfo(addresses);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	count = 0;
	for (current = addresses; current != NULL; current = current->ai_next) {
		if ((current->ai_family != AF_INET && current->ai_family != AF_INET6) ||
		    current->ai_addrlen > sizeof(result->items[count].storage))
			continue;
		result->items[count].family = current->ai_family == AF_INET ?
		    TCP_CLIENT_ADDRESS_IPV4 : TCP_CLIENT_ADDRESS_IPV6;
		result->items[count].native_family = current->ai_family;
		result->items[count].length = current->ai_addrlen;
		memcpy(result->items[count].storage, current->ai_addr,
		    current->ai_addrlen);
		count++;
	}
	freeaddrinfo(addresses);
	result->count = count;
	return 0;
}

void
tcp_client_free_addresses(struct tcp_client_address_list *addresses)
{
	if (addresses == NULL)
		return;
	free(addresses->items);
	addresses->items = NULL;
	addresses->count = 0;
}
