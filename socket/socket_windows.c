/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "socket_internal.h"

#include "io_error.h"
#include "st_time.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <ws2tcpip.h>

int
socket_internal_map_error(int error)
{
	switch (error) {
		case 0:
			return 0;
		case WSAEACCES:
			return IO_ERROR_PERMISSION_DENIED;
		case WSA_NOT_ENOUGH_MEMORY:
		case WSAENOBUFS:
			return IO_ERROR_OUT_OF_MEMORY;
		case WSAEINTR:
			return IO_ERROR_INTERRUPTED;
		case WSAEWOULDBLOCK:
			return IO_ERROR_WOULD_BLOCK;
		case WSAEINPROGRESS:
		case WSAEALREADY:
			return IO_ERROR_IN_PROGRESS;
		case WSAETIMEDOUT:
			return IO_ERROR_TIMED_OUT;
		case WSAECONNREFUSED:
			return IO_ERROR_CONNECTION_REFUSED;
		case WSAECONNRESET:
		case WSAESHUTDOWN:
			return IO_ERROR_CONNECTION_RESET;
		case WSAECONNABORTED:
			return IO_ERROR_CONNECTION_ABORTED;
		case WSAENOTCONN:
			return IO_ERROR_NOT_CONNECTED;
		case WSAENETUNREACH:
			return IO_ERROR_NETWORK_UNREACHABLE;
		case WSAEHOSTUNREACH:
			return IO_ERROR_HOST_UNREACHABLE;
		case WSAEADDRINUSE:
			return IO_ERROR_ADDRESS_IN_USE;
		case WSAEADDRNOTAVAIL:
			return IO_ERROR_ADDRESS_NOT_AVAILABLE;
		case WSAEMSGSIZE:
			return IO_ERROR_MESSAGE_TOO_LARGE;
		case WSAEPROTONOSUPPORT:
		case WSAEPROTOTYPE:
		case WSAEAFNOSUPPORT:
			return IO_ERROR_PROTOCOL;
		case WSAEINVAL:
		case WSAEBADF:
		case WSAENOTSOCK:
		case WSAEDESTADDRREQ:
			return IO_ERROR_INVALID_ARGUMENT;
		default:
			return IO_ERROR_IO;
	}
}

int
socket_runtime_init(void)
{
	WSADATA data;
	int result = WSAStartup(MAKEWORD(2, 2), &data);

	if (result != 0)
		return socket_internal_map_error(result);
	if (LOBYTE(data.wVersion) != 2 || HIBYTE(data.wVersion) != 2) {
		(void)WSACleanup();
		return IO_ERROR_NOT_SUPPORTED;
	}
	return 0;
}

int
socket_runtime_cleanup(void)
{
	if (WSACleanup() == 0)
		return 0;
	return socket_internal_map_error(WSAGetLastError());
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
	if (closesocket(socket) == 0)
		return 0;
	return socket_internal_map_error(WSAGetLastError());
}

int
socket_shutdown(socket_handle_t socket,
    enum socket_shutdown_direction direction)
{
	int how;

	switch (direction) {
		case SOCKET_SHUTDOWN_READ:
			how = SD_RECEIVE;
			break;
		case SOCKET_SHUTDOWN_WRITE:
			how = SD_SEND;
			break;
		case SOCKET_SHUTDOWN_BOTH:
			how = SD_BOTH;
			break;
		default:
			return IO_ERROR_INVALID_ARGUMENT;
	}
	if (shutdown(socket, how) == 0)
		return 0;
	return socket_internal_map_error(WSAGetLastError());
}

int
socket_set_blocking(socket_handle_t socket, bool blocking)
{
	u_long mode = blocking ? 0 : 1;

	if (ioctlsocket(socket, FIONBIO, &mode) == 0)
		return 0;
	return socket_internal_map_error(WSAGetLastError());
}

static int
socket_set_boolean_option(socket_handle_t socket, int level, int option,
    bool enabled)
{
	BOOL value = enabled ? TRUE : FALSE;

	if (setsockopt(socket, level, option, (const char *)&value,
	    (int)sizeof(value)) == 0)
		return 0;
	return socket_internal_map_error(WSAGetLastError());
}

int
socket_set_keepalive(socket_handle_t socket, bool enabled)
{
	return socket_set_boolean_option(socket, SOL_SOCKET, SO_KEEPALIVE,
	    enabled);
}

int
socket_set_no_delay(socket_handle_t socket, bool enabled)
{
	return socket_set_boolean_option(socket, IPPROTO_TCP, TCP_NODELAY,
	    enabled);
}

