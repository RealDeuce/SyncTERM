/* Terminal waits must leave damage flushing and blinking independent. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm.h"
#include "cterm_apc.h"
#include "console_internal.h"
#include "audio_internal.h"
#include "st_threads.h"
#include "st_time.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum operation { ANSI_MUSIC, APC_MUSIC, APC_FX, APC_CHANNEL, APC_LAST_CHANNEL,
	BELL, RESPONSE, APC_CALLBACK, OPERATION_COUNT };

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); exit(1); } } while (0)

struct presentation {
	struct st_console_bitmap_presenter *bitmap;
	st_mutex_t mutex;
	st_condition_t changed;
	unsigned cursor_phases;
	bool damage;
	bool entered, release;
};

static void
wait_callback(struct presentation *p)
{
	CHECK(st_mutex_lock(&p->mutex) == ST_THREAD_SUCCESS);
	p->entered = true;
	CHECK(st_condition_signal(&p->changed) == ST_THREAD_SUCCESS);
	while (!p->release)
		CHECK(st_condition_wait(&p->changed, &p->mutex) == ST_THREAD_SUCCESS);
	CHECK(st_mutex_unlock(&p->mutex) == ST_THREAD_SUCCESS);
}

static void
response(void *context, enum st_cterm_output_origin origin, const uint8_t *bytes, size_t size)
{
	CHECK(origin == ST_CTERM_OUTPUT_RESPONSE && size == 4 && memcmp(bytes, "\x1b[0n", 4) == 0);
	wait_callback(context);
}

static void
apc(void *context, const char *bytes, size_t size)
{
	CHECK(size == 4 && memcmp(bytes, "wait", 4) == 0);
	wait_callback(context);
}

static int
present(st_console_t *console)
{
	struct presentation *p = console->display->backend_state;
	struct st_console_bitmap_frame frame;
	uint64_t now;
	CHECK(st_time_monotonic_ms(&now) == 0);
	CHECK(st_console_bitmap_presenter_frame(p->bitmap, console, 640, 400,
	    (uint32_t)now, NULL, NULL, &frame) == 0);
	/* One frame supplies both the cursor pixel and independent pixel damage.
	 * The parser is paused after A, with its underline at column two. */
	CHECK(st_mutex_lock(&p->mutex) == ST_THREAD_SUCCESS);
	if (console->cursor_x == 1 && console->cursor_y == 0) {
		uint32_t cursor = frame.pixels->pixels[14 * 640 + 8];
		CHECK(cursor == 0 || cursor == 0xa8a8a8);
		p->cursor_phases |= cursor == 0 ? 1U : 2U;
	}
	if (frame.pixels->pixels[399 * 640 + 639] == 0x010203)
		p->damage = true;
	CHECK(st_condition_signal(&p->changed) == ST_THREAD_SUCCESS);
	CHECK(st_mutex_unlock(&p->mutex) == ST_THREAD_SUCCESS);
	return 0;
}

static const struct st_console_backend_ops bitmap_ops = {
	.presents_pixels = true, .deferred_present = true,
	.animated_present = true, .present = present,
};

struct writer {
	st_cterm_t *terminal;
	atomic_bool done;
	int status;
	size_t consumed;
	const char *wire;
};

static int
write_waiting_command(void *context)
{
	struct writer *w = context;
	/* Following text must remain unconsumed until the operation completes. */
	w->status = st_cterm_write(w->terminal, w->wire, strlen(w->wire), &w->consumed);
	atomic_store(&w->done, true);
	return 0;
}

static void
feed(st_cterm_t *terminal, const char *text)
{
	/* Retain bytewise coverage of introducer, modes and capture. */
	for (size_t i = 0; i < strlen(text); ++i) {
		size_t used;
		CHECK(st_cterm_write(terminal, text + i, 1, &used) == 0 && used == 1);
	}
}

static uint64_t
now_ms(void)
{
	uint64_t now;
	CHECK(st_time_monotonic_ms(&now) == 0);
	return now;
}

