/*
 * Copyright Rob Swindell - http://www.synchro.net/copyright.html
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "audio.h"

#include "io_error.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define AUDIO_PI 3.14159265358979323846
#define AUDIO_TWO_PI 6.28318530717958647692

static bool
valid_shape(enum audio_wave_shape shape)
{
	return shape >= AUDIO_WAVE_SINE && shape <= AUDIO_WAVE_SINE_SAW_HARM;
}

static bool
noisy_shape(enum audio_wave_shape shape)
{
	return shape == AUDIO_WAVE_SINE_HARM || shape == AUDIO_WAVE_SINE_SAW
	    || shape == AUDIO_WAVE_SINE_SAW_CHORD
	    || shape == AUDIO_WAVE_SINE_SAW_HARM;
}

static uint8_t
wrap_u8(double value)
{
	return (uint8_t)(int)value;
}

int
audio_generate_tone(int16_t *result, size_t frame_count, double frequency,
    enum audio_wave_shape shape, bool clean_edges)
{
	size_t i;
	size_t midpoint;
	double increment;
	int16_t *mono;

	if (result == NULL || frame_count == 0 || !isfinite(frequency)
	    || frequency < 0.0 || frequency > AUDIO_SAMPLE_RATE / 2.0
	    || !valid_shape(shape))
		return IO_ERROR_INVALID_ARGUMENT;
	if (frame_count > SIZE_MAX / AUDIO_FRAME_SIZE)
		return IO_ERROR_OUT_OF_RANGE;
	if (frequency == 0.0) {
		memset(result, 0, frame_count * AUDIO_FRAME_SIZE);
		return 0;
	}
	midpoint = frame_count / 2;
	increment = AUDIO_TWO_PI * frequency / AUDIO_SAMPLE_RATE;
	mono = result;
	for (i = 0; i < frame_count; i++) {
		double position = fmod(increment * (double)i, AUDIO_TWO_PI);

		if (noisy_shape(shape)) {
			uint8_t sample = 0;

			switch (shape) {
			case AUDIO_WAVE_SINE_HARM:
				sample = wrap_u8(sin(position) * 64.0 + 128.0);
				sample = wrap_u8(sin(increment * 2.0 * i) * 24.0);
				sample = wrap_u8(sin(increment * 3.0 * i) * 16.0);
				break;
			case AUDIO_WAVE_SINE_SAW:
				sample = wrap_u8(((sin(position) * 127.0 + 128.0)
				    + (AUDIO_TWO_PI - position) * 40.5) / 2.0);
				break;
			case AUDIO_WAVE_SINE_SAW_CHORD:
				sample = wrap_u8(((sin(position) * 64.0 + 128.0)
				    + (AUDIO_TWO_PI - position) * 6.2) / 2.0);
				sample = wrap_u8(sample
				    + sin(increment * 0.5 * i) * 24.0);
				sample = wrap_u8(sample
				    + sin(increment / 3.0 * i) * 16.0);
				break;
			case AUDIO_WAVE_SINE_SAW_HARM:
				sample = wrap_u8(((sin(position) * 64.0 + 128.0)
				    + (AUDIO_TWO_PI - position) * 6.2) / 2.0);
				sample = wrap_u8(sample
				    + sin(increment * 2.0 * i) * 24.0);
				sample = wrap_u8(sample
				    + sin(increment * 3.0 * i) * 16.0);
				break;
			default:
				break;
			}
			mono[i] = (int16_t)(((int)sample - 128) * 256);
		}
		else {
			double value;

			switch (shape) {
			case AUDIO_WAVE_SINE:
				value = sin(position) * 32767.0;
				break;
			case AUDIO_WAVE_SAWTOOTH:
				value = 32767.0
				    - position / AUDIO_TWO_PI * 65535.0;
				break;
			case AUDIO_WAVE_SQUARE:
				value = position < AUDIO_PI ? 32767.0 : -32768.0;
				break;
			default:
				value = 0.0;
				break;
			}
			mono[i] = (int16_t)value;
		}
	}

	if (clean_edges) {
		bool ending_high = mono[frame_count - 1] > 0;
		size_t crossings_needed = (size_t)(frequency / 100.0);

		i = frame_count - 1;
		if (mono[i] != 0) {
			for (; i > midpoint; i--) {
				if ((ending_high && mono[i] < 0)
				    || (!ending_high && mono[i] > 0))
					break;
				mono[i] = 0;
			}
		}
		if (crossings_needed > 0) {
			size_t crossings = 0;

			for (i = 0; i + 1 < frame_count; i++) {
				size_t divisor;

				if (((mono[i] < 0 && mono[i + 1] >= 0)
				    || (mono[i] > 0 && mono[i + 1] <= 0))
				    && i > 2 && ++crossings >= crossings_needed)
					break;
				divisor = crossings_needed - crossings;
				mono[i] = (int16_t)(mono[i]
				    / (int64_t)(divisor * divisor));
			}
			crossings = 0;
			for (i = frame_count - 1; i > 0; i--) {
				size_t divisor;

				if (((mono[i] < 0 && mono[i - 1] >= 0)
				    || (mono[i] > 0 && mono[i - 1] <= 0))
				    && i > 2 && ++crossings >= crossings_needed)
					break;
				divisor = crossings_needed - crossings;
				mono[i] = (int16_t)(mono[i]
				    / (int64_t)(divisor * divisor));
			}
		}
	}
	for (i = frame_count; i-- > 0;) {
		int16_t value = mono[i];

		result[i * 2] = value;
		result[i * 2 + 1] = value;
	}
	return 0;
}
