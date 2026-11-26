/* Independently specified native N sample times, bounds and live storage. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io_error.h"
#include "st_big_control.h"
#include "st_big_music.h"
#define CHECK(x)                                                                \
	do {                                                                    \
		if (!(x)) {                                                     \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); \
			exit(1);                                                \
		}                                                               \
	} while (0)

struct fixture {
	struct st_big_music          music;
	struct st_big_music_buffer   buffer;
	struct st_big_music_schedule schedule;
	uint64_t                     frame;

	struct {
		uint64_t frame;
		uint16_t address, value;
	} writes[8];

	size_t   count;
	unsigned reads;
	bool     blocked;
};

static int
read_buffer(void *context, uint16_t offset, bool word, uint16_t *value)
{
	struct fixture *f = context;
	++f->reads;
	return f->blocked ? IO_ERROR_IN_PROGRESS : st_big_music_buffer_read(&f->buffer, offset, word, value);
}

static int
write_register(void *context, uint16_t address, uint16_t value)
{
	struct fixture *f = context;
	CHECK(f->count < 8);
	f->writes[f->count].frame   = f->frame;
	f->writes[f->count].address = address;
	f->writes[f->count++].value = value;
	return 0;
}

static int
poll(struct fixture *f, size_t budget)
{
	const struct st_big_music_sink sink = {f, read_buffer, write_register};
	return st_big_music_schedule_poll(&f->schedule, &f->music, &sink, &budget);
}

static void
init(struct fixture *f, const uint8_t *bytes, size_t size, uint16_t length)
{
	memset(f, 0, sizeof(*f));
	CHECK(size <= sizeof(f->buffer.bytes));
	if (size)
		memcpy(f->buffer.bytes, bytes, size);
	f->music = (struct st_big_music) {.running = 1,
	    .enabled                               = 1,
	    .length                                = length,
	    .divider                               = 2485,
	    .cached_divider                        = 2485,
	    .position_low                          = 2,
	    .end_low                               = (uint16_t)(length - 1),
	    .delay                                 = 1};
	CHECK(st_big_music_schedule_arm(&f->schedule, &f->music, 1) == 0);
}

static void
run_to(struct fixture *f, uint64_t target, size_t chunk, size_t budget)
{
	unsigned polls = 0;
	for (;;) {
		CHECK(++polls < 100000);
		int status = poll(f, budget);
		CHECK(status == 0 || status == IO_ERROR_IN_PROGRESS);
		if (status == IO_ERROR_IN_PROGRESS)
			continue;
		if (f->frame == target)
			break;
		uint64_t span = target - f->frame;
		if (span > chunk)
			span = chunk;
		if (f->schedule.armed && span > f->schedule.remaining)
			span = f->schedule.remaining;
		CHECK(span != 0 && st_big_music_schedule_consume(&f->schedule, span) == 0);
		f->frame += span;
	}
}

static void
bounds_and_retention(void)
{
	struct st_big_music_buffer b     = {0};
	uint16_t                   value = 0xdead;
	CHECK(st_big_music_buffer_store(&b, 0, 0xb5) == 0 && st_big_music_buffer_store(&b, 1, 9) == 0);
	CHECK(st_big_music_buffer_read(&b, 0, true, &value) == 0 && value == 0x09b5);
	CHECK(st_big_music_buffer_store(&b, 65533, 0x9a) == 0);
	CHECK(st_big_music_buffer_read(&b, 65533, false, &value) == 0 && value == 0x9a);
	CHECK(st_big_music_buffer_read(&b, 65532, true, &value) == 0 && value == 0x9a00);
	value = 0xbeef;
	CHECK(st_big_music_buffer_read(&b, 65533, true, &value) == IO_ERROR_OUT_OF_RANGE && value == 0xbeef);
	CHECK(st_big_music_buffer_read(&b, 65534, false, &value) == IO_ERROR_OUT_OF_RANGE && value == 0xbeef);
	CHECK(st_big_music_buffer_store(&b, 65534, 7) == IO_ERROR_OUT_OF_RANGE && b.bytes[65533] == 0x9a);
	CHECK(st_big_music_buffer_store(&b, 65535, 7) == IO_ERROR_OUT_OF_RANGE);
	CHECK(b.bytes[0] == 0xb5 && b.bytes[1] == 9 && b.bytes[65533] == 0x9a);
	CHECK(st_big_music_buffer_read(NULL, 0, true, &value) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_music_buffer_read(&b, 0, true, NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_music_buffer_store(NULL, 0, 0) == IO_ERROR_INVALID_ARGUMENT);
}

static void
literal_callback_times(void)
{
	static const uint8_t bytes[] = {0xb5, 9, 0x20, 0x23, 1, 0xff, 4, 0, 0x21, 0x56, 0, 0xff, 0, 0, 0xff};
	for (unsigned budget = 1; budget <= 64; ++budget)
		for (unsigned chunk = 1; chunk <= 4096; chunk *= 16) {
			struct fixture f;
			init(&f, bytes, sizeof(bytes), 15);
			CHECK(f.schedule.remaining == 91 && f.schedule.fraction == 1009029);
			run_to(&f, 90, chunk, budget);
			CHECK(f.count == 0 && st_big_music_position(&f.music) == 2);
			run_to(&f, 458, chunk, budget);
			CHECK(f.count == 1 && f.writes[0].frame == 91 && f.writes[0].address == 0x20
			      && f.writes[0].value == 0x123);
			CHECK(st_big_music_position(&f.music) == 8 && f.music.running == 1
			      && f.schedule.remaining == 1);
			run_to(&f, 459, chunk, budget);
			CHECK(f.count == 2 && f.writes[1].frame == 459 && f.writes[1].address == 0x21
			      && f.writes[1].value == 0x56);
			CHECK(st_big_music_position(&f.music) == 14 && f.music.running == 0 && f.music.delay == 1
			      && !f.schedule.armed);
			unsigned reads = f.reads;
			run_to(&f, 10000, chunk, budget);
			CHECK(f.reads == reads && f.count == 2);
		}
}

static void
conversion_and_controls(void)
{
	struct fixture f;
	init(&f, NULL, 0, 100);
	struct st_big_music before;
	memcpy(&before, &f.music, sizeof(before));
	f.music.divider = 0;
	before.divider  = 0;
	CHECK(st_big_music_schedule_arm(&f.schedule, &f.music, 1) == 0 && f.schedule.remaining == 2422
	      && f.schedule.fraction == 253218);
	CHECK(memcmp(&f.music, &before, sizeof(before)) == 0); /* No protocol arm/delay stores. */
	f.music.divider = 65535;
	CHECK(st_big_music_schedule_arm(&f.schedule, &f.music, 65535) == 0 && f.schedule.remaining == 158737255
	      && f.schedule.fraction == 864345);
	CHECK(st_big_music_schedule_rate(&f.schedule, &f.music, 0) == 0 && f.schedule.remaining == 158739677
	      && f.schedule.fraction == 1073463);
	CHECK(st_big_music_schedule_rate(&f.schedule, &f.music, 65535) == 0 && f.schedule.remaining == 158737255
	      && f.schedule.fraction == 864345);
	struct st_big_music_schedule old;
	memcpy(&old, &f.schedule, sizeof(old));
	CHECK(st_big_music_schedule_consume(&f.schedule, 158737256) == IO_ERROR_OUT_OF_RANGE
	      && memcmp(&f.schedule, &old, sizeof(old)) == 0);
	CHECK(st_big_music_schedule_consume(&f.schedule, 100000000) == 0 && f.schedule.remaining == 58737255);
	/* Change rate midway through a local duration, preserving elapsed time
	 * and leaving the protocol/header/countdown request fields untouched. */
	init(&f, NULL, 0, 100);
	CHECK(st_big_music_schedule_consume(&f.schedule, 45) == 0);
	CHECK(st_big_music_schedule_rate(&f.schedule, &f.music, 0) == 0 && f.schedule.remaining == 1235
	      && f.schedule.fraction == 529306);
	CHECK(f.music.cached_divider == 2485 && f.music.delay == 1 && f.music.position_low == 2
	      && f.music.running == 1);
	CHECK(st_big_music_schedule_arm(&f.schedule, &f.music, 1) == 0 && f.schedule.remaining == 2422
	      && f.schedule.fraction == 253218);
	/* The record operator also reads a freshly changed divider when it
	 * requests its next span, independently of the old cached header. */
	static const uint8_t bytes[] = {0xb5, 9, 0xff, 4, 0, 0x20, 0x23, 1, 0xff, 3, 0};
	init(&f, bytes, sizeof(bytes), 100);
	f.music.divider = 0;
	CHECK(f.schedule.remaining == 91 && f.music.cached_divider == 2485);
	run_to(&f, 91, 1, 64);
	CHECK(f.count == 0 && f.schedule.remaining == 9689 && f.schedule.fraction == 828720);
	st_big_music_schedule_cancel(&f.schedule);
	CHECK(!f.schedule.armed && f.music.running == 1 && f.music.delay == 4);
	CHECK(st_big_music_schedule_consume(&f.schedule, UINT64_MAX) == 0);
	CHECK(st_big_music_schedule_arm(&f.schedule, &f.music, 0) == 0 && f.schedule.remaining == 0
	      && f.schedule.fraction == 0);
	CHECK(st_big_music_schedule_arm(NULL, &f.music, 1) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_music_schedule_arm(&f.schedule, NULL, 1) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_music_schedule_rate(NULL, &f.music, 1) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_music_schedule_rate(&f.schedule, NULL, 1) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_music_schedule_consume(NULL, 0) == IO_ERROR_INVALID_ARGUMENT);
	st_big_music_schedule_cancel(NULL);
	st_big_music_schedule_cancel(&f.schedule);
	CHECK(st_big_music_schedule_rate(&f.schedule, &f.music, 1) == 0 && !f.schedule.armed
	      && f.music.divider == 1);
	/* Subsample requests accumulate on the native grid; no artificial
	 * one-sample minimum is imposed on each positive callback delay. */
	f.music.running = 2;
	f.music.delay   = 1;
	CHECK(st_big_music_schedule_arm(&f.schedule, &f.music, 0) == 0);
	CHECK(poll(&f, 100) == 0 && f.schedule.remaining == 1 && f.schedule.fraction == 41619);
	/* Invalid internal durations are rejected before either multiply and
	 * preserve both owners; every legitimate WORD duration fits. */
	f.schedule.remaining = UINT64_MAX;
	memcpy(&old, &f.schedule, sizeof(old));
	memcpy(&before, &f.music, sizeof(before));
	CHECK(st_big_music_schedule_rate(&f.schedule, &f.music, 0) == IO_ERROR_OUT_OF_RANGE);
	CHECK(memcmp(&f.schedule, &old, sizeof(old)) == 0 && memcmp(&f.music, &before, sizeof(before)) == 0);
	f.schedule.remaining = UINT64_MAX / UINT32_C(1193181);
	f.schedule.fraction  = 0;
	memcpy(&old, &f.schedule, sizeof(old));
	CHECK(st_big_music_schedule_rate(&f.schedule, &f.music, 0) == IO_ERROR_OUT_OF_RANGE);
	CHECK(memcmp(&f.schedule, &old, sizeof(old)) == 0 && memcmp(&f.music, &before, sizeof(before)) == 0);
}