static int
socket_set_buffer_size(socket_handle_t socket, int option, size_t size)
{
	int value;

	if (size > INT_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	value = (int)size;
	if (setsockopt(socket, SOL_SOCKET, option, (const char *)&value,
	    (int)sizeof(value)) == 0)
		return 0;
	return socket_internal_map_error(WSAGetLastError());
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
	int result;

	if (buffer == NULL && size != 0)
		return IO_ERROR_INVALID_ARGUMENT;
	if (size > INT_MAX)
		size = INT_MAX;
	result = send(socket, buffer, (int)size, 0);
	if (result >= 0)
		return (ssize_t)result;
	return socket_internal_map_error(WSAGetLastError());
}

ssize_t
socket_receive(socket_handle_t socket, void *buffer, size_t size,
    unsigned flags)
{
	int native_flags = 0;
	int result;

	if ((buffer == NULL && size != 0) ||
	    (flags & ~(SOCKET_RECEIVE_PEEK | SOCKET_RECEIVE_URGENT)) != 0)
		return IO_ERROR_INVALID_ARGUMENT;
	/* Keep the receive contract identical to the POSIX implementation. */
	if (flags == (SOCKET_RECEIVE_PEEK | SOCKET_RECEIVE_URGENT))
		return IO_ERROR_INVALID_ARGUMENT;
	if (size > INT_MAX)
		size = INT_MAX;
	if ((flags & SOCKET_RECEIVE_PEEK) != 0)
		native_flags |= MSG_PEEK;
	if ((flags & SOCKET_RECEIVE_URGENT) != 0)
		native_flags |= MSG_OOB;
	result = recv(socket, buffer, (int)size, native_flags);
	if (result >= 0)
		return (ssize_t)result;
	return socket_internal_map_error(WSAGetLastError());
}

int
socket_at_urgent_mark(socket_handle_t socket, bool *at_mark)
{
	u_long value;

	if (at_mark == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (ioctlsocket(socket, SIOCATMARK, &value) != 0)
		return socket_internal_map_error(WSAGetLastError());
	*at_mark = value != 0;
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
	return FD_SETSIZE;
}

int
socket_internal_wait_many(struct socket_wait_item *items, size_t count,
    int64_t timeout_ms)
{
	fd_set read_set;
	fd_set write_set;
	fd_set except_set;
	struct timeval timeout;
	struct timeval *timeout_pointer = NULL;
	int result;

	if ((items == NULL && count != 0) || count > FD_SETSIZE ||
	    timeout_ms < -1)
		return IO_ERROR_INVALID_ARGUMENT;
	FD_ZERO(&read_set);
	FD_ZERO(&write_set);
	FD_ZERO(&except_set);
	for (size_t index = 0; index < count; index++) {
		items[index].events = 0;
		items[index].error = 0;
		if ((items[index].interests & SOCKET_WAIT_READABLE) != 0)
			FD_SET(items[index].socket, &read_set);
		if ((items[index].interests & SOCKET_WAIT_WRITABLE) != 0)
			FD_SET(items[index].socket, &write_set);
		FD_SET(items[index].socket, &except_set);
	}
	if (timeout_ms >= 0) {
		if (timeout_ms > INT_MAX)
			timeout_ms = INT_MAX;
		timeout.tv_sec = (long)(timeout_ms / 1000);
		timeout.tv_usec = (long)((timeout_ms % 1000) * 1000);
		timeout_pointer = &timeout;
	}
	result = select(0, &read_set, &write_set, &except_set,
	    timeout_pointer);
	if (result == SOCKET_ERROR)
		return socket_internal_map_error(WSAGetLastError());
	if (result == 0)
		return IO_ERROR_TIMED_OUT;
	for (size_t index = 0; index < count; index++) {
		char byte;
		int received;
		if (FD_ISSET(items[index].socket, &write_set))
			items[index].events |= SOCKET_WAIT_WRITABLE;
		if (FD_ISSET(items[index].socket, &read_set)) {
			received = recv(items[index].socket, &byte, 1, MSG_PEEK);
			if (received > 0 &&
			    (items[index].interests & SOCKET_WAIT_READABLE) != 0)
				items[index].events |= SOCKET_WAIT_READABLE;
			else if (received == 0)
				items[index].events |= SOCKET_WAIT_CLOSED;
			else if (received == SOCKET_ERROR) {
				items[index].events |= SOCKET_WAIT_ERROR;
				items[index].error = socket_internal_map_error(
				    WSAGetLastError());
			}
		}
		if (FD_ISSET(items[index].socket, &except_set)) {
			int socket_error = socket_internal_connect_result(
			    items[index].socket);
			if (socket_error != 0) {
				items[index].events |= SOCKET_WAIT_ERROR;
				items[index].error = socket_error;
			}
			else if ((items[index].interests & SOCKET_WAIT_URGENT) != 0)
				items[index].events |= SOCKET_WAIT_URGENT;
		}
	}
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
	int error;

	if (address == NULL || result == NULL || complete == NULL ||
	    address_length > INT_MAX)
		return IO_ERROR_INVALID_ARGUMENT;
	descriptor = socket(family, SOCK_STREAM, IPPROTO_TCP);
	if (!socket_handle_is_valid(descriptor))
		return socket_internal_map_error(WSAGetLastError());
	error = socket_set_blocking(descriptor, false);
	if (error != 0) {
		(void)closesocket(descriptor);
		return error;
	}
	if (connect(descriptor, address, (int)address_length) == 0) {
		*result = descriptor;
		*complete = true;
		return 0;
	}
	error = socket_internal_map_error(WSAGetLastError());
	if (error != IO_ERROR_IN_PROGRESS && error != IO_ERROR_WOULD_BLOCK) {
		(void)closesocket(descriptor);
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
	int size = (int)sizeof(error);

	if (getsockopt(socket, SOL_SOCKET, SO_ERROR, (char *)&error, &size) != 0)
		return socket_internal_map_error(WSAGetLastError());
	return socket_internal_map_error(error);
}
