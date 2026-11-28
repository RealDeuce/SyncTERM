/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_tracker.h"

bool
st_tracker_available(void)
{
	return false;
}

struct st_tracker *
st_tracker_open(FILE *file)
{
	(void)file;
	return NULL;
}

size_t
st_tracker_read(struct st_tracker *player, int16_t *pcm, size_t frames)
{
	(void)player;
	(void)pcm;
	(void)frames;
	return 0;
}

struct st_tracker *
st_tracker_play(st_audio_runtime_t *audio, FILE *file, unsigned volume)
{
	(void)audio;
	(void)file;
	(void)volume;
	return NULL;
}

void
st_tracker_destroy(struct st_tracker *player)
{
	(void)player;
}

void
st_tracker_toggle_pause(struct st_tracker *player)
{
	(void)player;
}

void
st_tracker_step_order(struct st_tracker *player, int direction)
{
	(void)player;
	(void)direction;
}

void
st_tracker_set_volume(struct st_tracker *player, unsigned volume)
{
	(void)player;
	(void)volume;
}

unsigned
st_tracker_levels(const struct st_tracker *player, uint8_t levels[ST_TRACKER_METER_CHANNELS])
{
	(void)player;
	(void)levels;
	return 0;
}
