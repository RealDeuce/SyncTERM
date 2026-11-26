/* Musicomm serial receiver, as recovered from Michael Ferrara's accessory. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "io_error.h"
#include "st_musicomm_internal.h"
#include "st_time.h"

_Static_assert(ST_IGS_PSG_RATE == AUDIO_SAMPLE_RATE, "Musicomm PSG rate");
_Static_assert(ST_IGS_PSG_CHANNELS == AUDIO_CHANNELS, "Musicomm PSG channels");
/* MUSICOMM.ACC text $012c..$03b2 initializes these 56 words. See
 * docs/MUSICOMM.md for provenance. Inactive envelope fields are retained. */
static const uint16_t preset[ST_IGS_GIST_WORDS] = {200, 477, 65535, 15, 1, 0, 0xca1b, 65535, 0x6666, 14, 0x6666,
    65535, 0xbe2c, 0, 0, 0, 0, 0, 0, 0xfffc, 0x76d7, 0xffe3, 0xb6ba, 5, 0xa841, 0, 0, 28, 0x4946, 86, 0x0b6e,
    0xfff5, 0xe31b, 10, 0x534b, 0xffab, 0xb9e3, 0xfff5, 0xe31b, 50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0};

static void
write_psg(void *context, unsigned reg, uint8_t value)
{
	st_igs_psg_write(context, reg, value);
}

/* Caller holds mutex. The original timer advances envelopes at 200 Hz;
 * this credit expresses the same intervals in local PCM frames. */
static void
advance(struct st_musicomm *m, int32_t *pcm, uint64_t count)
{
	while (count != 0) {
		if (pcm == NULL && !st_igs_gist_active(&m->gist)) {
			st_igs_psg_skip(&m->psg, count);
			m->credit = (unsigned)(m->credit + (count % AUDIO_SAMPLE_RATE) * 200) % AUDIO_SAMPLE_RATE;
			return;
		}
		size_t step = (AUDIO_SAMPLE_RATE - m->credit + 199) / 200;
		if (step > count)
			step = (size_t)count;
		if (pcm != NULL) {
			int16_t samples[2 * ((AUDIO_SAMPLE_RATE + 199) / 200)];
			st_igs_psg_render(&m->psg, samples, step);
			for (size_t i = 0; i < 2 * step; ++i)
				*pcm++ = samples[i];
		}
		else
			st_igs_psg_skip(&m->psg, step);
		count -= step;
		m->credit += (unsigned)step * 200;
		if (m->credit >= AUDIO_SAMPLE_RATE) {
			m->credit -= AUDIO_SAMPLE_RATE;
			st_igs_gist_update(&m->gist);
		}
	}
}

static size_t
source(void *context, int32_t *pcm, size_t count)
{
	struct st_musicomm *m = context;
	if (st_mutex_lock(&m->mutex) != ST_THREAD_SUCCESS)
		return 0;
	advance(m, pcm, count);
	(void)st_mutex_unlock(&m->mutex);
	return count;
}

static bool
allowed(struct st_musicomm *m)
{
	st_audio_runtime_t *r = m->config.audio;
	return r != NULL && !st_audio_runtime_stopped(r)
	       && (st_audio_runtime_is_pull(r) || st_audio_runtime_allowed(r) != 0);
}

static void
fallback(struct st_musicomm *m)
{
	m->midi                  = false;
	bool output              = allowed(m);
	int  status = m->config.player == NULL ? IO_ERROR_NOT_SUPPORTED
	                                       : st_music_set_pcm_source(m->config.player, source, m, output);
	m->physical = status == 0 && output;
	if (status != 0 && !m->warned) {
		m->warned = true;
		if (m->config.warning != NULL)
			m->config.warning(m->config.context, "Musicomm audio unavailable; continuing muted");
	}
}

void
st_musicomm_poll(struct st_musicomm *m)
{
	if (m == NULL || m->state == ST_MUSICOMM_OFF || m->state == ST_MUSICOMM_END_DRAIN)
		return;
	uint64_t now;
	if (st_time_monotonic_ms(&now) != 0)
		return;
	if (m->physical) {
		m->local_ms = now;
		m->fraction = 0;
		if (allowed(m) && st_music_pcm_source_active(m->config.player, m))
			return;
		(void)st_music_clear_pcm_source(m->config.player, m);
		m->physical = false;
	}
	uint64_t elapsed = now >= m->local_ms ? now - m->local_ms : 0;
	/* Bound multiplication even if the host clock jumps far ahead. */
	if (elapsed > (UINT64_MAX - 999) / AUDIO_SAMPLE_RATE)
		elapsed = (UINT64_MAX - 999) / AUDIO_SAMPLE_RATE;
	uint64_t frames = elapsed * AUDIO_SAMPLE_RATE + m->fraction;
	m->local_ms     = now;
	m->fraction     = (unsigned)(frames % 1000);
	(void)st_mutex_lock(&m->mutex);
	advance(m, NULL, frames / 1000);
	(void)st_mutex_unlock(&m->mutex);
	if (m->midi && !st_music_available(m->config.player)) {
		(void)st_music_flush(m->config.player, 0);
		fallback(m);
	}
	else if (!m->midi && allowed(m))
		fallback(m);
}

enum st_musicomm_state
st_musicomm_state(const struct st_musicomm *m)
{
	return m == NULL ? ST_MUSICOMM_OFF : m->state;
}

