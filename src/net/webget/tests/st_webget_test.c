/* Local HTTP integration tests for the conditional downloader. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_webget.h"

#include "filesystem.h"
#include "io_error.h"
#include "socket.h"
#include "st_threads.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if SYNCTERM_PLATFORM_WINDOWS
	#include <ws2tcpip.h>
	typedef int test_socklen_t;
#else
	#include <arpa/inet.h>
	#include <netinet/in.h>
	#include <sys/socket.h>
	typedef socklen_t test_socklen_t;
#endif

#ifndef ST_WEBGET_TEST_DIRECTORY
#error "ST_WEBGET_TEST_DIRECTORY is required"
#endif

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "failed: %s:%d: %s\n", __FILE__, __LINE__, \
		    #expression); \
		return 1; \
	} \
} while (0)

struct server {
	socket_handle_t listener;
	const char *responses[2];
	size_t response_count;
	char requests[2][4096];
};

static int
server_thread(void *argument)
{
	struct server *server = argument;

	for (size_t index = 0; index < server->response_count; ++index) {
		socket_handle_t client = accept(server->listener, NULL, NULL);
		size_t used = 0;
		size_t sent = 0;

		if (!socket_handle_is_valid(client))
			return __LINE__;
		while (used + 1 < sizeof(server->requests[index])) {
			ssize_t count = socket_receive(client,
			    server->requests[index] + used,
			    sizeof(server->requests[index]) - used - 1,
			    SOCKET_RECEIVE_NONE);
			if (count <= 0) {
				(void)socket_close(client);
				return __LINE__;
			}
			used += (size_t)count;
			server->requests[index][used] = '\0';
			if (strstr(server->requests[index], "\r\n\r\n") != NULL)
				break;
		}
		while (server->responses[index][sent] != '\0') {
			ssize_t count = socket_send(client,
			    server->responses[index] + sent,
			    strlen(server->responses[index] + sent));
			if (count <= 0) {
				(void)socket_close(client);
				return __LINE__;
			}
			sent += (size_t)count;
		}
		(void)socket_shutdown(client, SOCKET_SHUTDOWN_BOTH);
		(void)socket_close(client);
	}
	(void)socket_close(server->listener);
	return 0;
}

static int
start_server(struct server *server, uint16_t *port, const char *first,
    const char *second, st_thread_t *thread)
{
	struct sockaddr_in address = {0};
	test_socklen_t size = (test_socklen_t)sizeof(address);

	memset(server, 0, sizeof(*server));
	server->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (!socket_handle_is_valid(server->listener))
		return IO_ERROR_IO;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address.sin_port = 0;
	if (bind(server->listener, (const struct sockaddr *)&address,
	    (test_socklen_t)sizeof(address)) != 0
	    || listen(server->listener, 1) != 0
	    || getsockname(server->listener, (struct sockaddr *)&address,
	    &size) != 0) {
		(void)socket_close(server->listener);
		return IO_ERROR_IO;
	}
	server->responses[0] = first;
	server->responses[1] = second;
	server->response_count = second == NULL ? 1 : 2;
	*port = ntohs(address.sin_port);
	return st_thread_create(thread, server_thread, server)
	    == ST_THREAD_SUCCESS ? 0 : IO_ERROR_IO;
}

struct validation {
	const char *expected;
	int calls;
};

static int
validate_list(const char *path, void *context_ptr)
{
	FILE *stream;
	char data[7] = {0};
	struct validation *context = context_ptr;
	int status = fs_open(&stream, path, "rb");

	if (status < 0)
		return status;
	if (fread(data, 1, 6, stream) != 6 || fclose(stream) != 0)
		return IO_ERROR_IO;
	context->calls++;
	return strcmp(data, context->expected) == 0 ? 0
	    : IO_ERROR_INVALID_FORMAT;
}

static int
write_text(const char *path, const char *text)
{
	FILE *stream;
	int status = fs_open(&stream, path, "wb");

	if (status < 0)
		return status;
	if (fwrite(text, 1, strlen(text), stream) != strlen(text))
		status = IO_ERROR_IO;
	if (fclose(stream) != 0 && status == 0)
		status = IO_ERROR_IO;
	return status;
}

static bool
file_equals(const char *path, const char *expected)
{
	FILE *stream;
	char data[16] = {0};
	size_t length = strlen(expected);
	int status = fs_open(&stream, path, "rb");

	if (status < 0)
		return false;
	bool equal = length < sizeof(data)
	    && fread(data, 1, length + 1, stream) == length
	    && memcmp(data, expected, length) == 0;
	if (fclose(stream) != 0)
		return false;
	return equal;
}

static bool
cancel_network(const struct st_webget_progress *progress, void *context)
{
	(void)progress;
	(void)context;
	return false;
}

static void
clean_fixture(void)
{
	static const char *const names[] = {
		"list.lst", "directory.cacheinfo", "directory.cacheinfo.new",
		"directory.cacheinfo.lock", "directory.new", "default.lst",
		"default-freshness.cacheinfo",
		"default-freshness.cacheinfo.new",
		"default-freshness.cacheinfo.lock", "default-freshness.new",
		"legacy.lst", "legacy.cacheinfo", "legacy.cacheinfo.new",
		"legacy.cacheinfo.lock", "legacy.new",
	};
	char path[sizeof(ST_WEBGET_TEST_DIRECTORY) + 64];

	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
		(void)snprintf(path, sizeof(path), "%s/%s",
		    ST_WEBGET_TEST_DIRECTORY, names[i]);
		(void)fs_remove_file(path);
	}
	(void)fs_remove_directory(ST_WEBGET_TEST_DIRECTORY);
}

int
main(void)
{
	static const char downloaded[] =
	    "HTTP/1.1 200 OK\r\nContent-Length: 6\r\n"
	    "ETag: \"one\"\r\nCache-Control: max-age=0\r\n\r\nAlpha\n";
	static const char unchanged[] =
	    "HTTP/1.1 304 Not Modified\r\nETag: \"one\"\r\n"
	    "Cache-Control: max-age=3600\r\n\r\n";
	static const char no_freshness[] =
	    "HTTP/1.1 200 OK\r\nContent-Length: 6\r\n\r\nAlpha\n";
	static const char unsolicited[] =
	    "HTTP/1.1 304 Not Modified\r\n\r\n";
	static const char replacement[] =
	    "HTTP/1.1 200 OK\r\nContent-Length: 6\r\n\r\nBravo\n";
	struct st_webget_request request = {0};
	struct st_webget_result result;
	struct validation validation = {"Alpha\n", 0};
	struct server server;
	st_thread_t thread;
	char url[128];
	char path[sizeof(ST_WEBGET_TEST_DIRECTORY) + 64];
	char metadata[96];
	uint16_t port;
	int thread_result;

	clean_fixture();
	CHECK(fs_create_directories(ST_WEBGET_TEST_DIRECTORY) == 0);
	CHECK(socket_runtime_init() == 0);
	CHECK(start_server(&server, &port, downloaded, unchanged, &thread) == 0);
	(void)snprintf(url, sizeof(url), "http://127.0.0.1:%u?source=test",
	    (unsigned)port);
	request.url = url;
	request.cache_directory = ST_WEBGET_TEST_DIRECTORY;
	request.cache_key = "directory";
	request.output_name = "list.lst";
	request.connect_timeout_ms = 2000;
	request.io_timeout_ms = 2000;
	request.validate = validate_list;
	request.validate_context = &validation;
	CHECK(st_webget(&result, &request) == 0);
	CHECK(result.outcome == ST_WEBGET_DOWNLOADED && result.size == 6);
	CHECK(st_webget(&result, &request) == 0);
	CHECK(result.outcome == ST_WEBGET_NOT_MODIFIED && result.size == 6);
	CHECK(st_thread_join(thread, &thread_result) == ST_THREAD_SUCCESS);
	CHECK(thread_result == 0);
	CHECK(strstr(server.requests[0],
	    "GET /?source=test HTTP/1.1\r\n") != NULL);
	CHECK(strstr(server.requests[1],
	    "If-None-Match: \"one\"\r\n") != NULL);
	CHECK(validation.calls == 1);

	/* The closed listener makes an accidental network request fail. */
	CHECK(st_webget(&result, &request) == 0);
	CHECK(result.outcome == ST_WEBGET_FRESH_CACHE && result.size == 6);
	CHECK(validation.calls == 1);

	CHECK(start_server(&server, &port, no_freshness, NULL, &thread) == 0);
	(void)snprintf(url, sizeof(url), "http://127.0.0.1:%u/default",
	    (unsigned)port);
	request.cache_key = "default-freshness";
	request.output_name = "default.lst";
	CHECK(st_webget(&result, &request) == 0);
	CHECK(result.outcome == ST_WEBGET_DOWNLOADED && result.size == 6);
	CHECK(st_thread_join(thread, &thread_result) == ST_THREAD_SUCCESS);
	CHECK(thread_result == 0);
	CHECK(validation.calls == 2);
	/* Responses without freshness headers remain cached for one day. */
	CHECK(st_webget(&result, &request) == 0);
	CHECK(result.outcome == ST_WEBGET_FRESH_CACHE && result.size == 6);
	CHECK(validation.calls == 2);
	/* The same named download may be configured with a different URL.
	 * Cancel before connecting; the old resource must not be a fresh hit. */
	(void)snprintf(url, sizeof(url), "http://127.0.0.1:%u/different",
	    (unsigned)port);
	request.progress = cancel_network;
	CHECK(st_webget(&result, &request) == IO_ERROR_CANCELLED);
	request.progress = NULL;
	(void)snprintf(path, sizeof(path), "%s/default.lst",
	    ST_WEBGET_TEST_DIRECTORY);
	CHECK(file_equals(path, "Alpha\n"));

	/* A changed resource must not accept an unsolicited 304 for the old body. */
	CHECK(start_server(&server, &port, unsolicited, NULL, &thread) == 0);
	(void)snprintf(url, sizeof(url), "http://127.0.0.1:%u/different",
	    (unsigned)port);
	CHECK(st_webget(&result, &request) == IO_ERROR_PROTOCOL);
	CHECK(st_thread_join(thread, &thread_result) == ST_THREAD_SUCCESS);
	CHECK(thread_result == 0);
	CHECK(strstr(server.requests[0], "If-None-Match:") == NULL);
	CHECK(strstr(server.requests[0], "If-Modified-Since:") == NULL);
	CHECK(file_equals(path, "Alpha\n"));

	/* Successful replacement binds the stable cache key to its new URL. */
	CHECK(start_server(&server, &port, replacement, NULL, &thread) == 0);
	(void)snprintf(url, sizeof(url), "http://127.0.0.1:%u/replacement",
	    (unsigned)port);
	validation.expected = "Bravo\n";
	CHECK(st_webget(&result, &request) == 0);
	CHECK(result.outcome == ST_WEBGET_DOWNLOADED && result.size == 6);
	CHECK(st_thread_join(thread, &thread_result) == ST_THREAD_SUCCESS);
	CHECK(thread_result == 0);
	CHECK(strstr(server.requests[0], "If-None-Match:") == NULL);
	CHECK(strstr(server.requests[0], "If-Modified-Since:") == NULL);
	CHECK(validation.calls == 3);
	CHECK(file_equals(path, "Bravo\n"));
	CHECK(st_webget(&result, &request) == 0);
	CHECK(result.outcome == ST_WEBGET_FRESH_CACHE && result.size == 6);
	CHECK(validation.calls == 3);

	/* Legacy metadata without URL identity is never treated as fresh. */
	(void)snprintf(path, sizeof(path), "%s/legacy.lst",
	    ST_WEBGET_TEST_DIRECTORY);
	CHECK(write_text(path, "Alpha\n") == 0);
	(void)snprintf(path, sizeof(path), "%s/legacy.cacheinfo",
	    ST_WEBGET_TEST_DIRECTORY);
	(void)snprintf(metadata, sizeof(metadata), "ResponseTime=%lld\n",
	    (long long)time(NULL));
	CHECK(write_text(path, metadata) == 0);
	request.url = "http://127.0.0.1:1/legacy";
	request.cache_key = "legacy";
	request.output_name = "legacy.lst";
	request.progress = cancel_network;
	CHECK(st_webget(&result, &request) == IO_ERROR_CANCELLED);
	(void)snprintf(path, sizeof(path), "%s/legacy.lst",
	    ST_WEBGET_TEST_DIRECTORY);
	CHECK(file_equals(path, "Alpha\n"));
	CHECK(socket_runtime_cleanup() == 0);

	clean_fixture();
	return 0;
}
