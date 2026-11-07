/* Deterministic calibration: no synth library, MIDI device or audio output. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "music_internal.h"
#include <math.h>
#include <stdlib.h>
#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%d: %s\n", __LINE__, #e); exit(1); } } while (0)
struct source { int amplitude, frequency, dc[AUDIO_CHANNELS], tail, program, restored; bool active, probed, fail_send, fail_render; size_t frames, fail_at; };
static int send(void *context, const uint8_t *bytes, size_t size)
{
	struct source *s = context;
	if (s->fail_send) return IO_ERROR_IO;
	if (size == 2 && bytes[0] == 0xc0) {
		s->program = bytes[1];
		if (s->probed && bytes[1] == 81) { CHECK(!s->active); ++s->restored; }
	}
	if (size == 3 && bytes[0] == 0x90) {
		CHECK(s->program == 81 && bytes[1] == 69 && bytes[2] == 100);
		s->active = true; s->probed = true;
	}
	if (size == 3 && bytes[0] == 0xb0 && bytes[1] == 120) s->active = false;
	return 0;
}
static int render(void *context, int16_t *frames, size_t count)
{
	struct source *s = context;
	if (s->fail_render || (s->fail_at != 0 && s->frames >= s->fail_at)) return IO_ERROR_IO;
	for (size_t i = 0; i < count * AUDIO_CHANNELS; ++i) {
		double phase = 0x1.921fb54442d18p+2 * (s->frequency != 0 ? s->frequency : 1000)
		    * (s->frames + i / AUDIO_CHANNELS) / AUDIO_SAMPLE_RATE;
		int wave = (int)round(s->amplitude * sin(phase));
		frames[i] = s->active ? wave + s->dc[i % AUDIO_CHANNELS] : s->tail;
	}
	s->frames += count;
	return 0;
}
static float reference(int amplitude, int frequency, int left_dc, int right_dc)
{
	struct source source = {.amplitude = amplitude, .frequency = frequency, .dc = {left_dc, right_dc}};
	struct st_midi_sink sink = {.state = &source, .send = send, .render = render};
	CHECK(st_midi_sink_calibrate(&sink) == 0);
	CHECK(source.probed && !source.active && source.restored == 1 && source.program == 81);
	CHECK(source.frames >= 2U * AUDIO_SAMPLE_RATE && source.frames < 3U * AUDIO_SAMPLE_RATE);
	return sink.pcm_gain_db;
}
static float calibrate(int amplitude) { return reference(amplitude, 1000, 0, 0); }
int main(void)
{
	/* Independent libebur128 references at 44.1 kHz: classic -8.421789,
	 * 1000-unit sine at 1 kHz -30.299550, at 4 kHz -27.027139 LKFS.
	 * Their differences pin frequency weighting, rather than power alone. */
	float gain = calibrate(1000);
	CHECK(fabsf(gain - 21.877760f) < 0.02f);
	CHECK(fabsf(reference(1000, 4000, 0, 0) - 18.605350f) < 0.02f);
	CHECK(fabsf(reference(1000, 440, 0, 0) - 22.566189f) < 0.02f);
	CHECK(fabsf(gain - calibrate(2000) - 6.0206f) < 0.001f);
	/* Remove DC independently in each channel: opposite offsets cancel
	 * in a combined mean, but neither channel's bias is audible signal. */
	CHECK(fabsf(gain - reference(1000, 1000, 5000, -5000)) < 0.001f);
	CHECK(fabsf(gain - reference(1000, 1000, 12000, 7000)) < 0.001f);
	CHECK(reference(0, 1000, 12000, -7000) == 0);
	CHECK(calibrate(0) == 0 && calibrate(1) == 0 && calibrate(4) == 0);
	CHECK(calibrate(10) == 48); /* Bound gain for an unusually quiet bank. */
	struct source source = {.fail_send = true};
	struct st_midi_sink sink = {.state = &source, .send = send, .render = render};
	CHECK(st_midi_sink_calibrate(&sink) == IO_ERROR_IO && !source.active && source.restored == 0);
	source.fail_send = false; source.fail_render = true;
	CHECK(st_midi_sink_calibrate(&sink) == IO_ERROR_IO && !source.active && source.restored == 0);
	source = (struct source){.fail_at = 2U * AUDIO_SAMPLE_RATE};
	CHECK(st_midi_sink_calibrate(&sink) == IO_ERROR_IO && !source.active && source.restored == 0);
	source = (struct source){.amplitude = 1000, .tail = 10};
	CHECK(st_midi_sink_calibrate(&sink) == 0 && source.restored == 1);
	CHECK(source.frames >= 10U * AUDIO_SAMPLE_RATE && source.frames < 11U * AUDIO_SAMPLE_RATE);
	source.frames = 0; sink.render = NULL;
	CHECK(st_midi_sink_calibrate(&sink) == 0 && source.frames == 0 && source.restored == 1);
	return 0;
}
