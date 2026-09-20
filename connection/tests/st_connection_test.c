/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_connection.h"
#include "connection_internal.h"

#include "io_error.h"
#include "socket.h"
#include "st_threads.h"

#include <stdio.h>
#include <stdatomic.h>
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
	atomic_int stage;
};

struct log_state {
	atomic_uint entries;
	atomic_int last_error;
};

static void
test_log(void *context, enum st_connection_log_level level,
	const char *source, const char *message)
{
	struct log_state *state = context;

	(void)source;
	(void)message;
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
	static const uint8_t expected[] = {
		0, 's', 'e', 'c', 'r', 'e', 't', 0,
		'u', 's', 'e', 'r', 0,
		'a', 'n', 's', 'i', '/', '1', '1', '5', '2', '0', '0', 0,
	};
	static const uint8_t expected_window[] = {
		0xff, 0xff, 's', 's', 0, 30, 0, 100, 0, 0, 0, 0,
	};
	socket_handle_t client;
	uint8_t buffer[64];
	uint8_t urgent = 0x80;
#if !SYNCTERM_PLATFORM_WINDOWS
	uint8_t cooked = 0x20;
#endif

	client = accept(state->listener, NULL, NULL);
	if (!socket_handle_is_valid(client))
		return 1;
	atomic_store(&state->stage, 1);
	if (receive_exact(client, buffer, sizeof(expected)) != 0
	    || memcmp(buffer, expected, sizeof(expected)) != 0)
		goto failed;
	atomic_store(&state->stage, 2);
	while (!atomic_load(&state->allow_urgent))
		st_thread_yield();
#if SYNCTERM_PLATFORM_WINDOWS
	if (send(client, (const char *)&urgent, 1, MSG_OOB) != 1)
#else
	if (send(client, &urgent, 1, MSG_OOB) != 1)
#endif
		goto failed;
	atomic_store(&state->stage, 3);
	if (receive_exact(client, buffer, sizeof(expected_window)) != 0
	    || memcmp(buffer, expected_window, sizeof(expected_window)) != 0)
		goto failed;
	atomic_store(&state->stage, 4);

#if !SYNCTERM_PLATFORM_WINDOWS
	if (send(client, &cooked, 1, MSG_OOB) != 1)
		goto failed;
#endif
	if (socket_send(client, "cooked", 6) != 6)
		goto failed;
	if (receive_exact(client, buffer, 2) != 0
	    || memcmp(buffer, "AB", 2) != 0)
		goto failed;
	if (socket_send(client, "ready", 5) != 5)
		goto failed;
	atomic_store(&state->stage, 5);
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
	CHECK(st_connection_connect_raw(&connection, &profile, NULL, &error) == 0);
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
test_rlogin(void)
{
	struct server_state server;
	struct st_profile profile;
	struct st_screen_geometry geometry = {80, 25, 0, 0};
	struct st_connection_error error;
	struct log_state log_state;
	struct st_connection_hooks hooks = {0};
	st_connection_t *connection = NULL;
	st_thread_t thread;
	char buffer[8];
	uint16_t port;
	int thread_result;
	ssize_t received;

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
	geometry.columns = 100;
	geometry.rows = 30;
	CHECK(st_connection_update_terminal_size(connection, &geometry) == 0);
	CHECK(st_connection_update_terminal_size(connection, &geometry) == 0);
	atomic_store(&server.allow_urgent, true);
	received = st_connection_receive(connection, buffer, 6, 2000);
	if (received != 6)
		fprintf(stderr, "rlogin receive=%zd server-stage=%d error-log=%d\n",
		    received, atomic_load(&server.stage),
		    atomic_load(&log_state.last_error));
	CHECK(received == 6);
	CHECK(memcmp(buffer, "cooked", 6) == 0);
#if SYNCTERM_PLATFORM_WINDOWS
	CHECK(st_connection_set_binary_mode(connection, false) == 0);
#endif
	CHECK(st_connection_send(connection, "\x13" "AB" "\x11", 4, 1000)
	    == 4);
	CHECK(st_connection_receive(connection, buffer, 5, 2000) == 5);
	CHECK(memcmp(buffer, "ready", 5) == 0);
	CHECK(st_connection_close(connection) == 0);
	CHECK(st_thread_join(thread, &thread_result) == ST_THREAD_SUCCESS);
	CHECK(thread_result == 0);
	CHECK(socket_close(server.listener) == 0);
	CHECK(socket_runtime_cleanup() == 0);
	CHECK(atomic_load(&log_state.entries) >= 1);
	st_profile_dispose(&profile);
	return 0;
}

static int
test_shell(void)
{
	struct st_profile profile;
	struct st_screen_geometry geometry = {80, 25, 0, 0};
	struct st_connection_error error;
	st_connection_t *connection = NULL;
	char buffer[64];
	ssize_t count;

	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_SHELL;
	profile.screen_mode = ST_SCREEN_MODE_CURRENT;
#if SYNCTERM_PLATFORM_WINDOWS
	strcpy(profile.address, "cmd.exe /d /c <nul set /p =shell-test");
#else
	strcpy(profile.address, "printf shell-test");
#endif
#if ST_CONNECTION_TEST_HAVE_SHELL
	count = st_connection_connect_shell(&connection, &profile, &geometry,
	    NULL, &error);
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
	count = st_connection_receive(connection, buffer, sizeof(buffer), 1000);
	CHECK(count == 10);
	CHECK(memcmp(buffer, "shell-test", 10) == 0);
	CHECK(st_connection_close(connection) == 0);
#else
	count = st_connection_connect_shell(&connection, &profile, &geometry,
	    NULL, &error);
	CHECK(count == IO_ERROR_NOT_SUPPORTED);
#endif
	st_profile_dispose(&profile);
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
	    NULL, NULL) == 0);
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

int
main(int argc, char **argv)
{
	if (argc == 2 && strcmp(argv[1], "--partial-input") == 0)
		return test_partial_input() != 0;
	if (argc != 1) {
		fprintf(stderr, "usage: %s [--partial-input]\n", argv[0]);
		return 1;
	}
	CHECK(test_raw() == 0);
	CHECK(test_rlogin() == 0);
	CHECK(test_shell() == 0);
	int failures = test_shell_line_discipline();
	failures += test_partial_input();
	return failures != 0;
}
