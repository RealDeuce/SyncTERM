/* Local wire-level fixtures for Telnet, TLS, GHost, and MQTT.
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#include "st_connection.h"
#include "connection_internal.h"
#include "socket.h"
#include "st_threads.h"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); return 1; } } while (0)
enum protocol { TELNET, NVT, TELNET_TLS, GHOST, GHOST_BAD,
	MQTT, MQTT_BAD, MQTT_OVERSIZE, MQTT_CERT, TLS_BAD };
struct fixture { int listener, socket; SSL_CTX *ctx; SSL *ssl; enum protocol type; };
static const uint8_t text[] = {'A', '\r', 0, 'B', 255, 'C'};
static const uint8_t keys[] = {'x', 3, 255, '\r', '\n'};
static uint8_t large[9000];

static int
read_exact(struct fixture *f, void *buffer, size_t size)
{
	uint8_t *p = buffer;
	for (size_t used = 0; used < size;) {
		ssize_t n = f->ssl == NULL ? recv(f->socket, p + used, size - used, 0)
		    : SSL_read(f->ssl, p + used, (int)(size - used));
		CHECK(n > 0);
		used += (size_t)n;
	}
	return 0;
}
static int
write_exact(struct fixture *f, const void *buffer, size_t size)
{
	const uint8_t *p = buffer;
	for (size_t used = 0; used < size;) {
		ssize_t n = f->ssl == NULL ? send(f->socket, p + used, size - used, 0)
		    : SSL_write(f->ssl, p + used, (int)(size - used));
		CHECK(n > 0);
		used += (size_t)n;
	}
	return 0;
}
static int
expect(struct fixture *f, const void *expected, size_t size)
{
	uint8_t buffer[10000];
	CHECK(size <= sizeof(buffer));
	CHECK(read_exact(f, buffer, size) == 0);
	if (memcmp(buffer, expected, size) != 0) {
		fprintf(stderr, "wire mismatch (%zu bytes)\n", size);
		for (size_t i = 0; i < size; ++i)
			fprintf(stderr, "%02x/%02x ", buffer[i], ((const uint8_t *)expected)[i]);
		fputc('\n', stderr);
		return 1;
	}
	return 0;
}
static int
fragment(struct fixture *f, const void *data, size_t size)
{
	const uint8_t *p = data;
	const struct timespec delay = {0, 1000000};
	for (size_t i = 0; i < size; ++i) {
		CHECK(write_exact(f, p + i, 1) == 0);
		(void)st_thread_sleep(&delay, NULL);
	}
	return 0;
}
static int
telnet_server(struct fixture *f)
{
	static const uint8_t initial[] = {
		255,251,3, 255,253,3, 255,251,0, 255,253,0, 255,253,1,
	};
	static const uint8_t negotiation[] = {
		255,253,0, 255,251,0, 255,253,31, 255,253,24,
		255,250,24,1,255,240, 255,251,99,
	};
	static const uint8_t replies[] = {
		255,251,31, 255,250,31,0,80,0,24,255,240,
		255,251,24, 255,250,24,0,'s','y','n','c','t','e','r','m',255,240, 255,254,99,
	};
	static const uint8_t resized[] = {255,250,31,0,255,255,1,255,255,255,240};
	static const uint8_t incoming[] = {'A','\r',0,'B',255,255,'C'};
	static const uint8_t outgoing[] = {'x',3,255,255,'\r','\n'};
	CHECK(expect(f, initial, sizeof(initial)) == 0);
	CHECK(fragment(f, negotiation, sizeof(negotiation)) == 0);
	CHECK(expect(f, replies, sizeof(replies)) == 0);
	CHECK(fragment(f, incoming, sizeof(incoming)) == 0);
	CHECK(expect(f, resized, sizeof(resized)) == 0);
	CHECK(expect(f, outgoing, sizeof(outgoing)) == 0);
	CHECK(write_exact(f, "Z", 1) == 0);
	return 0;
}
static int
nvt_server(struct fixture *f)
{
	static const uint8_t trigger[] = {255,251,3};
	static const uint8_t initial[] = {255,251,3,255,253,3,255,253,1};
	unsigned events;
	CHECK(socket_wait(f->socket, SOCKET_WAIT_READABLE, 50, &events) == IO_ERROR_TIMED_OUT);
	CHECK(fragment(f, trigger, sizeof(trigger)) == 0);
	CHECK(expect(f, initial, sizeof(initial)) == 0);
	CHECK(fragment(f, "A\r\0B", 4) == 0);
	CHECK(expect(f, "x\r\ny", 4) == 0);
	CHECK(write_exact(f, "Z", 1) == 0);
	return 0;
}
static int
ghost_server(struct fixture *f)
{
	static const char launch[] = "MBBS: program 2 'Test User' 999 GR\r\n";
	CHECK(expect(f, "\r\nMBBS: PING\r\n", 14) == 0);
	if (f->type == GHOST_BAD) return write_exact(f, "NO\r\n", 4);
	CHECK(fragment(f, "ERROR\r\n", 7) == 0);
	CHECK(expect(f, launch, sizeof(launch) - 1) == 0);
	CHECK(write_exact(f, "OK\r\n", 4) == 0);
	CHECK(write_exact(f, text, sizeof(text)) == 0);
	CHECK(expect(f, keys, sizeof(keys)) == 0);
	CHECK(write_exact(f, "Z", 1) == 0);
	return 0;
}

static size_t
encode_varint(uint8_t *out, size_t value)
{
	size_t n = 0;
	do { out[n] = (uint8_t)(value & 127); value >>= 7; if (value) out[n] |= 128; ++n; } while (value);
	return n;
}
static int
mq_send(struct fixture *f, uint8_t type, const void *data, size_t size)
{
	uint8_t header[5];
	header[0] = type;
	size_t n = 1 + encode_varint(header + 1, size);
	CHECK(fragment(f, header, n) == 0);
	return write_exact(f, data, size);
}
static int
mq_receive(struct fixture *f, uint8_t expected, uint8_t *body, size_t *size)
{
	uint8_t byte;
	size_t length = 0, shift = 0;
	CHECK(read_exact(f, &byte, 1) == 0 && byte == expected);
	do {
		CHECK(shift < 28 && read_exact(f, &byte, 1) == 0);
		length |= (size_t)(byte & 127) << shift; shift += 7;
	} while (byte & 128);
	CHECK(length <= 10000 && read_exact(f, body, length) == 0);
	*size = length;
	return 0;
}
static int
mq_publish(struct fixture *f, const char *topic, const void *payload, size_t size)
{
	uint8_t body[10000];
	size_t n = strlen(topic);
	CHECK(n + 3 + size < sizeof(body));
	body[0] = (uint8_t)(n >> 8); body[1] = (uint8_t)n;
	memcpy(body + 2, topic, n); body[2 + n] = 0;
	memcpy(body + 3 + n, payload, size);
	return mq_send(f, 0x30, body, 3 + n + size);
}
static int
mq_subscription(struct fixture *f, bool remove, const char *topic)
{
	uint8_t body[10000], ack[4];
	size_t size, n = strlen(topic);
	CHECK(mq_receive(f, remove ? 0xa2 : 0x82, body, &size) == 0);
	CHECK(size == n + (remove ? 5 : 6) && body[2] == 0
	    && body[3] == 0 && body[4] == n && memcmp(body + 5, topic, n) == 0);
	ack[0] = body[0]; ack[1] = body[1]; ack[2] = ack[3] = 0;
	return mq_send(f, remove ? 0xb0 : 0x90, ack, sizeof(ack));
}
static int
mqtt_server(struct fixture *f)
{
	uint8_t body[10000];
	size_t size, used = 0;
	static const uint8_t connect[] = {
		0,4,'M','Q','T','T',5,0xc2,0,60,0,0,0,
		0,5,'G','u','e','s','t',0,6,'S','y','s','t','e','m',
	};
	static const uint8_t certificate_connect[] = {
		0,4,'M','Q','T','T',5,0xc2,0,60,0,0,0,
		0,5,'G','u','e','s','t',0,8,'P','a','s','s','w','o','r','d',
	};
	CHECK(mq_receive(f, 0x10, body, &size) == 0);
	if (f->type == MQTT_CERT)
		CHECK(size == sizeof(certificate_connect) && memcmp(body, certificate_connect, size) == 0);
	else
		CHECK(size == sizeof(connect) && memcmp(body, connect, size) == 0);
	CHECK(mq_send(f, 0x20, "\0\0\0", 3) == 0);
	if (f->type == MQTT_BAD) {
		CHECK(mq_receive(f, 0x82, body, &size) == 0);
		return mq_send(f, 0x90, "\0\1\x80", 3);
	}
	if (f->type == MQTT_OVERSIZE) {
		uint8_t header[5] = {0x30};
		size_t n = 1 + encode_varint(header + 1, 1024 * 1024 + 1);
		CHECK(mq_receive(f, 0x82, body, &size) == 0);
		return write_exact(f, header, n);
	}
	CHECK(mq_subscription(f, false, "sbbs/+/version") == 0);
	CHECK(mq_publish(f, "sbbs/TEST/version", "1.0", 3) == 0);
	CHECK(mq_subscription(f, true, "sbbs/+/version") == 0);
	CHECK(mq_subscription(f, false, "sbbs/TEST/node/+/status") == 0);
	CHECK(mq_publish(f, "sbbs/TEST/node/3/status", "3", 1) == 0);
	CHECK(mq_subscription(f, true, "sbbs/TEST/node/+/status") == 0);
	CHECK(mq_subscription(f, false, "sbbs/TEST/node/3/output") == 0);
	CHECK(mq_publish(f, "sbbs/TEST/node/3/output", large, sizeof(large)) == 0);
	while (used < sizeof(large)) {
		static const char topic[] = "sbbs/TEST/node/3/input";
		size_t prefix = sizeof(topic) + 2;
		CHECK(mq_receive(f, 0x30, body, &size) == 0);
		CHECK(size > prefix && body[0] == 0 && body[1] == sizeof(topic) - 1
		    && memcmp(body + 2, topic, sizeof(topic) - 1) == 0
		    && body[prefix - 1] == 0);
		CHECK(size - prefix <= sizeof(large) - used
		    && memcmp(body + prefix, large + used, size - prefix) == 0);
		used += size - prefix;
	}
	CHECK(mq_send(f, 0xe0, NULL, 0) == 0);
	return 0;
}

static unsigned
psk(SSL *ssl, const char *identity, unsigned char *key, unsigned capacity)
{
	(void)ssl;
	if (identity == NULL || strcmp(identity, "guest") != 0 || capacity < 8) return 0;
	memcpy(key, "password", 8);
	return 8;
}
static int
certificate(SSL_CTX *ctx)
{
	EVP_PKEY *key = EVP_PKEY_Q_keygen(NULL, NULL, "RSA", 2048);
	X509 *cert = X509_new();
	int good = key != NULL && cert != NULL && X509_set_version(cert, 2)
	    && ASN1_INTEGER_set(X509_get_serialNumber(cert), 1)
	    && X509_gmtime_adj(X509_getm_notBefore(cert), -60)
	    && X509_gmtime_adj(X509_getm_notAfter(cert), 3600)
	    && X509_set_pubkey(cert, key)
	    && X509_NAME_add_entry_by_txt(X509_get_subject_name(cert),
	    "CN", MBSTRING_ASC, (const unsigned char *)"localhost", -1, -1, 0)
	    && X509_set_issuer_name(cert, X509_get_subject_name(cert))
	    && X509_sign(cert, key, EVP_sha256())
	    && SSL_CTX_use_certificate(ctx, cert) && SSL_CTX_use_PrivateKey(ctx, key);
	X509_free(cert); EVP_PKEY_free(key);
	return good;
}
static int
server(void *context)
{
	struct fixture *f = context;
	struct timeval timeout = {10, 0};
	int status;
	f->socket = accept(f->listener, NULL, NULL);
	CHECK(f->socket >= 0);
	(void)setsockopt(f->socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	(void)setsockopt(f->socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
	(void)socket_set_no_delay(f->socket, true);
	if (f->ctx != NULL) {
		f->ssl = SSL_new(f->ctx);
		CHECK(f->ssl != NULL && SSL_set_fd(f->ssl, f->socket));
		status = SSL_accept(f->ssl);
		if (f->type == MQTT_CERT) {
			CHECK(status != 1);
			SSL_free(f->ssl);
			close(f->socket);
			f->socket = accept(f->listener, NULL, NULL);
			CHECK(f->socket >= 0);
			(void)setsockopt(f->socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
			(void)setsockopt(f->socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
			f->ssl = SSL_new(f->ctx);
			CHECK(f->ssl != NULL && SSL_set_fd(f->ssl, f->socket));
			status = SSL_accept(f->ssl);
		}
		if (status != 1 && f->type != TLS_BAD) ERR_print_errors_fp(stderr);
		if (f->type == TLS_BAD) {
			CHECK(status != 1);
			SSL_free(f->ssl); f->ssl = NULL; close(f->socket); return 0;
		}
		CHECK(status == 1);
	}
	status = f->type >= MQTT && f->type <= MQTT_CERT ? mqtt_server(f)
	    : f->type == NVT ? nvt_server(f)
	    : f->type == GHOST || f->type == GHOST_BAD ? ghost_server(f)
	    : telnet_server(f);
	if (f->ssl != NULL) { (void)SSL_shutdown(f->ssl); SSL_free(f->ssl); f->ssl = NULL; }
	close(f->socket);
	return status;
}
static int
choose_node(void *context, const char *title, const char *message,
	const char *const *choices, size_t count)
{
	(void)context; (void)message;
	CHECK(strcmp(title, "MQTT nodes") == 0 && count == 2
	    && strstr(choices[0], "Node 3") != NULL);
	return 0;
}
static int
connection_read(st_connection_t *connection, const void *expected, size_t size)
{
	uint8_t buffer[10000];
	for (size_t used = 0; used < size;) {
		ssize_t n = st_connection_receive(connection, buffer + used, size - used, 5000);
		CHECK(n > 0); used += (size_t)n;
	}
	CHECK(memcmp(buffer, expected, size) == 0);
	return 0;
}
static int
run(enum protocol type)
{
	struct fixture f = {.type = type};
	struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
	socklen_t length = sizeof(address);
	struct st_profile profile;
	struct st_screen_geometry screen = {80,25,640,400}, resized = {255,511,0,0};
	struct st_connection_hooks hooks = {.choice = choose_node};
	struct st_connection_error error = {0};
	st_connection_t *connection = NULL;
	st_thread_t thread;
	int status, thread_status;
	bool mqtt = type >= MQTT && type <= MQTT_CERT;
	f.listener = socket(AF_INET, SOCK_STREAM, 0);
	CHECK(f.listener >= 0 && bind(f.listener, (struct sockaddr *)&address, sizeof(address)) == 0
	    && listen(f.listener, 1) == 0
	    && getsockname(f.listener, (struct sockaddr *)&address, &length) == 0);
	if (type == TELNET_TLS || type == TLS_BAD || mqtt) {
		f.ctx = SSL_CTX_new(TLS_server_method()); CHECK(f.ctx != NULL);
		if (mqtt && type != MQTT_CERT) {
			CHECK(SSL_CTX_set_min_proto_version(f.ctx, TLS1_2_VERSION)
			    && SSL_CTX_set_max_proto_version(f.ctx, TLS1_2_VERSION)
			    && SSL_CTX_set_cipher_list(f.ctx, "DHE-PSK-AES128-GCM-SHA256")
			    && SSL_CTX_set_dh_auto(f.ctx, 1));
			SSL_CTX_set_psk_server_callback(f.ctx, psk);
		} else {
			CHECK(certificate(f.ctx));
			if (type == MQTT_CERT)
				CHECK(SSL_CTX_set_min_proto_version(f.ctx, TLS1_3_VERSION));
		}
	}
	CHECK(st_thread_create(&thread, server, &f) == ST_THREAD_SUCCESS);
	st_profile_init(&profile);
	strcpy(profile.address, "127.0.0.1"); profile.port = ntohs(address.sin_port);
	profile.connection_type = type == GHOST || type == GHOST_BAD ? ST_CONNECTION_MBBS_GHOST
	    : mqtt ? ST_CONNECTION_MQTT
	    : type == TELNET_TLS || type == TLS_BAD ? ST_CONNECTION_TELNET_TLS : ST_CONNECTION_TELNET;
	if (mqtt) {
		strcpy(profile.username, "Guest"); strcpy(profile.password, "Password");
		strcpy(profile.system_password, "System");
	} else strcpy(profile.username, "Test User");
	strcpy(profile.ghost_program, "program");
	profile.defer_telnet_negotiation = type == NVT;
	profile.telnet_no_binary = type == NVT;
	profile.tls_trust_web_pki = type == TLS_BAD;
	status = type == GHOST || type == GHOST_BAD
	    ? st_connection_connect_ghost(&connection, &profile, NULL, &error)
	    : mqtt ? st_connection_connect_mqtt(&connection, &profile, &hooks, &error)
	    : st_connection_connect_telnet(&connection, &profile, &screen, NULL, &error);
	if (type == GHOST_BAD || type == TLS_BAD || type == MQTT_BAD || type == MQTT_OVERSIZE) {
		CHECK(status < 0 && connection == NULL);
		if (type == MQTT_BAD) CHECK(status == IO_ERROR_PROTOCOL);
		if (type == MQTT_OVERSIZE) CHECK(status == IO_ERROR_MESSAGE_TOO_LARGE);
	}
	else {
		if (status != 0) fprintf(stderr, "connect %d: %d %s\n", type, status, error.detail);
		CHECK(status == 0 && connection != NULL);
		if (mqtt) {
			CHECK(connection_read(connection, large, sizeof(large)) == 0);
			for (size_t used = 0; used < sizeof(large);) {
				ssize_t n = st_connection_send(connection, large + used, sizeof(large) - used, 5000);
				CHECK(n > 0); used += (size_t)n;
			}
		} else if (type == NVT) {
			CHECK(connection_read(connection, "A\rB", 3) == 0);
			CHECK(!atomic_load(&connection->binary_mode));
			CHECK(st_connection_send(connection, "x\r", 2, 1000) == 2);
			CHECK(st_connection_send(connection, "\ny", 2, 1000) == 2);
		} else {
			CHECK(connection_read(connection, text, sizeof(text)) == 0);
			if (type != GHOST) CHECK(st_connection_update_terminal_size(connection, &resized) == 0);
			CHECK(st_connection_send(connection, keys, sizeof(keys), 1000) == (ssize_t)sizeof(keys));
		}
		if (!mqtt) CHECK(connection_read(connection, "Z", 1) == 0);
		CHECK(st_thread_join(thread, &thread_status) == ST_THREAD_SUCCESS && thread_status == 0);
		CHECK(st_connection_close(connection) == 0);
	}
	if (connection == NULL)
		CHECK(st_thread_join(thread, &thread_status) == ST_THREAD_SUCCESS && thread_status == 0);
	close(f.listener); SSL_CTX_free(f.ctx); st_profile_dispose(&profile);
	return 0;
}
static enum st_connection_setup_action
cancel_setup(void *context, enum st_connection_setup_phase phase)
{
	(void)context; (void)phase;
	return ST_CONNECTION_SETUP_CANCEL;
}
static int
cancel_connections(void)
{
	const int types[] = {ST_CONNECTION_TELNET, ST_CONNECTION_TELNET_TLS,
	    ST_CONNECTION_SSH, ST_CONNECTION_SSH_NO_AUTH, ST_CONNECTION_MBBS_GHOST,
	    ST_CONNECTION_MQTT};
	const struct st_screen_geometry geometry = {80,25,640,400};
	const struct st_connection_hooks hooks = {.setup = cancel_setup};
	struct st_profile profile;
	st_connection_t *connection = NULL;
	st_profile_init(&profile);
	strcpy(profile.address, "example.invalid");
	strcpy(profile.username, "user"); strcpy(profile.password, "secret");
	strcpy(profile.ghost_program, "program");
	for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); ++i) {
		int status;
		bool available = ST_CONNECTION_TEST_HAVE_TLS
		    || types[i] == ST_CONNECTION_TELNET || types[i] == ST_CONNECTION_MBBS_GHOST;
		profile.connection_type = types[i];
		if (types[i] == ST_CONNECTION_TELNET || types[i] == ST_CONNECTION_TELNET_TLS)
			status = st_connection_connect_telnet(&connection, &profile, &geometry, &hooks, NULL);
		else if (types[i] == ST_CONNECTION_SSH || types[i] == ST_CONNECTION_SSH_NO_AUTH)
			status = st_connection_connect_ssh(&connection, &profile, &geometry, &hooks, NULL);
		else if (types[i] == ST_CONNECTION_MQTT)
			status = st_connection_connect_mqtt(&connection, &profile, &hooks, NULL);
		else status = st_connection_connect_ghost(&connection, &profile, &hooks, NULL);
		CHECK(status == (available ? IO_ERROR_CANCELLED : IO_ERROR_NOT_SUPPORTED)
		    && connection == NULL);
	}
	st_profile_dispose(&profile);
	return 0;
}
int
main(void)
{
	(void)signal(SIGPIPE, SIG_IGN);
	for (size_t i = 0; i < sizeof(large); ++i) large[i] = (uint8_t)i;
	CHECK(socket_runtime_init() == 0);
	CHECK(cancel_connections() == 0);
	for (enum protocol p = TELNET; p <= TLS_BAD; ++p) {
		if (!ST_CONNECTION_TEST_HAVE_TLS && (p == TELNET_TLS || p >= MQTT))
			continue;
		fprintf(stderr, "protocol fixture %d\n", p);
		CHECK(run(p) == 0);
	}
	CHECK(socket_runtime_cleanup() == 0);
	return 0;
}
