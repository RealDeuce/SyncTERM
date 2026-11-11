/* Live producer ownership, acknowledgment and concurrent cancellation. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "audio_mixer.h"
#include "io_error.h"
#include "st_threads.h"
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct source { size_t position,end; unsigned calls; bool invalid; };
static size_t
render(void *context, int16_t *frames, size_t capacity)
{
	struct source *s = context; ++s->calls;
	if (s->invalid) return capacity+1;
	size_t count = s->end-s->position;
	if (count > capacity) count = capacity;
	for (size_t i = 0; i < count; ++i) {
		frames[2*i] = (int16_t)(100+s->position+i);
		frames[2*i+1] = (int16_t)(-100-(int)(s->position+i));
	}
	s->position += count;
	return count;
}

static void
basics(void)
{
	audio_mixer_t *m; audio_mixer_stream_t *live,*queue;
	CHECK(audio_mixer_create(&m) == 0);
	CHECK(audio_mixer_stream_create(&live,m,0,0) == 0 && audio_mixer_stream_create(&queue,m,0,0) == 0);
	struct source source = {.end = 2053};
	CHECK(audio_mixer_stream_source_active(live,true) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(audio_mixer_stream_set_source(live,render,&source) == 0);
	CHECK(audio_mixer_stream_is_idle(live));
	CHECK(audio_mixer_stream_source_active(live,true) == 0);
	CHECK(!audio_mixer_stream_is_idle(live) && audio_mixer_stream_buffered_frames(live) == SIZE_MAX);
	CHECK(audio_mixer_stream_drain(live) == IO_ERROR_BUSY);
	int16_t input[4200] = {0},actual[4200],expected[4200];
	CHECK(audio_mixer_stream_append(live,input,1,NULL) == IO_ERROR_BUSY);
	uint64_t ticket; CHECK(audio_mixer_pull_pending(m,actual,2100,&ticket) == 2053);
	CHECK(source.position == 2053 && source.calls == 3 && audio_mixer_stream_is_idle(live));
	CHECK(audio_mixer_stream_buffered_frames(live) == 2053);
	/* Existing queued mixing gives an independent gain/soft-clip reference. */
	for (size_t i = 0; i < 2053; ++i) { input[2*i] = (int16_t)(100+i); input[2*i+1] = (int16_t)(-100-(int)i); }
	CHECK(audio_mixer_stream_append(queue,input,2053,NULL) == 0);
	audio_mixer_acknowledge(m,ticket);
	CHECK(audio_mixer_stream_buffered_frames(live) == 0 && audio_mixer_stream_drain(live) == 0);
	CHECK(audio_mixer_pull(m,expected,2100) == 2053 && memcmp(actual,expected,sizeof(actual)) == 0);
	unsigned calls = source.calls;
	CHECK(audio_mixer_pull(m,actual,5) == 0 && source.calls == calls);
	source.end += 20; CHECK(audio_mixer_stream_source_active(live,true) == 0);
	CHECK(audio_mixer_stream_clear(live) == 0 && audio_mixer_stream_is_idle(live));
	CHECK(audio_mixer_pull(m,actual,5) == 0 && source.calls == calls);
	CHECK(audio_mixer_stream_source_active(live,true) == 0);
	CHECK(audio_mixer_pull_pending(m,actual,25,&ticket) == 20);
	CHECK(audio_mixer_stream_clear(live) == 0 && audio_mixer_stream_buffered_frames(live) == 0);
	audio_mixer_acknowledge(m,ticket); /* A late acknowledgment after clear is harmless. */
	CHECK(audio_mixer_stream_set_source(live,NULL,NULL) == 0);
	CHECK(audio_mixer_stream_append(live,input,2,NULL) == 0);
	CHECK(audio_mixer_stream_set_source(live,render,&source) == 0); /* Explicit replacement drops those frames. */
	CHECK(audio_mixer_stream_buffered_frames(live) == 0);
	source.invalid = true; CHECK(audio_mixer_stream_source_active(live,true) == 0);
	CHECK(audio_mixer_pull(m,actual,10) == 0 && audio_mixer_stream_drain(live) == IO_ERROR_OUT_OF_RANGE);
	CHECK(audio_mixer_stream_set_source(live,NULL,NULL) == 0);
	audio_mixer_stream_destroy(live); audio_mixer_stream_destroy(queue); audio_mixer_destroy(m);
}

