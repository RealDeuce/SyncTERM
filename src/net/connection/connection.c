/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "connection_internal.h"

#include "io_error.h"
#include "st_settings.h"
#include "st_time.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t
now_ms(void)
{
	uint64_t now = 0;

	(void)st_time_monotonic_ms(&now);
	return now;
}

void
st_connection_notice(const struct st_connection_hooks *hooks,
	const char *title, const char *message)
{
	if (hooks == NULL) return;
	if (hooks->status != NULL) hooks->status(hooks->ui_context, NULL);
	if (hooks->alert != NULL) hooks->alert(hooks->ui_context, title, message);
	else if (hooks->choice != NULL) {
		const char *choices[] = {"OK"};
		(void)hooks->choice(hooks->ui_context, title, message, choices, 1);
	}
}

bool
st_connection_error_dialog(const struct st_connection_hooks *hooks,
	const char *title, const char *message)
{
	const char *choices[] = {"OK", "View Protocol Log"};
	if (hooks == NULL || hooks->choice == NULL) return false;
	if (hooks->status != NULL) hooks->status(hooks->ui_context, NULL);
	for (;;) {
		int choice = hooks->choice(hooks->ui_context, title, message, choices,
		    hooks->help == NULL ? 1 : 2);
		if (choice != 1 || hooks->help == NULL) break;
		hooks->help(hooks->ui_context, title, message);
	}
	return true;
}

static struct timespec
deadline_after(int64_t timeout_ms)
{
	struct timespec deadline = {0, 0};

	(void)st_time_utc_deadline_after_ms(&deadline, timeout_ms);
	return deadline;
}

static int
queue_init(struct st_connection_queue *queue)
{
	memset(queue, 0, sizeof(*queue));
	if (st_mutex_init(&queue->mutex, ST_MUTEX_PLAIN) != ST_THREAD_SUCCESS)
		return -1;
	if (st_condition_init(&queue->changed) != ST_THREAD_SUCCESS) {
		st_mutex_destroy(&queue->mutex);
		return -1;
	}
	return 0;
}

static void
queue_destroy(struct st_connection_queue *queue)
{
	st_condition_destroy(&queue->changed);
	st_mutex_destroy(&queue->mutex);
}

static void
queue_stop(struct st_connection_queue *queue, bool eof)
{
	(void)st_mutex_lock(&queue->mutex);
	queue->stopped = true;
	queue->eof = queue->eof || eof;
	/* A dead connection cannot require a local START to deliver final EOF. */
	queue->paused = false;
	(void)st_condition_broadcast(&queue->changed);
	(void)st_mutex_unlock(&queue->mutex);
}

