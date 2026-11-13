/* BIG X0 sampling around the existing marker/point adapters, refs22/58. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BIG_SPRAY_H_
#define ST_BIG_SPRAY_H_
#include "st_big_draw.h"
#include <stdint.h>
struct st_big_spray {
	uint16_t x,y,width,height,count,counter,dx,dy;
};
/* Counter0 follows five completed reads. Two shared draws per attempt,
 * signed range scaling/truncation, independent current type predicates and
 * no P-style tail move. No key/time/flow calls or periodic raster yields.
 * Serialized callbacks may change current owners, never reenter. Errors
 * retain draws/writes and stop before counter increment and later effects. */
int st_big_draw_spray(struct st_big_state *, struct st_big_spray *, uint32_t *random_state,
	const struct st_big_draw_sink *);
#endif
