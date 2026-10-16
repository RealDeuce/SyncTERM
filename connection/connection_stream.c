/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#include "connection_stream.h"
#include "st_connection_config.h"
#include "tcp_client.h"
#include "st_time.h"
#include "st_settings.h"
#include <string.h>
#include <stdio.h>

static void
capture_tls_diagnostic(const struct st_tls_diagnostic *record, void *context)
{
	struct st_connection_stream *stream = context;
	if (record->fatal) {
		(void)snprintf(stream->diagnostic, sizeof(stream->diagnostic), "%s: %s",
		    record->provider == NULL ? "TLS" : record->provider,
		    record->message == NULL ? "negotiation failed" : record->message);
		if (stream->tls_ready) {
			stream->error_pending = true;
			(void)snprintf(stream->error_title, sizeof(stream->error_title),
			    "TLS error %s data", stream->writing ? "sending" : "receiving");
		}
	}
	if (stream->hooks.log != NULL)
		stream->hooks.log(stream->hooks.log_context, record->level == ST_TLS_LOG_ERROR
		    ? ST_CONNECTION_LOG_ERROR : record->level == ST_TLS_LOG_WARNING
		    ? ST_CONNECTION_LOG_WARNING : ST_CONNECTION_LOG_DEBUG,
		    "TLS", record->message == NULL ? "TLS diagnostic" : record->message);
}

static bool
cancel_connect(void *context)
{
	return st_connection_cancelled(context, ST_CONNECTION_SETUP_CONNECTING);
}

static enum st_tls_protocol_version
tls_version(int version)
{
	return version == ST_TLS_VERSION_1_2 ? ST_TLS_PROTOCOL_1_2
	    : version == ST_TLS_VERSION_1_3 ? ST_TLS_PROTOCOL_1_3
	    : ST_TLS_PROTOCOL_DEFAULT;
}

