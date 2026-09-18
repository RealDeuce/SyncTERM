/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "tcp_client.h"

#include "io_error.h"
#include "socket_internal.h"
#include "tcp_client_internal.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TCP_CLIENT_ATTEMPT_DELAY_MS 250
#define TCP_CLIENT_CANCEL_POLL_MS 50

struct tcp_client_attempt {
	socket_handle_t socket;
	size_t address_index;
	bool active;
};

static int
tcp_client_error_rank(int error)
{
	switch (error) {
		case IO_ERROR_CONNECTION_REFUSED:
		case IO_ERROR_CONNECTION_RESET:
		case IO_ERROR_CONNECTION_ABORTED:
		case IO_ERROR_NETWORK_UNREACHABLE:
		case IO_ERROR_HOST_UNREACHABLE:
		case IO_ERROR_ADDRESS_NOT_AVAILABLE:
		case IO_ERROR_PERMISSION_DENIED:
			return 3;
		case IO_ERROR_TIMED_OUT:
		case IO_ERROR_PROTOCOL:
		case IO_ERROR_NOT_SUPPORTED:
			return 2;
		default:
			return 1;
	}
}

static void
tcp_client_remember_error(int *saved, int error)
{
	if (error != 0 && (*saved == 0 ||
	    tcp_client_error_rank(error) > tcp_client_error_rank(*saved)))
		*saved = error;
}

static void
tcp_client_close_attempts(struct tcp_client_attempt *attempts, size_t count,
    socket_handle_t except)
{
	for (size_t index = 0; index < count; index++) {
		if (attempts[index].active && attempts[index].socket != except) {
			(void)socket_close(attempts[index].socket);
			attempts[index].active = false;
		}
	}
}