struct concurrent {
	audio_mixer_t *mixer; audio_mixer_stream_t *stream;
	st_mutex_t mutex; st_condition_t condition;
	bool entered,release;
	unsigned action;
	atomic_bool action_started,action_done;
};
static size_t
blocked(void *context, int16_t *frames, size_t count)
{
	struct concurrent *c = context;
	CHECK(st_mutex_lock(&c->mutex) == ST_THREAD_SUCCESS);
	c->entered = true; CHECK(st_condition_broadcast(&c->condition) == ST_THREAD_SUCCESS);
	while (!c->release) CHECK(st_condition_wait(&c->condition,&c->mutex) == ST_THREAD_SUCCESS);
	CHECK(st_mutex_unlock(&c->mutex) == ST_THREAD_SUCCESS);
	memset(frames,0,count*2*sizeof(*frames)); return count;
}
static int
pull(void *context)
{
	struct concurrent *c = context; int16_t frames[20];
	CHECK(audio_mixer_pull(c->mixer,frames,10) == 10); return 0;
}
static int
cancel(void *context)
{
	struct concurrent *c = context;
	atomic_store(&c->action_started,true);
	if (c->action == 0)
		CHECK(audio_mixer_stream_set_source(c->stream,NULL,NULL) == 0);
	else if (c->action == 1) {
		audio_mixer_stream_destroy(c->stream);
		c->stream = NULL;
	}
	else
		audio_mixer_stop(c->mixer,IO_ERROR_CANCELLED);
	atomic_store(&c->action_done,true); return 0;
}
static int
wait_data(void *context)
{
	return audio_mixer_wait_for_data(context);
}
static void
concurrency(unsigned action)
{
	struct concurrent c = {.action = action};
	atomic_init(&c.action_started,false); atomic_init(&c.action_done,false);
	CHECK(st_mutex_init(&c.mutex,ST_MUTEX_PLAIN) == ST_THREAD_SUCCESS);
	CHECK(st_condition_init(&c.condition) == ST_THREAD_SUCCESS);
	CHECK(audio_mixer_create(&c.mixer) == 0 && audio_mixer_stream_create(&c.stream,c.mixer,0,0) == 0);
	CHECK(audio_mixer_stream_set_source(c.stream,blocked,&c) == 0);
	st_thread_t waiting; CHECK(st_thread_create(&waiting,wait_data,c.mixer) == ST_THREAD_SUCCESS);
	CHECK(audio_mixer_stream_source_active(c.stream,true) == 0);
	int result; CHECK(st_thread_join(waiting,&result) == ST_THREAD_SUCCESS && result == 0);
	st_thread_t pulling,cancelling;
	CHECK(st_thread_create(&pulling,pull,&c) == ST_THREAD_SUCCESS);
	CHECK(st_mutex_lock(&c.mutex) == ST_THREAD_SUCCESS);
	while (!c.entered) CHECK(st_condition_wait(&c.condition,&c.mutex) == ST_THREAD_SUCCESS);
	CHECK(st_thread_create(&cancelling,cancel,&c) == ST_THREAD_SUCCESS);
	/* Detach, destroy and stop must wait for the callback to release
	 * its caller-owned context. No elapsed-time assertion is needed. */
	while (!atomic_load(&c.action_started)) st_thread_yield();
	CHECK(!atomic_load(&c.action_done)); c.release = true;
	CHECK(st_condition_broadcast(&c.condition) == ST_THREAD_SUCCESS);
	CHECK(st_mutex_unlock(&c.mutex) == ST_THREAD_SUCCESS);
	CHECK(st_thread_join(pulling,&result) == ST_THREAD_SUCCESS && result == 0);
	CHECK(st_thread_join(cancelling,&result) == ST_THREAD_SUCCESS && result == 0 && atomic_load(&c.action_done));
	if (c.stream != NULL) {
		CHECK(audio_mixer_stream_is_idle(c.stream));
		CHECK(audio_mixer_stream_buffered_frames(c.stream) == 0);
	}
	if (action != 2) {
		CHECK(st_thread_create(&waiting,wait_data,c.mixer) == ST_THREAD_SUCCESS);
		audio_mixer_stop(c.mixer,IO_ERROR_CANCELLED);
		CHECK(st_thread_join(waiting,&result) == ST_THREAD_SUCCESS && result == IO_ERROR_CANCELLED);
	}
	if (c.stream != NULL)
		CHECK(audio_mixer_stream_source_active(c.stream,true) == IO_ERROR_CANCELLED);
	audio_mixer_stream_destroy(c.stream); audio_mixer_destroy(c.mixer);
	st_condition_destroy(&c.condition); st_mutex_destroy(&c.mutex);
}

int main(void) { basics(); for (unsigned i = 0; i < 3; ++i) concurrency(i); return 0; }
