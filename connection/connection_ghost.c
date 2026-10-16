/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#include "connection_stream.h"
#include "st_time.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
response(struct st_connection_stream *stream, const char *expected,
	const struct st_connection_hooks *hooks)
{
	char line[128];
	size_t used = 0;
	struct st_deadline deadline;
	int status = st_deadline_start(&deadline, 10000);
	int64_t remaining;
	if (status != 0) return status;
	while ((status = st_deadline_remaining_ms(&deadline, &remaining)) == 0
	    && remaining != 0) {
		ssize_t n;
		if (st_connection_cancelled(hooks, ST_CONNECTION_SETUP_NEGOTIATING))
			return IO_ERROR_CANCELLED;
		n = st_connection_stream_read(stream, line + used, 1, 100);
		if (n == IO_ERROR_TIMED_OUT || n == IO_ERROR_WOULD_BLOCK) continue;
		if (n <= 0) return n < 0 ? (int)n : IO_ERROR_CONNECTION_RESET;
		if (++used == sizeof(line)) return IO_ERROR_PROTOCOL;
		line[used] = '\0';
		if (line[used - 1] == '\n') {
			if (strcmp(line, expected) == 0) return 0;
			if (used > 2) return IO_ERROR_PROTOCOL;
			used = 0;
		}
	}
	return status != 0 ? status : IO_ERROR_TIMED_OUT;
}

static ssize_t
ghost_read(void *state, void *data, size_t size, int64_t timeout)
{
	return st_connection_stream_read(state, data, size, timeout);
}
static ssize_t
ghost_write(void *state, const void *data, size_t size, int64_t timeout)
{
	return st_connection_stream_write(state, data, size, timeout);
}
static void
ghost_interrupt(void *state)
{
	st_connection_stream_interrupt(state);
}
static void
ghost_destroy(void *state)
{
	st_connection_stream_close(state);
	free(state);
}
static const struct st_connection_transport_ops operations = {
	.read = ghost_read, .write = ghost_write,
	.interrupt = ghost_interrupt, .destroy = ghost_destroy,
};

int
st_connection_connect_ghost(st_connection_t **result,
	const struct st_profile *profile, const struct st_connection_hooks *hooks,
	struct st_connection_error *error)
{
	struct st_connection_stream *stream;
	static const char probe[] = "\r\nMBBS: PING\r\n";
	char launch[ST_PROFILE_GHOST_PROGRAM_MAX + ST_PROFILE_USER_MAX + 64];
	const char *program;
	int length, status;

	if (result == NULL || profile == NULL
	    || profile->connection_type != ST_CONNECTION_MBBS_GHOST
	    || profile->address[0] == '\0')
		return st_connection_fail(error, IO_ERROR_INVALID_ARGUMENT,
		    ST_CONNECTION_SETUP_NONE, "invalid GHost profile");
	program = profile->ghost_program[0] != '\0'
	    ? profile->ghost_program : profile->password;
	if (program[0] == '\0' || strpbrk(program, "\r\n") != NULL
	    || strpbrk(profile->username, "'\r\n") != NULL)
		return st_connection_fail(error, IO_ERROR_INVALID_ARGUMENT,
		    ST_CONNECTION_SETUP_NONE, "invalid GHost program or user name");
	length = snprintf(launch, sizeof(launch), "MBBS: %s 2 '%s' 999 GR\r\n",
	    program, profile->username);
	if (length < 0 || (size_t)length >= sizeof(launch)) return IO_ERROR_OUT_OF_RANGE;
	stream = calloc(1, sizeof(*stream));
	if (stream == NULL) return IO_ERROR_OUT_OF_MEMORY;
	status = st_connection_stream_open(stream, profile, false, NULL, hooks, error);
	if (status != 0) {
		ghost_destroy(stream);
		return status;
	}
	status = st_connection_stream_send_all(stream, probe, sizeof(probe) - 1, hooks);
	if (status == 0) status = response(stream, "ERROR\r\n", hooks);
	if (status == 0)
		status = st_connection_stream_send_all(stream, launch, (size_t)length, hooks);
	if (status == 0) status = response(stream, "OK\r\n", hooks);
	if (status != 0) {
		ghost_destroy(stream);
		return st_connection_fail(error, status,
		    ST_CONNECTION_SETUP_NEGOTIATING, "GHost launch failed");
	}
	return st_connection_start(result, stream, &operations, "GHost",
	    hooks, NULL, error);
}
