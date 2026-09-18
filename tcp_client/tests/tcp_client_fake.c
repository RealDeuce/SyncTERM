/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "tcp_client_fake.h"

#include "io_error.h"
#include "socket_internal.h"
#include "tcp_client_internal.h"

#include <stdlib.h>
#include <string.h>

#define TCP_CLIENT_FAKE_MAX_ADDRESSES 8
#define TCP_CLIENT_FAKE_HANDLE_BASE 100

static struct tcp_client_fake_address fake_addresses[
    TCP_CLIENT_FAKE_MAX_ADDRESSES];
static size_t fake_address_count;
static size_t fake_started[TCP_CLIENT_FAKE_MAX_ADDRESSES];
static int64_t fake_start_times[TCP_CLIENT_FAKE_MAX_ADDRESSES];
static size_t fake_starts;
static int64_t fake_time;
static size_t fake_closes;

void
tcp_client_fake_reset(const struct tcp_client_fake_address *addresses,
    size_t count)
{
	if (count > TCP_CLIENT_FAKE_MAX_ADDRESSES)
		count = TCP_CLIENT_FAKE_MAX_ADDRESSES;
	memcpy(fake_addresses, addresses, count * sizeof(*addresses));
	fake_address_count = count;
	fake_starts = 0;
	fake_time = 0;
	fake_closes = 0;
}

size_t
tcp_client_fake_start_count(void)
{
	return fake_starts;
}

size_t
tcp_client_fake_started_address(size_t index)
{
	return fake_started[index];
}

int64_t
tcp_client_fake_start_time(size_t index)
{
	return fake_start_times[index];
}

int64_t
tcp_client_fake_now(void)
{
	return fake_time;
}

size_t
tcp_client_fake_close_count(void)
{
	return fake_closes;
}

int
tcp_client_resolve(struct tcp_client_address_list *result,
    const char *host, uint16_t port,
    enum tcp_client_address_family family)
{
	(void)host;
	(void)port;
	(void)family;
	result->items = calloc(fake_address_count, sizeof(*result->items));
	if (result->items == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	result->count = fake_address_count;
	for (size_t index = 0; index < fake_address_count; index++) {
		result->items[index].family = fake_addresses[index].family;
		result->items[index].native_family = (int)index + 1;
		result->items[index].length = 1;
		result->items[index].storage[0] = (unsigned char)index;
	}
	return 0;
}

void
tcp_client_free_addresses(struct tcp_client_address_list *addresses)
{
	free(addresses->items);
	addresses->items = NULL;
	addresses->count = 0;
}

int
socket_internal_now_ms(int64_t *result)
{
	*result = fake_time;
	return 0;
}

size_t
socket_internal_wait_capacity(void)
{
	return TCP_CLIENT_FAKE_MAX_ADDRESSES;
}

int
socket_internal_start_connect(const void *address, size_t address_length,
    int family, socket_handle_t *result, bool *complete)
{
	size_t index;

	(void)family;
	if (address_length != 1)
		return IO_ERROR_INVALID_ARGUMENT;
	index = *(const unsigned char *)address;
	fake_started[fake_starts] = index;
	fake_start_times[fake_starts++] = fake_time;
	if (fake_addresses[index].behavior == TCP_CLIENT_FAKE_FAIL_START)
		return fake_addresses[index].error;
	*result = (socket_handle_t)(TCP_CLIENT_FAKE_HANDLE_BASE + index);
	*complete = fake_addresses[index].behavior ==
	    TCP_CLIENT_FAKE_IMMEDIATE_SUCCESS;
	return 0;
}

int
socket_internal_wait_many(struct socket_wait_item *items, size_t count,
    int64_t timeout_ms)
{
	int64_t wake = timeout_ms < 0 ? INT64_MAX : fake_time + timeout_ms;
	bool ready = false;

	for (size_t index = 0; index < count; index++) {
		size_t address = (size_t)items[index].socket -
		    TCP_CLIENT_FAKE_HANDLE_BASE;
		items[index].events = 0;
		items[index].error = 0;
		if (fake_addresses[address].behavior != TCP_CLIENT_FAKE_NEVER &&
		    fake_addresses[address].ready_at < wake)
			wake = fake_addresses[address].ready_at;
	}
	if (wake == INT64_MAX)
		return IO_ERROR_IO;
	fake_time = wake;
	for (size_t index = 0; index < count; index++) {
		size_t address = (size_t)items[index].socket -
		    TCP_CLIENT_FAKE_HANDLE_BASE;
		if (fake_addresses[address].behavior != TCP_CLIENT_FAKE_NEVER &&
		    fake_addresses[address].ready_at <= fake_time) {
			items[index].events = SOCKET_WAIT_WRITABLE;
			ready = true;
		}
	}
	return ready ? 0 : IO_ERROR_TIMED_OUT;
}

int
socket_internal_connect_result(socket_handle_t socket)
{
	size_t address = (size_t)socket - TCP_CLIENT_FAKE_HANDLE_BASE;

	if (fake_addresses[address].behavior == TCP_CLIENT_FAKE_PENDING_SUCCESS)
		return 0;
	return fake_addresses[address].error;
}

int
socket_set_blocking(socket_handle_t socket, bool blocking)
{
	(void)socket;
	return blocking ? 0 : IO_ERROR_INVALID_ARGUMENT;
}

int
socket_close(socket_handle_t socket)
{
	(void)socket;
	fake_closes++;
	return 0;
}
