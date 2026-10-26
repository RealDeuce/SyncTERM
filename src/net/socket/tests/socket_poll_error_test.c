/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>

static int peek_error;
static int pending_error;
static unsigned queries;

static ssize_t
test_recv(int descriptor, void *buffer, size_t size, int flags)
{
	(void)descriptor;
	(void)buffer;
	(void)size;
	(void)flags;
	errno = peek_error;
	return -1;
}

static int
test_getsockopt(int descriptor, int level, int option, void *value,
	socklen_t *size)
{
	(void)descriptor;
	(void)level;
	(void)option;
	(void)size;
	++queries;
	*(int *)value = pending_error;
	return 0;
}

/* Model Linux's read-and-clear SO_ERROR behavior independently of the host
 * kernel. The real public TCP test also exercises loopback refusal. */
#define recv test_recv
#define getsockopt test_getsockopt
#include "../socket_posix.c"
#undef getsockopt
#undef recv

#define CHECK(e) do { if (!(e)) { \
	fprintf(stderr, "socket poll check failed: %s:%d: %s\n", __FILE__, __LINE__, #e); \
	return 1; \
} } while (0)

int
main(void)
{
	struct socket_wait_item item = {.interests = SOCKET_WAIT_WRITABLE};

	peek_error = ECONNREFUSED;
	pending_error = 0; /* recv consumed the pending error. */
	socket_poll_events(&item, POLLIN | POLLHUP | POLLERR);
	CHECK(item.error == IO_ERROR_CONNECTION_REFUSED);
	CHECK((item.events & SOCKET_WAIT_ERROR) != 0);
	CHECK(queries == 0);

	item.events = 0;
	item.error = 0;
	peek_error = EAGAIN;
	pending_error = ECONNREFUSED;
	socket_poll_events(&item, POLLIN | POLLERR);
	CHECK(item.error == IO_ERROR_CONNECTION_REFUSED);
	CHECK(queries == 1);

	item.events = 0;
	item.error = 0;
	pending_error = 0;
	socket_poll_events(&item, POLLERR);
	CHECK(item.error == IO_ERROR_IO);
	CHECK(queries == 2);
	return 0;
}
