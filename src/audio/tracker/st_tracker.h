/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_TRACKER_H_
#define ST_TRACKER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "st_audio_runtime.h"

struct st_tracker;

bool st_tracker_available(void);
/* A one-shot decoder for any module recognized by libopenmpt. The caller
 * retains the FILE and may close it after open. Read returns stereo frames,
 * or zero at end of song. One thread owns the decoder through destruction. */
struct st_tracker *st_tracker_open(FILE *file);
size_t             st_tracker_read(struct st_tracker *player, int16_t *pcm, size_t frames);

enum {
	ST_TRACKER_METER_CHANNELS = 16
};

/* Levels for the first pattern channels, normalized to 0..255. */
unsigned st_tracker_levels(const struct st_tracker *, uint8_t levels[ST_TRACKER_METER_CHANNELS]);
/* The caller owns the file and can close it as soon as this call returns.
 * One owner thread controls the player; the audio engine clocks playback. */
struct st_tracker *st_tracker_play(st_audio_runtime_t *, FILE *, unsigned volume);
void               st_tracker_destroy(struct st_tracker *);
void               st_tracker_toggle_pause(struct st_tracker *);
void               st_tracker_step_order(struct st_tracker *, int direction);
void               st_tracker_set_volume(struct st_tracker *, unsigned volume);
#endif
