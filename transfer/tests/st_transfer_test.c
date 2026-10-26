/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "private/transfer_internal.h"
#include <stdio.h>
#include "st_transfer_detect.h"
#define CHECK(e) do { if (!(e)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#e); return 1; } } while (0)
struct output { uint8_t bytes[20000]; size_t size; };
static ssize_t write_output(void *context, const void *bytes, size_t size, int64_t timeout)
{
	struct output *out = context;
	(void)timeout;
	if (size > 7) size = 7;
	if (size > sizeof(out->bytes) - out->size) return IO_ERROR_IO;
	memcpy(out->bytes + out->size, bytes, size); out->size += size;
	return (ssize_t)size;
}
static int wait_done(st_transfer_t *t)
{
	for (unsigned i = 0; i < 1000 && !st_transfer_done(t); ++i) st_transfer_sleep(2);
	return st_transfer_done(t) ? st_transfer_result(t) : IO_ERROR_TIMED_OUT;
}
/* The receiver must flush its last ACK without another receive call, and a
 * sender must consume a queued ACK even when EOF immediately follows it. */
struct handshake {
	st_transfer_t *transfer;
	uint8_t packet[1030];
	size_t size;
	unsigned acknowledgments, eots, terminators;
	bool sending, acknowledge_end, retry_end, ended;
};
static void feed_reply(struct handshake *h, const uint8_t *bytes, size_t size)
{
	(void)st_transfer_feed(h->transfer, bytes, size);
}
static ssize_t handshake_write(void *context, const void *bytes, size_t size, int64_t timeout)
{
	struct handshake *h = context;
	const uint8_t *data = bytes;
	(void)timeout;
	for (size_t i = 0; i < size; ++i) {
		uint8_t byte = data[i];
		if (!h->sending) {
			if (byte == 'C' && !h->ended) {
				uint8_t terminal[133] = {1, 0, 255};
				feed_reply(h, terminal, sizeof(terminal));
				st_transfer_eof(h->transfer);
				h->ended = true;
			} else if (byte == 6) ++h->acknowledgments;
			continue;
		}
		if (h->size == 0 && byte == 4) {
			const uint8_t reply[] = {6, 'C'};
			if (++h->eots == 1) { const uint8_t nak = 21; feed_reply(h, &nak, 1); }
			else feed_reply(h, reply, sizeof(reply));
			continue;
		}
		if (h->ended) continue; /* Ignore cancellation after a missing ACK. */
		h->packet[h->size++] = byte;
		size_t expected = h->packet[0] == 1 ? 133 : 1029;
		if (h->size != expected) continue;
		bool header = h->packet[1] == 0;
		if (header && h->packet[3] == 0) {
			if (++h->terminators == 1 && h->retry_end) {
				const uint8_t nak = 21; feed_reply(h, &nak, 1); h->size = 0; continue;
			}
			if (h->acknowledge_end) { const uint8_t ack = 6; feed_reply(h, &ack, 1); }
			st_transfer_eof(h->transfer);
			h->ended = true;
		} else {
			const uint8_t reply[] = {6, 'C'};
			feed_reply(h, reply, header ? 2 : 1);
		}
		h->size = 0;
	}
	return (ssize_t)size;
}
static int test_batch_terminator(const char *file)
{
	struct handshake h = {0};
	struct st_transfer_config config = {.protocol = ST_TRANSFER_YMODEM,
	    .directory = ".", .send = handshake_write, .send_context = &h};
	CHECK(st_transfer_create(&h.transfer, &config) == 0);
	CHECK(st_transfer_launch(h.transfer) == 0 && wait_done(h.transfer) == 0);
	CHECK(h.ended && h.acknowledgments == 1);
	st_transfer_destroy(&h.transfer);
	for (unsigned ack = 0; ack < 3; ++ack) {
		memset(&h, 0, sizeof(h)); h.sending = true; h.acknowledge_end = ack != 0; h.retry_end = ack == 2;
		config.upload = true; config.paths = &file; config.path_count = 1;
		CHECK(st_transfer_create(&h.transfer, &config) == 0);
		const uint8_t request = 'C'; feed_reply(&h, &request, 1);
		CHECK(st_transfer_launch(h.transfer) == 0);
		int result = wait_done(h.transfer);
		CHECK(h.ended && result == (ack ? 0 : IO_ERROR_PROTOCOL));
		CHECK(h.terminators == (ack == 2 ? 2U : 1U));
		st_transfer_destroy(&h.transfer);
	}
	return 0;
}
static int test_detector(void)
{
	struct st_transfer_detector detector = {0};
	const uint8_t header[] = "**\030B00000000000000";
	uint8_t output[33]; size_t size;
	for (size_t i = 0; i < sizeof(header) - 1; ++i) {
		int event = st_transfer_detect(&detector, header[i], output, &size);
		CHECK(event == (i == sizeof(header) - 2 ? 1 : 0));
		if (i == 1) CHECK(size == 1 && output[0] == '*');
		else if (event == 0) CHECK(size == 0);
		else CHECK(size == 17 && memcmp(output, header + 1, 17) == 0);
	}
	uint8_t invalid[sizeof(header)]; memcpy(invalid, header, sizeof(header));
	invalid[sizeof(header) - 2] = '1';
	uint8_t literal[64]; size_t used = 0;
	for (size_t i = 0; i < sizeof(header) - 1; ++i) {
		CHECK(st_transfer_detect(&detector, invalid[i], output, &size) == 0);
		memcpy(literal + used, output, size); used += size;
	}
	CHECK(used == sizeof(header) - 1 && memcmp(literal, invalid, used) == 0);
	CHECK(st_transfer_detect(&detector, '*', output, &size) == 0 && size == 0);
	CHECK(st_transfer_detect(&detector, 'X', output, &size) == 0 && size == 2 && memcmp(output, "*X", 2) == 0);
	return 0;
}
static void feed_cet(st_transfer_t *t, const char *block, bool corrupt)
{
	uint8_t check = 0; char wire[1024];
	const char *start = strstr(block, "|A");
	for (const char *p = start + 2; *p; ++p) check ^= (uint8_t)*p;
	int count = snprintf(wire, sizeof(wire), "%s|Z%03u", block, corrupt ? check ^ 1U : check);
	(void)st_transfer_feed(t, wire, (size_t)count);
}
struct cet_output { st_transfer_t *transfer; unsigned frames, retries; bool bad; };
static ssize_t cet_write(void *context, const void *bytes, size_t size, int64_t timeout)
{
	struct cet_output *o = context;
	(void)timeout;
	if (size == 3 && memcmp(bytes, "*00", 3) == 0) {
		++o->retries;
		feed_cet(o->transfer, "\f|A|Ga01|I|1@A", false);
		feed_cet(o->transfer, "|A|Ga11|IB", false);
	} else if (size == 1 && *(const uint8_t *)bytes == '_') {
		if (++o->frames == 1) {
			feed_cet(o->transfer, "\f|A|Ga01|I|1@A", o->bad);
			if (!o->bad) feed_cet(o->transfer, "|A|Ga11|IB", false);
		} else {
			feed_cet(o->transfer, "\f|A|Gb|I|0}|E|}|L|F", false);
			st_transfer_eof(o->transfer);
		}
	}
	return (ssize_t)size;
}
static int test_cet(void)
{
	for (unsigned corrupt = 0; corrupt < 2; ++corrupt) {
		struct cet_output output = {.bad = corrupt != 0};
		struct st_transfer_config config = {.protocol = ST_TRANSFER_CET,
		    .directory = ".", .send = cet_write, .send_context = &output};
		CHECK(st_transfer_create(&output.transfer, &config) == 0);
		feed_cet(output.transfer, "\f|A|Gz|Itransfer-cet-test.bin|L002", false);
		CHECK(st_transfer_launch(output.transfer) == 0 && wait_done(output.transfer) == 0);
		CHECK(output.frames == 2 && output.retries == corrupt);
		FILE *file = fopen("transfer-cet-test.bin", "rb"); CHECK(file != NULL);
		uint8_t actual[32]; size_t count = fread(actual, 1, sizeof(actual), file);
		CHECK(fclose(file) == 0);
#if SYNCTERM_PLATFORM_WINDOWS
		CHECK(count == 8 && memcmp(actual, "\0\1\2 |}\r\n", 8) == 0);
#else
		CHECK(count == 7 && memcmp(actual, "\0\1\2 |}\n", 7) == 0);
#endif
		size_t replay_size; const uint8_t *replay = st_transfer_replay(output.transfer, &replay_size);
		CHECK(replay_size > 10 && replay[0] == 12 && memcmp(replay + 1, "|A|Gb|I", 7) == 0);
		st_transfer_destroy(&output.transfer);
		CHECK(remove("transfer-cet-test.bin") == 0);
	}
	return 0;
}
static int test_dialog_shutdown(const char *file)
{
	for (unsigned eof = 0; eof < 2; ++eof) {
		struct output out = {0};
		struct st_transfer_config config = {.protocol = ST_TRANSFER_CET,
		    .directory = ".", .send = write_output, .send_context = &out};
		st_transfer_t *t = NULL;
		CHECK(st_transfer_create(&t, &config) == 0);
		char header[1024]; snprintf(header, sizeof(header), "|A%s|L001", file);
		feed_cet(t, header, false);
		CHECK(st_transfer_launch(t) == 0);
		unsigned i;
		for (i = 0; i < 500 && st_transfer_dialog(t, NULL, 0) == ST_TRANSFER_DIALOG_NONE; ++i) st_transfer_sleep(2);
		CHECK(i != 500);
		if (eof) st_transfer_eof(t); else CHECK(st_transfer_request_abort(t));
		CHECK(wait_done(t) != 0);
		st_transfer_destroy(&t);
	}
	return 0;
}
struct xy_wire { uint8_t bytes[600]; size_t size, position; unsigned acks; };
static int xy_read(void *context, unsigned timeout)
{
	struct xy_wire *w = context; (void)timeout;
	return w->position < w->size ? w->bytes[w->position++] : -1;
}
static int xy_write(void *context, uint8_t byte, unsigned timeout)
{
	struct xy_wire *w = context; (void)timeout;
	if (byte == ACK) ++w->acks;
	return 0;
}
static bool xy_connected(void *context)
{
	struct xy_wire *w = context; return w->position < w->size;
}
static void xy_packet(struct xy_wire *w, uint8_t number, uint8_t payload, bool corrupt)
{
	uint16_t crc = 0;
	w->bytes[w->size++] = SOH; w->bytes[w->size++] = number; w->bytes[w->size++] = (uint8_t)~number;
	for (unsigned i = 0; i < 128; ++i) { w->bytes[w->size++] = payload; crc = st_transfer_crc16_byte(payload, crc); }
	w->bytes[w->size++] = (uint8_t)(crc >> 8);
	w->bytes[w->size++] = (uint8_t)(crc ^ (corrupt ? 1 : 0));
}
static int test_xy_recovery(void)
{
	struct xy_wire wire = {0}; xmodem_t xm; long mode = XMODEM | CRC | RECV;
	xmodem_init(&xm, &wire, &mode, NULL, NULL, xy_write, xy_read, xy_connected, NULL, NULL);
	uint8_t block[1024];
	xy_packet(&wire, 1, 'A', false); xy_packet(&wire, 1, 'A', false); xy_packet(&wire, 2, 'B', false);
	CHECK(xmodem_get_block(&xm, block, 1) == SUCCESS && block[0] == 'A');
	CHECK(xmodem_get_block(&xm, block, 2) == SUCCESS && block[0] == 'B' && wire.acks == 1);
	memset(&wire, 0, sizeof(wire));
	xy_packet(&wire, 1, 'A', true); xy_packet(&wire, 1, 'A', false);
	CHECK(xmodem_get_block(&xm, block, 1) == FAILURE);
	CHECK(xmodem_get_block(&xm, block, 1) == SUCCESS);
	memset(&wire, 0, sizeof(wire)); xy_packet(&wire, 1, 'A', false); --wire.size;
	CHECK(xmodem_get_block(&xm, block, 1) == FAILURE);
	return 0;
}
struct mailbox_output { struct output output; st_transfer_t *transfer; bool fed; };
static ssize_t mailbox_write(void *context, const void *bytes, size_t size, int64_t timeout)
{
	struct mailbox_output *o = context;
	ssize_t count = write_output(&o->output, bytes, size, timeout);
	if (!o->fed && count > 0 && *(const uint8_t *)bytes == 'G') {
		o->fed = true;
		for (unsigned i = 1; i <= 32; ++i) {
			struct xy_wire wire = {0};
			xy_packet(&wire, (uint8_t)i, (uint8_t)i, false);
			(void)st_transfer_feed(o->transfer, wire.bytes, wire.size);
		}
		const uint8_t eot = 4;
		(void)st_transfer_feed(o->transfer, &eot, 1);
	}
	return count;
}
static int test_received_mailbox(const char *existing)
{
	/* A slow renderer must backpressure a streaming receiver, and both
	 * cancellation and EOF must wake it without requiring another drain. */
	for (unsigned action = 0; action < 3; ++action) {
		struct mailbox_output out = {0};
		struct st_transfer_config config = {.protocol = ST_TRANSFER_XMODEM_G,
		    .directory = ".", .filename = existing, .discard = true,
		    .receive_events = true, .send = mailbox_write, .send_context = &out};
		st_transfer_t *t = NULL;
		CHECK(st_transfer_create(&t, &config) == 0);
		out.transfer = t;
		CHECK(st_transfer_launch(t) == 0);
		bool full = false;
		for (unsigned i = 0; i < 500 && !full; ++i) {
			(void)st_mutex_lock(&t->mutex); full = t->received_size == 16; (void)st_mutex_unlock(&t->mutex);
			st_transfer_sleep(2);
		}
		if (!full) {
			struct st_transfer_log logs[32]; size_t count = st_transfer_drain_log(t, logs, 32);
			for (size_t i = 0; i < count; ++i) fprintf(stderr, "%s\n", logs[i].text);
			fprintf(stderr, "mailbox action %u: result %d\n", action, st_transfer_result(t));
		}
		CHECK(full && !st_transfer_done(t));
		if (action != 0) {
			if (action == 1) CHECK(st_transfer_request_abort(t));
			else st_transfer_eof(t);
			CHECK(wait_done(t) != 0);
		} else {
			unsigned blocks = 0, starts = 0, ends = 0;
			for (unsigned i = 0; i < 1000; ++i) {
				struct st_transfer_receive_event event;
				while (st_transfer_take_received(t, &event)) {
					if (event.kind == ST_TRANSFER_RECEIVE_BEGIN) ++starts;
					else if (event.kind == ST_TRANSFER_RECEIVE_END) { CHECK(event.success); ++ends; }
					else {
						++blocks; CHECK(event.size == 128);
						for (size_t b = 0; b < event.size; ++b) CHECK(event.bytes[b] == blocks);
					}
				}
				if (st_transfer_done(t) && ends == 1) break;
				st_transfer_sleep(2);
			}
			CHECK(st_transfer_done(t) && st_transfer_result(t) == 0);
			CHECK(starts == 1 && blocks == 32 && ends == 1);
			CHECK(out.output.size != 0 && out.output.bytes[0] == 'G');
		}
		st_transfer_destroy(&t);
		struct fs_info info;
		CHECK(fs_get_info(&info, existing) == 0 && info.size == 8200);
	}
	return 0;
}
int main(void)
{
	CHECK(test_xy_recovery() == 0);
	CHECK(test_detector() == 0);
	CHECK(test_cet() == 0);
	CHECK(st_transfer_crc16_byte(0, 0) == 0);
	uint16_t crc = 0;
	for (const char *p = "123456789"; *p; ++p) crc = st_transfer_crc16_byte((uint8_t)*p, crc);
	CHECK(crc == 0x31c3);
	CHECK(~st_transfer_crc32_update(UINT32_MAX, "123456789", 9) == UINT32_C(0xcbf43926));
	struct st_transfer_config config = {.protocol = ST_TRANSFER_ZMODEM, .directory = ".", .send = write_output};
	struct output out = {0}; config.send_context = &out;
	st_transfer_t *t = NULL;
	CHECK(st_transfer_start(&t, &config) == 0);
	CHECK(st_transfer_request_abort(t));
	CHECK(wait_done(t) == IO_ERROR_CANCELLED);
	st_transfer_destroy(&t); CHECK(t == NULL);
	CHECK(st_transfer_start(&t, &config) == 0);
	st_transfer_eof(t);
	CHECK(wait_done(t) != 0);
	st_transfer_destroy(&t);
	const char *file = "transfer-ascii-test.txt";
	FILE *stream = fopen(file, "wb"); CHECK(stream != NULL);
	uint8_t data[8200]; memset(data, 'x', sizeof(data)); data[8191] = '\r'; data[8192] = '\n'; data[8193] = '\n'; data[8194] = 0;
	CHECK(fwrite(data, 1, sizeof(data), stream) == sizeof(data) && fclose(stream) == 0);
	CHECK(test_received_mailbox(file) == 0);
	for (unsigned raw = 0; raw < 2; ++raw) {
		out.size = 0;
		config.protocol = raw ? ST_TRANSFER_RAW : ST_TRANSFER_ASCII;
		config.upload = true; config.paths = &file; config.path_count = 1;
		CHECK(st_transfer_start(&t, &config) == 0 && wait_done(t) == 0);
		CHECK(out.size == sizeof(data) + (raw ? 0 : 1));
		CHECK(memcmp(out.bytes, data, 8193) == 0);
		if (raw) CHECK(memcmp(out.bytes, data, sizeof(data)) == 0);
		else CHECK(out.bytes[8193] == '\r' && out.bytes[8194] == '\n' && out.bytes[8195] == 0);
		st_transfer_destroy(&t);
	}
	CHECK(test_batch_terminator(file) == 0);
	CHECK(test_dialog_shutdown(file) == 0);
	CHECK(remove(file) == 0);
	return 0;
}
