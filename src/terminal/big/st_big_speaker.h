/* BIG b/n's shared native speaker oscillator and approved softened square. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BIG_SPEAKER_H_
#define ST_BIG_SPEAKER_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
	ST_BIG_SPEAKER_RATE = 44100
};

struct st_big_speaker {
	uint32_t phase; /* Cycle fraction in Q32; retained by running F updates. */
	uint64_t step;  /* Local frame advance; can exceed one whole cycle. */
	int64_t  low;   /* Carried Q16 amplitude in the one-pole low-pass. */
	uint16_t frequency, divisor;
	bool     playing;
};

/* Init clears oscillator/filter state. All calls use only the caller-owned,
 * serialized value; no device, heap, lock, thread or calibration is involved.
 * Source gain is the fixed0.279224085 measured in the device-free fixture. */
int st_big_speaker_init(struct st_big_speaker *);
/* Original F word rule:0..18 are complete no-ops. Other words use the
 * original integer frequency divisor. Return true only on a reached update.
 * Running updates retain phase; a gated restart starts a new high half. */
bool st_big_speaker_frequency(struct st_big_speaker *, uint16_t);
void st_big_speaker_stop(struct st_big_speaker *);
/* Stereo S16 at44.1kHz with one fixed calibrated source gain. Stream/channel
 * gain stays in the existing mixer. Stop renders the low-pass decay tail. */
int st_big_speaker_render(struct st_big_speaker *, int16_t *stereo, size_t count);
/* Muted local-time progression: preserve oscillator phase/gate/frequency,
 * discard acoustic filter history so it cannot replay after re-enable. */
void st_big_speaker_skip(struct st_big_speaker *, uint64_t count);
#endif
