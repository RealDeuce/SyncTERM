/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_connection.h"
#include "connection_internal.h"

#include "io_error.h"
#include "socket.h"
#include "st_threads.h"
#include "st_time.h"

#include <stdio.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if !SYNCTERM_PLATFORM_WINDOWS
	#include <arpa/inet.h>
	#include <netinet/in.h>
	#include <sys/socket.h>
	#include <unistd.h>
#endif

#define CHECK(e) do { if (!(e)) { \
	fprintf(stderr, "failed: %s:%d: %s\n", __FILE__, __LINE__, #e); \
	return 1; \
} } while (0)

struct server_state {
	socket_handle_t listener;
	atomic_bool allow_urgent;
	atomic_bool allow_flush;
	atomic_bool allow_postmark;
	atomic_int stage;
};

struct log_state {
	atomic_uint entries;
	atomic_int last_error;
	atomic_uint events;
	atomic_bool leaked;
	atomic_int exit_status;
};

enum log_event {
	LOG_RESOLVE = 1, LOG_ATTEMPT = 2, LOG_CONNECTED = 4,
	LOG_HANDSHAKE = 8, LOG_URGENT = 16, LOG_WINDOW = 32,
	LOG_XOFF = 64, LOG_XON = 128, LOG_CLOSED = 256,
	LOG_SHELL_START = 512, LOG_SHELL_EXIT = 1024, LOG_EOF = 2048,
	LOG_FLUSH = 4096
};

static const uint8_t rlogin_handshake[] = {
	0, 's', 'e', 'c', 'r', 'e', 't', 0,
	'u', 's', 'e', 'r', 0,
	'a', 'n', 's', 'i', '/', '1', '1', '5', '2', '0', '0', 0,
};

static void
test_log(void *context, enum st_connection_log_level level,
	const char *source, const char *message)
{
	struct log_state *state = context;

	(void)source;
	fprintf(stderr, "connection log [%d] %s: %s\n", (int)level, source, message);
	unsigned events = 0;
	if (strstr(message, "secret") || strstr(message, "shell-test")
	    || strstr(message, "printf ") || strstr(message, "cmd.exe"))
		atomic_store(&state->leaked, true);
	if (strstr(message, "resolving remote endpoint")) events |= LOG_RESOLVE;
	if (strstr(message, "trying address family=")) events |= LOG_ATTEMPT;
	if (strstr(message, "connection established family=")) events |= LOG_CONNECTED;
	if (strstr(message, "sending RFC 1282 handshake")) events |= LOG_HANDSHAKE;
	if (strstr(message, "urgent control=")) events |= LOG_URGENT;
	if (strstr(message, "urgent control=0x02")) events |= LOG_FLUSH;
	if (strstr(message, "sending window size")) events |= LOG_WINDOW;
	if (strstr(message, "local XOFF consumed")) events |= LOG_XOFF;
	if (strstr(message, "local XON consumed")) events |= LOG_XON;
	if (strstr(message, "provider closed")) events |= LOG_CLOSED;
	if (strstr(message, "starting PTY shell") || strstr(message, "starting ConPTY shell")) events |= LOG_SHELL_START;
	if (strstr(message, "shell exited status=") || strstr(message, "shell terminated signal=")) events |= LOG_SHELL_EXIT;
	int exit_status;
	if (sscanf(message, "shell exited status=%d", &exit_status) == 1)
		atomic_store(&state->exit_status, exit_status);
	if (strstr(message, "peer closed input")) events |= LOG_EOF;
	atomic_fetch_or(&state->events, events);
	(void)atomic_fetch_add(&state->entries, 1);
	if (level == ST_CONNECTION_LOG_ERROR)
		atomic_store(&state->last_error, 1);
}

static int
receive_exact(socket_handle_t socket, void *data, size_t size)
{
	uint8_t *bytes = data;
	size_t received = 0;

	while (received < size) {
		ssize_t count = socket_receive(socket, bytes + received,
		    size - received, SOCKET_RECEIVE_NONE);
		if (count <= 0)
			return 1;
		received += (size_t)count;
	}
	return 0;
}

static int
send_urgent(socket_handle_t socket, uint8_t control)
{
#if SYNCTERM_PLATFORM_WINDOWS
	return send(socket, (const char *)&control, 1, MSG_OOB) == 1 ? 0 : 1;
#else
	return send(socket, &control, 1, MSG_OOB) == 1 ? 0 : 1;
#endif
}

static int
wait_for_stage(struct server_state *state, int stage)
{
	const struct timespec pause = {0, 1000000L};
	struct st_deadline deadline;
	int64_t remaining;

	CHECK(st_deadline_start(&deadline, 3000) == 0);
	for (;;) {
		if (atomic_load(&state->stage) >= stage)
			return 0;
		CHECK(st_deadline_remaining_ms(&deadline, &remaining) == 0);
		if (remaining == 0)
			break;
		(void)st_thread_sleep(&pause, NULL);
	}
	fprintf(stderr, "server stage is %d, expected %d\n",
	    atomic_load(&state->stage), stage);
	return 1;
}

static int
connection_receive_exact(st_connection_t *connection, void *data,
	size_t size)
{
	uint8_t *bytes = data;
	size_t received = 0;

	while (received < size) {
		ssize_t count = st_connection_receive(connection, bytes + received,
		    size - received, 2000);

		if (count <= 0)
			return 1;
		received += (size_t)count;
	}
	return 0;
}

static int
wait_for_pending(st_connection_t *connection, size_t count)
{
	const struct timespec pause = {0, 1000000L};
	struct st_deadline deadline;
	int64_t remaining;

	/* A sleep request is not an elapsed-time measurement: native Windows
	 * may round each millisecond sleep up to its timer resolution. */
	CHECK(st_deadline_start(&deadline, 3000) == 0);
	for (;;) {
		if (st_connection_pending_raw(connection) == count)
			return 0;
		CHECK(st_deadline_remaining_ms(&deadline, &remaining) == 0);
		if (remaining == 0)
			break;
		(void)st_thread_sleep(&pause, NULL);
	}
	fprintf(stderr, "pending input is %zu, expected %zu (connected=%d)\n",
	    st_connection_pending_raw(connection), count,
	    st_connection_is_connected(connection));
	return 1;
}

static int
server_worker(void *argument)
{
	struct server_state *state = argument;
	socket_handle_t client;
	char buffer[16];

	client = accept(state->listener, NULL, NULL);
	if (!socket_handle_is_valid(client))
		return 1;
	if (socket_receive(client, buffer, 3, SOCKET_RECEIVE_NONE) != 3
	    || memcmp(buffer, "one", 3) != 0) {
		(void)socket_close(client);
		return 1;
	}
	if (socket_send(client, "twothree", 8) != 8) {
		(void)socket_close(client);
		return 1;
	}
	(void)socket_shutdown(client, SOCKET_SHUTDOWN_BOTH);
	(void)socket_close(client);
	return 0;
}

static int
rlogin_server_worker(void *argument)
{
	struct server_state *state = argument;
	static const uint8_t expected_window[] = {
		0xff, 0xff, 's', 's', 0, 30, 0, 100, 0, 0, 0, 0,
	};
	static const uint8_t updated_window[] = {
		0xff, 0xff, 's', 's', 0, 40, 0, 120, 0, 0, 0, 0,
	};
	socket_handle_t client;
	uint8_t buffer[256];

	client = accept(state->listener, NULL, NULL);
	if (!socket_handle_is_valid(client))
		return 1;
	atomic_store(&state->stage, 1);
	if (receive_exact(client, buffer, sizeof(rlogin_handshake)) != 0
	    || memcmp(buffer, rlogin_handshake, sizeof(rlogin_handshake)) != 0)
		goto failed;
	if (socket_send(client, "", 1) != 1)
		goto failed;
	if (receive_exact(client, buffer, sizeof(buffer)) != 0)
		goto failed;
	for (size_t i = 0; i < sizeof(buffer); ++i) {
		if (buffer[i] != (uint8_t)i)
			goto failed;
	}
	if (socket_send(client, buffer, sizeof(buffer)) != (ssize_t)sizeof(buffer))
		goto failed;
	atomic_store(&state->stage, 2);
	while (!atomic_load(&state->allow_urgent))
		st_thread_yield();
	if (send_urgent(client, 0x80) != 0)
		goto failed;
	atomic_store(&state->stage, 3);
	if (receive_exact(client, buffer, sizeof(expected_window)) != 0
	    || memcmp(buffer, expected_window, sizeof(expected_window)) != 0)
		goto failed;
	atomic_store(&state->stage, 4);
	if (receive_exact(client, buffer, sizeof(updated_window)) != 0
	    || memcmp(buffer, updated_window, sizeof(updated_window)) != 0)
		goto failed;
	atomic_store(&state->stage, 5);
	if (send_urgent(client, 0x80) != 0
	    || receive_exact(client, buffer, sizeof(updated_window)) != 0
	    || memcmp(buffer, updated_window, sizeof(updated_window)) != 0)
		goto failed;
	if (socket_send(client, "y", 1) != 1)
		goto failed;
	atomic_store(&state->stage, 6);
	while (!atomic_load(&state->allow_flush))
		st_thread_yield();
	if (send_urgent(client, 0x20) != 0 || socket_send(client, "cooked", 6) != 6)
		goto failed;
	if (receive_exact(client, buffer, 2) != 0
	    || memcmp(buffer, "AB", 2) != 0)
		goto failed;
	if (socket_send(client, "ready", 5) != 5)
		goto failed;
	atomic_store(&state->stage, 7);
	(void)socket_shutdown(client, SOCKET_SHUTDOWN_BOTH);
	(void)socket_close(client);
	return 0;

failed:
	(void)socket_close(client);
	return 1;
}

