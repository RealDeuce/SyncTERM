/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_audio.h"
#include "st_audio_file.h"
#include "filesystem.h"
#include "st_threads.h"
#include "io_error.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct st_rip3_audio {
	audio_engine_t *engine;
	audio_stream_t *stream;
	audio_stream_t *tone_stream;
	/* Shared only with this player's existing mixer consumer callbacks. */
	st_mutex_t mutex;
	int16_t *frames;
	size_t count, position;
	struct st_rip3_tones tones;
	uint32_t tone_frames, phase, millisecond_remainder;
	uint16_t frequency;
};

static unsigned
le16(const uint8_t *p)
{
	return (unsigned)p[0] | (unsigned)p[1] << 8;
}

static uint32_t
le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8
	    | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int
read_at(FILE *file, uint64_t offset, void *bytes, size_t size)
{
	int status = fs_stream_seek(file, (int64_t)offset);
	if (status != 0)
		return status;
	if (fread(bytes, 1, size, file) != size)
		return ferror(file) ? IO_ERROR_IO : IO_ERROR_INVALID_FORMAT;
	return 0;
}

static int
validate_wave(FILE *file)
{
	uint8_t header[16];
	int64_t size = fs_stream_size(file);
	uint64_t end, offset;
	unsigned alignment = 0;
	bool format = false, data = false;
	int status;

	if (size < 0)
		return IO_ERROR_IO;
	if (size < 12)
		return IO_ERROR_INVALID_FORMAT;
	status = read_at(file, 0, header, 12);
	if (status != 0)
		return status;
	if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0)
		return IO_ERROR_INVALID_FORMAT;
	end = (uint64_t)le32(header + 4) + 8;
	if (end != (uint64_t)size || end < 12)
		return IO_ERROR_INVALID_FORMAT;
	for (offset = 12; offset < end;) {
		uint64_t payload, next;
		uint32_t length;

		if (end - offset < 8)
			return IO_ERROR_INVALID_FORMAT;
		status = read_at(file, offset, header, 8);
		if (status != 0)
			return status;
		length = le32(header + 4);
		payload = offset + 8;
		next = payload + length + (length & 1);
		if (next > end)
			return IO_ERROR_INVALID_FORMAT;
		if (memcmp(header, "fmt ", 4) == 0) {
			unsigned channels, bits;
			uint32_t rate, average;

			if (format || data || length < 16)
				return IO_ERROR_INVALID_FORMAT;
			status = read_at(file, payload, header, 16);
			if (status != 0)
				return status;
			if (le16(header) != 1)
				return IO_ERROR_NOT_SUPPORTED;
			channels = le16(header + 2); rate = le32(header + 4);
			average = le32(header + 8); alignment = le16(header + 12);
			bits = le16(header + 14);
			if (bits != 8 && bits != 16 && bits != 24 && bits != 32)
				return IO_ERROR_NOT_SUPPORTED;
			if (channels == 0 || rate == 0 || alignment == 0
			    || (uint64_t)channels * (bits / 8) != alignment
			    || (uint64_t)rate * alignment != average)
				return IO_ERROR_INVALID_FORMAT;
			format = true;
		}
		else if (memcmp(header, "data", 4) == 0) {
			if (!format || data || length % alignment != 0)
				return IO_ERROR_INVALID_FORMAT;
			data = true;
		}
		offset = next;
	}
	return format && data ? 0 : IO_ERROR_INVALID_FORMAT;
}

static size_t
source(void *context, int16_t *output, size_t requested)
{
	struct st_rip3_audio *player = context;
	size_t count;

	if (st_mutex_lock(&player->mutex) != ST_THREAD_SUCCESS)
		return 0;
	count = player->count - player->position;
	if (count > requested)
		count = requested;
	if (count != 0)
		memcpy(output, player->frames + player->position * AUDIO_CHANNELS,
		    count * AUDIO_FRAME_SIZE);
	player->position += count;
	(void)st_mutex_unlock(&player->mutex);
	return count;
}

