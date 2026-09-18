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
	const char *response;
	char request[4096];
};

static int
server_thread(void *argument)
{
	struct server *server = argument;
	socket_handle_t client = accept(server->listener, NULL, NULL);
	size_t used = 0;
	size_t sent = 0;

	if (!socket_handle_is_valid(client))
		return __LINE__;
	while (used + 1 < sizeof(server->request)) {
		ssize_t count = socket_receive(client, server->request + used,
		    sizeof(server->request) - used - 1, SOCKET_RECEIVE_NONE);
		if (count <= 0) {
			(void)socket_close(client);
			return __LINE__;
		}
		used += (size_t)count;
		server->request[used] = '\0';
		if (strstr(server->request, "\r\n\r\n") != NULL)
			break;
	}
	while (server->response[sent] != '\0') {
		ssize_t count = socket_send(client, server->response + sent,
		    strlen(server->response + sent));
		if (count <= 0) {
			(void)socket_close(client);
			return __LINE__;
		}
		sent += (size_t)count;
	}
	(void)socket_shutdown(client, SOCKET_SHUTDOWN_BOTH);
	(void)socket_close(client);
	(void)socket_close(server->listener);
	return 0;
}

static int
start_server(struct server *server, uint16_t *port, const char *response,
    st_thread_t *thread)
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
	server->response = response;
	*port = ntohs(address.sin_port);
	return st_thread_create(thread, server_thread, server)
	    == ST_THREAD_SUCCESS ? 0 : IO_ERROR_IO;
}

static int
validate_list(const char *path, void *context)
{
	FILE *stream;
	char data[7] = {0};
	int *calls = context;
	int status = fs_open(&stream, path, "rb");

	if (status < 0)
		return status;
	if (fread(data, 1, 6, stream) != 6 || fclose(stream) != 0)
		return IO_ERROR_IO;
	(*calls)++;
	return strcmp(data, "Alpha\n") == 0 ? 0 : IO_ERROR_INVALID_FORMAT;
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
	struct st_webget_request request = {0};
	struct st_webget_result result;
	struct server server;
	st_thread_t thread;
	char url[128];
	char path[sizeof(ST_WEBGET_TEST_DIRECTORY) + 64];
	uint16_t port;
	int thread_result;
	int validations = 0;

	CHECK(fs_create_directories(ST_WEBGET_TEST_DIRECTORY) == 0);
	CHECK(socket_runtime_init() == 0);
	CHECK(start_server(&server, &port, downloaded, &thread) == 0);
	(void)snprintf(url, sizeof(url), "http://127.0.0.1:%u?source=test",
	    (unsigned)port);
	request.url = url;
	request.cache_directory = ST_WEBGET_TEST_DIRECTORY;
	request.cache_key = "directory";
	request.output_name = "list.lst";
	request.connect_timeout_ms = 2000;
	request.io_timeout_ms = 2000;
	request.validate = validate_list;
	request.validate_context = &validations;
	CHECK(st_webget(&result, &request) == 0);
	CHECK(result.outcome == ST_WEBGET_DOWNLOADED && result.size == 6);
	CHECK(st_thread_join(thread, &thread_result) == ST_THREAD_SUCCESS);
	CHECK(thread_result == 0);
	CHECK(strstr(server.request, "GET /?source=test HTTP/1.1\r\n") != NULL);
	CHECK(validations == 1);

	CHECK(start_server(&server, &port, unchanged, &thread) == 0);
	(void)snprintf(url, sizeof(url), "http://127.0.0.1:%u?source=test",
	    (unsigned)port);
	CHECK(st_webget(&result, &request) == 0);
	CHECK(result.outcome == ST_WEBGET_NOT_MODIFIED && result.size == 6);
	CHECK(st_thread_join(thread, &thread_result) == ST_THREAD_SUCCESS);
	CHECK(thread_result == 0);
	CHECK(strstr(server.request, "If-None-Match: \"one\"\r\n") != NULL);
	CHECK(validations == 1);

	/* The closed listener makes an accidental network request fail. */
	CHECK(st_webget(&result, &request) == 0);
	CHECK(result.outcome == ST_WEBGET_FRESH_CACHE && result.size == 6);
	CHECK(validations == 1);

	CHECK(start_server(&server, &port, no_freshness, &thread) == 0);
	(void)snprintf(url, sizeof(url), "http://127.0.0.1:%u/default",
	    (unsigned)port);
	request.cache_key = "default-freshness";
	request.output_name = "default.lst";
	CHECK(st_webget(&result, &request) == 0);
	CHECK(result.outcome == ST_WEBGET_DOWNLOADED && result.size == 6);
	CHECK(st_thread_join(thread, &thread_result) == ST_THREAD_SUCCESS);
	CHECK(thread_result == 0);
	CHECK(validations == 2);
	/* Responses without freshness headers remain cached for one day. */
	CHECK(st_webget(&result, &request) == 0);
	CHECK(result.outcome == ST_WEBGET_FRESH_CACHE && result.size == 6);
	CHECK(validations == 2);
	CHECK(socket_runtime_cleanup() == 0);

	(void)snprintf(path, sizeof(path), "%s/list.lst",
	    ST_WEBGET_TEST_DIRECTORY);
	(void)fs_remove_file(path);
	(void)snprintf(path, sizeof(path), "%s/directory.cacheinfo",
	    ST_WEBGET_TEST_DIRECTORY);
	(void)fs_remove_file(path);
	(void)snprintf(path, sizeof(path), "%s/directory.cacheinfo.lock",
	    ST_WEBGET_TEST_DIRECTORY);
	(void)fs_remove_file(path);
	(void)snprintf(path, sizeof(path), "%s/default.lst",
	    ST_WEBGET_TEST_DIRECTORY);
	(void)fs_remove_file(path);
	(void)snprintf(path, sizeof(path), "%s/default-freshness.cacheinfo",
	    ST_WEBGET_TEST_DIRECTORY);
	(void)fs_remove_file(path);
	(void)snprintf(path, sizeof(path), "%s/default-freshness.cacheinfo.lock",
	    ST_WEBGET_TEST_DIRECTORY);
	(void)fs_remove_file(path);
	(void)fs_remove_directory(ST_WEBGET_TEST_DIRECTORY);
	return 0;
}