static void
pause_test(void)
{
	const struct timespec delay = {0, 2000000};
	(void)st_thread_sleep(&delay, NULL);
}

static void
exercise(enum st_cterm_encoding encoding, enum operation operation, unsigned completion)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {
		.columns = 80, .rows = 25, .cell_width = 8, .cell_height = 16,
	};
	st_console_t *console;
	st_font_catalog_t *fonts;
	st_audio_runtime_t *audio;
	audio_engine_t *engine;
	audio_stream_t *stream = NULL;
	st_cterm_t *terminal;
	struct presentation p = {0};
	st_thread_t parser;
	struct timespec deadline;
	static const char *const wires[] = {
		"\x0eZ",
		"\x1b[|MBT120P1\x0e\x1b_SyncTERM:A;Wait;C=0\x1b\\Z",
		"\x1b_SyncTERM:A;Wait;C=1\x1b\\Z",
		"\x1b_SyncTERM:A;Wait;C=2\x1b\\Z",
		"\x1b_SyncTERM:A;Wait;C=15\x1b\\Z",
		"\x07Z", "\x1b[5nZ", "\x1b_wait\x1b\\Z",
	};
	fprintf(stderr, "encoding=%d operation=%d completion=%u\n", (int)encoding, (int)operation, completion);

	CHECK(st_console_create(&console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	/* Pull-only audio and a rest cannot reach physical audio or MIDI. */
	CHECK(st_audio_runtime_create_pull(&audio) == 0);
	const struct st_cterm_config config = {
		.console = console, .fonts = fonts, .audio = audio,
		.emulation = ST_CTERM_ANSI_BBS, .encoding = encoding,
		.codepage = ST_CODEPAGE_CP437,
		.output = response, .output_context = &p,
		.apc = apc, .apc_context = &p,
		.apc_directory = ST_APC_TEST_DIRECTORY,
	};
	CHECK(st_cterm_create(&terminal, &config) == 0);
	CHECK(st_music_create(&terminal->music_player, audio, &terminal->music_stream) == 0);
	feed(terminal, "\x1b[2J\x1b[H\x1b[3 qA");
	if (operation == ANSI_MUSIC)
		feed(terminal, "\x1b[|MFT120P1");
	CHECK(st_audio_runtime_acquire(audio, &engine) == 0);
	if (operation >= APC_FX && operation <= APC_LAST_CHANNEL) {
		audio_stream_t **slot = operation == APC_FX ? &terminal->fx_stream
		    : &terminal->apc->channels[operation == APC_CHANNEL ? 0 : 13];
		CHECK(audio_stream_create(slot, engine, -12, -12) == 0);
		stream = *slot;
		int16_t *silence = calloc(AUDIO_SAMPLE_RATE, AUDIO_FRAME_SIZE);
		CHECK(silence != NULL);
		CHECK(audio_stream_append_take(stream, silence, AUDIO_SAMPLE_RATE, NULL) == 0);
	}
	CHECK(st_mutex_init(&p.mutex, ST_MUTEX_PLAIN) == ST_THREAD_SUCCESS);
	CHECK(st_condition_init(&p.changed) == ST_THREAD_SUCCESS);
	CHECK(st_console_bitmap_presenter_create(&p.bitmap, console) == 0);
	console->display->backend_ops = &bitmap_ops;
	console->display->backend_state = &p;
	CHECK(st_console_start_presentation_worker(console) == 0);
	struct writer w = {.terminal = terminal, .wire = wires[operation]};
	atomic_init(&w.done, false);
	CHECK(st_thread_create(&parser, write_waiting_command, &w) == ST_THREAD_SUCCESS);
	uint64_t limit = now_ms() + 5000;
	bool ready = false;
	while (!ready) {
		if (operation == BELL)
			ready = audio_engine_stream_count_for_test(engine) != 0;
		else if (operation >= RESPONSE) {
			CHECK(st_mutex_lock(&p.mutex) == ST_THREAD_SUCCESS);
			ready = p.entered;
			CHECK(st_mutex_unlock(&p.mutex) == ST_THREAD_SUCCESS);
		}
		else
			ready = audio_stream_buffered_frames(operation >= APC_FX ? stream
			    : st_music_pcm_stream(terminal->music_player)) != 0;
		CHECK(!atomic_load(&w.done) && now_ms() < limit);
		pause_test();
	}
	CHECK(!atomic_load(&w.done));
	/* This damage is generated after the parser has entered its wait. The
	 * presentation worker must flush it and continue animating the cursor. */
	CHECK(st_mutex_lock(&p.mutex) == ST_THREAD_SUCCESS);
	p.cursor_phases = 0;
	p.damage = false;
	CHECK(st_mutex_unlock(&p.mutex) == ST_THREAD_SUCCESS);
	CHECK(st_console_set_pixel(console, 639, 399, ST_PIXEL_COLOR_RGB_VALUE(1, 2, 3)) == 0);
	CHECK(st_time_utc_deadline_after_ms(&deadline, 3000) == 0);
	CHECK(st_mutex_lock(&p.mutex) == ST_THREAD_SUCCESS);
	while (!p.damage || p.cursor_phases != 3)
		CHECK(st_condition_timedwait(&p.changed, &p.mutex, &deadline) == ST_THREAD_SUCCESS);
	CHECK(st_mutex_unlock(&p.mutex) == ST_THREAD_SUCCESS);
	CHECK(!atomic_load(&w.done));
	CHECK(st_rwlock_read_lock(&console->state_lock) == ST_THREAD_SUCCESS);
	CHECK(console->update_depth == 0 && console->cursor_x == 1);
	CHECK(st_rwlock_unlock(&console->state_lock) == ST_THREAD_SUCCESS);

	if (operation >= RESPONSE) {
		CHECK(st_mutex_lock(&p.mutex) == ST_THREAD_SUCCESS);
		p.release = true;
		CHECK(st_condition_signal(&p.changed) == ST_THREAD_SUCCESS);
		CHECK(st_mutex_unlock(&p.mutex) == ST_THREAD_SUCCESS);
	}
	else if (completion == 1) {
		if (operation >= APC_FX)
			CHECK(audio_stream_clear(stream) == 0);
		else
			CHECK(st_music_flush(terminal->music_player, 0) == 0);
	}
	else if (completion == 2)
		st_audio_runtime_stop(audio);
	limit = now_ms() + 5000;
	while (!atomic_load(&w.done)) {
		int16_t frames[1024 * AUDIO_CHANNELS];
		if (completion == 0)
			CHECK(st_audio_runtime_pull(audio, frames, 1024) == 0);
		CHECK(now_ms() < limit);
		pause_test();
	}
	CHECK(st_thread_join(parser, NULL) == ST_THREAD_SUCCESS);
	CHECK(w.status == 0 && w.consumed == strlen(w.wire));
	CHECK(console->cursor_x == 2 && console->update_depth == 0);
	CHECK(st_cterm_reset_terminal(terminal) == 0);
	CHECK(console->update_depth == 0);
	feed(terminal, "R");
	CHECK(console->cursor_x == 1);
	st_cterm_destroy(&terminal);
	st_audio_runtime_destroy(audio);
	st_font_catalog_destroy(fonts);
	/* Console destruction joins the presentation worker before its state. */
	st_console_destroy(console);
	st_console_bitmap_presenter_destroy(p.bitmap);
	st_condition_destroy(&p.changed);
	st_mutex_destroy(&p.mutex);
}

int
st_cterm_music_render_test_main(void)
{
	for (unsigned utf8 = 0; utf8 < 2; ++utf8)
		for (enum operation operation = ANSI_MUSIC; operation < OPERATION_COUNT; ++operation)
			for (unsigned completion = 0; completion < (operation >= BELL ? 1U : 3U); ++completion)
				exercise(utf8 ? ST_CTERM_ENCODING_UTF8 : ST_CTERM_ENCODING_CODEPAGE, operation, completion);
	return 0;
}
