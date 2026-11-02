/* Incremental Telnet framing shared by TCP and TLS sessions.
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#include "connection_stream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { TN_SE = 240, TN_SB = 250, TN_WILL = 251, TN_WONT = 252,
	TN_DO = 253, TN_DONT = 254, TN_IAC = 255 };
enum { TN_BINARY = 0, TN_ECHO = 1, TN_SGA = 3, TN_TYPE = 24, TN_NAWS = 31 };
enum q_state { Q_NO, Q_YES, Q_WANTNO, Q_WANTYES };
struct q_option { enum q_state state; bool opposite; };

static bool
enabled(const struct q_option *option)
{
	return option->state == Q_YES || option->state == Q_WANTNO;
}

struct telnet_transport {
	struct st_connection_stream stream;
	st_connection_t *connection;
	st_mutex_t mutex; /* options, geometry, and outgoing NVT CR state */
	struct q_option local[256], remote[256];
	struct st_screen_geometry geometry;
	char terminal[ST_PROFILE_TERMINAL_NAME_MAX + 1];
	uint8_t command, sub[128];
	size_t sub_size;
	enum { TN_DATA, TN_COMMAND, TN_OPTION, TN_SUB, TN_SUB_IAC } parser;
	bool sub_overflow, incoming_cr, outgoing_cr, deferred, no_binary;
	int error;
};

static void
log_command(struct telnet_transport *t, const char *direction, uint8_t cmd, uint8_t option)
{
	const char *name = option == TN_BINARY ? "Binary Transmission"
	    : option == TN_ECHO ? "Echo" : option == TN_SGA ? "Suppress Go Ahead"
	    : option == TN_TYPE ? "Terminal Type" : option == TN_NAWS ? "Negotiate About Window Size" : NULL;
	char unknown[32];
	if (name == NULL) {
		(void)snprintf(unknown, sizeof(unknown), "Unknown option %u", option);
		name = unknown;
	}
	st_connection_log(t->connection, ST_CONNECTION_LOG_INFO, "%s: %s %s", direction,
	    cmd == TN_WILL ? "WILL" : cmd == TN_WONT ? "WONT" : cmd == TN_DO ? "DO" : "DONT", name);
}

static int
command(struct telnet_transport *t, uint8_t cmd, uint8_t option)
{
	const uint8_t packet[] = {TN_IAC, cmd, option};
	ssize_t n = st_connection_queue_output_atomic(t->connection, packet, 3, -1);
	if (n == 3) log_command(t, "TX", cmd, option);
	return n == 3 ? 0 : n < 0 ? (int)n : IO_ERROR_IO;
}

/* RFC 1143: each direction has its own negotiation state and one queued
 * opposite request. Requests do not become effective until acknowledged. */
static int
request(struct telnet_transport *t, bool local, uint8_t option, bool enable)
{
	struct q_option *q = local ? &t->local[option] : &t->remote[option];
	uint8_t positive = local ? TN_WILL : TN_DO;
	uint8_t negative = local ? TN_WONT : TN_DONT;

	if (enable) {
		if (q->state == Q_NO) {
			q->state = Q_WANTYES;
			return command(t, positive, option);
		}
		if (q->state == Q_WANTNO)
			q->opposite = true;
		else if (q->state == Q_WANTYES)
			q->opposite = false;
	} else {
		if (q->state == Q_YES) {
			q->state = Q_WANTNO;
			return command(t, negative, option);
		}
		if (q->state == Q_WANTYES)
			q->opposite = true;
		else if (q->state == Q_WANTNO)
			q->opposite = false;
	}
	return 0;
}

static int
initial_state(struct telnet_transport *t)
{
	int status = request(t, true, TN_SGA, true);
	if (status == 0) status = request(t, false, TN_SGA, true);
	if (status == 0 && !t->no_binary)
		status = request(t, true, TN_BINARY, true);
	if (status == 0 && !t->no_binary)
		status = request(t, false, TN_BINARY, true);
	if (status == 0) status = request(t, false, TN_ECHO, true);
	t->deferred = false;
	return status;
}

