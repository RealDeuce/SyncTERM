/* Signed-byte SkyPix samples, converted from the original Amiga clock. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_skypix.h"
#include "cterm.h"
#include "io_error.h"
#include "st_audio_runtime.h"
#include "st_time.h"
#include <stdlib.h>
#include <string.h>

int cterm_skypix_audio_load(struct cterm_skypix *s, const void *bytes, size_t size)
{
	const uint8_t *p = bytes;
	if (p == NULL || size < 6)
		return IO_ERROR_INVALID_FORMAT;
	uint32_t count = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
	unsigned rate = (unsigned)p[4] << 8 | p[5];
	if (count == 0 || rate == 0)
		return IO_ERROR_INVALID_FORMAT;
	if (count > CTERM_SKYPIX_SAMPLE_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	if (count > size - 6)
		return IO_ERROR_INVALID_FORMAT;
	if (s->audio.playing)
		return IO_ERROR_BUSY;
	uint8_t *copy = malloc(count);
	if (copy == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	memcpy(copy, p + 6, count);
	free(s->audio.samples);
	s->audio = (struct cterm_skypix_audio){.samples = copy, .count = count,
		.period = (uint16_t)(UINT32_C(3579545) / rate)};
	return 0;
}

void cterm_skypix_audio_cancel(struct cterm_skypix *s)
{
	/* Channel 1 belongs to CTerm. Only an active SkyPix play cancels its
	 * pending effects; ordinary resets with no SkyPix sound leave it alone. */
	if (s->audio.playing && s->terminal->fx_stream != NULL)
		(void)audio_stream_clear(s->terminal->fx_stream);
	s->audio.playing = s->audio.draining = false;
	s->audio.repeats = 0;
	s->audio.phase = s->audio.drain_until = 0;
}

void cterm_skypix_audio_reset(struct cterm_skypix *s)
{
	cterm_skypix_audio_cancel(s);
	free(s->audio.samples);
	memset(&s->audio, 0, sizeof(s->audio));
}

static int amplitude(uint8_t sample)
{
	return (sample < 128 ? (int)sample : (int)sample - 256) * 256;
}

size_t cterm_skypix_audio_convert(struct cterm_skypix_audio *a, int16_t *frames, size_t capacity)
{
	const uint64_t denominator = (uint64_t)a->period * AUDIO_SAMPLE_RATE;
	const uint64_t end = (uint64_t)a->count * denominator;
	size_t written = 0;
	/* Phase stays within one pass, even for 65535 repeats. At the 4 MiB
	 * input bound, phase and interpolation products fit unsigned/signed 64
	 * bits respectively. Never multiply all repetitions into a duration. */
	while (written < capacity && a->repeats != 0) {
		size_t index = (size_t)(a->phase / denominator);
		uint64_t fraction = a->phase % denominator;
		size_t next = index + 1;
		if (next == a->count)
			next = a->repeats > 1 ? 0 : index;
		int first = amplitude(a->samples[index]);
		int64_t product = (int64_t)(amplitude(a->samples[next]) - first) * (int64_t)fraction;
		/* Explicit floor rounding, including descending waveforms. */
		int64_t delta = product >= 0 ? product / (int64_t)denominator
			: -((-product + (int64_t)denominator - 1) / (int64_t)denominator);
		frames[written * 2] = frames[written * 2 + 1] = (int16_t)(first + delta);
		++written;
		a->phase += UINT32_C(3579545);
		while (a->phase >= end && a->repeats != 0) {
			a->phase -= end;
			--a->repeats;
		}
	}
	return written;
}

static bool enabled(struct cterminal *t)
{
	return t->screen.audio != NULL && !st_audio_runtime_stopped(t->screen.audio)
		&& (st_audio_runtime_is_pull(t->screen.audio) || st_audio_runtime_allowed(t->screen.audio) != 0);
}

void cterm_skypix_audio_poll(struct cterm_skypix *s, uint64_t now)
{
	struct cterm_skypix_audio *a = &s->audio;
	if (!a->playing)
		return;
	if (!enabled(s->terminal)) {
		cterm_skypix_audio_cancel(s);
		return;
	}
	audio_stream_t *stream = s->terminal->fx_stream;
	size_t queued = audio_stream_buffered_frames(stream);
	if (a->repeats != 0 && queued < 8192) {
		int16_t frames[4096 * AUDIO_CHANNELS];
		size_t capacity = 8192 - queued;
		if (capacity > 4096) capacity = 4096;
		size_t count = cterm_skypix_audio_convert(a, frames, capacity);
		if (audio_stream_append(stream, frames, count, NULL) != 0) {
			cterm_log(s->terminal, ST_CTERM_LOG_WARNING, "SkyPix audio output failed");
			cterm_skypix_audio_cancel(s);
			return;
		}
	}
	if (a->repeats == 0 && audio_stream_is_idle(stream)) {
		if (!a->draining) {
			a->draining = true;
			a->drain_until = now + audio_stream_latency_ms(stream);
		}
		if (now >= a->drain_until) {
			a->playing = false;
			a->draining = false;
		}
	}
}

void cterm_skypix_audio_play(struct cterm_skypix *s, unsigned repeats)
{
	if (s->audio.samples == NULL || s->audio.playing || !enabled(s->terminal)
		|| cterm_fx_stream(s->terminal) == NULL)
		return;
	uint64_t now;
	if (st_time_monotonic_ms(&now) != 0)
		return;
	s->audio.phase = s->audio.drain_until = 0;
	s->audio.draining = false;
	s->audio.repeats = repeats == 0 ? 1 : repeats;
	s->audio.playing = true;
	cterm_skypix_audio_poll(s, now);
}
