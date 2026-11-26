/* Musicomm receiver: literal serial/MIDI expectations, no physical output. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io_error.h"
#include "music_internal.h"
#include "st_musicomm_internal.h"
#include "st_time.h"

#define CHECK(e)                                                   \
	do {                                                       \
		if (!(e)) {                                        \
			fprintf(stderr, "%d: %s\n", __LINE__, #e); \
			exit(1);                                   \
		}                                                  \
	} while (0)

struct recording {
	uint8_t  bytes[131072];
	size_t   size, cached;
	unsigned bank_msb, bank_lsb, program, pan, pitch;
	unsigned modulation, expression, sustain, resets;
	bool     held;
	int16_t  tail;
};
static struct recording replies;

static int
send_midi(void *context, const uint8_t *bytes, size_t size)
{
	struct recording *r = context;
	if (size == 6 && memcmp(bytes, "\xf0\x7e\x7f\x09\x01\xf7", 6) == 0) {
		++r->resets;
		r->held       = false;
		r->tail       = 0;
		r->cached     = 64;
		r->pan        = 64;
		r->pitch      = 8192;
		r->modulation = 0;
		r->expression = 127;
		r->sustain    = 0;
	}
	if (size == 3 && bytes[0] == 0xe0)
		r->pitch = bytes[1] + 128u * bytes[2];
	if (size == 3 && bytes[0] == 0xb0) {
		if (bytes[1] == 10)
			r->pan = bytes[2];
		if (bytes[1] == 1)
			r->modulation = bytes[2];
		if (bytes[1] == 11)
			r->expression = bytes[2];
		if (bytes[1] == 64)
			r->sustain = bytes[2];
		if (bytes[1] == 120 || bytes[1] == 123)
			r->held = false;
		if (bytes[1] == 0)
			r->bank_msb = bytes[2];
		if (bytes[1] == 32)
			r->bank_lsb = bytes[2];
	}
	if (size == 2 && bytes[0] == 0xc0)
		r->program = bytes[1];
	if (size == 3 && bytes[0] == 0x90 && bytes[2] != 0) {
		r->held = true;
		r->tail = 1000;
		CHECK(r->bank_msb == 0 && r->bank_lsb == 0 && r->program == 80 && r->resets != 0 && r->pan == 64
		      && r->pitch == 8192 && r->modulation == 0 && r->expression == 127 && r->sustain == 0);
	}
	if (size == 3 && bytes[0] == 0x90 && bytes[2] == 0)
		r->held = false;
	CHECK(size <= sizeof(r->bytes) - r->size);
	memcpy(r->bytes + r->size, bytes, size);
	r->size += size;
	return 0;
}

static void
reply(void *context, const uint8_t *bytes, size_t size)
{
	(void)context;
	(void)send_midi(&replies, bytes, size);
}

static void
expect_reply(const char *bytes, size_t size)
{
	CHECK(replies.size == size && memcmp(replies.bytes, bytes, size) == 0);
	replies.size = 0;
}

static void
exercise(bool stop_runtime, bool midi)
{
	struct st_musicomm *receiver = NULL;
	st_audio_runtime_t *audio;
	st_music_t         *player = NULL;
	audio_stream_t     *stream = NULL;
	CHECK(st_audio_runtime_create_pull(&audio) == 0);
	st_audio_runtime_set_midi_allowed(audio, 0);
	struct recording r = {.bank_msb = 1,
	    .bank_lsb                   = 2,
	    .program                    = 81,
	    .pan                        = 0,
	    .pitch                      = 0,
	    .modulation                 = 127,
	    .expression                 = 13,
	    .sustain                    = 127};
	if (midi) {
		const struct st_midi_sink sink = {.state = &r, .send = send_midi};
		CHECK(st_music_create_with_sink(&player, audio, &stream, &sink) == 0);
	}
	else
		CHECK(st_music_create(&player, audio, &stream) == 0);
	const struct st_musicomm_config config = {.audio = audio, .player = player, .reply = reply};
	CHECK(st_musicomm_start(&receiver, &config) == 0);
	CHECK(st_musicomm_start(&receiver, &config) == IO_ERROR_BUSY);
	CHECK(st_musicomm_state(receiver) == ST_MUSICOMM_WAITING);
	st_musicomm_feed(receiver, "\xff\r\n\0", 4);
	expect_reply("", 0);
	/* Queued startup bytes, even in separate writes, are drained. */
	st_musicomm_feed(receiver, "\x02\x3c", 2);
	expect_reply("\x02", 1);
	st_musicomm_feed(receiver, "\x40", 1);
	CHECK(st_musicomm_state(receiver) == ST_MUSICOMM_START_DRAIN);
	st_musicomm_idle(receiver);
	CHECK(st_musicomm_state(receiver) == ST_MUSICOMM_PLAYING);
	const uint8_t notes[] = {60, 64, 60, 64, 0, 0, 2, 2, 10, 10, 13, 13, 17, 17, 19, 19, 127, 127};
	for (size_t split = 0; split <= sizeof(notes); ++split) {
		st_musicomm_feed(receiver, notes, split);
		st_musicomm_feed(receiver, notes + split, sizeof(notes) - split);
		/* The sender owns timing; don't saturate the bounded worker FIFO
		 * while replaying every split in this one artificial burst. */
		if (midi)
			CHECK(st_music_wait(player) == 0);
	}
	CHECK(st_music_wait(player) == 0);
	if (midi) {
		CHECK(receiver->midi && !receiver->physical && stream == NULL);
		/* Initial channel gain controllers are sink policy; find the first
		 * note, then require every event's exact status, pitch and velocity. */
		size_t first = 0;
		while (first < r.size && r.bytes[first] != 0x90)
			++first;
		CHECK(first >= 8);
		/* Literal bank-zero / Lead 1 (square) selection precedes the first
		 * note, replacing the prior ANSI calibration voice. */
		const uint8_t instrument[] = {0xb0, 0, 0, 0xb0, 32, 0, 0xc0, 80};
		CHECK(memcmp(r.bytes + first - sizeof(instrument), instrument, sizeof(instrument)) == 0);
		CHECK(r.size - first == (sizeof(notes) + 1) * sizeof(notes) * 3);
		for (size_t i = 0; i < (sizeof(notes) + 1) * sizeof(notes); ++i) {
			size_t n = i % sizeof(notes);
			CHECK(r.bytes[first + 3 * i] == 0x90 && r.bytes[first + 3 * i + 1] == notes[n]);
			CHECK(
			    r.bytes[first + 3 * i + 2] == ((n == 0 || n == 1 || (n >= 4 && n % 2 == 0)) ? 64 : 0));
		}
	}
	else {
		CHECK(!receiver->midi);
		st_musicomm_feed(receiver, "\x3c", 1);
		int16_t pcm[2048];
		CHECK(st_audio_runtime_pull(audio, pcm, 1024) == 0);
		bool nonzero = false;
		for (size_t i = 0; i < 2048; ++i)
			if (pcm[i] != 0)
				nonzero = true;
		CHECK(nonzero);
	}
	size_t before_end = r.size;
	st_musicomm_feed(receiver, "\x80LOST", 5);
	expect_reply("\xff\xff\xff", 3);
	if (midi) {
		while (before_end < r.size && r.bytes[before_end] != 0x90)
			++before_end;
		CHECK(r.size >= before_end + 384);
		CHECK(r.resets >= 2);
		for (unsigned n = 0; n < 128; ++n) {
			CHECK(r.bytes[before_end + 3 * n] == 0x90);
			CHECK(r.bytes[before_end + 3 * n + 1] == n && r.bytes[before_end + 3 * n + 2] == 0);
		}
	}
	st_musicomm_feed(receiver, "LOST", 4);
	st_musicomm_idle(receiver);
	CHECK(st_musicomm_state(receiver) == ST_MUSICOMM_OFF);
	CHECK(!st_music_busy(player));
	for (unsigned end = 128; end < 256; ++end) {
		CHECK(st_musicomm_start(&receiver, &config) == 0);
		st_musicomm_feed(receiver, "\x02", 1);
		st_musicomm_idle(receiver);
		uint8_t byte = (uint8_t)end;
		st_musicomm_feed(receiver, &byte, 1);
		expect_reply("\x02\xff\xff\xff", 4);
		st_musicomm_idle(receiver);
		r.size = 0;
	}
	if (!midi) {
		CHECK(st_musicomm_start(&receiver, &config) == 0);
		st_musicomm_feed(receiver, "\x02", 1);
		st_musicomm_idle(receiver);
		expect_reply("\x02", 1);
		const uint8_t chord[] = {0, 24, 108, 127};
		st_musicomm_feed(receiver, chord, sizeof(chord));
		struct st_musicomm *m = receiver;
		CHECK(m->on[0] && m->on[24] && m->on[108] && m->on[127]);
		CHECK(m->channel[0] == 0 && m->channel[24] == 1 && m->channel[108] == 2 && m->channel[127] == 2);
		/* Literal periods from the accessory's $2652 table. Note 127 folds
		 * to 103; note 0 folds to 24, keeping separate toggle flags. */
		CHECK(m->gist.voice[0].parameters[1] == 3822 && m->gist.voice[1].parameters[1] == 3822);
		CHECK(m->gist.voice[2].parameters[1] == 40);
		/* Release the stolen note: the original channel association releases
		 * its replacement while the replacement's logical flag stays set. */
		st_musicomm_feed(receiver, "\x6c", 1);
		CHECK(!m->on[108] && m->on[127] && m->gist.voice[2].control == -1);
		int16_t pcm[882 * 2];
		for (unsigned i = 0; i < 100; ++i)
			CHECK(st_audio_runtime_pull(audio, pcm, 882) == 0);
		/* Held pitches inhibit the preset countdown in the actual binary. */
		CHECK(m->gist.voice[0].parameters[0] == 200 && m->gist.voice[1].parameters[0] == 200);
		CHECK(m->gist.voice[2].parameters[0] == 0);
		CHECK(st_musicomm_stop(receiver));
		expect_reply("\xff\xff\xff", 3);
		st_musicomm_idle(receiver);
	}
	/* A local abort also completes while waiting for the handshake. */
	CHECK(st_musicomm_start(&receiver, &config) == 0);
	CHECK(st_musicomm_stop(receiver));
	expect_reply("\xff\xff\xff", 3);
	st_musicomm_idle(receiver);
	CHECK(st_musicomm_start(&receiver, &config) == 0);
	st_musicomm_feed(receiver, "\x02", 1);
	st_musicomm_idle(receiver);
	expect_reply("\x02", 1);
	st_musicomm_feed(receiver, "\x3c", 1);
	st_musicomm_cancel(receiver);
	CHECK(!st_music_busy(player));
	CHECK(st_musicomm_state(receiver) == ST_MUSICOMM_OFF);
	CHECK(st_musicomm_start(&receiver, &config) == 0);
	st_musicomm_feed(receiver, "\x02", 1);
	st_musicomm_idle(receiver);
	expect_reply("\x02", 1);
	st_musicomm_feed(receiver, "\x3c", 1);
	st_musicomm_cancel(receiver);
	CHECK(!st_music_busy(player));
	CHECK(st_musicomm_state(receiver) == ST_MUSICOMM_OFF);
	CHECK(st_musicomm_start(&receiver, &config) == 0);
	st_musicomm_feed(receiver, "\x02", 1);
	st_musicomm_idle(receiver);
	expect_reply("\x02", 1);
	st_musicomm_feed(receiver, "\x3c", 1);
	if (stop_runtime)
		st_audio_runtime_stop(audio);
	st_musicomm_destroy(&receiver);
	st_music_destroy(player);
	audio_stream_destroy(stream);
	st_audio_runtime_destroy(audio);
}

