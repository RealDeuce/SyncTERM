/* Musicomm serial receiver, as recovered from Michael Ferrara's accessory. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_musicomm.h"
#include "cterm.h"
#include "st_time.h"
#include "io_error.h"
#include <stdlib.h>
#include <string.h>

_Static_assert(ST_IGS_PSG_RATE == AUDIO_SAMPLE_RATE, "Musicomm PSG rate");
_Static_assert(ST_IGS_PSG_CHANNELS == AUDIO_CHANNELS, "Musicomm PSG channels");
/* MUSICOMM.ACC text $012c..$03b2 initializes these 56 words. See
 * docs/MUSICOMM.md for provenance. Inactive envelope fields are retained. */
static const uint16_t preset[ST_IGS_GIST_WORDS] = {
	200,477,65535,15,1,0,0xca1b,65535,0x6666,14,0x6666,65535,0xbe2c,0,
	0,0,0,0,0,0xfffc,0x76d7,0xffe3,0xb6ba,5,0xa841,0,0,28,0x4946,
	86,0x0b6e,0xfff5,0xe31b,10,0x534b,0xffab,0xb9e3,0xfff5,0xe31b,
	50,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};
static void write_psg(void *context, unsigned reg, uint8_t value)
{
	st_igs_psg_write(context,reg,value);
}
/* Caller holds mutex. The original timer advances envelopes at 200 Hz;
 * this credit expresses the same intervals in local PCM frames. */
