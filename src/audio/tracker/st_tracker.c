/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include <libopenmpt/libopenmpt.h>
#include <libopenmpt/libopenmpt_stream_callbacks_file.h>

#include "st_tracker.h"

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
	size_t count  = openmpt_module_read_interleaved_stereo(player->module, AUDIO_SAMPLE_RATE, frames, pcm);
	int    volume = (int)atomic_load_explicit(&player->volume, memory_order_relaxed);
	/* The decoder belongs to the mixer while running. Each track meter
	 * is independent and can be sampled by the session between blocks. */
	for (unsigned i = 0; i < player->channels; ++i) {
		float    vu    = openmpt_module_get_current_channel_vu_mono(player->module, (int32_t)i);
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
		openmpt_module_destroy(player->module);
	free(player);
}

struct st_tracker *
st_tracker_play(st_audio_runtime_t *audio, FILE *file, unsigned volume)
{
	if (file == NULL || volume > 64 || (!st_audio_runtime_is_pull(audio) && !st_audio_runtime_allowed(audio)))
		return NULL;
	struct st_tracker *player = calloc(1, sizeof(*player));
	if (player == NULL)
		return NULL;
	atomic_init(&player->volume, volume);
	static const openmpt_module_initial_ctl controls[] = {
	    {"load.skip_plugins", "1" },
            {NULL,                NULL}
        };
	player->module = openmpt_module_create2(openmpt_stream_get_file_callbacks2(), file,
	    openmpt_log_func_silent, NULL, NULL, NULL, NULL, NULL, controls);
	if (player->module != NULL) {
		const char *type      = openmpt_module_get_metadata(player->module, "type");
		bool        supported = type != NULL && (strcmp(type, "mod") == 0 || strcmp(type, "s3m") == 0);
		openmpt_free_string(type);
		int32_t channels = openmpt_module_get_num_channels(player->module);
		if (channels > 0)
			player->channels =
			    channels > ST_TRACKER_METER_CHANNELS ? ST_TRACKER_METER_CHANNELS : (unsigned)channels;
		for (unsigned i = 0; i < player->channels; ++i)
			atomic_init(&player->levels[i], 0);
		audio_engine_t *engine;
		if (supported && openmpt_module_set_repeat_count(player->module, -1)
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

void
st_tracker_step_order(struct st_tracker *player, int direction)
{
	if (player == NULL || (direction != -1 && direction != 1))
		return;
	/* Quiesce the mixer's read callback before touching the decoder. */
	if (!player->paused && audio_stream_source_active(player->stream, false) != 0)
		return;
	int order = openmpt_module_get_current_order(player->module) + direction;
	if (order >= openmpt_module_get_num_orders(player->module))
		order = 0;
	openmpt_module_set_position_order_row(player->module, order < 0 ? 0 : order, 0);
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
