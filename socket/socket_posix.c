/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "socket_internal.h"

#include "io_error.h"
#include "socket_config.h"
#include "st_time.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

int
socket_internal_map_error(int error)
{
	switch (error) {
		case 0:
			return 0;
		case EACCES:
		case EPERM:
			return IO_ERROR_PERMISSION_DENIED;
		case ENOMEM:
			return IO_ERROR_OUT_OF_MEMORY;
		case EINTR:
			return IO_ERROR_INTERRUPTED;
#if EAGAIN != EWOULDBLOCK
		case EAGAIN:
#endif
		case EWOULDBLOCK:
			return IO_ERROR_WOULD_BLOCK;
		case EINPROGRESS:
		case EALREADY:
			return IO_ERROR_IN_PROGRESS;
		case ETIMEDOUT:
			return IO_ERROR_TIMED_OUT;
		case ECONNREFUSED:
			return IO_ERROR_CONNECTION_REFUSED;
		case ECONNRESET:
		case EPIPE:
			return IO_ERROR_CONNECTION_RESET;
		case ECONNABORTED:
			return IO_ERROR_CONNECTION_ABORTED;
		case ENOTCONN:
			return IO_ERROR_NOT_CONNECTED;
		case ENETUNREACH:
			return IO_ERROR_NETWORK_UNREACHABLE;
		case EHOSTUNREACH:
			return IO_ERROR_HOST_UNREACHABLE;
		case EADDRINUSE:
			return IO_ERROR_ADDRESS_IN_USE;
		case EADDRNOTAVAIL:
			return IO_ERROR_ADDRESS_NOT_AVAILABLE;
		case EMSGSIZE:
			return IO_ERROR_MESSAGE_TOO_LARGE;
		case EPROTONOSUPPORT:
		case EPROTOTYPE:
		case EAFNOSUPPORT:
			return IO_ERROR_PROTOCOL;
		case EINVAL:
		case EBADF:
		case ENOTSOCK:
		case EDESTADDRREQ:
			return IO_ERROR_INVALID_ARGUMENT;
		case ENOSPC:
		case ENOBUFS:
			return IO_ERROR_NO_SPACE;
		default:
			return IO_ERROR_IO;
	}
}

int
socket_runtime_init(void)
{
	return 0;
}

int
socket_runtime_cleanup(void)
{
	return 0;
}

bool
socket_handle_is_valid(socket_handle_t socket)
{
	return socket != SOCKET_HANDLE_INVALID;
}

int
socket_close(socket_handle_t socket)
{
	if (!socket_handle_is_valid(socket))
		return IO_ERROR_INVALID_ARGUMENT;
	if (close(socket) == 0)
		return 0;
	return socket_internal_map_error(errno);
}

int
socket_shutdown(socket_handle_t socket,
    enum socket_shutdown_direction direction)
{
	int how;

	switch (direction) {
		case SOCKET_SHUTDOWN_READ:
			how = SHUT_RD;
			break;
		case SOCKET_SHUTDOWN_WRITE:
			how = SHUT_WR;
			break;
		case SOCKET_SHUTDOWN_BOTH:
			how = SHUT_RDWR;
			break;
		default:
			return IO_ERROR_INVALID_ARGUMENT;
	}
	if (shutdown(socket, how) == 0)
		return 0;
	return socket_internal_map_error(errno);
}

int
socket_set_blocking(socket_handle_t socket, bool blocking)
{
	int flags = fcntl(socket, F_GETFL);

	if (flags < 0)
		return socket_internal_map_error(errno);
	if (blocking)
		flags &= ~O_NONBLOCK;
	else
		flags |= O_NONBLOCK;
	if (fcntl(socket, F_SETFL, flags) == 0)
		return 0;
	return socket_internal_map_error(errno);
}

static int
socket_set_boolean_option(socket_handle_t socket, int option, bool enabled)
{
	int value = enabled ? 1 : 0;

	if (setsockopt(socket, SOL_SOCKET, option, &value,
	    (socklen_t)sizeof(value)) == 0)
		return 0;
	return socket_internal_map_error(errno);
}

int
socket_set_keepalive(socket_handle_t socket, bool enabled)
{
	return socket_set_boolean_option(socket, SO_KEEPALIVE, enabled);
}