static int
rlogin_flush_server_worker(void *argument)
{
	struct server_state *state = argument;
	socket_handle_t client;
	uint8_t buffer[64];

	client = accept(state->listener, NULL, NULL);
	if (!socket_handle_is_valid(client))
		return 1;
	if (receive_exact(client, buffer, sizeof(rlogin_handshake)) != 0
	    || memcmp(buffer, rlogin_handshake, sizeof(rlogin_handshake)) != 0)
		goto failed;
	if (socket_send(client, "", 1) != 1)
		goto failed;
	if (socket_send(client, "stale!!", 7) != 7)
		goto failed;
	atomic_store(&state->stage, 3);
	while (!atomic_load(&state->allow_flush))
		st_thread_yield();
	if (send_urgent(client, 0x02) != 0)
		goto failed;
	atomic_store(&state->stage, 4);
	while (!atomic_load(&state->allow_postmark))
		st_thread_yield();
	if (socket_send(client, "fresh", 5) != 5)
		goto failed;
	atomic_store(&state->stage, 5);
	if (receive_exact(client, buffer, 5) != 0
	    || memcmp(buffer, "keepF", 5) != 0)
		goto failed;
	if (socket_send(client, "ready", 5) != 5)
		goto failed;
	atomic_store(&state->stage, 6);
	(void)socket_shutdown(client, SOCKET_SHUTDOWN_BOTH);
	(void)socket_close(client);
	return 0;

failed:
	(void)socket_close(client);
	return 1;
}

static int
create_listener(struct server_state *server, uint16_t *port)
{
	struct sockaddr_in address;
#if SYNCTERM_PLATFORM_WINDOWS
	int address_size = sizeof(address);
#else
	socklen_t address_size = sizeof(address);
#endif

	server->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (!socket_handle_is_valid(server->listener))
		return 1;
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bind(server->listener, (struct sockaddr *)&address,
	    sizeof(address)) != 0
	    || getsockname(server->listener, (struct sockaddr *)&address,
	    &address_size) != 0 || listen(server->listener, 1) != 0) {
		(void)socket_close(server->listener);
		return 1;
	}
	*port = ntohs(address.sin_port);
	atomic_init(&server->allow_urgent, false);
	atomic_init(&server->allow_flush, false);
	atomic_init(&server->allow_postmark, false);
	atomic_init(&server->stage, 0);
	return 0;
}

static enum st_connection_setup_action
cancel_setup(void *context, enum st_connection_setup_phase phase)
{
	(void)context;
	return phase == ST_CONNECTION_SETUP_RESOLVING
	    ? ST_CONNECTION_SETUP_CANCEL : ST_CONNECTION_SETUP_CONTINUE;
}

static int
test_raw(void)
{
	struct log_state log = {0};
	struct server_state server;
	struct st_profile profile;
	struct st_connection_hooks hooks = {0};
	struct st_connection_error error;
	st_connection_t *connection = NULL;
	st_thread_t thread;
	char buffer[8];
	int thread_result;
	uint16_t port;
	unsigned attempts;

	CHECK(socket_runtime_init() == 0);
	CHECK(create_listener(&server, &port) == 0);
	CHECK(st_thread_create(&thread, server_worker, &server)
	    == ST_THREAD_SUCCESS);
	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_RAW;
	profile.address_family = ST_ADDRESS_FAMILY_IPV4;
	strcpy(profile.address, "127.0.0.1");
	profile.port = port;
	hooks.log = test_log;
	hooks.log_context = &log;
	CHECK(st_connection_connect_raw(&connection, &profile, &hooks, &error) == 0);
	CHECK(st_connection_send(connection, "", 1, -2)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_connection_send(connection, "one", 3, 1000) == 3);
	for (attempts = 0; attempts < 1000
	    && st_connection_pending_raw(connection) == 0; ++attempts)
		(void)st_thread_sleep(&(struct timespec){0, 1000000L}, NULL);
	CHECK(st_connection_pending_raw(connection) == 8);
	CHECK(st_connection_peek_raw(connection, buffer, 3) == 3);
	CHECK(memcmp(buffer, "two", 3) == 0);
	CHECK(st_connection_receive(connection, buffer, 3, 1000) == 3);
	CHECK(memcmp(buffer, "two", 3) == 0);
	CHECK(st_connection_receive_raw(connection, buffer, 5, 1000) == 5);
	CHECK(memcmp(buffer, "three", 5) == 0);
	CHECK(st_connection_receive(connection, buffer, 1, 1000) == 0);
	CHECK(st_connection_close(connection) == 0);
	CHECK(st_thread_join(thread, &thread_result) == ST_THREAD_SUCCESS);
	CHECK(thread_result == 0);
	CHECK(socket_close(server.listener) == 0);
	CHECK(socket_runtime_cleanup() == 0);
	CHECK(atomic_load(&log.events) == (LOG_RESOLVE | LOG_ATTEMPT | LOG_CONNECTED | LOG_CLOSED | LOG_EOF));
	CHECK(!atomic_load(&log.leaked) && !atomic_load(&log.last_error));

	connection = (st_connection_t *)(uintptr_t)1;
	hooks.setup = cancel_setup;
	CHECK(st_connection_connect_raw(&connection, &profile, &hooks, &error)
	    == IO_ERROR_CANCELLED);
	CHECK(connection == (st_connection_t *)(uintptr_t)1);
	CHECK(error.phase == ST_CONNECTION_SETUP_RESOLVING);
	st_profile_dispose(&profile);
	return 0;
}

static int
wait_for_binary(st_connection_t *connection, bool binary)
{
	const struct timespec pause = {0, 1000000L};
	struct st_deadline deadline;
	int64_t remaining;

	CHECK(st_deadline_start(&deadline, 3000) == 0);
	for (;;) {
		if (atomic_load(&connection->binary_mode) == binary)
			return 0;
		CHECK(st_deadline_remaining_ms(&deadline, &remaining) == 0);
		if (remaining == 0)
			break;
		(void)st_thread_sleep(&pause, NULL);
	}
	fprintf(stderr, "RLogin did not switch to %s mode\n", binary ? "raw" : "cooked");
	return 1;
}

