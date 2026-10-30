/* Synchronet MQTT 5 terminal bridge.
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#include "connection_stream.h"
#include "st_time.h"
#include "xp_crypto.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MQTT_PACKET_LIMIT = 1024 * 1024, MQTT_NODE_LIMIT = 128 };
struct mqtt_transport {
	struct st_connection_stream stream;
	st_connection_t *connection;
	uint8_t header[5], *body;
	size_t header_size, body_size, body_used, body_capacity;
	size_t payload_offset, payload_size;
	bool have_length, ready, ping_pending;
	uint16_t packet_id;
	atomic_uint_fast64_t last_send;
	uint64_t ping_sent;
	char bbsid[64], input_topic[160], output_topic[160];
};
struct mqtt_publish {
	const uint8_t *topic, *payload;
	size_t topic_size, payload_size, payload_offset;
};
struct mqtt_node { unsigned number; int status; };

static const char *
packet_name(uint8_t header)
{
	static const char *const names[] = {"reserved", "CONNECT", "CONNACK", "PUBLISH",
		"PUBACK", "PUBREC", "PUBREL", "PUBCOMP", "SUBSCRIBE", "SUBACK",
		"UNSUBSCRIBE", "UNSUBACK", "PINGREQ", "PINGRESP", "DISCONNECT", "AUTH"};
	return names[header >> 4];
}

static uint64_t
now_ms(void)
{
	uint64_t now = 0;
	(void)st_time_monotonic_ms(&now);
	return now;
}

static size_t
varint(uint8_t *output, size_t value)
{
	size_t used = 0;
	do {
		output[used] = (uint8_t)(value & 127);
		value >>= 7;
		if (value != 0) output[used] |= 128;
		++used;
	} while (value != 0);
	return used;
}

static int
read_varint(const uint8_t *data, size_t size, size_t *value, size_t *used)
{
	*value = 0;
	for (size_t i = 0; i < 4 && i < size; ++i) {
		*value |= (size_t)(data[i] & 127) << (7 * i);
		if ((data[i] & 128) == 0) {
			*used = i + 1;
			return 0;
		}
	}
	return size >= 4 ? IO_ERROR_PROTOCOL : IO_ERROR_WOULD_BLOCK;
}

static size_t
put_string(uint8_t *output, const char *text)
{
	size_t size = strlen(text);
	output[0] = (uint8_t)(size >> 8);
	output[1] = (uint8_t)size;
	memcpy(output + 2, text, size);
	return size + 2;
}

static int
packet(struct mqtt_transport *t, uint8_t type, const void *data, size_t size,
	const struct st_connection_hooks *hooks)
{
	uint8_t *wire;
	size_t header;
	int status;
	ssize_t n;
	if (size > MQTT_PACKET_LIMIT) return IO_ERROR_MESSAGE_TOO_LARGE;
	st_connection_log_hooks(&t->stream.hooks, "MQTT", ST_CONNECTION_LOG_DEBUG,
	    "sending %s packet bytes=%zu (sensitive fields omitted)", packet_name(type), size);
	wire = malloc(size + 5);
	if (wire == NULL) return IO_ERROR_OUT_OF_MEMORY;
	wire[0] = type;
	header = 1 + varint(wire + 1, size);
	if (size != 0) memcpy(wire + header, data, size);
	if (t->connection == NULL)
		status = st_connection_stream_send_all(&t->stream, wire, header + size, hooks);
	else {
		n = st_connection_queue_output_atomic(t->connection, wire, header + size, -1);
		status = n == (ssize_t)(header + size) ? 0 : n < 0 ? (int)n : IO_ERROR_IO;
	}
	free(wire);
	if (status == 0) atomic_store(&t->last_send, now_ms());
	return status;
}

static void
release_packet(struct mqtt_transport *t)
{
	t->ready = false;
	t->header_size = 0;
	t->body_size = t->body_used = 0;
	t->have_length = false;
}

static int
receive_packet(struct mqtt_transport *t, int64_t timeout)
{
	if (t->ready) return 0;
	while (!t->have_length) {
		ssize_t n = st_connection_stream_read(&t->stream,
		    t->header + t->header_size, 1, timeout);
		size_t used;
		int status;
		if (n <= 0) return n < 0 ? (int)n : IO_ERROR_NOT_CONNECTED;
		++t->header_size;
		if (t->header_size < 2) continue;
		status = read_varint(t->header + 1, t->header_size - 1, &t->body_size, &used);
		if (status == IO_ERROR_WOULD_BLOCK) continue;
		if (status != 0) return status;
		if (t->body_size > MQTT_PACKET_LIMIT) return IO_ERROR_MESSAGE_TOO_LARGE;
		if (t->body_size > t->body_capacity) {
			uint8_t *fresh = realloc(t->body, t->body_size);
			if (fresh == NULL) return IO_ERROR_OUT_OF_MEMORY;
			t->body = fresh;
			t->body_capacity = t->body_size;
		}
		t->have_length = true;
	}
	while (t->body_used < t->body_size) {
		ssize_t n = st_connection_stream_read(&t->stream, t->body + t->body_used,
		    t->body_size - t->body_used, timeout);
		if (n <= 0) return n < 0 ? (int)n : IO_ERROR_NOT_CONNECTED;
		t->body_used += (size_t)n;
	}
	t->ready = true;
	st_connection_log_hooks(&t->stream.hooks, "MQTT", ST_CONNECTION_LOG_DEBUG,
	    "received %s packet bytes=%zu", packet_name(t->header[0]), t->body_size);
	return 0;
}

static int
wait_packet(struct mqtt_transport *t, const struct st_connection_hooks *hooks,
	uint64_t deadline)
{
	for (;;) {
		int status;
		if (st_connection_cancelled(hooks, ST_CONNECTION_SETUP_DISCOVERING))
			return IO_ERROR_CANCELLED;
		if (now_ms() >= deadline) return IO_ERROR_TIMED_OUT;
		status = receive_packet(t, 100);
		if (status != IO_ERROR_WOULD_BLOCK && status != IO_ERROR_TIMED_OUT)
			return status;
	}
}

static int
publish(struct mqtt_transport *t, struct mqtt_publish *result,
	const struct st_connection_hooks *hooks)
{
	size_t offset, properties, used, topic_size;
	unsigned qos = (t->header[0] >> 1) & 3;
	int status;
	if (t->body_size < 3 || qos > 1) return IO_ERROR_PROTOCOL;
	topic_size = ((size_t)t->body[0] << 8) | t->body[1];
	if (topic_size == 0 || topic_size > t->body_size - 2) return IO_ERROR_PROTOCOL;
	offset = 2 + topic_size;
	if (qos == 1) {
		uint8_t ack[2];
		if (t->body_size - offset < 2) return IO_ERROR_PROTOCOL;
		memcpy(ack, t->body + offset, 2);
		if (ack[0] == 0 && ack[1] == 0) return IO_ERROR_PROTOCOL;
		status = packet(t, 0x40, ack, sizeof(ack), hooks);
		if (status != 0) return status;
		offset += 2;
	}
	status = read_varint(t->body + offset, t->body_size - offset, &properties, &used);
	if (status != 0 || properties > t->body_size - offset - used)
		return IO_ERROR_PROTOCOL;
	offset += used + properties;
	result->topic = t->body + 2;
	result->topic_size = topic_size;
	result->payload = t->body + offset;
	result->payload_offset = offset;
	result->payload_size = t->body_size - offset;
	return 0;
}

static int
subscribe(struct mqtt_transport *t, const char *topic, bool remove,
	const struct st_connection_hooks *hooks)
{
	uint8_t body[256];
	size_t used = 3;
	uint16_t id = ++t->packet_id;
	body[0] = (uint8_t)(id >> 8);
	body[1] = (uint8_t)id;
	body[2] = 0; /* empty properties */
	used += put_string(body + used, topic);
	if (!remove) body[used++] = 0; /* QoS 0, deliver retained data */
	st_connection_log_hooks(&t->stream.hooks, "MQTT", ST_CONNECTION_LOG_DEBUG,
	    "sending %s packet-id=%u topic-bytes=%zu", remove ? "UNSUBSCRIBE" : "SUBSCRIBE", id, strlen(topic));
	return packet(t, remove ? 0xa2 : 0x82, body, used, hooks);
}

