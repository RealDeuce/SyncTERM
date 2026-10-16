/* OpenSSL client-only TLS sessions for SyncTERM. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_tls.h"

#include "io_error.h"
#include "st_time.h"
#include "string_compat.h"
#include "xp_crypto.h"

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct peer_certificate {
	unsigned char *data;
	size_t size;
};

struct st_tls_session {
	socket_handle_t socket;
	SSL_CTX *context;
	SSL *ssl;
	char error[256];
	char *psk_identity;
	unsigned char *psk;
	size_t psk_size;
	struct peer_certificate *peer_certificates;
	size_t peer_certificate_count;
	st_tls_diagnostic_fn diagnostic;
	void *diagnostic_context;
	enum st_tls_log_level diagnostic_level;
	st_tls_peer_chain_fn peer_chain;
	void *peer_chain_context;
	bool terminating;
};

static char *
certificate_text(X509 *certificate, unsigned field)
{
	BIO *bio = BIO_new(BIO_s_mem());
	char *result = NULL, *bytes = NULL;
	long size;
	if (bio == NULL) return NULL;
	if (field < 2)
		(void)X509_NAME_print_ex(bio, field == 0
		    ? X509_get_subject_name(certificate) : X509_get_issuer_name(certificate),
		    0, XN_FLAG_RFC2253);
	else if (field < 4) {
		struct tm value;
		const ASN1_TIME *time = field == 2 ? X509_get0_notBefore(certificate)
		    : X509_get0_notAfter(certificate);
		if (ASN1_TIME_to_tm(time, &value) == 1)
			(void)BIO_printf(bio, "%04d-%02d-%02d %02d:%02d:%02d UTC",
			    value.tm_year + 1900, value.tm_mon + 1, value.tm_mday,
			    value.tm_hour, value.tm_min, value.tm_sec);
	} else if (field == 4) {
		unsigned char hash[EVP_MAX_MD_SIZE];
		unsigned length = 0;
		if (X509_digest(certificate, EVP_sha256(), hash, &length) == 1)
			for (unsigned i = 0; i < length; ++i)
				(void)BIO_printf(bio, "%s%02X", i == 0 ? "" : ":", hash[i]);
	} else
		(void)PEM_write_bio_X509(bio, certificate);
	size = BIO_get_mem_data(bio, &bytes);
	if (size >= 0 && (unsigned long)size < SIZE_MAX) {
		result = malloc((size_t)size + 1);
		if (result != NULL) {
			memcpy(result, bytes, (size_t)size);
			result[size] = '\0';
		}
	}
	BIO_free(bio);
	return result;
}

static void
report_peer_chain(st_tls_session_t *session)
{
	STACK_OF(X509) *chain = SSL_get_peer_cert_chain(session->ssl);
	int count = chain == NULL ? 0 : sk_X509_num(chain);
	struct st_tls_peer_certificate *records;
	char **strings;
	if (session->peer_chain == NULL || count <= 0) return;
	records = calloc((size_t)count, sizeof(*records));
	strings = calloc((size_t)count, 6 * sizeof(*strings));
	if (records == NULL || strings == NULL) goto finished;
	for (int i = 0; i < count; ++i) {
		char **text = strings + (size_t)i * 6;
		for (unsigned field = 0; field < 6; ++field) {
			text[field] = certificate_text(sk_X509_value(chain, i), field);
			if (text[field] == NULL) goto finished;
		}
		records[i] = (struct st_tls_peer_certificate){
		    text[0], text[1], text[2], text[3], text[4], text[5]};
	}
	session->peer_chain(records, (size_t)count, session->peer_chain_context);
finished:
	if (strings != NULL)
		for (size_t i = 0; i < (size_t)count * 6; ++i) free(strings[i]);
	free(strings);
	free(records);
}

static void
free_peer_certificates(st_tls_session_t *session)
{
	for (size_t i = 0; i < session->peer_certificate_count; ++i)
		free(session->peer_certificates[i].data);
	free(session->peer_certificates);
	session->peer_certificates = NULL;
	session->peer_certificate_count = 0;
}

static int
capture_peer_certificates(st_tls_session_t *session)
{
	X509 *leaf = SSL_get0_peer_certificate(session->ssl);
	STACK_OF(X509) *chain = SSL_get_peer_cert_chain(session->ssl);
	int chain_count = chain == NULL ? 0 : sk_X509_num(chain);
	bool chain_has_leaf = chain_count != 0 && sk_X509_value(chain, 0) == leaf;
	int count = chain_count + (leaf != NULL && !chain_has_leaf ? 1 : 0);

	if (count <= 0)
		return 0;
	session->peer_certificates = calloc((size_t)count,
	    sizeof(*session->peer_certificates));
	if (session->peer_certificates == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	for (int i = 0; i < count; ++i) {
		X509 *certificate = leaf != NULL && !chain_has_leaf && i == 0
		    ? leaf : sk_X509_value(chain,
		    i - (leaf != NULL && !chain_has_leaf ? 1 : 0));
		int length = i2d_X509(certificate, NULL);
		unsigned char *cursor;

		if (length <= 0) {
			free_peer_certificates(session);
			return IO_ERROR_INVALID_FORMAT;
		}
		session->peer_certificates[i].data = malloc((size_t)length);
		if (session->peer_certificates[i].data == NULL) {
			free_peer_certificates(session);
			return IO_ERROR_OUT_OF_MEMORY;
		}
		session->peer_certificate_count++;
		cursor = session->peer_certificates[i].data;
		if (i2d_X509(certificate, &cursor) != length) {
			free_peer_certificates(session);
			return IO_ERROR_INVALID_FORMAT;
		}
		session->peer_certificates[i].size = (size_t)length;
	}
	return 0;
}

static void
diagnose(st_tls_session_t *session, enum st_tls_log_level level,
	int status, bool fatal, const char *message)
{
	unsigned long native = status < 0 ? ERR_peek_last_error() : 0;
	if (status < 0)
		(void)snprintf(session->error, sizeof(session->error), "%s", message);
	if (session->diagnostic != NULL && level <= session->diagnostic_level) {
		const struct st_tls_diagnostic diagnostic = {
			status, native, fatal, "OpenSSL", message, level
		};
		session->diagnostic(&diagnostic, session->diagnostic_context);
	}
}

static void
report(st_tls_session_t *session, int status, bool fatal, const char *message)
{
	diagnose(session, ST_TLS_LOG_ERROR, status, fatal, message);
}

static void
openssl_diagnostic(const SSL *ssl, int where, int value)
{
	st_tls_session_t *session = SSL_get_app_data(ssl);
	if (session == NULL) return;
	if ((where & SSL_CB_ALERT) != 0) {
		bool fatal = ((value >> 8) & 255) == SSL3_AL_FATAL;
		enum st_tls_log_level level = (value & 255) == SSL_AD_CLOSE_NOTIFY
		    ? ST_TLS_LOG_DEBUG : fatal ? ST_TLS_LOG_ERROR : ST_TLS_LOG_WARNING;
		char message[256];
		(void)snprintf(message, sizeof(message), "%s %s alert: %s",
		    (where & SSL_CB_WRITE) != 0 ? "sent" : "received",
		    SSL_alert_type_string_long(value), SSL_alert_desc_string_long(value));
		diagnose(session, level, 0, fatal, message);
		return;
	}
	if ((where & SSL_CB_HANDSHAKE_START) != 0)
		diagnose(session, ST_TLS_LOG_DEBUG, 0, false, "handshake started");
	if ((where & SSL_CB_LOOP) != 0)
		diagnose(session, ST_TLS_LOG_DEBUG, 0, false, SSL_state_string_long(ssl));
	if ((where & SSL_CB_HANDSHAKE_DONE) != 0) {
		char message[256];
		(void)snprintf(message, sizeof(message), "handshake completed: %s, %s",
		    SSL_get_version(ssl), SSL_get_cipher_name(ssl));
		diagnose(session, ST_TLS_LOG_DEBUG, 0, false, message);
	}
}

static int
wait_for_ssl(st_tls_session_t *session, int ssl_error,
    const struct st_deadline *deadline)
{
	unsigned interests;
	unsigned events;
	int64_t remaining;
	int status;
	if (ssl_error == SSL_ERROR_WANT_READ)
		interests = SOCKET_WAIT_READABLE;
	else if (ssl_error == SSL_ERROR_WANT_WRITE)
		interests = SOCKET_WAIT_WRITABLE;
	else
		return IO_ERROR_PROTOCOL;
	status = st_deadline_remaining_ms(deadline, &remaining);
	return status < 0 ? status : socket_wait(session->socket, interests,
	    remaining, &events);
}

static unsigned int
psk_client(SSL *ssl, const char *hint, char *identity,
    unsigned int identity_capacity, unsigned char *psk,
    unsigned int psk_capacity)
{
	st_tls_session_t *session = SSL_get_app_data(ssl);
	size_t identity_size;
	(void)hint;
	if (session == NULL || session->psk_identity == NULL)
		return 0;
	identity_size = strlen(session->psk_identity);
	if (identity_size + 1 > identity_capacity
	    || session->psk_size > psk_capacity || session->psk_size > UINT_MAX)
		return 0;
	memcpy(identity, session->psk_identity, identity_size + 1);
	memcpy(psk, session->psk, session->psk_size);
	return (unsigned int)session->psk_size;
}

static int
psk_session(SSL *ssl, const EVP_MD *digest, const unsigned char **identity,
    size_t *identity_size, SSL_SESSION **result)
{
	static const unsigned char cipher_id[] = { 0x13, 0x01 };
	st_tls_session_t *session = SSL_get_app_data(ssl);
	SSL_SESSION *created;
	const SSL_CIPHER *cipher;
	(void)digest;
	if (session == NULL || session->psk_identity == NULL)
		return 0;
	created = SSL_SESSION_new();
	cipher = SSL_CIPHER_find(ssl, cipher_id);
	if (created == NULL || cipher == NULL
	    || !SSL_SESSION_set1_master_key(created, session->psk,
	    session->psk_size)
	    || !SSL_SESSION_set_protocol_version(created, TLS1_3_VERSION)
	    || !SSL_SESSION_set_cipher(created, cipher)) {
		SSL_SESSION_free(created);
		return 0;
	}
	*identity = (const unsigned char *)session->psk_identity;
	*identity_size = strlen(session->psk_identity);
	*result = created;
	return 1;
}

static int
set_versions(SSL_CTX *context, enum st_tls_protocol_version minimum,
    enum st_tls_protocol_version maximum)
{
	int low = minimum == ST_TLS_PROTOCOL_1_3
	    ? TLS1_3_VERSION : TLS1_2_VERSION;
	int high = maximum == ST_TLS_PROTOCOL_1_2
	    ? TLS1_2_VERSION : TLS1_3_VERSION;
	return SSL_CTX_set_min_proto_version(context, low)
	    && SSL_CTX_set_max_proto_version(context, high)
	    ? 0 : IO_ERROR_PROTOCOL;
}

static int
set_peer_name(SSL *ssl, const char *name)
{
	ASN1_OCTET_STRING *address = a2i_IPADDRESS(name);
	int result;
	if (address != NULL) {
		result = X509_VERIFY_PARAM_set1_ip(SSL_get0_param(ssl),
		    ASN1_STRING_get0_data(address), (size_t)ASN1_STRING_length(address));
		ASN1_OCTET_STRING_free(address);
	} else {
		ERR_clear_error();
		result = SSL_set1_host(ssl, name);
	}
	return result;
}

int
st_tls_client_open(st_tls_session_t **result, socket_handle_t socket,
    const struct st_tls_client_options *options)
{
	st_tls_session_t *session;
	struct st_deadline deadline;
	bool psk;
	bool client_certificate;
	int status;

	if (result == NULL || options == NULL || !socket_handle_is_valid(socket))
		return IO_ERROR_INVALID_ARGUMENT;
	if ((unsigned)options->server_auth > ST_TLS_SERVER_AUTH_UNTRUSTED)
		return IO_ERROR_INVALID_ARGUMENT;
	if ((unsigned)options->diagnostic_level > ST_TLS_LOG_DEBUG)
		return IO_ERROR_INVALID_ARGUMENT;
	*result = NULL;
	psk = options->psk != NULL || options->psk_size != 0
	    || options->psk_identity != NULL;
	client_certificate = options->client_certificate_file != NULL
	    || options->client_key_file != NULL;
	if (psk && (options->psk == NULL || options->psk_size == 0
	    || options->psk_identity == NULL || options->psk_identity[0] == '\0'
	    || (options->psk_version != ST_TLS_PROTOCOL_1_2
	    && options->psk_version != ST_TLS_PROTOCOL_1_3)))
		return IO_ERROR_INVALID_ARGUMENT;
	if (!psk && client_certificate
	    && (options->client_certificate_file == NULL
	    || options->client_key_file == NULL))
		return IO_ERROR_INVALID_ARGUMENT;
	if (!psk && options->server_auth == ST_TLS_SERVER_AUTH_CERTIFICATE
	    && options->trusted_certificate_file == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (!psk && options->server_auth != ST_TLS_SERVER_AUTH_NONE
	    && (options->server_name == NULL || options->server_name[0] == '\0'))
		return IO_ERROR_INVALID_ARGUMENT;
	session = calloc(1, sizeof(*session));
	if (session == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	session->socket = socket;
	session->diagnostic = options->diagnostic;
	session->diagnostic_context = options->diagnostic_context;
	session->diagnostic_level = options->diagnostic_level;
	session->peer_chain = options->peer_chain;
	session->peer_chain_context = options->peer_chain_context;
	session->context = SSL_CTX_new(TLS_client_method());
	if (session->context == NULL) {
		status = IO_ERROR_OUT_OF_MEMORY;
		goto fail;
	}
	status = set_versions(session->context,
	    psk ? options->psk_version : options->minimum_version,
	    psk ? options->psk_version : options->maximum_version);
	if (status < 0)
		goto fail;
	if (psk) {
		session->psk_identity = strdup(options->psk_identity);
		session->psk = malloc(options->psk_size);
		if (session->psk_identity == NULL || session->psk == NULL) {
			status = IO_ERROR_OUT_OF_MEMORY;
			goto fail;
		}
		memcpy(session->psk, options->psk, options->psk_size);
		session->psk_size = options->psk_size;
		if (options->psk_version == ST_TLS_PROTOCOL_1_2) {
			const char *cipher_list = options->psk_policy
			    == ST_TLS_PSK_TLS12_COMPATIBILITY
			    ? "ECDHE-PSK:DHE-PSK:PSK"
			    : "kECDHEPSK:kDHEPSK:!CBC:!eNULL:!RC4:!3DES:!DES";
			if (!SSL_CTX_set_cipher_list(session->context, cipher_list)) {
				status = IO_ERROR_NOT_SUPPORTED;
				goto fail;
			}
			SSL_CTX_set_psk_client_callback(session->context, psk_client);
		} else {
			SSL_CTX_set_psk_use_session_callback(session->context, psk_session);
		}
		SSL_CTX_set_verify(session->context, SSL_VERIFY_NONE, NULL);
	} else {
		if (options->server_auth == ST_TLS_SERVER_AUTH_WEB_PKI) {
			if (!SSL_CTX_set_default_verify_paths(session->context)) {
				status = IO_ERROR_NOT_FOUND;
				goto fail;
			}
			SSL_CTX_set_verify(session->context, SSL_VERIFY_PEER, NULL);
		} else if (options->server_auth == ST_TLS_SERVER_AUTH_CERTIFICATE) {
			if (!SSL_CTX_load_verify_locations(session->context,
			    options->trusted_certificate_file, NULL)) {
				status = IO_ERROR_NOT_FOUND;
				goto fail;
			}
			SSL_CTX_set_verify(session->context, SSL_VERIFY_PEER, NULL);
			(void)X509_VERIFY_PARAM_set_flags(SSL_CTX_get0_param(session->context),
			    X509_V_FLAG_PARTIAL_CHAIN);
		} else if (options->server_auth == ST_TLS_SERVER_AUTH_UNTRUSTED) {
			SSL_CTX_set_verify(session->context, SSL_VERIFY_PEER, NULL);
		} else
			SSL_CTX_set_verify(session->context, SSL_VERIFY_NONE, NULL);
		if (client_certificate) {
			if (!SSL_CTX_use_certificate_chain_file(session->context,
			    options->client_certificate_file)
			    || !SSL_CTX_use_PrivateKey_file(session->context,
			    options->client_key_file, SSL_FILETYPE_PEM)
			    || !SSL_CTX_check_private_key(session->context)) {
				status = IO_ERROR_INVALID_FORMAT;
				goto fail;
			}
		}
	}
	session->ssl = SSL_new(session->context);
	if (session->ssl == NULL) {
		status = IO_ERROR_OUT_OF_MEMORY;
		goto fail;
	}
	SSL_set_app_data(session->ssl, session);
	SSL_set_info_callback(session->ssl, openssl_diagnostic);
	if (options->server_name != NULL && options->server_name[0] != '\0') {
		if (!SSL_set_tlsext_host_name(session->ssl, options->server_name)
		    || (!psk && options->server_auth != ST_TLS_SERVER_AUTH_NONE
		    && !set_peer_name(session->ssl, options->server_name))) {
			status = IO_ERROR_INVALID_ARGUMENT;
			goto fail;
		}
	}
	if (!SSL_set_fd(session->ssl, (int)socket)) {
		status = IO_ERROR_IO;
		goto fail;
	}
	status = socket_set_blocking(socket, false);
	if (status < 0)
		goto fail;
	status = st_deadline_start(&deadline,
	    options->handshake_timeout_ms == 0 ? 30000
	    : options->handshake_timeout_ms);
	if (status < 0)
		goto fail_blocking;
	for (;;) {
		int result_code = SSL_connect(session->ssl);
		int ssl_error;
		if (result_code == 1)
			break;
		ssl_error = SSL_get_error(session->ssl, result_code);
		status = wait_for_ssl(session, ssl_error, &deadline);
		if (status < 0) {
			long verification = SSL_get_verify_result(session->ssl);
			char message[256];
			unsigned long native = ERR_peek_last_error();
			if (verification != X509_V_OK) {
				status = IO_ERROR_PERMISSION_DENIED;
				(void)snprintf(message, sizeof(message), "Certificate verification failed: %s",
				    X509_verify_cert_error_string(verification));
			} else if (native != 0)
				ERR_error_string_n(native, message, sizeof(message));
			else
				(void)snprintf(message, sizeof(message), "TLS handshake failed (%d)", status);
			report(session, status, true, message);
			goto fail_blocking;
		}
	}
	if (psk && (SSL_version(session->ssl) >= TLS1_3_VERSION
	    ? SSL_session_reused(session->ssl) != 1
	    : SSL_get_psk_identity(session->ssl) == NULL)) {
		status = IO_ERROR_PROTOCOL;
		report(session, status, true,
		    "TLS handshake did not select the configured PSK");
		goto fail_blocking;
	}
	status = capture_peer_certificates(session);
	if (status < 0)
		goto fail_blocking;
	report_peer_chain(session);
	session->peer_chain = NULL;
	session->peer_chain_context = NULL;
	*result = session;
	return 0;

fail_blocking:
	if (session->ssl != NULL) report_peer_chain(session);
	(void)socket_set_blocking(socket, true);
fail:
	if (session->ssl != NULL)
		SSL_free(session->ssl);
	if (session->context != NULL)
		SSL_CTX_free(session->context);
	xp_crypto_clear(session->psk, session->psk_size);
	free(session->psk);
	free(session->psk_identity);
	free_peer_certificates(session);
	free(session);
	return status;
}

static int
ssl_io(st_tls_session_t *session, bool writing, void *data, size_t size,
    int64_t timeout_ms, size_t *transferred)
{
	struct st_deadline deadline;
	int status;

	if (transferred != NULL)
		*transferred = 0;
	if (session == NULL || (data == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_deadline_start(&deadline, timeout_ms);
	if (status < 0)
		return status;
	for (;;) {
		size_t count = 0;
		int result_code = writing
		    ? SSL_write_ex(session->ssl, data, size, &count)
		    : SSL_read_ex(session->ssl, data, size, &count);
		int ssl_error;
		if (result_code == 1) {
			if (transferred != NULL)
				*transferred = count;
			return 0;
		}
		ssl_error = SSL_get_error(session->ssl, result_code);
		if (ssl_error == SSL_ERROR_ZERO_RETURN)
			return IO_ERROR_NOT_CONNECTED;
		{
			status = wait_for_ssl(session, ssl_error, &deadline);
			if (status < 0) {
				if (status != IO_ERROR_TIMED_OUT) {
					char message[256];
					unsigned long native = ERR_peek_last_error();
					if (native != 0) ERR_error_string_n(native, message, sizeof(message));
					else (void)snprintf(message, sizeof(message), "TLS I/O failed (%d)", status);
					report(session, status, true, message);
				}
				return status;
			}
		}
	}
}

int
st_tls_send(st_tls_session_t *session, const void *data, size_t size,
    int64_t timeout_ms, size_t *sent)
{
	return ssl_io(session, true, (void *)data, size, timeout_ms, sent);
}

int
st_tls_receive(st_tls_session_t *session, void *data, size_t size,
    int64_t timeout_ms, size_t *received)
{
	return ssl_io(session, false, data, size, timeout_ms, received);
}

int
st_tls_flush(st_tls_session_t *session, int64_t timeout_ms)
{
	(void)timeout_ms;
	return session == NULL ? IO_ERROR_INVALID_ARGUMENT : 0;
}

bool
st_tls_has_pending(const st_tls_session_t *session)
{
	return session != NULL && SSL_pending(session->ssl) > 0;
}

enum st_tls_protocol_version
st_tls_protocol_version(const st_tls_session_t *session)
{
	int version;
	if (session == NULL)
		return ST_TLS_PROTOCOL_DEFAULT;
	version = SSL_version(session->ssl);
	return version == TLS1_2_VERSION ? ST_TLS_PROTOCOL_1_2
	    : version == TLS1_3_VERSION ? ST_TLS_PROTOCOL_1_3
	    : ST_TLS_PROTOCOL_DEFAULT;
}

const char *
st_tls_cipher(const st_tls_session_t *session)
{
	return session == NULL ? NULL : SSL_get_cipher_name(session->ssl);
}

const char *
st_tls_error(const st_tls_session_t *session)
{
	return session == NULL ? "invalid TLS session" : session->error;
}

size_t
st_tls_peer_certificate_count(const st_tls_session_t *session)
{
	return session == NULL ? 0 : session->peer_certificate_count;
}

int
st_tls_peer_certificate(const st_tls_session_t *session, size_t index,
    const void **data, size_t *size)
{
	if (session == NULL || data == NULL || size == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (index >= session->peer_certificate_count)
		return IO_ERROR_OUT_OF_RANGE;
	*data = session->peer_certificates[index].data;
	*size = session->peer_certificates[index].size;
	return 0;
}

int
st_tls_terminate(st_tls_session_t *session)
{
	if (session == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	session->terminating = true;
	(void)SSL_shutdown(session->ssl);
	return socket_shutdown(session->socket, SOCKET_SHUTDOWN_BOTH);
}

void
st_tls_close(st_tls_session_t *session)
{
	if (session == NULL)
		return;
	if (!session->terminating)
		(void)SSL_shutdown(session->ssl);
	SSL_free(session->ssl);
	SSL_CTX_free(session->context);
	(void)socket_close(session->socket);
	xp_crypto_clear(session->psk, session->psk_size);
	free(session->psk);
	free(session->psk_identity);
	free_peer_certificates(session);
	free(session);
}