static int
test_rlogin(void)
{
	struct server_state server;
	struct st_profile profile;
	struct st_screen_geometry geometry = {80, 25, 0, 0};
	struct st_connection_error error;
	struct log_state log_state = {0};
	struct st_connection_hooks hooks = {0};
	st_connection_t *connection = NULL;
	st_thread_t thread;
	uint8_t buffer[256];
	uint16_t port;
	int thread_result;

	atomic_init(&log_state.entries, 0);
	atomic_init(&log_state.last_error, 0);
	hooks.log = test_log;
	hooks.log_context = &log_state;

	CHECK(socket_runtime_init() == 0);
	CHECK(create_listener(&server, &port) == 0);
	CHECK(st_thread_create(&thread, rlogin_server_worker, &server)
	    == ST_THREAD_SUCCESS);
	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_RLOGIN;
	profile.address_family = ST_ADDRESS_FAMILY_IPV4;
	profile.screen_mode = ST_SCREEN_MODE_CURRENT;
	profile.port = port;
	strcpy(profile.address, "127.0.0.1");
	strcpy(profile.username, "user");
	strcpy(profile.password, "secret");
	strcpy(profile.terminal_name, "ansi");
	CHECK(st_connection_connect_rlogin(&connection, &profile, &geometry,
	    &hooks, &error) == 0);
	CHECK(atomic_load(&connection->binary_mode));
	for (size_t i = 0; i < sizeof(buffer); ++i)
		buffer[i] = (uint8_t)i;
	CHECK(st_connection_send(connection, buffer, sizeof(buffer), 1000)
	    == (ssize_t)sizeof(buffer));
	memset(buffer, 0, sizeof(buffer));
	CHECK(connection_receive_exact(connection, buffer, sizeof(buffer)) == 0);
	for (size_t i = 0; i < sizeof(buffer); ++i)
		CHECK(buffer[i] == (uint8_t)i);
	geometry.columns = 100;
	geometry.rows = 30;
	CHECK(st_connection_update_terminal_size(connection, &geometry) == 0);
	CHECK(st_connection_update_terminal_size(connection, &geometry) == 0);
	atomic_store(&server.allow_urgent, true);
	CHECK(wait_for_stage(&server, 4) == 0);
	geometry.columns = 120;
	geometry.rows = 40;
	CHECK(st_connection_update_terminal_size(connection, &geometry) == 0);
	CHECK(wait_for_stage(&server, 6) == 0);
	CHECK(connection_receive_exact(connection, buffer, 1) == 0);
	CHECK(buffer[0] == 'y');
	atomic_store(&server.allow_flush, true);
	CHECK(connection_receive_exact(connection, buffer, 6) == 0);
	CHECK(memcmp(buffer, "cooked", 6) == 0);
	CHECK(wait_for_binary(connection, false) == 0);
	CHECK(st_connection_send(connection, "\x13" "AB" "\x11", 4, 1000)
	    == 4);
	CHECK(connection_receive_exact(connection, buffer, 5) == 0);
	CHECK(memcmp(buffer, "ready", 5) == 0);
	CHECK(st_connection_close(connection) == 0);
	CHECK(st_thread_join(thread, &thread_result) == ST_THREAD_SUCCESS);
	CHECK(thread_result == 0);
	CHECK(socket_close(server.listener) == 0);
	CHECK(socket_runtime_cleanup() == 0);
	CHECK(atomic_load(&log_state.entries) >= 1);
	CHECK((atomic_load(&log_state.events) & (LOG_RESOLVE | LOG_ATTEMPT | LOG_CONNECTED
	    | LOG_HANDSHAKE | LOG_URGENT | LOG_WINDOW | LOG_XOFF | LOG_XON | LOG_CLOSED))
	    == (LOG_RESOLVE | LOG_ATTEMPT | LOG_CONNECTED | LOG_HANDSHAKE
	    | LOG_URGENT | LOG_WINDOW | LOG_XOFF | LOG_XON | LOG_CLOSED));
	CHECK(!atomic_load(&log_state.leaked));
	st_profile_dispose(&profile);
	return 0;
}

static int
test_rlogin_flush(void)
{
	struct server_state server;
	struct st_profile profile;
	struct st_screen_geometry geometry = {80, 25, 0, 0};
	st_connection_t *connection = NULL;
	st_thread_t thread;
	char buffer[8];
	uint16_t port;
	int thread_result;

	CHECK(socket_runtime_init() == 0);
	CHECK(create_listener(&server, &port) == 0);
	CHECK(st_thread_create(&thread, rlogin_flush_server_worker, &server)
	    == ST_THREAD_SUCCESS);
	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_RLOGIN;
	profile.address_family = ST_ADDRESS_FAMILY_IPV4;
	profile.screen_mode = ST_SCREEN_MODE_CURRENT;
	profile.port = port;
	strcpy(profile.address, "127.0.0.1");
	strcpy(profile.username, "user");
	strcpy(profile.password, "secret");
	strcpy(profile.terminal_name, "ansi");
	CHECK(st_connection_connect_rlogin(&connection, &profile, &geometry,
	    NULL, NULL) == 0);
	CHECK(wait_for_stage(&server, 3) == 0);
	CHECK(wait_for_pending(connection, 7) == 0);
	CHECK(st_connection_send(connection, "keep", 4, 1000) == 4);
	atomic_store(&server.allow_flush, true);
	CHECK(wait_for_stage(&server, 4) == 0);
	CHECK(wait_for_pending(connection, 0) == 0);
	atomic_store(&server.allow_postmark, true);
	CHECK(wait_for_stage(&server, 5) == 0);
	CHECK(wait_for_pending(connection, 5) == 0);
	CHECK(st_connection_peek_raw(connection, buffer, 5) == 5);
	CHECK(memcmp(buffer, "fresh", 5) == 0);
	CHECK(connection_receive_exact(connection, buffer, 5) == 0);
	CHECK(memcmp(buffer, "fresh", 5) == 0);
	CHECK(st_connection_send(connection, "F", 1, 1000) == 1);
	CHECK(connection_receive_exact(connection, buffer, 5) == 0);
	CHECK(memcmp(buffer, "ready", 5) == 0);
	CHECK(st_connection_close(connection) == 0);
	CHECK(st_thread_join(thread, &thread_result) == ST_THREAD_SUCCESS);
	CHECK(thread_result == 0);
	CHECK(socket_close(server.listener) == 0);
	CHECK(socket_runtime_cleanup() == 0);
	st_profile_dispose(&profile);
	return 0;
}

static int
rlogin_flow_server(void *argument)
{
	struct server_state *state = argument;
	socket_handle_t client = accept(state->listener, NULL, NULL);
	uint8_t bytes[64];
	static const uint8_t window[] = {
		0xff, 0xff, 's', 's', 0, 25, 0, 80, 0, 0, 0, 0,
	};
	if (!socket_handle_is_valid(client))
		return 1;
	if (receive_exact(client, bytes, sizeof(rlogin_handshake)) != 0
	    || memcmp(bytes, rlogin_handshake, sizeof(rlogin_handshake)) != 0
	    || socket_send(client, "\0start", 6) != 6)
		goto failed;
	if (receive_exact(client, bytes, 1) != 0 || bytes[0] != 'I'
	    || send_urgent(client, 0x20) != 0)
		goto failed;
	while (!atomic_load(&state->allow_urgent))
		st_thread_yield();
	if (socket_send(client, "held", 4) != 4 || send_urgent(client, 0x80) != 0
	    || receive_exact(client, bytes, sizeof(window)) != 0
	    || memcmp(bytes, window, sizeof(window)) != 0)
		goto failed;
	atomic_store(&state->stage, 3);
	if (receive_exact(client, bytes, 1) != 0 || bytes[0] != 'A'
	    || socket_send(client, "post", 4) != 4
	    || receive_exact(client, bytes, 1) != 0 || bytes[0] != 'Z'
	    || send_urgent(client, 0x01) != 0
	    || socket_send(client, "unknown", 7) != 7
	    || receive_exact(client, bytes, 1) != 0 || bytes[0] != 'B'
	    || send_urgent(client, 0x10) != 0
	    || receive_exact(client, bytes, 3) != 0
	    || memcmp(bytes, "\x13R\x11", 3) != 0
	    || send_urgent(client, 0x20) != 0
	    || receive_exact(client, bytes, 1) != 0 || bytes[0] != 'C'
	    || socket_send(client, "again", 5) != 5
	    || send_urgent(client, 0x10) != 0
	    || receive_exact(client, bytes, 1) != 0 || bytes[0] != 'D')
		goto failed;
	atomic_store(&state->stage, 5);
	(void)socket_close(client);
	return 0;
failed:
	(void)socket_close(client);
	return 1;
}