static int
subnegotiation(struct telnet_transport *t, const uint8_t *bytes, size_t size)
{
	uint8_t packet[2 * (ST_PROFILE_TERMINAL_NAME_MAX + 2) + 4];
	size_t used = 0;
	ssize_t n;

	packet[used++] = TN_IAC;
	packet[used++] = TN_SB;
	for (size_t i = 0; i < size; ++i) {
		packet[used++] = bytes[i];
		if (bytes[i] == TN_IAC) packet[used++] = TN_IAC;
	}
	packet[used++] = TN_IAC;
	packet[used++] = TN_SE;
	n = st_connection_queue_output_atomic(t->connection, packet, used, -1);
	return n == (ssize_t)used ? 0 : n < 0 ? (int)n : IO_ERROR_IO;
}

static int
window_size(struct telnet_transport *t)
{
	uint8_t bytes[] = {TN_NAWS,
	    (uint8_t)(t->geometry.columns >> 8), (uint8_t)t->geometry.columns,
	    (uint8_t)(t->geometry.rows >> 8), (uint8_t)t->geometry.rows};
	if (!enabled(&t->local[TN_NAWS])) return 0;
	int status = subnegotiation(t, bytes, sizeof(bytes));
	if (status == 0) st_connection_log(t->connection, ST_CONNECTION_LOG_INFO,
	    "TX: Window Size is %u x %u", t->geometry.columns, t->geometry.rows);
	return status;
}

static int
negotiate(struct telnet_transport *t, uint8_t cmd, uint8_t option)
{
	bool local = cmd == TN_DO || cmd == TN_DONT;
	bool positive = cmd == TN_DO || cmd == TN_WILL;
	struct q_option *q = local ? &t->local[option] : &t->remote[option];
	bool supported = option == TN_BINARY || option == TN_SGA
	    || option == TN_TYPE || option == TN_NAWS
	    || (!local && option == TN_ECHO);
	int status = 0;

	log_command(t, "RX", cmd, option);
	if (positive) {
		switch (q->state) {
		case Q_NO:
			if (supported) q->state = Q_YES;
			status = command(t, local
			    ? (supported ? TN_WILL : TN_WONT)
			    : (supported ? TN_DO : TN_DONT), option);
			break;
		case Q_YES: break;
		case Q_WANTNO:
			q->state = q->opposite ? Q_YES : Q_NO;
			q->opposite = false;
			break;
		case Q_WANTYES:
			q->state = q->opposite ? Q_WANTNO : Q_YES;
			if (q->opposite)
				status = command(t, local ? TN_WONT : TN_DONT, option);
			q->opposite = false;
			break;
		}
	} else {
		switch (q->state) {
		case Q_NO: break;
		case Q_YES:
			q->state = Q_NO;
			status = command(t, local ? TN_WONT : TN_DONT, option);
			break;
		case Q_WANTNO:
			q->state = q->opposite ? Q_WANTYES : Q_NO;
			if (q->opposite)
				status = command(t, local ? TN_WILL : TN_DO, option);
			q->opposite = false;
			break;
		case Q_WANTYES:
			q->state = Q_NO;
			q->opposite = false;
			break;
		}
	}
	atomic_store(&t->connection->binary_mode,
	    enabled(&t->local[TN_BINARY]) && enabled(&t->remote[TN_BINARY]));
	if (status == 0 && local && option == TN_NAWS && positive)
		status = window_size(t);
	return status;
}

static int
finish_sub(struct telnet_transport *t)
{
	uint8_t bytes[ST_PROFILE_TERMINAL_NAME_MAX + 2];
	size_t length;
	int status = 0;

	if (!t->sub_overflow && t->sub_size == 2
	    && t->sub[0] == TN_TYPE && t->sub[1] == 1) {
		length = strlen(t->terminal);
		bytes[0] = TN_TYPE;
		bytes[1] = 0;
		memcpy(bytes + 2, t->terminal, length);
		status = subnegotiation(t, bytes, length + 2);
		if (status == 0) st_connection_log(t->connection, ST_CONNECTION_LOG_INFO,
		    "TX: Terminal Type is %s", t->terminal);
		if (status == 0) status = request(t, true, TN_NAWS, true);
	}
	return status;
}