static int
control_packet(struct mqtt_transport *t)
{
	uint8_t type = t->header[0] >> 4;
	if (type == 14) {
		unsigned reason = t->body_size == 0 ? 0 : t->body[0];
		st_connection_log_hooks(&t->stream.hooks, "MQTT", reason == 0
		    ? ST_CONNECTION_LOG_INFO : ST_CONNECTION_LOG_WARNING, "broker DISCONNECT reason=0x%02X", reason);
		return IO_ERROR_NOT_CONNECTED;
	}
	if (type == 13) {
		if (t->header[0] != 0xd0 || t->body_size != 0) return IO_ERROR_PROTOCOL;
		t->ping_pending = false;
	} else if (type == 9) {
		size_t properties, used;
		if (t->header[0] != 0x90 || t->body_size < 4
		    || read_varint(t->body + 2, t->body_size - 2, &properties, &used) != 0
		    || properties >= t->body_size - 2 - used) return IO_ERROR_PROTOCOL;
		for (size_t i = 2 + used + properties; i < t->body_size; ++i) {
			st_connection_log_hooks(&t->stream.hooks, "MQTT", t->body[i] >= 128
			    ? ST_CONNECTION_LOG_ERROR : ST_CONNECTION_LOG_DEBUG, "SUBACK reason=0x%02X", t->body[i]);
			if (t->body[i] >= 128) return IO_ERROR_PERMISSION_DENIED;
		}
	} else if (type != 11) return IO_ERROR_PROTOCOL;
	return 0;
}