static void
zero_delays_live_bytes_and_errors(void)
{
	static const uint8_t bytes[] = {0, 0, 0xff, 0, 0, 0x20, 0x23, 1, 0xff, 5, 0, 0xff, 0, 0};
	struct fixture       f;
	init(&f, bytes, sizeof(bytes), 100);
	CHECK(st_big_music_schedule_arm(&f.schedule, &f.music, 0) == 0);
	f.blocked = true;
	CHECK(poll(&f, 100) == IO_ERROR_IN_PROGRESS && f.count == 0 && f.schedule.remaining == 0);
	CHECK(st_big_music_schedule_rate(&f.schedule, &f.music, 0) == 0 && f.schedule.remaining == 0
	      && f.schedule.pending);
	CHECK(st_big_music_schedule_rate(&f.schedule, &f.music, 2485) == 0 && f.schedule.remaining == 0
	      && f.schedule.pending);
	f.blocked = false;
	/* Stores are shared immediately, independently of the loaded length. */
	CHECK(st_big_music_buffer_store(&f.buffer, 6, 0x67) == 0);
	f.music.length = 2;
	CHECK(poll(&f, 100) == 0 && f.count == 1 && f.writes[0].frame == 0 && f.writes[0].value == 0x167);
	CHECK(f.music.position_low == 11 && f.music.running == 1 && f.schedule.remaining == 459);
	f.music.running = 2;
	f.music.delay   = 0;
	CHECK(st_big_music_schedule_arm(&f.schedule, &f.music, 0) == 0);
	unsigned reads = f.reads;
	CHECK(poll(&f, 100) == IO_ERROR_IN_PROGRESS && f.reads == reads && f.schedule.remaining == 0);
	f.music.running = 0;
	CHECK(poll(&f, 100) == 0 && !f.schedule.armed && f.music.delay == 1);
	init(&f, NULL, 0, 100);
	f.music.position_low  = 65533;
	f.buffer.bytes[65533] = 0xff;
	CHECK(st_big_music_schedule_arm(&f.schedule, &f.music, 0) == 0 && poll(&f, 100) == IO_ERROR_OUT_OF_RANGE);
	CHECK(f.music.position_low == 65533 && f.music.address == 255 && f.music.value_offset == 65534);
	reads = f.reads;
	CHECK(poll(&f, 100) == IO_ERROR_OUT_OF_RANGE && f.reads == reads);
	st_big_music_schedule_cancel(&f.schedule);
	CHECK(poll(&f, 0) == 0 && f.reads == reads);
	CHECK(st_big_music_schedule_poll(NULL, &f.music, NULL, NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_music_schedule_poll(&f.schedule, NULL, NULL, NULL) == IO_ERROR_INVALID_ARGUMENT);
}

static void
no_marker_and_x7(void)
{
	struct fixture f;
	init(&f, NULL, 0, 3);
	CHECK(st_big_music_schedule_arm(&f.schedule, &f.music, 0) == 0);
	for (unsigned i = 0; i < 30; ++i)
		CHECK(poll(&f, 1) == IO_ERROR_IN_PROGRESS);
	CHECK(f.music.running == 0 && f.schedule.armed && f.schedule.pending && f.schedule.remaining == 0
	      && f.count == 3);
	CHECK(f.music.position_low == 11); /* No ff means no callback return yet. */
	init(&f, NULL, 0, 3);
	CHECK(st_big_music_schedule_arm(&f.schedule, &f.music, 0) == 0);
	for (unsigned i = 0; i < 8; ++i)
		CHECK(st_big_music_buffer_store(&f.buffer, (uint16_t)(2 + 3 * i), 0xff) == 0);
	/* A long zero-delay sequence makes progress over bounded polls without
	 * an invented sample, a total retry cap or a synthetic terminator. */
	f.music.end_low = 100;
	size_t polls    = 0;
	while (f.schedule.remaining == 0 && f.music.position_low < 26) {
		CHECK(poll(&f, 1) == IO_ERROR_IN_PROGRESS && ++polls < 200);
	}
	CHECK(f.music.position_low == 26 && f.frame == 0);
	/* Current state can end an in-flight nonff run only after its real ff.
	 * Move to a known valid run with an ff in retained storage. */
	st_big_music_schedule_cancel(&f.schedule);
	f.music.position_low = 2;
	f.music.end_low      = 3;
	f.buffer.bytes[2]    = 0x20;
	f.buffer.bytes[3]    = 0x23;
	f.buffer.bytes[4]    = 1;
	f.buffer.bytes[5]    = 0x21;
	f.buffer.bytes[6]    = 0x56;
	f.buffer.bytes[7]    = 0;
	f.buffer.bytes[8]    = 0xff;
	f.buffer.bytes[9]    = 0;
	f.buffer.bytes[10]   = 0;
	f.music.running      = 1;
	CHECK(st_big_music_schedule_arm(&f.schedule, &f.music, 0) == 0);
	while (f.music.running != 0)
		CHECK(poll(&f, 1) == IO_ERROR_IN_PROGRESS);
	CHECK(f.count == 1 && f.music.position_low == 5 && f.schedule.pending && f.schedule.armed && f.frame == 0);
	while (f.schedule.armed) {
		int status = poll(&f, 1);
		CHECK(status == 0 || status == IO_ERROR_IN_PROGRESS);
	}
	CHECK(f.count == 2 && f.music.position_low == 11 && f.music.running == 0 && !f.schedule.armed);
	struct st_big_control control  = {0};
	control.music                  = f.music;
	control.music.running          = 1;
	control.music.sync             = 1;
	control.music.position_low     = 2;
	struct st_big_sync_reader sync = {0};
	st_big_sync_begin(&sync, &control);
	CHECK(st_big_sync_poll(&sync, &control) == IO_ERROR_IN_PROGRESS);
	control.music = f.music;
	CHECK(st_big_sync_poll(&sync, &control) == 0);
}

int
main(void)
{
	bounds_and_retention();
	literal_callback_times();
	conversion_and_controls();
	zero_delays_live_bytes_and_errors();
	no_marker_and_x7();
	return 0;
}
