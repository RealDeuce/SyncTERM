/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "socket.h"

#include "io_error.h"

#include <stdio.h>
#include <string.h>

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

int
main(void)
{
	socket_handle_t listener;
	socket_handle_t client;
	socket_handle_t accepted;
	struct sockaddr_in address = {0};
	test_socklen_t address_size = (test_socklen_t)sizeof(address);
	unsigned events = 99;
	char received[4] = {0};

	CHECK(socket_runtime_init() == 0);
	CHECK(!socket_handle_is_valid(SOCKET_HANDLE_INVALID));
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
	client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	CHECK(socket_handle_is_valid(client));
	CHECK(connect(client, (const struct sockaddr *)&address,
	    (test_socklen_t)sizeof(address)) == 0);
	accepted = accept(listener, NULL, NULL);
	CHECK(socket_handle_is_valid(accepted));

	CHECK(socket_set_blocking(client, true) == 0);
	CHECK(socket_set_keepalive(client, true) == 0);
	CHECK(socket_set_no_delay(client, true) == 0);
	CHECK(socket_set_send_buffer_size(client, 4096) == 0);
	CHECK(socket_set_receive_buffer_size(client, 4096) == 0);
	CHECK(socket_wait(client, SOCKET_WAIT_READABLE, 0, &events) ==
	    IO_ERROR_TIMED_OUT);
	CHECK(events == 0);
	CHECK(socket_send(accepted, "abc", 3) == 3);
	CHECK(socket_wait(client, SOCKET_WAIT_READABLE, 1000, &events) == 0);
	CHECK((events & SOCKET_WAIT_READABLE) != 0);
	CHECK(socket_receive(client, received, 1, SOCKET_RECEIVE_PEEK) == 1);
	CHECK(received[0] == 'a');
	CHECK(socket_receive(client, received, 3, SOCKET_RECEIVE_NONE) == 3);
	CHECK(memcmp(received, "abc", 3) == 0);
	CHECK(send(accepted, "!", 1, MSG_OOB) == 1);
	CHECK(socket_wait(client, SOCKET_WAIT_URGENT, 1000, &events) == 0);
	CHECK((events & SOCKET_WAIT_URGENT) != 0);
	CHECK(socket_receive(client, received, 1, SOCKET_RECEIVE_URGENT) == 1);
	CHECK(received[0] == '!');
	CHECK(socket_shutdown(accepted, SOCKET_SHUTDOWN_WRITE) == 0);
	CHECK(socket_wait(client, SOCKET_WAIT_READABLE, 1000, &events) == 0);
	CHECK((events & SOCKET_WAIT_CLOSED) != 0);
	CHECK(socket_receive(client, received, sizeof(received),
	    SOCKET_RECEIVE_NONE) == 0);

	CHECK(socket_close(accepted) == 0);
	CHECK(socket_close(client) == 0);
	CHECK(socket_close(listener) == 0);
	CHECK(socket_runtime_cleanup() == 0);
	return 0;
}