static int
discover(struct mqtt_transport *t, struct mqtt_node *nodes, size_t *count,
	bool find_bbs, const struct st_connection_hooks *hooks)
{
	const uint64_t window = find_bbs ? 5000 : 1500;
	uint64_t end = now_ms() + 10000;
	bool subscribed = false;
	while (now_ms() < end) {
		struct mqtt_publish p;
		int status = wait_packet(t, hooks, end);
		if (status == IO_ERROR_TIMED_OUT) break;
		if (status != 0) return status;
		if ((t->header[0] >> 4) == 3) {
			status = publish(t, &p, hooks);
			if (status != 0) return status;
			if (find_bbs && p.topic_size > 13
			    && memcmp(p.topic, "sbbs/", 5) == 0
			    && memcmp(p.topic + p.topic_size - 8, "/version", 8) == 0) {
				size_t length = p.topic_size - 13;
				if (length < sizeof(t->bbsid)
				    && memchr(p.topic + 5, '/', length) == NULL) {
					memcpy(t->bbsid, p.topic + 5, length);
					t->bbsid[length] = '\0';
					release_packet(t);
					return 0;
				}
			} else if (!find_bbs && p.topic_size < 160) {
				char topic[160], prefix[96], *tail;
				unsigned long number;
				memcpy(topic, p.topic, p.topic_size);
				topic[p.topic_size] = '\0';
				(void)snprintf(prefix, sizeof(prefix), "sbbs/%s/node/", t->bbsid);
				if (strncmp(topic, prefix, strlen(prefix)) == 0) {
					errno = 0;
					number = strtoul(topic + strlen(prefix), &tail, 10);
					if (errno == 0 && number > 0 && number <= INT_MAX
					    && strcmp(tail, "/status") == 0) {
						size_t i;
						for (i = 0; i < *count; ++i)
							if (nodes[i].number == number) break;
						if (i < MQTT_NODE_LIMIT) {
							nodes[i].number = (unsigned)number;
							nodes[i].status = p.payload_size != 0
							    && p.payload[0] >= '0' && p.payload[0] <= '9'
							    ? p.payload[0] - '0' : -1;
							if (i == *count) ++*count;
						}
					}
				}
			}
		} else {
			status = control_packet(t);
			if (status != 0) return status;
			/* Broker acceptance can take longer than the retained-message
			 * collection window. Start that window at this subscription's
			 * SUBACK; older acknowledgements must not restart it. */
			if (!subscribed && t->header[0] == 0x90
			    && (((unsigned)t->body[0] << 8) | t->body[1]) == t->packet_id) {
				subscribed = true;
				end = now_ms() + window;
			}
		}
		release_packet(t);
	}
	return subscribed ? 0 : IO_ERROR_TIMED_OUT;
}