static ssize_t
telnet_read(void *state, void *data, size_t size, int64_t timeout_ms)
{
	struct telnet_transport *t = state;
	uint8_t raw[4096], *output = data;
	size_t used = 0;
	ssize_t count;
	int status = 0;

	if (t->error != 0) return t->error;
	count = st_connection_stream_read(&t->stream, raw,
	    size < sizeof(raw) ? size : sizeof(raw), timeout_ms);
	if (count <= 0) return count;
	(void)st_mutex_lock(&t->mutex);
	for (ssize_t i = 0; i < count && status == 0; ++i) {
		uint8_t byte = raw[i];
		switch (t->parser) {
		case TN_DATA:
			if (byte == TN_IAC) t->parser = TN_COMMAND;
			else {
				if (!(t->incoming_cr && byte == 0
				    && !enabled(&t->remote[TN_BINARY])))
					output[used++] = byte;
				t->incoming_cr = byte == '\r';
			}
			break;
		case TN_COMMAND:
			if (t->deferred && byte != TN_IAC) status = initial_state(t);
			if (byte == TN_IAC) {
				output[used++] = byte;
				t->incoming_cr = false;
				t->parser = TN_DATA;
			} else if (byte >= TN_WILL && byte <= TN_DONT) {
				t->command = byte;
				t->parser = TN_OPTION;
			} else if (byte == TN_SB) {
				t->sub_size = 0;
				t->sub_overflow = false;
				t->parser = TN_SUB;
			} else t->parser = TN_DATA;
			break;
		case TN_OPTION:
			status = negotiate(t, t->command, byte);
			t->parser = TN_DATA;
			break;
		case TN_SUB:
			if (byte == TN_IAC) t->parser = TN_SUB_IAC;
			else if (t->sub_size < sizeof(t->sub))
				t->sub[t->sub_size++] = byte;
			else t->sub_overflow = true;
			break;
		case TN_SUB_IAC:
			if (byte == TN_SE) {
				status = finish_sub(t);
				t->parser = TN_DATA;
			} else {
				if (byte == TN_IAC && t->sub_size < sizeof(t->sub))
					t->sub[t->sub_size++] = byte;
				else t->sub_overflow = true;
				t->parser = TN_SUB;
			}
			break;
		}
	}
	(void)st_mutex_unlock(&t->mutex);
	return status != 0 ? status : used != 0 ? (ssize_t)used : IO_ERROR_WOULD_BLOCK;
}

static ssize_t
telnet_send(st_connection_t *connection, const void *data, size_t size,
	int64_t timeout_ms, bool atomic_write, enum st_connection_send_kind kind)
{
	struct telnet_transport *t = connection->transport;
	const uint8_t *bytes = data;
	uint8_t output[8192];
	size_t consumed = size < 4096 ? size : 4096, used = 0;
	bool cr;
	ssize_t sent;

	if (atomic_write && size > ST_CONNECTION_ATOMIC_SEND_MAX)
		return IO_ERROR_MESSAGE_TOO_LARGE;
	(void)st_mutex_lock(&t->mutex);
	cr = kind == ST_CONNECTION_SEND_TEXT && t->outgoing_cr;
	for (size_t i = 0; i < consumed; ++i) {
		uint8_t byte = bytes[i];
		if (cr && byte == '\n') { cr = false; continue; }
		cr = false;
		output[used++] = byte;
		if (byte == TN_IAC) output[used++] = byte;
		if (byte == '\r' && !enabled(&t->local[TN_BINARY])) {
			output[used++] = kind == ST_CONNECTION_SEND_TEXT ? '\n' : '\0';
			cr = kind == ST_CONNECTION_SEND_TEXT;
		}
	}
	sent = st_connection_queue_output_atomic(connection, output, used, timeout_ms);
	if (sent == (ssize_t)used) {
		t->outgoing_cr = cr;
		sent = (ssize_t)consumed;
	}
	(void)st_mutex_unlock(&t->mutex);
	return sent;
}

