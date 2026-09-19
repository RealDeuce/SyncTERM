/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* Standalone POSIX regression: a PSK client must reject a certificate-only
 * server that has never been given the client's secret. Link the client with
 * either st_tls provider; OpenSSL is used only to construct the test server.
 */
#include "st_tls.h"

#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

int
main(void)
{
	static const unsigned char secret[] = "not supplied to the server";
	struct st_tls_client_options options = {
		.server_name = "localhost",
		.psk_identity = "audit-client",
		.psk = secret,
		.psk_size = sizeof(secret) - 1,
		.psk_version = ST_TLS_PROTOCOL_1_3,
		.handshake_timeout_ms = 3000,
	};
	st_tls_session_t *session = NULL;
	EVP_PKEY *key = EVP_PKEY_Q_keygen(NULL, NULL, "RSA", 2048);
	X509 *certificate = X509_new();
	SSL_CTX *context = SSL_CTX_new(TLS_server_method());
	int pair[2];
	int child_status;
	int status;
	pid_t child;

	(void)signal(SIGPIPE, SIG_IGN);
	alarm(10);
	if (key == NULL || certificate == NULL || context == NULL
	    || !X509_set_version(certificate, 2)
	    || !ASN1_INTEGER_set(X509_get_serialNumber(certificate), 1)
	    || X509_gmtime_adj(X509_getm_notBefore(certificate), -60) == NULL
	    || X509_gmtime_adj(X509_getm_notAfter(certificate), 3600) == NULL
	    || !X509_set_pubkey(certificate, key)
	    || !X509_NAME_add_entry_by_txt(X509_get_subject_name(certificate),
	        "CN", MBSTRING_ASC, (const unsigned char *)"localhost", -1, -1, 0)
	    || !X509_set_issuer_name(certificate, X509_get_subject_name(certificate))
	    || !X509_sign(certificate, key, EVP_sha256())
	    || !SSL_CTX_use_certificate(context, certificate)
	    || !SSL_CTX_use_PrivateKey(context, key)
	    || !SSL_CTX_set_min_proto_version(context, TLS1_3_VERSION)
	    || !SSL_CTX_set_ciphersuites(context, "TLS_AES_128_GCM_SHA256")
	    || socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0)
		return 2;
	child = fork();
	if (child < 0)
		return 2;
	if (child == 0) {
		SSL *server = SSL_new(context);
		close(pair[0]);
		alarm(5);
		if (server == NULL || !SSL_set_fd(server, pair[1]))
			_exit(2);
		/* Rejection by a correctly enforcing client is an expected outcome. */
		(void)SSL_accept(server);
		SSL_free(server);
		close(pair[1]);
		_exit(0);
	}
	close(pair[1]);
	status = st_tls_client_open(&session, pair[0], &options);
	fprintf(stderr, "PSK client against certificate-only server: status=%d\n",
	    status);
	if (status == 0)
		st_tls_close(session);
	else
		close(pair[0]);
	if (waitpid(child, &child_status, 0) != child
	    || !WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0)
		return 2;
	SSL_CTX_free(context);
	X509_free(certificate);
	EVP_PKEY_free(key);
	return status == 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
