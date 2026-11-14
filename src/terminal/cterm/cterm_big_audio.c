/* BIG speaker: existing mixer consumer, channel1 and local muted progression. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_big_audio.h"
#include "cterm_big_internal.h"
#include "cterm.h"
#include "st_audio_runtime.h"
#include "st_console_test.h"
#include "st_threads.h"
#include "st_time.h"
#include "io_error.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
_Static_assert(ST_BIG_SPEAKER_RATE==AUDIO_SAMPLE_RATE,"BIG speaker mixer rate");
/* The terminal owns this value and its attachment to borrowed fx_stream.
 * No new thread: the existing mixer consumer calls source(). The mutex
 * protects only speaker/error. No stream/mixer API is called under it.
 * Detach quiesces source before mutex destruction/free at terminal shutdown. */
struct cterm_big_audio {
	st_mutex_t mutex;
	struct st_big_speaker speaker;
	audio_stream_t *stream;
	uint64_t local_ns;
	uint32_t fraction;
	int error;
	bool sampled, allowed, unavailable, warned, clock_fault;
};
static bool allow(struct cterm_big *b)
{
	st_audio_runtime_t *r=b->terminal->screen.audio;
	return r!=NULL && !st_audio_runtime_stopped(r)
	    && (st_audio_runtime_is_pull(r) || st_audio_runtime_allowed(r)!=0);
}
static int sample(struct cterm_big *b, uint64_t *ns)
{
	int status=b->numeric.io.now_ns==NULL?st_time_monotonic_ns(ns):b->numeric.io.now_ns(b->numeric.io.context,ns);
	if (status==IO_ERROR_IN_PROGRESS) return IO_ERROR_PROTOCOL;
	return status==0 && b->reset_pending?IO_ERROR_INTERRUPTED:status;
}
static size_t source(void *context, int16_t *pcm, size_t count)
{
	struct cterm_big_audio *a=context;
	if (st_mutex_lock(&a->mutex)!=ST_THREAD_SUCCESS) return 0;
	bool quiet=!a->speaker.playing && a->speaker.low>-65536 && a->speaker.low<65536;
	if (quiet) a->speaker.low=0;
	else {
		a->error=st_big_speaker_render(&a->speaker,pcm,count);
		if (!a->speaker.playing && a->speaker.low>-65536 && a->speaker.low<65536) a->speaker.low=0;
	}
	int error=a->error;
	(void)st_mutex_unlock(&a->mutex);
	return quiet || error!=0?0:count;
}
static void detach(struct cterm_big_audio *a)
{
	if (a->stream!=NULL) (void)audio_stream_set_source(a->stream,NULL,NULL);
	a->stream=NULL;
}
static int create(struct cterm_big *b)
{
	if (b->audio!=NULL) return 0;
	struct cterm_big_audio *a=calloc(1,sizeof(*a));
	if (a==NULL) return IO_ERROR_OUT_OF_MEMORY;
	if (st_mutex_init(&a->mutex,ST_MUTEX_PLAIN)!=ST_THREAD_SUCCESS) { free(a); return IO_ERROR_IO; }
	int status=st_big_speaker_init(&a->speaker);
	if (status!=0) { st_mutex_destroy(&a->mutex); free(a); return status; }
	b->audio=a; return 0;
}
int cterm_big_audio_poll(struct cterm_big *b)
{
	struct cterm_big_audio *a=b->audio;
	if (a==NULL || a->clock_fault) return 0;
	uint64_t ns; int status=sample(b,&ns);
	if (status!=0) { a->clock_fault=true; a->sampled=false; return status; }
	if (a->sampled && ns<a->local_ns) { a->clock_fault=true; a->sampled=false; return IO_ERROR_OUT_OF_RANGE; }
	uint64_t elapsed=a->sampled?ns-a->local_ns:0;
	a->local_ns=ns; a->sampled=true;
	bool enabled=allow(b);
	if (enabled!=a->allowed) a->unavailable=false;
	a->allowed=enabled;
	if (a->stream!=NULL) {
		bool idle=audio_stream_is_idle(a->stream);
		if (enabled && !idle) return 0;
		detach(a); elapsed=0; a->fraction=0;
		if (st_mutex_lock(&a->mutex)!=ST_THREAD_SUCCESS) return IO_ERROR_IO;
		/* Explicit channel clear/finished tail leaves no source to revive.
		 * A later F request can start it again. Policy mute retains the gate. */
		if (enabled && idle) st_big_speaker_stop(&a->speaker);
		st_big_speaker_skip(&a->speaker,0);
		(void)st_mutex_unlock(&a->mutex);
	}
	uint64_t fraction=(elapsed%UINT64_C(1000000000))*ST_BIG_SPEAKER_RATE+a->fraction;
	uint64_t frames=(elapsed/UINT64_C(1000000000))*ST_BIG_SPEAKER_RATE+fraction/UINT64_C(1000000000);
	a->fraction=(uint32_t)(fraction%UINT64_C(1000000000));
	if (st_mutex_lock(&a->mutex)!=ST_THREAD_SUCCESS) return IO_ERROR_IO;
	st_big_speaker_skip(&a->speaker,frames);
	bool wanted=a->speaker.playing;
	status=a->error; a->error=0;
	(void)st_mutex_unlock(&a->mutex);
	if (status!=0) return status;
	if (!enabled || a->unavailable || !wanted) return 0;
	audio_stream_t *stream=cterm_fx_stream(b->terminal);
	status=stream==NULL?IO_ERROR_IO:audio_stream_set_source(stream,source,a);
	if (status==0) status=audio_stream_source_active(stream,true);
	if (status==0) { a->stream=stream; a->fraction=0; return 0; }
	if (stream!=NULL) (void)audio_stream_set_source(stream,NULL,NULL);
	a->unavailable=true;
	if (!a->warned) { a->warned=true; cterm_log(b->terminal,ST_CTERM_LOG_WARNING,"BIG speaker output unavailable; continuing muted"); }
	return 0;
}
int cterm_big_audio_frequency(struct cterm_big *b, uint16_t frequency)
{
	if (frequency<=18) return 0;
	int status=create(b);
	if (status==0) b->audio->clock_fault=false;
	if (status==0) status=cterm_big_audio_poll(b);
	if (status!=0) return status;
	struct cterm_big_audio *a=b->audio;
	if (st_mutex_lock(&a->mutex)!=ST_THREAD_SUCCESS) return IO_ERROR_IO;
	(void)st_big_speaker_frequency(&a->speaker,frequency);
	(void)st_mutex_unlock(&a->mutex);
	a->unavailable=false;
	return cterm_big_audio_poll(b);
}
int cterm_big_audio_stop(struct cterm_big *b)
{
	struct cterm_big_audio *a=b->audio;
	if (a==NULL) return 0;
	int status=cterm_big_audio_poll(b); if (status!=0) return status;
	if (st_mutex_lock(&a->mutex)!=ST_THREAD_SUCCESS) return IO_ERROR_IO;
	st_big_speaker_stop(&a->speaker);
	(void)st_mutex_unlock(&a->mutex); return 0;
}
void cterm_big_yield_fx(struct cterm_big *b)
{
	if (b==NULL || b->audio==NULL) return;
	struct cterm_big_audio *a=b->audio;
	detach(a);
	if (st_mutex_lock(&a->mutex)==ST_THREAD_SUCCESS) {
		st_big_speaker_stop(&a->speaker); st_big_speaker_skip(&a->speaker,0);
		(void)st_mutex_unlock(&a->mutex);
	}
	a->sampled=false; a->fraction=0; a->clock_fault=false;
}
void cterm_big_audio_reset(struct cterm_big *b) { cterm_big_yield_fx(b); }
void cterm_big_audio_destroy(struct cterm_big *b)
{
	if (b==NULL || b->audio==NULL) return;
	cterm_big_audio_reset(b); st_mutex_destroy(&b->audio->mutex); free(b->audio); b->audio=NULL;
}
int cterm_big_speaker_snapshot(const struct cterm_big *b, struct st_big_speaker *s)
{
	if (b==NULL || s==NULL) return IO_ERROR_INVALID_ARGUMENT;
	memset(s,0,sizeof(*s)); struct cterm_big_audio *a=b->audio;
	if (a==NULL) return 0;
	if (st_mutex_lock(&a->mutex)!=ST_THREAD_SUCCESS) return IO_ERROR_IO;
	memcpy(s,&a->speaker,sizeof(*s)); (void)st_mutex_unlock(&a->mutex); return 0;
}
#if ST_CONSOLE_TEST
void cterm_big_audio_inspect(const struct cterm_big *b, struct st_console_test_application *app)
{
	struct st_big_speaker s;
	if (cterm_big_speaker_snapshot(b,&s)!=0) return;
	char value[192];
	snprintf(value,sizeof(value),"%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
	    (unsigned)b->music_memory,(unsigned)b->control.music.sync,(unsigned)b->control.music.running,
	    (unsigned)b->control.music.position_low,(unsigned)b->control.music.position_high,
	    (unsigned)b->control.music.scratch,(unsigned)b->sync_entry.waiting,(unsigned)s.playing,
	    (unsigned)s.frequency,(unsigned)s.divisor,(unsigned)b->note.active,(unsigned)b->note.mode,
	    (unsigned)b->note.from,(unsigned)b->note.to,(unsigned)b->note.step,
	    (unsigned)b->note.delay,(unsigned)b->note.sustain,(unsigned)b->note.off);
	st_console_test_text(app,"cterm.big_audio",value);
}
#endif
