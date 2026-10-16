/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_CONNECTION_INTERNAL_H_
#define ST_CONNECTION_INTERNAL_H_

#include "st_connection.h"
#include "st_threads.h"

#include <stdatomic.h>

#define ST_CONNECTION_BUFFER_SIZE 16384

struct st_connection_queue {
	st_mutex_t mutex;
	st_condition_t changed;
	uint8_t data[ST_CONNECTION_BUFFER_SIZE];
	size_t head;
	size_t count;
	bool stopped;
	bool eof;
};

struct st_connection_transport_ops {
	void (*bind)(void *state, st_connection_t *connection);
	ssize_t (*read)(void *state, void *data, size_t size, int64_t timeout_ms);
	ssize_t (*write)(void *state, const void *data, size_t size,
		int64_t timeout_ms);
	void (*interrupt)(void *state);
	int (*update_terminal_size)(void *state,
		const struct st_screen_geometry *geometry);
	void (*destroy)(void *state);
	int (*set_binary_mode)(void *state, bool enabled);
	int (*process_requests)(void *state);
};

typedef ssize_t (*st_connection_send_filter_fn)(st_connection_t *connection,
	const void *data, size_t size, int64_t timeout_ms);

struct st_connection {
	const struct st_connection_transport_ops *operations;
	void *transport;
	struct st_connection_hooks hooks;
	struct st_connection_queue input;
	struct st_connection_queue output;
	st_mutex_t state_mutex;
	st_condition_t state_changed;
	st_thread_t input_thread;
	st_thread_t output_thread;
	uint64_t started_ms;
	const char *log_source;
	st_connection_send_filter_fn send_filter;
	atomic_bool connected;
	atomic_bool stopping;
	atomic_bool binary_mode;
	bool input_paused;
	bool input_thread_started;
	bool output_thread_started;
};

int st_connection_start(st_connection_t **result, void *transport,
	const struct st_connection_transport_ops *operations, const char *source,
	const struct st_connection_hooks *hooks,
	st_connection_send_filter_fn send_filter,
	struct st_connection_error *error);
ssize_t st_connection_queue_output(st_connection_t *connection,
	const void *data, size_t size, int64_t timeout_ms);
ssize_t st_connection_queue_output_atomic(st_connection_t *connection,
	const void *data, size_t size, int64_t timeout_ms);
void st_connection_clear_input(st_connection_t *connection);
void st_connection_pause_input(st_connection_t *connection, bool paused);
void st_connection_log(st_connection_t *connection,
	enum st_connection_log_level level, const char *format, ...);
bool st_connection_cancelled(const struct st_connection_hooks *hooks,
	enum st_connection_setup_phase phase);
int st_connection_fail(struct st_connection_error *error, int code,
	enum st_connection_setup_phase phase, const char *detail);
int st_connection_profile_geometry(const struct st_profile *profile,
	const struct st_screen_geometry *current,
	struct st_screen_geometry *result);
const char *st_connection_terminal_name(const struct st_profile *profile);

#endif