static int
ask(const struct st_connection_hooks *hooks, const char *title,
	const char *question, const char *initial, size_t limit, bool masked, char **result)
{
	*result = NULL;
	if (hooks == NULL || hooks->prompt == NULL) return IO_ERROR_PASSWORD_REQUIRED;
	int status = hooks->prompt(hooks->ui_context, title, question, initial, limit, masked, result);
	return status != 0 ? status : *result == NULL ? IO_ERROR_CANCELLED : 0;
}

static int
node_compare(const void *left, const void *right)
{
	const struct mqtt_node *a = left, *b = right;
	return a->number < b->number ? -1 : a->number > b->number;
}

static int
select_node(struct mqtt_node *nodes, size_t count,
	const struct st_connection_hooks *hooks, unsigned *node)
{
	char labels[MQTT_NODE_LIMIT][64];
	const char *choices[MQTT_NODE_LIMIT + 1];
	static const char *const states[] = {
		"WFC", "logon", "newuser", "main", "xfer", "msg",
		"ext", "in use", "quiet", "logout",
	};
	int selected, status;
	char *answer = NULL, *end;
	unsigned long number;
	qsort(nodes, count, sizeof(*nodes), node_compare);
	if (count != 0 && hooks != NULL && hooks->choice != NULL) {
		for (size_t i = 0; i < count; ++i) {
			(void)snprintf(labels[i], sizeof(labels[i]), "Node %u  (%s)",
			    nodes[i].number, nodes[i].status >= 0 && nodes[i].status < 10
			    ? states[nodes[i].status] : "?");
			choices[i] = labels[i];
		}
		choices[count] = "(Enter node number manually)";
		selected = hooks->choice(hooks->ui_context, "Active Nodes",
		    "Choose a node whose input/output stream should be attached.",
		    choices, count + 1);
		if (selected < 0) return selected;
		if ((size_t)selected < count) { *node = nodes[selected].number; return 0; }
		if ((size_t)selected != count) return IO_ERROR_INVALID_ARGUMENT;
	}
	status = ask(hooks, "Node Number", "Enter the BBS node number to connect to.", "1", 7, false, &answer);
	if (status != 0) return status;
	errno = 0;
	number = strtoul(answer, &end, 10);
	status = errno == 0 && end != answer && *end == '\0'
	    && number > 0 && number <= INT_MAX ? 0 : IO_ERROR_INVALID_ARGUMENT;
	free(answer);
	if (status == 0) *node = (unsigned)number;
	return status;
}

static ssize_t
mqtt_read(void *state, void *data, size_t size, int64_t timeout)
{
	struct mqtt_transport *t = state;
	if (t->payload_size != 0) {
		size_t copied = size < t->payload_size ? size : t->payload_size;
		memcpy(data, t->body + t->payload_offset, copied);
		t->payload_offset += copied;
		t->payload_size -= copied;
		if (t->payload_size == 0) release_packet(t);
		return (ssize_t)copied;
	}
	if (t->ping_pending && now_ms() - t->ping_sent > 60000)
		return IO_ERROR_CONNECTION_RESET;
	if (!t->ping_pending && now_ms() - atomic_load(&t->last_send) >= 30000) {
		int status = packet(t, 0xc0, NULL, 0, NULL);
		if (status != 0) return status;
		t->ping_sent = now_ms();
		t->ping_pending = true;
	}
	int status = receive_packet(t, timeout);
	if (status != 0) return status;
	if ((t->header[0] >> 4) == 3) {
		struct mqtt_publish p;
		status = publish(t, &p, NULL);
		if (status != 0) return status;
		if (p.topic_size == strlen(t->output_topic)
		    && memcmp(p.topic, t->output_topic, p.topic_size) == 0
		    && p.payload_size != 0) {
			t->payload_offset = p.payload_offset;
			t->payload_size = p.payload_size;
			return mqtt_read(t, data, size, 0);
		}
	} else {
		status = control_packet(t);
		if (status != 0) return status == IO_ERROR_NOT_CONNECTED ? 0 : status;
	}
	release_packet(t);
	return IO_ERROR_WOULD_BLOCK;
}

