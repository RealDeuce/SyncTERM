/* TLS decisions against a local certificate peer, before terminal I/O.
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#include "st_connection.h"
#include "st_settings.h"
#include "st_threads.h"
#include "string_compat.h"
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "TLS prompt %d: %s\n", __LINE__, #e); return 1; } } while (0)
enum scenario { REVIEW, INSTALL, READ_ONLY, CERT_LOG, INSTALL_FAIL,
	DOWNGRADE_REJECT, DOWNGRADE_ONCE, DOWNGRADE_REMEMBER, DOWNGRADE_LOG,
	UPGRADE, SAVE_FAIL, NO_UI, CLIENT_IDENTITY, RUNTIME_ERROR,
	NAME_MISMATCH, IP_PINNED, HIDDEN_DOWNGRADE };
struct fixture { int listener; SSL_CTX *context; bool corrupt; };
struct ui {
	enum scenario scenario;
	unsigned choices, details, helps, alerts, saves, installs;
	bool invalid;
	int version;
	st_thread_t owner;
};
static const char certificate_path[] = "connection-prompt-cert.pem";
static const char key_path[] = "connection-prompt-key.pem";

static int
peer(void *argument)
{
	struct fixture *f = argument;
	int client = accept(f->listener, NULL, NULL);
	struct timeval timeout = {10, 0};
	CHECK(client >= 0);
	(void)setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	(void)setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
	SSL *ssl = SSL_new(f->context);
	CHECK(ssl != NULL && SSL_set_fd(ssl, client));
	if (SSL_accept(ssl) == 1) {
		char byte;
		if (f->corrupt) {
			static const unsigned char invalid[] = {23,3,3,0,1,0};
			CHECK(send(client, invalid, sizeof(invalid), 0) == (ssize_t)sizeof(invalid));
		} else (void)SSL_write(ssl, "X", 1);
		/* No terminal negotiation is allowed before the user's decision. */
		CHECK(SSL_read(ssl, &byte, 1) <= 0);
	}
	SSL_free(ssl);
	close(client);
	return 0;
}

static int
choice(void *context, const char *title, const char *message,
	const char *const *labels, size_t count)
{
	struct ui *ui = context;
	if (!st_thread_equal(st_thread_current(), ui->owner)) ui->invalid = true;
	if (strcmp(title, "TLS error receiving data") == 0) {
		if (count != 2 || message[0] == '\0') ui->invalid = true;
		return ui->choices++ == 0 ? 1 : 0;
	}
	if (strcmp(title, "TLS Certificate Error") == 0) {
		if (count != 3 || strstr(message, "verification failed") == NULL
		    || strstr(labels[0], "localhost") == NULL) ui->invalid = true;
		if (ui->choices++ == 0) return ui->scenario == CERT_LOG ? 1 : 0;
		if (ui->scenario == CERT_LOG && ui->details == 0) return 0;
		return 2;
	}
	if (strcmp(title, "Presented Certificate") == 0) {
		++ui->details;
		if (strstr(message, "Subject:") == NULL || strstr(message, "Issuer:") == NULL
		    || strstr(message, "SHA-256 fingerprint:") == NULL
		    || strstr(message, "Valid until:") == NULL
		    || count != (ui->scenario == READ_ONLY ? 1U : 2U)) ui->invalid = true;
		return ui->scenario == INSTALL || ui->scenario == INSTALL_FAIL ? 1 : 0;
	}
	if (strcmp(title, "TLS version downgraded") == 0) {
		if (count != 4 || strcmp(labels[0], "Disconnect") != 0
		    || strcmp(labels[1], "Accept Once") != 0
		    || strcmp(labels[2], "Accept and Remember") != 0
		    || strcmp(labels[3], "View Protocol Log") != 0
		    || strstr(message, "TLS 1.3") == NULL || strstr(message, "TLS 1.2") == NULL)
			ui->invalid = true;
		if (ui->scenario == DOWNGRADE_REJECT) return 0;
		if (ui->scenario == DOWNGRADE_REMEMBER) return 2;
		if (ui->scenario == DOWNGRADE_LOG && ui->choices++ == 0) return 3;
		return 1;
	}
	ui->invalid = true;
	return IO_ERROR_CANCELLED;
}

