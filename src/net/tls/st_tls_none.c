/* TLS-unavailable provider. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_tls.h"

#include "io_error.h"

int
st_tls_client_open(st_tls_session_t **result, socket_handle_t socket,
    const struct st_tls_client_options *options)
{
	(void)socket;
	(void)options;
	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*result = NULL;
	return IO_ERROR_NOT_SUPPORTED;
}

int st_tls_send(st_tls_session_t *s, const void *d, size_t n, int64_t t,
    size_t *sent)
{ (void)s; (void)d; (void)n; (void)t; if (sent) *sent = 0; return IO_ERROR_NOT_SUPPORTED; }
int st_tls_receive(st_tls_session_t *s, void *d, size_t n, int64_t t,
    size_t *received)
{ (void)s; (void)d; (void)n; (void)t; if (received) *received = 0; return IO_ERROR_NOT_SUPPORTED; }
int st_tls_flush(st_tls_session_t *s, int64_t t)
{ (void)s; (void)t; return IO_ERROR_NOT_SUPPORTED; }
bool st_tls_has_pending(const st_tls_session_t *s) { (void)s; return false; }
enum st_tls_protocol_version st_tls_protocol_version(const st_tls_session_t *s)
{ (void)s; return ST_TLS_PROTOCOL_DEFAULT; }
const char *st_tls_cipher(const st_tls_session_t *s) { (void)s; return NULL; }
const char *st_tls_error(const st_tls_session_t *s)
{ (void)s; return "TLS support is unavailable"; }
size_t st_tls_peer_certificate_count(const st_tls_session_t *s)
{ (void)s; return 0; }
int st_tls_peer_certificate(const st_tls_session_t *s, size_t i,
    const void **d, size_t *n)
{ (void)s; (void)i; (void)d; (void)n; return IO_ERROR_NOT_SUPPORTED; }
int st_tls_terminate(st_tls_session_t *s)
{ (void)s; return IO_ERROR_NOT_SUPPORTED; }
void st_tls_close(st_tls_session_t *s) { (void)s; }
