/* BIG original point/line/marker adapters around shared thin BGI stepping. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BIG_DRAW_H_
#define ST_BIG_DRAW_H_
#include "st_big_state.h"
struct st_big_draw_sink {
	void *context;
	/* Accepted physical pixels, logical pen0..15; serialized callbacks.
	 * Callbacks are synchronous (IN_PROGRESS is a protocol error).
	 * Failure stops subsequent writes and point tails without rollback. */
	int (*pixel)(void *, uint16_t x, uint16_t y, uint8_t pen, bool xor_mode);
};
/* Original fixed-slope WORD clip. Rejection sets all endpointsffff.
 * Signed divide faults/cycles return OUT_OF_RANGE rather than trapping/hanging. */
int st_big_draw_clip(const uint16_t bounds[4], uint16_t points[4], bool *visible);
int st_big_draw_style(struct st_big_state *, uint16_t style, uint16_t pattern, uint16_t width);
int st_big_draw_color(struct st_big_state *, uint16_t);
int st_big_draw_move(struct st_big_state *, uint16_t x, uint16_t y);
int st_big_draw_point(struct st_big_state *, uint16_t x, uint16_t y, uint16_t color, const struct st_big_draw_sink *);
int st_big_draw_line(struct st_big_state *, const uint16_t points[4], const struct st_big_draw_sink *);
int st_big_draw_to(struct st_big_state *, uint16_t x, uint16_t y, const struct st_big_draw_sink *);
int st_big_draw_relative(struct st_big_state *, uint16_t dx, uint16_t dy, const struct st_big_draw_sink *);
/* Direct helper: type0 draws nothing; crosshair ends at its right ray tip.
 * P's standalone dot/tail and input's XOR/restore belong to those callers. */
int st_big_draw_marker(struct st_big_state *, uint16_t x, uint16_t y, const struct st_big_draw_sink *);
#endif
