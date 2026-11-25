/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_ROUNDED_RECTANGLE_H_
#define ST_RIP3_ROUNDED_RECTANGLE_H_

#include "st_rip3_fill.h"

#include <stdint.h>

/* Resolved device geometry, relative to the current viewport. The constructor
 * normalizes opposing corners. Coordinates are finite within +/-1024*INT32_MAX;
 * radius is nonnegative and no larger than half the smaller dimension.
 * Oversized radii reject here; U/u perform wire conversion and independent
 * semiaxis fitting in their separate command path.
 * A zero radius is a rectangle; a zero dimension has no filled interior. */
struct st_rip3_rounded_rectangle {
	double x0, y0, x1, y1, radius;
};

int st_rip3_rounded_rectangle_set(struct st_rip3_rounded_rectangle *result,
	double x0, double y0, double x1, double y1, double radius);
/* Solid geometric outline, with round-capped segments and quarter circles.
 * Thickness is an explicit positive device-pixel count. Shared joins are
 * applied once. U/u separately resolve logical pens and their pass policy. */
int st_rip3_draw_rounded_rectangle(struct st_rip3_ports *ports,
	const struct st_rip3_rounded_rectangle *rectangle,
	uint32_t thickness, uint32_t color, enum st_rip3_write_mode mode);
/* Pixel-center fills: straight bounds are left/top inclusive, right/bottom
 * exclusive; circular corner boundary ties are excluded. Pattern phase is
 * explicit in fill. Border zero disables it; otherwise a solid COPY border
 * follows the fill. All border allocations finish before any pixels change.
 * Invalid input/allocation failures preserve pixels. These service statuses
 * belong to parser-owned recovery, never the terminal/session error path. */
int st_rip3_draw_filled_rounded_rectangle(struct st_rip3_ports *ports,
	const struct st_rip3_rounded_rectangle *rectangle,
	const struct st_rip3_fill *fill, uint32_t foreground, uint32_t background,
	uint32_t border_thickness, uint32_t border_color, enum st_rip3_write_mode mode);

#endif
