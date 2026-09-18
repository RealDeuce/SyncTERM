/* Concurrent web-backed BBS-list refresh tests. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_bbs_list.h"

#include "filesystem.h"
#include "io_error.h"
#include "socket.h"
#include "st_app_paths.h"
#include "st_settings.h"
#include "st_threads.h"
#include "st_time.h"

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

#ifndef ST_BBS_WEB_TEST_DIRECTORY
#error "ST_BBS_WEB_TEST_DIRECTORY is required"
#endif

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "failed: %s:%d: %s\n", __FILE__, __LINE__, \
		    #expression); \
		return 1; \
	} \
} while (0)

struct server_group {
	st_mutex_t mutex;
	st_condition_t changed;
	size_t required;
	size_t accepted;
	bool barrier_failed;
};

struct server {
	socket_handle_t listener;
	struct server_group *group;
	const char *response;
};

struct progress_capture {
	struct st_bbs_web_refresh_item items[3];
	size_t count;
	size_t calls;
	bool saw_active;
};

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

static int
server_group_init(struct server_group *group, size_t required)
{
	memset(group, 0, sizeof(*group));
	group->required = required;
	if (st_mutex_init(&group->mutex, ST_MUTEX_PLAIN) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (st_condition_init(&group->changed) != ST_THREAD_SUCCESS) {
		st_mutex_destroy(&group->mutex);
		return IO_ERROR_IO;
	}
	return 0;
}

static void
server_group_dispose(struct server_group *group)
{
	st_condition_destroy(&group->changed);
	st_mutex_destroy(&group->mutex);
}

static int
server_thread(void *argument)
{
	struct server *server = argument;
	struct server_group *group = server->group;
	socket_handle_t client = accept(server->listener, NULL, NULL);
	char request[4096];
	size_t used = 0;
	size_t sent = 0;
	struct timespec deadline;

	if (!socket_handle_is_valid(client))
		return __LINE__;
	(void)st_mutex_lock(&group->mutex);
	group->accepted++;
	(void)st_condition_broadcast(&group->changed);
	if (st_time_utc_deadline_after_ms(&deadline, 3000) != 0) {
		group->barrier_failed = true;
	}
	while (group->accepted < group->required && !group->barrier_failed) {
		int status = st_condition_timedwait(&group->changed, &group->mutex,
		    &deadline);

		if (status == ST_THREAD_TIMED_OUT) {
			group->barrier_failed = true;
			(void)st_condition_broadcast(&group->changed);
		}
		else if (status != ST_THREAD_SUCCESS)
			group->barrier_failed = true;
	}
	(void)st_mutex_unlock(&group->mutex);
	while (used + 1 < sizeof(request)) {
		ssize_t count = socket_receive(client, request + used,
		    sizeof(request) - used - 1, SOCKET_RECEIVE_NONE);

		if (count <= 0) {
			(void)socket_close(client);
			return __LINE__;
		}
		used += (size_t)count;
		request[used] = '\0';
		if (strstr(request, "\r\n\r\n") != NULL)
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
start_server(struct server *server, struct server_group *group,
	const char *response, uint16_t *port, st_thread_t *thread)
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
	server->group = group;
	server->response = response;
	*port = ntohs(address.sin_port);
	return st_thread_create(thread, server_thread, server)
	    == ST_THREAD_SUCCESS ? 0 : IO_ERROR_IO;
}

static void
capture_progress(const struct st_bbs_web_refresh_item *items, size_t count,
	void *context)
{
	struct progress_capture *capture = context;

	capture->calls++;
	capture->count = count;
	for (size_t i = 0; i < count && i < 3; ++i) {
		capture->items[i] = items[i];
		if (items[i].state == ST_BBS_WEB_REFRESH_ACTIVE)
			capture->saw_active = true;
	}
}

static void
make_response(char *result, size_t size, const char *body)
{
	if (body == NULL) {
		(void)snprintf(result, size,
		    "HTTP/1.1 500 Server Error\r\nContent-Length: 0\r\n\r\n");
		return;
	}
	(void)snprintf(result, size,
	    "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n"
	    "Cache-Control: max-age=0\r\n\r\n%s", strlen(body), body);
}

static int
run_pair(const struct st_app_paths *paths, const char *first_body,
	const char *second_body, struct progress_capture *capture,
	bool *concurrent)
{
	struct server_group group;
	struct server servers[2];
	st_thread_t threads[2];
	char responses[2][512];
	char urls[2][128];
	uint16_t ports[2];
	int thread_result;
	int status;

	make_response(responses[0], sizeof(responses[0]), first_body);
	make_response(responses[1], sizeof(responses[1]), second_body);
	CHECK(server_group_init(&group, 2) == 0);
	CHECK(start_server(&servers[0], &group, responses[0], &ports[0],
	    &threads[0]) == 0);
	CHECK(start_server(&servers[1], &group, responses[1], &ports[1],
	    &threads[1]) == 0);
	(void)snprintf(urls[0], sizeof(urls[0]), "http://127.0.0.1:%u/one",
	    (unsigned)ports[0]);
	(void)snprintf(urls[1], sizeof(urls[1]), "http://127.0.0.1:%u/two",
	    (unsigned)ports[1]);
	CHECK(st_settings_web_download_set(0, "One", urls[0]) == 0);
	CHECK(st_settings_web_download_set(1, "Two", urls[1]) == 0);
	memset(capture, 0, sizeof(*capture));
	status = st_bbs_web_cache_refresh(paths, capture_progress, capture);
	CHECK(st_thread_join(threads[0], &thread_result) == ST_THREAD_SUCCESS);
	CHECK(thread_result == 0);
	CHECK(st_thread_join(threads[1], &thread_result) == ST_THREAD_SUCCESS);
	CHECK(thread_result == 0);
	*concurrent = group.accepted == 2 && !group.barrier_failed;
	server_group_dispose(&group);
	return status;
}

static void
remove_cache_file(const char *directory, const char *name, const char *suffix)
{
	char path[sizeof(ST_BBS_WEB_TEST_DIRECTORY) + 96];

	(void)snprintf(path, sizeof(path), "%s/%s%s", directory, name, suffix);
	(void)fs_remove_file(path);
}

int
main(void)
{
	static const char first[] = "[Remote One]\nAddress=one.example\n";
	static const char second[] = "[Remote Two]\nAddress=two.example\n";
	static const char replacement[] =
	    "[Remote Two New]\nAddress=two-new.example\n";
	static const char manual[] = "[Manual]\nAddress=manual.example\n";
	struct st_app_paths paths = {0};
	struct progress_capture capture;
	struct server_group group;
	struct server server;
	st_thread_t thread;
	st_bbs_list_t *list = NULL;
	char personal[sizeof(ST_BBS_WEB_TEST_DIRECTORY) + 32];
	char shared[sizeof(ST_BBS_WEB_TEST_DIRECTORY) + 32];
	char cache[sizeof(ST_BBS_WEB_TEST_DIRECTORY) + 32];
	char system_cache[sizeof(ST_BBS_WEB_TEST_DIRECTORY) + 48];
	char system_list[sizeof(ST_BBS_WEB_TEST_DIRECTORY) + 64];
	char response[512];
	char url[128];
	uint16_t port;
	int thread_result;
	int status;
	bool concurrent;

	(void)snprintf(personal, sizeof(personal), "%s/personal.lst",
	    ST_BBS_WEB_TEST_DIRECTORY);
	(void)snprintf(shared, sizeof(shared), "%s/shared.lst",
	    ST_BBS_WEB_TEST_DIRECTORY);
	(void)snprintf(cache, sizeof(cache), "%s/cache",
	    ST_BBS_WEB_TEST_DIRECTORY);
	(void)snprintf(system_cache, sizeof(system_cache), "%s/cache/system",
	    ST_BBS_WEB_TEST_DIRECTORY);
	(void)snprintf(system_list, sizeof(system_list), "%s/System List.lst",
	    system_cache);
	CHECK(fs_create_directories(system_cache) == 0);
	CHECK(write_text(personal, "") == 0);
	CHECK(write_text(shared, "") == 0);
	paths.personal_list = personal;
	paths.shared_list = shared;
	paths.cache_directory = cache;
	paths.system_cache_directory = system_cache;
	paths.cached_system_list = system_list;
	paths.download_directory = ST_BBS_WEB_TEST_DIRECTORY;
	st_settings_init(&st_settings);
	memset(&capture, 0, sizeof(capture));
	CHECK(st_bbs_web_cache_refresh(&paths, capture_progress, &capture) == 0);
	CHECK(capture.calls == 0);
	CHECK(st_settings_web_download_append("One", "http://invalid/") == 0);
	CHECK(st_settings_web_download_append("Two", "http://invalid/") == 0);
	CHECK(socket_runtime_init() == 0);
	CHECK(run_pair(&paths, first, second, &capture, &concurrent) == 0);
	CHECK(concurrent && capture.calls >= 2 && capture.count == 2
	    && capture.saw_active);
	CHECK(capture.items[0].state == ST_BBS_WEB_REFRESH_COMPLETE);
	CHECK(capture.items[1].state == ST_BBS_WEB_REFRESH_COMPLETE);

	status = run_pair(&paths, NULL, replacement, &capture, &concurrent);
	CHECK(status < 0 && concurrent);
	CHECK(capture.items[0].state == ST_BBS_WEB_REFRESH_FAILED);
	CHECK(capture.items[1].state == ST_BBS_WEB_REFRESH_COMPLETE);

	CHECK(st_bbs_list_open(&list, &paths, NULL, 0) == 0);
	make_response(response, sizeof(response), manual);
	CHECK(server_group_init(&group, 1) == 0);
	CHECK(start_server(&server, &group, response, &port, &thread) == 0);
	CHECK(st_settings_web_download_set(0, "One",
	    "http://127.0.0.1:1/unreachable") == 0);
	(void)snprintf(url, sizeof(url), "http://127.0.0.1:%u/manual",
	    (unsigned)port);
	CHECK(st_settings_web_download_set(1, "Two", url) == 0);
	CHECK(st_bbs_list_refresh_web_at(list, 1, NULL, NULL) == 0);
	CHECK(st_bbs_list_find(list, "Manual") != NULL);
	CHECK(st_thread_join(thread, &thread_result) == ST_THREAD_SUCCESS);
	CHECK(thread_result == 0 && !group.barrier_failed);
	server_group_dispose(&group);
	st_bbs_list_close(list);
	CHECK(socket_runtime_cleanup() == 0);
	st_settings_dispose(&st_settings);

	remove_cache_file(system_cache, "One", ".lst");
	remove_cache_file(system_cache, "One", ".cacheinfo");
	remove_cache_file(system_cache, "One", ".cacheinfo.lock");
	remove_cache_file(system_cache, "Two", ".lst");
	remove_cache_file(system_cache, "Two", ".cacheinfo");
	remove_cache_file(system_cache, "Two", ".cacheinfo.lock");
	(void)fs_remove_file(personal);
	remove_cache_file(ST_BBS_WEB_TEST_DIRECTORY, "personal.lst", ".lock");
	(void)fs_remove_file(shared);
	(void)fs_remove_directory(system_cache);
	(void)fs_remove_directory(cache);
	(void)fs_remove_directory(ST_BBS_WEB_TEST_DIRECTORY);
	return 0;
}
