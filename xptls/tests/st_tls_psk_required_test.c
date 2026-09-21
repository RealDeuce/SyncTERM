/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_tls.h"

#include "io_error.h"
#include "st_threads.h"

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if SYNCTERM_PLATFORM_WINDOWS
	#include <ws2tcpip.h>
	typedef int test_socklen_t;
#else
	#include <arpa/inet.h>
	#include <netinet/in.h>
	#include <signal.h>
	#include <sys/socket.h>
	typedef socklen_t test_socklen_t;
#endif

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, \
		    __LINE__, #expression); \
		return 1; \
	} \
} while (0)

static const unsigned char expected_secret[] = {
	0x9f, 0x10, 0x43, 0x52, 0xa1, 0x8c, 0x37, 0x6e,
	0x28, 0x79, 0xb4, 0x60, 0xd9, 0x05, 0xee, 0x13,
};
static const unsigned char wrong_secret[] = {
	0x44, 0x0e, 0x7d, 0x21, 0x9a, 0x86, 0x3c, 0x5f,
	0x72, 0xd1, 0x00, 0xb8, 0x35, 0xc9, 0x61, 0xaa,
};
static const char expected_identity[] = "syncterm-test";

struct server_state {
	socket_handle_t listener;
	SSL_CTX *context;
	bool use_psk;
	int result;
	unsigned long error;
};

struct diagnostic_capture {
	int status;
	bool fatal;
	char message[128];
};

static void
capture_diagnostic(const struct st_tls_diagnostic *record, void *context)
{
	struct diagnostic_capture *capture = context;

	capture->status = record->status;
	capture->fatal = record->fatal;
	(void)snprintf(capture->message, sizeof(capture->message), "%s",
	    record->message);
}

static unsigned int
server_psk12(SSL *ssl, const char *identity, unsigned char *secret,
    unsigned int capacity)
{
	struct server_state *state = SSL_get_app_data(ssl);

	if (state == NULL || !state->use_psk || identity == NULL
	    || strcmp(identity, expected_identity) != 0
	    || capacity < sizeof(expected_secret))
		return 0;
	memcpy(secret, expected_secret, sizeof(expected_secret));
	return (unsigned int)sizeof(expected_secret);
}

static int
server_psk13(SSL *ssl, const unsigned char *identity, size_t identity_size,
    SSL_SESSION **result)
{
	static const unsigned char cipher_id[] = { 0x13, 0x01 };
	struct server_state *state = SSL_get_app_data(ssl);
	SSL_SESSION *session;
	const SSL_CIPHER *cipher;

	*result = NULL;
	if (state == NULL || !state->use_psk
	    || identity_size != sizeof(expected_identity) - 1
	    || memcmp(identity, expected_identity, identity_size) != 0)
		return 1;
	session = SSL_SESSION_new();
	cipher = SSL_CIPHER_find(ssl, cipher_id);
	if (session == NULL || cipher == NULL
	    || !SSL_SESSION_set1_master_key(session, expected_secret,
	    sizeof(expected_secret))
	    || !SSL_SESSION_set_protocol_version(session, TLS1_3_VERSION)
	    || !SSL_SESSION_set_cipher(session, cipher)) {
		SSL_SESSION_free(session);
		return 0;
	}
	*result = session;
	return 1;
}

static int
install_certificate(SSL_CTX *context)
{
	EVP_PKEY *key = EVP_PKEY_Q_keygen(NULL, NULL, "RSA", 2048);
	X509 *certificate = X509_new();
	int result = 0;

	if (key != NULL && certificate != NULL
	    && X509_set_version(certificate, 2)
	    && ASN1_INTEGER_set(X509_get_serialNumber(certificate), 1)
	    && X509_gmtime_adj(X509_getm_notBefore(certificate), -60) != NULL
	    && X509_gmtime_adj(X509_getm_notAfter(certificate), 3600) != NULL
	    && X509_set_pubkey(certificate, key)
	    && X509_NAME_add_entry_by_txt(X509_get_subject_name(certificate),
	    "CN", MBSTRING_ASC, (const unsigned char *)"localhost", -1, -1, 0)
	    && X509_set_issuer_name(certificate,
	    X509_get_subject_name(certificate))
	    && X509_sign(certificate, key, EVP_sha256())
	    && SSL_CTX_use_certificate(context, certificate)
	    && SSL_CTX_use_PrivateKey(context, key)
	    && SSL_CTX_check_private_key(context))
		result = 1;
	X509_free(certificate);
	EVP_PKEY_free(key);
	return result;
}