static ssize_t
mqtt_send(st_connection_t *connection, const void *data, size_t size, int64_t timeout)
{
	struct mqtt_transport *t = connection->transport;
	uint8_t wire[4400];
	size_t consumed = size < 4096 ? size : 4096;
	size_t topic = strlen(t->input_topic), body = 2 + topic + 1 + consumed;
	size_t used = 1;
	ssize_t n;
	wire[0] = 0x30;
	used += varint(wire + used, body);
	used += put_string(wire + used, t->input_topic);
	wire[used++] = 0; /* empty properties */
	memcpy(wire + used, data, consumed);
	used += consumed;
	n = st_connection_queue_output_atomic(connection, wire, used, timeout);
	if (n == (ssize_t)used) st_connection_log(connection, ST_CONNECTION_LOG_DEBUG,
	    "sending PUBLISH topic-bytes=%zu payload-bytes=%zu", strlen(t->input_topic), consumed);
	return n == (ssize_t)used ? (ssize_t)consumed : n < 0 ? n : IO_ERROR_IO;
}

static void
mqtt_bind(void *state, st_connection_t *connection)
{
	struct mqtt_transport *t = state;
	t->connection = connection;
}
static ssize_t
mqtt_write(void *state, const void *data, size_t size, int64_t timeout)
{
	struct mqtt_transport *t = state;
	ssize_t n = st_connection_stream_write(&t->stream, data, size, timeout);
	if (n > 0) atomic_store(&t->last_send, now_ms());
	return n;
}
static void
mqtt_interrupt(void *state)
{
	struct mqtt_transport *t = state;
	st_connection_stream_interrupt(&t->stream);
}
static void
mqtt_destroy(void *state)
{
	struct mqtt_transport *t = state;
	st_connection_log_hooks(&t->stream.hooks, "MQTT", ST_CONNECTION_LOG_DEBUG, "closing MQTT session");
	st_connection_stream_close(&t->stream);
	free(t->body);
	free(t);
}
static int
mqtt_process_requests(void *state)
{
	struct mqtt_transport *t = state;
	return st_connection_stream_process_requests(&t->stream);
}
static const struct st_connection_transport_ops operations = {
	.bind = mqtt_bind, .read = mqtt_read, .write = mqtt_write,
	.interrupt = mqtt_interrupt, .destroy = mqtt_destroy,
	.process_requests = mqtt_process_requests,
};

static void
lowercase(char *text)
{
	for (; *text != '\0'; ++text)
		if (*text >= 'A' && *text <= 'Z') *text += 'a' - 'A';
}