static int
render_midi(void *context, int16_t *pcm, size_t frames)
{
	struct recording *r = context;
	for (size_t i = 0; i < frames; ++i) {
		int16_t value = r->cached != 0 ? 7000 : r->held ? 10000 : r->tail;
		if (r->cached != 0)
			--r->cached;
		pcm[2 * i] = pcm[2 * i + 1] = value;
	}
	return 0;
}

struct consumer {
	st_audio_runtime_t *audio;
	atomic_bool         stop;
};

static int
consume(void *context)
{
	struct consumer *c = context;
	while (!atomic_load(&c->stop)) {
		int16_t pcm[512 * AUDIO_CHANNELS];
		CHECK(st_audio_runtime_pull(c->audio, pcm, 512) == 0);
		const struct timespec pause = {0, 1000000};
		(void)st_thread_sleep(&pause, NULL);
	}
	return 0;
}

static void
software_session(void)
{
	struct st_musicomm *receiver = NULL;
	st_audio_runtime_t *audio;
	st_music_t         *player = NULL;
	audio_stream_t     *stream = NULL;
	CHECK(st_audio_runtime_create_pull(&audio) == 0);
	struct recording          r = {.pan = 0, .pitch = 16383, .modulation = 127, .sustain = 127, .tail = 1000};
	const struct st_midi_sink sink = {.state = &r, .send = send_midi, .render = render_midi};
	CHECK(st_music_create_with_sink(&player, audio, &stream, &sink) == 0);
	const struct st_musicomm_config config = {.audio = audio, .player = player, .reply = reply};
	/* This private pull consumer supplies completion acknowledgments while
	 * the serialized terminal owner performs its blocking abort/reset. It
	 * owns no terminal state; stop and join it before destroying audio. */
	struct consumer c = {.audio = audio};
	atomic_init(&c.stop, false);
	st_thread_t thread;
	CHECK(st_thread_create(&thread, consume, &c) == ST_THREAD_SUCCESS);
	for (unsigned pass = 0; pass < 5; ++pass) {
		CHECK(st_music_volume(player, -3, -18, 0) == 0);
		CHECK(st_musicomm_start(&receiver, &config) == 0);
		float left, right;
		CHECK(st_music_get_volume(player, &left, &right) == 0 && left == -12 && right == -12);
		st_musicomm_feed(receiver, "\x02", 1);
		st_musicomm_idle(receiver);
		expect_reply("\x02", 1);
		st_musicomm_feed(receiver, "\x3c\x40\x43", 3);
		CHECK(st_music_wait(player) == 0 && st_music_busy(player));
		if (pass < 3) {
			CHECK(st_musicomm_stop(receiver));
			expect_reply("\xff\xff\xff", 3);
			st_musicomm_idle(receiver);
		}
		else
			st_musicomm_cancel(receiver);
		CHECK(st_musicomm_state(receiver) == ST_MUSICOMM_OFF);
		CHECK(!st_music_busy(player) && audio_stream_buffered_frames(stream) == 0);
		CHECK(!r.held && r.tail == 0 && r.cached == 0);
		int16_t pcm[1024 * AUDIO_CHANNELS];
		CHECK(st_audio_runtime_pull(audio, pcm, 1024) == 0);
		for (size_t i = 0; i < 1024 * AUDIO_CHANNELS; ++i)
			CHECK(pcm[i] == 0);
	}
	CHECK(st_musicomm_start(&receiver, &config) == 0);
	st_musicomm_feed(receiver, "\x02", 1);
	st_musicomm_idle(receiver);
	expect_reply("\x02", 1);
	st_musicomm_feed(receiver, "\x3c", 1);
	st_musicomm_destroy(&receiver);
	st_music_destroy(player);
	audio_stream_destroy(stream);
	CHECK(!r.held && r.tail == 0 && r.cached == 0);
	atomic_store(&c.stop, true);
	CHECK(st_thread_join(thread, NULL) == ST_THREAD_SUCCESS);
	st_audio_runtime_destroy(audio);
}

