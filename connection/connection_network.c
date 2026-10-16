/* Raw TCP and RFC 1282 RLogin transports. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "connection_internal.h"
#include "connection_tcp.h"

#include "io_error.h"
#include "socket.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
st_connection_tcp_diagnostic(void *context, enum tcp_client_event event,
	enum tcp_client_address_family family, int status)
{
	const struct st_connection_hooks *hooks = context;
	const char *name = family == TCP_CLIENT_ADDRESS_IPV4 ? "IPv4"
	    : family == TCP_CLIENT_ADDRESS_IPV6 ? "IPv6" : "any";
	if (event == TCP_CLIENT_RESOLVING)
		st_connection_log_hooks(hooks, "TCP", ST_CONNECTION_LOG_DEBUG,
		    "resolving remote endpoint family=%s", name);
	else if (event == TCP_CLIENT_ATTEMPT)
		st_connection_log_hooks(hooks, "TCP", ST_CONNECTION_LOG_DEBUG,
		    "trying address family=%s", name);
	else if (event == TCP_CLIENT_ATTEMPT_FAILED)
		st_connection_log_hooks(hooks, "TCP", ST_CONNECTION_LOG_WARNING,
		    "address attempt failed family=%s error=%d", name, status);
	else if (event == TCP_CLIENT_CONNECTED)
		st_connection_log_hooks(hooks, "TCP", ST_CONNECTION_LOG_INFO,
		    "connection established family=%s", name);
	else
		st_connection_log_hooks(hooks, "TCP", status == IO_ERROR_CANCELLED
		    ? ST_CONNECTION_LOG_INFO : ST_CONNECTION_LOG_ERROR,
		    "connection setup failed (%d)", status);
}

struct network_transport {
	st_connection_t *connection;
	st_mutex_t mutex;
	socket_handle_t socket;
	struct st_screen_geometry geometry;
	bool rlogin;
	bool window_enabled;
	bool urgent_pending;
	bool urgent_control_known;
	bool urgent_control_consumed;
	bool urgent_flush_pending;
	uint8_t urgent_control;
};

struct connect_cancel_context {
	const struct st_connection_hooks *hooks;
};

static bool
cancel_connect(void *context)
{
	const struct connect_cancel_context *cancel = context;

	return st_connection_cancelled(cancel->hooks,
	    ST_CONNECTION_SETUP_CONNECTING);
}

static void
network_bind(void *state, st_connection_t *connection)
{
	struct network_transport *transport = state;

	transport->connection = connection;
}

static int
send_window(struct network_transport *transport,
	const struct st_screen_geometry *geometry)
{
	uint8_t packet[12];
	uint16_t columns;
	uint16_t rows;
	uint16_t width;
	uint16_t height;
	ssize_t result;

	if (geometry->columns > UINT16_MAX || geometry->rows > UINT16_MAX
	    || geometry->pixel_width > UINT16_MAX
	    || geometry->pixel_height > UINT16_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	columns = (uint16_t)geometry->columns;
	rows = (uint16_t)geometry->rows;
	width = (uint16_t)geometry->pixel_width;
	height = (uint16_t)geometry->pixel_height;
	packet[0] = 0xff;
	packet[1] = 0xff;
	packet[2] = 's';
	packet[3] = 's';
	packet[4] = (uint8_t)(rows >> 8);
	packet[5] = (uint8_t)rows;
	packet[6] = (uint8_t)(columns >> 8);
	packet[7] = (uint8_t)columns;
	packet[8] = (uint8_t)(width >> 8);
	packet[9] = (uint8_t)width;
	packet[10] = (uint8_t)(height >> 8);
	packet[11] = (uint8_t)height;
	result = st_connection_queue_output_atomic(transport->connection, packet,
	    sizeof(packet), -1);
	if (result == (ssize_t)sizeof(packet)) {
		st_connection_log(transport->connection, ST_CONNECTION_LOG_DEBUG,
		    "sending window size rows=%u columns=%u pixels=%ux%u", rows, columns, width, height);
		return 0;
	}
	return result < 0 ? (int)result : IO_ERROR_IO;
}

static int
handle_urgent(struct network_transport *transport, uint8_t control)
{
	st_connection_t *connection = transport->connection;
	int result = 0;

	st_connection_log(connection, ST_CONNECTION_LOG_DEBUG, "received RFC 1282 urgent control=0x%02X", control);
	switch (control) {
	case 0x02:
		break;
	case 0x10:
		result = st_connection_set_binary_mode(connection, true);
		break;
	case 0x20:
		result = st_connection_set_binary_mode(connection, false);
		break;
	case 0x80:
		(void)st_mutex_lock(&transport->mutex);
		transport->window_enabled = true;
		result = send_window(transport, &transport->geometry);
		(void)st_mutex_unlock(&transport->mutex);
		break;
	default:
		break;
	}
	return result;
}

static int
process_urgent(struct network_transport *transport, bool *handled)
{
	uint8_t control;
	ssize_t received;
	int result;

	*handled = false;
	if (!transport->urgent_control_known) {
		received = socket_receive(transport->socket,
		    &transport->urgent_control, 1,
		    SOCKET_RECEIVE_PEEK | SOCKET_RECEIVE_URGENT);
		if (received == IO_ERROR_INVALID_ARGUMENT) {
			/* Some stacks signal the exceptional condition before an OOB
			 * peek succeeds. Remember a directly consumed control so a
			 * flush can still drain the normal stream to its mark. */
			received = socket_receive(transport->socket,
			    &transport->urgent_control, 1, SOCKET_RECEIVE_URGENT);
			if (received == 1)
				transport->urgent_control_consumed = true;
		}
		if (received == IO_ERROR_WOULD_BLOCK)
			transport->urgent_pending = true;
		if (received < 0)
			return (int)received;
		if (received != 1)
			return IO_ERROR_CONNECTION_RESET;
		transport->urgent_control_known = true;
		transport->urgent_pending = true;
		if (transport->urgent_control == 0x02) {
			st_connection_clear_input(transport->connection);
			transport->urgent_flush_pending = true;
		}
	}
	if (transport->urgent_flush_pending) {
		uint8_t discarded[4096];

		for (;;) {
			bool at_mark;

			result = socket_at_urgent_mark(transport->socket, &at_mark);
			if (result != 0)
				return result;
			if (at_mark)
				break;
			received = socket_receive(transport->socket, discarded,
			    sizeof(discarded), SOCKET_RECEIVE_NONE);
			if (received > 0)
				continue;
			if (received < 0)
				return (int)received;
			return IO_ERROR_CONNECTION_RESET;
		}
	}
	if (transport->urgent_control_consumed)
		control = transport->urgent_control;
	else {
		received = socket_receive(transport->socket, &control, 1,
		    SOCKET_RECEIVE_URGENT);
		if (received == IO_ERROR_INVALID_ARGUMENT)
			return IO_ERROR_WOULD_BLOCK;
		if (received < 0)
			return (int)received;
		if (received != 1)
			return IO_ERROR_CONNECTION_RESET;
	}
	transport->urgent_pending = false;
	transport->urgent_control_known = false;
	transport->urgent_control_consumed = false;
	transport->urgent_flush_pending = false;
	result = handle_urgent(transport, control);
	if (result == 0)
		*handled = true;
	return result;
}

