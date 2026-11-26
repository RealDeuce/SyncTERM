/* Fixed 44.1-kHz level measurement shared by native synthesis calibration. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_AUDIO_WEIGHTING_H_
#define ST_AUDIO_WEIGHTING_H_

/* Zero-initialize one value per independent signal/channel. Measurement
 * history only; this filter never equalizes the signal sent to a device. */
struct st_audio_weighting {
	double state[2][2];
};

/* Caller owns/serializes the state and supplies finite PCM amplitudes. */
double st_audio_weight_sample(struct st_audio_weighting *, double sample);
#endif