int
st_connection_connect_mqtt(st_connection_t **result, const struct st_profile *profile,
	const struct st_connection_hooks *hooks, struct st_connection_error *error)
{
	struct mqtt_transport *t;
	struct mqtt_node nodes[MQTT_NODE_LIMIT];
	struct st_profile copy;
	struct st_tls_client_options tls = {0};
	char identity[ST_PROFILE_USER_MAX + 1], secret[ST_PROFILE_PASSWORD_MAX + 1];
	uint8_t connect[ST_PROFILE_USER_MAX + ST_PROFILE_PASSWORD_MAX
	    + ST_PROFILE_SYSTEM_PASSWORD_MAX + 128];
	char status_topic[160], *answer = NULL, psk_error[1024] = "";
	size_t used, count = 0;
	unsigned node = 0;
	int status;
	bool psk = true, stream_failed = false;
	enum st_connection_setup_phase phase = ST_CONNECTION_SETUP_AUTHENTICATING;
	if (result == NULL || profile == NULL || profile->connection_type != ST_CONNECTION_MQTT
	    || profile->address[0] == '\0') return IO_ERROR_INVALID_ARGUMENT;
	st_profile_init(&copy);
	if (!st_profile_copy(&copy, profile)) return IO_ERROR_OUT_OF_MEMORY;
	t = calloc(1, sizeof(*t));
	if (t == NULL) { st_profile_dispose(&copy); return IO_ERROR_OUT_OF_MEMORY; }
	atomic_init(&t->last_send, now_ms());
	if (copy.username[0] == '\0') {
		status = ask(hooks, "MQTT authentication", "User name", "", ST_PROFILE_USER_MAX, false, &answer);
		if (status != 0) goto failed;
		(void)snprintf(copy.username, sizeof(copy.username), "%s", answer);
		free(answer); answer = NULL;
	}
	if (copy.password[0] == '\0' && copy.tls_psk[0] == '\0') {
		status = ask(hooks, "MQTT authentication", "Password", "", ST_PROFILE_PASSWORD_MAX, true, &answer);
		if (status != 0) goto failed;
		(void)snprintf(copy.password, sizeof(copy.password), "%s", answer);
		xp_crypto_clear(answer, strlen(answer)); free(answer); answer = NULL;
	}
	strcpy(identity, copy.username);
	strcpy(secret, copy.password);
	lowercase(identity); lowercase(secret);
	tls.server_name = copy.address;
	tls.psk_identity = copy.tls_psk_identity[0] != '\0' ? copy.tls_psk_identity : identity;
	tls.psk = copy.tls_psk[0] != '\0' ? copy.tls_psk : secret;
	tls.psk_size = strlen(tls.psk);
	tls.psk_version = copy.tls_psk[0] != '\0'
	    && copy.tls_psk_version == ST_TLS_VERSION_1_3
	    ? ST_TLS_PROTOCOL_1_3 : ST_TLS_PROTOCOL_1_2;
	tls.psk_policy = copy.tls_psk[0] == '\0' ? ST_TLS_PSK_TLS12_COMPATIBILITY
	    : ST_TLS_PSK_MODERN;
	tls.handshake_timeout_ms = 15000;
	st_connection_log_hooks(hooks, "MQTT", ST_CONNECTION_LOG_INFO, "starting TLS-PSK negotiation");
	status = st_connection_stream_open(&t->stream, &copy, true, &tls, hooks, error);
	xp_crypto_clear(secret, sizeof(secret));
	if (status != 0 && status != IO_ERROR_CANCELLED && copy.tls_psk[0] == '\0') {
		(void)snprintf(psk_error, sizeof(psk_error), "%s", error == NULL
		    ? t->stream.diagnostic : error->detail);
		psk = false;
		st_connection_log_hooks(hooks, "MQTT", ST_CONNECTION_LOG_INFO, "starting certificate TLS negotiation");
		status = st_connection_stream_open(&t->stream, &copy, true, NULL, hooks, error);
	}
	if (status != 0) {
		stream_failed = true;
		phase = error == NULL ? ST_CONNECTION_SETUP_NEGOTIATING : error->phase;
		goto failed;
	}
	st_connection_log_hooks(hooks, "MQTT", ST_CONNECTION_LOG_INFO,
	    "TLS established using %s authentication", psk ? "PSK" : "certificate");
	if (psk && copy.system_password[0] == '\0') {
		status = ask(hooks, "System Password", "The Synchronet internal MQTT broker authenticates with the BBS system password.", "",
		    ST_PROFILE_SYSTEM_PASSWORD_MAX, true, &answer);
		if (status != 0) goto failed;
		(void)snprintf(copy.system_password, sizeof(copy.system_password), "%s", answer);
		xp_crypto_clear(answer, strlen(answer)); free(answer); answer = NULL;
		if (copy.system_password[0] == '\0') { status = IO_ERROR_CANCELLED; goto failed; }
	}
	if (hooks != NULL && hooks->status != NULL) hooks->status(hooks->ui_context, "Sending CONNECT");
	used = put_string(connect, "MQTT");
	connect[used++] = 5; /* MQTT 5.0 */
	connect[used++] = 0xc2; /* clean start, username and password */
	connect[used++] = 0; connect[used++] = 60;
	connect[used++] = 0; /* empty properties */
	used += put_string(connect + used, ""); /* broker-assigned client ID */
	used += put_string(connect + used, copy.username);
	used += put_string(connect + used, psk ? copy.system_password : copy.password);
	status = packet(t, 0x10, connect, used, hooks);
	xp_crypto_clear(connect, sizeof(connect));
	if (status == 0) status = wait_packet(t, hooks, now_ms() + 10000);
	if (status == 0 && (t->header[0] != 0x20 || t->body_size < 3
	    || t->body[0] != 0 || t->body[1] != 0)) status = IO_ERROR_PERMISSION_DENIED;
	if (status != 0) goto failed;
	st_connection_log_hooks(hooks, "MQTT", ST_CONNECTION_LOG_INFO, "broker accepted CONNECT");
	release_packet(t);
	phase = ST_CONNECTION_SETUP_DISCOVERING;
	if (hooks != NULL && hooks->status != NULL) hooks->status(hooks->ui_context, "Discovering BBSID");
	status = subscribe(t, "sbbs/+/version", false, hooks);
	if (status == 0) status = discover(t, nodes, &count, true, hooks);
	if (status == 0) status = subscribe(t, "sbbs/+/version", true, hooks);
	if (status != 0) goto failed;
	if (t->bbsid[0] == '\0') {
		status = ask(hooks, "BBSID", "Enter the upper-case BBSID (QWK ID) configured in SCFG under System Information.",
		    "", sizeof(t->bbsid) - 1, false, &answer);
		if (status != 0) goto failed;
		(void)snprintf(t->bbsid, sizeof(t->bbsid), "%s", answer);
		free(answer); answer = NULL;
		for (char *p = t->bbsid; *p != '\0'; ++p)
			if (*p >= 'a' && *p <= 'z') *p -= 'a' - 'A';
	}
	if (t->bbsid[0] == '\0' || strpbrk(t->bbsid, "/+#") != NULL) {
		status = IO_ERROR_PROTOCOL; goto failed;
	}
	for (const unsigned char *p = (const unsigned char *)t->bbsid; *p != 0; ++p)
		if (*p < 0x21 || *p > 0x7e) { status = IO_ERROR_PROTOCOL; goto failed; }
	st_connection_log_hooks(hooks, "MQTT", ST_CONNECTION_LOG_INFO, "BBSID selected (value omitted)");
	(void)snprintf(status_topic, sizeof(status_topic), "sbbs/%s/node/+/status", t->bbsid);
	if (hooks != NULL && hooks->status != NULL) hooks->status(hooks->ui_context, "Discovering nodes");
	status = subscribe(t, status_topic, false, hooks);
	if (status == 0) status = discover(t, nodes, &count, false, hooks);
	if (status == 0) status = select_node(nodes, count, hooks, &node);
	if (status == 0) status = subscribe(t, status_topic, true, hooks);
	(void)snprintf(t->input_topic, sizeof(t->input_topic), "sbbs/%s/node/%u/input", t->bbsid, node);
	(void)snprintf(t->output_topic, sizeof(t->output_topic), "sbbs/%s/node/%u/output", t->bbsid, node);
	if (status == 0) status = subscribe(t, t->output_topic, false, hooks);
	if (status != 0) goto failed;
	st_connection_log_hooks(hooks, "MQTT", ST_CONNECTION_LOG_INFO, "node %u selected", node);
	xp_crypto_clear(copy.password, sizeof(copy.password));
	xp_crypto_clear(copy.system_password, sizeof(copy.system_password));
	xp_crypto_clear(copy.tls_psk, sizeof(copy.tls_psk));
	st_profile_dispose(&copy);
	if (hooks != NULL && hooks->status != NULL) hooks->status(hooks->ui_context, NULL);
	return st_connection_start(result, t, &operations, "MQTT", hooks, mqtt_send, error);
failed:
	st_connection_log_hooks(hooks, "MQTT", status == IO_ERROR_CANCELLED
	    ? ST_CONNECTION_LOG_INFO : ST_CONNECTION_LOG_ERROR,
	    "MQTT setup %s (%d)", status == IO_ERROR_CANCELLED ? "cancelled" : "failed", status);
	if (answer != NULL) { xp_crypto_clear(answer, strlen(answer)); free(answer); }
	xp_crypto_clear(copy.password, sizeof(copy.password));
	xp_crypto_clear(copy.system_password, sizeof(copy.system_password));
	xp_crypto_clear(copy.tls_psk, sizeof(copy.tls_psk));
	st_profile_dispose(&copy);
	mqtt_destroy(t);
	char message[2400];
	if (stream_failed)
		(void)snprintf(message, sizeof(message), "TLS handshake failed.\n%s%s%s%s",
		    psk_error[0] == '\0' ? "" : "PSK leg: ", psk_error,
		    psk_error[0] == '\0' ? "" : "\nCert leg: ",
		    error == NULL ? "TLS connection failed" : error->detail);
	else
		(void)snprintf(message, sizeof(message), "MQTT authentication, discovery, or node selection failed (%d)", status);
	st_connection_fail(error, status, phase, message);
	if (status != IO_ERROR_CANCELLED) {
		bool presented = st_connection_error_dialog(hooks, stream_failed ? "MQTT TLS Error" : "MQTT connection failed", message);
		if (error != NULL) error->presented = presented;
	}
	return status;
}
