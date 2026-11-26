/* BIG channel0 source ownership, control, handoff and quiescent shutdown. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "music_internal.h"
#include "st_music.h"
#include "st_threads.h"
#include "st_time.h"
#define CHECK(x)                                                                \
	do {                                                                    \
		if (!(x)) {                                                     \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); \
			exit(1);                                                \
		}                                                               \
	} while (0)

struct source {
	size_t         count, limit;
	int32_t        amplitude;
	st_mutex_t     mutex;
	st_condition_t condition;
	bool           block, entered, release;
	unsigned       midi;
};

static void
source_init(struct source *s)
{
	memset(s, 0, sizeof(*s));
	s->limit     = SIZE_MAX;
	s->amplitude = 90000;
	CHECK(st_mutex_init(&s->mutex, ST_MUTEX_PLAIN) == ST_THREAD_SUCCESS
	      && st_condition_init(&s->condition) == ST_THREAD_SUCCESS);
}

static void
source_destroy(struct source *s)
{
	st_condition_destroy(&s->condition);
	st_mutex_destroy(&s->mutex);
}

static void
checkpoint(struct source *s)
{
	CHECK(st_mutex_lock(&s->mutex) == ST_THREAD_SUCCESS);
	s->entered = true;
	CHECK(st_condition_broadcast(&s->condition) == ST_THREAD_SUCCESS);
	while (s->block && !s->release)
		CHECK(st_condition_wait(&s->condition, &s->mutex) == ST_THREAD_SUCCESS);
	CHECK(st_mutex_unlock(&s->mutex) == ST_THREAD_SUCCESS);
}

static size_t
render(void *context, int32_t *pcm, size_t capacity)
{
	struct source *s = context;
	checkpoint(s);
	size_t count = s->limit - s->count;
	if (count > capacity)
		count = capacity;
	for (size_t i = 0; i < count; ++i)
		pcm[2 * i] = pcm[2 * i + 1] = s->amplitude;
	s->count += count;
	return count;
}

static int
midi(void *context, const uint8_t *bytes, size_t count)
{
	struct source *s = context;
	(void)bytes;
	(void)count;
	CHECK(st_mutex_lock(&s->mutex) == ST_THREAD_SUCCESS);
	++s->midi;
	CHECK(st_mutex_unlock(&s->mutex) == ST_THREAD_SUCCESS);
	return 0;
}

static int
midi_render(void *context, int16_t *pcm, size_t count)
{
	checkpoint(context);
	memset(pcm, 0, count * AUDIO_FRAME_SIZE);
	return 0;
}

struct fixture {
	st_audio_runtime_t *runtime;
	st_music_t         *music;
	audio_stream_t     *slot;
};

static void
create(struct fixture *f, struct source *injected, bool muted)
{
	memset(f, 0, sizeof(*f));
	CHECK((muted ? st_audio_runtime_create(&f->runtime, 0) : st_audio_runtime_create_pull(&f->runtime)) == 0);
	if (injected != NULL) {
		const struct st_midi_sink sink = {.kind = ST_MIDI_FLUIDSYNTH,
		    .state                              = injected,
		    .send                               = midi,
		    .render                             = midi_render};
		CHECK(st_music_create_with_sink(&f->music, f->runtime, &f->slot, &sink) == 0);
	}
	else
		CHECK(st_music_create(&f->music, f->runtime, &f->slot) == 0);
}

static void
destroy(struct fixture *f)
{
	st_music_destroy(f->music);
	CHECK(f->slot == NULL);
	st_audio_runtime_destroy(f->runtime);
}

static void
notes(st_music_t *music, size_t length)
{
	struct st_music_note *note = calloc(1, sizeof(*note));
	CHECK(note != NULL);
	*note = (struct st_music_note) {.pitch = 12, .sound_frames = length};
	CHECK(st_music_queue_notes(music, note, 1) == 0);
}

static void
controls_and_handoff(void)
{
	struct fixture f;
	struct source  s, other;
	source_init(&s);
	source_init(&other);
	create(&f, &other, false);
	CHECK(st_music_set_pcm_source(f.music, render, &s, true) == 0 && f.slot != NULL
	      && st_music_pcm_stream(f.music) == f.slot);
	CHECK(st_music_pcm_source_selected(f.music, &s) && st_music_pcm_source_active(f.music, &s)
	      && st_music_busy(f.music));
	CHECK(!st_music_pcm_source_selected(f.music, &other) && st_music_wait(f.music) == 0 && other.midi == 0);
	struct st_music_note invalid = {.pitch = 84};
	CHECK(st_music_queue_notes(f.music, &invalid, 1) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_music_queue_midi(f.music, NULL, 0) == 0 && st_music_pcm_source_active(f.music, &s));
	FILE *bad = tmpfile();
	CHECK(bad != NULL && fwrite("invalid", 1, 7, bad) == 7 && fseek(bad, 0, SEEK_SET) == 0);
	CHECK(st_music_queue_file(f.music, bad) != 0 && st_music_pcm_source_active(f.music, &s));
	fclose(bad);
	int16_t pcm[256];
	CHECK(st_audio_runtime_pull(f.runtime, pcm, 1) == 0 && pcm[0] > 19000 && pcm[1] == pcm[0]);
	float left, right;
	CHECK(st_music_get_volume(f.music, &left, &right) == 0 && left == -12 && right == -12);
	CHECK(st_music_volume(f.music, 0, -6, 8) == 0 && st_audio_runtime_pull(f.runtime, pcm, 3) == 0);
	struct audio_stream_volume state;
	CHECK(audio_stream_get_volume_state(f.slot, &state) == 0 && state.ramp_frames == 5);
	CHECK(
	    st_music_get_volume(f.music, &left, &right) == 0 && left == state.left_db && right == state.right_db);
	CHECK(st_music_clear_pcm_source(f.music, &other) == 0 && st_music_pcm_source_active(f.music, &s));
	audio_stream_t *slot = f.slot;
	CHECK(st_music_set_pcm_source(f.music, render, &other, true) == 0 && f.slot == slot
	      && !st_music_pcm_source_selected(f.music, &s));
	CHECK(audio_stream_get_volume_state(f.slot, &state) == 0 && state.ramp_frames == 5
	      && state.target_left_db == 0 && state.target_right_db == -6);
	size_t old_count = s.count;
	CHECK(st_audio_runtime_pull(f.runtime, pcm, 5) == 0 && s.count == old_count);
	CHECK(st_music_get_volume(f.music, &left, &right) == 0 && left == 0 && right == -6);
	CHECK(st_music_flush(f.music, 4) == 0 && st_music_busy(f.music));
	CHECK(st_audio_runtime_pull(f.runtime, pcm, 2) == 0 && st_music_busy(f.music));
	CHECK(st_audio_runtime_pull(f.runtime, pcm, 2) == 0 && !st_music_busy(f.music)
	      && !st_music_pcm_source_active(f.music, &other));
	CHECK(st_music_wait(f.music) == 0 && st_music_clear_pcm_source(f.music, &other) == 0);
	CHECK(st_music_set_pcm_source(f.music, render, &s, true) == 0);
	old_count = s.count;
	notes(f.music, 128);
	CHECK(!st_music_pcm_source_selected(f.music, &s));
	uint64_t start, now;
	CHECK(st_time_monotonic_ms(&start) == 0);
	while (st_music_busy(f.music)) {
		CHECK(st_audio_runtime_pull(f.runtime, pcm, 128) == 0 && st_time_monotonic_ms(&now) == 0
		      && now - start < 2000);
		st_thread_yield();
	}
	CHECK(st_music_wait(f.music) == 0 && s.count == old_count && f.slot == slot && other.midi != 0);
	CHECK(st_music_set_pcm_source(NULL, render, &s, true) == IO_ERROR_INVALID_ARGUMENT
	      && st_music_set_pcm_source(f.music, NULL, &s, true) == IO_ERROR_INVALID_ARGUMENT);
	destroy(&f);
	source_destroy(&s);
	source_destroy(&other);
}

static void
muted_and_failure(void)
{
	struct fixture f;
	struct source  s;
	source_init(&s);
	create(&f, NULL, true);
	CHECK(
	    st_music_set_pcm_source(f.music, render, &s, false) == 0 && f.slot == NULL && st_music_busy(f.music));
	CHECK(st_music_pcm_source_selected(f.music, &s) && !st_music_pcm_source_active(f.music, &s)
	      && st_music_wait(f.music) == 0 && s.count == 0);
	CHECK(st_music_volume(f.music, -3, -9, 100) == 0);
	float left, right;
	CHECK(st_music_get_volume(f.music, &left, &right) == 0 && left == -3 && right == -9);
	CHECK(st_music_flush(f.music, 100) == 0 && !st_music_pcm_source_selected(f.music, &s)
	      && !st_music_busy(f.music));
	/* The existing engine rejects an empty physical-backend mask. Logical
	 * muted selection must remain usable after that opening failure. */
	CHECK(st_music_set_pcm_source(f.music, render, &s, true) == IO_ERROR_INVALID_ARGUMENT
	      && !st_music_pcm_source_selected(f.music, &s));
	CHECK(st_music_set_pcm_source(f.music, render, &s, false) == 0);
	st_audio_runtime_stop(f.runtime);
	CHECK(st_music_set_pcm_source(f.music, render, &s, false) == IO_ERROR_CANCELLED);
	CHECK(st_music_clear_pcm_source(f.music, &s) == 0);
	destroy(&f);
	create(&f, NULL, false);
	CHECK(st_music_set_pcm_source(f.music, render, &s, false) == 0 && f.slot == NULL);
	CHECK(st_music_set_pcm_source(f.music, render, &s, true) == 0);
	int16_t pcm[2];
	CHECK(st_audio_runtime_pull(f.runtime, pcm, 1) == 0 && s.count == 1);
	CHECK(st_music_set_pcm_source(f.music, render, &s, false) == 0 && !st_music_pcm_source_active(f.music, &s)
	      && st_music_busy(f.music));
	CHECK(st_audio_runtime_pull(f.runtime, pcm, 1) == 0 && s.count == 1 && pcm[0] == 0 && pcm[1] == 0);
	destroy(&f);
	source_destroy(&s);
}