static int
rlogin_backpressure_server(void *argument)
{
	struct server_state *state = argument;
	socket_handle_t client = accept(state->listener, NULL, NULL);
	uint8_t bytes[1024];
	static const uint8_t window[] = {
		0xff, 0xff, 's', 's', 0, 25, 0, 80, 0, 0, 0, 0,
	};
	if (!socket_handle_is_valid(client))
		return 1;
	if (receive_exact(client, bytes, sizeof(rlogin_handshake)) != 0
	    || memcmp(bytes, rlogin_handshake, sizeof(rlogin_handshake)) != 0
	    || socket_send(client, "", 1) != 1)
		goto failed;
	if (receive_exact(client, bytes, 1) != 0 || bytes[0] != 'I'
	    || send_urgent(client, 0x20) != 0)
		goto failed;
	while (!atomic_load(&state->allow_urgent))
		st_thread_yield();
	memset(bytes, 'x', sizeof(bytes));
	/* More than the queue plus the worker's staged chunk: stale bytes must
	 * remain in the native socket when FLUSHWRITE arrives. */
	for (size_t sent = 0; sent < 3 * ST_CONNECTION_BUFFER_SIZE;) {
		ssize_t count = socket_send(client, bytes, sizeof(bytes));
		if (count <= 0)
			goto failed;
		sent += (size_t)count;
		if (sent % ST_CONNECTION_BUFFER_SIZE == 0)
			fprintf(stderr, "RLogin backpressure peer queued=%zu bytes\n", sent);
	}
	while (!atomic_load(&state->allow_flush))
		st_thread_yield();
	fprintf(stderr, "RLogin backpressure peer sending window control\n");
	if (send_urgent(client, 0x80) != 0
	    || receive_exact(client, bytes, 12) != 0
	    || memcmp(bytes, window, sizeof(window)) != 0)
		goto failed;
	atomic_store(&state->stage, 4);
	while (!atomic_load(&state->allow_postmark))
		st_thread_yield();
	if (send_urgent(client, 0x02) != 0)
		goto failed;
	atomic_store(&state->stage, 5);
	if (receive_exact(client, bytes, 1) != 0 || bytes[0] != 'K'
	    || socket_send(client, "fresh", 5) != 5
	    || receive_exact(client, bytes, 1) != 0 || bytes[0] != 'D')
		goto failed;
	(void)socket_close(client);
	return 0;
failed:
	(void)socket_close(client);
	return 1;
}

static int
wait_for_log_event(struct log_state *state, unsigned event)
{
	const struct timespec pause = {0, 1000000L};
	struct st_deadline deadline;
	int64_t remaining;

	CHECK(st_deadline_start(&deadline, 3000) == 0);
	while ((atomic_load(&state->events) & event) == 0) {
		CHECK(st_deadline_remaining_ms(&deadline, &remaining) == 0);
		if (remaining == 0) {
			fprintf(stderr, "missing connection log event 0x%x (events=0x%x)\n",
			    event, atomic_load(&state->events));
			return 1;
		}
		(void)st_thread_sleep(&pause, NULL);
	}
	return 0;
}

#if SYNCTERM_PLATFORM_WINDOWS
static int
receive_unpositioned_flush(st_connection_t *connection)
{
	static const char fresh[] = "fresh";
	struct st_deadline deadline;
	size_t stale = 0, matched = 0;
	char bytes[1024];

	CHECK(st_deadline_start(&deadline, 3000) == 0);
	while (matched < sizeof(fresh) - 1) {
		int64_t remaining;
		CHECK(st_deadline_remaining_ms(&deadline, &remaining) == 0);
		CHECK(remaining > 0);
		ssize_t count = st_connection_receive(connection, bytes, sizeof(bytes), remaining);
		CHECK(count > 0);
		for (ssize_t i = 0; i < count; ++i) {
			/* Windows cannot discard precisely to the native urgent mark.
			 * Allow retained kernel input, but require every fresh byte in
			 * order: a fallback must not drain away the suffix. */
			if (matched == 0 && bytes[i] == 'x') {
				CHECK(++stale <= 3 * ST_CONNECTION_BUFFER_SIZE);
				continue;
			}
			CHECK(matched < sizeof(fresh) - 1 && bytes[i] == fresh[matched]);
			++matched;
		}
	}
	fprintf(stderr, "RLogin unpositioned flush retained=%zu native bytes; fresh suffix intact\n", stale);
	return 0;
}
#endif

static int
test_rlogin_flow(bool backpressure)
{
	struct server_state server;
	struct log_state log = {0};
	const struct st_connection_hooks hooks = {.log = test_log, .log_context = &log};
	struct st_profile profile;
	struct st_screen_geometry geometry = {80, 25, 0, 0};
	st_connection_t *connection = NULL;
	st_thread_t thread;
	char bytes[8];
	uint16_t port;
	int thread_result;
	CHECK(socket_runtime_init() == 0);
	CHECK(create_listener(&server, &port) == 0);
	CHECK(st_thread_create(&thread, backpressure ? rlogin_backpressure_server
	    : rlogin_flow_server, &server) == ST_THREAD_SUCCESS);
	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_RLOGIN;
	profile.address_family = ST_ADDRESS_FAMILY_IPV4;
	profile.screen_mode = ST_SCREEN_MODE_CURRENT;
	profile.hide_status = true;
	profile.port = port;
	strcpy(profile.address, "127.0.0.1");
	strcpy(profile.username, "user");
	strcpy(profile.password, "secret");
	strcpy(profile.terminal_name, "ansi");
	CHECK(st_connection_connect_rlogin(&connection, &profile, &geometry,
	    &hooks, NULL) == 0);
	CHECK(atomic_load(&connection->binary_mode));
	if (!backpressure) {
		CHECK(connection_receive_exact(connection, bytes, 5) == 0);
		CHECK(memcmp(bytes, "start", 5) == 0); /* ACK must not reach data. */
	}
	CHECK(st_connection_send(connection, "I", 1, 1000) == 1);
	CHECK(wait_for_binary(connection, false) == 0);
	CHECK(st_connection_send(connection, "\x13", 1, 1000) == 1);
	atomic_store(&server.allow_urgent, true);
	if (backpressure) {
		CHECK(wait_for_pending(connection, ST_CONNECTION_BUFFER_SIZE) == 0);
		atomic_store(&server.allow_flush, true);
		CHECK(wait_for_stage(&server, 4) == 0); /* Window request while full. */
	} else {
		CHECK(wait_for_stage(&server, 3) == 0);
		CHECK(wait_for_pending(connection, 4) == 0);
	}
	CHECK(st_connection_receive(connection, bytes, 1, 0) == IO_ERROR_WOULD_BLOCK);
	CHECK(st_connection_peek_raw(connection, bytes, 1) == IO_ERROR_WOULD_BLOCK);
	if (backpressure) {
		atomic_store(&server.allow_postmark, true);
		CHECK(wait_for_stage(&server, 5) == 0);
		CHECK(wait_for_log_event(&log, LOG_FLUSH) == 0);
#if !SYNCTERM_PLATFORM_WINDOWS
		CHECK(wait_for_pending(connection, 0) == 0);
#endif
		CHECK(st_connection_send(connection, "K", 1, 1000) == 1);
#if !SYNCTERM_PLATFORM_WINDOWS
		CHECK(wait_for_pending(connection, 5) == 0);
#endif
		CHECK(st_connection_receive(connection, bytes, 1, 0) == IO_ERROR_WOULD_BLOCK);
	}
	CHECK(st_connection_send(connection, "\x11", 1, 1000) == 1);
#if SYNCTERM_PLATFORM_WINDOWS
	if (backpressure)
		CHECK(receive_unpositioned_flush(connection) == 0);
	else
#endif
	{
		CHECK(connection_receive_exact(connection, bytes, backpressure ? 5 : 4) == 0);
		CHECK(memcmp(bytes, backpressure ? "fresh" : "held", backpressure ? 5 : 4) == 0);
	}
	if (!backpressure) {
		CHECK(st_connection_send(connection, "A", 1, 1000) == 1);
		CHECK(connection_receive_exact(connection, bytes, 4) == 0);
		CHECK(memcmp(bytes, "post", 4) == 0);
		CHECK(st_connection_send(connection, "Z", 1, 1000) == 1);
		CHECK(connection_receive_exact(connection, bytes, 7) == 0);
		CHECK(memcmp(bytes, "unknown", 7) == 0);
		CHECK(!atomic_load(&connection->binary_mode));
		CHECK(st_connection_send(connection, "B", 1, 1000) == 1);
		CHECK(wait_for_binary(connection, true) == 0);
		CHECK(st_connection_send(connection, "\x13R\x11", 3, 1000) == 3);
		CHECK(wait_for_binary(connection, false) == 0);
		CHECK(st_connection_send(connection, "\x13" "C", 2, 1000) == 2);
		CHECK(wait_for_binary(connection, true) == 0); /* Raw resumes XOFF. */
		CHECK(connection_receive_exact(connection, bytes, 5) == 0);
		CHECK(memcmp(bytes, "again", 5) == 0);
	}
	CHECK(st_connection_send(connection, "D", 1, 1000) == 1);
	ssize_t final_read = st_connection_receive(connection, bytes, 1, 3000);
	if (final_read != 0)
		fprintf(stderr, "RLogin final read=%zd server-stage=%d connected=%d queued=%zu\n",
		    final_read, atomic_load(&server.stage), st_connection_is_connected(connection),
		    st_connection_pending_raw(connection));
	CHECK(final_read == 0);
	CHECK(st_connection_close(connection) == 0);
	CHECK(st_thread_join(thread, &thread_result) == ST_THREAD_SUCCESS);
	CHECK(thread_result == 0);
	CHECK(socket_close(server.listener) == 0);
	CHECK(socket_runtime_cleanup() == 0);
	st_profile_dispose(&profile);
	return 0;
}

