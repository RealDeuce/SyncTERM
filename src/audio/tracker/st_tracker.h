/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_TRACKER_H_
#define ST_TRACKER_H_

#include <stdio.h>

#include "st_audio_runtime.h"

struct st_tracker;
/* The caller owns the file and can close it as soon as this call returns.
 * One owner thread controls the player; the audio engine clocks playback. */
struct st_tracker *st_tracker_play(st_audio_runtime_t *, FILE *, unsigned volume);
void               st_tracker_destroy(struct st_tracker *);
void               st_tracker_toggle_pause(struct st_tracker *);
void               st_tracker_step_order(struct st_tracker *, int direction);
void               st_tracker_set_volume(struct st_tracker *, unsigned volume);
#endif
