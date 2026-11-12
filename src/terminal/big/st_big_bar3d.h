/* Original public bar3d and DDO fallback, BIG23/59. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BIG_BAR3D_H_
#define ST_BIG_BAR3D_H_
#include "st_big_producer.h"
#include <stdint.h>
/* Public unsigned corner sort and lower move, translated inset/ordered
 * strokes, current orientation/producer/tuple. Public CP is unchanged.
 * top uses its low BYTE. Native errors retain the reached prefix. */
int st_big_draw_bar3d(struct st_big_state *, const uint16_t points[4],
	uint16_t depth, uint16_t top, struct st_big_producer *, const struct st_big_producer_sink *);
#endif
