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
 * protects speaker/effect/error and the borrowed RNG. Only the terminal
 * owner publishes RNG to parser state, while b holds numeric delivery.
 * No stream/mixer API is called under this mutex.
 * Detach quiesces source before mutex destruction/free at terminal shutdown. */
struct cterm_big_audio {
	st_mutex_t mutex;
	struct st_big_speaker speaker;
	struct st_big_effect_player effect;
	bool effect_owned; /* RNG is borrowed exclusively until completion/cancel. */
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
	bool quiet=!a->effect.active && !a->speaker.playing && a->speaker.low>-65536 && a->speaker.low<65536;
	if (quiet) a->speaker.low=0;
	else {
		a->error=a->effect.active?st_big_effect_render(&a->effect,&a->speaker,pcm,count)
		    :st_big_speaker_render(&a->speaker,pcm,count);
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
static int publish(struct cterm_big *b, struct cterm_big_audio *a)
{
	if (st_mutex_lock(&a->mutex)!=ST_THREAD_SUCCESS) return IO_ERROR_IO;
	bool owned=a->effect_owned; uint32_t rng=a->effect.random_state;
	(void)st_mutex_unlock(&a->mutex);
	if (owned) b->numeric.random_state=rng;
	return 0;
}
bool cterm_big_audio_needs_poll(const struct cterm_big *b)
{
	struct cterm_big_audio *a = b->audio;
	if (a == NULL || a->clock_fault) return false;
	if (a->stream != NULL) return true;
	(void)st_mutex_lock(&a->mutex);
	bool active = a->effect.active || a->speaker.playing;
	(void)st_mutex_unlock(&a->mutex);
	return active;
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
		if (enabled && !idle) return publish(b,a);
		detach(a); elapsed=0; a->fraction=0;
		if (st_mutex_lock(&a->mutex)!=ST_THREAD_SUCCESS) return IO_ERROR_IO;
		/* Explicit channel clear/finished tail leaves no source to revive.
		 * A later F request can start it again. Policy mute retains the gate. */
		if (enabled && idle) { st_big_speaker_stop(&a->speaker); a->effect.active=false; }
		st_big_speaker_skip(&a->speaker,0);
		(void)st_mutex_unlock(&a->mutex);
	}
	uint64_t fraction=(elapsed%UINT64_C(1000000000))*ST_BIG_SPEAKER_RATE+a->fraction;
	uint64_t frames=(elapsed/UINT64_C(1000000000))*ST_BIG_SPEAKER_RATE+fraction/UINT64_C(1000000000);
	a->fraction=(uint32_t)(fraction%UINT64_C(1000000000));
	if (st_mutex_lock(&a->mutex)!=ST_THREAD_SUCCESS) return IO_ERROR_IO;
	if (a->effect.active) status=st_big_effect_skip(&a->effect,&a->speaker,frames);
	else st_big_speaker_skip(&a->speaker,frames);
	bool wanted=a->speaker.playing || a->effect.active;
	if (status==0) status=a->error;
	a->error=0;
	(void)st_mutex_unlock(&a->mutex);
	if (status!=0) return status;
	status=publish(b,a); if (status!=0) return status;
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
int cterm_big_audio_effect(struct cterm_big *b, uint16_t selector)
{
	int status=create(b); if (status!=0) return status;
	struct cterm_big_audio *a=b->audio; a->clock_fault=false;
	status=cterm_big_audio_poll(b); if (status!=0) return status;
	if (st_mutex_lock(&a->mutex)!=ST_THREAD_SUCCESS) return IO_ERROR_IO;
	status=st_big_effect_player_begin(&a->effect,selector,true,b->numeric.random_state);
	a->effect_owned=status==0;
	/* Apply the initial Z and prepare the first F or delay at this edge. */
	if (status==0) status=st_big_effect_render(&a->effect,&a->speaker,NULL,0);
	(void)st_mutex_unlock(&a->mutex);
	if (status!=0) return status;
	a->unavailable=false; return cterm_big_audio_poll(b);
}
int cterm_big_audio_effect_done(struct cterm_big *b, bool *done)
{
	if (done==NULL) return IO_ERROR_INVALID_ARGUMENT;
	struct cterm_big_audio *a=b->audio; if (a==NULL) { *done=true; return 0; }
	if (st_mutex_lock(&a->mutex)!=ST_THREAD_SUCCESS) return IO_ERROR_IO;
	*done=!a->effect.active;
	bool owned=a->effect_owned; uint32_t rng=a->effect.random_state;
	if (*done) a->effect_owned=false;
	(void)st_mutex_unlock(&a->mutex);
	if (owned) b->numeric.random_state=rng;
	return 0;
}
void cterm_big_audio_cancel_effect(struct cterm_big *b)
{
	if (b==NULL || b->audio==NULL) return;
	struct cterm_big_audio *a=b->audio;
	if (st_mutex_lock(&a->mutex)!=ST_THREAD_SUCCESS) return;
	bool owned=a->effect_owned; uint32_t rng=a->effect.random_state;
	a->effect_owned=false; a->effect.active=false;
	(void)st_mutex_unlock(&a->mutex);
	if (owned) b->numeric.random_state=rng;
}
void cterm_big_yield_fx(struct cterm_big *b)
{
	if (b==NULL || b->audio==NULL) return;
	struct cterm_big_audio *a=b->audio;
	detach(a);
	cterm_big_audio_cancel_effect(b);
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
int cterm_big_effect_snapshot(const struct cterm_big *b, struct st_big_effect_player *p)
{
	if (b==NULL || p==NULL) return IO_ERROR_INVALID_ARGUMENT;
	memset(p,0,sizeof(*p)); struct cterm_big_audio *a=b->audio;
	if (a==NULL) return 0;
	if (st_mutex_lock(&a->mutex)!=ST_THREAD_SUCCESS) return IO_ERROR_IO;
	*p=a->effect; (void)st_mutex_unlock(&a->mutex); return 0;
}
#if ST_CONSOLE_TEST
void cterm_big_audio_inspect(const struct cterm_big *b, struct st_console_test_application *app)
{
	struct st_big_speaker s={0}; uint32_t position=0;
	struct cterm_big_audio *a=b->audio;
	if (a!=NULL) {
		if (st_mutex_lock(&a->mutex)!=ST_THREAD_SUCCESS) return;
		s=a->speaker; position=a->effect.sequence.position;
		(void)st_mutex_unlock(&a->mutex);
	}
	char value[192];
	struct st_big_music music; if (cterm_big_music_snapshot(b,&music)!=0) return;
	snprintf(value,sizeof(value),"%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
	    (unsigned)b->music_memory,(unsigned)music.sync,(unsigned)music.running,
	    (unsigned)music.position_low,(unsigned)music.position_high,
	    (unsigned)music.scratch,(unsigned)b->sync_entry.waiting,(unsigned)s.playing,
	    (unsigned)s.frequency,(unsigned)s.divisor,(unsigned)b->note.active,(unsigned)b->note.mode,
	    (unsigned)b->note.from,(unsigned)b->note.to,(unsigned)b->note.step,
	    (unsigned)b->note.delay,(unsigned)b->note.sustain,(unsigned)b->note.off,
	    (unsigned)b->effect_sound_on,(unsigned)b->effect.active,(unsigned)b->effect.selector,(unsigned)position);
	st_console_test_text(app,"cterm.big_audio",value);
	snprintf(value,sizeof(value),"%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
	    (unsigned)music.enabled,(unsigned)music.length,(unsigned)music.cached_divider,(unsigned)music.divider,
	    (unsigned)music.end_low,(unsigned)music.end_high,(unsigned)music.delay,(unsigned)music.address,
	    (unsigned)music.data,(unsigned)music.value_offset,(unsigned)b->music_command.phase,(unsigned)b->music_command.mode);
	st_console_test_text(app,"cterm.big_music",value);
}
#endif
