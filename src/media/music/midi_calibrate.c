/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "music_internal.h"
#include <math.h>
#include <stdlib.h>

/* ITU-R BS.1770 K-weighting, bilinear-mapped to the PCM format's 44.1 kHz:
 * the head-response shelf followed by the RLB high-pass. These are two
 * independent filter states per channel, not a playback equalizer. */
_Static_assert(AUDIO_SAMPLE_RATE == 44100U, "Update K-weighting coefficients for the PCM rate");
struct weighting { double state[AUDIO_CHANNELS][2][2]; };
static double weighted(struct weighting *filter, size_t channel, double sample)
{
	static const double b[2][3] = {
		{1.5308412300503478, -2.6509799951547297, 1.1690790799215870},
		{1.0, -2.0, 1.0},
	};
	static const double a[2][2] = {
		{-1.6636551132560204, 0.7125954280732254},
		{-1.9891696736297960, 0.9891990357870393},
	};
	for (size_t stage = 0; stage < 2; ++stage) {
		double *state = filter->state[channel][stage];
		double output = b[stage][0] * sample + state[0];
		state[0] = b[stage][1] * sample - a[stage][0] * output + state[1];
		state[1] = b[stage][2] * sample - a[stage][1] * output;
		sample = output;
	}
	return sample;
}

/* Match the weighted level of the ANSI synth voice, A440, velocity/CC7 100,
 * to the existing beep/ANSI waveform. Discard attack and release. This is one
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
	double reference = 0, signal = 0, signal_ac = 0;
	double signal_sum[AUDIO_CHANNELS] = {0};
	struct weighting reference_filter = {0}, signal_filter = {0};
	size_t first = AUDIO_SAMPLE_RATE / 2, last = first + AUDIO_SAMPLE_RATE;
	if (status == 0) {
		/* Warm the filters before the measurement window. The high-pass
		 * rejects the classic waveform's large DC offset. */
		for (size_t i = 0; i < last * AUDIO_CHANNELS; ++i) {
			double value = weighted(&reference_filter, i % AUDIO_CHANNELS, tone[i]);
			if (i >= first * AUDIO_CHANNELS) reference += value * value;
		}
		status = st_midi_sink_start(sink);
	}
	free(tone);
	if (status == 0) status = st_midi_sink_ansi(sink);
	const uint8_t note[] = {0x90, 69, 100};
	if (status == 0) status = st_midi_sink_send(sink, note, sizeof(note));
	int16_t frames[1024 * AUDIO_CHANNELS];
	for (size_t at = 0; status == 0 && at < duration;) {
		size_t count = duration - at < 1024 ? duration - at : 1024;
		status = sink->render(sink->state, frames, count);
		if (status != 0) break;
		for (size_t i = 0; i < count; ++i) {
			for (size_t ch = 0; ch < AUDIO_CHANNELS; ++ch) {
				double sample = frames[i * AUDIO_CHANNELS + ch];
				double value = weighted(&signal_filter, ch, sample);
				if (at + i >= first && at + i < last) {
					signal += value * value;
					signal_ac += sample * sample;
					signal_sum[ch] += sample;
				}
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
	if (status == 0) status = st_midi_sink_ansi(sink);
	/* Keep the silence/noise floor in unweighted, DC-free PCM units. */
	for (size_t ch = 0; ch < AUDIO_CHANNELS; ++ch)
		signal_ac -= signal_sum[ch] * signal_sum[ch] / (last - first);
	/* A bank without an audible reference remains usable. Do not amplify dither
	 * or reject partial banks. The upper bound also limits noise gain. */
	if (status == 0 && signal > 0 && signal_ac > (double)AUDIO_SAMPLE_RATE * AUDIO_CHANNELS * 16) {
		double gain = 10.0 * log10(reference / signal);
		if (gain > 48) gain = 48;
		if (gain < -48) gain = -48;
		sink->pcm_gain_db = (float)gain;
	}
	return status;
}