enum rlogin_setup_case {
	RLOGIN_REJECT, RLOGIN_EARLY_EOF, RLOGIN_CANCEL,
	RLOGIN_STOPPED_CLOSE, RLOGIN_STOPPED_EOF,
};

struct rlogin_setup_state {
	struct server_state server;
	enum rlogin_setup_case mode;
};

static int
rlogin_setup_server(void *argument)
{
	struct rlogin_setup_state *state = argument;
	socket_handle_t client = accept(state->server.listener, NULL, NULL);
	uint8_t bytes[1024];
	ssize_t received;
	int result = 1;
	if (!socket_handle_is_valid(client))
		return 1;
	if (receive_exact(client, bytes, sizeof(rlogin_handshake)) != 0
	    || memcmp(bytes, rlogin_handshake, sizeof(rlogin_handshake)) != 0)
		goto done;
	if (state->mode == RLOGIN_REJECT) {
		result = socket_send(client, "\1Rejected\n", 10) == 10 ? 0 : 1;
		goto done;
	}
	if (state->mode == RLOGIN_EARLY_EOF) {
		result = 0;
		goto done;
	}
	if (state->mode == RLOGIN_STOPPED_CLOSE || state->mode == RLOGIN_STOPPED_EOF) {
		if (socket_send(client, "", 1) != 1 || send_urgent(client, 0x20) != 0)
			goto done;
		memset(bytes, 'x', sizeof(bytes));
		for (size_t sent = 0; sent < 3 * ST_CONNECTION_BUFFER_SIZE;) {
			ssize_t count = socket_send(client, bytes, sizeof(bytes));
			if (count <= 0)
				goto done;
			sent += (size_t)count;
		}
		atomic_store(&state->server.stage, 3);
		if (state->mode == RLOGIN_STOPPED_EOF) {
			while (!atomic_load(&state->server.allow_flush))
				st_thread_yield();
			result = 0;
			goto done;
		}
	}
	/* Cancellation during ACK wait and local close while stopped must both
	 * close the transport and release the peer, not strand a worker. */
	received = socket_receive(client, bytes, 1, SOCKET_RECEIVE_NONE);
	result = received == 0 || (state->mode == RLOGIN_STOPPED_CLOSE
	    && received == IO_ERROR_CONNECTION_RESET) ? 0 : 1;
done:
	(void)socket_close(client);
	return result;
}

static enum st_connection_setup_action
cancel_rlogin_ack(void *context, enum st_connection_setup_phase phase)
{
	(void)context;
	return phase == ST_CONNECTION_SETUP_NEGOTIATING
	    ? ST_CONNECTION_SETUP_CANCEL : ST_CONNECTION_SETUP_CONTINUE;
}

static int
test_rlogin_setup(void)
{
	for (int mode = RLOGIN_REJECT; mode <= RLOGIN_STOPPED_EOF; ++mode) {
		struct rlogin_setup_state state;
		struct st_profile profile;
		struct st_screen_geometry geometry = {80, 25, 0, 0};
		struct st_connection_error error;
		struct log_state log = {0};
		struct st_connection_hooks hooks = {.log = test_log, .log_context = &log};
		st_connection_t *connection = NULL;
		st_thread_t thread;
		uint16_t port;
		int thread_result;
		CHECK(socket_runtime_init() == 0);
		CHECK(create_listener(&state.server, &port) == 0);
		state.mode = (enum rlogin_setup_case)mode;
		CHECK(st_thread_create(&thread, rlogin_setup_server, &state) == ST_THREAD_SUCCESS);
		st_profile_init(&profile);
		profile.connection_type = ST_CONNECTION_RLOGIN;
		profile.address_family = ST_ADDRESS_FAMILY_IPV4;
		profile.screen_mode = ST_SCREEN_MODE_CURRENT;
		profile.port = port;
		strcpy(profile.address, "127.0.0.1");
		strcpy(profile.username, "user");
		strcpy(profile.password, "secret");
		strcpy(profile.terminal_name, "ansi");
		if (mode == RLOGIN_CANCEL)
			hooks.setup = cancel_rlogin_ack;
		int status = st_connection_connect_rlogin(&connection, &profile, &geometry,
		    &hooks, &error);
		if (mode == RLOGIN_STOPPED_CLOSE || mode == RLOGIN_STOPPED_EOF) {
			CHECK(status == 0);
			CHECK(wait_for_binary(connection, false) == 0);
			CHECK(st_connection_send(connection, "\x13", 1, 1000) == 1);
			CHECK(wait_for_pending(connection, ST_CONNECTION_BUFFER_SIZE) == 0);
			CHECK(wait_for_stage(&state.server, 3) == 0);
			if (mode == RLOGIN_STOPPED_EOF) {
				uint8_t bytes[1024];
				size_t received = 0;
				ssize_t count;
				atomic_store(&state.server.allow_flush, true);
				while ((count = st_connection_receive(connection, bytes, sizeof(bytes), 2000)) > 0) {
					for (ssize_t i = 0; i < count; ++i)
						CHECK(bytes[i] == 'x');
					received += (size_t)count;
				}
				if (count != 0 || received != 3 * ST_CONNECTION_BUFFER_SIZE)
					fprintf(stderr, "held EOF read=%zd received=%zu queued=%zu connected=%d\n",
					    count, received, st_connection_pending_raw(connection),
					    st_connection_is_connected(connection));
				CHECK(count == 0 && received == 3 * ST_CONNECTION_BUFFER_SIZE);
			}
			CHECK(st_connection_close(connection) == 0);
		} else {
			int expected = mode == RLOGIN_REJECT ? IO_ERROR_PERMISSION_DENIED
			    : mode == RLOGIN_EARLY_EOF ? IO_ERROR_CONNECTION_RESET : IO_ERROR_CANCELLED;
			CHECK(status == expected && error.code == expected);
			CHECK(error.phase == ST_CONNECTION_SETUP_NEGOTIATING);
			CHECK(connection == NULL);
		}
		CHECK(st_thread_join(thread, &thread_result) == ST_THREAD_SUCCESS);
		CHECK(thread_result == 0);
		CHECK(socket_close(state.server.listener) == 0);
		CHECK(socket_runtime_cleanup() == 0);
		st_profile_dispose(&profile);
	}
	return 0;
}

