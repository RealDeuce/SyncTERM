/* Authored native speaker/level expectations: BIG7/14/66 and approved timbre. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "io_error.h"
#include "st_audio_weighting.h"
#include "st_big_speaker.h"
#define CHECK(x)                                                                \
	do {                                                                    \
		if (!(x)) {                                                     \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); \
			exit(1);                                                \
		}                                                               \
	} while (0)

static void
weighting_impulse(void)
{
	/* Independent polynomial expansion of the two section transfer
	 * functions, evaluated in60-digit decimal arithmetic. */
	static const double       expected[] = {1.5308412300503478, -0.12076766534074256, -0.11044104456741488,
	    -0.0981722183651433, -0.08511577612662963, -0.07213304847785137, -0.05983423719766489,
	    -0.04862072713202837};
	struct st_audio_weighting p = {0}, other = {0};
	for (unsigned i = 0; i < 8; ++i) {
		CHECK(fabs(st_audio_weight_sample(&p, i ? 0 : 1) - expected[i]) < 1e-12);
		CHECK(st_audio_weight_sample(&other, 0) == 0);
	}
}

static void
literal_samples(void)
{
	struct st_big_speaker s;
	CHECK(st_big_speaker_init(&s) == 0);
	CHECK(!s.playing && s.phase == 0 && s.divisor == 0 && s.low == 0);
	struct st_big_speaker before;
	memcpy(&before, &s, sizeof(s));
	CHECK(!st_big_speaker_frequency(&s, 0) && !st_big_speaker_frequency(&s, 18));
	CHECK(memcmp(&s, &before, sizeof(s)) == 0);
	CHECK(st_big_speaker_frequency(&s, 19) && s.divisor == 62799 && s.playing);
	int16_t pcm[8];
	CHECK(st_big_speaker_render(&s, pcm, 3) == 0);
	CHECK(pcm[0] == 4574 && pcm[2] == 6861 && pcm[4] == 8005);
	for (unsigned i = 0; i < 3; ++i)
		CHECK(pcm[2 * i] == pcm[2 * i + 1]);
	uint32_t phase = s.phase;
	CHECK(st_big_speaker_frequency(&s, 440) && s.divisor == 2711 && s.phase == phase);
	memcpy(&before, &s, sizeof(s));
	CHECK(!st_big_speaker_frequency(&s, 16) && memcmp(&s, &before, sizeof(s)) == 0);
	st_big_speaker_stop(&s);
	CHECK(!s.playing && s.phase == phase);
	CHECK(st_big_speaker_render(&s, pcm, 1) == 0 && pcm[0] == 4002 && s.phase == phase);
	CHECK(st_big_speaker_frequency(&s, 440) && s.phase == 0); /* Gated restart only. */
	CHECK(st_big_speaker_frequency(&s, 65535) && s.divisor == 18);
	CHECK(st_big_speaker_init(&s) == 0);
	s.playing = true;
	s.step    = UINT64_C(1) << 30;
	CHECK(st_big_speaker_render(&s, pcm, 4) == 0);
	static const int16_t four[] = {4574, 6861, -1143, -5146};
	for (unsigned i = 0; i < 4; ++i)
		CHECK(pcm[2 * i] == four[i]);
	CHECK(s.phase == 0);
	/* One native sample straddling equal high/low halves averages before
	 * low-pass quantization. No sample-boundary hard-square alias is added. */
	s.low   = 0;
	s.phase = UINT32_C(0x60000000);
	CHECK(st_big_speaker_render(&s, pcm, 1) == 0 && pcm[0] == 0 && s.phase == UINT32_C(0xa0000000));
	s.low   = 0;
	s.phase = 0;
	s.step  = UINT64_C(3) << 32;
	CHECK(st_big_speaker_render(&s, pcm, 1) == 0 && pcm[0] == 0 && s.phase == 0);
}