static void
telnet_bind(void *state, st_connection_t *connection)
{
	struct telnet_transport *t = state;
	t->connection = connection;
	atomic_store(&connection->binary_mode, false);
	if (!t->deferred) t->error = initial_state(t);
}

static ssize_t
telnet_write(void *state, const void *data, size_t size, int64_t timeout_ms)
{
	struct telnet_transport *t = state;
	return st_connection_stream_write(&t->stream, data, size, timeout_ms);
}

static void
telnet_interrupt(void *state)
{
	struct telnet_transport *t = state;
	st_connection_stream_interrupt(&t->stream);
}

static void
telnet_destroy(void *state)
{
	struct telnet_transport *t = state;
	st_connection_stream_close(&t->stream);
	st_mutex_destroy(&t->mutex);
	free(t);
}

static int
telnet_resize(void *state, const struct st_screen_geometry *geometry)
{
	struct telnet_transport *t = state;
	int status;
	if (geometry->columns > UINT16_MAX || geometry->rows > UINT16_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	(void)st_mutex_lock(&t->mutex);
	t->geometry = *geometry;
	status = window_size(t);
	(void)st_mutex_unlock(&t->mutex);
	return status;
}

static int
telnet_binary(void *state, bool enabled)
{
	struct telnet_transport *t = state;
	int status;
	(void)st_mutex_lock(&t->mutex);
	status = request(t, true, TN_BINARY, enabled);
	if (status == 0) status = request(t, false, TN_BINARY, enabled);
	(void)st_mutex_unlock(&t->mutex);
	return status;
}

static int
telnet_process_requests(void *state)
{
	struct telnet_transport *t = state;
	return st_connection_stream_process_requests(&t->stream);
}

static const struct st_connection_transport_ops operations = {
	.bind = telnet_bind, .read = telnet_read, .write = telnet_write,
	.interrupt = telnet_interrupt, .destroy = telnet_destroy,
	.update_terminal_size = telnet_resize, .set_binary_mode = telnet_binary,
	.process_requests = telnet_process_requests,
};

int
st_connection_connect_telnet(st_connection_t **result,
	const struct st_profile *profile, const struct st_screen_geometry *current,
	const struct st_connection_hooks *hooks, struct st_connection_error *error)
{
	struct telnet_transport *t;
	int status;
	if (result == NULL || profile == NULL || profile->address[0] == '\0'
	    || (profile->connection_type != ST_CONNECTION_TELNET
	    && profile->connection_type != ST_CONNECTION_TELNET_TLS))
		return st_connection_fail(error, IO_ERROR_INVALID_ARGUMENT,
		    ST_CONNECTION_SETUP_NONE, "invalid Telnet profile");
	t = calloc(1, sizeof(*t));
	if (t == NULL) return IO_ERROR_OUT_OF_MEMORY;
	if (st_mutex_init(&t->mutex, ST_MUTEX_PLAIN) != ST_THREAD_SUCCESS) {
		free(t);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	t->deferred = profile->defer_telnet_negotiation;
	t->no_binary = profile->telnet_no_binary;
	strcpy(t->terminal, st_connection_terminal_name(profile));
	status = st_connection_profile_geometry(profile, current, &t->geometry);
	if (status == 0)
		status = st_connection_stream_open(&t->stream, profile,
		    profile->connection_type == ST_CONNECTION_TELNET_TLS, NULL, hooks, error);
	if (status != 0) { telnet_destroy(t); return status; }
	return st_connection_start(result, t, &operations,
	    profile->connection_type == ST_CONNECTION_TELNET_TLS ? "Telnet/TLS" : "Telnet",
	    hooks, telnet_send, error);
}
