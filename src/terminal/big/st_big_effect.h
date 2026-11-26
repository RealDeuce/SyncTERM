/* Original BIG b speaker schedules and their native sample-domain player. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BIG_EFFECT_H_
#define ST_BIG_EFFECT_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "st_big_speaker.h"

enum st_big_effect_operation {
	ST_BIG_EFFECT_FREQUENCY,
	ST_BIG_EFFECT_STOP,
	ST_BIG_EFFECT_DELAY
};

struct st_big_effect_event {
	enum st_big_effect_operation operation;
	uint16_t                     value;
};

struct st_big_effect {
	uint32_t position;
	uint16_t selector;
	bool     enabled;
};

uint16_t st_big_effect_selector(uint16_t word, bool registration_equal);
int      st_big_effect_begin(struct st_big_effect *, uint16_t selector, bool enabled);
/* Exactly one original F/Z/D per returning event; no synthesized holds or
 * clocks here. RNG is shared caller state, advanced only by reached effect7
 * F events. A direct invalid enabled library request emits entry Z only. */
int st_big_effect_next(struct st_big_effect *, uint32_t *random_state, struct st_big_effect_event *,
    bool *complete);

struct st_big_effect_player {
	struct st_big_effect sequence;
	uint32_t             random_state;
	uint64_t             remaining;
	unsigned             fraction; /* Carried fractional sample numerator, denominator1000. */
	bool                 active;
};

int st_big_effect_player_begin(struct st_big_effect_player *, uint16_t selector, bool enabled,
    uint32_t random_state);
/* Native user-selected profile: each F request occupies one44.1-kHz sample,
 * including threshold no-ops; D WORD milliseconds carries fractional samples.
 * No CPU/bus/BIOS clocks, heap, devices or normalization are owned here.
 * Zero frames prepares the next boundary, including entry Z/first F or D.
 * Completion leaves final Z's ordinary speaker filter tail to the caller. */
int st_big_effect_render(struct st_big_effect_player *, struct st_big_speaker *, int16_t *, size_t frames);
int st_big_effect_skip(struct st_big_effect_player *, struct st_big_speaker *, uint64_t frames);
#endif