static void
partitions_skip_errors(void)
{
	struct st_big_speaker p, q;
	CHECK(st_big_speaker_init(&p) == 0);
	CHECK(st_big_speaker_frequency(&p, 440));
	memcpy(&q, &p, sizeof(p));
	int16_t a[2048], b[2048];
	CHECK(st_big_speaker_render(&p, a, 1024) == 0);
	for (unsigned i = 0; i < 1024; ++i)
		CHECK(st_big_speaker_render(&q, b + 2 * i, 1) == 0);
	CHECK(memcmp(a, b, sizeof(a)) == 0 && memcmp(&p, &q, sizeof(p)) == 0);
	CHECK(st_big_speaker_frequency(&p, 65535));
	uint64_t count    = 1000007;
	uint32_t expected = (uint32_t)((uint64_t)p.phase + p.step * count);
	st_big_speaker_skip(&p, count);
	CHECK(p.phase == expected && p.low == 0 && p.playing);
	expected = (uint32_t)((uint64_t)p.phase + (UINT64_C(1) << 32) - (uint32_t)p.step);
	st_big_speaker_skip(&p, UINT64_MAX);
	CHECK(p.phase == expected && p.low == 0 && p.playing);
	st_big_speaker_stop(&p);
	expected = p.phase;
	st_big_speaker_skip(&p, UINT64_MAX);
	CHECK(p.phase == expected && p.low == 0 && !p.playing);
	memcpy(&q, &p, sizeof(p));
	CHECK(st_big_speaker_render(&p, NULL, 1) == IO_ERROR_INVALID_ARGUMENT && memcmp(&p, &q, sizeof(p)) == 0);
	CHECK(st_big_speaker_render(&p, NULL, 0) == 0 && memcmp(&p, &q, sizeof(p)) == 0);
	CHECK(st_big_speaker_render(&p, a, SIZE_MAX) == IO_ERROR_OUT_OF_RANGE && memcmp(&p, &q, sizeof(p)) == 0);
	CHECK(st_big_speaker_init(NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_speaker_render(NULL, a, 1) == IO_ERROR_INVALID_ARGUMENT);
}

static void
levels_and_harmonics(void)
{
	const size_t count     = 2 * AUDIO_SAMPLE_RATE;
	int16_t     *reference = malloc(count * AUDIO_FRAME_SIZE), *pcm = malloc(count * AUDIO_FRAME_SIZE);
	CHECK(reference && pcm);
	CHECK(audio_generate_tone(reference, count, 440, AUDIO_WAVE_SINE_SAW_HARM, true) == 0);
	struct st_big_speaker p;
	CHECK(st_big_speaker_init(&p) == 0);
	CHECK(st_big_speaker_frequency(&p, 440) && st_big_speaker_render(&p, pcm, count) == 0);
	struct st_audio_weighting r = {0}, s = {0};
	double                    rp = 0, sp = 0;
	int                       peak = 0;
	for (size_t i = 0; i < count; ++i) {
		double a = st_audio_weight_sample(&r, reference[2 * i]),
		       b = st_audio_weight_sample(&s, pcm[2 * i]);
		if (i >= AUDIO_SAMPLE_RATE / 2 && i < 3 * AUDIO_SAMPLE_RATE / 2) {
			rp += a * a;
			sp += b * b;
		}
		CHECK(pcm[2 * i] == pcm[2 * i + 1]);
		int v = abs(pcm[2 * i]);
		if (v > peak)
			peak = v;
	}
	/* The raw level includes compensation for the mixer's soft clip;
	 * the strict equality target is checked after the actual mixer below. */
	CHECK(rp > 0 && sp > 0 && fabs(10 * log10(sp / rp)) < 0.25 && peak < 32767);
	printf("speaker fixed gain0.279224085; weighted raw difference %+.6f dB; peak %d\n", 10 * log10(sp / rp),
	    peak);
	/* Recheck the level through the real device-free mixer at channel1's
	 * -12dB base. Streams are sequentially owned/destroyed; no physical
	 * output or simultaneous reference tone enters the capture. */
	audio_engine_t *engine = NULL;
	audio_stream_t *stream = NULL;
	CHECK(audio_engine_create_pull(&engine) == 0);
	CHECK(audio_stream_create(&stream, engine, -12, -12) == 0);
	CHECK(audio_stream_append(stream, reference, count, NULL) == 0
	      && audio_engine_pull(engine, reference, count) == 0);
	audio_stream_destroy(stream);
	stream = NULL;
	CHECK(audio_stream_create(&stream, engine, -12, -12) == 0);
	CHECK(audio_stream_append(stream, pcm, count, NULL) == 0 && audio_engine_pull(engine, pcm, count) == 0);
	audio_stream_destroy(stream);
	audio_engine_destroy(engine);
	memset(&r, 0, sizeof(r));
	memset(&s, 0, sizeof(s));
	rp = sp = 0;
	peak    = 0;
	for (size_t i = 0; i < count; ++i) {
		double x = st_audio_weight_sample(&r, reference[2 * i]),
		       y = st_audio_weight_sample(&s, pcm[2 * i]);
		if (i >= AUDIO_SAMPLE_RATE / 2 && i < 3 * AUDIO_SAMPLE_RATE / 2) {
			rp += x * x;
			sp += y * y;
		}
		CHECK(pcm[2 * i] == pcm[2 * i + 1]);
		int v = abs(pcm[2 * i]);
		if (v > peak)
			peak = v;
	}
	CHECK(rp > 0 && sp > 0 && fabs(10 * log10(sp / rp)) < 0.05 && peak < 32767);
	printf("mixed speaker/reference difference %+.6f dB; mixed peak %d\n", 10 * log10(sp / rp), peak);
	/* Compare against the actual phase frequency, avoiding a pitched-bin
	 * error due to the original frequency divisor's integer quantization. */
	CHECK(st_big_speaker_init(&p) == 0 && st_big_speaker_frequency(&p, 1000));
	CHECK(st_big_speaker_render(&p, pcm, count) == 0);
	double                re[3] = {0}, im[3] = {0};
	static const unsigned harmonics[] = {1, 3, 15};
	double                f           = (double)p.step * AUDIO_SAMPLE_RATE / 4294967296.0;
	for (unsigned h = 0; h < 3; ++h)
		for (size_t i = AUDIO_SAMPLE_RATE; i < count; ++i) {
			double phase = 6.2831853071795864769 * f * harmonics[h] * i / AUDIO_SAMPLE_RATE;
			re[h] += pcm[2 * i] * cos(phase);
			im[h] += pcm[2 * i] * sin(phase);
		}
	double fundamental = hypot(re[0], im[0]);
	CHECK(fundamental > 0 && hypot(re[1], im[1]) / fundamental < 1.0 / 3.0);
	CHECK(hypot(re[2], im[2]) / fundamental < 1.0 / 15.0);
	free(reference);
	free(pcm);
}

int
main(void)
{
	weighting_impulse();
	literal_samples();
	partitions_skip_errors();
	levels_and_harmonics();
	return 0;
}
