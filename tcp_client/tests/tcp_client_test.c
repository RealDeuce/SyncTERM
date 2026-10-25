/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "tcp_client.h"

#include "io_error.h"

#include <stdio.h>
#include <inttypes.h>
#include <errno.h>
#include "socket_internal.h"

static void
diagnose(void *context, enum tcp_client_event event,
	enum tcp_client_address_family family, int status)
{
	int64_t now = 0;
	int clock_status = socket_internal_now_ms(&now);

	(void)context;
	fprintf(stderr, "TCP event=%d family=%d status=%d at=%" PRId64
	    "ms clock-status=%d\n", event, family, status, now, clock_status);
}

#if SYNCTERM_PLATFORM_WINDOWS
	#include <ws2tcpip.h>
	typedef int test_socklen_t;
#else
	#include <arpa/inet.h>
	#include <netinet/in.h>
	#include <sys/socket.h>
	#include <sys/select.h>
	typedef socklen_t test_socklen_t;
#endif

static int
native_error(void)
{
#if SYNCTERM_PLATFORM_WINDOWS
	return WSAGetLastError();
#else
	return errno;
#endif
}

static void
probe_refusal(const struct sockaddr_in *address)
{
	socket_handle_t probe = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	fd_set readers, writers, exceptions;
	struct timeval timeout = {.tv_sec = 10, .tv_usec = 0};
	int pending = 0;
	test_socklen_t length = (test_socklen_t)sizeof(pending);

	if (!socket_handle_is_valid(probe)) {
		fprintf(stderr, "native refusal probe socket failed: %d\n", native_error());
		return;
	}
#if !SYNCTERM_PLATFORM_WINDOWS
	if (probe >= FD_SETSIZE) {
		fprintf(stderr, "native refusal probe fd=%d exceeds select capacity\n", probe);
		(void)socket_close(probe);
		return;
	}
#endif
	int status = socket_set_blocking(probe, false);
	if (status != 0) {
		fprintf(stderr, "native refusal probe nonblocking failed: %d\n", status);
		(void)socket_close(probe);
		return;
	}
	int connected = connect(probe, (const struct sockaddr *)address,
	    (test_socklen_t)sizeof(*address));
	int connect_error = connected == 0 ? 0 : native_error();
	FD_ZERO(&readers);
	FD_ZERO(&writers);
	FD_ZERO(&exceptions);
	FD_SET(probe, &readers);
	FD_SET(probe, &writers);
	FD_SET(probe, &exceptions);
#if SYNCTERM_PLATFORM_WINDOWS
	int ready = select(0, &readers, &writers, &exceptions, &timeout);
	int wait_error = ready < 0 ? native_error() : 0;
	int queried = getsockopt(probe, SOL_SOCKET, SO_ERROR, (char *)&pending, &length);
#else
	int ready = select(probe + 1, &readers, &writers, &exceptions, &timeout);
	int wait_error = ready < 0 ? native_error() : 0;
	int queried = getsockopt(probe, SOL_SOCKET, SO_ERROR, &pending, &length);
#endif
	int query_error = queried == 0 ? 0 : native_error();
	fprintf(stderr, "native refusal probe: connect=%d error=%d select=%d"
	    " error=%d readable=%d writable=%d exception=%d SO_ERROR=%d"
	    " query=%d error=%d wait-budget=10000ms\n", connected, connect_error, ready, wait_error,
	    FD_ISSET(probe, &readers) != 0, FD_ISSET(probe, &writers) != 0,
	    FD_ISSET(probe, &exceptions) != 0, pending, queried, query_error);
	(void)socket_close(probe);
}

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
		.family = TCP_CLIENT_ADDRESS_IPV4, .timeout_ms = 2000
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
	/* Choose an unused loopback port, then release it. Some stacks leave
	 * connections to a bound, non-listening socket pending until listen(). */
	listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	CHECK(socket_handle_is_valid(listener));
	address.sin_port = 0;
	CHECK(bind(listener, (const struct sockaddr *)&address,
	    (test_socklen_t)sizeof(address)) == 0);
	CHECK(getsockname(listener, (struct sockaddr *)&address, &address_size) == 0);
	CHECK(socket_close(listener) == 0);
	connected = SOCKET_HANDLE_INVALID;
	family = TCP_CLIENT_ADDRESS_ANY;
	/* Native Windows TCP can retry a refused loopback connect before
	 * reporting WSAECONNREFUSED. Deadline behavior is tested separately;
	 * this integration case checks the eventual refusal, not retry timing. */
	options.timeout_ms = 10000;
	options.diagnostic = diagnose;
	fprintf(stderr, "refusal test: closed loopback port=%u timeout=%" PRId64 "ms\n",
	    (unsigned)ntohs(address.sin_port), options.timeout_ms);
	int refused = tcp_client_connect(&connected, &family, "127.0.0.1",
	    ntohs(address.sin_port), &options);
	if (refused != IO_ERROR_CONNECTION_REFUSED)
		probe_refusal(&address);
	if (refused == 0)
		(void)socket_close(connected);
	if (refused != IO_ERROR_CONNECTION_REFUSED)
		fprintf(stderr, "refused connection returned %d\n", refused);
	CHECK(refused == IO_ERROR_CONNECTION_REFUSED);
	CHECK(connected == SOCKET_HANDLE_INVALID && family == TCP_CLIENT_ADDRESS_ANY);
	CHECK(socket_runtime_cleanup() == 0);
	return 0;
}
