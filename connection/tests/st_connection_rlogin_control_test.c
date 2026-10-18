/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "connection_internal.h"
#include "connection_tcp.h"
#include "socket.h"
#include "io_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t normal_size, prefix_size, read_position, urgent_ready_after;
static uint8_t scripted_control;
static bool control_consumed, fail_read;

static ssize_t
scripted_receive(socket_handle_t socket, void *data, size_t size, unsigned flags)
{
	(void)socket;
	if (flags == SOCKET_RECEIVE_URGENT) {
		if (control_consumed)
			return IO_ERROR_INVALID_ARGUMENT;
		if (read_position < urgent_ready_after)
			return IO_ERROR_WOULD_BLOCK;
		*(uint8_t *)data = scripted_control;
		control_consumed = true;
		return 1;
	}
	if (fail_read)
		return IO_ERROR_IO;
	size_t end = read_position < prefix_size ? prefix_size : normal_size;
	if (end > normal_size) end = normal_size;
	size_t count = end - read_position;
	if (count > size) count = size;
	if (count > 1024) count = 1024;
	for (size_t i = 0; i < count; ++i)
		((uint8_t *)data)[i] = (uint8_t)((read_position + i) % 251);
	if ((flags & SOCKET_RECEIVE_PEEK) == 0)
		read_position += count;
	return (ssize_t)count;
}

static int
scripted_mark(socket_handle_t socket, bool *at_mark)
{
	(void)socket;
	*at_mark = read_position == prefix_size;
	return 0;
}

static int
scripted_wait(socket_handle_t socket, unsigned interests, int64_t timeout,
	unsigned *events)
{
	(void)socket;
	(void)interests;
	(void)timeout;
	*events = read_position < normal_size ? SOCKET_WAIT_READABLE : SOCKET_WAIT_CLOSED;
	return 0;
}

/* Model an urgent pointer arriving before its byte, independently of native
 * receive-window sizes and TCP scheduling. All policy under test is real. */
#define socket_receive scripted_receive
#define socket_at_urgent_mark scripted_mark
#define socket_wait scripted_wait
#define st_connection_connect_raw test_connect_raw
#define st_connection_connect_rlogin test_connect_rlogin
#define st_connection_tcp_diagnostic test_tcp_diagnostic
#include "../connection_network.c"
#undef socket_receive
#undef socket_at_urgent_mark
#undef socket_wait

#define CHECK(e) do { if (!(e)) { \
	fprintf(stderr, "RLogin control check failed: %s:%d: %s\n", __FILE__, __LINE__, #e); \
	return 1; \
} } while (0)

static int
check_case(unsigned scenario)
{
	static const struct st_connection_transport_ops operations = {0};
	st_connection_t connection = {.operations = &operations};
	struct network_transport transport = {.connection = &connection, .urgent_pending = true};
	uint8_t bytes[1024];
	bool handled, discard;
	size_t delivered = 0;
	CHECK(st_mutex_init(&connection.input.mutex, ST_MUTEX_PLAIN) == ST_THREAD_SUCCESS);
	CHECK(st_condition_init(&connection.input.changed) == ST_THREAD_SUCCESS);
	atomic_init(&connection.binary_mode, false);
	connection.input.paused = true;
	connection.input.count = 4;
	normal_size = 12288;
	prefix_size = 8192;
	urgent_ready_after = 4096;
	read_position = 0;
	control_consumed = false;
	fail_read = scenario == 4;
	scripted_control = scenario == 0 ? 0x01 : scenario == 1 ? 0x10 : 0x02;
	if (scenario == 3) {
		normal_size = prefix_size = urgent_ready_after = 2 * PENDING_URGENT_INPUT_LIMIT;
		scripted_control = 0x01;
	} else if (scenario == 5) {
		normal_size = 8192;
		prefix_size = normal_size + 1;
		urgent_ready_after = normal_size + 2;
	} else if (scenario == 6) {
		urgent_ready_after = prefix_size + 1;
		scripted_control = 0x01;
	}
	CHECK(process_urgent(&transport, &handled, &discard) == IO_ERROR_WOULD_BLOCK);
	CHECK(!handled && !discard);
	int result = buffer_pending_urgent(&transport);
	if (scenario == 4) {
		CHECK(result == IO_ERROR_IO && transport.pending_input_size == 0);
	} else if (scenario == 3) {
		CHECK(result == ST_CONNECTION_INPUT_CONTROL_NONE && !control_consumed);
		CHECK(transport.pending_input_size == PENDING_URGENT_INPUT_LIMIT);
		CHECK(buffer_pending_urgent(&transport) == ST_CONNECTION_INPUT_CONTROL_NONE);
		CHECK(read_position == PENDING_URGENT_INPUT_LIMIT);
	} else {
		CHECK(control_consumed == (scenario != 5 && scenario != 6));
		if (scenario == 5) {
			CHECK(result == ST_CONNECTION_INPUT_CONTROL_EOF);
			CHECK(transport.pending_input_size == normal_size);
		} else if (scenario == 6) {
			CHECK(result == ST_CONNECTION_INPUT_CONTROL_NONE);
			CHECK(transport.pending_input_size == prefix_size);
		} else if (scenario == 2) {
			CHECK(result == ST_CONNECTION_INPUT_CONTROL_DISCARD);
			CHECK(connection.input.count == 0 && transport.pending_input_size == 0);
			CHECK(read_position == prefix_size);
			delivered = prefix_size;
		} else {
			CHECK(result == ST_CONNECTION_INPUT_CONTROL_NONE);
			CHECK(transport.pending_input_size == urgent_ready_after);
			CHECK(connection.input.count == 4);
			CHECK(atomic_load(&connection.binary_mode) == (scenario == 1));
			CHECK(connection.input.paused == (scenario != 1));
		}
		while (delivered < normal_size) {
			if (scenario == 6 && delivered == prefix_size) {
				CHECK(network_read(&transport, bytes, sizeof(bytes), 0) == IO_ERROR_WOULD_BLOCK);
				CHECK(read_position == prefix_size && !control_consumed);
				urgent_ready_after = prefix_size;
			}
			ssize_t count = network_read(&transport, bytes, sizeof(bytes), 0);
			CHECK(count > 0);
			for (ssize_t i = 0; i < count; ++i)
				CHECK(bytes[i] == (uint8_t)((delivered + (size_t)i) % 251));
			delivered += (size_t)count;
		}
		CHECK(network_read(&transport, bytes, sizeof(bytes), 0) == 0);
	}
	free(transport.pending_input);
	st_condition_destroy(&connection.input.changed);
	st_mutex_destroy(&connection.input.mutex);
	return 0;
}

int
main(void)
{
	for (unsigned scenario = 0; scenario < 7; ++scenario)
		CHECK(check_case(scenario) == 0);
	return 0;
}
