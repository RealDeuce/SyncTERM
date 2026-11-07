/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "music_internal.h"
#include <math.h>
#include <stdlib.h>

/* Match the steady RMS of a GM oboe, A440, velocity/CC7 100, to the
 * existing classic ANSI waveform. Discard attack and release. This is one
 * fixed source gain for the renderer, never normalization of playing music.
 * Probe samples are discarded here and never enter an output stream. */
int st_midi_sink_calibrate(struct st_midi_sink *sink)
{
	if (sink->render == NULL) return 0;
	const size_t duration = 2U * AUDIO_SAMPLE_RATE;
	int16_t *tone = malloc(duration * AUDIO_FRAME_SIZE);
	if (tone == NULL) return IO_ERROR_OUT_OF_MEMORY;
	int status = audio_generate_tone(tone, duration, 440,
	    AUDIO_WAVE_SINE_SAW_HARM, true);
	double reference = 0, signal = 0;
	size_t first = AUDIO_SAMPLE_RATE / 2, last = first + AUDIO_SAMPLE_RATE;
	if (status == 0) {
		for (size_t i = first * AUDIO_CHANNELS; i < last * AUDIO_CHANNELS; ++i)
			reference += (double)tone[i] * tone[i];
		status = st_midi_sink_start(sink);
	}
	free(tone);
	const uint8_t oboe[] = {0xc0, 68, 0x90, 69, 100};
	if (status == 0) status = st_midi_sink_send(sink, oboe, sizeof(oboe));
	int16_t frames[1024 * AUDIO_CHANNELS];
	for (size_t at = 0; status == 0 && at < duration;) {
		size_t count = duration - at < 1024 ? duration - at : 1024;
		status = sink->render(sink->state, frames, count);
		if (status != 0) break;
		for (size_t i = 0; i < count; ++i) {
			if (at + i < first || at + i >= last) continue;
			for (size_t ch = 0; ch < AUDIO_CHANNELS; ++ch) {
				double value = frames[i * AUDIO_CHANNELS + ch];
				signal += value * value;
			}
		}
		at += count;
	}
	st_midi_sink_release(sink, true);
	/* Drain effects into discarded buffers as well, so the reference cannot
	 * leak into the first capture. A quarter-second below two PCM LSBs is
	 * quiet; the same eight-second bound applies to every internal renderer. */
	size_t quiet = 0;
	for (size_t at = 0; status == 0 && at < 8U * AUDIO_SAMPLE_RATE && quiet < AUDIO_SAMPLE_RATE / 4; at += 1024) {
		status = sink->render(sink->state, frames, 1024);
		if (status != 0) break;
		bool silent = true;
		for (size_t i = 0; i < 1024 * AUDIO_CHANNELS; ++i)
			if (frames[i] < -2 || frames[i] > 2) silent = false;
		quiet = silent ? quiet + 1024 : 0;
	}
	if (status == 0) status = st_midi_sink_start(sink);
	if (status == 0 && sink->ansi != NULL) sink->ansi(sink->state);
	/* A bank without an audible oboe remains usable. Do not amplify dither
	 * or reject partial banks. The upper bound also limits noise gain. */
	if (status == 0 && signal > (double)AUDIO_SAMPLE_RATE * AUDIO_CHANNELS * 16) {
		double gain = 10.0 * log10(reference / signal);
		if (gain > 48) gain = 48;
		if (gain < -48) gain = -48;
		sink->pcm_gain_db = (float)gain;
	}
	return status;
}
