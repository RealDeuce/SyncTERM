/* BIG N's native buffer, source and resumable command owner. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef CTERM_BIG_MUSIC_H_
#define CTERM_BIG_MUSIC_H_
#include "st_big_music.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct cterm_big;
struct cterm_big_music_audio;
enum cterm_big_music_phase { CTERM_BIG_MUSIC_OFF, CTERM_BIG_MUSIC_SELECT,
	CTERM_BIG_MUSIC_GATE, CTERM_BIG_MUSIC_NUMBER, CTERM_BIG_MUSIC_RAW_START,
	CTERM_BIG_MUSIC_RAW_READ, CTERM_BIG_MUSIC_ENABLE, CTERM_BIG_MUSIC_START,
	CTERM_BIG_MUSIC_END_HIGH, CTERM_BIG_MUSIC_END_LOW, CTERM_BIG_MUSIC_HEADER,
	CTERM_BIG_MUSIC_POSITION_HIGH, CTERM_BIG_MUSIC_POSITION_LOW,
	CTERM_BIG_MUSIC_RUNNING, CTERM_BIG_MUSIC_DELAY, CTERM_BIG_MUSIC_FEATURE,
	CTERM_BIG_MUSIC_ARM, CTERM_BIG_MUSIC_RESTORE, CTERM_BIG_MUSIC_WARM,
	CTERM_BIG_MUSIC_CLOCK, CTERM_BIG_MUSIC_DONE };
struct cterm_big_music_command {
	enum cterm_big_music_phase phase;
	uint16_t mode, whole, from, to, saved_enable;
	unsigned index, warm;
	uint64_t deadline, sample;
	bool chained, number_started, wire, sampled, warm_begun;
};
void cterm_big_music_setup(struct cterm_big *);
void cterm_big_music_begin(struct cterm_big *, bool chained);
int cterm_big_music_command_poll(struct cterm_big *, const uint8_t *, size_t, size_t *, size_t *, bool *);
int cterm_big_music_poll(struct cterm_big *);
int cterm_big_music_clock(struct cterm_big *, bool restore);
void cterm_big_music_reset(struct cterm_big *);
void cterm_big_music_destroy(struct cterm_big *);
void cterm_big_music_sync_begin(struct cterm_big *);
int cterm_big_music_sync_poll(struct cterm_big *);
int cterm_big_music_sync_set(struct cterm_big *, uint16_t);
int cterm_big_music_snapshot(const struct cterm_big *, struct st_big_music *);
/* Serialized fixture inspection, with no physical device or borrowed buffer. */
int cterm_big_music_read(struct cterm_big *, uint16_t offset, bool word, uint16_t *);
#endif
