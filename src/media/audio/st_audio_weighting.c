/* Extracted unchanged K-weighting arithmetic from MIDI level calibration. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_audio_weighting.h"
#include "audio.h"
#include <stddef.h>
/* ITU-R BS.1770 head-response shelf and RLB high-pass, bilinear-mapped to
 * the existing 44.1-kHz mixer. Coefficients/arithmetic retain the previous
 * MIDI calibrator's order; each independent channel owns its own history. */
_Static_assert(AUDIO_SAMPLE_RATE==44100U,"Update K-weighting coefficients for the PCM rate");
double st_audio_weight_sample(struct st_audio_weighting *filter, double sample)
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
		double *state = filter->state[stage];
		double output = b[stage][0] * sample + state[0];
		state[0] = b[stage][1] * sample - a[stage][0] * output + state[1];
		state[1] = b[stage][2] * sample - a[stage][1] * output;
		sample = output;
	}
	return sample;
}