static void
alert(void *context, const char *title, const char *message)
{
	struct ui *ui = context;
	++ui->alerts;
	if (ui->scenario == INSTALL) {
		if (strcmp(title, "Certificate installed") != 0 || strstr(message, "Reconnect") == NULL)
			ui->invalid = true;
	} else if (ui->scenario == INSTALL_FAIL) {
		if (strcmp(title, "Certificate not installed") != 0) ui->invalid = true;
	} else if (strcmp(title, "TLS version not saved") != 0)
		ui->invalid = true;
}

static void
help(void *context, const char *title, const char *message)
{
	struct ui *ui = context;
	++ui->helps;
	if (strstr(title, "TLS") == NULL || message[0] == '\0') ui->invalid = true;
}

static int
remember(void *context, int version)
{
	struct ui *ui = context;
	++ui->saves; ui->version = version;
	return ui->scenario == SAVE_FAIL ? IO_ERROR_IO : 0;
}

static int
trust(void *context, const char *pem, char **filename)
{
	struct ui *ui = context;
	++ui->installs;
	if (strstr(pem, "-----BEGIN CERTIFICATE-----") != pem) ui->invalid = true;
	if (ui->scenario == INSTALL_FAIL) return IO_ERROR_IO;
	FILE *file = fopen(certificate_path, "wb");
	CHECK(file != NULL && fwrite(pem, 1, strlen(pem), file) == strlen(pem) && fclose(file) == 0);
	*filename = strdup(certificate_path);
	return *filename == NULL ? IO_ERROR_OUT_OF_MEMORY : 0;
}

static int
verify_client(int valid, X509_STORE_CTX *store)
{
	(void)valid; (void)store;
	return 1;
}

