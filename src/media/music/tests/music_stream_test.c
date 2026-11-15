/* Live MIDI ownership, held voices and lifecycle without physical devices. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "music_internal.h"
#include "st_threads.h"
#include "st_time.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%d: %s\n", __LINE__, #e); exit(1); } } while (0)
struct recording {
	st_mutex_t mutex;
	st_condition_t changed;
	uint8_t bytes[8192];
	size_t size, messages, frames, closed, modes, resets, cached;
	int16_t tail;
	st_thread_t owner;
	bool sounding, ansi, fail_send, fail_render, block, entered;
};
static int send(void *context, const uint8_t *bytes, size_t count)
{
	struct recording *r = context;
	CHECK(!st_thread_equal(r->owner, st_thread_current()));
	st_mutex_lock(&r->mutex);
	r->entered = true; st_condition_broadcast(&r->changed);
	while (r->block) st_condition_wait(&r->changed, &r->mutex);
	CHECK(count <= sizeof(r->bytes) - r->size);
	memcpy(r->bytes + r->size, bytes, count); r->size += count; ++r->messages;
	if (count == 6 && memcmp(bytes,"\xf0\x7e\x7f\x09\x01\xf7",6) == 0) {
		++r->resets; r->sounding=false; r->tail=0;
		/* A renderer may retain samples from its last partial DSP block. */
		r->cached=64;
	}
	if ((bytes[0] & 0xf0) == 0x90) r->sounding = bytes[2] != 0;
	if ((bytes[0] & 0xf0) == 0x80 || ((bytes[0] & 0xf0) == 0xb0 && (bytes[1] == 120 || bytes[1] == 123))) r->sounding = false;
	bool fail = r->fail_send;
	st_mutex_unlock(&r->mutex);
	return fail ? IO_ERROR_IO : 0;
}
static int render(void *context, int16_t *frames, size_t count)
{
	struct recording *r = context;
	CHECK(!st_thread_equal(r->owner, st_thread_current()));
	st_mutex_lock(&r->mutex);
	for (size_t i = 0; i < count; ++i) {
		int16_t value = r->cached != 0 ? 7000 : r->sounding ? 10000 : r->tail;
		if (r->cached != 0) --r->cached;
		frames[2*i]=frames[2*i+1]=value;
	}
	r->frames += count; bool fail = r->fail_render;
	st_mutex_unlock(&r->mutex);
	return fail ? IO_ERROR_IO : 0;
}
static int gain(void *context, float left, float right)
{
	struct recording *r = context; (void)left; (void)right;
	CHECK(!st_thread_equal(r->owner, st_thread_current())); return 0;
}
static int mode(void *context, bool ansi)
{
	struct recording *r = context;
	CHECK(!st_thread_equal(r->owner, st_thread_current()));
	st_mutex_lock(&r->mutex); r->ansi = ansi; ++r->modes; st_mutex_unlock(&r->mutex);
	return 0;
}
static void close_sink(void *context)
{
	struct recording *r = context; ++r->closed;
}
static void delay(void) { const struct timespec t = {0, 1000000}; st_thread_sleep(&t, NULL); }
static uint64_t now(void) { uint64_t ns; CHECK(st_time_monotonic_ns(&ns) == 0); return ns; }
static void init(struct recording *r)
{
	memset(r, 0, sizeof(*r)); r->owner = st_thread_current();
	CHECK(st_mutex_init(&r->mutex, ST_MUTEX_PLAIN) == ST_THREAD_SUCCESS);
	CHECK(st_condition_init(&r->changed) == ST_THREAD_SUCCESS);
}
static void done(struct recording *r)
{
	CHECK(r->closed == 1);
	st_condition_destroy(&r->changed); st_mutex_destroy(&r->mutex);
}
static struct st_midi_sink sink(struct recording *r, bool pcm)
{
	return (struct st_midi_sink){.state = r, .send = send, .render = pcm ? render : NULL,
	    .gain = gain, .set_ansi = mode, .close = close_sink};
}
static void expect(struct recording *r, const uint8_t *bytes, size_t size)
{
	st_mutex_lock(&r->mutex);
	CHECK(r->size == size && (size == 0 || memcmp(r->bytes, bytes, size) == 0));
	st_mutex_unlock(&r->mutex);
}
static void native(void)
{
	st_audio_runtime_t *runtime; st_music_t *m; audio_stream_t *pcm = NULL;
	CHECK(st_audio_runtime_create(&runtime, 0) == 0);
	struct recording r; init(&r); struct st_midi_sink s = sink(&r, false);
	CHECK(st_music_create_with_sink(&m, runtime, &pcm, &s) == 0);
	const uint8_t partial[] = {0x90,60};
	CHECK(st_music_queue_midi(m, partial, sizeof(partial)) == 0 && st_music_wait(m) == 0);
	expect(&r, NULL, 0); CHECK(pcm == NULL && st_music_busy(m));
	const uint8_t velocity = 100;
	CHECK(st_music_queue_midi(m, &velocity, 1) == 0 && st_music_wait(m) == 0);
	const uint8_t on[] = {0x90,60,100}; expect(&r, on, sizeof(on));
	/* Wait completes a write without releasing held voices or running status. */
	for (int i = 0; i < 4; ++i) delay();
	st_mutex_lock(&r.mutex); CHECK(r.sounding && !r.ansi && r.modes == 1); st_mutex_unlock(&r.mutex);
	const uint8_t next[] = {61,0xf8,101,0xf0,0x7d,1};
	CHECK(st_music_queue_midi(m, next, sizeof(next)) == 0 && st_music_wait(m) == 0);
	const uint8_t expected[] = {0x90,60,100,0xf8,0x90,61,101}; expect(&r, expected, sizeof(expected));
	const uint8_t end[] = {2,0xf7,0x80,60,0};
	CHECK(st_music_queue_midi(m, end, sizeof(end)) == 0 && st_music_wait(m) == 0);
	const uint8_t complete[] = {0x90,60,100,0xf8,0x90,61,101,0xf0,0x7d,1,2,0xf7,0x80,60,0};
	expect(&r, complete, sizeof(complete));
	CHECK(st_music_flush(m, 0) == 0 && !st_music_busy(m));
	/* Flush cancels incomplete messages; following orphan data stays harmless. */
	CHECK(st_music_queue_midi(m, &velocity, 1) == 0 && st_music_wait(m) == 0);
	st_mutex_lock(&r.mutex); CHECK(r.size == sizeof(complete) + 192); st_mutex_unlock(&r.mutex);
	/* A regular ANSI request can take over from a live session. */
	struct st_music_note *note = malloc(sizeof(*note)); CHECK(note != NULL);
	*note = (struct st_music_note){12,0,0};
	CHECK(st_music_queue_notes(m, note, 1) == 0 && st_music_wait(m) == 0 && !st_music_busy(m));
	st_mutex_lock(&r.mutex); CHECK(r.ansi); st_mutex_unlock(&r.mutex);
	CHECK(st_music_queue_midi(m, on, sizeof(on)) == 0 && st_music_wait(m) == 0);
	st_mutex_lock(&r.mutex); CHECK(!r.ansi && r.sounding); st_mutex_unlock(&r.mutex);
	st_music_destroy(m); CHECK(pcm == NULL && !r.sounding);
	st_audio_runtime_destroy(runtime); done(&r);
}
struct waiter { st_music_t *m; atomic_bool done; int status; };
static int wait_write(void *context)
{
	struct waiter *w = context; w->status = st_music_wait(w->m);
	atomic_store(&w->done, true); return 0;
}
static void acknowledge(st_music_t *m, st_audio_runtime_t *runtime)
{
	struct waiter w = {.m = m}; atomic_init(&w.done, false);
	st_thread_t thread; CHECK(st_thread_create(&thread, wait_write, &w) == ST_THREAD_SUCCESS);
	uint64_t deadline = now() + 3000000000U;
	int16_t frames[256 * AUDIO_CHANNELS];
	while (!atomic_load(&w.done)) {
		CHECK(st_audio_runtime_pull(runtime, frames, 256) == 0);
		CHECK(now() < deadline); delay();
	}
	CHECK(st_thread_join(thread, NULL) == ST_THREAD_SUCCESS && w.status == 0);
}
static void buffered(st_music_t *m)
{
	uint64_t deadline = now() + 3000000000U;
	for (;;) {
		audio_stream_t *stream = st_music_pcm_stream(m);
		size_t count = stream == NULL ? 0 : audio_stream_buffered_frames(stream);
		/* Support three consecutive 512-frame device reads while bounding
		 * queued lookahead to 50 ms, without a physical clock or device. */
		CHECK(count <= AUDIO_SAMPLE_RATE/20);
		if (count >= 3*512) return;
		CHECK(now() < deadline); delay();
	}
}
static void software(void)
{
	st_audio_runtime_t *runtime; st_music_t *m; audio_stream_t *pcm = NULL;
	CHECK(st_audio_runtime_create_pull(&runtime) == 0);
	struct recording r; init(&r); struct st_midi_sink s = sink(&r, true);
	CHECK(st_music_create_with_sink(&m, runtime, &pcm, &s) == 0);
	const uint8_t on[] = {0x90,60,100};
	CHECK(st_music_queue_midi(m, on, sizeof(on)) == 0);
	acknowledge(m, runtime); buffered(m);
	/* Backpressure bounds rendering without a hardware clock or extra worker. */
	st_mutex_lock(&r.mutex); size_t rendered = r.frames; st_mutex_unlock(&r.mutex);
	for (int i = 0; i < 4; ++i) delay();
	st_mutex_lock(&r.mutex); CHECK(r.frames == rendered && r.sounding); st_mutex_unlock(&r.mutex);
	/* A held constant voice must fill whole device blocks, not merely the
	 * first 256 frames. Also cover a larger callback and consecutive device
	 * reads before the producer has had another scheduling opportunity. */
	const size_t blocks[] = {512,512,1024,512};
	for (size_t pass = 0; pass < sizeof(blocks)/sizeof(*blocks); ++pass) {
		buffered(m); int16_t frames[1024 * AUDIO_CHANNELS];
		CHECK(st_audio_runtime_pull(runtime, frames, blocks[pass]) == 0);
		for (size_t i = 0; i < blocks[pass]*AUDIO_CHANNELS; ++i) CHECK(frames[i] > 2000 && frames[i] < 2600);
	}
	buffered(m);
	for (unsigned pass=0;pass<3;++pass) {
		int16_t frames[512*AUDIO_CHANNELS];
		CHECK(st_audio_runtime_pull(runtime,frames,512)==0);
		for (size_t i=0;i<512*AUDIO_CHANNELS;++i) CHECK(frames[i]>2000 && frames[i]<2600);
	}
	expect(&r, on, sizeof(on));
	const uint8_t off[] = {0x80,60,0};
	CHECK(st_music_queue_midi(m, off, sizeof(off)) == 0); acknowledge(m, runtime); buffered(m);
	int16_t frames[256 * AUDIO_CHANNELS]; CHECK(st_audio_runtime_pull(runtime, frames, 256) == 0);
	for (size_t i = 0; i < sizeof(frames) / sizeof(frames[0]); ++i) CHECK(frames[i] == 0);
	/* Software raw->ANSI uses the same stream without losing completion markers. */
	struct st_music_note *note = malloc(sizeof(*note)); CHECK(note != NULL);
	*note = (struct st_music_note){12,64,0}; CHECK(st_music_queue_notes(m, note, 1) == 0);
	acknowledge(m, runtime);
	uint64_t idle_deadline = now() + 3000000000U;
	while (st_music_busy(m)) { CHECK(now() < idle_deadline); delay(); }
	CHECK(st_music_queue_midi(m, on, sizeof(on)) == 0); acknowledge(m, runtime); buffered(m);
	/* Files retain their usual reset, release tail and completion ordering
	 * when the FIFO changes from live bytes to a file and back again. */
	static const uint8_t song[] = {
		'M','T','h','d',0,0,0,6,0,0,0,1,0,96,
		'M','T','r','k',0,0,0,12,0,0x90,61,100,0,0x80,61,0,0,0xff,0x2f,0,
	};
	FILE *file = tmpfile(); CHECK(file != NULL);
	CHECK(fwrite(song, 1, sizeof(song), file) == sizeof(song) && fflush(file) == 0);
	CHECK(st_music_queue_file(m, file) == 0);
	CHECK(st_music_queue_midi(m, on, sizeof(on)) == 0); acknowledge(m, runtime); buffered(m);
	st_mutex_lock(&r.mutex);
	CHECK(r.sounding && !r.ansi && r.size >= sizeof(on) && memcmp(r.bytes + r.size - sizeof(on), on, sizeof(on)) == 0);
	st_mutex_unlock(&r.mutex);
	CHECK(st_music_flush(m, 64) == 0); acknowledge(m, runtime);
	uint64_t deadline = now() + 3000000000U;
	while (st_music_busy(m)) { CHECK(st_audio_runtime_pull(runtime, frames, 256) == 0); CHECK(now() < deadline); delay(); }
	CHECK(audio_stream_is_idle(pcm));
	st_music_destroy(m); CHECK(pcm == NULL);
	st_audio_runtime_destroy(runtime); done(&r);
}
static void capacity(void)
{
	st_audio_runtime_t *runtime; st_music_t *m; audio_stream_t *pcm = NULL;
	CHECK(st_audio_runtime_create(&runtime, 0) == 0);
	struct recording r; init(&r); r.block = true; struct st_midi_sink s = sink(&r, false);
	CHECK(st_music_create_with_sink(&m, runtime, &pcm, &s) == 0);
	uint8_t note[] = {0x90,60,100};
	CHECK(st_music_queue_midi(m, note, sizeof(note)) == 0);
	st_mutex_lock(&r.mutex);
	while (!r.entered) st_condition_wait(&r.changed, &r.mutex);
	st_mutex_unlock(&r.mutex);
	for (int i = 1; i < 128; ++i) CHECK(st_music_queue_midi(m, note, sizeof(note)) == 0);
	CHECK(st_music_queue_midi(m, note, sizeof(note)) == IO_ERROR_BUSY);
	memset(note, 0, sizeof(note));
	st_mutex_lock(&r.mutex); r.block = false; st_condition_broadcast(&r.changed); st_mutex_unlock(&r.mutex);
	CHECK(st_music_wait(m) == 0);
	st_mutex_lock(&r.mutex);
	CHECK(r.messages == 128 && r.size == 384);
	for (size_t i = 0; i < 128; ++i) CHECK(r.bytes[i * 3] == 0x90 && r.bytes[i * 3 + 1] == 60 && r.bytes[i * 3 + 2] == 100);
	st_mutex_unlock(&r.mutex);
	st_music_destroy(m); st_audio_runtime_destroy(runtime); done(&r);
}
struct controller { st_music_t *m; bool destroy, reset; atomic_bool entered, done; int status; };
static int control(void *context)
{
	struct controller *c = context; atomic_store(&c->entered, true);
	if (c->destroy) st_music_destroy(c->m);
	else c->status = c->reset ? st_music_reset_midi(c->m) : st_music_flush(c->m,0);
	atomic_store(&c->done, true); return 0;
}
static void lifetime(void)
{
	for (int operation = 0; operation < 3; ++operation) {
		st_audio_runtime_t *runtime; st_music_t *m; audio_stream_t *pcm = NULL;
		CHECK(st_audio_runtime_create(&runtime, 0) == 0);
		struct recording r; init(&r); r.block = true; struct st_midi_sink s = sink(&r, false);
		CHECK(st_music_create_with_sink(&m, runtime, &pcm, &s) == 0);
		const uint8_t on[] = {0x90,60,100}; CHECK(st_music_queue_midi(m, on, sizeof(on)) == 0);
		st_mutex_lock(&r.mutex);
		while (!r.entered) st_condition_wait(&r.changed, &r.mutex);
		st_mutex_unlock(&r.mutex);
		struct controller c = {.m=m,.destroy=operation==1,.reset=operation==2};
		atomic_init(&c.entered, false); atomic_init(&c.done, false);
		st_thread_t thread; CHECK(st_thread_create(&thread, control, &c) == ST_THREAD_SUCCESS);
		while (!atomic_load(&c.entered)) delay();
		/* An in-flight sink callback must quiesce before either API returns. */
		CHECK(!atomic_load(&c.done));
		st_mutex_lock(&r.mutex); r.block = false; st_condition_broadcast(&r.changed); st_mutex_unlock(&r.mutex);
		CHECK(st_thread_join(thread, NULL) == ST_THREAD_SUCCESS && atomic_load(&c.done));
		if (!c.destroy) { CHECK(c.status==0 && !st_music_busy(m)); st_music_destroy(m); }
		CHECK(pcm == NULL && !r.sounding);
		st_audio_runtime_destroy(runtime); done(&r);
	}
}
static void session_reset(void)
{
	for (unsigned pcm_output=0;pcm_output<2;++pcm_output) {
		st_audio_runtime_t *runtime; st_music_t *m; audio_stream_t *pcm=NULL;
		CHECK((pcm_output ? st_audio_runtime_create_pull(&runtime)
		    : st_audio_runtime_create(&runtime,0))==0);
		struct recording r; init(&r); struct st_midi_sink s=sink(&r,pcm_output!=0);
		CHECK(st_music_create_with_sink(&m,runtime,&pcm,&s)==0);
		CHECK(st_music_volume(m,-6,-9,0)==0);
		const uint8_t on[]={0x90,60,100};
		for (unsigned pass=0;pass<3;++pass) {
			CHECK(st_music_queue_midi(m,on,sizeof(on))==0);
			if (pcm_output) buffered(m); else CHECK(st_music_wait(m)==0);
			const uint8_t partial[]={0xf0,0x7d,1};
			CHECK(st_music_queue_midi(m,partial,sizeof(partial))==0);
			if (pcm_output) acknowledge(m,runtime); else CHECK(st_music_wait(m)==0);
			st_mutex_lock(&r.mutex); r.tail=2000; st_mutex_unlock(&r.mutex);
			CHECK(st_music_reset_midi(m)==0 && !st_music_busy(m));
			float left,right;
			CHECK(st_music_get_volume(m,&left,&right)==0 && left==-6 && right==-9);
			st_mutex_lock(&r.mutex);
			CHECK(r.resets==pass+1 && !r.sounding && !r.ansi && r.tail==0);
			if (pcm_output) CHECK(r.cached==0);
			st_mutex_unlock(&r.mutex);
			if (pcm_output) {
				CHECK(audio_stream_buffered_frames(pcm)==0);
				int16_t frames[1024*AUDIO_CHANNELS];
				CHECK(st_audio_runtime_pull(runtime,frames,1024)==0);
				for (size_t i=0;i<1024*AUDIO_CHANNELS;++i) CHECK(frames[i]==0);
			}
		}
		st_mutex_lock(&r.mutex); r.fail_send=true; st_mutex_unlock(&r.mutex);
		CHECK(st_music_reset_midi(m)==IO_ERROR_IO && !st_music_busy(m));
		st_mutex_lock(&r.mutex); r.fail_send=false; r.fail_render=pcm_output!=0; st_mutex_unlock(&r.mutex);
		CHECK(st_music_reset_midi(m)==(pcm_output ? IO_ERROR_IO : 0));
		st_mutex_lock(&r.mutex); r.fail_render=false; st_mutex_unlock(&r.mutex);
		CHECK(st_music_reset_midi(m)==0);
		st_audio_runtime_stop(runtime);
		CHECK(st_music_reset_midi(m)==IO_ERROR_CANCELLED && !st_music_busy(m));
		st_music_destroy(m); st_audio_runtime_destroy(runtime); done(&r);
	}
	/* A blocked reset owns the sink and rejects new submissions. Stop during
	 * that reset still waits for the callback and reports cancellation. */
	for (unsigned stop=0;stop<2;++stop) {
		st_audio_runtime_t *runtime; st_music_t *m; audio_stream_t *pcm=NULL;
		CHECK(st_audio_runtime_create(&runtime,0)==0);
		struct recording r; init(&r); r.block=true; struct st_midi_sink s=sink(&r,false);
		CHECK(st_music_create_with_sink(&m,runtime,&pcm,&s)==0);
		struct controller c={.m=m,.reset=true};
		atomic_init(&c.entered,false); atomic_init(&c.done,false);
		st_thread_t thread; CHECK(st_thread_create(&thread,control,&c)==ST_THREAD_SUCCESS);
		st_mutex_lock(&r.mutex);
		while (!r.entered) st_condition_wait(&r.changed,&r.mutex);
		st_mutex_unlock(&r.mutex);
		const uint8_t note[]={0x90,60,100};
		CHECK(st_music_queue_midi(m,note,sizeof(note))==IO_ERROR_BUSY && !atomic_load(&c.done));
		if (stop) st_audio_runtime_stop(runtime);
		st_mutex_lock(&r.mutex); r.block=false; st_condition_broadcast(&r.changed); st_mutex_unlock(&r.mutex);
		CHECK(st_thread_join(thread,NULL)==ST_THREAD_SUCCESS);
		CHECK(c.status==(stop ? IO_ERROR_CANCELLED : 0));
		st_music_destroy(m); st_audio_runtime_destroy(runtime); done(&r);
	}
}
static void failures(void)
{
	for (int failure = 0; failure < 3; ++failure) {
		st_audio_runtime_t *runtime; st_music_t *m; audio_stream_t *pcm = NULL;
		CHECK(st_audio_runtime_create_pull(&runtime) == 0);
		struct recording r; init(&r); r.fail_send = failure == 0; r.fail_render = failure == 1;
		struct st_midi_sink s = sink(&r, failure != 0);
		CHECK(st_music_create_with_sink(&m, runtime, &pcm, &s) == 0);
		const uint8_t on[] = {0x90,60,100};
		CHECK(st_music_queue_midi(m, on, sizeof(on)) == 0);
		if (failure == 2) { buffered(m); st_audio_runtime_stop(runtime); }
		uint64_t deadline = now() + 3000000000U;
		while (st_music_busy(m)) { CHECK(now() < deadline); delay(); }
		CHECK(st_music_wait(m) == 0 && !r.sounding);
		if (failure != 2) {
			st_mutex_lock(&r.mutex); r.fail_send = r.fail_render = false; st_mutex_unlock(&r.mutex);
			CHECK(st_music_queue_midi(m, on, sizeof(on)) == 0);
			acknowledge(m, runtime); CHECK(st_music_busy(m));
		} else CHECK(st_music_queue_midi(m, on, sizeof(on)) == IO_ERROR_CANCELLED);
		st_music_destroy(m); CHECK(pcm == NULL); st_audio_runtime_destroy(runtime); done(&r);
	}
}
int main(void)
{
	CHECK(st_music_queue_midi(NULL, NULL, 0) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_music_reset_midi(NULL)==IO_ERROR_INVALID_ARGUMENT);
	st_audio_runtime_t *runtime; st_music_t *m; audio_stream_t *pcm = NULL;
	CHECK(st_audio_runtime_create(&runtime, 0) == 0 && st_music_create(&m, runtime, &pcm) == 0);
	CHECK(st_music_queue_midi(m, NULL, 1) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_music_queue_midi(m, NULL, 0) == 0 && !st_music_busy(m));
	const uint8_t byte = 0xf8;
	CHECK(st_music_queue_midi(m, &byte, SIZE_MAX) == IO_ERROR_MESSAGE_TOO_LARGE);
	CHECK(st_music_queue_midi(m, &byte, 1) == IO_ERROR_NOT_SUPPORTED && !st_music_busy(m));
	CHECK(st_music_reset_midi(m)==IO_ERROR_NOT_SUPPORTED && !st_music_busy(m));
	st_music_destroy(m); st_audio_runtime_destroy(runtime);
	native(); software(); capacity(); lifetime(); session_reset(); failures(); return 0;
}