int
socket_set_no_delay(socket_handle_t socket, bool enabled)
{
	int value = enabled ? 1 : 0;

	if (setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &value,
	    (socklen_t)sizeof(value)) == 0)
		return 0;
	return socket_internal_map_error(errno);
}

static int
socket_set_buffer_size(socket_handle_t socket, int option, size_t size)
{
	int value;

	if (size > INT_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	value = (int)size;
	if (setsockopt(socket, SOL_SOCKET, option, &value,
	    (socklen_t)sizeof(value)) == 0)
		return 0;
	return socket_internal_map_error(errno);
}

int
socket_set_send_buffer_size(socket_handle_t socket, size_t size)
{
	return socket_set_buffer_size(socket, SO_SNDBUF, size);
}

int
socket_set_receive_buffer_size(socket_handle_t socket, size_t size)
{
	return socket_set_buffer_size(socket, SO_RCVBUF, size);
}

ssize_t
socket_send(socket_handle_t socket, const void *buffer, size_t size)
{
	ssize_t result;
	int flags = 0;

	if (buffer == NULL && size != 0)
		return IO_ERROR_INVALID_ARGUMENT;
	if (size > (size_t)PTRDIFF_MAX)
		size = (size_t)PTRDIFF_MAX;
#if SYNCTERM_SOCKET_HAVE_MSG_NOSIGNAL
	flags |= MSG_NOSIGNAL;
#endif
	result = send(socket, buffer, size, flags);
	if (result >= 0)
		return result;
	return socket_internal_map_error(errno);
}

ssize_t
socket_receive(socket_handle_t socket, void *buffer, size_t size,
    unsigned flags)
{
	int native_flags = 0;
	ssize_t result;

	if ((buffer == NULL && size != 0) ||
	    (flags & ~(SOCKET_RECEIVE_PEEK | SOCKET_RECEIVE_URGENT)) != 0)
		return IO_ERROR_INVALID_ARGUMENT;
	/* Native urgent peeks can consume the stream mark (Darwin). Do not
	 * expose a supposedly non-consuming operation with that side effect. */
	if (flags == (SOCKET_RECEIVE_PEEK | SOCKET_RECEIVE_URGENT))
		return IO_ERROR_INVALID_ARGUMENT;
	if (size > (size_t)PTRDIFF_MAX)
		size = (size_t)PTRDIFF_MAX;
	if ((flags & SOCKET_RECEIVE_PEEK) != 0)
		native_flags |= MSG_PEEK;
	if ((flags & SOCKET_RECEIVE_URGENT) != 0)
		native_flags |= MSG_OOB;
	result = recv(socket, buffer, size, native_flags);
	if (result >= 0)
		return result;
	return socket_internal_map_error(errno);
}

int
socket_at_urgent_mark(socket_handle_t socket, bool *at_mark)
{
	int result;

	if (at_mark == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = sockatmark(socket);
	if (result < 0)
		return socket_internal_map_error(errno);
	*at_mark = result != 0;
	return 0;
}

int
socket_internal_now_ms(int64_t *result)
{
	uint64_t now;
	int status;

	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_time_monotonic_ms(&now);
	if (status < 0)
		return status;
	*result = now > INT64_MAX ? INT64_MAX : (int64_t)now;
	return 0;
}

size_t
socket_internal_wait_capacity(void)
{
	return SIZE_MAX;
}

static void
socket_poll_events(struct socket_wait_item *item, short events)
{
	int socket_error = 0;
	socklen_t error_size = (socklen_t)sizeof(socket_error);
	char byte;
	ssize_t received;

	if ((events & POLLOUT) != 0 &&
	    (item->interests & SOCKET_WAIT_WRITABLE) != 0)
		item->events |= SOCKET_WAIT_WRITABLE;
	if ((events & POLLPRI) != 0 &&
	    (item->interests & SOCKET_WAIT_URGENT) != 0) {
		item->events |= SOCKET_WAIT_URGENT;
		/* Even an ordinary MSG_PEEK can clear the urgent mark on BSD.
		 * Let the caller consume OOB before classifying data versus EOF. */
		if ((events & POLLIN) != 0 &&
		    (item->interests & SOCKET_WAIT_READABLE) != 0)
			item->events |= SOCKET_WAIT_READABLE;
	}
	else if ((events & (POLLIN | POLLHUP)) != 0) {
		received = recv(item->socket, &byte, 1, MSG_PEEK | MSG_DONTWAIT);
		if (received > 0) {
			if ((item->interests & SOCKET_WAIT_READABLE) != 0)
				item->events |= SOCKET_WAIT_READABLE;
		}
		else if (received == 0)
			item->events |= SOCKET_WAIT_CLOSED;
		else if (errno != EAGAIN && errno != EWOULDBLOCK) {
			item->events |= SOCKET_WAIT_ERROR;
			item->error = socket_internal_map_error(errno);
		}
	}
	/* recv() can consume SO_ERROR. Do not replace that specific error with
	 * EIO after a second read of the now-cleared socket error. */
	if ((events & (POLLERR | POLLNVAL)) != 0 && item->error == 0) {
		if (getsockopt(item->socket, SOL_SOCKET, SO_ERROR, &socket_error,
		    &error_size) != 0)
			socket_error = errno;
		if (socket_error == 0)
			socket_error = EIO;
		item->events |= SOCKET_WAIT_ERROR;
		item->error = socket_internal_map_error(socket_error);
	}
}

static int
socket_select_wait_many(struct socket_wait_item *items, size_t count,
    int64_t timeout_ms)
{
	fd_set read_set;
	fd_set write_set;
	fd_set except_set;
	struct timeval timeout;
	struct timeval *timeout_pointer = NULL;
	int maximum = -1;
	int result;

	FD_ZERO(&read_set);
	FD_ZERO(&write_set);
	FD_ZERO(&except_set);
	for (size_t index = 0; index < count; index++) {
		if (items[index].socket < 0 || items[index].socket >= FD_SETSIZE)
			return IO_ERROR_OUT_OF_RANGE;
		items[index].events = 0;
		items[index].error = 0;
		if ((items[index].interests & SOCKET_WAIT_READABLE) != 0)
			FD_SET(items[index].socket, &read_set);
		if ((items[index].interests & SOCKET_WAIT_WRITABLE) != 0)
			FD_SET(items[index].socket, &write_set);
		if ((items[index].interests & SOCKET_WAIT_URGENT) != 0)
			FD_SET(items[index].socket, &except_set);
		if (items[index].socket > maximum)
			maximum = items[index].socket;
	}
	if (timeout_ms >= 0) {
		if (timeout_ms > INT_MAX)
			timeout_ms = INT_MAX;
		timeout.tv_sec = (long)(timeout_ms / 1000);
		timeout.tv_usec = (long)((timeout_ms % 1000) * 1000);
		timeout_pointer = &timeout;
	}
	result = select(maximum + 1, &read_set, &write_set, &except_set,
	    timeout_pointer);
	if (result < 0)
		return socket_internal_map_error(errno);
	if (result == 0)
		return IO_ERROR_TIMED_OUT;
	for (size_t index = 0; index < count; index++) {
		short events = 0;

		if (FD_ISSET(items[index].socket, &read_set))
			events |= POLLIN;
		if (FD_ISSET(items[index].socket, &write_set))
			events |= POLLOUT;
		if (FD_ISSET(items[index].socket, &except_set))
			events |= POLLPRI;
		socket_poll_events(&items[index], events);
	}
	return 0;
}

int
socket_internal_wait_many(struct socket_wait_item *items, size_t count,
    int64_t timeout_ms)
{
	struct pollfd *poll_items;
	nfds_t native_count = (nfds_t)count;
	int timeout;
	int result;

	if ((items == NULL && count != 0) || timeout_ms < -1 ||
	    (size_t)native_count != count)
		return IO_ERROR_INVALID_ARGUMENT;
	for (size_t index = 0; index < count; index++) {
		if ((items[index].interests & SOCKET_WAIT_URGENT) != 0)
			return socket_select_wait_many(items, count, timeout_ms);
	}
	if (timeout_ms > INT_MAX)
		timeout = INT_MAX;
	else
		timeout = (int)timeout_ms;
	poll_items = count == 0 ? NULL : calloc(count, sizeof(*poll_items));
	if (count != 0 && poll_items == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	for (size_t index = 0; index < count; index++) {
		items[index].events = 0;
		items[index].error = 0;
		poll_items[index].fd = items[index].socket;
		poll_items[index].events = 0;
		if ((items[index].interests & SOCKET_WAIT_READABLE) != 0)
			poll_items[index].events |= POLLIN;
		if ((items[index].interests & SOCKET_WAIT_WRITABLE) != 0)
			poll_items[index].events |= POLLOUT;
		if ((items[index].interests & SOCKET_WAIT_URGENT) != 0)
			poll_items[index].events |= POLLPRI;
	}
	result = poll(poll_items, native_count, timeout);
	if (result < 0) {
		int error = socket_internal_map_error(errno);
		free(poll_items);
		return error;
	}
	if (result == 0) {
		free(poll_items);
		return IO_ERROR_TIMED_OUT;
	}
	for (size_t index = 0; index < count; index++)
		socket_poll_events(&items[index], poll_items[index].revents);
	free(poll_items);
	return 0;
}

int
socket_wait(socket_handle_t socket, unsigned interests,
    int64_t timeout_ms, unsigned *events)
{
	struct socket_wait_item item = {socket, interests, 0, 0};
	int64_t deadline = INT64_MAX;
	int error;

	if (events == NULL || interests == 0 ||
	    (interests & ~(SOCKET_WAIT_READABLE | SOCKET_WAIT_WRITABLE |
	    SOCKET_WAIT_URGENT)) != 0)
		return IO_ERROR_INVALID_ARGUMENT;
	*events = 0;
	if (timeout_ms >= 0) {
		int64_t now;
		error = socket_internal_now_ms(&now);
		if (error != 0)
			return error;
		deadline = now > INT64_MAX - timeout_ms ?
		    INT64_MAX : now + timeout_ms;
	}
	for (;;) {
		int64_t wait = timeout_ms;
		if (timeout_ms >= 0) {
			int64_t now;
			error = socket_internal_now_ms(&now);
			if (error != 0)
				return error;
			wait = deadline > now ? deadline - now : 0;
		}
		error = socket_internal_wait_many(&item, 1, wait);
		if (error != IO_ERROR_TIMED_OUT || wait <= INT_MAX) {
			if (error != 0)
				return error;
			*events = item.events;
			return item.error;
		}
	}
}

int
socket_internal_start_connect(const void *address, size_t address_length,
    int family, socket_handle_t *result, bool *complete)
{
	socket_handle_t descriptor;
	socklen_t native_length = (socklen_t)address_length;
	int error;

	if (address == NULL || result == NULL || complete == NULL ||
	    (size_t)native_length != address_length)
		return IO_ERROR_INVALID_ARGUMENT;
	descriptor = socket(family, SOCK_STREAM, IPPROTO_TCP);
	if (!socket_handle_is_valid(descriptor))
		return socket_internal_map_error(errno);
#if SYNCTERM_SOCKET_HAVE_SO_NOSIGPIPE
	{
		int enabled = 1;
		if (setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE, &enabled,
		    (socklen_t)sizeof(enabled)) != 0) {
			error = socket_internal_map_error(errno);
			(void)close(descriptor);
			return error;
		}
	}
#endif
	error = socket_set_blocking(descriptor, false);
	if (error != 0) {
		(void)close(descriptor);
		return error;
	}
	if (connect(descriptor, address, native_length) == 0) {
		*result = descriptor;
		*complete = true;
		return 0;
	}
	error = socket_internal_map_error(errno);
	if (error != IO_ERROR_IN_PROGRESS && error != IO_ERROR_WOULD_BLOCK) {
		(void)close(descriptor);
		return error;
	}
	*result = descriptor;
	*complete = false;
	return 0;
}

int
socket_internal_connect_result(socket_handle_t socket)
{
	int error = 0;
	socklen_t size = (socklen_t)sizeof(error);

	if (getsockopt(socket, SOL_SOCKET, SO_ERROR, &error, &size) != 0)
		return socket_internal_map_error(errno);
	return socket_internal_map_error(error);
}