int
st_connection_stream_open(struct st_connection_stream *stream,
	const struct st_profile *profile, bool encrypted,
	const struct st_tls_client_options *tls_options,
	const struct st_connection_hooks *hooks, struct st_connection_error *error)
{
	struct tcp_client_options tcp = {
		.family = profile->address_family == ST_ADDRESS_FAMILY_IPV4
		    ? TCP_CLIENT_ADDRESS_IPV4
		    : profile->address_family == ST_ADDRESS_FAMILY_IPV6
		    ? TCP_CLIENT_ADDRESS_IPV6 : TCP_CLIENT_ADDRESS_ANY,
		.timeout_ms = 60000,
		.cancel = cancel_connect,
		.cancel_context = (void *)hooks,
	};
	struct st_tls_client_options tls = {
		.server_name = profile->address,
		.server_auth = profile->tls_trust_web_pki ? ST_TLS_SERVER_AUTH_WEB_PKI
		    : profile->tls_trusted_certificate != NULL && profile->tls_trusted_certificate[0] != '\0'
		    ? ST_TLS_SERVER_AUTH_CERTIFICATE : ST_TLS_SERVER_AUTH_UNTRUSTED,
		.trusted_certificate_file = profile->tls_trusted_certificate,
		.client_certificate_file = profile->tls_client_certificate,
		.client_key_file = profile->tls_client_key,
		.handshake_timeout_ms = 15000,
	};
	int status;

	memset(stream, 0, sizeof(*stream));
	if (hooks != NULL) stream->hooks = *hooks;
	if (profile->hide_popups) {
		stream->hooks.alert = NULL;
		stream->hooks.choice = NULL;
		stream->hooks.help = NULL;
		stream->hooks.status = NULL;
		stream->hooks.trust_tls_certificate = NULL;
	}
	hooks = &stream->hooks;
	/* MQTT v1.10a's certificate fallback is opportunistic; Telnet/TLS must
	 * authenticate and offer certificate review when no trust is installed. */
	if (profile->connection_type == ST_CONNECTION_MQTT && tls_options == NULL
	    && !profile->tls_trust_web_pki && (profile->tls_trusted_certificate == NULL
	    || profile->tls_trusted_certificate[0] == '\0'))
		tls.server_auth = ST_TLS_SERVER_AUTH_NONE;
	if ((tls.client_certificate_file == NULL || tls.client_certificate_file[0] == '\0')
	    && (tls.client_key_file == NULL || tls.client_key_file[0] == '\0')) {
		tls.client_certificate_file = st_settings.tls_client_certificate;
		tls.client_key_file = st_settings.tls_client_key;
	}
	if (tls.client_certificate_file != NULL && tls.client_certificate_file[0] == '\0')
		tls.client_certificate_file = NULL;
	if (tls.client_key_file != NULL && tls.client_key_file[0] == '\0') tls.client_key_file = NULL;
	stream->socket = SOCKET_HANDLE_INVALID;
	if (encrypted && !ST_CONNECTION_HAVE_TLS)
		return st_connection_fail(error, IO_ERROR_NOT_SUPPORTED,
		    ST_CONNECTION_SETUP_NONE, "this build has no TLS provider");
	if (st_connection_cancelled(hooks, ST_CONNECTION_SETUP_RESOLVING))
		return st_connection_fail(error, IO_ERROR_CANCELLED,
		    ST_CONNECTION_SETUP_RESOLVING, "connection cancelled");
	status = socket_runtime_init();
	if (status != 0)
		return st_connection_fail(error, status, ST_CONNECTION_SETUP_CONNECTING,
		    "unable to initialize sockets");
	if (st_mutex_init(&stream->mutex, ST_MUTEX_PLAIN) != ST_THREAD_SUCCESS) {
		(void)socket_runtime_cleanup();
		return st_connection_fail(error, IO_ERROR_OUT_OF_MEMORY,
		    ST_CONNECTION_SETUP_CONNECTING, "unable to initialize TLS synchronization");
	}
	stream->initialized = true;
	status = tcp_client_connect(&stream->socket, NULL, profile->address,
	    profile->port != 0 ? profile->port
	    : st_connection_default_port((enum st_connection_type)profile->connection_type),
	    &tcp);
	if (status != 0) {
		st_connection_stream_close(stream);
		return st_connection_fail(error, status,
		    ST_CONNECTION_SETUP_CONNECTING, "unable to connect to host");
	}
	(void)socket_set_keepalive(stream->socket, true);
	(void)socket_set_no_delay(stream->socket, true);
	(void)socket_set_send_buffer_size(stream->socket, 1024U * 1024U);
	(void)socket_set_receive_buffer_size(stream->socket, 1024U * 1024U);
	status = socket_set_blocking(stream->socket, false);
	if (status == 0 && encrypted) {
		if (st_connection_cancelled(hooks, ST_CONNECTION_SETUP_NEGOTIATING))
			status = IO_ERROR_CANCELLED;
		else {
			if (tls_options == NULL && (profile->tls_psk[0] != '\0'
			    || profile->tls_psk_identity[0] != '\0')) {
				tls.psk_identity = profile->tls_psk_identity;
				tls.psk = profile->tls_psk;
				tls.psk_size = strlen(profile->tls_psk);
				tls.psk_version = tls_version(profile->tls_psk_version);
			}
			if (tls_options != NULL) tls = *tls_options;
			if (hooks != NULL && hooks->status != NULL)
				hooks->status(hooks->ui_context, profile->connection_type != ST_CONNECTION_MQTT
				    ? "Activating Session" : tls.psk != NULL ? "Negotiating TLS-PSK" : "Negotiating TLS (cert)");
			tls.diagnostic = capture_tls_diagnostic;
			tls.diagnostic_context = stream;
			tls.diagnostic_level = profile->protocol_log_level <= ST_PROFILE_LOG_ERROR
			    ? ST_TLS_LOG_ERROR : profile->protocol_log_level < ST_PROFILE_LOG_DEBUG
			    ? ST_TLS_LOG_WARNING : ST_TLS_LOG_DEBUG;
			status = st_connection_stream_start_tls(stream, profile, &tls, error);
			if (status != 0) {
				st_connection_stream_close(stream);
				return status;
			}
		}
	}
	if (status != 0) {
		st_connection_stream_close(stream);
		return st_connection_fail(error, status,
		    ST_CONNECTION_SETUP_NEGOTIATING, stream->diagnostic[0] == '\0'
		    ? "network/TLS negotiation failed" : stream->diagnostic);
	}
	return 0;
}

