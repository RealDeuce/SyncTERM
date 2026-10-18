/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "audio.h"
#include "io_error.h"
#include "st_md5.h"

#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int
main(void)
{
	int16_t frames[1000 * AUDIO_CHANNELS];
	static int16_t overflow_frames[50000 * AUDIO_CHANNELS];
	uint8_t digest[16];
	/* The same v1.10a little-endian PCM oracle used by the APC test. */
	static const struct {
		enum audio_wave_shape shape;
		double frequency;
		size_t count;
		const char *digest;
	} cases[] = {
		{AUDIO_WAVE_SINE, 440, 600, "b31b93d98b0a346e5356996d54d882af"},
		{AUDIO_WAVE_SAWTOOTH, 440, 600, "170eca44f4c74f5b4c7ebbcd6c89c3cd"},
		{AUDIO_WAVE_SQUARE, 440, 600, "12a90ffffe3f514ee414628062703231"},
		{AUDIO_WAVE_SINE_HARM, 440, 600, "a1baa8b4226cf585898bf6b0b6bceb1b"},
		{AUDIO_WAVE_SINE_SAW, 440, 600, "9bd8d735c988b66ba5a5668198e8c718"},
		{AUDIO_WAVE_SINE_SAW_CHORD, 440, 600, "dc910958ff7b71c8992c5ef4e6afdc50"},
		{AUDIO_WAVE_SINE_SAW_HARM, 440, 600, "f5904a7db10e00480cbf55ed4351a636"},
		{AUDIO_WAVE_SAWTOOTH, 1000, 1000, "885195505c054ed2430fd4f4fd88d04f"},
		{AUDIO_WAVE_SQUARE, 30000, 600, "5be28002868969cd4512b0e91a389a52"},
		{AUDIO_WAVE_SINE, 0, 220, "e2bdd4d017ce36dec632e386e894a4e5"},
		{AUDIO_WAVE_SINE, 440, 200, "7c4e56b4f98b748c2a26b84c265d761b"},
	};
	struct st_md5 hash;

	if (audio_generate_tone(frames, 1000, DBL_MAX, AUDIO_WAVE_SINE, true)
	    != IO_ERROR_OUT_OF_RANGE
	    || audio_generate_tone(overflow_frames, 50000, DBL_MAX / 10,
	    AUDIO_WAVE_SINE_SAW_HARM, true) != IO_ERROR_OUT_OF_RANGE) {
		fprintf(stderr, "synthesis overflow validation failed; FLT_EVAL_METHOD=%d\n",
		    FLT_EVAL_METHOD);
		return 1;
	}
	for (size_t test = 0; test < sizeof(cases) / sizeof(cases[0]); ++test) {
		char hex[33];

		if (audio_generate_tone(frames, cases[test].count, cases[test].frequency,
		    cases[test].shape, true) != 0)
			return 1;
		st_md5_init(&hash);
		for (size_t i = 0; i < cases[test].count * AUDIO_CHANNELS; ++i) {
			uint16_t value = (uint16_t)frames[i];
			uint8_t bytes[2] = {(uint8_t)value, (uint8_t)(value >> 8)};

			st_md5_update(&hash, bytes, sizeof(bytes));
		}
		st_md5_finish(&hash, digest);
		for (size_t i = 0; i < sizeof(digest); ++i)
			(void)snprintf(hex + i * 2, sizeof(hex) - i * 2, "%02x", digest[i]);
		if (strcmp(hex, cases[test].digest) != 0) {
			fprintf(stderr, "PCM case=%zu differs; FLT_EVAL_METHOD=%d expected=%s actual=%s\n",
			    test, FLT_EVAL_METHOD, cases[test].digest, hex);
			return 1;
		}
	}
	return 0;
}
