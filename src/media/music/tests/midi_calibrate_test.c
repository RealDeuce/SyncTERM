/* Deterministic calibration: no synth library, MIDI device or audio output. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "music_internal.h"
#include <math.h>
#include <stdlib.h>
#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%d: %s\n", __LINE__, #e); exit(1); } } while (0)
struct source { int amplitude, tail, program, restored; bool active, probed, fail_send, fail_render; size_t frames, fail_at; };
static int send(void *context, const uint8_t *bytes, size_t size)
{
	struct source *s = context;
	if (s->fail_send) return IO_ERROR_IO;
	if (size == 2 && bytes[0] == 0xc0) {
		s->program = bytes[1];
		if (s->probed && bytes[1] == 80) { CHECK(!s->active); ++s->restored; }
	}
	if (size == 3 && bytes[0] == 0x90) {
		CHECK(s->program == 80 && bytes[1] == 69 && bytes[2] == 100);
		s->active = true; s->probed = true;
	}
	if (size == 3 && bytes[0] == 0xb0 && bytes[1] == 120) s->active = false;
	return 0;
}
static int render(void *context, int16_t *frames, size_t count)
{
	struct source *s = context;
	if (s->fail_render || (s->fail_at != 0 && s->frames >= s->fail_at)) return IO_ERROR_IO;
	for (size_t i = 0; i < count * AUDIO_CHANNELS; ++i) frames[i] = s->active ? s->amplitude : s->tail;
	s->frames += count;
	return 0;
}
static float calibrate(int amplitude)
{
	struct source source = {.amplitude = amplitude};
	struct st_midi_sink sink = {.state = &source, .send = send, .render = render};
	CHECK(st_midi_sink_calibrate(&sink) == 0);
	CHECK(source.probed && !source.active && source.restored == 1 && source.program == 80);
	CHECK(source.frames >= 2U * AUDIO_SAMPLE_RATE && source.frames < 3U * AUDIO_SAMPLE_RATE);
	return sink.pcm_gain_db;
}
int main(void)
{
	/* The independent steady classic A440 oracle is RMS 16927.69 PCM units.
	 * A 1000-unit reference needs 24.571 dB; doubling it needs 6.021 less. */
	float gain = calibrate(1000);
	CHECK(fabsf(gain - 24.571f) < 0.02f);
	CHECK(fabsf(gain - calibrate(2000) - 6.0206f) < 0.001f);
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