static int
test_shell(void)
{
	struct log_state log = {0};
	struct st_connection_hooks hooks = {.log = test_log, .log_context = &log};
	struct st_profile profile;
	struct st_screen_geometry geometry = {80, 25, 0, 0};
	struct st_connection_error error;
	st_connection_t *connection = NULL;
	ssize_t count;

	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_SHELL;
	profile.screen_mode = ST_SCREEN_MODE_CURRENT;
#if SYNCTERM_PLATFORM_WINDOWS
	/* SET /P writes without a newline but returns failure on its NUL input.
	 * Make this successful-output fixture explicitly exit successfully. */
	strcpy(profile.address, "cmd.exe /d /c <nul set /p =shell-test & exit /b 0");
#else
	strcpy(profile.address, "printf shell-test");
#endif
#if ST_CONNECTION_TEST_HAVE_SHELL
	char buffer[4096];
	size_t received = 0;

	count = st_connection_connect_shell(&connection, &profile, &geometry,
	    NULL, &hooks, &error);
#if SYNCTERM_PLATFORM_WINDOWS
	if (count == IO_ERROR_NOT_SUPPORTED) {
		st_profile_dispose(&profile);
		return 0;
	}
#endif
	CHECK(count == 0);
	geometry.rows = 24;
	CHECK(st_connection_update_terminal_size(connection, &geometry) == 0
	    || !st_connection_is_connected(connection));
	/* Pipes and PTYs are streams, not one-write/one-read records. ConPTY
	 * can also emit terminal setup sequences around the command output. */
	for (unsigned attempt = 0; attempt < 50; ++attempt) {
		count = st_connection_receive(connection, buffer + received,
		    sizeof(buffer) - 1 - received, 100);
		if (count == IO_ERROR_TIMED_OUT)
			continue;
		if (count <= 0)
			break;
		received += (size_t)count;
		if (received == sizeof(buffer) - 1)
			break;
	}
	buffer[received] = '\0';
	if (count != 0 || strstr(buffer, "shell-test") == NULL) {
		fprintf(stderr, "shell output: last read=%zd bytes=%zu data=",
		    count, received);
		for (size_t i = 0; i < received; ++i)
			fprintf(stderr, "%02x ", (unsigned char)buffer[i]);
		fputc('\n', stderr);
	}
	CHECK(count == 0);
#if SYNCTERM_PLATFORM_WINDOWS
	CHECK(strstr(buffer, "shell-test") != NULL);
#else
	CHECK(received == 10 && memcmp(buffer, "shell-test", 10) == 0);
#endif
	CHECK(st_connection_close(connection) == 0);
	CHECK((atomic_load(&log.events) & (LOG_SHELL_START | LOG_SHELL_EXIT | LOG_CLOSED))
	    == (LOG_SHELL_START | LOG_SHELL_EXIT | LOG_CLOSED));
	CHECK(!atomic_load(&log.leaked));
	CHECK(atomic_load(&log.exit_status) == 0);
	atomic_store(&log.events, 0);
#if SYNCTERM_PLATFORM_WINDOWS
	strcpy(profile.address, "cmd.exe /d /c exit 7");
#else
	strcpy(profile.address, "exit 7");
#endif
	CHECK(st_connection_connect_shell(&connection, &profile, &geometry,
	    NULL, &hooks, &error) == 0);
	received = 0;
	/* ConPTY may emit terminal setup/teardown even for a command that
	 * writes nothing. Drain that stream, but still require actual EOF. */
	for (unsigned attempt = 0; attempt < 50; ++attempt) {
		count = st_connection_receive(connection, buffer, sizeof(buffer), 100);
		if (count == IO_ERROR_TIMED_OUT)
			continue;
		if (count <= 0)
			break;
		received += (size_t)count;
	}
	if (count != 0)
		fprintf(stderr, "exit-only shell: last read=%zd bytes=%zu connected=%d\n",
		    count, received, st_connection_is_connected(connection));
	CHECK(count == 0);
#if !SYNCTERM_PLATFORM_WINDOWS
	CHECK(received == 0);
#endif
	CHECK(st_connection_close(connection) == 0);
	CHECK((atomic_load(&log.events) & LOG_SHELL_EXIT) != 0);
	CHECK(atomic_load(&log.exit_status) == 7 && !atomic_load(&log.leaked));
#else
	count = st_connection_connect_shell(&connection, &profile, &geometry,
	    NULL, &hooks, &error);
	CHECK(count == IO_ERROR_NOT_SUPPORTED);
#endif
	st_profile_dispose(&profile);
	return 0;
}

static int
test_shell_drain(void)
{
#if SYNCTERM_PLATFORM_WINDOWS && ST_CONNECTION_TEST_HAVE_SHELL
	struct st_screen_geometry geometry = {80, 25, 0, 0};
	struct st_profile profile;
	st_connection_t *connection = NULL;
	struct st_deadline deadline;
	char tail[8192] = {0};
	size_t used = 0;
	bool marker_seen = false;
	ssize_t count = -1;

	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_SHELL;
	profile.screen_mode = ST_SCREEN_MODE_CURRENT;
	strcpy(profile.address, "cmd.exe /d /q /c "
	    "for /L %i in (1,1,4096) do @echo 0123456789abcdef0123456789abcdef"
	    " & echo drain-complete");
	int status = st_connection_connect_shell(&connection, &profile,
	    &geometry, NULL, NULL, NULL);
	if (status == IO_ERROR_NOT_SUPPORTED) {
		st_profile_dispose(&profile);
		return 0;
	}
	CHECK(status == 0);
	CHECK(st_deadline_start(&deadline, 15000) == 0);
	for (;;) {
		int64_t remaining;
		CHECK(st_deadline_remaining_ms(&deadline, &remaining) == 0);
		if (remaining == 0)
			break;
		count = st_connection_receive(connection, tail + used,
		    sizeof(tail) - 1 - used, remaining);
		if (count <= 0)
			break;
		used += (size_t)count;
		tail[used] = '\0';
		marker_seen |= strstr(tail, "drain-complete") != NULL;
		if (used > sizeof(tail) / 2) {
			/* Preserve a marker split across receive boundaries. */
			memmove(tail, tail + used - 32, 32);
			used = 32;
		}
	}
	if (count != 0 || !marker_seen)
		fprintf(stderr, "ConPTY final output: read=%zd marker=%d\n", count, marker_seen);
	CHECK(st_connection_close(connection) == 0);
	st_profile_dispose(&profile);
	CHECK(count == 0 && marker_seen);
#endif
	return 0;
}

static int
test_shell_shutdown(void)
{
#if SYNCTERM_PLATFORM_WINDOWS && ST_CONNECTION_TEST_HAVE_SHELL
	static const struct timespec pending_pause = {0, 25000000};
	static const struct timespec output_pause = {0, 250000000};
	struct st_screen_geometry geometry = {80, 25, 0, 0};
	struct st_connection_error error;
	struct st_profile profile;
	st_connection_t *connection = NULL;
	int status;

	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_SHELL;
	profile.screen_mode = ST_SCREEN_MODE_CURRENT;
	strcpy(profile.address, "cmd.exe /d /q");
	for (unsigned int attempt = 0; attempt < 8; ++attempt) {
		status = st_connection_connect_shell(&connection, &profile,
		    &geometry, NULL, NULL, &error);
		if (status == IO_ERROR_NOT_SUPPORTED) {
			st_profile_dispose(&profile);
			return 0;
		}
		CHECK(status == 0);
		(void)st_thread_sleep(&pending_pause, NULL);
		CHECK(st_connection_close(connection) == 0);
		connection = NULL;
	}
	strcpy(profile.address,
	    "cmd.exe /d /q /c for /L %i in (1,1,2147483647) do "
	    "@echo 0123456789abcdef0123456789abcdef");
	CHECK(st_connection_connect_shell(&connection, &profile, &geometry,
	    NULL, NULL, &error) == 0);
	(void)st_thread_sleep(&output_pause, NULL);
	CHECK(st_connection_close(connection) == 0);
	st_profile_dispose(&profile);
#endif
	return 0;
}

static int
test_shell_line_discipline(void)
{
#if !SYNCTERM_PLATFORM_WINDOWS && ST_CONNECTION_TEST_HAVE_SHELL
	struct st_profile profile;
	struct st_screen_geometry geometry = {80, 25, 0, 0};
	st_connection_t *connection = NULL;
	char buffer[32];
	size_t total = 0;

	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_SHELL;
	profile.screen_mode = ST_SCREEN_MODE_CURRENT;
	strcpy(profile.address, "printf 'a\\nb\\n'");
	CHECK(st_connection_connect_shell(&connection, &profile, &geometry,
	    NULL, NULL, NULL) == 0);
	while (total < sizeof(buffer)) {
		ssize_t count = st_connection_receive(connection, buffer + total,
		    sizeof(buffer) - total, 1000);
		if (count <= 0)
			break;
		total += (size_t)count;
	}
	CHECK(st_connection_close(connection) == 0);
	st_profile_dispose(&profile);
	if (total != 6)
		fprintf(stderr, "shell newline output: %zu bytes, expected 6 with CRLF\n",
		    total);
	CHECK(total == 6 && memcmp(buffer, "a\r\nb\r\n", 6) == 0);
#endif
	return 0;
}

#if !SYNCTERM_PLATFORM_WINDOWS && ST_CONNECTION_TEST_HAVE_SHELL
static int
receive_until(st_connection_t *connection, char *buffer, size_t capacity,
	const char *expected)
{
	size_t total = 0;

	while (total + 1 < capacity) {
		ssize_t count = st_connection_receive(connection, buffer + total,
		    capacity - total - 1, 2000);

		if (count <= 0)
			break;
		total += (size_t)count;
		buffer[total] = '\0';
		if (strstr(buffer, expected) != NULL)
			return 0;
	}
	buffer[total] = '\0';
	fprintf(stderr, "shell fixture did not emit %s; received: %s\n",
	    expected, buffer);
	return 1;
}

static int
make_fixture_command(char *result, size_t capacity, const char *path)
{
	static const char prefix[] = "exec '";
	size_t used = sizeof(prefix) - 1;

	if (capacity <= used)
		return 1;
	memcpy(result, prefix, used);
	for (; *path != '\0'; ++path) {
		if (*path == '\'') {
			static const char escaped[] = "'\\''";

			if (sizeof(escaped) - 1 > capacity - used - 1)
				return 1;
			memcpy(result + used, escaped, sizeof(escaped) - 1);
			used += sizeof(escaped) - 1;
		}
		else {
			if (used + 1 >= capacity)
				return 1;
			result[used++] = *path;
		}
	}
	if (used + 2 > capacity)
		return 1;
	result[used++] = '\'';
	result[used] = '\0';
	return 0;
}

struct saved_environment {
	char *value;
	bool present;
};