struct action {
	struct fixture *f;
	struct source  *source;
	unsigned        kind;
	atomic_bool     started, done;
	int             status;
};

static int
apply(void *context)
{
	struct action *a = context;
	atomic_store(&a->started, true);
	if (a->kind == 0)
		a->status = st_music_clear_pcm_source(a->f->music, a->source);
	else if (a->kind == 1)
		a->status = st_music_set_pcm_source(a->f->music, render, a->source, true);
	else if (a->kind == 2) {
		st_music_destroy(a->f->music);
		a->f->music = NULL;
		a->status   = 0;
	}
	else if (a->kind == 3)
		a->status = st_music_flush(a->f->music, 0);
	else
		a->status = st_music_wait(a->f->music);
	atomic_store(&a->done, true);
	return 0;
}

static int
pull(void *context)
{
	int16_t pcm[128];
	return st_audio_runtime_pull(context, pcm, 64);
}

static void
entered(struct source *s)
{
	CHECK(st_mutex_lock(&s->mutex) == ST_THREAD_SUCCESS);
	while (!s->entered)
		CHECK(st_condition_wait(&s->condition, &s->mutex) == ST_THREAD_SUCCESS);
	CHECK(st_mutex_unlock(&s->mutex) == ST_THREAD_SUCCESS);
}

