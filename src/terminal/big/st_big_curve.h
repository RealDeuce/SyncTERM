/* BIG curve scalar adapters; complete fronts/recurrence remain separate. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BIG_CURVE_H_
#define ST_BIG_CURVE_H_
#include <stdint.h>
int32_t st_big_curve_sin(uint16_t angle);
int32_t st_big_curve_cos(uint16_t angle);
uint16_t st_big_curve_metric(uint16_t x, uint16_t y);
/* Radius is unsigned; aspect words are signed. Zero denominator is a checked
 * error and leaves the output unchanged. No graphics positioning occurs. */
int st_big_curve_aspect(uint16_t radius, uint16_t aspect_x, uint16_t aspect_y, uint16_t *radius_y);
struct st_big_curve_decimal_frame {
	uint16_t center[2], radius[2], start[2], end[2];
};
struct st_big_curve_point_sink {
	void *context;
	/* Synchronous physical WORD requests, retaining order and duplicates.
	 * The adapter owns current pen/producer/point/batch effects. */
	int (*point)(void *, uint16_t x, uint16_t y);
};
/* Outline arm of DDO0d70 after batch entry. Limits use original endpoint
 * offsets before replacing zero radii. Captured geometry survives callbacks.
 * Checked32 overflow is a normal empty return; errors keep prior requests.
 * Batch exit, public/DDO/device point gates and arc publication are callers. */
int st_big_curve_decimal_outline(struct st_big_curve_decimal_frame,
	const struct st_big_curve_point_sink *);
#endif
