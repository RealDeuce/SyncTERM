/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "tcp_client.h"

#include "io_error.h"

#include <stdio.h>

#if SYNCTERM_PLATFORM_WINDOWS
	#include <ws2tcpip.h>
	typedef int test_socklen_t;
#else
	#include <arpa/inet.h>
	#include <netinet/in.h>
	#include <sys/socket.h>
	typedef socklen_t test_socklen_t;
#endif

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, \
		    __LINE__, #expression); \
		return 1; \
	} \
} while (0)

static bool
cancel_immediately(void *context)
{
	(void)context;
	return true;
}

int
main(void)
{
	struct tcp_client_options options = {
		TCP_CLIENT_ADDRESS_IPV4, 2000, NULL, NULL
	};
	struct sockaddr_in address = {0};
	test_socklen_t address_size = (test_socklen_t)sizeof(address);
	socket_handle_t listener;
	socket_handle_t connected = SOCKET_HANDLE_INVALID;
	socket_handle_t accepted;
	enum tcp_client_address_family family = TCP_CLIENT_ADDRESS_ANY;
	char byte = 0;

	CHECK(socket_runtime_init() == 0);
	listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	CHECK(socket_handle_is_valid(listener));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address.sin_port = 0;
	CHECK(bind(listener, (const struct sockaddr *)&address,
	    (test_socklen_t)sizeof(address)) == 0);
	CHECK(listen(listener, 1) == 0);
	CHECK(getsockname(listener, (struct sockaddr *)&address,
	    &address_size) == 0);
	CHECK(tcp_client_connect(&connected, &family, "127.0.0.1",
	    ntohs(address.sin_port), &options) == 0);
	CHECK(family == TCP_CLIENT_ADDRESS_IPV4);
	accepted = accept(listener, NULL, NULL);
	CHECK(socket_handle_is_valid(accepted));
	CHECK(socket_send(connected, "x", 1) == 1);
	CHECK(socket_receive(accepted, &byte, 1, SOCKET_RECEIVE_NONE) == 1);
	CHECK(byte == 'x');
	CHECK(socket_close(accepted) == 0);
	CHECK(socket_close(connected) == 0);

	connected = listener;
	options.cancel = cancel_immediately;
	CHECK(tcp_client_connect(&connected, NULL, "127.0.0.1",
	    ntohs(address.sin_port), &options) == IO_ERROR_CANCELLED);
	CHECK(connected == listener);
	options.cancel = NULL;
	options.timeout_ms = 0;
	CHECK(tcp_client_connect(&connected, NULL, "127.0.0.1",
	    ntohs(address.sin_port), &options) == IO_ERROR_TIMED_OUT);
	CHECK(connected == listener);

	CHECK(socket_close(listener) == 0);
	CHECK(socket_runtime_cleanup() == 0);
	return 0;
}