static void
release(struct source *s)
{
	CHECK(st_mutex_lock(&s->mutex) == ST_THREAD_SUCCESS);
	s->release = true;
	CHECK(st_condition_broadcast(&s->condition) == ST_THREAD_SUCCESS
	      && st_mutex_unlock(&s->mutex) == ST_THREAD_SUCCESS);
}

static void
concurrent_source(unsigned kind)
{
	struct fixture f;
	struct source  s, next;
	source_init(&s);
	source_init(&next);
	s.block = true;
	create(&f, NULL, false);
	CHECK(st_music_set_pcm_source(f.music, render, &s, true) == 0);
	st_thread_t producer, controller;
	CHECK(st_thread_create(&producer, pull, f.runtime) == ST_THREAD_SUCCESS);
	entered(&s);
	struct action a = {.f = &f, .source = kind == 1 ? &next : &s, .kind = kind};
	atomic_init(&a.started, false);
	atomic_init(&a.done, false);
	CHECK(st_thread_create(&controller, apply, &a) == ST_THREAD_SUCCESS);
	while (!atomic_load(&a.started))
		st_thread_yield();
	CHECK(!atomic_load(&a.done));
	release(&s);
	int result;
	CHECK(st_thread_join(producer, &result) == ST_THREAD_SUCCESS && result == 0);
	CHECK(st_thread_join(controller, &result) == ST_THREAD_SUCCESS && result == 0 && a.status == 0
	      && atomic_load(&a.done));
	if (kind == 1)
		CHECK(st_music_pcm_source_active(f.music, &next));
	else
		CHECK(!st_music_pcm_source_selected(f.music, &s));
	destroy(&f);
	source_destroy(&s);
	source_destroy(&next);
}