static int
save_environment(const char *name, struct saved_environment *saved)
{
	const char *value = getenv(name);

	saved->value = NULL;
	saved->present = value != NULL;
	if (value != NULL) {
		size_t length = strlen(value) + 1;

		saved->value = malloc(length);
		if (saved->value == NULL)
			return 1;
		memcpy(saved->value, value, length);
	}
	return 0;
}

static int
restore_environment(const char *name, struct saved_environment *saved)
{
	int result = saved->present ? setenv(name, saved->value, 1)
	    : unsetenv(name);

	free(saved->value);
	saved->value = NULL;
	return result != 0;
}

static int
test_shell_environment(const char *fixture)
{
	static const char expected[] =
	    "TERM=syncterm-test\r\n"
	    "COLUMNS=80\r\n"
	    "LINES=24\r\n"
	    "TERMCAP=syncterm|SyncTERM:co#80:li#24:\r\n"
	    "MM_CHARSET=CP437\r\n";
	static const struct st_connection_shell_environment supplied = {
		.termcap = "syncterm|SyncTERM:co#80:li#24:",
		.mm_charset = "CP437",
	};
	static const struct st_connection_shell_environment omitted = {
		.termcap = NULL,
		.mm_charset = "",
	};
	struct saved_environment saved_termcap;
	struct saved_environment saved_charset;
	struct st_screen_geometry geometry = {80, 25, 0, 0};
	struct st_profile profile;
	st_connection_t *connection = NULL;
	char buffer[512];
	bool environment_saved = false;
	int failed = 0;

	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_SHELL;
	profile.screen_mode = ST_SCREEN_MODE_CURRENT;
	strcpy(profile.terminal_name, "syncterm-test");
	if (fixture == NULL || make_fixture_command(profile.address,
	    sizeof(profile.address), fixture) != 0) {
		fprintf(stderr, "unable to construct shell fixture command\n");
		failed = 1;
		goto cleanup;
	}
	if (st_connection_connect_shell(&connection, &profile, &geometry,
	    &supplied, NULL, NULL) != 0
	    || receive_until(connection, buffer, sizeof(buffer), expected) != 0) {
		fprintf(stderr, "shell fixture did not receive supplied environment\n");
		failed = 1;
		goto cleanup;
	}
	if (st_connection_close(connection) != 0) {
		failed = 1;
		connection = NULL;
		goto cleanup;
	}
	connection = NULL;
	if (save_environment("TERMCAP", &saved_termcap) != 0
	    || save_environment("MM_CHARSET", &saved_charset) != 0) {
		free(saved_termcap.value);
		fprintf(stderr, "unable to save shell test environment\n");
		failed = 1;
		goto cleanup;
	}
	environment_saved = true;
	if (setenv("TERMCAP", "inherited-termcap", 1) != 0
	    || setenv("MM_CHARSET", "inherited-charset", 1) != 0
	    || st_connection_connect_shell(&connection, &profile, &geometry,
	    &omitted, NULL, NULL) != 0
	    || receive_until(connection, buffer, sizeof(buffer),
	    "TERMCAP=<unset>\r\nMM_CHARSET=<unset>\r\n") != 0) {
		fprintf(stderr, "shell fixture inherited omitted environment\n");
		failed = 1;
	}

cleanup:
	if (connection != NULL && st_connection_close(connection) != 0)
		failed = 1;
	if (environment_saved) {
		if (restore_environment("TERMCAP", &saved_termcap) != 0)
			failed = 1;
		if (restore_environment("MM_CHARSET", &saved_charset) != 0)
			failed = 1;
	}
	st_profile_dispose(&profile);
	return failed;
}

static int
test_shell_default_executable(const char *fixture)
{
	static const char fallback_command[] =
	    "printf '\\146\\141\\154\\154\\142\\141\\143\\153\\055"
	    "\\157\\153\\n'\n";
	struct saved_environment saved_shell;
	struct st_screen_geometry geometry = {80, 25, 0, 0};
	struct st_profile profile;
	st_connection_t *connection = NULL;
	char link_name[128];
	char buffer[512];
	char expected[192];
	bool shell_saved = false;
	bool link_created = false;
	int failed = 0;

	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_SHELL;
	profile.screen_mode = ST_SCREEN_MODE_CURRENT;
	if (fixture == NULL || snprintf(link_name, sizeof(link_name),
	    "./st connection shell default-%ld", (long)getpid()) < 0
	    || save_environment("SHELL", &saved_shell) != 0) {
		fprintf(stderr, "unable to prepare default shell test\n");
		failed = 1;
		goto cleanup;
	}
	shell_saved = true;
	if (symlink(fixture, link_name) != 0) {
		fprintf(stderr, "unable to create default shell fixture link\n");
		failed = 1;
		goto cleanup;
	}
	link_created = true;
	if (snprintf(expected, sizeof(expected), "ARGV0=%s\r\n", link_name)
	    < 0) {
		failed = 1;
		goto cleanup;
	}
	if (setenv("SHELL", link_name, 1) != 0
	    || st_connection_connect_shell(&connection, &profile, &geometry,
	    NULL, NULL, NULL) != 0
	    || receive_until(connection, buffer, sizeof(buffer), expected) != 0) {
		fprintf(stderr, "SHELL was not executed as a literal path\n");
		failed = 1;
		goto cleanup;
	}
	if (st_connection_close(connection) != 0) {
		failed = 1;
		connection = NULL;
		goto cleanup;
	}
	connection = NULL;
	if (unsetenv("SHELL") != 0
	    || st_connection_connect_shell(&connection, &profile, &geometry,
	    NULL, NULL, NULL) != 0
	    || st_connection_send(connection, fallback_command,
	    sizeof(fallback_command) - 1, 1000)
	    != (ssize_t)(sizeof(fallback_command) - 1)
	    || receive_until(connection, buffer, sizeof(buffer),
	    "fallback-ok\r\n") != 0) {
		fprintf(stderr, "empty SHELL did not fall back to /bin/sh\n");
		failed = 1;
	}

cleanup:
	if (connection != NULL && st_connection_close(connection) != 0)
		failed = 1;
	if (link_created && unlink(link_name) != 0)
		failed = 1;
	if (shell_saved && restore_environment("SHELL", &saved_shell) != 0)
		failed = 1;
	st_profile_dispose(&profile);
	return failed;
}
#endif

static int
test_shell_interactive(const char *fixture)
{
#if !SYNCTERM_PLATFORM_WINDOWS && ST_CONNECTION_TEST_HAVE_SHELL
	struct st_profile profile;
	struct st_screen_geometry geometry = {80, 25, 0, 0};
	st_connection_t *connection = NULL;
	char buffer[256];
	int failed = 0;

	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_SHELL;
	profile.screen_mode = ST_SCREEN_MODE_CURRENT;
	if (fixture == NULL || make_fixture_command(profile.address,
	    sizeof(profile.address), fixture) != 0) {
		fprintf(stderr, "unable to construct shell fixture command\n");
		failed = 1;
		goto cleanup;
	}
	if (st_connection_connect_shell(&connection, &profile, &geometry,
	    NULL, NULL, NULL) != 0) {
		fprintf(stderr, "unable to connect to shell fixture\n");
		failed = 1;
		goto cleanup;
	}
	if (receive_until(connection, buffer, sizeof(buffer),
	    "flags-ok\r\nready\r\n") != 0) {
		failed = 1;
		goto cleanup;
	}
	if (st_connection_send(connection, "\x13" "abc", 4, 1000) != 4
	    || receive_until(connection, buffer, sizeof(buffer), "abc") != 0) {
		fprintf(stderr, "shell fixture did not echo input immediately\n");
		failed = 1;
		goto cleanup;
	}
	if (st_connection_send(connection, "\r", 1, 1000) != 1
	    || receive_until(connection, buffer, sizeof(buffer),
	    "line-ok\r\nsignal-ready\r\n") != 0) {
		fprintf(stderr, "shell fixture did not accept Return canonically\n");
		failed = 1;
		goto cleanup;
	}
	if (st_connection_send(connection, "\x03", 1, 1000) != 1
	    || receive_until(connection, buffer, sizeof(buffer),
	    "signal-ok\r\n") != 0) {
		fprintf(stderr, "shell fixture did not receive terminal SIGINT\n");
		failed = 1;
	}

cleanup:
	if (connection != NULL && st_connection_close(connection) != 0)
		failed = 1;
	st_profile_dispose(&profile);
	return failed;
#else
	(void)fixture;
	return 0;
#endif
}

struct partial_input_state {
	atomic_uint reads;
};