static ssize_t
network_read(void *state, void *data, size_t size, int64_t timeout_ms)
{
	struct network_transport *transport = state;
	unsigned interests = SOCKET_WAIT_READABLE;
	unsigned events;
	bool urgent_handled = false;
	int result;

	if (transport->urgent_pending) {
		result = process_urgent(transport, &urgent_handled);
		if (result != 0 && result != IO_ERROR_WOULD_BLOCK
		    && result != IO_ERROR_INVALID_ARGUMENT)
			return result;
	}
	if (transport->rlogin)
		interests |= SOCKET_WAIT_URGENT;
	result = socket_wait(transport->socket, interests, timeout_ms, &events);
	if (result != 0)
		return result;
	if ((events & SOCKET_WAIT_ERROR) != 0)
		return IO_ERROR_IO;
	if (!urgent_handled && ((events & SOCKET_WAIT_URGENT) != 0
	    || transport->urgent_pending)) {
		result = process_urgent(transport, &urgent_handled);
		if (result == IO_ERROR_INVALID_ARGUMENT) {
			transport->urgent_pending = transport->urgent_pending
			    || (events & SOCKET_WAIT_READABLE) != 0;
			if ((events & SOCKET_WAIT_URGENT) != 0)
				return IO_ERROR_WOULD_BLOCK;
			result = IO_ERROR_WOULD_BLOCK;
		}
		if (result == IO_ERROR_WOULD_BLOCK
		    && transport->urgent_control_known)
			return result;
		if (result != 0 && result != IO_ERROR_WOULD_BLOCK)
			return result;
	}
	if ((events & SOCKET_WAIT_READABLE) != 0 || urgent_handled) {
		ssize_t received = socket_receive(transport->socket, data, size,
		    SOCKET_RECEIVE_NONE);

		if (urgent_handled && (received == 0
		    || received == IO_ERROR_WOULD_BLOCK))
			return IO_ERROR_WOULD_BLOCK;
		return received;
	}
	if ((events & SOCKET_WAIT_CLOSED) != 0)
		return 0;
	return IO_ERROR_WOULD_BLOCK;
}