int
st_rip3_audio_create(struct st_rip3_audio **result, audio_engine_t *engine,
    float left, float right)
{
	struct st_rip3_audio *player;
	int status;

	if (result == NULL || engine == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	player = calloc(1, sizeof(*player));
	if (player == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	if (st_mutex_init(&player->mutex, ST_MUTEX_PLAIN) != ST_THREAD_SUCCESS) {
		free(player);
		return IO_ERROR_IO;
	}
	player->engine = engine;
	status = audio_stream_create(&player->stream, engine, left, right);
	if (status == 0)
		status = audio_stream_set_source(player->stream, source, player);
	if (status != 0) {
		st_rip3_audio_destroy(player);
		return status;
	}
	*result = player;
	return 0;
}

void
st_rip3_audio_destroy(struct st_rip3_audio *player)
{
	if (player == NULL)
		return;
	/* Destroy waits for the current callback; no consumer can touch the PCM
	 * or mutex after this returns. The borrowed engine remains client-owned. */
	audio_stream_destroy(player->stream);
	audio_stream_destroy(player->tone_stream);
	free(player->frames);
	st_mutex_destroy(&player->mutex);
	free(player);
}

int
st_rip3_audio_stop(struct st_rip3_audio *player)
{
	int status;

	if (player == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	/* Quiesce without holding the callback's lock. */
	status = audio_stream_clear(player->stream);
	if (st_mutex_lock(&player->mutex) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	free(player->frames);
	player->frames = NULL; player->count = player->position = 0;
	(void)st_mutex_unlock(&player->mutex);
	return status;
}

int
st_rip3_audio_play(struct st_rip3_audio *player,
    const struct st_rip3_resource_paths *paths, const char *filename)
{
	struct st_rip3_resource resource = {0};
	int16_t *frames = NULL;
	size_t count;
	int status;

	if (player == NULL || filename == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (strcmp(filename, "$OFF$") == 0)
		return st_rip3_audio_stop(player);
	status = st_rip3_resource_open(&resource, paths, ST_RIP3_RESOURCE_MEDIA, filename);
	if (status == IO_ERROR_NOT_FOUND)
		return 0;
	if (status != 0)
		return status;
	status = validate_wave(resource.stream);
	if (status == 0)
		status = st_audio_file_decode_stream(&frames, &count, resource.stream);
	st_rip3_resource_dispose(&resource);
	if (status != 0)
		return status;
	if (st_mutex_lock(&player->mutex) != ST_THREAD_SUCCESS) {
		free(frames);
		return IO_ERROR_IO;
	}
	free(player->frames);
	player->frames = frames; player->count = count; player->position = 0;
	(void)st_mutex_unlock(&player->mutex);
	return audio_stream_source_active(player->stream, true);
}

static size_t
tone_source(void *context, int16_t *output, size_t requested)
{
	struct st_rip3_audio *player = context;
	if (st_mutex_lock(&player->mutex) != ST_THREAD_SUCCESS)
		return 0;
	size_t count = 0;
	while (count < requested) {
		if (player->tone_frames == 0) {
			struct st_rip3_tone note;
			if (!st_rip3_tones_next(&player->tones, &note))
				break;
			/* Carry subframe remainders across notes, with no inter-note gap. */
			uint32_t scaled = (uint32_t)note.milliseconds * AUDIO_SAMPLE_RATE + player->millisecond_remainder;
			player->tone_frames = scaled / 1000;
			player->millisecond_remainder = scaled % 1000;
			player->frequency = note.frequency;
			player->phase = 0;
			continue;
		}
		size_t frames = requested - count;
		if (frames > player->tone_frames)
			frames = player->tone_frames;
		for (size_t i = 0; i < frames; i++) {
			double radians = (0x1.921fb54442d18p+2 / AUDIO_SAMPLE_RATE) * player->phase;
			int16_t sample = (int16_t)(sin(radians) * INT16_MAX);
			*output++ = sample;
			*output++ = sample;
			player->phase = (player->phase + player->frequency) % AUDIO_SAMPLE_RATE;
		}
		player->tone_frames -= (uint32_t)frames;
		count += frames;
	}
	(void)st_mutex_unlock(&player->mutex);
	return count;
}

bool
st_rip3_audio_play_tones(struct st_rip3_audio *player, const struct st_rip3_tones *tones)
{
	if (player == NULL || tones->repeats == 0 || tones->notes[0].milliseconds == 0)
		return false;
	if (player->tone_stream == NULL) {
		int status = audio_stream_create(&player->tone_stream, player->engine, -12.0f, -12.0f);
		if (status == 0)
			status = audio_stream_set_source(player->tone_stream, tone_source, player);
		if (status != 0) {
			audio_stream_destroy(player->tone_stream);
			player->tone_stream = NULL;
			return false;
		}
	}
	if (st_rip3_audio_tones_pending(player) || st_mutex_lock(&player->mutex) != ST_THREAD_SUCCESS)
		return false;
	player->tones = *tones;
	player->tone_frames = 0;
	player->millisecond_remainder = 0;
	(void)st_mutex_unlock(&player->mutex);
	return audio_stream_source_active(player->tone_stream, true) == 0;
}

bool
st_rip3_audio_tones_pending(const struct st_rip3_audio *player)
{
	return player != NULL && !audio_stream_is_idle(player->tone_stream);
}

void
st_rip3_audio_stop_tones(struct st_rip3_audio *player)
{
	if (player != NULL && player->tone_stream != NULL)
		(void)audio_stream_clear(player->tone_stream);
}
