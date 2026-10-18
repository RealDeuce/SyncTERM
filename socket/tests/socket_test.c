/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "socket.h"

#include "io_error.h"

#include <errno.h>
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

static void
diagnose_urgent_mark(socket_handle_t socket, const char *stage, bool at_mark)
{
#if SYNCTERM_PLATFORM_WINDOWS
	u_long mark = 0;
	u_long available = 0;
	int inline_oob = -1;
	int inline_size = sizeof(inline_oob);
	int mark_status = ioctlsocket(socket, SIOCATMARK, &mark);
	int mark_error = mark_status == 0 ? 0 : WSAGetLastError();
	int available_status = ioctlsocket(socket, FIONREAD, &available);
	int available_error = available_status == 0 ? 0 : WSAGetLastError();
	int inline_status = getsockopt(socket, SOL_SOCKET, SO_OOBINLINE,
	    (char *)&inline_oob, &inline_size);
	int inline_error = inline_status == 0 ? 0 : WSAGetLastError();

	fprintf(stderr, "urgent stage=%s portable-mark=%d SIOCATMARK=%lu"
	    " status=%d error=%d FIONREAD=%lu status=%d error=%d"
	    " SO_OOBINLINE=%d status=%d error=%d\n",
	    stage, at_mark, mark, mark_status, mark_error, available,
	    available_status, available_error, inline_oob, inline_status, inline_error);
#else
	(void)socket;
	fprintf(stderr, "urgent stage=%s portable-mark=%d\n", stage, at_mark);
#endif
}

static ssize_t
receive_urgent(socket_handle_t socket, void *data, unsigned flags)
{
	bool before = false, after = false;
	int before_status = socket_at_urgent_mark(socket, &before);
	ssize_t result = socket_receive(socket, data, 1, flags);
#if SYNCTERM_PLATFORM_WINDOWS
	int native_error = result < 0 ? WSAGetLastError() : 0;
#else
	int native_error = result < 0 ? errno : 0;
#endif
	int after_status = socket_at_urgent_mark(socket, &after);

	fprintf(stderr, "urgent receive flags=%u result=%zd native-error=%d"
	    " before-mark=%d status=%d after-mark=%d status=%d\n",
	    flags, result, native_error, before, before_status, after, after_status);
	return result;
}

int
main(void)
{
	socket_handle_t listener;
	socket_handle_t client;
	socket_handle_t accepted;
	struct sockaddr_in address = {0};
	test_socklen_t address_size = (test_socklen_t)sizeof(address);
	unsigned events = 99;
	char received[5] = {0};
	bool at_mark;

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

	CHECK(socket_set_blocking(client, false) == 0);
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
	CHECK(socket_at_urgent_mark(client, NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(socket_send(accepted, "pre", 3) == 3);
	received[0] = 0x02;
	CHECK(send(accepted, received, 1, MSG_OOB) == 1);
	CHECK(socket_send(accepted, "post", 4) == 4);
	CHECK(socket_wait(client, SOCKET_WAIT_URGENT, 1000, &events) == 0);
	CHECK((events & SOCKET_WAIT_URGENT) != 0);
	CHECK(socket_at_urgent_mark(client, &at_mark) == 0);
	diagnose_urgent_mark(client, "before pre", at_mark);
	CHECK(!at_mark);
	CHECK(receive_urgent(client, received,
	    SOCKET_RECEIVE_PEEK | SOCKET_RECEIVE_URGENT) == 1);
	CHECK(received[0] == 0x02);
	CHECK(socket_receive(client, received, 3, SOCKET_RECEIVE_NONE) == 3);
	CHECK(memcmp(received, "pre", 3) == 0);
	CHECK(socket_at_urgent_mark(client, &at_mark) == 0);
	diagnose_urgent_mark(client, "after pre", at_mark);
	CHECK(at_mark);
	CHECK(receive_urgent(client, received, SOCKET_RECEIVE_URGENT) == 1);
	CHECK(received[0] == 0x02);
	CHECK(socket_receive(client, received, 4, SOCKET_RECEIVE_NONE) == 4);
	CHECK(memcmp(received, "post", 4) == 0);
	CHECK(send(accepted, "?", 1, MSG_OOB) == 1);
	CHECK(socket_wait(client, SOCKET_WAIT_URGENT, 1000, &events) == 0);
	CHECK((events & SOCKET_WAIT_URGENT) != 0);
	CHECK(receive_urgent(client, received,
	    SOCKET_RECEIVE_PEEK | SOCKET_RECEIVE_URGENT) == 1);
	CHECK(received[0] == '?');
	CHECK(receive_urgent(client, received, SOCKET_RECEIVE_URGENT) == 1);
	CHECK(received[0] == '?');
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