static void
unavailable(void)
{
	for (unsigned mode = 0; mode < 3; ++mode) {
		struct st_musicomm *receiver = NULL;
		st_audio_runtime_t *audio    = NULL;
		if (mode != 0)
			CHECK(st_audio_runtime_create(&audio, 0) == 0);
		if (mode == 2)
			st_audio_runtime_stop(audio);
		const struct st_musicomm_config config = {.audio = audio, .reply = reply};
		CHECK(st_musicomm_start(&receiver, &config) == 0);
		st_musicomm_feed(receiver, "\x02", 1);
		st_musicomm_idle(receiver);
		expect_reply("\x02", 1);
		st_musicomm_feed(receiver, "\x3c", 1);
		CHECK(receiver->on[60] && !receiver->physical && !receiver->midi);
		/* Local muted time advances envelopes without opening an output. */
		receiver->local_ms -= 2000;
		st_musicomm_poll(receiver);
		CHECK(receiver->gist.voice[0].parameters[0] == 200);
		CHECK(receiver->gist.voice[0].parameters[4] == 3);
		CHECK(st_musicomm_stop(receiver));
		CHECK(st_musicomm_stop(receiver));
		expect_reply("\xff\xff\xff", 3);
		st_musicomm_idle(receiver);
		st_musicomm_destroy(&receiver);
		st_audio_runtime_destroy(audio);
	}
}

int
st_musicomm_test_main(void)
{
	for (unsigned stopped = 0; stopped < 2; ++stopped)
		for (unsigned midi = 0; midi < 2; ++midi)
			exercise(stopped, midi);
	unavailable();
	software_session();
	return 0;
}