static ssize_t
stream_io(struct st_connection_stream *stream, void *data, size_t size,
	int64_t timeout_ms, bool sending)
{
	size_t amount = 0;
	ssize_t result;
	unsigned events;
	int status;
	bool pending = false;

	/* The TLS providers are not concurrently callable. Socket waits occur
	 * outside the mutex so a quiet receive cannot block the output worker. */
	if (stream->tls != NULL) {
		(void)st_mutex_lock(&stream->mutex);
		stream->writing = sending;
		status = st_tls_flush(stream->tls, 0);
		pending = status == IO_ERROR_WOULD_BLOCK || status == IO_ERROR_TIMED_OUT;
		if (status == 0 || status == IO_ERROR_WOULD_BLOCK
		    || status == IO_ERROR_TIMED_OUT)
			status = sending ? st_tls_send(stream->tls, data, size, 0, &amount)
			    : st_tls_receive(stream->tls, data, size, 0, &amount);
		(void)st_mutex_unlock(&stream->mutex);
		if (amount != 0)
			return (ssize_t)amount;
		if (!sending && status == IO_ERROR_NOT_CONNECTED)
			return 0;
		if (status != IO_ERROR_WOULD_BLOCK && status != IO_ERROR_TIMED_OUT)
			return status;
	} else {
		result = sending ? socket_send(stream->socket, data, size)
		    : socket_receive(stream->socket, data, size, SOCKET_RECEIVE_NONE);
		if (result != IO_ERROR_WOULD_BLOCK)
			return result;
	}
	if (timeout_ms == 0)
		return IO_ERROR_WOULD_BLOCK;
	status = socket_wait(stream->socket,
	    sending ? SOCKET_WAIT_WRITABLE : pending
	    ? SOCKET_WAIT_READABLE | SOCKET_WAIT_WRITABLE : SOCKET_WAIT_READABLE,
	    timeout_ms < 0 || timeout_ms > 100 ? 100 : timeout_ms, &events);
	return status == 0 ? IO_ERROR_WOULD_BLOCK : status;
}

ssize_t
st_connection_stream_read(struct st_connection_stream *stream, void *data,
	size_t size, int64_t timeout_ms)
{
	return stream_io(stream, data, size, timeout_ms, false);
}

ssize_t
st_connection_stream_write(struct st_connection_stream *stream,
	const void *data, size_t size, int64_t timeout_ms)
{
	return stream_io(stream, (void *)data, size, timeout_ms, true);
}

int
st_connection_stream_send_all(struct st_connection_stream *stream,
	const void *data, size_t size, const struct st_connection_hooks *hooks)
{
	const uint8_t *bytes = data;
	uint64_t start = 0, now;

	(void)st_time_monotonic_ms(&start);
	for (size_t sent = 0; sent < size;) {
		ssize_t amount;

		if (st_connection_cancelled(hooks, ST_CONNECTION_SETUP_NEGOTIATING))
			return IO_ERROR_CANCELLED;
		if (st_time_monotonic_ms(&now) == 0 && now - start > 15000)
			return IO_ERROR_TIMED_OUT;
		amount = st_connection_stream_write(stream, bytes + sent,
		    size - sent, 100);
		if (amount == IO_ERROR_WOULD_BLOCK || amount == IO_ERROR_TIMED_OUT)
			continue;
		if (amount <= 0)
			return amount < 0 ? (int)amount : IO_ERROR_CONNECTION_RESET;
		sent += (size_t)amount;
	}
	return 0;
}

void
st_connection_stream_interrupt(struct st_connection_stream *stream)
{
	if (stream->initialized && socket_handle_is_valid(stream->socket))
		(void)socket_shutdown(stream->socket, SOCKET_SHUTDOWN_BOTH);
}

void
st_connection_stream_close(struct st_connection_stream *stream)
{
	if (!stream->initialized)
		return;
	if (stream->tls != NULL)
		st_tls_close(stream->tls);
	else if (socket_handle_is_valid(stream->socket))
		(void)socket_close(stream->socket);
	st_mutex_destroy(&stream->mutex);
	(void)socket_runtime_cleanup();
	stream->initialized = false;
}