static int
wait_for_full_input_queue(st_connection_t *connection)
{
	const struct timespec pause = {0, 1000000};

	for (unsigned int i = 0; i < 2000; ++i) {
		if (st_connection_pending_raw(connection)
		    == ST_CONNECTION_BUFFER_SIZE)
			return 0;
		(void)st_thread_sleep(&pause, NULL);
	}
	return 1;
}

static void
partial_input_bind(void *state, st_connection_t *connection)
{
	(void)state;
	/* Bind runs before the workers start. Leave one byte of queue space. */
	memset(connection->input.data, 'a', ST_CONNECTION_BUFFER_SIZE - 1);
	connection->input.count = ST_CONNECTION_BUFFER_SIZE - 1;
}

static ssize_t
partial_input_read(void *context, void *data, size_t size, int64_t timeout_ms)
{
	struct partial_input_state *state = context;
	const struct timespec pause = {0, 1000000};

	(void)timeout_ms;
	if (atomic_fetch_add(&state->reads, 1) == 0) {
		if (size < 4)
			return IO_ERROR_INVALID_ARGUMENT;
		memcpy(data, "bcde", 4);
		return 4;
	}
	(void)st_thread_sleep(&pause, NULL);
	return IO_ERROR_TIMED_OUT;
}

static ssize_t
partial_input_write(void *context, const void *data, size_t size,
	int64_t timeout_ms)
{
	(void)context;
	(void)data;
	(void)timeout_ms;
	return (ssize_t)size;
}

static void
partial_input_destroy(void *context)
{
	(void)context;
}

static int
test_partial_input_queue(void)
{
	static const struct st_connection_transport_ops operations = {
		.bind = partial_input_bind,
		.read = partial_input_read,
		.write = partial_input_write,
		.destroy = partial_input_destroy,
	};
	struct partial_input_state state;
	st_connection_t *connection = NULL;
	unsigned char received[ST_CONNECTION_BUFFER_SIZE + 3];
	size_t total = 0;

	atomic_init(&state.reads, 0);
	CHECK(st_connection_start(&connection, &state, &operations, "test",
	    NULL, NULL, NULL) == 0);
	/* Wait for the worker to fill the last queue byte before draining it.
	 * A correct worker may block here retaining the other three bytes. */
	CHECK(wait_for_full_input_queue(connection) == 0);
	CHECK(atomic_load(&state.reads) == 1);
	while (total < sizeof(received)) {
		ssize_t count = st_connection_receive_raw(connection, received + total,
		    sizeof(received) - total, 1000);
		if (count <= 0)
			break;
		total += (size_t)count;
	}
	CHECK(st_connection_close(connection) == 0);
	if (total != sizeof(received))
		fprintf(stderr, "input queue retained %zu of %zu bytes\n",
		    total, sizeof(received));
	CHECK(total == sizeof(received));
	for (size_t i = 0; i < ST_CONNECTION_BUFFER_SIZE - 1; ++i)
		CHECK(received[i] == 'a');
	CHECK(memcmp(received + ST_CONNECTION_BUFFER_SIZE - 1, "bcde", 4) == 0);
	return 0;
}

static int
test_partial_input_shutdown(void)
{
	static const struct st_connection_transport_ops operations = {
		.bind = partial_input_bind,
		.read = partial_input_read,
		.write = partial_input_write,
		.destroy = partial_input_destroy,
	};
	struct partial_input_state state;
	st_connection_t *connection = NULL;

	atomic_init(&state.reads, 0);
	CHECK(st_connection_start(&connection, &state, &operations, "test",
	    NULL, NULL, NULL) == 0);
	CHECK(wait_for_full_input_queue(connection) == 0);
	CHECK(atomic_load(&state.reads) == 1);
	CHECK(st_connection_close(connection) == 0);
	return 0;
}

static int
test_partial_input(void)
{
	int failures = test_partial_input_queue();

	failures += test_partial_input_shutdown();
	return failures;
}

struct auxiliary_state {
	atomic_bool write_failed, eof, interrupted, destroyed;
};
static ssize_t auxiliary_read(void *context, void *data, size_t size, int64_t timeout)
{
	struct auxiliary_state *s = context;
	(void)data; (void)size; (void)timeout;
	if (atomic_load(&s->eof)) return 0;
	const struct timespec pause = {.tv_nsec = 1000000};
	(void)st_thread_sleep(&pause, NULL);
	return IO_ERROR_WOULD_BLOCK;
}
static ssize_t auxiliary_write(void *context, const void *data, size_t size, int64_t timeout)
{
	(void)data; (void)size; (void)timeout;
	atomic_store(&((struct auxiliary_state *)context)->write_failed, true);
	return IO_ERROR_CONNECTION_RESET;
}
static bool auxiliary_alive(void *context)
{ return !atomic_load(&((struct auxiliary_state *)context)->interrupted); }
static void auxiliary_interrupt(void *context)
{ atomic_store(&((struct auxiliary_state *)context)->interrupted, true); }
static void auxiliary_destroy(void *context)
{ atomic_store(&((struct auxiliary_state *)context)->destroyed, true); }
static int test_auxiliary_close(void)
{
	struct auxiliary_state state;
	atomic_init(&state.write_failed, false); atomic_init(&state.eof, false);
	atomic_init(&state.interrupted, false); atomic_init(&state.destroyed, false);
	const struct st_connection_transport_ops operations = {
		.retain_after_eof = true, .auxiliary_alive = auxiliary_alive,
		.read = auxiliary_read, .write = auxiliary_write,
		.interrupt = auxiliary_interrupt, .destroy = auxiliary_destroy,
	};
	st_connection_t *connection = NULL;
	CHECK(st_connection_start(&connection, &state, &operations, "auxiliary test", NULL, NULL, NULL) == 0);
	CHECK(st_connection_send(connection, "x", 1, 1000) == 1);
	const struct timespec pause = {.tv_nsec = 1000000};
	for (unsigned i = 0; i < 3000 && !atomic_load(&state.write_failed); ++i)
		(void)st_thread_sleep(&pause, NULL);
	CHECK(atomic_load(&state.write_failed) && !atomic_load(&state.interrupted));
	atomic_store(&state.eof, true);
	for (unsigned i = 0; i < 3000 && st_connection_is_connected(connection); ++i)
		(void)st_thread_sleep(&pause, NULL);
	CHECK(!st_connection_is_connected(connection) && st_connection_auxiliary_alive(connection));
	CHECK(!atomic_load(&state.interrupted));
	CHECK(st_connection_close(connection) == 0);
	CHECK(atomic_load(&state.interrupted) && atomic_load(&state.destroyed));
	return 0;
}

int
main(int argc, char **argv)
{
	const char *fixture = NULL;
	if (argc == 2 && strcmp(argv[1], "--auxiliary-close") == 0)
		return test_auxiliary_close();

	if (argc == 2 && strcmp(argv[1], "--partial-input") == 0)
		return test_partial_input() != 0;
	if (argc == 2 && strcmp(argv[1], "--shell-shutdown") == 0)
		return test_shell_shutdown() != 0;
	if (argc == 2 && strcmp(argv[1], "--shell-drain") == 0)
		return test_shell_drain() != 0;
	if (argc == 2 && strcmp(argv[1], "--rlogin-flow") == 0)
		return test_rlogin_flow(false);
	if (argc == 2 && strcmp(argv[1], "--rlogin-backpressure") == 0)
		return test_rlogin_flow(true);
	if (argc == 2 && strcmp(argv[1], "--rlogin-setup") == 0)
		return test_rlogin_setup();
#if !SYNCTERM_PLATFORM_WINDOWS && ST_CONNECTION_TEST_HAVE_SHELL
	if (argc == 2)
		fixture = argv[1];
	else
#else
	if (argc != 1)
#endif
	{
		fprintf(stderr,
		    "usage: %s [--partial-input|--shell-shutdown|--shell-drain|"
		    "--rlogin-flow|--rlogin-backpressure|--rlogin-setup] "
		    "[shell-fixture]\n", argv[0]);
		return 1;
	}
	CHECK(test_raw() == 0);
	CHECK(test_rlogin_flush() == 0);
	CHECK(test_rlogin() == 0);
	CHECK(test_shell() == 0);
	int failures = test_shell_line_discipline();
#if !SYNCTERM_PLATFORM_WINDOWS && ST_CONNECTION_TEST_HAVE_SHELL
	failures += test_shell_environment(fixture);
	failures += test_shell_default_executable(fixture);
#endif
	failures += test_shell_interactive(fixture);
	failures += test_partial_input();
	failures += test_auxiliary_close();
	return failures != 0;
}
