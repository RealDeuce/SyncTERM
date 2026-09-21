/* Client-only TLS sessions for SyncTERM. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_TLS_H_
#define ST_TLS_H_

#include "socket.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct st_tls_session st_tls_session_t;

#ifdef __cplusplus
extern "C" {
#endif

enum st_tls_server_auth {
	ST_TLS_SERVER_AUTH_NONE,
	ST_TLS_SERVER_AUTH_WEB_PKI,
	ST_TLS_SERVER_AUTH_CERTIFICATE,
};

enum st_tls_protocol_version {
	ST_TLS_PROTOCOL_DEFAULT,
	ST_TLS_PROTOCOL_1_2,
	ST_TLS_PROTOCOL_1_3,
};

enum st_tls_psk_policy {
	ST_TLS_PSK_MODERN,
	ST_TLS_PSK_TLS12_COMPATIBILITY,
};

struct st_tls_diagnostic {
	int status;
	unsigned long provider_status;
	bool fatal;
	const char *provider;
	const char *message;
};

typedef void (*st_tls_diagnostic_fn)(const struct st_tls_diagnostic *record,
    void *context);

struct st_tls_client_options {
	const char *server_name;
	enum st_tls_server_auth server_auth;
	const char *trusted_certificate_file;
	const char *client_certificate_file;
	const char *client_key_file;
	const char *psk_identity;
	const void *psk;
	size_t psk_size;
	enum st_tls_protocol_version psk_version;
	enum st_tls_psk_policy psk_policy;
	enum st_tls_protocol_version minimum_version;
	enum st_tls_protocol_version maximum_version;
	int64_t handshake_timeout_ms;
	st_tls_diagnostic_fn diagnostic;
	void *diagnostic_context;
};

/*
 * Setting any PSK field selects PSK-only authentication. The identity,
 * secret, and exact TLS version must all be supplied; certificate
 * authentication options are ignored. Success guarantees that the peer
 * selected the configured PSK rather than a certificate fallback.
 *
 * Ownership of socket transfers only on success. Sessions make it
 * nonblocking internally and st_tls_close() closes it.
 */
int st_tls_client_open(st_tls_session_t **result, socket_handle_t socket,
    const struct st_tls_client_options *options);
int st_tls_send(st_tls_session_t *session, const void *data, size_t size,
    int64_t timeout_ms, size_t *sent);
int st_tls_receive(st_tls_session_t *session, void *data, size_t size,
    int64_t timeout_ms, size_t *received);
int st_tls_flush(st_tls_session_t *session, int64_t timeout_ms);
bool st_tls_has_pending(const st_tls_session_t *session);
enum st_tls_protocol_version st_tls_protocol_version(
    const st_tls_session_t *session);
const char *st_tls_cipher(const st_tls_session_t *session);
const char *st_tls_error(const st_tls_session_t *session);
/* Borrowed DER data remains valid until st_tls_close(). */
size_t st_tls_peer_certificate_count(const st_tls_session_t *session);
int st_tls_peer_certificate(const st_tls_session_t *session, size_t index,
    const void **data, size_t *size);
int st_tls_terminate(st_tls_session_t *session);
void st_tls_close(st_tls_session_t *session);

#ifdef __cplusplus
}
#endif

#endif