static SSL_CTX *
make_server_context(enum st_tls_protocol_version version, bool use_psk)
{
	SSL_CTX *context = SSL_CTX_new(TLS_server_method());
	int native_version = version == ST_TLS_PROTOCOL_1_2
	    ? TLS1_2_VERSION : TLS1_3_VERSION;

	if (context == NULL
	    || !SSL_CTX_set_min_proto_version(context, native_version)
	    || !SSL_CTX_set_max_proto_version(context, native_version)) {
		SSL_CTX_free(context);
		return NULL;
	}
	if (!use_psk && !install_certificate(context)) {
		SSL_CTX_free(context);
		return NULL;
	}
	if (version == ST_TLS_PROTOCOL_1_2 && use_psk) {
		if (!SSL_CTX_set_cipher_list(context,
		    "ECDHE-PSK-CHACHA20-POLY1305:DHE-PSK-AES128-GCM-SHA256")
		    || !SSL_CTX_set_dh_auto(context, 1)) {
			SSL_CTX_free(context);
			return NULL;
		}
		SSL_CTX_set_psk_server_callback(context, server_psk12);
	} else if (version == ST_TLS_PROTOCOL_1_3 && use_psk) {
		if (!SSL_CTX_set_ciphersuites(context, "TLS_AES_128_GCM_SHA256")) {
			SSL_CTX_free(context);
			return NULL;
		}
		SSL_CTX_set_psk_find_session_callback(context, server_psk13);
	}
	return context;
}

static int
server_thread(void *argument)
{
	struct server_state *state = argument;
	socket_handle_t client = accept(state->listener, NULL, NULL);
	SSL *ssl;

	if (!socket_handle_is_valid(client)) {
		state->result = -1;
		return 0;
	}
	ssl = SSL_new(state->context);
	if (ssl == NULL || !SSL_set_fd(ssl, (int)client)) {
		SSL_free(ssl);
		(void)socket_close(client);
		state->result = -1;
		return 0;
	}
	SSL_set_app_data(ssl, state);
	state->result = SSL_accept(ssl) == 1 ? 1 : 0;
	state->error = ERR_peek_last_error();
	SSL_free(ssl);
	(void)socket_close(client);
	return 0;
}

static int
open_connected_socket(socket_handle_t *listener, socket_handle_t *client)
{
	struct sockaddr_in address = {0};
	test_socklen_t address_size = (test_socklen_t)sizeof(address);

	*listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	*client = SOCKET_HANDLE_INVALID;
	if (!socket_handle_is_valid(*listener))
		return 0;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address.sin_port = 0;
	if (bind(*listener, (const struct sockaddr *)&address,
	    (test_socklen_t)sizeof(address)) != 0
	    || listen(*listener, 1) != 0
	    || getsockname(*listener, (struct sockaddr *)&address,
	    &address_size) != 0)
		goto fail;
	*client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (!socket_handle_is_valid(*client)
	    || connect(*client, (const struct sockaddr *)&address,
	    (test_socklen_t)sizeof(address)) != 0)
		goto fail;
	return 1;

fail:
	if (socket_handle_is_valid(*client))
		(void)socket_close(*client);
	(void)socket_close(*listener);
	*listener = SOCKET_HANDLE_INVALID;
	*client = SOCKET_HANDLE_INVALID;
	return 0;
}

