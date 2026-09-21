/* Webget transport ownership tests with link-time fakes. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_webget.h"

#include "filesystem.h"
#include "io_error.h"
#include "socket.h"
#include "tcp_client.h"

#include <stdio.h>

#ifndef ST_WEBGET_TRANSPORT_TEST_DIRECTORY
#error "ST_WEBGET_TRANSPORT_TEST_DIRECTORY is required"
#endif

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "failed: %s:%d: %s\n", __FILE__, __LINE__, \
		    #expression); \
		return 1; \
	} \
} while (0)

#define FAKE_SOCKET ((socket_handle_t)42)

struct st_tls_session {
	int unused;
};

struct fake_state {
	bool tls_success;
	bool raw_closed;
	size_t runtime_init_calls;
	size_t runtime_cleanup_calls;
	size_t connect_calls;
	size_t raw_close_calls;
	size_t repeated_close_calls;
	size_t tls_open_calls;
	size_t tls_close_calls;
};

static struct fake_state fake;

static struct st_tls_session fake_session;

static void
fake_reset(bool tls_success)
{
	fake = (struct fake_state){0};
	fake.tls_success = tls_success;
}

int
socket_runtime_init(void)
{
	fake.runtime_init_calls++;
	return 0;
}

int
socket_runtime_cleanup(void)
{
	fake.runtime_cleanup_calls++;
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
	if (socket != FAKE_SOCKET)
		return IO_ERROR_INVALID_ARGUMENT;
	fake.raw_close_calls++;
	if (fake.raw_closed)
		fake.repeated_close_calls++;
	fake.raw_closed = true;
	return 0;
}

ssize_t
socket_send(socket_handle_t socket, const void *buffer, size_t size)
{
	(void)socket;
	(void)buffer;
	(void)size;
	return IO_ERROR_IO;
}

ssize_t
socket_receive(socket_handle_t socket, void *buffer, size_t size,
    unsigned flags)
{
	(void)socket;
	(void)buffer;
	(void)size;
	(void)flags;
	return IO_ERROR_IO;
}

int
socket_wait(socket_handle_t socket, unsigned interests, int64_t timeout_ms,
    unsigned *events)
{
	(void)socket;
	(void)interests;
	(void)timeout_ms;
	if (events != NULL)
		*events = 0;
	return IO_ERROR_IO;
}

int
tcp_client_connect(socket_handle_t *result,
    enum tcp_client_address_family *connected_family, const char *host,
    uint16_t port, const struct tcp_client_options *options)
{
	(void)host;
	(void)port;
	(void)options;
	fake.connect_calls++;
	*result = FAKE_SOCKET;
	if (connected_family != NULL)
		*connected_family = TCP_CLIENT_ADDRESS_IPV4;
	return 0;
}

int
st_tls_client_open(st_tls_session_t **result, socket_handle_t socket,
    const struct st_tls_client_options *options)
{
	(void)options;
	if (socket != FAKE_SOCKET)
		return IO_ERROR_INVALID_ARGUMENT;
	fake.tls_open_calls++;
	if (!fake.tls_success)
		return IO_ERROR_NOT_SUPPORTED;
	*result = &fake_session;
	return 0;
}

int
st_tls_send(st_tls_session_t *session, const void *data, size_t size,
    int64_t timeout_ms, size_t *sent)
{
	(void)session;
	(void)data;
	(void)size;
	(void)timeout_ms;
	if (sent != NULL)
		*sent = 0;
	return IO_ERROR_IO;
}

int
st_tls_receive(st_tls_session_t *session, void *data, size_t size,
    int64_t timeout_ms, size_t *received)
{
	(void)session;
	(void)data;
	(void)size;
	(void)timeout_ms;
	if (received != NULL)
		*received = 0;
	return IO_ERROR_IO;
}

void
st_tls_close(st_tls_session_t *session)
{
	if (session == &fake_session)
		fake.tls_close_calls++;
}

static bool
cancel_at_phase(const struct st_webget_progress *progress, void *context)
{
	const enum st_webget_phase *phase = context;

	return progress->phase != *phase;
}

static void
clean_fixture(void)
{
	static const char *const keys[] = {"tls-cancel", "tls-fail", "tls-ok"};
	char path[sizeof(ST_WEBGET_TRANSPORT_TEST_DIRECTORY) + 64];

	for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
		(void)snprintf(path, sizeof(path), "%s/%s.cacheinfo.lock",
		    ST_WEBGET_TRANSPORT_TEST_DIRECTORY, keys[i]);
		(void)fs_remove_file(path);
	}
	(void)fs_remove_directory(ST_WEBGET_TRANSPORT_TEST_DIRECTORY);
}

static int
run_request(const char *key, enum st_webget_phase cancel_phase)
{
	struct st_webget_request request = {
		.url = "https://example.invalid/resource",
		.cache_directory = ST_WEBGET_TRANSPORT_TEST_DIRECTORY,
		.cache_key = key,
		.output_name = "unused.out",
		.connect_timeout_ms = 1000,
		.io_timeout_ms = 1000,
		.progress = cancel_at_phase,
		.progress_context = &cancel_phase,
	};
	struct st_webget_result result;

	return st_webget(&result, &request);
}

int
main(void)
{
	clean_fixture();

	fake_reset(false);
	CHECK(run_request("tls-cancel", ST_WEBGET_TLS) == IO_ERROR_CANCELLED);
	CHECK(fake.runtime_init_calls == 1 && fake.runtime_cleanup_calls == 1);
	CHECK(fake.connect_calls == 1 && fake.tls_open_calls == 0);
	CHECK(fake.raw_close_calls == 1 && fake.repeated_close_calls == 0);
	CHECK(fake.tls_close_calls == 0);

	fake_reset(false);
	CHECK(run_request("tls-fail", ST_WEBGET_REQUESTING)
	    == IO_ERROR_NOT_SUPPORTED);
	CHECK(fake.runtime_init_calls == 1 && fake.runtime_cleanup_calls == 1);
	CHECK(fake.connect_calls == 1 && fake.tls_open_calls == 1);
	CHECK(fake.raw_close_calls == 1 && fake.repeated_close_calls == 0);
	CHECK(fake.tls_close_calls == 0);

	fake_reset(true);
	CHECK(run_request("tls-ok", ST_WEBGET_REQUESTING) == IO_ERROR_CANCELLED);
	CHECK(fake.runtime_init_calls == 1 && fake.runtime_cleanup_calls == 1);
	CHECK(fake.connect_calls == 1 && fake.tls_open_calls == 1);
	CHECK(fake.raw_close_calls == 0 && fake.repeated_close_calls == 0);
	CHECK(fake.tls_close_calls == 1);

	clean_fixture();
	return 0;
}