int
st_musicomm_start(struct st_musicomm **receiver, const struct st_musicomm_config *config)
{
	if (receiver == NULL || config == NULL || config->reply == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_musicomm_state(*receiver) != ST_MUSICOMM_OFF)
		return IO_ERROR_BUSY;
	uint64_t now;
	int      status = st_time_monotonic_ms(&now);
	if (status != 0)
		return status;
	struct st_musicomm *m = *receiver;
	if (m == NULL) {
		m = calloc(1, sizeof(*m));
		if (m == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		if (st_mutex_init(&m->mutex, ST_MUTEX_PLAIN) != ST_THREAD_SUCCESS) {
			free(m);
			return IO_ERROR_IO;
		}
		*receiver = m;
	}
	m->config   = *config;
	m->local_ms = now;
	(void)st_music_flush(m->config.player, 0);
	memset(m->on, 0, sizeof(m->on));
	m->credit = m->fraction = 0;
	m->warned               = false;
	st_igs_gist_init(&m->gist);
	st_igs_psg_init(&m->psg);
	m->gist.write   = write_psg;
	m->gist.context = &m->psg;
	m->state        = ST_MUSICOMM_WAITING;
	/* Musicomm owns its own session defaults rather than inheriting a
	 * previous file's pan, pedals, pitch, gain or renderer effect tails. */
	if (m->config.player != NULL)
		(void)st_music_volume(m->config.player, -12, -12, 0);
	m->midi = st_music_available(m->config.player);
	if (m->midi)
		m->midi = st_music_reset_midi(m->config.player) == 0;
	if (m->midi) {
		/* Select a square lead explicitly: software MIDI calibration leaves
		 * the ANSI sawtooth voice selected. Queue before any note events. */
		const uint8_t instrument[] = {0xb0, 0, 0, 0xb0, 32, 0, 0xc0, 80};
		if (st_music_queue_midi(m->config.player, instrument, sizeof(instrument)) != 0)
			m->midi = false;
	}
	if (!m->midi) {
		fallback(m);
		if (m->config.player != NULL)
			(void)st_music_volume(m->config.player, -12, -12, 0);
	}
	return 0;
}

static void
silence(struct st_musicomm *m)
{
	if (m->midi) {
		(void)st_music_flush(m->config.player, 0);
		uint8_t bytes[128 * 3];
		for (unsigned n = 0; n < 128; ++n) {
			bytes[3 * n]     = 0x90;
			bytes[3 * n + 1] = (uint8_t)n;
			bytes[3 * n + 2] = 0;
		}
		if (st_music_queue_midi(m->config.player, bytes, sizeof(bytes)) == 0)
			(void)st_music_wait(m->config.player);
		(void)st_music_reset_midi(m->config.player);
	}
	else {
		(void)st_music_clear_pcm_source(m->config.player, m);
		audio_stream_t *stream = st_music_pcm_stream(m->config.player);
		if (stream != NULL)
			(void)audio_stream_drain(stream);
	}
	m->midi = m->physical = false;
	(void)st_mutex_lock(&m->mutex);
	for (int i = 0; i < 3; ++i)
		st_igs_gist_stop(&m->gist, i);
	(void)st_mutex_unlock(&m->mutex);
	memset(m->on, 0, sizeof(m->on));
}

bool
st_musicomm_stop(struct st_musicomm *m)
{
	enum st_musicomm_state state = st_musicomm_state(m);
	if (state == ST_MUSICOMM_OFF)
		return false;
	if (state == ST_MUSICOMM_END_DRAIN)
		return true;
	m->state = ST_MUSICOMM_END_DRAIN;
	m->config.reply(m->config.context, (const uint8_t *)"\xff\xff\xff", 3);
	silence(m);
	return true;
}

void
st_musicomm_cancel(struct st_musicomm *m)
{
	if (st_musicomm_state(m) == ST_MUSICOMM_OFF)
		return;
	m->state = ST_MUSICOMM_OFF;
	silence(m);
}

void
st_musicomm_destroy(struct st_musicomm **receiver)
{
	if (receiver == NULL || *receiver == NULL)
		return;
	struct st_musicomm *m = *receiver;
	st_musicomm_cancel(m);
	st_mutex_destroy(&m->mutex);
	free(m);
	*receiver = NULL;
}

void
st_musicomm_idle(struct st_musicomm *m)
{
	enum st_musicomm_state state = st_musicomm_state(m);
	if (state == ST_MUSICOMM_START_DRAIN)
		m->state = ST_MUSICOMM_PLAYING;
	if (state == ST_MUSICOMM_END_DRAIN)
		m->state = ST_MUSICOMM_OFF;
}

void
st_musicomm_feed(struct st_musicomm *m, const void *data, size_t size)
{
	const uint8_t *bytes = data;
	st_musicomm_poll(m);
	for (size_t i = 0; i < size; ++i) {
		uint8_t n = bytes[i];
		if (m->state == ST_MUSICOMM_WAITING) {
			if (n == 2) {
				m->state = ST_MUSICOMM_START_DRAIN;
				m->config.reply(m->config.context, (const uint8_t *)"\x02", 1);
			}
		}
		else if (m->state == ST_MUSICOMM_PLAYING) {
			if (n >= 128) {
				(void)st_musicomm_stop(m);
				continue;
			}
			m->on[n] = !m->on[n];
			(void)st_mutex_lock(&m->mutex);
			if (m->on[n])
				m->channel[n] = st_igs_gist_on(&m->gist, preset, -1, 14, n, 1);
			else
				st_igs_gist_release(&m->gist, m->channel[n]);
			(void)st_mutex_unlock(&m->mutex);
			if (m->midi) {
				const uint8_t event[] = {0x90, n, m->on[n] ? 64 : 0};
				if (st_music_queue_midi(m->config.player, event, sizeof(event)) != 0) {
					(void)st_music_flush(m->config.player, 0);
					fallback(m);
				}
			}
		}
	}
}