static int
run(enum scenario scenario)
{
	struct fixture f = {.listener = -1, .context = SSL_CTX_new(TLS_server_method()), .corrupt = scenario == RUNTIME_ERROR};
	struct ui ui = {.scenario = scenario, .owner = st_thread_current()};
	struct st_connection_hooks hooks = {.choice = choice, .alert = alert,
	    .help = help, .ui_context = &ui, .remember_tls_version = remember,
	    .trust_tls_certificate = scenario == READ_ONLY ? NULL : trust};
	struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
	socklen_t length = sizeof(address);
	struct st_profile profile;
	const struct st_screen_geometry geometry = {80,25,640,400};
	struct st_connection_error error = {0};
	st_connection_t *connection = NULL;
	st_thread_t thread;
	int thread_result;
	bool authenticated = scenario >= DOWNGRADE_REJECT;
	bool success = authenticated && scenario != DOWNGRADE_REJECT && scenario != NO_UI
	    && scenario != NAME_MISMATCH && scenario != HIDDEN_DOWNGRADE;
	EVP_PKEY *key = EVP_PKEY_Q_keygen(NULL, NULL, "RSA", 2048);
	X509 *certificate = X509_new();
	CHECK(f.context != NULL && key != NULL && certificate != NULL);
	CHECK(X509_set_version(certificate, 2)
	    && ASN1_INTEGER_set(X509_get_serialNumber(certificate), 1)
	    && X509_gmtime_adj(X509_getm_notBefore(certificate), -60)
	    && X509_gmtime_adj(X509_getm_notAfter(certificate), 3600)
	    && X509_set_pubkey(certificate, key)
	    && X509_NAME_add_entry_by_txt(X509_get_subject_name(certificate), "CN", MBSTRING_ASC,
	    (const unsigned char *)"localhost", -1, -1, 0)
	    && X509_set_issuer_name(certificate, X509_get_subject_name(certificate)));
	if (scenario == IP_PINNED) {
		X509_EXTENSION *extension = X509V3_EXT_conf_nid(NULL, NULL, NID_subject_alt_name, "IP:127.0.0.1");
		CHECK(extension != NULL && X509_add_ext(certificate, extension, -1));
		X509_EXTENSION_free(extension);
	}
	CHECK(X509_sign(certificate, key, EVP_sha256())
	    && SSL_CTX_use_certificate(f.context, certificate) && SSL_CTX_use_PrivateKey(f.context, key));
	bool tls12 = scenario >= DOWNGRADE_REJECT && scenario <= DOWNGRADE_LOG;
	if (scenario == NO_UI || scenario == HIDDEN_DOWNGRADE) tls12 = true;
	CHECK(SSL_CTX_set_min_proto_version(f.context, tls12 ? TLS1_2_VERSION : TLS1_3_VERSION)
	    && SSL_CTX_set_max_proto_version(f.context, tls12 ? TLS1_2_VERSION : TLS1_3_VERSION));
	if (scenario == CLIENT_IDENTITY)
		SSL_CTX_set_verify(f.context, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, verify_client);
	if (authenticated) {
		FILE *file = fopen(certificate_path, "wb");
		CHECK(file != NULL && PEM_write_X509(file, certificate) && fclose(file) == 0);
	}
	if (scenario == CLIENT_IDENTITY) {
		FILE *file = fopen(key_path, "wb");
		CHECK(file != NULL && PEM_write_PrivateKey(file, key, NULL, NULL, 0, NULL, NULL) && fclose(file) == 0);
		st_settings.tls_client_certificate = (char *)certificate_path;
		st_settings.tls_client_key = (char *)key_path;
	}
	X509_free(certificate); EVP_PKEY_free(key);
	f.listener = socket(AF_INET, SOCK_STREAM, 0);
	CHECK(f.listener >= 0 && bind(f.listener, (struct sockaddr *)&address, sizeof(address)) == 0
	    && listen(f.listener, 1) == 0 && getsockname(f.listener, (struct sockaddr *)&address, &length) == 0);
	CHECK(st_thread_create(&thread, peer, &f) == ST_THREAD_SUCCESS);
	st_profile_init(&profile);
	strcpy(profile.address, "localhost");
	profile.address_family = ST_ADDRESS_FAMILY_IPV4;
	profile.port = ntohs(address.sin_port);
	profile.connection_type = ST_CONNECTION_TELNET_TLS;
	profile.tls_trust_web_pki = false;
	profile.hide_popups = scenario == HIDDEN_DOWNGRADE;
	if (scenario == NAME_MISMATCH || scenario == IP_PINNED) strcpy(profile.address, "127.0.0.1");
	profile.defer_telnet_negotiation = true;
	if (authenticated) {
		profile.tls_trusted_certificate = strdup(certificate_path);
		CHECK(profile.tls_trusted_certificate != NULL);
		profile.tls_version_floor = tls12 ? ST_TLS_VERSION_1_3 : ST_TLS_VERSION_1_2;
	}
	int status = st_connection_connect_telnet(&connection, &profile, &geometry,
	    scenario == NO_UI ? NULL : &hooks, &error);
	if (success) {
		if (status != 0) fprintf(stderr, "TLS case %d: %d %s\n", scenario, status, error.detail);
		CHECK(status == 0 && connection != NULL);
		char byte = 0;
		if (scenario == RUNTIME_ERROR) {
			CHECK(st_connection_receive(connection, &byte, 1, 5000) <= 0);
			CHECK(st_connection_process_requests(connection) == 0);
		} else CHECK(st_connection_receive(connection, &byte, 1, 5000) == 1 && byte == 'X');
		CHECK(st_connection_close(connection) == 0);
	} else CHECK(status < 0 && connection == NULL);
	CHECK(st_thread_join(thread, &thread_result) == ST_THREAD_SUCCESS && thread_result == 0);
	CHECK(!ui.invalid);
	CHECK(ui.saves == (scenario == DOWNGRADE_REMEMBER || scenario == UPGRADE
	    || scenario == SAVE_FAIL || scenario == CLIENT_IDENTITY || scenario == RUNTIME_ERROR
	    || scenario == IP_PINNED ? 1U : 0U));
	if (ui.saves != 0) CHECK(ui.version == (tls12 ? ST_TLS_VERSION_1_2 : ST_TLS_VERSION_1_3));
	CHECK(ui.installs == (scenario == INSTALL || scenario == INSTALL_FAIL ? 1U : 0U));
	CHECK(ui.helps == (scenario == CERT_LOG || scenario == DOWNGRADE_LOG || scenario == RUNTIME_ERROR ? 1U : 0U));
	CHECK(ui.alerts == (scenario == INSTALL || scenario == INSTALL_FAIL || scenario == SAVE_FAIL ? 1U : 0U));
	if (authenticated || scenario == INSTALL) CHECK(remove(certificate_path) == 0);
	if (scenario == CLIENT_IDENTITY) {
		CHECK(remove(key_path) == 0);
		st_settings.tls_client_certificate = st_settings.tls_client_key = NULL;
	}
	st_profile_dispose(&profile);
	close(f.listener); SSL_CTX_free(f.context);
	return 0;
}

int
main(void)
{
	(void)signal(SIGPIPE, SIG_IGN);
	for (enum scenario scenario = REVIEW; scenario <= HIDDEN_DOWNGRADE; ++scenario) {
		fprintf(stderr, "TLS prompt scenario %d\n", scenario);
		CHECK(run(scenario) == 0);
	}
	return 0;
}