static int
tcp_client_interleave(struct tcp_client_address_list *addresses)
{
	struct tcp_client_address *ordered;
	enum tcp_client_address_family wanted;
	size_t used = 0;

	if (addresses->count < 2)
		return 0;
	ordered = calloc(addresses->count, sizeof(*ordered));
	if (ordered == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	wanted = addresses->items[0].family;
	while (used < addresses->count) {
		bool found = false;
		for (size_t index = 0; index < addresses->count; index++) {
			if (addresses->items[index].length != 0 &&
			    addresses->items[index].family == wanted) {
				ordered[used++] = addresses->items[index];
				addresses->items[index].length = 0;
				found = true;
				break;
			}
		}
		if (!found) {
			for (size_t index = 0; index < addresses->count; index++) {
				if (addresses->items[index].length != 0) {
					ordered[used++] = addresses->items[index];
					addresses->items[index].length = 0;
					break;
				}
			}
		}
		wanted = wanted == TCP_CLIENT_ADDRESS_IPV4 ?
		    TCP_CLIENT_ADDRESS_IPV6 : TCP_CLIENT_ADDRESS_IPV4;
	}
	free(addresses->items);
	addresses->items = ordered;
	return 0;
}

static int64_t
tcp_client_wait_time(int64_t now, int64_t deadline, int64_t next_attempt,
    bool has_next, bool has_cancel)
{
	int64_t wait = INT64_MAX;

	if (deadline != INT64_MAX)
		wait = deadline > now ? deadline - now : 0;
	if (has_next && next_attempt - now < wait)
		wait = next_attempt > now ? next_attempt - now : 0;
	if (has_cancel && wait > TCP_CLIENT_CANCEL_POLL_MS)
		wait = TCP_CLIENT_CANCEL_POLL_MS;
	return wait == INT64_MAX ? -1 : wait;
}

static int
tcp_client_finish(socket_handle_t candidate,
    struct tcp_client_attempt *attempts, size_t count,
    const struct tcp_client_address_list *addresses,
    size_t address_index, socket_handle_t *result,
    enum tcp_client_address_family *connected_family)
{
	int error = socket_set_blocking(candidate, true);

	if (error != 0)
		return error;
	tcp_client_close_attempts(attempts, count, candidate);
	*result = candidate;
	if (connected_family != NULL)
		*connected_family = addresses->items[address_index].family;
	return 0;
}

int
tcp_client_connect(socket_handle_t *result,
    enum tcp_client_address_family *connected_family, const char *host,
    uint16_t port, const struct tcp_client_options *options)
{
	struct tcp_client_address_list addresses = {0};
	struct tcp_client_attempt *attempts = NULL;
	struct socket_wait_item *wait_items = NULL;
	size_t *wait_indices = NULL;
	size_t next_address = 0;
	size_t active = 0;
	size_t capacity;
	int64_t now;
	int64_t deadline;
	int64_t next_attempt;
	int saved_error = 0;
	int error;

	if (result == NULL || host == NULL || *host == '\0' || options == NULL ||
	    options->timeout_ms < -1 ||
	    (options->family != TCP_CLIENT_ADDRESS_ANY &&
	    options->family != TCP_CLIENT_ADDRESS_IPV4 &&
	    options->family != TCP_CLIENT_ADDRESS_IPV6))
		return IO_ERROR_INVALID_ARGUMENT;
	error = tcp_client_resolve(&addresses, host, port, options->family);
	if (error != 0)
		return error;
	if (addresses.count == 0) {
		error = IO_ERROR_NOT_FOUND;
		goto done;
	}
	error = tcp_client_interleave(&addresses);
	if (error != 0)
		goto done;
	attempts = calloc(addresses.count, sizeof(*attempts));
	wait_items = calloc(addresses.count, sizeof(*wait_items));
	wait_indices = calloc(addresses.count, sizeof(*wait_indices));
	if (attempts == NULL || wait_items == NULL || wait_indices == NULL) {
		error = IO_ERROR_OUT_OF_MEMORY;
		goto done;
	}
	for (size_t index = 0; index < addresses.count; index++)
		attempts[index].socket = SOCKET_HANDLE_INVALID;
	capacity = socket_internal_wait_capacity();
	if (capacity == 0) {
		error = IO_ERROR_NOT_SUPPORTED;
		goto done;
	}
	error = socket_internal_now_ms(&now);
	if (error != 0)
		goto done;
	if (options->timeout_ms < 0 || now > INT64_MAX - options->timeout_ms)
		deadline = INT64_MAX;
	else
		deadline = now + options->timeout_ms;
	next_attempt = now;

	for (;;) {
		size_t wait_count = 0;

		if (options->cancel != NULL &&
		    options->cancel(options->cancel_context)) {
			error = IO_ERROR_CANCELLED;
			goto done;
		}
		error = socket_internal_now_ms(&now);
		if (error != 0)
			goto done;
		if (now >= deadline) {
			error = IO_ERROR_TIMED_OUT;
			goto done;
		}
		if (next_address < addresses.count && active == 0)
			next_attempt = now;
		if (next_address < addresses.count && active < capacity &&
		    now >= next_attempt) {
			bool complete;
			socket_handle_t candidate = SOCKET_HANDLE_INVALID;
			size_t index = next_address++;

			error = socket_internal_start_connect(
			    addresses.items[index].storage,
			    addresses.items[index].length,
			    addresses.items[index].native_family, &candidate,
			    &complete);
			if (error != 0)
				tcp_client_remember_error(&saved_error, error);
			else if (complete) {
				error = tcp_client_finish(candidate, attempts,
				    addresses.count, &addresses, index, result,
				    connected_family);
				if (error == 0)
					goto done;
				(void)socket_close(candidate);
				tcp_client_remember_error(&saved_error, error);
			}
			else {
				attempts[index].socket = candidate;
				attempts[index].address_index = index;
				attempts[index].active = true;
				active++;
			}
			next_attempt = now + TCP_CLIENT_ATTEMPT_DELAY_MS;
			continue;
		}
		if (active == 0 && next_address == addresses.count) {
			error = saved_error != 0 ? saved_error : IO_ERROR_IO;
			goto done;
		}
		for (size_t index = 0; index < addresses.count; index++) {
			if (!attempts[index].active)
				continue;
			wait_items[wait_count].socket = attempts[index].socket;
			wait_items[wait_count].interests = SOCKET_WAIT_WRITABLE;
			wait_indices[wait_count] = index;
			wait_count++;
		}
		error = socket_internal_wait_many(wait_items, wait_count,
		    tcp_client_wait_time(now, deadline, next_attempt,
		    next_address < addresses.count && active < capacity,
		    options->cancel != NULL));
		if (error == IO_ERROR_TIMED_OUT || error == IO_ERROR_INTERRUPTED)
			continue;
		if (error != 0)
			goto done;
		for (size_t wait_index = 0; wait_index < wait_count; wait_index++) {
			size_t index = wait_indices[wait_index];
			int connect_error;

			if (wait_items[wait_index].events == 0)
				continue;
			connect_error = socket_internal_connect_result(
			    attempts[index].socket);
			if (connect_error == IO_ERROR_IN_PROGRESS ||
			    connect_error == IO_ERROR_WOULD_BLOCK)
				continue;
			if (connect_error == 0) {
				error = tcp_client_finish(attempts[index].socket,
				    attempts, addresses.count, &addresses,
				    attempts[index].address_index, result,
				    connected_family);
				if (error == 0)
					goto done;
				tcp_client_remember_error(&saved_error, error);
			}
			else
				tcp_client_remember_error(&saved_error, connect_error);
			(void)socket_close(attempts[index].socket);
			attempts[index].active = false;
			active--;
		}
	}

done:
	if (attempts != NULL)
		tcp_client_close_attempts(attempts, addresses.count,
		    error == 0 ? *result : SOCKET_HANDLE_INVALID);
	free(wait_indices);
	free(wait_items);
	free(attempts);
	tcp_client_free_addresses(&addresses);
	return error;
}