static ssize_t
network_write(void *state, const void *data, size_t size, int64_t timeout_ms)
{
	struct network_transport *transport = state;
	unsigned events;
	int result = socket_wait(transport->socket, SOCKET_WAIT_WRITABLE,
	    timeout_ms, &events);

	if (result != 0)
		return result;
	if ((events & SOCKET_WAIT_ERROR) != 0)
		return IO_ERROR_IO;
	return socket_send(transport->socket, data, size);
}

static void
network_interrupt(void *state)
{
	struct network_transport *transport = state;

	(void)socket_shutdown(transport->socket, SOCKET_SHUTDOWN_BOTH);
}

static int
network_update_terminal_size(void *state,
	const struct st_screen_geometry *geometry)
{
	struct network_transport *transport = state;
	int result = 0;

	if (!transport->rlogin)
		return 0;
	if (geometry->columns > UINT16_MAX || geometry->rows > UINT16_MAX
	    || geometry->pixel_width > UINT16_MAX
	    || geometry->pixel_height > UINT16_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	(void)st_mutex_lock(&transport->mutex);
	if (memcmp(&transport->geometry, geometry, sizeof(*geometry)) != 0) {
		transport->geometry = *geometry;
		if (transport->window_enabled)
			result = send_window(transport, geometry);
	}
	(void)st_mutex_unlock(&transport->mutex);
	return result;
}

static void
network_destroy(void *state)
{
	struct network_transport *transport = state;

	(void)socket_close(transport->socket);
	(void)socket_runtime_cleanup();
	st_mutex_destroy(&transport->mutex);
	free(transport);
}

static const struct st_connection_transport_ops network_operations = {
	.bind = network_bind,
	.read = network_read,
	.write = network_write,
	.interrupt = network_interrupt,
	.update_terminal_size = network_update_terminal_size,
	.destroy = network_destroy,
};

static ssize_t
rlogin_send(st_connection_t *connection, const void *data, size_t size,
	int64_t timeout_ms)
{
	const uint8_t *bytes = data;
	size_t consumed = 0;

	if (timeout_ms < -1 || (data == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	if (atomic_load(&connection->binary_mode))
		return st_connection_queue_output(connection, data, size, timeout_ms);
	while (consumed < size) {
		size_t start = consumed;
		ssize_t result;

		while (consumed < size && bytes[consumed] != 0x11
		    && bytes[consumed] != 0x13)
			++consumed;
		if (consumed != start) {
			result = st_connection_queue_output(connection, bytes + start,
			    consumed - start, timeout_ms);
			if (result < 0)
				return start == 0 ? result : (ssize_t)start;
			if ((size_t)result != consumed - start)
				return (ssize_t)(start + (size_t)result);
		}
		if (consumed < size) {
			st_connection_log(connection, ST_CONNECTION_LOG_DEBUG,
			    "local %s consumed", bytes[consumed] == 0x13 ? "XOFF" : "XON");
			st_connection_pause_input(connection, bytes[consumed] == 0x13);
			++consumed;
		}
	}
	return (ssize_t)consumed;
}

static int
send_direct(socket_handle_t socket, const void *data, size_t size,
	const struct st_connection_hooks *hooks)
{
	const uint8_t *bytes = data;
	size_t sent = 0;

	while (sent < size) {
		unsigned events;
		ssize_t result;

		if (st_connection_cancelled(hooks,
		    ST_CONNECTION_SETUP_CONNECTING))
			return IO_ERROR_CANCELLED;
		result = socket_wait(socket, SOCKET_WAIT_WRITABLE, 100, &events);
		if (result == IO_ERROR_TIMED_OUT)
			continue;
		if (result != 0)
			return (int)result;
		result = socket_send(socket, bytes + sent, size - sent);
		if (result <= 0)
			return result == 0 ? IO_ERROR_IO : (int)result;
		sent += (size_t)result;
	}
	return 0;
}

static int
send_c_string(socket_handle_t socket, const char *string,
	const struct st_connection_hooks *hooks)
{
	return send_direct(socket, string, strlen(string) + 1, hooks);
}

static int
connect_network(st_connection_t **result, const struct st_profile *profile,
	const struct st_screen_geometry *current_screen,
	const struct st_connection_hooks *hooks,
	struct st_connection_error *error, bool rlogin)
{
	struct network_transport *transport;
	struct tcp_client_options options = {0};
	struct connect_cancel_context cancel = {hooks};
	socket_handle_t socket;
	uint16_t port;
	int status;

	if (result == NULL || profile == NULL || profile->address[0] == '\0')
		return st_connection_fail(error, IO_ERROR_INVALID_ARGUMENT,
		    ST_CONNECTION_SETUP_NONE, "invalid network profile");
	if (rlogin && profile->connection_type != ST_CONNECTION_RLOGIN
	    && profile->connection_type != ST_CONNECTION_RLOGIN_REVERSED)
		return st_connection_fail(error, IO_ERROR_INVALID_ARGUMENT,
		    ST_CONNECTION_SETUP_NONE, "profile is not an RLogin connection");
	if (!rlogin && profile->connection_type != ST_CONNECTION_RAW)
		return st_connection_fail(error, IO_ERROR_INVALID_ARGUMENT,
		    ST_CONNECTION_SETUP_NONE, "profile is not a raw TCP connection");
	if (st_connection_cancelled(hooks, ST_CONNECTION_SETUP_RESOLVING))
		return st_connection_fail(error, IO_ERROR_CANCELLED,
		    ST_CONNECTION_SETUP_RESOLVING, "connection cancelled");
	if (st_connection_cancelled(hooks, ST_CONNECTION_SETUP_CONNECTING))
		return st_connection_fail(error, IO_ERROR_CANCELLED,
		    ST_CONNECTION_SETUP_CONNECTING, "connection cancelled");
	status = socket_runtime_init();
	if (status != 0)
		return st_connection_fail(error, status,
		    ST_CONNECTION_SETUP_CONNECTING,
		    "unable to initialize the socket environment");
	options.family = profile->address_family == ST_ADDRESS_FAMILY_IPV4
	    ? TCP_CLIENT_ADDRESS_IPV4
	    : profile->address_family == ST_ADDRESS_FAMILY_IPV6
	    ? TCP_CLIENT_ADDRESS_IPV6 : TCP_CLIENT_ADDRESS_ANY;
	options.timeout_ms = -1;
	options.cancel = cancel_connect;
	options.cancel_context = &cancel;
	options.diagnostic = st_connection_tcp_diagnostic;
	options.diagnostic_context = (void *)hooks;
	port = profile->port != 0 ? profile->port : (rlogin ? 513 : 23);
	status = tcp_client_connect(&socket, NULL, profile->address, port,
	    &options);
	if (status != 0) {
		(void)socket_runtime_cleanup();
		return st_connection_fail(error, status,
		    ST_CONNECTION_SETUP_CONNECTING, "unable to connect to host");
	}
	st_connection_socket_diagnostic(hooks, "keepalive", socket_set_keepalive(socket, true));
	st_connection_socket_diagnostic(hooks, "send buffer", socket_set_send_buffer_size(socket, 1024U * 1024U));
	st_connection_socket_diagnostic(hooks, "receive buffer", socket_set_receive_buffer_size(socket, 1024U * 1024U));
	if (rlogin) {
		const char nul = '\0';
		const char *first = profile->connection_type
		    == ST_CONNECTION_RLOGIN_REVERSED
		    ? profile->username : profile->password;
		const char *second = profile->connection_type
		    == ST_CONNECTION_RLOGIN_REVERSED
		    ? profile->password : profile->username;
		char terminal[ST_PROFILE_TERMINAL_NAME_MAX + 32];

		(void)snprintf(terminal, sizeof(terminal), "%s/%u",
		    st_connection_terminal_name(profile),
		    profile->bps_rate != 0 ? profile->bps_rate : 115200U);
		st_connection_log_hooks(hooks, "RLogin", ST_CONNECTION_LOG_INFO,
		    "sending RFC 1282 handshake (login fields omitted)");
		status = send_direct(socket, &nul, 1, hooks);
		if (status == 0)
			status = send_c_string(socket, first, hooks);
		if (status == 0)
			status = send_c_string(socket, second, hooks);
		if (status == 0)
			status = send_c_string(socket, terminal, hooks);
		if (status != 0) {
			st_connection_log_hooks(hooks, "RLogin", status == IO_ERROR_CANCELLED
			    ? ST_CONNECTION_LOG_INFO : ST_CONNECTION_LOG_ERROR,
			    "RLogin negotiation failed (%d)", status);
			(void)socket_close(socket);
			(void)socket_runtime_cleanup();
			return st_connection_fail(error, status,
			    ST_CONNECTION_SETUP_CONNECTING,
			    "RLogin negotiation failed");
		}
		st_connection_log_hooks(hooks, "RLogin", ST_CONNECTION_LOG_DEBUG,
		    "sent terminal parameters emulation=%s rate=%u", st_connection_terminal_name(profile),
		    profile->bps_rate != 0 ? profile->bps_rate : 115200U);
	}
	status = socket_set_blocking(socket, false);
	if (status != 0) {
		st_connection_log_hooks(hooks, "TCP", ST_CONNECTION_LOG_ERROR,
		    "nonblocking socket configuration failed (%d)", status);
		(void)socket_close(socket);
		(void)socket_runtime_cleanup();
		return st_connection_fail(error, status,
		    ST_CONNECTION_SETUP_CONNECTING,
		    "unable to configure connected socket");
	}
	transport = calloc(1, sizeof(*transport));
	if (transport == NULL) {
		(void)socket_close(socket);
		(void)socket_runtime_cleanup();
		return st_connection_fail(error, IO_ERROR_OUT_OF_MEMORY,
		    ST_CONNECTION_SETUP_CONNECTING,
		    "unable to allocate network transport");
	}
	transport->socket = socket;
	transport->rlogin = rlogin;
	if (st_mutex_init(&transport->mutex, ST_MUTEX_PLAIN)
	    != ST_THREAD_SUCCESS) {
		free(transport);
		(void)socket_close(socket);
		(void)socket_runtime_cleanup();
		return st_connection_fail(error, IO_ERROR_OUT_OF_MEMORY,
		    ST_CONNECTION_SETUP_CONNECTING,
		    "unable to initialize network transport");
	}
	if (rlogin) {
		status = st_connection_profile_geometry(profile, current_screen,
		    &transport->geometry);
		if (status != 0) {
			network_destroy(transport);
			return st_connection_fail(error, status,
			    ST_CONNECTION_SETUP_NONE,
			    "invalid initial terminal geometry");
		}
	}
	status = st_connection_start(result, transport, &network_operations,
	    rlogin ? "RLogin" : "Raw", hooks, rlogin ? rlogin_send : NULL,
	    error);
	if (status == 0)
		st_connection_log(*result, ST_CONNECTION_LOG_INFO,
		    "session established");
	return status;
}

int
st_connection_connect_raw(st_connection_t **result,
	const struct st_profile *profile, const struct st_connection_hooks *hooks,
	struct st_connection_error *error)
{
	return connect_network(result, profile, NULL, hooks, error, false);
}

int
st_connection_connect_rlogin(st_connection_t **result,
	const struct st_profile *profile,
	const struct st_screen_geometry *current_screen,
	const struct st_connection_hooks *hooks,
	struct st_connection_error *error)
{
	return connect_network(result, profile, current_screen, hooks, error,
	    true);
}
