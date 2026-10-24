/* Synchronous libsndfile decoding to native S16 stereo, 44100 frames/second. */
/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */
#ifndef ST_AUDIO_FILE_H_
#define ST_AUDIO_FILE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Build support without attempting to load a runtime library. */
bool st_audio_file_supported(void);
bool st_audio_file_available(void);
/* major is the unshifted libsndfile container ID; subtype is its encoding ID. */
bool st_audio_file_format_available(uint32_t major, uint32_t subtype);
/* Results are unchanged on failure. Success returns malloc-owned PCM and
 * a count of stereo frames, not samples or bytes. Mono duplicates L/R;
 * multichannel input uses its first two channels. Linear interpolation
 * preserves SyncTERM's fixed-point resampling and rounding. */
int st_audio_file_decode_memory(int16_t **frames, size_t *frame_count,
	const void *data, size_t size);
/* Borrows a seekable stream for this call, decoding the whole file from
 * offset zero. Does not close it; its final position is unspecified. */
int st_audio_file_decode_stream(int16_t **frames, size_t *frame_count,
	FILE *stream);

#endif
