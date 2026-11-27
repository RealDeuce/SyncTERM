/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include <libopenmpt/libopenmpt.h>
#include <libopenmpt/libopenmpt_stream_callbacks_file.h>

#include "st_tracker.h"

#define ST_API_NAME mpt
#define ST_API_PREFIX openmpt_
#define ST_API_LINKED ST_DEP_OPENMPT_LINKED
#define ST_API_NAMES "openmpt", "libopenmpt"
#define ST_API_MAJOR 0
#define ST_API_FUNCTIONS(X) \
	X(void, free_string, (const char *)) \
	X(void, log_func_silent, (const char *, void *)) \
	X(openmpt_module *, module_create2, (openmpt_stream_callbacks, void *, openmpt_log_func, void *, openmpt_error_func, void *, int *, const char **, const openmpt_module_initial_ctl *)) \
	X(void, module_destroy, (openmpt_module *)) \
	X(float, module_get_current_channel_vu_mono, (openmpt_module *, int32_t)) \
	X(int32_t, module_get_current_order, (openmpt_module *)) \
	X(const char *, module_get_metadata, (openmpt_module *, const char *)) \
	X(int32_t, module_get_num_channels, (openmpt_module *)) \
	X(int32_t, module_get_num_orders, (openmpt_module *)) \
	X(int32_t, module_get_order_pattern, (openmpt_module *, int32_t)) \
	X(int32_t, module_get_pattern_num_rows, (openmpt_module *, int32_t)) \
	X(int, module_is_order_stop_entry, (openmpt_module *, int32_t)) \
	X(size_t, module_read_interleaved_stereo, (openmpt_module *, int32_t, size_t, int16_t *)) \
	X(double, module_set_position_order_row, (openmpt_module *, int32_t, int32_t)) \
	X(int, module_set_repeat_count, (openmpt_module *, int32_t))
#include "dynamic_library_api.inc"

bool
st_tracker_available(void)
{
	return mpt_available();
}

struct st_tracker {
	openmpt_module *module;
	audio_stream_t *stream;
	atomic_uint     volume;
	unsigned        channels;
	atomic_uchar    levels[ST_TRACKER_METER_CHANNELS];
	bool            paused;
};

static size_t
render(void *context, int16_t *pcm, size_t frames)
{
	struct st_tracker *player = context;
	size_t count  = mpt.module_read_interleaved_stereo(player->module, AUDIO_SAMPLE_RATE, frames, pcm);
	int    volume = (int)atomic_load_explicit(&player->volume, memory_order_relaxed);
	/* The decoder belongs to the mixer while running. Each track meter
	 * is independent and can be sampled by the session between blocks. */
	for (unsigned i = 0; i < player->channels; ++i) {
		float    vu    = mpt.module_get_current_channel_vu_mono(player->module, (int32_t)i);
		unsigned level = vu >= 1 ? 255 : vu > 0 ? (unsigned)(vu * 255 + 0.5f) : 0;
		atomic_store_explicit(&player->levels[i], (unsigned char)level, memory_order_relaxed);
	}
	if (volume < 64) {
		for (size_t i = 0; i < 2 * count; ++i)
			pcm[i] = (int16_t)((int32_t)pcm[i] * volume / 64);
	}
	return count;
}

void
st_tracker_destroy(struct st_tracker *player)
{
	if (player == NULL)
		return;
	audio_stream_destroy(player->stream);
	if (player->module != NULL)
		mpt.module_destroy(player->module);
	free(player);
}

struct st_tracker *
st_tracker_play(st_audio_runtime_t *audio, FILE *file, unsigned volume)
{
	if (file == NULL || volume > 64 || (!st_audio_runtime_is_pull(audio) && !st_audio_runtime_allowed(audio)))
		return NULL;
	if (!mpt_available())
		return NULL;
	struct st_tracker *player = calloc(1, sizeof(*player));
	if (player == NULL)
		return NULL;
	atomic_init(&player->volume, volume);
	static const openmpt_module_initial_ctl controls[] = {
	    {"load.skip_plugins", "1" },
            {NULL,                NULL}
        };
	player->module = mpt.module_create2(openmpt_stream_get_file_callbacks2(), file,
	    mpt.log_func_silent, NULL, NULL, NULL, NULL, NULL, controls);
	if (player->module != NULL) {
		const char *type      = mpt.module_get_metadata(player->module, "type");
		bool        supported = type != NULL && (strcmp(type, "mod") == 0 || strcmp(type, "s3m") == 0);
		mpt.free_string(type);
		int32_t channels = mpt.module_get_num_channels(player->module);
		if (channels > 0)
			player->channels =
			    channels > ST_TRACKER_METER_CHANNELS ? ST_TRACKER_METER_CHANNELS : (unsigned)channels;
		for (unsigned i = 0; i < player->channels; ++i)
			atomic_init(&player->levels[i], 0);
		audio_engine_t *engine;
		if (supported && mpt.module_set_repeat_count(player->module, -1)
		    && st_audio_runtime_acquire(audio, &engine) == 0
		    && audio_stream_create(&player->stream, engine, 0, 0) == 0
		    && audio_stream_set_source(player->stream, render, player) == 0
		    && audio_stream_source_active(player->stream, true) == 0)
			return player;
	}
	st_tracker_destroy(player);
	return NULL;
}

void
st_tracker_toggle_pause(struct st_tracker *player)
{
	if (player != NULL && audio_stream_source_active(player->stream, player->paused) == 0)
		player->paused = !player->paused;
}

static int32_t
playable_order(openmpt_module *module, int32_t count, int32_t order, int direction)
{
	while (order >= 0 && order < count && !mpt.module_is_order_stop_entry(module, order)) {
		int32_t pattern = mpt.module_get_order_pattern(module, order);
		if (mpt.module_get_pattern_num_rows(module, pattern) > 0)
			return order;
		order += direction;
	}
	return -1;
}

void
st_tracker_step_order(struct st_tracker *player, int direction)
{
	if (player == NULL || (direction != -1 && direction != 1))
		return;
	/* Quiesce the mixer's read callback before touching the decoder. */
	if (!player->paused && audio_stream_source_active(player->stream, false) != 0)
		return;
	openmpt_module *module  = player->module;
	int32_t         current = mpt.module_get_current_order(module);
	int32_t         count   = mpt.module_get_num_orders(module);
	int32_t         next    = playable_order(module, count, current + direction, direction);
	if (next < 0) {
		/* Both advancing past the end and rewinding past the start
		 * select the first pattern of the current song. */
		int32_t start = current;
		while (start > 0 && !mpt.module_is_order_stop_entry(module, start - 1))
			--start;
		next = playable_order(module, count, start, 1);
	}
	if (next >= 0)
		mpt.module_set_position_order_row(module, next, 0);
	if (!player->paused)
		audio_stream_source_active(player->stream, true);
}

void
st_tracker_set_volume(struct st_tracker *player, unsigned volume)
{
	if (player != NULL && volume <= 64)
		atomic_store_explicit(&player->volume, volume, memory_order_relaxed);
}

unsigned
st_tracker_levels(const struct st_tracker *player, uint8_t levels[ST_TRACKER_METER_CHANNELS])
{
	if (!player)
		return 0;
	for (unsigned i = 0; i < player->channels; ++i)
		levels[i] = player->paused ? 0 : atomic_load_explicit(&player->levels[i], memory_order_relaxed);
	return player->channels;
}
