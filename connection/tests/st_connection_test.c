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
	LOG_SHELL_START = 512, LOG_SHELL_EXIT = 1024, LOG_EOF = 2048
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
	unsigned events = 0;
	if (strstr(message, "secret") || strstr(message, "shell-test")
	    || strstr(message, "printf ") || strstr(message, "cmd.exe"))
		atomic_store(&state->leaked, true);
	if (strstr(message, "resolving remote endpoint")) events |= LOG_RESOLVE;
	if (strstr(message, "trying address family=")) events |= LOG_ATTEMPT;
	if (strstr(message, "connection established family=")) events |= LOG_CONNECTED;
	if (strstr(message, "sending RFC 1282 handshake")) events |= LOG_HANDSHAKE;
	if (strstr(message, "urgent control=")) events |= LOG_URGENT;
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

	for (unsigned attempts = 0; attempts < 3000; ++attempts) {
		if (atomic_load(&state->stage) >= stage)
			return 0;
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

	for (unsigned attempts = 0; attempts < 3000; ++attempts) {
		if (st_connection_pending_raw(connection) == count)
			return 0;
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
	uint8_t buffer[64];

	client = accept(state->listener, NULL, NULL);
	if (!socket_handle_is_valid(client))
		return 1;
	atomic_store(&state->stage, 1);
	if (receive_exact(client, buffer, sizeof(rlogin_handshake)) != 0
	    || memcmp(buffer, rlogin_handshake, sizeof(rlogin_handshake)) != 0)
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
	if (socket_send(client, "cooked", 6) != 6)
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
	char buffer[8];
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
	CHECK(st_connection_set_binary_mode(connection, false) == 0);
	CHECK(connection_receive_exact(connection, buffer, 6) == 0);
	CHECK(memcmp(buffer, "cooked", 6) == 0);
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
test_shell(void)
{
	struct log_state log = {0};
	struct st_connection_hooks hooks = {.log = test_log, .log_context = &log};
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
	count = st_connection_receive(connection, buffer, sizeof(buffer), 1000);
	CHECK(count == 10);
	CHECK(memcmp(buffer, "shell-test", 10) == 0);
	CHECK(st_connection_receive(connection, buffer, sizeof(buffer), 1000) == 0);
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
	CHECK(st_connection_receive(connection, buffer, sizeof(buffer), 1000) == 0);
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
#if !SYNCTERM_PLATFORM_WINDOWS && ST_CONNECTION_TEST_HAVE_SHELL
	if (argc == 2)
		fixture = argv[1];
	else
#else
	if (argc != 1)
#endif
	{
		fprintf(stderr,
		    "usage: %s [--partial-input|--shell-shutdown] "
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
