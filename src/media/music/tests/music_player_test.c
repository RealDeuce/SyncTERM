/* Deterministic sinks: no MIDI device or physical audio output is opened. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "music_internal.h"
#include "st_threads.h"
#include "st_time.h"
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%d: %s\n", __LINE__, #e); exit(1); } } while (0)
struct recording {
	st_mutex_t mutex;
	size_t frames, events, on, off, sysex;
	uint8_t keys[256];
	uint64_t positions[256];
	bool sounding, fail, render_fail;
	float left, right;
};
static int send(void *context, const uint8_t *b, size_t size)
{
	struct recording *r = context;
	st_mutex_lock(&r->mutex);
	++r->events;
	if (b[0] == 0xf0) { CHECK(size >= 2 && b[size - 1] == 0xf7); ++r->sysex; }
	if ((b[0] & 0xf0) == 0x90 && b[2]) {
		CHECK(r->on < 256); r->keys[r->on] = b[1]; r->positions[r->on++] = r->frames; r->sounding = true;
	}
	if ((b[0] & 0xf0) == 0x80 || ((b[0] & 0xf0) == 0x90 && !b[2])) { ++r->off; r->sounding = false; }
	if ((b[0] & 0xf0) == 0xb0 && (b[1] == 120 || b[1] == 123)) r->sounding = false;
	bool fail = r->fail;
	st_mutex_unlock(&r->mutex);
	return fail ? IO_ERROR_IO : 0;
}
static int render(void *context, int16_t *frames, size_t count)
{
	struct recording *r = context;
	st_mutex_lock(&r->mutex);
	if (r->render_fail) { st_mutex_unlock(&r->mutex); return IO_ERROR_IO; }
	for (size_t i = 0; i < count * AUDIO_CHANNELS; ++i) frames[i] = r->sounding ? 10000 : 0;
	r->frames += count;
	st_mutex_unlock(&r->mutex);
	return 0;
}
static int gain(void *context, float left, float right)
{
	struct recording *r = context;
	st_mutex_lock(&r->mutex); r->left = left; r->right = right; st_mutex_unlock(&r->mutex);
	return 0;
}
static void delay(void) { const struct timespec d = {0, 2000000}; st_thread_sleep(&d, NULL); }
static uint64_t now(void) { uint64_t ns; CHECK(st_time_monotonic_ns(&ns) == 0); return ns; }
static void until_on(struct recording *r, size_t count)
{
	uint64_t deadline = now() + 3000000000U;
	for (;;) {
		st_mutex_lock(&r->mutex); bool done = r->on >= count; st_mutex_unlock(&r->mutex);
		if (done) return;
		CHECK(now() < deadline); delay();
	}
}
static struct st_music_note *note(int pitch, size_t frames)
{
	struct st_music_note *n = malloc(sizeof(*n)); CHECK(n != NULL);
	*n = (struct st_music_note){pitch, frames, 0}; return n;
}
static void pull_until_idle(st_music_t *m, st_audio_runtime_t *runtime)
{
	int16_t pcm[512 * AUDIO_CHANNELS];
	uint64_t deadline = now() + 5000000000U;
	while (st_music_busy(m)) {
		CHECK(st_audio_runtime_pull(runtime, pcm, 512) == 0);
		CHECK(now() < deadline); delay();
	}
	CHECK(st_music_wait(m) == 0);
}
static void internal(void)
{
	st_audio_runtime_t *runtime; audio_stream_t *stream = NULL; st_music_t *m;
	CHECK(st_audio_runtime_create_pull(&runtime) == 0);
	struct recording r = {0}; CHECK(st_mutex_init(&r.mutex, ST_MUTEX_PLAIN) == ST_THREAD_SUCCESS);
	struct st_midi_sink sink = {.state = &r, .send = send, .render = render};
	CHECK(st_music_create_with_sink(&m, runtime, &stream, &sink) == 0);
	CHECK(st_music_queue_notes(m, note(0, 128), 1) == 0);
	CHECK(st_music_queue_notes(m, note(83, 256), 1) == 0);
	until_on(&r, 2);
	uint64_t ready = now() + 3000000000U;
	while (audio_stream_buffered_frames(st_music_pcm_stream(m)) < 384) { CHECK(now() < ready); delay(); }
	/* Both requests must render before either drains. One coherent pull
	 * must have no inserted silence at the capture boundary. */
	int16_t consecutive[384 * AUDIO_CHANNELS];
	CHECK(st_audio_runtime_pull(runtime, consecutive, 384) == 0);
	for (size_t i = 0; i < sizeof(consecutive) / sizeof(consecutive[0]); ++i) CHECK(consecutive[i] != 0);
	pull_until_idle(m, runtime);
	CHECK(r.on == 2 && r.off == 2 && r.keys[0] == 36 && r.keys[1] == 119);
	CHECK(r.positions[0] == 0 && r.positions[1] == 128 && r.frames == 384);
	CHECK(st_music_queue_notes(m, note(1, AUDIO_SAMPLE_RATE * 10U), 1) == 0);
	until_on(&r, 3);
	/* A synth never runs farther ahead than the bounded PCM window. */
	for (int i = 0; i < 20; ++i) delay();
	st_mutex_lock(&r.mutex); CHECK(r.frames <= 384 + 4096); st_mutex_unlock(&r.mutex);
	CHECK(st_music_flush(m, 0) == 0 && !st_music_busy(m));
	CHECK(stream != NULL && audio_stream_is_idle(stream));
	CHECK(st_music_queue_notes(m, note(2, 4096), 1) == 0);
	until_on(&r, 4);
	CHECK(st_music_volume(m, -6, -9, 256) == 0);
	CHECK(st_music_flush(m, 512) == 0);
	pull_until_idle(m, runtime);
	CHECK(audio_stream_is_idle(stream));
	/* A fresh generation must still play after a faded flush. */
	CHECK(st_music_queue_notes(m, note(3, 64), 1) == 0);
	pull_until_idle(m, runtime);
	CHECK(r.on == 5 && r.off == 5);
	st_mutex_lock(&r.mutex); r.render_fail = true; st_mutex_unlock(&r.mutex);
	CHECK(st_music_queue_notes(m, note(44, 128), 1) == 0);
	CHECK(st_music_wait(m) == 0 && !st_music_busy(m) && audio_stream_is_idle(stream));
	CHECK(r.on == 6 && r.off == 6);
	st_mutex_lock(&r.mutex); r.render_fail = false; st_mutex_unlock(&r.mutex);
	CHECK(st_music_queue_notes(m, note(44, 64), 1) == 0);
	pull_until_idle(m, runtime);
	CHECK(r.on == 7 && r.off == 7);
	st_music_destroy(m); CHECK(stream == NULL);
	st_audio_runtime_destroy(runtime); st_mutex_destroy(&r.mutex);
}
struct waiter { st_music_t *music; atomic_bool entered, done; int status; };
static int wait_music(void *context)
{
	struct waiter *w = context; atomic_store(&w->entered, true);
	w->status = st_music_wait(w->music); atomic_store(&w->done, true); return 0;
}
static void external(void)
{
	st_audio_runtime_t *runtime; audio_stream_t *stream = NULL; st_music_t *m;
	CHECK(st_audio_runtime_create(&runtime, 0) == 0);
	struct recording r = {0}; CHECK(st_mutex_init(&r.mutex, ST_MUTEX_PLAIN) == ST_THREAD_SUCCESS);
	struct st_midi_sink sink = {.state = &r, .send = send, .gain = gain};
	CHECK(st_music_create_with_sink(&m, runtime, &stream, &sink) == 0);
	CHECK(st_music_available(m));
	CHECK(st_music_queue_notes(m, note(33, AUDIO_SAMPLE_RATE * 10U), 1) == 0);
	until_on(&r, 1); CHECK(stream == NULL && st_music_busy(m));
	CHECK(st_music_volume(m, -3, -6, 0) == 0);
	for (int i = 0; i < 10; ++i) delay();
	st_mutex_lock(&r.mutex); CHECK(r.left == -3 && r.right == -6); st_mutex_unlock(&r.mutex);
	for (size_t i = 1; i < 128; ++i) CHECK(st_music_queue_notes(m, note(0, 1), 1) == 0);
	struct st_music_note *rejected = note(0, 1);
	CHECK(st_music_queue_notes(m, rejected, 1) == IO_ERROR_BUSY); free(rejected);
	FILE *full = tmpfile(); CHECK(full != NULL);
	CHECK(st_music_queue_file(m, full) == IO_ERROR_BUSY); CHECK(fclose(full) == 0);
	struct waiter w = {.music = m}; atomic_init(&w.entered, false); atomic_init(&w.done, false);
	st_thread_t thread; CHECK(st_thread_create(&thread, wait_music, &w) == ST_THREAD_SUCCESS);
	while (!atomic_load(&w.entered)) delay();
	delay();
	CHECK(st_music_flush(m, 0) == 0 && !st_music_busy(m));
	CHECK(st_thread_join(thread, NULL) == ST_THREAD_SUCCESS && atomic_load(&w.done));
	CHECK(w.status == IO_ERROR_CANCELLED && r.on == 1 && r.off == 1);
	CHECK(st_music_queue_notes(m, note(34, AUDIO_SAMPLE_RATE), 1) == 0);
	until_on(&r, 2);
	CHECK(st_music_flush(m, 441) == 0);
	uint64_t deadline = now() + 1000000000U;
	while (st_music_busy(m)) { CHECK(now() < deadline); delay(); }
	st_mutex_lock(&r.mutex); r.fail = true; st_mutex_unlock(&r.mutex);
	CHECK(st_music_queue_notes(m, note(34, 4410), 1) == 0);
	CHECK(st_music_wait(m) == 0 && !st_music_busy(m));
	CHECK(r.on == 3 && r.off == 2 && !r.sounding);
	st_mutex_lock(&r.mutex); r.fail = false; st_mutex_unlock(&r.mutex);
	CHECK(st_music_queue_notes(m, note(35, AUDIO_SAMPLE_RATE * 10U), 1) == 0);
	until_on(&r, 4);
	st_audio_runtime_stop(runtime);
	CHECK(st_music_wait(m) == 0); CHECK(!st_music_busy(m));
	CHECK(st_music_queue_notes(m, rejected = note(0, 1), 1) == IO_ERROR_CANCELLED); free(rejected);
	st_music_destroy(m); st_audio_runtime_destroy(runtime); st_mutex_destroy(&r.mutex);
}
static void files(void)
{
	static const uint8_t song[] = {
		'M','T','h','d',0,0,0,6,0,0,0,1,0,96,
		'M','T','r','k',0,0,0,20,
		0,0xf0,5,0x7e,0x7f,9,1,0xf7,0,0x90,60,100,96,0x80,60,0,0,0xff,0x2f,0,
	};
	st_audio_runtime_t *runtime; audio_stream_t *stream = NULL; st_music_t *m;
	CHECK(st_audio_runtime_create_pull(&runtime) == 0);
	struct recording r = {0}; CHECK(st_mutex_init(&r.mutex, ST_MUTEX_PLAIN) == ST_THREAD_SUCCESS);
	struct st_midi_sink sink = {.state = &r, .send = send, .render = render};
	CHECK(st_music_create_with_sink(&m, runtime, &stream, &sink) == 0);
	FILE *file = tmpfile(); CHECK(file != NULL);
	CHECK(fwrite(song, 1, sizeof(song), file) == sizeof(song) && fflush(file) == 0);
	CHECK(st_music_queue_file(m, file) == 0); /* transfers the open file */
	CHECK(st_music_queue_notes(m, note(33, 128), 1) == 0);
	FILE *invalid = tmpfile(); CHECK(invalid != NULL);
	CHECK(fwrite(song, 1, sizeof(song) - 1, invalid) == sizeof(song) - 1 && fflush(invalid) == 0);
	CHECK(st_music_queue_file(m, invalid) == IO_ERROR_INVALID_FORMAT);
	CHECK(fclose(invalid) == 0); /* rejection retains caller ownership */
	pull_until_idle(m, runtime);
	CHECK(r.on == 2 && r.off == 2 && r.sysex == 1);
	CHECK(r.keys[0] == 60 && r.keys[1] == 69 && r.positions[1] == 110250);
	CHECK(r.frames == 110378);
	st_music_destroy(m); st_audio_runtime_destroy(runtime); st_mutex_destroy(&r.mutex);
}
int main(void) { internal(); external(); files(); return 0; }