static int
run_case(enum st_tls_protocol_version version, bool matching_secret,
    bool server_uses_psk)
{
	const unsigned char *client_secret = matching_secret
	    ? expected_secret : wrong_secret;
	struct diagnostic_capture diagnostic = {0};
	struct st_tls_client_options options = {
		.server_name = "localhost",
		.server_auth = ST_TLS_SERVER_AUTH_CERTIFICATE,
		.trusted_certificate_file = "ignored-PSK-server-certificate.pem",
		.client_certificate_file = "ignored-PSK-client-certificate.pem",
		.client_key_file = "ignored-PSK-client-key.pem",
		.psk_identity = expected_identity,
		.psk = client_secret,
		.psk_size = sizeof(expected_secret),
		.psk_version = version,
		.psk_policy = ST_TLS_PSK_MODERN,
		.handshake_timeout_ms = 3000,
		.diagnostic = capture_diagnostic,
		.diagnostic_context = &diagnostic,
	};
	struct server_state server = {
		.listener = SOCKET_HANDLE_INVALID,
		.context = make_server_context(version, server_uses_psk),
		.use_psk = server_uses_psk,
		.result = -1,
		.error = 0,
	};
	socket_handle_t client = SOCKET_HANDLE_INVALID;
	st_tls_session_t *session = NULL;
	st_thread_t thread;
	int thread_result;
	int status;
	int result = 1;

	if (server.context == NULL
	    || !open_connected_socket(&server.listener, &client)
	    || st_thread_create(&thread, server_thread, &server)
	    != ST_THREAD_SUCCESS)
		goto cleanup;
	status = st_tls_client_open(&session, client, &options);
	if (st_thread_join(thread, &thread_result) != ST_THREAD_SUCCESS
	    || thread_result != 0)
		goto cleanup;
	if (server_uses_psk && matching_secret) {
		if (status != 0 || session == NULL || server.result != 1
		    || st_tls_protocol_version(session) != version) {
			fprintf(stderr, "TLS %s PSK status=%d server=%d error=%lu\n",
			    version == ST_TLS_PROTOCOL_1_2 ? "1.2" : "1.3",
			    status, server.result, server.error);
			goto cleanup;
		}
		st_tls_close(session);
		session = NULL;
		client = SOCKET_HANDLE_INVALID;
	} else {
		if (status >= 0 || session != NULL)
			goto cleanup;
		if (!server_uses_psk && version == ST_TLS_PROTOCOL_1_3
		    && status != IO_ERROR_PROTOCOL)
			goto cleanup;
#ifdef ST_TLS_TEST_OPENSSL_PROVIDER
		if (!server_uses_psk && version == ST_TLS_PROTOCOL_1_3
		    && (!diagnostic.fatal || diagnostic.status != IO_ERROR_PROTOCOL
		    || strcmp(diagnostic.message,
		    "TLS handshake did not select the configured PSK") != 0))
			goto cleanup;
#endif
		if (socket_close(client) != 0)
			goto cleanup;
		client = SOCKET_HANDLE_INVALID;
	}
	result = 0;

cleanup:
	if (session != NULL) {
		st_tls_close(session);
		client = SOCKET_HANDLE_INVALID;
	}
	if (socket_handle_is_valid(client))
		(void)socket_close(client);
	if (socket_handle_is_valid(server.listener))
		(void)socket_close(server.listener);
	SSL_CTX_free(server.context);
	return result;
}

int
main(void)
{
	int result = 0;

#if !SYNCTERM_PLATFORM_WINDOWS
	(void)signal(SIGPIPE, SIG_IGN);
#endif
	CHECK(socket_runtime_init() == 0);
	if (run_case(ST_TLS_PROTOCOL_1_2, true, true) != 0) {
		fprintf(stderr, "TLS 1.2 matching PSK failed\n");
		result = 1;
	}
	if (run_case(ST_TLS_PROTOCOL_1_2, false, true) != 0) {
		fprintf(stderr, "TLS 1.2 wrong-secret rejection failed\n");
		result = 1;
	}
	if (run_case(ST_TLS_PROTOCOL_1_2, true, false) != 0) {
		fprintf(stderr, "TLS 1.2 certificate fallback rejection failed\n");
		result = 1;
	}
	if (run_case(ST_TLS_PROTOCOL_1_3, true, true) != 0) {
		fprintf(stderr, "TLS 1.3 matching PSK failed\n");
		result = 1;
	}
	if (run_case(ST_TLS_PROTOCOL_1_3, false, true) != 0) {
		fprintf(stderr, "TLS 1.3 wrong-secret rejection failed\n");
		result = 1;
	}
	if (run_case(ST_TLS_PROTOCOL_1_3, true, false) != 0) {
		fprintf(stderr, "TLS 1.3 certificate fallback rejection failed\n");
		result = 1;
	}
	CHECK(socket_runtime_cleanup() == 0);
	return result;
}
