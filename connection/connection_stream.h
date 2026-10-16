/* TCP/TLS mechanism shared by network protocol providers. */
/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_CONNECTION_STREAM_H_
#define ST_CONNECTION_STREAM_H_
#include "connection_internal.h"
#include "socket.h"
#include "st_tls.h"

struct st_connection_stream {
	socket_handle_t socket;
	st_tls_session_t *tls;
	st_mutex_t mutex;
	bool initialized;
	char diagnostic[256];
	struct st_connection_hooks hooks;
	bool tls_ready, error_pending;
	bool writing;
	char error_title[64];
};

int st_connection_stream_open(struct st_connection_stream *stream,
	const struct st_profile *profile, bool encrypted,
	const struct st_tls_client_options *tls_options,
	const struct st_connection_hooks *hooks, struct st_connection_error *error);
ssize_t st_connection_stream_read(struct st_connection_stream *stream,
	void *data, size_t size, int64_t timeout_ms);
ssize_t st_connection_stream_write(struct st_connection_stream *stream,
	const void *data, size_t size, int64_t timeout_ms);
int st_connection_stream_send_all(struct st_connection_stream *stream,
	const void *data, size_t size, const struct st_connection_hooks *hooks);
void st_connection_stream_interrupt(struct st_connection_stream *stream);
void st_connection_stream_close(struct st_connection_stream *stream);
int st_connection_stream_start_tls(struct st_connection_stream *stream,
	const struct st_profile *profile, struct st_tls_client_options *options,
	struct st_connection_error *error);
int st_connection_stream_process_requests(struct st_connection_stream *stream);
#endif
