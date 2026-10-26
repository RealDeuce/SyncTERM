/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "st_audio_file.h"
#include "io_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); return 1; } } while (0)

static void
put32(uint8_t *p, uint32_t value)
{
	for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (i * 8));
}

static int
check_wave(unsigned channels, unsigned rate, const int16_t *samples,
	size_t count, const int16_t *expected, size_t expected_frames)
{
	uint8_t wave[256] = {0};
	size_t size = 44 + count * 2;
	int16_t *memory = NULL, *file = NULL;
	size_t memory_count = 0, file_count = 0;
	FILE *stream = tmpfile();

	CHECK(stream != NULL && size <= sizeof(wave));
	memcpy(wave, "RIFF", 4); put32(wave + 4, (uint32_t)size - 8);
	memcpy(wave + 8, "WAVEfmt ", 8); put32(wave + 16, 16);
	wave[20] = 1; wave[22] = (uint8_t)channels;
	put32(wave + 24, rate); put32(wave + 28, rate * channels * 2);
	wave[32] = (uint8_t)(channels * 2); wave[34] = 16;
	memcpy(wave + 36, "data", 4); put32(wave + 40, (uint32_t)count * 2);
	for (size_t i = 0; i < count; ++i) {
		wave[44 + i * 2] = (uint8_t)(uint16_t)samples[i];
		wave[45 + i * 2] = (uint8_t)((uint16_t)samples[i] >> 8);
	}
	CHECK(st_audio_file_decode_memory(&memory, &memory_count, wave, size) == 0);
	CHECK(memory_count == expected_frames);
	CHECK(memcmp(memory, expected, expected_frames * 2 * sizeof(int16_t)) == 0);
	CHECK(fwrite(wave, 1, size, stream) == size);
	CHECK(st_audio_file_decode_stream(&file, &file_count, stream) == 0);
	CHECK(file_count == memory_count);
	CHECK(memcmp(file, memory, file_count * 2 * sizeof(int16_t)) == 0);
	CHECK(fclose(stream) == 0);
	free(file); free(memory);
	return 0;
}

int
main(int argc, char **argv)
{
	int16_t sentinel = 123, *frames = &sentinel;
	size_t count = 321;
	int status = st_audio_file_decode_memory(&frames, &count, "bad", 3);

	CHECK(status != 0 && frames == &sentinel && count == 321);
	CHECK(st_audio_file_decode_memory(NULL, &count, "", 0) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(!st_audio_file_format_available(0, 2));
	CHECK(!st_audio_file_format_available(UINT32_MAX, 2));
	if (!st_audio_file_available()) {
		CHECK(argc == 1 || strcmp(argv[1], "--require-available") != 0);
		CHECK(status == IO_ERROR_NOT_SUPPORTED);
		CHECK(!st_audio_file_format_available(1, 2));
		return 0;
	}
	CHECK(st_audio_file_format_available(1, 2)); /* WAV, signed PCM16. */
	const int16_t mono[] = {0, 1000, -1001};
	const int16_t upsampled[] = {0,0, 500,500, 1000,1000, -1,-1, -1001,-1001, -1001,-1001};
	CHECK(check_wave(1, 22050, mono, 3, upsampled, 6) == 0);
	const int16_t stereo[] = {-32768,32767, -4,7, 1000,-2000};
	CHECK(check_wave(2, 44100, stereo, 6, stereo, 3) == 0);
	const int16_t multi[] = {1,2,3, 4,5,6};
	const int16_t first_two[] = {1,2,4,5};
	CHECK(check_wave(3, 44100, multi, 6, first_two, 2) == 0);
	return 0;
}
