/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_AUDIO_H_
#define ST_RIP3_AUDIO_H_

#include "audio.h"
#include "st_rip3_resources.h"
#include "st_rip3_tones.h"

#include <stdbool.h>

struct st_rip3_audio;

/* One session thread owns create/play/stop/destroy. The client owns the engine
 * and must destroy this player first. Its existing mixer consumer pulls the
 * player's exclusive stream; no new worker or physical device is opened here.
 * The player's mutex protects PCM ownership and cursor against that consumer.
 * Source activation never occurs while holding the player's mutex. */
int st_rip3_audio_create(struct st_rip3_audio **result, audio_engine_t *engine,
	float volume_left_db, float volume_right_db);
void st_rip3_audio_destroy(struct st_rip3_audio *player);
/* Read-only MEDIA lookup and PCM WAVE validation precede decoding through the
 * shared audio-file service. Success replaces the prior file in one channel;
 * missing files are successful no-ops. Invalid/unavailable files do not replace
 * current PCM. A repeating file wraps its PCM cursor until stopped or replaced.
 * Literal $OFF$ stops without requiring paths. Paths are borrowed for this
 * call only; filenames are bare UTF-8 resource names.
 *
 * This is a storage/playback service, not a RIP input-processing entry. The
 * enclosing parser must consume all statuses nonfatally. Framing, macro/reset
 * dispatch, master audio policy and IFS readiness belong to that owner. */
int st_rip3_audio_play(struct st_rip3_audio *player,
	const struct st_rip3_resource_paths *paths, const char *filename, bool repeat);
int st_rip3_audio_stop(struct st_rip3_audio *player);

/* A separate mixer stream consumes the finite sequence on demand. The
 * session can poll completion without blocking or touching WAVE playback. */
bool st_rip3_audio_play_tones(struct st_rip3_audio *, const struct st_rip3_tones *);
bool st_rip3_audio_tones_pending(const struct st_rip3_audio *);
void st_rip3_audio_stop_tones(struct st_rip3_audio *);

#endif