static void
worker_handoff(void)
{
	struct fixture f;
	struct source  old, next;
	source_init(&old);
	source_init(&next);
	old.block = true;
	create(&f, &old, false);
	notes(f.music, 44100);
	entered(&old);
	struct action a = {.f = &f, .source = &next, .kind = 1};
	atomic_init(&a.started, false);
	atomic_init(&a.done, false);
	st_thread_t controller;
	CHECK(st_thread_create(&controller, apply, &a) == ST_THREAD_SUCCESS);
	while (!atomic_load(&a.started))
		st_thread_yield();
	CHECK(!atomic_load(&a.done));
	release(&old);
	int result;
	CHECK(st_thread_join(controller, &result) == ST_THREAD_SUCCESS && result == 0 && a.status == 0);
	CHECK(st_music_pcm_source_active(f.music, &next) && st_music_wait(f.music) == 0);
	int16_t pcm[2];
	CHECK(st_audio_runtime_pull(f.runtime, pcm, 1) == 0 && pcm[0] > 19000);
	destroy(&f);
	source_destroy(&old);
	source_destroy(&next);
}

static void
finite_wait_cancel(bool cancel)
{
	struct fixture f;
	struct source  s;
	source_init(&s);
	create(&f, NULL, false);
	CHECK(st_music_set_pcm_source(f.music, render, &s, true) == 0 && st_music_flush(f.music, 4) == 0);
	struct action a = {.f = &f, .source = &s, .kind = 4};
	atomic_init(&a.started, false);
	atomic_init(&a.done, false);
	st_thread_t thread;
	CHECK(st_thread_create(&thread, apply, &a) == ST_THREAD_SUCCESS);
	while (!atomic_load(&a.started))
		st_thread_yield();
	if (cancel)
		st_audio_runtime_stop(f.runtime);
	else {
		int16_t pcm[8];
		CHECK(st_audio_runtime_pull(f.runtime, pcm, 4) == 0);
	}
	int result;
	CHECK(st_thread_join(thread, &result) == ST_THREAD_SUCCESS && result == 0
	      && a.status == (cancel ? IO_ERROR_CANCELLED : 0));
	destroy(&f);
	source_destroy(&s);
}

static void
live_handoff(void)
{
	struct fixture f;
	struct source  s, sink;
	source_init(&s);
	source_init(&sink);
	create(&f, &sink, false);
	CHECK(st_music_set_pcm_source(f.music, render, &s, true) == 0);
	const uint8_t bytes[] = {0x90, 60, 100};
	CHECK(
	    st_music_queue_midi(f.music, bytes, sizeof(bytes)) == 0 && !st_music_pcm_source_selected(f.music, &s));
	struct action a = {.f = &f, .kind = 4};
	atomic_init(&a.started, false);
	atomic_init(&a.done, false);
	st_thread_t thread;
	CHECK(st_thread_create(&thread, apply, &a) == ST_THREAD_SUCCESS);
	uint64_t start, now;
	CHECK(st_time_monotonic_ms(&start) == 0);
	while (!atomic_load(&a.done)) {
		int16_t pcm[512];
		CHECK(st_audio_runtime_pull(f.runtime, pcm, 256) == 0);
		CHECK(st_time_monotonic_ms(&now) == 0 && now - start < 2000);
		st_thread_yield();
	}
	int result;
	CHECK(
	    st_thread_join(thread, &result) == ST_THREAD_SUCCESS && result == 0 && a.status == 0 && s.count == 0);
	/* The live renderer keeps held voices after request acknowledgment. A
	 * later OPL source must cancel that producer before taking its slot. */
	CHECK(st_music_set_pcm_source(f.music, render, &s, true) == 0 && st_music_pcm_source_active(f.music, &s));
	CHECK(sink.midi != 0);
	destroy(&f);
	source_destroy(&s);
	source_destroy(&sink);
}

int
main(void)
{
	controls_and_handoff();
	muted_and_failure();
	for (unsigned kind = 0; kind < 4; ++kind)
		concurrent_source(kind);
	worker_handoff();
	live_handoff();
	finite_wait_cancel(false);
	finite_wait_cancel(true);
	return 0;
}
