/* Native b/n speaker source: local audio frames, no PIT/BIOS clock model. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <string.h>

#include "io_error.h"
#include "st_big_speaker.h"
/* Fixed source gain measured against ANSI/beep through the -12dB mixer
 * path, including its soft clipping. See the test README for provenance.
 * Integer scaling is deterministic and adds no runtime calibration state. */
#define SPEAKER_GAIN_NUMERATOR   INT64_C(279224085)
#define SPEAKER_GAIN_DENOMINATOR INT64_C(1000000000)

bool
st_big_speaker_frequency(struct st_big_speaker *s, uint16_t frequency)
{
	if (s == NULL || frequency <= 18)
		return false;
	uint16_t divisor = (uint16_t)(UINT32_C(1193181) / frequency);
	if (!s->playing)
		s->phase = 0;
	s->frequency = frequency;
	s->divisor   = divisor;
	/* Convert the actual programmed frequency to a local sample step.
	 * No oscillator clock/CPU counter is advanced or exposed here. */
	s->step    = (UINT64_C(1193181) << 32) / ((uint64_t)divisor * ST_BIG_SPEAKER_RATE);
	s->playing = true;
	return true;
}

void
st_big_speaker_stop(struct st_big_speaker *s)
{
	if (s != NULL)
		s->playing = false;
}

static int32_t
next(struct st_big_speaker *s)
{
	int32_t input = 0;
	if (s->playing) {
		uint64_t remaining = s->step;
		int64_t  area      = 0;
		while (remaining != 0) {
			bool     high  = s->phase < UINT32_C(0x80000000);
			uint64_t until = (high ? UINT64_C(0x80000000) : (UINT64_C(1) << 32)) - s->phase;
			uint64_t span  = remaining < until ? remaining : until;
			area += (int64_t)span * (high ? 32767 : -32768);
			s->phase = (uint32_t)((uint64_t)s->phase + span);
			remaining -= span;
		}
		input = (int32_t)(area / (int64_t)s->step);
	}
	/* Average all transitions within the native sample first, then apply
	 * a carried one-pole alpha1/2 low-pass. This rounds each square edge;
	 * it does not add a per-note attack/release or reset between buffers. */
	s->low += ((int64_t)input * 65536 - s->low) / 2;
	return (int32_t)(s->low / 65536);
}

int
st_big_speaker_init(struct st_big_speaker *s)
{
	if (s == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	memset(s, 0, sizeof(*s));
	return 0;
}

int
st_big_speaker_render(struct st_big_speaker *s, int16_t *stereo, size_t count)
{
	if (s == NULL || (stereo == NULL && count != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	if (count > SIZE_MAX / (2 * sizeof(*stereo)))
		return IO_ERROR_OUT_OF_RANGE;
	if (s->playing && s->step == 0)
		return IO_ERROR_PROTOCOL;
	for (size_t i = 0; i < count; ++i) {
		int16_t value = (int16_t)((int64_t)next(s) * SPEAKER_GAIN_NUMERATOR / SPEAKER_GAIN_DENOMINATOR);
		stereo[2 * i] = stereo[2 * i + 1] = value;
	}
	return 0;
}

void
st_big_speaker_skip(struct st_big_speaker *s, uint64_t count)
{
	if (s == NULL)
		return;
	if (s->playing)
		s->phase = (uint32_t)((uint64_t)s->phase + (uint64_t)(uint32_t)s->step * (uint32_t)count);
	s->low = 0;
}