static ssize_t
queue_write(struct st_connection_queue *queue, const void *data, size_t size,
	int64_t timeout_ms)
{
	const uint8_t *source = data;
	struct timespec deadline;
	size_t written = 0;
	int wait_result;

	if (timeout_ms < -1 || (data == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	if (size == 0)
		return 0;
	if (timeout_ms > 0)
		deadline = deadline_after(timeout_ms);
	(void)st_mutex_lock(&queue->mutex);
	while (written < size) {
		size_t free_space;
		size_t tail;
		size_t chunk;

		if (queue->stopped)
			break;
		free_space = ST_CONNECTION_BUFFER_SIZE - queue->count;
		if (free_space == 0) {
			if (written != 0 || timeout_ms == 0)
				break;
			wait_result = timeout_ms < 0
			    ? st_condition_wait(&queue->changed, &queue->mutex)
			    : st_condition_timedwait(&queue->changed, &queue->mutex,
			    &deadline);
			if (wait_result == ST_THREAD_TIMED_OUT)
				break;
			if (wait_result != ST_THREAD_SUCCESS) {
				(void)st_mutex_unlock(&queue->mutex);
				return IO_ERROR_IO;
			}
			continue;
		}
		tail = (queue->head + queue->count) % ST_CONNECTION_BUFFER_SIZE;
		chunk = ST_CONNECTION_BUFFER_SIZE - tail;
		if (chunk > free_space)
			chunk = free_space;
		if (chunk > size - written)
			chunk = size - written;
		memcpy(queue->data + tail, source + written, chunk);
		queue->count += chunk;
		written += chunk;
		(void)st_condition_broadcast(&queue->changed);
	}
	if (written == 0 && queue->stopped) {
		(void)st_mutex_unlock(&queue->mutex);
		return IO_ERROR_NOT_CONNECTED;
	}
	(void)st_mutex_unlock(&queue->mutex);
	if (written != 0)
		return (ssize_t)written;
	return timeout_ms == 0 ? IO_ERROR_WOULD_BLOCK : IO_ERROR_TIMED_OUT;
}

static ssize_t
queue_write_atomic(struct st_connection_queue *queue, const void *data,
	size_t size, int64_t timeout_ms)
{
	const uint8_t *source = data;
	struct timespec deadline;
	size_t tail;
	size_t first;
	int wait_result;

	if (timeout_ms < -1 || (data == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	if (size == 0)
		return 0;
	if (size > ST_CONNECTION_BUFFER_SIZE)
		return IO_ERROR_MESSAGE_TOO_LARGE;
	if (timeout_ms > 0)
		deadline = deadline_after(timeout_ms);
	(void)st_mutex_lock(&queue->mutex);
	while (ST_CONNECTION_BUFFER_SIZE - queue->count < size
	    && !queue->stopped) {
		if (timeout_ms == 0) {
			(void)st_mutex_unlock(&queue->mutex);
			return IO_ERROR_WOULD_BLOCK;
		}
		wait_result = timeout_ms < 0
		    ? st_condition_wait(&queue->changed, &queue->mutex)
		    : st_condition_timedwait(&queue->changed, &queue->mutex,
		    &deadline);
		if (wait_result == ST_THREAD_TIMED_OUT) {
			(void)st_mutex_unlock(&queue->mutex);
			return IO_ERROR_TIMED_OUT;
		}
		if (wait_result != ST_THREAD_SUCCESS) {
			(void)st_mutex_unlock(&queue->mutex);
			return IO_ERROR_IO;
		}
	}
	if (queue->stopped) {
		(void)st_mutex_unlock(&queue->mutex);
		return IO_ERROR_NOT_CONNECTED;
	}
	tail = (queue->head + queue->count) % ST_CONNECTION_BUFFER_SIZE;
	first = ST_CONNECTION_BUFFER_SIZE - tail;
	if (first > size)
		first = size;
	memcpy(queue->data + tail, source, first);
	memcpy(queue->data, source + first, size - first);
	queue->count += size;
	(void)st_condition_broadcast(&queue->changed);
	(void)st_mutex_unlock(&queue->mutex);
	return (ssize_t)size;
}

static ssize_t
queue_read(struct st_connection_queue *queue, void *data, size_t size,
	int64_t timeout_ms, bool peek, bool honor_pause)
{
	uint8_t *destination = data;
	struct timespec deadline;
	size_t copied = 0;
	size_t position;
	int wait_result;

	if (timeout_ms < -1 || (data == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	if (size == 0)
		return 0;
	if (timeout_ms > 0)
		deadline = deadline_after(timeout_ms);
	(void)st_mutex_lock(&queue->mutex);
	while ((queue->count == 0 || (honor_pause && queue->paused)) && !queue->eof && !queue->stopped) {
		if (timeout_ms == 0) {
			(void)st_mutex_unlock(&queue->mutex);
			return IO_ERROR_WOULD_BLOCK;
		}
		wait_result = timeout_ms < 0
		    ? st_condition_wait(&queue->changed, &queue->mutex)
		    : st_condition_timedwait(&queue->changed, &queue->mutex,
		    &deadline);
		if (wait_result == ST_THREAD_TIMED_OUT) {
			(void)st_mutex_unlock(&queue->mutex);
			return IO_ERROR_TIMED_OUT;
		}
		if (wait_result != ST_THREAD_SUCCESS) {
			(void)st_mutex_unlock(&queue->mutex);
			return IO_ERROR_IO;
		}
	}
	position = queue->head;
	while (copied < size && copied < queue->count) {
		size_t chunk = ST_CONNECTION_BUFFER_SIZE - position;

		if (chunk > size - copied)
			chunk = size - copied;
		if (chunk > queue->count - copied)
			chunk = queue->count - copied;
		memcpy(destination + copied, queue->data + position, chunk);
		copied += chunk;
		position = (position + chunk) % ST_CONNECTION_BUFFER_SIZE;
	}
	if (!peek && copied != 0) {
		queue->head = position;
		queue->count -= copied;
		(void)st_condition_broadcast(&queue->changed);
	}
	(void)st_mutex_unlock(&queue->mutex);
	return (ssize_t)copied;
}

static size_t
queue_count(struct st_connection_queue *queue)
{
	size_t result;

	(void)st_mutex_lock(&queue->mutex);
	result = queue->count;
	(void)st_mutex_unlock(&queue->mutex);
	return result;
}

static void
stop_connection(st_connection_t *connection, bool eof)
{
	bool was_stopping = atomic_exchange(&connection->stopping, true);

	atomic_store(&connection->connected, false);
	queue_stop(&connection->input, eof);
	queue_stop(&connection->output, false);
	if (!was_stopping && connection->operations->interrupt != NULL)
		connection->operations->interrupt(connection->transport);
}

static int
input_worker(void *argument)
{
	st_connection_t *connection = argument;
	uint8_t buffer[ST_CONNECTION_INPUT_CHUNK_SIZE];

	while (!atomic_load(&connection->stopping)) {
		ssize_t received;

		received = connection->operations->read(connection->transport,
		    buffer, sizeof(buffer), 100);
		if (received == IO_ERROR_TIMED_OUT
		    || received == IO_ERROR_WOULD_BLOCK)
			continue;
		if (received <= 0) {
			if (received < 0 && !atomic_load(&connection->stopping))
				st_connection_log(connection, ST_CONNECTION_LOG_ERROR,
				    "input failed (%d)", (int)received);
			else if (received == 0 && !atomic_load(&connection->stopping))
				st_connection_log_hooks(&connection->hooks, "Connection",
				    ST_CONNECTION_LOG_INFO, "%s peer closed input", connection->log_source);
			if (received == 0 && connection->operations->retain_after_eof) {
				atomic_store(&connection->connected, false);
				queue_stop(&connection->input, true);
				queue_stop(&connection->output, false);
			} else stop_connection(connection, received == 0);
			break;
		}
		/* A write that fills the queue may return after partial progress.
		 * Retain that transport chunk until its complete suffix is queued. */
		for (size_t queued = 0; queued < (size_t)received;) {
			ssize_t result = queue_write(&connection->input,
			    buffer + queued, (size_t)received - queued,
			    connection->operations->poll_input_control != NULL ? 100 : -1);

			if (result > 0) {
				queued += (size_t)result;
				continue;
			}
			if (atomic_load(&connection->stopping)
			    || result == IO_ERROR_NOT_CONNECTED)
				return 0;
			if (result == IO_ERROR_TIMED_OUT
			    && connection->operations->poll_input_control != NULL) {
				int status = connection->operations->poll_input_control(
				    connection->transport);
				if (status < 0) {
					st_connection_log(connection, ST_CONNECTION_LOG_ERROR,
					    "input control failed (%d)", status);
					stop_connection(connection, false);
					return 0;
				}
				if (status == ST_CONNECTION_INPUT_CONTROL_DISCARD)
					break;
				if (status == ST_CONNECTION_INPUT_CONTROL_EOF) {
					(void)st_mutex_lock(&connection->input.mutex);
					connection->input.paused = false;
					(void)st_condition_broadcast(&connection->input.changed);
					(void)st_mutex_unlock(&connection->input.mutex);
				}
				continue;
			}
			st_connection_log(connection, ST_CONNECTION_LOG_ERROR,
			    "input queue failed (%d)", (int)result);
			stop_connection(connection, false);
			return 0;
		}
	}
	return 0;
}

const struct st_sftp_provider *
st_connection_sftp(st_connection_t *connection)
{
	return connection == NULL || connection->operations->sftp == NULL ? NULL
	    : connection->operations->sftp(connection->transport);
}
bool
st_connection_auxiliary_alive(st_connection_t *connection)
{
	return connection != NULL && !atomic_load(&connection->stopping)
	    && connection->operations->auxiliary_alive != NULL
	    && connection->operations->auxiliary_alive(connection->transport);
}

static int
output_worker(void *argument)
{
	st_connection_t *connection = argument;
	uint8_t buffer[4096];

	while (!atomic_load(&connection->stopping)) {
		ssize_t count = queue_read(&connection->output, buffer,
		    sizeof(buffer), -1, false, false);
		size_t sent = 0;

		if (count <= 0)
			break;
		if (connection->operations->retain_after_eof
		    && !atomic_load(&connection->connected))
			break;
		while (sent < (size_t)count
		    && !atomic_load(&connection->stopping)) {
			ssize_t result = connection->operations->write(
			    connection->transport, buffer + sent,
			    (size_t)count - sent, 100);
			if (result == IO_ERROR_TIMED_OUT
			    || result == IO_ERROR_WOULD_BLOCK)
				continue;
			if (result <= 0) {
				/* A closed SSH shell is not a closed SSH session. The
				 * reader still drains terminal data; auxiliary channels
				 * remain owned by the application until explicit close. */
				if (connection->operations->retain_after_eof
				    && connection->operations->auxiliary_alive != NULL
				    && connection->operations->auxiliary_alive(connection->transport)) {
					queue_stop(&connection->output, false);
					return 0;
				}
				if (result < 0 && !atomic_load(&connection->stopping))
					st_connection_log(connection,
					    ST_CONNECTION_LOG_ERROR,
					    "output failed (%d)", (int)result);
				stop_connection(connection, result == 0);
				return 0;
			}
			sent += (size_t)result;
		}
	}
	return 0;
}

int
st_connection_start(st_connection_t **result, void *transport,
	const struct st_connection_transport_ops *operations, const char *source,
	const struct st_connection_hooks *hooks,
	st_connection_send_filter_fn send_filter,
	struct st_connection_error *error)
{
	st_connection_t *connection;

	if (result == NULL || transport == NULL || operations == NULL
	    || operations->read == NULL || operations->write == NULL
	    || operations->destroy == NULL)
		return st_connection_fail(error, IO_ERROR_INVALID_ARGUMENT,
		    ST_CONNECTION_SETUP_NONE, "invalid connection transport");
	connection = calloc(1, sizeof(*connection));
	if (connection == NULL) {
		operations->destroy(transport);
		return st_connection_fail(error, IO_ERROR_OUT_OF_MEMORY,
		    ST_CONNECTION_SETUP_NONE, "unable to allocate connection");
	}
	connection->transport = transport;
	connection->operations = operations;
	connection->log_source = source;
	connection->send_filter = send_filter;
	if (hooks != NULL)
		connection->hooks = *hooks;
	if (queue_init(&connection->input) != 0) {
		operations->destroy(transport);
		free(connection);
		return st_connection_fail(error, IO_ERROR_OUT_OF_MEMORY,
		    ST_CONNECTION_SETUP_NONE, "unable to initialize input queue");
	}
	if (queue_init(&connection->output) != 0) {
		operations->destroy(transport);
		queue_destroy(&connection->input);
		free(connection);
		return st_connection_fail(error, IO_ERROR_OUT_OF_MEMORY,
		    ST_CONNECTION_SETUP_NONE, "unable to initialize output queue");
	}
	atomic_init(&connection->connected, true);
	atomic_init(&connection->stopping, false);
	atomic_init(&connection->binary_mode, true);
	connection->started_ms = now_ms();
	if (operations->bind != NULL)
		operations->bind(transport, connection);
	if (st_thread_create(&connection->input_thread, input_worker, connection)
	    != ST_THREAD_SUCCESS) {
		st_connection_close(connection);
		return st_connection_fail(error, IO_ERROR_IO,
		    ST_CONNECTION_SETUP_NONE, "unable to start input worker");
	}
	connection->input_thread_started = true;
	if (st_thread_create(&connection->output_thread, output_worker, connection)
	    != ST_THREAD_SUCCESS) {
		st_connection_close(connection);
		return st_connection_fail(error, IO_ERROR_IO,
		    ST_CONNECTION_SETUP_NONE, "unable to start output worker");
	}
	connection->output_thread_started = true;
	st_connection_log_hooks(&connection->hooks, "Connection", ST_CONNECTION_LOG_INFO,
	    "%s session established", connection->log_source);
	*result = connection;
	return 0;
}

ssize_t
st_connection_queue_output(st_connection_t *connection, const void *data,
	size_t size, int64_t timeout_ms)
{
	if (connection == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	return queue_write(&connection->output, data, size, timeout_ms);
}

ssize_t
st_connection_queue_output_atomic(st_connection_t *connection,
	const void *data, size_t size, int64_t timeout_ms)
{
	if (connection == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	return queue_write_atomic(&connection->output, data, size, timeout_ms);
}

ssize_t
st_connection_send(st_connection_t *connection, const void *data, size_t size,
	int64_t timeout_ms)
{
	if (connection == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (connection->send_filter != NULL)
		return connection->send_filter(connection, data, size, timeout_ms, false, ST_CONNECTION_SEND_TEXT);
	return st_connection_queue_output(connection, data, size, timeout_ms);
}

ssize_t
st_connection_send_bytes(st_connection_t *connection, const void *data,
	size_t size, int64_t timeout_ms)
{
	if (connection == NULL || (data == NULL && size != 0) || timeout_ms < -1)
		return IO_ERROR_INVALID_ARGUMENT;
	if (connection->send_filter != NULL)
		return connection->send_filter(connection, data, size, timeout_ms, false,
		    ST_CONNECTION_SEND_BYTES);
	return st_connection_queue_output(connection, data, size, timeout_ms);
}

ssize_t
st_connection_send_raw(st_connection_t *connection, const void *data,
	size_t size, int64_t timeout_ms)
{
	return st_connection_queue_output(connection, data, size, timeout_ms);
}

ssize_t
st_connection_try_send_atomic(st_connection_t *connection,
	const void *data, size_t size, enum st_connection_send_kind kind)
{
	ssize_t result;

	if (connection == NULL || (data == NULL && size != 0)
	    || kind < ST_CONNECTION_SEND_TEXT || kind > ST_CONNECTION_SEND_WIRE)
		return IO_ERROR_INVALID_ARGUMENT;
	if (size > ST_CONNECTION_ATOMIC_SEND_MAX)
		return IO_ERROR_MESSAGE_TOO_LARGE;
	if (!st_connection_is_connected(connection))
		return IO_ERROR_NOT_CONNECTED;
	if (kind != ST_CONNECTION_SEND_WIRE && connection->send_filter != NULL)
		result = connection->send_filter(connection, data, size, 0, true, kind);
	else
		result = st_connection_queue_output_atomic(connection, data, size, 0);
	return result >= 0 && (size_t)result != size ? IO_ERROR_IO : result;
}

ssize_t
st_connection_receive(st_connection_t *connection, void *data, size_t size,
	int64_t timeout_ms)
{
	if (connection == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	return queue_read(&connection->input, data, size, timeout_ms, false, true);
}

ssize_t
st_connection_receive_raw(st_connection_t *connection, void *data,
	size_t size, int64_t timeout_ms)
{
	if (connection == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	return queue_read(&connection->input, data, size, timeout_ms, false, false);
}

ssize_t
st_connection_peek_raw(st_connection_t *connection, void *data, size_t size)
{
	if (connection == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	return queue_read(&connection->input, data, size, 0, true, false);
}

size_t
st_connection_pending_raw(st_connection_t *connection)
{
	return connection == NULL ? 0 : queue_count(&connection->input);
}

size_t
st_connection_queued(st_connection_t *connection)
{
	return connection == NULL ? 0 : queue_count(&connection->output);
}

bool
st_connection_is_connected(st_connection_t *connection)
{
	return connection != NULL && atomic_load(&connection->connected);
}

int
st_connection_process_requests(st_connection_t *connection)
{
	if (connection == NULL) return IO_ERROR_INVALID_ARGUMENT;
	return connection->operations->process_requests == NULL ? 0
	    : connection->operations->process_requests(connection->transport);
}

const char *
st_connection_terminal_name(const struct st_profile *profile)
{
	if (profile->terminal_name[0] != '\0') return profile->terminal_name;
	switch ((enum st_screen_mode)profile->screen_mode) {
	case ST_SCREEN_MODE_C64:
	case ST_SCREEN_MODE_C128_40:
	case ST_SCREEN_MODE_C128_80: return "PETSCII";
	case ST_SCREEN_MODE_ATARI:
	case ST_SCREEN_MODE_ATARI_XEP80: return "ATASCII";
	case ST_SCREEN_MODE_PRESTEL: return "Prestel";
	case ST_SCREEN_MODE_BEEB: return "Beeb7";
	case ST_SCREEN_MODE_ATARIST_40X25:
	case ST_SCREEN_MODE_ATARIST_80X25:
	case ST_SCREEN_MODE_ATARIST_80X25_MONO: return "AtariST+VT52";
	default: return "syncterm";
	}
}

uint64_t
st_connection_elapsed_ms(st_connection_t *connection)
{
	uint64_t current;

	if (connection == NULL)
		return 0;
	current = now_ms();
	return current >= connection->started_ms
	    ? current - connection->started_ms : 0;
}

bool
st_connection_binary_mode(st_connection_t *connection)
{
	return connection != NULL && atomic_load(&connection->binary_mode);
}

int
st_connection_set_binary_mode(st_connection_t *connection, bool enabled)
{
	if (connection == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (connection->operations->set_binary_mode != NULL)
		return connection->operations->set_binary_mode(
		    connection->transport, enabled);
	/* Serialize a raw-mode resume with local XOFF: a racing sender must
	 * not leave delivery paused after the server has selected raw mode. */
	(void)st_mutex_lock(&connection->input.mutex);
	atomic_store(&connection->binary_mode, enabled);
	if (enabled) {
		connection->input.paused = false;
		(void)st_condition_broadcast(&connection->input.changed);
	}
	(void)st_mutex_unlock(&connection->input.mutex);
	return 0;
}

int
st_connection_update_terminal_size(st_connection_t *connection,
	const struct st_screen_geometry *geometry)
{
	if (connection == NULL || geometry == NULL || geometry->columns == 0
	    || geometry->rows == 0)
		return IO_ERROR_INVALID_ARGUMENT;
	if (connection->operations->update_terminal_size == NULL)
		return 0;
	return connection->operations->update_terminal_size(
	    connection->transport, geometry);
}

void
st_connection_clear_input(st_connection_t *connection)
{
	(void)st_mutex_lock(&connection->input.mutex);
	connection->input.head = 0;
	connection->input.count = 0;
	(void)st_condition_broadcast(&connection->input.changed);
	(void)st_mutex_unlock(&connection->input.mutex);
}

bool
st_connection_consume_flow_control(st_connection_t *connection, uint8_t byte)
{
	bool consumed = false;
	(void)st_mutex_lock(&connection->input.mutex);
	if (!atomic_load(&connection->binary_mode) && (byte == 0x11 || byte == 0x13)) {
		connection->input.paused = byte == 0x13;
		(void)st_condition_broadcast(&connection->input.changed);
		consumed = true;
	}
	(void)st_mutex_unlock(&connection->input.mutex);
	return consumed;
}

void
st_connection_log_hooks(const struct st_connection_hooks *hooks,
	const char *source, enum st_connection_log_level level, const char *format, ...)
{
	char message[1024];
	va_list arguments;

	if (hooks == NULL || hooks->log == NULL)
		return;
	va_start(arguments, format);
	(void)vsnprintf(message, sizeof(message), format, arguments);
	va_end(arguments);
	hooks->log(hooks->log_context, level, source, message);
}

void
st_connection_log(st_connection_t *connection,
	enum st_connection_log_level level, const char *format, ...)
{
	char message[1024];
	va_list arguments;
	if (connection == NULL || connection->hooks.log == NULL)
		return;
	va_start(arguments, format);
	(void)vsnprintf(message, sizeof(message), format, arguments);
	va_end(arguments);
	connection->hooks.log(connection->hooks.log_context, level,
	    connection->log_source, message);
}

void
st_connection_socket_diagnostic(const struct st_connection_hooks *hooks,
	const char *option, int status)
{
	if (status != 0)
		st_connection_log_hooks(hooks, "TCP", ST_CONNECTION_LOG_WARNING,
		    "socket %s configuration failed (%d)", option, status);
}

bool
st_connection_cancelled(const struct st_connection_hooks *hooks,
	enum st_connection_setup_phase phase)
{
	bool cancelled = hooks != NULL && hooks->setup != NULL
	    && hooks->setup(hooks->setup_context, phase)
	    == ST_CONNECTION_SETUP_CANCEL;
	if (cancelled)
		st_connection_log_hooks(hooks, "Connection", ST_CONNECTION_LOG_INFO,
		    "setup cancelled at phase %d", (int)phase);
	return cancelled;
}

int
st_connection_fail(struct st_connection_error *error, int code,
	enum st_connection_setup_phase phase, const char *detail)
{
	if (error != NULL) {
		memset(error, 0, sizeof(*error));
		error->code = code;
		error->phase = phase;
		if (detail != NULL)
			(void)snprintf(error->detail, sizeof(error->detail), "%s",
			    detail);
	}
	return code;
}

int
st_connection_profile_geometry(const struct st_profile *profile,
	const struct st_screen_geometry *current,
	struct st_screen_geometry *result)
{
	static const struct {
		uint32_t columns;
		uint32_t rows;
	} dimensions[] = {
		[ST_SCREEN_MODE_80X25] = {80, 25},
		[ST_SCREEN_MODE_LCD_80X25] = {80, 25},
		[ST_SCREEN_MODE_80X28] = {80, 28},
		[ST_SCREEN_MODE_80X30] = {80, 30},
		[ST_SCREEN_MODE_80X43] = {80, 43},
		[ST_SCREEN_MODE_80X50] = {80, 50},
		[ST_SCREEN_MODE_80X60] = {80, 60},
		[ST_SCREEN_MODE_132X37] = {132, 37},
		[ST_SCREEN_MODE_132X52] = {132, 52},
		[ST_SCREEN_MODE_132X25] = {132, 25},
		[ST_SCREEN_MODE_132X28] = {132, 28},
		[ST_SCREEN_MODE_132X30] = {132, 30},
		[ST_SCREEN_MODE_132X34] = {132, 34},
		[ST_SCREEN_MODE_132X43] = {132, 43},
		[ST_SCREEN_MODE_132X50] = {132, 50},
		[ST_SCREEN_MODE_132X60] = {132, 60},
		[ST_SCREEN_MODE_C64] = {40, 25},
		[ST_SCREEN_MODE_C128_40] = {40, 25},
		[ST_SCREEN_MODE_C128_80] = {80, 25},
		[ST_SCREEN_MODE_ATARI] = {40, 24},
		[ST_SCREEN_MODE_ATARI_XEP80] = {80, 25},
		[ST_SCREEN_MODE_EGA_80X25] = {80, 25},
		[ST_SCREEN_MODE_VGA_80X25] = {80, 25},
		[ST_SCREEN_MODE_PRESTEL] = {40, 25},
		[ST_SCREEN_MODE_BEEB] = {40, 25},
		[ST_SCREEN_MODE_ATARIST_40X25] = {40, 25},
		[ST_SCREEN_MODE_ATARIST_80X25] = {80, 25},
		[ST_SCREEN_MODE_ATARIST_80X25_MONO] = {80, 25},
		[ST_SCREEN_MODE_AMIGA_640X200] = {80, 25},
		[ST_SCREEN_MODE_AMIGA_640X400] = {80, 50},
		[ST_SCREEN_MODE_CGA_80X25] = {80, 25},
	};
	int mode;

	if (profile == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	mode = profile->screen_mode;
	memset(result, 0, sizeof(*result));
	if (mode == ST_SCREEN_MODE_CURRENT) {
		if (current == NULL || current->columns == 0 || current->rows == 0)
			return IO_ERROR_INVALID_ARGUMENT;
		*result = *current;
		if (!profile->hide_status && result->rows > 1)
			--result->rows;
		return 0;
	}
	if (mode == ST_SCREEN_MODE_CUSTOM) {
		if (st_settings.custom_columns <= 0 || st_settings.custom_rows <= 0)
			return IO_ERROR_INVALID_ARGUMENT;
		result->columns = (uint32_t)st_settings.custom_columns;
		result->rows = (uint32_t)st_settings.custom_rows;
		if (!profile->hide_status && result->rows > 1)
			--result->rows;
		return 0;
	}
	if (mode < 0 || (size_t)mode >= sizeof(dimensions) / sizeof(dimensions[0])
	    || dimensions[mode].columns == 0)
		return IO_ERROR_INVALID_ARGUMENT;
	result->columns = dimensions[mode].columns;
	result->rows = dimensions[mode].rows;
	if (!profile->hide_status && result->rows > 1)
		--result->rows;
	return 0;
}

int
st_connection_close(st_connection_t *connection)
{
	bool connected;
	if (connection == NULL)
		return 0;
	connected = atomic_load(&connection->connected);
	st_connection_log_hooks(&connection->hooks, "Connection",
	    connected ? ST_CONNECTION_LOG_DEBUG : ST_CONNECTION_LOG_INFO,
	    "%s %s", connection->log_source, connected
	    ? "local session teardown requested" : "closing after peer or I/O teardown");
	stop_connection(connection, false);
	if (connection->input_thread_started)
		(void)st_thread_join(connection->input_thread, NULL);
	if (connection->output_thread_started)
		(void)st_thread_join(connection->output_thread, NULL);
	connection->operations->destroy(connection->transport);
	st_connection_log_hooks(&connection->hooks, "Connection", ST_CONNECTION_LOG_DEBUG,
	    "%s provider closed", connection->log_source);
	queue_destroy(&connection->output);
	queue_destroy(&connection->input);
	free(connection);
	return 0;
}
