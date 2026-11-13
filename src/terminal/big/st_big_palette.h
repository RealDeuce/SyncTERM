/* Original X8 palette operators; protocol waits stay with the terminal. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BIG_PALETTE_H_
#define ST_BIG_PALETTE_H_
#include "st_big_state.h"
#include <stdint.h>
struct st_big_palette_rotation {
	uint16_t first,last,count,delay,index,saved;
};
/* After four class0 reads: optional exact1/1/1/1 capture with CBW saved
 * WORDs, then signed endpoint clamps to15. Ordinary entry does not refresh
 * the existing local. index/saved are retained until their reached stores. */
int st_big_palette_rotation_begin(struct st_big_palette_rotation *, struct st_big_state *,
	uint16_t first, uint16_t last, uint16_t count, uint16_t delay);
/* One complete outer turn, both independent direction tests. Each inclusive
 * endpoint setter is followed by a cache snapshot, including silent public
 * rejection. The final wrap setter has no snapshot. No waits/flow/keys or
 * work-budget yields occur here. Native errors retain accepted prefixes.
 * Callbacks are serialized and may change owner fields, never reenter. */
int st_big_palette_rotation_turn(struct st_big_palette_rotation *, struct st_big_state *,
	const struct st_big_state_sink *);
/* Ascending0..15 setters read each current saved WORD separately, with no
 * local refresh or caller S gates. Stops at the first backend error. */
int st_big_palette_rotation_restore(struct st_big_palette_rotation *, struct st_big_state *,
	const struct st_big_state_sink *);
#endif