static void advance(struct cterm_musicomm *m, int32_t *pcm, uint64_t count)
{
	while (count != 0) {
		if (pcm == NULL && !st_igs_gist_active(&m->gist)) {
			st_igs_psg_skip(&m->psg,count);
			m->credit = (unsigned)(m->credit+(count%AUDIO_SAMPLE_RATE)*200)%AUDIO_SAMPLE_RATE;
			return;
		}
		size_t step = (AUDIO_SAMPLE_RATE-m->credit+199)/200;
		if (step > count) step = (size_t)count;
		if (pcm != NULL) {
			int16_t samples[2*((AUDIO_SAMPLE_RATE+199)/200)];
			st_igs_psg_render(&m->psg,samples,step);
			for (size_t i=0;i<2*step;++i) *pcm++ = samples[i];
		} else st_igs_psg_skip(&m->psg,step);
		count -= step; m->credit += (unsigned)step*200;
		if (m->credit >= AUDIO_SAMPLE_RATE) {
			m->credit -= AUDIO_SAMPLE_RATE;
			st_igs_gist_update(&m->gist);
		}
	}
}
static size_t source(void *context, int32_t *pcm, size_t count)
{
	struct cterm_musicomm *m = context;
	if (st_mutex_lock(&m->mutex) != ST_THREAD_SUCCESS) return 0;
	advance(m,pcm,count);
	(void)st_mutex_unlock(&m->mutex);
	return count;
}
static bool allowed(st_cterm_t *t)
{
	st_audio_runtime_t *r = t->screen.audio;
	return r != NULL && !st_audio_runtime_stopped(r)
	    && (st_audio_runtime_is_pull(r) || st_audio_runtime_allowed(r) != 0);
}
static void fallback(st_cterm_t *t)
{
	struct cterm_musicomm *m = t->musicomm;
	m->midi = false;
	bool output = allowed(t);
	int status = t->music_player == NULL ? IO_ERROR_NOT_SUPPORTED
	    : st_music_set_pcm_source(t->music_player,source,m,output);
	m->physical = status == 0 && output;
	if (status != 0 && !m->warned) {
		m->warned = true;
		cterm_log(t,ST_CTERM_LOG_WARNING,"Musicomm audio unavailable; continuing muted");
	}
}
void cterm_musicomm_poll(st_cterm_t *t)
{
	struct cterm_musicomm *m = t->musicomm;
	if (m == NULL || m->state == ST_CTERM_MUSICOMM_OFF
	    || m->state == ST_CTERM_MUSICOMM_END_DRAIN) return;
	uint64_t now;
	if (st_time_monotonic_ms(&now) != 0) return;
	if (m->physical) {
		m->local_ms = now; m->fraction = 0;
		if (allowed(t) && st_music_pcm_source_active(t->music_player,m)) return;
		(void)st_music_clear_pcm_source(t->music_player,m);
		m->physical = false;
	}
	uint64_t elapsed = now >= m->local_ms ? now-m->local_ms : 0;
	/* Bound multiplication even if the host clock jumps far ahead. */
	if (elapsed > (UINT64_MAX-999)/AUDIO_SAMPLE_RATE) elapsed = (UINT64_MAX-999)/AUDIO_SAMPLE_RATE;
	uint64_t frames = elapsed*AUDIO_SAMPLE_RATE+m->fraction;
	m->local_ms = now; m->fraction = (unsigned)(frames%1000);
	(void)st_mutex_lock(&m->mutex);
	advance(m,NULL,frames/1000);
	(void)st_mutex_unlock(&m->mutex);
	if (m->midi && !st_music_available(t->music_player)) {
		(void)st_music_flush(t->music_player,0);
		fallback(t);
	} else if (!m->midi && allowed(t)) fallback(t);
}
enum st_cterm_musicomm_state st_cterm_musicomm_state(const st_cterm_t *t)
{
	return t == NULL || t->musicomm == NULL ? ST_CTERM_MUSICOMM_OFF : t->musicomm->state;
}
int st_cterm_musicomm_start(st_cterm_t *t)
{
	if (t == NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (st_cterm_musicomm_state(t) != ST_CTERM_MUSICOMM_OFF) return IO_ERROR_BUSY;
	int status = st_cterm_finish(t);
	if (status != 0) return status;
	if (t->musicomm == NULL) {
		struct cterm_musicomm *m = calloc(1,sizeof(*m));
		if (m == NULL) return IO_ERROR_OUT_OF_MEMORY;
		if (st_mutex_init(&m->mutex,ST_MUTEX_PLAIN) != ST_THREAD_SUCCESS) {
			free(m); return IO_ERROR_IO;
		}
		t->musicomm = m;
	}
	struct cterm_musicomm *m = t->musicomm;
	status = st_time_monotonic_ms(&m->local_ms);
	if (status != 0) return status;
	(void)st_music_flush(t->music_player,0);
	if (t->music_player == NULL && t->screen.audio != NULL)
		(void)st_music_create(&t->music_player,t->screen.audio,&t->music_stream);
	memset(m->on,0,sizeof(m->on));
	m->credit = m->fraction = 0; m->warned = false;
	st_igs_gist_init(&m->gist); st_igs_psg_init(&m->psg);
	m->gist.write = write_psg; m->gist.context = &m->psg;
	m->state = ST_CTERM_MUSICOMM_WAITING;
	m->midi = st_music_available(t->music_player);
	if (!m->midi) fallback(t);
	return 0;
}
static void silence(st_cterm_t *t)
{
	struct cterm_musicomm *m = t->musicomm;
	if (m->midi) {
		uint8_t bytes[128*3];
		for (unsigned n=0;n<128;++n) {
			bytes[3*n] = 0x90; bytes[3*n+1] = (uint8_t)n; bytes[3*n+2] = 0;
		}
		if (st_music_queue_midi(t->music_player,bytes,sizeof(bytes)) == 0)
			(void)st_music_wait(t->music_player);
		(void)st_music_flush(t->music_player,0);
	} else (void)st_music_clear_pcm_source(t->music_player,m);
	m->midi = m->physical = false;
	(void)st_mutex_lock(&m->mutex);
	for (int i=0;i<3;++i) st_igs_gist_stop(&m->gist,i);
	(void)st_mutex_unlock(&m->mutex);
	memset(m->on,0,sizeof(m->on));
}
bool st_cterm_musicomm_stop(st_cterm_t *t)
{
	enum st_cterm_musicomm_state state = st_cterm_musicomm_state(t);
	if (state == ST_CTERM_MUSICOMM_OFF) return false;
	if (state == ST_CTERM_MUSICOMM_END_DRAIN) return true;
	t->musicomm->state = ST_CTERM_MUSICOMM_END_DRAIN;
	cterm_respond(t,"\xff\xff\xff",3);
	silence(t);
	return true;
}
void cterm_musicomm_cancel(st_cterm_t *t)
{
	if (st_cterm_musicomm_state(t) == ST_CTERM_MUSICOMM_OFF) return;
	t->musicomm->state = ST_CTERM_MUSICOMM_OFF;
	silence(t);
}
void cterm_musicomm_destroy(st_cterm_t *t)
{
	if (t->musicomm == NULL) return;
	cterm_musicomm_cancel(t);
	st_mutex_destroy(&t->musicomm->mutex);
	free(t->musicomm); t->musicomm = NULL;
}
void st_cterm_musicomm_idle(st_cterm_t *t)
{
	enum st_cterm_musicomm_state state = st_cterm_musicomm_state(t);
	if (state == ST_CTERM_MUSICOMM_START_DRAIN) t->musicomm->state = ST_CTERM_MUSICOMM_PLAYING;
	if (state == ST_CTERM_MUSICOMM_END_DRAIN) t->musicomm->state = ST_CTERM_MUSICOMM_OFF;
}
void cterm_musicomm_feed(st_cterm_t *t, const uint8_t *bytes, size_t size)
{
	struct cterm_musicomm *m = t->musicomm;
	cterm_musicomm_poll(t);
	for (size_t i=0;i<size;++i) {
		uint8_t n = bytes[i];
		if (m->state == ST_CTERM_MUSICOMM_WAITING) {
			if (n == 2) {
				m->state = ST_CTERM_MUSICOMM_START_DRAIN;
				cterm_respond(t,"\x02",1);
			}
		} else if (m->state == ST_CTERM_MUSICOMM_PLAYING) {
			if (n >= 128) { (void)st_cterm_musicomm_stop(t); continue; }
			m->on[n] = !m->on[n];
			(void)st_mutex_lock(&m->mutex);
			if (m->on[n]) m->channel[n] = st_igs_gist_on(&m->gist,preset,-1,14,n,1);
			else st_igs_gist_release(&m->gist,m->channel[n]);
			(void)st_mutex_unlock(&m->mutex);
			if (m->midi) {
				const uint8_t event[] = {0x90,n,m->on[n]?64:0};
				if (st_music_queue_midi(t->music_player,event,sizeof(event)) != 0) {
					(void)st_music_flush(t->music_player,0); fallback(t);
				}
			}
		}
	}
}
